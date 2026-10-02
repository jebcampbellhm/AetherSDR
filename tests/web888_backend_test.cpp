#include "TestSettingsProfile.h"
#include "SeamThreadAffinityProbe.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888Backend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <cstring>
#include <limits>
#include <memory>

using namespace AetherSDR;

#define CHECK(condition) do { if (!(condition)) { qCritical() << "line" << __LINE__ << #condition; return 1; } } while (false)

class OfflineClient final : public KiwiSdrClient {
public:
    explicit OfflineClient(QObject* parent)
        : KiwiSdrClient(Streams::SoundAndWaterfall, Admission::IndependentReceiver, parent) {}
#ifdef HAVE_WEBSOCKETS
protected:
    void startStatusPreflight(const QUrl&) override {} // no network, including HTTP
#endif
};

class OfflineBackend final : public Web888Backend {
public:
    OfflineClient* client{nullptr};
protected:
    KiwiSdrClient* createClient() override
    {
        client = new OfflineClient(this);
        return client;
    }
};

int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("web888-backend"));
    if (!settings.isValid()) {
        return 1;
    }
    QCoreApplication app(argc, argv);
    OfflineBackend backend;
    const auto caps = backend.capabilities();
    CHECK(!caps.canTransmit && !caps.canCreateSlices && !caps.hasDaxStreams);
    CHECK(!caps.hasCwTune && !caps.hasFmRepeaterOffset && !caps.cwTextSupportsLive);
    CHECK(!caps.hasAgcThreshold && !caps.dbmAxisIsCalibrated());
    CHECK(caps.maxSlices == 1 && caps.maxPanadapters == 1 && backend.ownsRxAudio());
    CHECK(caps.receiveModeControl && caps.receiveAudioControl);
    RadioConnectRequest request;
    request.host = QStringLiteral("receiver.invalid");
    request.port = 8073;
    test::SeamThreadAffinityProbe probe(&backend);
    test::attachAllSeamSignals(probe);
    int connects = 0;
    int disconnects = 0;
    int speakerCount = 0;
    int sliceCount = 0;
    int spectra = 0;
    int meterDefinitions = 0;
    int meterUpdates = 0;
    double meterValue = 0;
    SliceDelta slice;
    PcmFrame speakerFrame;
    PcmFrame sliceFrame;
    QObject::connect(&backend, &IRadioBackend::connected, &app, [&] { ++connects; });
    QObject::connect(&backend, &IRadioBackend::disconnected, &app, [&] { ++disconnects; });
    QObject::connect(&backend, &IRadioBackend::sliceChanged, &app,
                     [&](int id, const SliceDelta& delta) { if (id == 0) { slice = delta; } });
    QObject::connect(&backend, &IRadioBackend::audioFrameReady, &app,
                     [&](const PcmFrame& frame) { ++speakerCount; speakerFrame = frame; });
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &app,
                     [&](int, const PcmFrame& frame) { ++sliceCount; sliceFrame = frame; });
    QObject::connect(&backend, &IRadioBackend::meterDefined, &app,
                     [&](const MeterDef&) { ++meterDefinitions; });
    QObject::connect(&backend, &IRadioBackend::meterUpdate, &app,
                     [&](const QString& name, double value) {
        if (name == QStringLiteral("SLC0:LEVEL")) {
            ++meterUpdates;
            meterValue = value;
        }
    });
    double center = 0;
    double width = 0;
    QObject::connect(&backend, &IRadioBackend::panCenterBandwidthChanged, &app,
                     [&](const QString&, double c, double w) { center = c; width = w; });
    bool geometryBeforeFrame = false;
    QObject::connect(&backend, &IRadioBackend::spectrumFrameReady, &app,
                     [&](int, const QByteArray& bins) {
        ++spectra;
        geometryBeforeFrame = center == 7.0 && width == 2.0 && bins.size() == 8;
    });
    backend.connectRadio(request);
#ifndef HAVE_WEBSOCKETS
    CHECK(!backend.isConnected() && disconnects == 1);
    return 0;
#else
    CHECK(!backend.isConnected());
    CHECK(backend.client->receiverFamily() == KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
    const float samples[]{0.25f, -0.5f, 0.75f, -1.0f};
    const QByteArray pcm(reinterpret_cast<const char*>(samples), sizeof(samples));
    emit backend.client->decodedAudioReady(pcm);
    CHECK(speakerCount == 0 && sliceCount == 0); // pending auth/audio readiness
    emit backend.client->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(backend.isConnected() && connects == 1 && slice.frequency == 7.1);
    emit backend.client->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(connects == 1);
    backend.setSliceFrequency(0, 14200000);
    CHECK(slice.frequency == 14.2);
    backend.setSliceFrequency(1, 7000000);
    backend.setSliceFrequency(0, std::numeric_limits<double>::quiet_NaN());
    CHECK(slice.frequency == 14.2);
    backend.setSliceMode(0, QStringLiteral("USB"));
    CHECK(slice.mode == QStringLiteral("USB") && slice.filterLow == 300);
    backend.setSliceFilter(0, 200, 2400);
    CHECK(slice.filterLow == 200 && slice.filterHigh == 2400);
    backend.setSliceFilter(0, 2400, 200);
    backend.setSliceMode(0, QStringLiteral("IQ"));
    CHECK(slice.filterHigh == 2400 && slice.mode == QStringLiteral("USB"));
    backend.setSliceAgc(0, QStringLiteral("fast"), 100);
    CHECK(backend.client->receiverControls().agcDecayMs == KiwiSdrProtocol::kAgcFastDecayMs);
    CHECK(backend.client->receiverControls().agcThresholdDb == -100);
    backend.setSliceAudioGain(0, 50);
    emit backend.client->decodedAudioReady(pcm);
    CHECK(sliceFrame.stream().purpose == PcmPurpose::Slice && sliceFrame.stream().sliceId == 0);
    CHECK(speakerFrame.stream().purpose == PcmPurpose::Speaker);
    CHECK(sliceFrame.legacyStereo24() == pcm && speakerFrame.samples()[0] == 0.125f);
    backend.setSliceAudioMute(0, true);
    emit backend.client->decodedAudioReady(pcm);
    CHECK(sliceFrame.legacyStereo24() == pcm && speakerFrame.samples()[0] == 0.0f);
    CHECK(sliceCount == 2 && speakerCount == 2);
    QByteArray invalid = pcm;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(invalid.data(), &nan, sizeof(nan));
    emit backend.client->decodedAudioReady(invalid);
    emit backend.client->decodedAudioReady(QByteArray("bad"));
    CHECK(sliceCount == 2 && speakerCount == 2);
    backend.setPanBandwidth(QStringLiteral("0"), 10000);
    emit backend.client->waterfallRowReady(QStringLiteral("0"), {-90.0f, -70.0f}, 6.0, 8.0, 0);
    CHECK(spectra == 1 && geometryBeforeFrame && slice.frequency == 14.2);
    emit backend.client->waterfallRowReady(QStringLiteral("0"), {nan}, 6.0, 8.0, 0);
    CHECK(spectra == 1);
    KiwiSdrProtocol::MeterReading meter;
    emit backend.client->meterReadingReady(meter);
    CHECK(meterDefinitions == 0 && meterUpdates == 0);
    meter.valid = true;
    meter.hasDbm = true;
    meter.dbm = -93;
    emit backend.client->meterReadingReady(meter);
    emit backend.client->meterReadingReady(meter);
    CHECK(meterDefinitions == 1 && meterUpdates == 2 && meterValue == -93);
    bool completed = false;
    backend.setKeying(true, {}, TxCoordinator::Completion([&] { completed = true; }));
    CHECK(completed && !backend.capabilities().canTransmit);

    // Put an actual queued backend callback in flight before disconnect.
    auto* oldClient = backend.client;
    std::unique_ptr<QThread> producer(QThread::create([&] { emit oldClient->decodedAudioReady(pcm); }));
    producer->start();
    CHECK(producer->wait(5000));
    const auto oldSlice = sliceFrame;
    const auto oldSpeaker = speakerFrame;
    backend.disconnectRadio();
    CHECK(!oldSlice.current() && !oldSpeaker.current());
    CHECK(disconnects == 1 && !backend.isConnected());
    probe.resetDisconnectGate();
    backend.connectRadio(request);
    CHECK(backend.client->receiverControls().agcDecayMs == KiwiSdrProtocol::kAgcFastDecayMs);
    emit backend.client->stateChanged(KiwiSdrClient::State::Connected, {});
    QCoreApplication::processEvents();
    CHECK(sliceCount == 2 && speakerCount == 2); // obsolete queued PCM rejected
    emit backend.client->decodedAudioReady(pcm);
    CHECK(sliceCount == 3 && sliceFrame.stream().session != oldSlice.stream().session);
    backend.disconnectRadio();
    backend.disconnectRadio();
    CHECK(disconnects == 2);
    CHECK(probe.violations().isEmpty() && probe.afterDisconnect().isEmpty());

    // An in-flight clean slice callback can cancel before the speaker route.
    OfflineBackend audioCancelled;
    int leakedSpeakers = 0;
    QObject::connect(&audioCancelled, &IRadioBackend::sliceAudioFrameReady, &audioCancelled,
                     [&](int, const PcmFrame&) { audioCancelled.disconnectRadio(); });
    QObject::connect(&audioCancelled, &IRadioBackend::audioFrameReady, &app,
                     [&](const PcmFrame&) { ++leakedSpeakers; });
    audioCancelled.connectRadio(request);
    emit audioCancelled.client->stateChanged(KiwiSdrClient::State::Connected, {});
    emit audioCancelled.client->decodedAudioReady(pcm);
    CHECK(!audioCancelled.isConnected() && leakedSpeakers == 0);

    // A reconnect initiated by a disconnected observer wins over the older
    // connectRadio call that triggered teardown.
    OfflineBackend reentrant;
    reentrant.connectRadio(request);
    auto reconnect = QObject::connect(&reentrant, &IRadioBackend::disconnected, &app,
                                     [&] { reentrant.connectRadio(request); });
    reentrant.connectRadio(request);
    CHECK(reentrant.client != nullptr && reentrant.client->state() == KiwiSdrClient::State::Connecting);
    QObject::disconnect(reconnect);
    emit reentrant.client->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(reentrant.isConnected());
    reentrant.disconnectRadio();

    // Direct observer cancellation during connected must prevent slice creation.
    OfflineBackend cancelled;
    int cancelledSlices = 0;
    QObject::connect(&cancelled, &IRadioBackend::connected, &cancelled, &IRadioBackend::disconnectRadio);
    QObject::connect(&cancelled, &IRadioBackend::sliceChanged, &app,
                     [&](int, const SliceDelta&) { ++cancelledSlices; });
    cancelled.connectRadio(request);
    emit cancelled.client->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(!cancelled.isConnected() && cancelledSlices == 0);

    // Refusal is terminal, without connectionError-triggered retry or PCM.
    for (auto state : {KiwiSdrClient::State::Busy, KiwiSdrClient::State::Error,
                       KiwiSdrClient::State::Waiting, KiwiSdrClient::State::Camping}) {
        OfflineBackend refused;
        int warnings = 0;
        int errors = 0;
        int stops = 0;
        QObject::connect(&refused, &IRadioBackend::configurationWarning, &app, [&](const QString&) { ++warnings; });
        QObject::connect(&refused, &IRadioBackend::connectionError, &app, [&](const QString&) { ++errors; });
        QObject::connect(&refused, &IRadioBackend::disconnected, &app, [&] { ++stops; });
        refused.connectRadio(request);
        emit refused.client->stateChanged(state, QStringLiteral("refused"));
        CHECK(!refused.isConnected() && warnings == 0 && errors == 1 && stops == 1);
    }
    qInfo() << "Web-888 offline backend checks passed";
    return 0;
#endif
}
