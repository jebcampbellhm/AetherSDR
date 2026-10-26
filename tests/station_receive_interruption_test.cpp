#include "TestSettingsProfile.h"
#include "models/RadioStation.h"
#include "models/MonitorAudioRouter.h"
#include <QCoreApplication>
#include <QThread>
#include <cstdio>

using namespace AetherSDR;
// Normalized local recorder only. No network, device or protocol peer.
class Backend final : public IRadioBackend {
public:
    bool held = false;
    int keyCalls = 0;
    RadioCapabilities capabilities() const override { return {}; }
    bool ownsRxAudio() const override { return true; }
    bool isConnected() const override { return true; }
    void connectRadio(const RadioConnectRequest&) override {}
    void disconnectRadio() override {}
    void setSliceFrequency(int, double) override {}
    void setSliceMode(int, const QString&) override {}
    void setSliceFilter(int, int, int) override {}
    void setSliceAgc(int, const QString&, int) override {}
    void setPanCenter(const QString&, double, PanCenterIntent) override {}
    void setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&) override { ++keyCalls; }
    void invokeExtension(const QString&, const QString&, quint64, const QVariant&) override {}
    void setReceiveInterrupted(bool value) override { held = value; }
};
static Backend* install(RadioModel& model)
{
    auto owned = std::make_unique<Backend>();
    auto* backend = owned.get();
    model.setBackendForTest(std::move(owned), QStringLiteral("test"));
    return backend;
}
int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("station-receive-interruption"));
    if (!profile.isValid()) { return 1; }
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", text); failures += !ok;
    };
    RadioModel primary, replacement;
    auto* primaryBackend = install(primary);
    auto* replacementBackend = install(replacement);
    RadioStation station;
    const int web = station.addSession(QStringLiteral("Web"));
    auto& webModel = station.session(web)->radioModel();
    auto* webBackend = install(webModel);
    station.watchTransmitter(&primary);
    station.selectAudioSession(web);
    MonitorAudioRouter monitor;
    monitor.selectSource(web);
    QObject::connect(&station, &RadioStation::audioFrameReady, &monitor,
        [&](int id, quint64 revision, const PcmFrame& frame) {
            if (station.acceptsAudio(id, revision, frame)) { monitor.feed(id, frame); }
        });
    QVector<PcmFrame> speaker, slices, primaryFrames, monitorFrames;
    QObject::connect(&webModel, &RadioModel::rxDemodAudioReady, &app, [&](const auto& f) { speaker.append(f); });
    QObject::connect(&webModel, &RadioModel::backendSliceAudioFrameReady, &app, [&](int, const auto& f) { slices.append(f); });
    QObject::connect(&primary, &RadioModel::rxDemodAudioReady, &app, [&](const auto& f) { primaryFrames.append(f); });
    QObject::connect(&monitor, &MonitorAudioRouter::pcmReady, &app, [&](const auto& f) { monitorFrames.append(f); });
    PcmProducer audio, slice, primaryAudio;
    audio.start(); slice.start(PcmPurpose::Slice, 0); primaryAudio.start();
    const auto first = audio.produce({0.1f, 0.2f});
    emit webBackend->audioFrameReady(*first);
    emit webBackend->sliceAudioFrameReady(0, *slice.produce({0.3f, 0.4f}));
    emit primaryBackend->audioFrameReady(*primaryAudio.produce({0.5f, 0.6f}));
    check(speaker.size() == 1 && slices.size() == 1 && primaryFrames.size() == 1 && monitorFrames.size() == 1,
          "normal model paths deliver primary, selected receiver and slice PCM");
    if (speaker.isEmpty() || slices.isEmpty() || primaryFrames.isEmpty() || monitorFrames.isEmpty()) { return 1; }
    const auto convertedLease = monitorFrames[0].epochLease();
    emit primary.radioTransmitConfirmed(true);
    check(station.receiveInterrupted() && webBackend->held && primaryBackend->held,
          "confirmed primary TX interrupts all station receive paths locally");
    check(!speaker[0].current() && !slices[0].current() && !primaryFrames[0].current()
          && !convertedLease.current() && first->current(),
          "queued receive and converted speaker leases retire without stopping hardware producers");
    emit webBackend->audioFrameReady(*audio.produce({0.7f, 0.8f}));
    emit webBackend->sliceAudioFrameReady(0, *slice.produce({0.7f, 0.8f}));
    emit primary.radioTransmittingChanged(false);
    emit primary.connectionStateChanged(false);
    emit webModel.radioTransmitConfirmed(false);
    check(station.receiveInterrupted() && speaker.size() == 1 && slices.size() == 1,
          "optimistic unkey, link loss and another receiver cannot resume interrupted audio");
    const int late = station.addSession(QStringLiteral("late receiver"));
    auto* lateBackend = install(station.session(late)->radioModel());
    check(lateBackend->held, "new and replacement backends inherit the receive interruption");
    emit primary.radioTransmitConfirmed(false);
    const auto resumed = audio.produce({0.9f, 1.0f});
    emit webBackend->audioFrameReady(*resumed);
    emit webBackend->sliceAudioFrameReady(0, *slice.produce({0.9f, 1.0f}));
    check(!station.receiveInterrupted() && !lateBackend->held && speaker.size() == 2 && slices.size() == 2
          && speaker.back().discontinuity() && slices.back().discontinuity()
          && speaker.back().firstSample() == resumed->firstSample() && !convertedLease.current(),
          "confirmed RX resumes every stream with a fresh gap marker and original sample positions");
    const int deliveredBeforeQueue = speaker.size();
    auto pending = std::unique_ptr<QThread>(QThread::create([&] {
        emit webBackend->audioFrameReady(*audio.produce({0.1f, 0.2f}));
    }));
    pending->start(); pending->wait();
    emit primary.radioTransmitConfirmed(true);
    auto during = std::unique_ptr<QThread>(QThread::create([&] {
        emit webBackend->audioFrameReady(*audio.produce({0.2f, 0.3f}));
    }));
    during->start(); during->wait();
    emit primary.radioTransmitConfirmed(false);
    QCoreApplication::processEvents();
    check(speaker.size() == deliveredBeforeQueue,
          "PCM queued before or emitted during TX cannot leak through after confirmed RX");
    emit webBackend->audioFrameReady(*audio.produce({0.3f, 0.4f}));
    check(speaker.size() == deliveredBeforeQueue + 1 && speaker.back().discontinuity(),
          "fresh post-RX audio survives the thread-boundary fence");
    emit primary.radioTransmitConfirmed(true);
    auto worker = std::unique_ptr<QThread>(QThread::create([&] { emit primary.radioTransmitConfirmed(false); }));
    worker->start(); worker->wait(); // Queue the old observer's callback before replacement.
    station.watchTransmitter(&replacement);
    QCoreApplication::processEvents();
    check(station.receiveInterrupted() && replacementBackend->held && !primaryBackend->held,
          "queued old-transmitter RX cannot release a replacement observer");
    emit replacement.radioTransmitConfirmed(false);
    check(!station.receiveInterrupted() && !webBackend->held && !replacementBackend->held
          && primaryBackend->keyCalls == 0 && replacementBackend->keyCalls == 0 && webBackend->keyCalls == 0,
          "fresh replacement RX releases the hold without any keying call");
    auto disappearing = std::make_unique<RadioModel>();
    install(*disappearing);
    station.watchTransmitter(disappearing.get());
    emit disappearing->radioTransmitConfirmed(true);
    auto lastMessage = std::unique_ptr<QThread>(QThread::create([&] {
        emit disappearing->radioTransmitConfirmed(false);
    }));
    lastMessage->start(); lastMessage->wait();
    disappearing.reset();
    QCoreApplication::processEvents();
    check(station.receiveInterrupted() && webBackend->held,
          "queued RX from a destroyed primary cannot release the station hold");
    station.watchTransmitter(&replacement);
    emit replacement.radioTransmitConfirmed(false);
    check(!station.receiveInterrupted(), "a fresh observer can recover after primary destruction");
    return failures ? 1 : 0;
}
