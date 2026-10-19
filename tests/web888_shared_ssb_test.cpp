#include "TestSettingsProfile.h"
#include "models/SliceModel.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888Backend.h"
#include "core/backends/web888/Web888IqSplitter.h"
#include "core/backends/web888/Web888SsbAudio.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <functional>
#include <numbers>
#include <cstdio>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return __LINE__; } } while (false)

namespace AetherSDR {
class KiwiSdrSessionTest : public KiwiSdrClient {
public:
    explicit KiwiSdrSessionTest(QObject* parent, Streams streams = Streams::SoundAndWaterfall) : KiwiSdrClient(streams,
        Admission::IndependentReceiver, parent) {}
    void rate(int value) { handleTextMessage(StreamKind::Sound, QStringLiteral("MSG sample_rate=%1").arg(value)); }
    QString mode() const { return kiwiMode(); }
    double center() const { return m_trackedFrequencyMhz * 1e6; }
protected:
#ifdef HAVE_WEBSOCKETS
    void startStatusPreflight(const QUrl&) override {}
#endif
    void sendSoundCommand(const QString&) override {}
};
}
class Backend : public Web888Backend {
public:
    KiwiSdrSessionTest* wire{};
    int clients{};
    int soundClients{};
protected:
    KiwiSdrClient* createClient() override { ++clients; wire = new KiwiSdrSessionTest(this); return wire; }
    KiwiSdrClient* createSoundClient() override { ++clients; ++soundClients; wire = new KiwiSdrSessionTest(this, KiwiSdrClient::Streams::SoundOnly); return wire; }
};
bool waitFor(const std::function<bool()>& done)
{
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return done();
}
double amplitude(const QVector<float>& audio, double hz)
{
    std::complex<double> value{};
    for (qsizetype n = 1000; n < audio.size(); ++n) {
        value += double(audio[n]) * std::polar(1.0, -2 * std::numbers::pi * hz * double(n) / 12000);
    }
    return 2 * std::abs(value) / double(audio.size() - 1000);
}
int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("web888-shared-ssb"));
    CHECK(settings.isValid());
    QCoreApplication app(argc, argv);
    // One capture: USB station at +1 kHz, LSB station at +6.5 kHz (dial +8 kHz).
    Web888IqSplitter splitter;
    CHECK(splitter.configure({{0, 14200000, 300, 2800}, {1, 14208000, -2800, -300, true}}));
    QVector<float> usb, lsb;
    KiwiIqBlock block;
    block.sampleRateHz = 24000; block.centerHz = 14200000; block.generation = 1;
    block.discontinuity = false;
    block.samples.resize(512);
    for (int b = 0; b < 64; ++b) {
        block.sequence = b;
        for (int n = 0; n < 512; ++n) {
            const double t = double(b * 512 + n) / 24000;
            block.samples[n] = std::complex<float>(0.05 * std::polar(1.0, 2 * std::numbers::pi * 1000 * t)
                + 0.5 * std::polar(1.0, 2 * std::numbers::pi * 6500 * t));
        }
        QString error;
        for (const auto& audio : splitter.process(block, &error)) {
            (audio.sliceId == 0 ? usb : lsb) += audio.mono;
        }
        CHECK(error.isEmpty());
    }
    std::fprintf(stderr, "USB wanted=%g unwanted=%g LSB wanted=%g unwanted=%g\n",
        amplitude(usb, 1000), amplitude(usb, 1500), amplitude(lsb, 1500), amplitude(lsb, 1000));
    CHECK(amplitude(usb, 1000) > 0.045 && amplitude(usb, 1500) < 0.001);
    CHECK(amplitude(lsb, 1500) > 0.45 && amplitude(lsb, 1000) < 0.001);
    Web888SsbAudio weak, strong;
    const auto weakAudio = weak.level(usb);
    const auto strongAudio = strong.level(lsb);
    CHECK(amplitude(weakAudio, 1000) > 0.13 && amplitude(weakAudio, 1000) < 0.18);
    CHECK(amplitude(strongAudio, 1500) > 0.13 && amplitude(strongAudio, 1500) < 0.18);
    weak.setMode(QStringLiteral("off"));
    CHECK(weak.level({0.02f, -0.03f}) == QVector<float>({0.02f, -0.03f}));
    Web888SsbAudio fast, slow;
    fast.setMode(QStringLiteral("fast")); slow.setMode(QStringLiteral("slow"));
    fast.level({1.0f}); slow.level({1.0f});
    CHECK(fast.level(QVector<float>(12000, 0.001f)).last()
        > slow.level(QVector<float>(12000, 0.001f)).last() * 10);
#ifdef HAVE_WEBSOCKETS
    Backend backend;
    QMap<int, SliceDelta> states;
    QMap<int, int> audioCounts;
    int errors = 0;
    QObject::connect(&backend, &IRadioBackend::sliceChanged, &app,
        [&](int id, const SliceDelta& d) { states[id] = d; });
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &app,
        [&](int id, const PcmFrame& frame) { if (frame.legacyStereo24().size()) { ++audioCounts[id]; } });
    QObject::connect(&backend, &IRadioBackend::sliceLifecycleFailed, &app,
        [&](const QString&, int, const QString&) { ++errors; });
    RadioConnectRequest request; request.params.insert(QStringLiteral("web888.password"), QString{}); request.host = QStringLiteral("receiver.invalid");
    backend.connectRadio(request);
    emit backend.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    backend.setSliceFrequency(0, 14200000); backend.setSliceMode(0, QStringLiteral("USB"));
    auto* primary = backend.wire;
    primary->rate(12000);
    CHECK(backend.createSlice("0", 14300000));
    auto* child = backend.wire;
    CHECK(backend.clients == 2 && backend.soundClients == 1);
    CHECK(!states.contains(1)); // No phantom slice before server admission.
    emit child->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(states[1].panId == "0" && states[1].frequency == 14.3);
    CHECK(primary->mode() == "usb" && primary->receiverControls().agcEnabled);
    backend.setSliceFrequency(1, 21200000);
    backend.setSliceMode(1, "AM");
    backend.setSliceAgc(1, "fast", 0);
    CHECK(states[1].frequency == 21.2 && states[1].mode == "AM");
    CHECK(primary->center() == 14200000 && child->center() == 21200000);
    CHECK(states[0].agcMode == "med" && states[1].agcMode == "fast");
    QVector<float> samples(128, 0.1f);
    QByteArray pcm(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(float));
    emit primary->decodedAudioReady(pcm);
    emit child->decodedAudioReady(pcm);
    CHECK(audioCounts[0] == 1 && audioCounts[1] == 1);
    backend.setActiveSlice(1);
    CHECK(backend.removeSlice(1));
    emit child->decodedAudioReady(pcm); // retired transport cannot deliver
    CHECK(audioCounts[1] == 1 && states[0].active == true);
    CHECK(!backend.removeSlice(0));
    CHECK(backend.createSlice("0", 7100000));
    auto* refused = backend.wire;
    emit refused->stateChanged(KiwiSdrClient::State::Busy, "Full");
    CHECK(backend.isConnected() && errors == 1 && !states.contains(2));
    CHECK(backend.createPanadapter());
    auto* secondPan = backend.wire;
    emit secondPan->stateChanged(KiwiSdrClient::State::Connected, {});
    secondPan->rate(12000);
    CHECK(backend.createSlice("1", 28000000));
    emit backend.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(states[4].panId == "1");
    CHECK(backend.removePanadapter("1"));
    CHECK(!backend.removeSlice(4) && backend.isConnected());
    // All pending leases count, even before the server replies.
    for (int i = 0; i < 11; ++i) { CHECK(backend.createSlice("0", 7100000 + i * 1000)); }
    CHECK(!backend.createSlice("0", 7200000));
    CHECK(backend.createPanadapter()); // preserve the reserved waterfall slot
    CHECK(!backend.createSlice("0", 7200000));
    backend.disconnectRadio();
    const auto counts = audioCounts;
    emit primary->decodedAudioReady(pcm);
    QCoreApplication::processEvents();
    CHECK(audioCounts == counts && !backend.isConnected());
#endif
    qInfo() << "Splitter DSP and independent hardware listener admission, tuning and cleanup passed";
    return 0;
}
