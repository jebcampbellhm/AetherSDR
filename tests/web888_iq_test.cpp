#include "TestSettingsProfile.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888IqCapture.h"
#include "core/backends/web888/Web888CapturePlan.h"
#include "core/backends/web888/Web888IqProcessor.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <limits>
#include <numbers>
#include <cstdio>
#include <algorithm>
#include <functional>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)
constexpr double tau = 2 * std::numbers::pi;
bool waitFor(const std::function<bool()>& done)
{
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return done();
}

namespace AetherSDR {
class KiwiSdrSessionTest : public KiwiSdrClient {
public:
    KiwiSdrSessionTest(QObject* parent = nullptr)
        : KiwiSdrClient(Streams::SoundOnly, Admission::IndependentReceiver, SoundFormat::FilteredIq, parent)
    {
        // The direct packet tests skip connection setup; initialize the same
        // protocol observation state that connectToEndpoint initializes.
        resetCapabilityState();
    }
    void bytes(const QByteArray& b) { handleSoundFrame(b); }
    void rate(double r) { handleTextMessage(StreamKind::Sound, QStringLiteral("MSG sample_rate=%1").arg(r, 0, 'f', 6)); }
    QString mode() const { return kiwiMode(); }
    int low() const { return kiwiLowCutHz(); }
protected:
#ifdef HAVE_WEBSOCKETS
    void startStatusPreflight(const QUrl&) override {}
#endif
};
}

class Capture : public Web888IqCapture {
public:
    explicit Capture(Web888IqProcessor* processor = nullptr) : Web888IqCapture(processor, nullptr) {}
    KiwiSdrSessionTest* client{};
protected:
    KiwiSdrClient* createClient() override { client = new KiwiSdrSessionTest(this); return client; }
};

QByteArray packet(quint32 seq, int count, bool little = false)
{
    QByteArray b(20 + count * 4, '\0');
    b.replace(0, 3, "SND"); b[3] = char(0x08 | (little ? 0x80 : 0));
    qToLittleEndian(seq, reinterpret_cast<uchar*>(b.data() + 4));
    for (int n = 0; n < count; ++n) {
        auto* p = reinterpret_cast<uchar*>(b.data() + 20 + n * 4);
        if (little) { qToLittleEndian<qint16>(16384, p); qToLittleEndian<qint16>(-8192, p + 2); }
        else { qToBigEndian<qint16>(16384, p); qToBigEndian<qint16>(-8192, p + 2); }
    }
    return b;
}

double amplitude(const QVector<float>& audio, double hz)
{
    std::complex<double> sum{};
    const int skip = 1000;
    for (qsizetype n = skip; n < audio.size(); ++n)
        sum += double(audio[n]) * std::polar(1.0, -tau * hz * double(n) / 12000);
    return 2 * std::abs(sum) / double(audio.size() - skip);
}

int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("web888-iq"));
    CHECK(settings.isValid());
    QCoreApplication app(argc, argv);
    for (const int rate : {12000, 24000, 36000}) {
        const auto base = planWeb888Captures(web888DigitalPresets(), rate, 13, 1);
        const int expected = rate == 12000 ? 13 : rate == 24000 ? 12 : 10;
        CHECK(base.windows.size() == expected);
        CHECK(base.fitsCapacity == (expected + 1 <= 13));
        const auto alternate = planWeb888Captures(web888DigitalPresets(false, true), rate, 13, 1);
        CHECK(alternate.windows.size() == expected + (rate == 36000 ? 0 : 1));
        for (const auto& window : alternate.windows)
            for (const auto& p : window.presets) {
                CHECK(p.slice.dialHz + p.slice.lowHz >= window.centerHz - rate / 2 + 1000);
                CHECK(p.slice.dialHz + p.slice.highHz <= window.centerHz + rate / 2 - 1000);
            }
    }
    const auto all = planWeb888Captures(web888DigitalPresets(true, true), 36000, 13, 1);
    CHECK(all.windows.size() == 12 && all.fitsCapacity);
    const auto tooMany = planWeb888Captures(web888DigitalPresets(true), 36000, 13, 2);
    CHECK(tooMany.windows.size() == 12 && !tooMany.fitsCapacity && !tooMany.error.isEmpty());
    CHECK(!planWeb888Captures(web888DigitalPresets(), 0, 13).fitsCapacity);
    auto reversed = web888DigitalPresets(); std::reverse(reversed.begin(), reversed.end());
    CHECK(planWeb888Captures(reversed, 36000, 13).windows.size() == 10);
    for (const bool little : {false, true}) {
        auto wire = packet(0xffffffff, 20, little);
        for (int n = 0; n < 10; ++n) wire[10 + n] = char(0xf0 + n);
        const auto b = decodeKiwiIq(wire, 36000, 14085500);
        CHECK(b.samples.size() == 20 && b.sequence == 0xffffffff);
        CHECK(b.samples[0] == std::complex<float>(0.5f, -0.25f));
        for (int n = 0; n < 10; ++n) CHECK(b.timingHeader[n] == quint8(0xf0 + n));
    }
    const auto redacted = decodeKiwiIq(packet(0, 20), 36000, 14085500);
    CHECK(redacted.samples.size() == 20);
    CHECK(std::all_of(redacted.timingHeader.begin(), redacted.timingHeader.end(), [](quint8 b) { return b == 0; }));
    auto bad = packet(0, 20); bad[3] = 0x18;
    CHECK(decodeKiwiIq(bad, 36000, 14085500).samples.isEmpty());
    for (int size = 0; size < 24; ++size)
        CHECK(decodeKiwiIq(packet(0, 20).left(size), 36000, 14085500).samples.isEmpty());
    CHECK(decodeKiwiIq(packet(0, 4097), 36000, 14085500).samples.isEmpty());
    CHECK(decodeKiwiIq(packet(0, 20).chopped(1), 36000, 14085500).samples.isEmpty());
    CHECK(decodeKiwiIq(packet(0, 20), 0, 14085500).samples.isEmpty());

    KiwiSdrSessionTest transport;
    transport.setTrackedSlice(0, 14.0855, "IQ", -17500, 17500, {}, {}, 0);
    int iqCount = 0, pcmCount = 0; KiwiIqBlock latest;
    QObject::connect(&transport, &KiwiSdrClient::filteredIqReady, [&](const auto& b) { latest = b; ++iqCount; });
    QObject::connect(&transport, &KiwiSdrClient::decodedAudioReady, [&](const auto&) { ++pcmCount; });
    transport.bytes(packet(0, 100)); CHECK(iqCount == 0); // no invented rate
    transport.rate(36000.125); CHECK(transport.mode() == "iq" && transport.low() == -17500);
    transport.bytes(packet(0xffffffff, 100)); CHECK(latest.discontinuity && latest.sampleRateHz == 36000.125);
    transport.bytes(packet(0, 100)); CHECK(!latest.discontinuity);
    transport.bytes(packet(2, 100)); CHECK(latest.discontinuity);
    transport.bytes(bad); transport.bytes(packet(3, 100)); CHECK(latest.discontinuity);
    transport.rate(24000); transport.bytes(packet(4, 100)); CHECK(latest.discontinuity);
    CHECK(pcmCount == 0);
    CHECK(transport.telemetry().protocol.sound.mode == KiwiSdrProtocol::StreamMode::Iq);
    CHECK(!transport.telemetry().protocol.waterfall.requested);

    // Packetization cannot change samples. Exercise valid narrow captures at
    // both lower rates, including a frame boundary at every 97 input samples.
    for (const double rate : {12000.0, 24000.0}) {
        Web888IqSplitter whole, chunked;
        CHECK(whole.configure({{7, 10139800, 0, 3000}}));
        CHECK(chunked.configure({{7, 10139800, 0, 3000}}));
        KiwiIqBlock block; block.sampleRateHz = rate; block.centerHz = 10140000;
        for (int n = 0; n < 4096; ++n)
            block.samples.append(std::complex<float>(0.25 * std::polar(1.0, tau * 800 * n / rate)));
        const auto expected = whole.process(block);
        CHECK(expected.size() == 1 && amplitude(expected[0].mono, 1000) > 0.23);
        QVector<float> actual;
        for (int start = 0; start < 4096; start += 97) {
            auto part = block; part.samples = block.samples.mid(start, 97);
            part.sequence = start / 97; part.discontinuity = start == 0;
            for (const auto& out : chunked.process(part)) actual += out.mono;
        }
        CHECK(actual.size() == expected[0].mono.size());
        for (qsizetype n = 0; n < actual.size(); ++n)
            CHECK(std::abs(actual[n] - expected[0].mono[n]) < 1e-6);
    }

    // Two RF signals on opposite sides of center. Independent USB dial references
    // must recover 1000/1500 Hz and reject the other slice and an out-of-band tone.
    const QVector<Web888DigitalSlice> slices{{1, 14074000, 0, 3000}, {2, 14095600, 1400, 1600}};
    for (const double rate : {12000.0, 24000.0, 36000.0, 36000.125, 48000.0}) {
        Web888IqSplitter splitter;
        CHECK(splitter.configure(slices));
        QVector<float> a, b;
        int discontinuities = 0;
        quint64 nextA = 0, nextB = 0;
        quint64 offset = 0;
        for (int seq = 0; seq < 100; ++seq) {
            KiwiIqBlock block; block.sampleRateHz = rate; block.centerHz = 14085500;
            block.sequence = seq; block.generation = 3; block.discontinuity = seq == 0;
            const int count = 503 + seq % 11; // irregular framing exercises continuous phase
            for (int n = 0; n < count; ++n, ++offset) {
                const double t = offset / rate;
                const auto v = 0.25 * std::polar(1.0, tau * -10500 * t)
                    + 0.2 * std::polar(1.0, tau * 11600 * t)
                    + 0.2 * std::polar(1.0, tau * 14000 * t);
                block.samples.append(std::complex<float>(v));
            }
            QString error;
            const auto out = splitter.process(block, &error);
            if (rate < 26000) { CHECK(out.isEmpty() && !error.isEmpty()); continue; }
            CHECK(error.isEmpty());
            for (const auto& audio : out) {
                auto& next = audio.sliceId == 1 ? nextA : nextB;
                CHECK(audio.discontinuity || audio.firstSampleIndex == next);
                next = audio.firstSampleIndex + audio.mono.size();
                CHECK(std::abs(audio.filterDelaySeconds - 256 / rate) < 1e-12);
                discontinuities += audio.discontinuity;
                (audio.sliceId == 1 ? a : b) += audio.mono;
            }
        }
        if (rate < 26000) continue;
        CHECK(discontinuities == 2);
        CHECK(amplitude(a, 1000) > 0.22 && amplitude(a, 1000) < 0.27);
        CHECK(amplitude(b, 1500) > 0.18 && amplitude(b, 1500) < 0.22);
        CHECK(amplitude(a, 1500) < 0.002 && amplitude(b, 1000) < 0.002);
        CHECK(std::abs(a.size() - (double(offset) - 513) * 12000 / rate) < 3);
        KiwiIqBlock gap; gap.sampleRateHz = rate; gap.centerHz = 14085500;
        gap.samples.fill({0.1f, 0}, 1000); gap.sequence = 200; gap.discontinuity = false;
        const auto reset = splitter.process(gap);
        CHECK(reset.size() == 2 && reset[0].discontinuity && reset[1].discontinuity);
        gap.samples[0] = {std::numeric_limits<float>::quiet_NaN(), 0};
        CHECK(splitter.process(gap).isEmpty());
    }
    Web888IqSplitter invalid;
    CHECK(!invalid.configure({{1, 14074000}, {1, 14095600}}));
    CHECK(!invalid.configure({{1, 14074000, 3000, 1000}}));

#ifdef HAVE_WEBSOCKETS
    Capture capture;
    int delivered = 0, failed = 0;
    Web888DecoderAudio held;
    bool wasCurrent = false;
    QObject::connect(&capture, &Web888IqCapture::decoderAudioReady, [&](const auto& audio) {
        held = audio; wasCurrent = audio.current(); ++delivered; capture.stop();
    });
    QObject::connect(&capture, &Web888IqCapture::failed, [&](const auto&) { ++failed; });
    CHECK(capture.start("receiver.invalid:8073", 14085500, slices));
    QPointer<KiwiSdrSessionTest> oldClient = capture.client;
    oldClient->rate(36000); oldClient->bytes(packet(0, 1000));
    CHECK(waitFor([&] { return delivered == 1; }));
    CHECK(delivered == 1 && failed == 0); // reentrant stop fences the second slice
    CHECK(wasCurrent && !held.current()); // queued/retained output is revoked too
    if (oldClient) oldClient->bytes(packet(1, 1000));
    CHECK(delivered == 1);
    CHECK(capture.start("receiver.invalid:8073", 14085500, slices));
    capture.client->rate(12000); capture.client->bytes(packet(0, 1000));
    CHECK(waitFor([&] { return failed == 1; }));
    CHECK(failed == 1 && delivered == 1); // insufficient negotiated bandwidth
    auto processor = std::make_unique<Web888IqProcessor>();
    Capture shared(processor.get());
    int processorFailures = 0;
    QObject::connect(&shared, &Web888IqCapture::failed, [&](const QString&) { ++processorFailures; });
    CHECK(shared.start("receiver.invalid:8073", 14085500, slices));
    processor.reset();
    CHECK(processorFailures == 1 && !shared.running());
    // A silent admitted stream must revoke retained audio and release its lease.
    // Exercise the actual timer signal without spending ten seconds in this test.
    Capture stalled;
    int stallFailures = 0, stallOutputs = 0;
    Web888DecoderAudio retained;
    QObject::connect(&stalled, &Web888IqCapture::failed, [&](const QString& reason) {
        if (reason.contains("progress")) ++stallFailures;
    });
    QObject::connect(&stalled, &Web888IqCapture::decoderAudioReady, [&](const auto& out) {
        retained = out; ++stallOutputs;
    });
    CHECK(stalled.start("receiver.invalid:8073", 14085500, slices));
    auto* progress = stalled.findChild<QTimer*>("captureProgressDeadline");
    CHECK(progress && !progress->isActive() && progress->interval() == 10000);
    CHECK(QMetaObject::invokeMethod(progress, "timeout", Qt::DirectConnection));
    CHECK(stallFailures == 0 && stalled.running()); // admission has its own pool deadline
    stalled.client->rate(36000); stalled.client->bytes(packet(0, 1000));
    CHECK(waitFor([&] { return stallOutputs == 2; }));
    CHECK(progress->isActive() && retained.current());
    CHECK(QMetaObject::invokeMethod(progress, "timeout", Qt::DirectConnection));
    CHECK(stallFailures == 1 && !stalled.running() && !retained.current() && !progress->isActive());
    CHECK(QMetaObject::invokeMethod(progress, "timeout", Qt::DirectConnection));
    CHECK(stallFailures == 1);
    CHECK(stalled.start("receiver.invalid:8073", 14085500, slices));
    CHECK(!progress->isActive());
    stalled.client->rate(36000); stalled.client->bytes(packet(0, 1000));
    CHECK(waitFor([&] { return stallOutputs == 4; }));
    CHECK(progress->isActive() && retained.current());
    stalled.stop();
    CHECK(!progress->isActive() && !retained.current());
    // Repeated filter resets can validate IQ forever without producing any PCM.
    // Such empty completions must not keep an admitted capture alive indefinitely.
    Capture starved;
    int starvationFailures = 0, starvationReady = 0, starvationAudio = 0;
    QObject::connect(&starved, &Web888IqCapture::failed, [&](const QString&) { ++starvationFailures; });
    QObject::connect(&starved, &Web888IqCapture::ready, [&](double) { ++starvationReady; });
    QObject::connect(&starved, &Web888IqCapture::decoderAudioReady, [&](const auto&) { ++starvationAudio; });
    CHECK(starved.start("receiver.invalid:8073", 14085500, slices));
    auto* starvationDeadline = starved.findChild<QTimer*>("captureProgressDeadline");
    CHECK(starvationDeadline);
    starvationDeadline->setInterval(100); // production remains ten seconds
    starved.client->rate(36000); starved.client->bytes(packet(0, 100));
    CHECK(waitFor([&] { return starvationReady == 1; }));
    QTimer resets;
    quint32 resetSequence = 0;
    QObject::connect(&resets, &QTimer::timeout, [&] {
        if (starved.running()) starved.client->bytes(packet(resetSequence += 2, 100));
    });
    resets.start(10);
    const bool expired = waitFor([&] { return starvationFailures == 1; });
    resets.stop();
    CHECK(expired && !starved.running() && starvationAudio == 0);
    Capture neverStarted;
    int startupFailures = 0, startupAudio = 0;
    QObject::connect(&neverStarted, &Web888IqCapture::failed, [&](const QString& reason) {
        if (reason.contains("startup")) ++startupFailures;
    });
    QObject::connect(&neverStarted, &Web888IqCapture::decoderAudioReady, [&](const auto&) { ++startupAudio; });
    CHECK(neverStarted.start("receiver.invalid:8073", 14085500, slices));
    auto* startup = neverStarted.findChild<QTimer*>("captureStartupDeadline");
    CHECK(startup && startup->isActive() && startup->interval() == 30000);
    CHECK(QMetaObject::invokeMethod(startup, "timeout", Qt::DirectConnection));
    CHECK(startupFailures == 1 && !neverStarted.running() && !startup->isActive());
    CHECK(QMetaObject::invokeMethod(startup, "timeout", Qt::DirectConnection));
    CHECK(startupFailures == 1);
    CHECK(neverStarted.start("receiver.invalid:8073", 14085500, slices));
    CHECK(startup->isActive());
    neverStarted.client->rate(36000); neverStarted.client->bytes(packet(0, 1000));
    CHECK(waitFor([&] { return startupAudio == 2; }));
    CHECK(!startup->isActive());
    CHECK(QMetaObject::invokeMethod(startup, "timeout", Qt::DirectConnection));
    CHECK(startupFailures == 1 && neverStarted.running());
    neverStarted.stop();
    CHECK(!startup->isActive());
#endif
    qInfo() << "IQ wire, rate, sequence, channel separation and capture lifecycle passed";
    return 0;
}
