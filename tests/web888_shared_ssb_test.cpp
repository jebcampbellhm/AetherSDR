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
    explicit KiwiSdrSessionTest(QObject* parent) : KiwiSdrClient(Streams::SoundAndWaterfall,
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
protected:
    KiwiSdrClient* createClient() override { ++clients; wire = new KiwiSdrSessionTest(this); return wire; }
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
    RadioConnectRequest request; request.host = QStringLiteral("receiver.invalid");
    backend.connectRadio(request);
    emit backend.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    backend.setSliceFrequency(0, 14200000); backend.setSliceMode(0, QStringLiteral("USB"));
    CHECK(!backend.createSlice("0", 14204000)); // no negotiated rate
    backend.wire->rate(12000);
    CHECK(!backend.createSlice("0", 14204000));
    backend.wire->rate(24000);
    CHECK(!backend.createSlice("0", 14300000));
    CHECK(backend.createSlice("0", 14204000));
    CHECK(backend.clients == 1 && backend.wire->mode() == "iq");
    CHECK(!backend.wire->receiverControls().agcEnabled);
    CHECK(states[1].panId == "0" && states[1].frequency == 14.204);
    CHECK(!backend.createSlice("0", 14205000)); // bounded to two per capture
    backend.setSliceMode(1, "LSB");
    backend.setSliceFrequency(1, 14208000);
    backend.setSliceFilter(1, -2400, -200);
    backend.setSliceAgc(1, "fast", 0);
    CHECK(states[0].agcMode == "med" && states[1].agcMode == "fast");
    CHECK(!backend.wire->receiverControls().agcEnabled && backend.wire->center() == 14200000);
    SliceModel desktop(1);
    desktop.applyChanges(states[1]);
    QObject::connect(&backend, &IRadioBackend::sliceChanged, &desktop,
        [&](int id, const SliceDelta& d) { if (id == 1) { desktop.applyChanges(d); } });
    QObject::connect(&desktop, &SliceModel::receiveTuneRequested, &backend,
        [&](const SliceTuneRequest& r) { backend.setSliceFrequency(1, r.frequencyHz); });
    QObject::connect(&desktop, &SliceModel::receiveFilterRequested, &backend,
        [&](const SliceFilterRequest& r) { backend.setSliceFilter(1, r.lowHz, r.highHz); });
    QObject::connect(&desktop, &SliceModel::modeChangeRequested, &backend,
        [&](const QString& mode) { backend.setSliceMode(1, mode); });
    QObject::connect(&desktop, &SliceModel::controlRequested, &backend,
        [&](const SliceControlRequest& r) { backend.requestSliceControl(1, r); });
    desktop.setActive(true);
    CHECK(states[1].active == true && states[0].active == false);
    desktop.setMode("AM");
    CHECK(desktop.mode() == "LSB");
    desktop.setFrequency(14.300);
    CHECK(desktop.frequency() == 14.208);
    desktop.setFilterWidth(-6000, 6000);
    CHECK(desktop.filterLow() == -2400 && desktop.filterHigh() == -200);
    backend.setSliceFrequency(1, 14300000);
    CHECK(states[1].frequency == 14.208);
    backend.setSliceMode(1, "AM");
    CHECK(states[1].mode == "LSB");
    backend.setSliceFilter(1, -6000, 6000);
    CHECK(states[1].filterLow == -2400);
    int submitted = 0;
    for (int n = 0; n < 12; ++n) {
        block.sequence = ++submitted;
        emit backend.wire->filteredIqReady(block);
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 20) { QCoreApplication::processEvents(); QThread::msleep(1); }
    }
    CHECK(waitFor([&] { return audioCounts[0] > 0 && audioCounts[1] > 0; }));
    const int oldCount = audioCounts[1];
    backend.setActiveSlice(1);
    CHECK(backend.removeSlice(1));
    CHECK(backend.wire->mode() == "usb" && backend.wire->receiverControls().agcEnabled);
    CHECK(!backend.removeSlice(0));
    CHECK(backend.createSlice("0", 14203000));
    CHECK(states.contains(2) && backend.clients == 1);
    auto* primaryWire = backend.wire;
    CHECK(backend.createPanadapter());
    emit backend.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    backend.wire->rate(24000);
    CHECK(backend.createSlice("1", 14078000));
    CHECK(backend.clients == 2 && states[4].panId == "1");
    CHECK(backend.removePanadapter("1"));
    CHECK(backend.isConnected() && !backend.removeSlice(4));
    backend.wire = primaryWire;
    for (int n = 0; n < 5; ++n) { block.sequence = ++submitted; emit backend.wire->filteredIqReady(block); }
    backend.disconnectRadio();
    const auto countsAtDisconnect = audioCounts;
    QCoreApplication::processEvents();
    CHECK(audioCounts == countsAtDisconnect);
    CHECK(audioCounts[1] == oldCount);
    CHECK(!backend.createSlice("0", 14203000));
    CHECK(errors >= 5);
    // Observer callbacks cannot recursively admit a second child or publish
    // creation/removal after the callback disconnects the session.
    Backend cancelled;
    cancelled.connectRadio(request);
    emit cancelled.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    cancelled.wire->rate(24000);
    cancelled.setSliceMode(0, "USB");
    bool recursiveAccepted = false;
    auto cancelCreation = QObject::connect(&cancelled, &IRadioBackend::sliceChanged, &app,
        [&](int id, const SliceDelta& d) {
            if (id == 0 && d.modeList && d.modeList->size() == 2) {
                recursiveAccepted = cancelled.createSlice("0", 7103000);
                cancelled.disconnectRadio();
            }
        });
    CHECK(!cancelled.createSlice("0", 7102000));
    CHECK(!recursiveAccepted && !cancelled.isConnected());
    QObject::disconnect(cancelCreation);
    cancelled.connectRadio(request);
    emit cancelled.wire->stateChanged(KiwiSdrClient::State::Connected, {});
    cancelled.wire->rate(24000);
    CHECK(cancelled.createSlice("0", 7102000));
    bool disconnected = false;
    int lateRemovals = 0;
    QObject::connect(&cancelled, &IRadioBackend::disconnected, &app, [&] { disconnected = true; });
    QObject::connect(&cancelled, &IRadioBackend::sliceRemoved, &app,
        [&](int) { if (disconnected) { ++lateRemovals; } });
    QObject::connect(&cancelled, &IRadioBackend::sliceChanged, &app,
        [&](int id, const SliceDelta& d) {
            if (id == 0 && d.modeList && d.modeList->size() == 4) { cancelled.disconnectRadio(); }
        });
    CHECK(cancelled.removeSlice(1));
    CHECK(disconnected && lateRemovals == 0);
#endif
    qInfo() << "Shared SSB isolation, independent AGC, admission, tuning and cleanup passed";
    return 0;
}
