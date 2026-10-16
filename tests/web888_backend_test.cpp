#include "TestSettingsProfile.h"
#include "SeamThreadAffinityProbe.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888Backend.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/PanadapterModel.h"

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
    CHECK(!caps.canTransmit && caps.canCreateSlices && !caps.hasDaxStreams);
    CHECK(!caps.hasCwTune && !caps.hasFmRepeaterOffset && !caps.cwTextSupportsLive);
    CHECK(!caps.hasAgcThreshold && !caps.dbmAxisIsCalibrated());
    CHECK(caps.maxSlices == 4 && caps.maxPanadapters == 2 && backend.ownsRxAudio());
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
    CHECK(caps.extensionNamespaces.contains(QStringLiteral("digital-monitor")));
    QVariantMap monitorReply;
    QString monitorError;
    auto monitorResult = QObject::connect(&backend, &IRadioBackend::extensionResult, &app,
        [&](quint64 id, const QVariant& value) { if (id == 987) monitorReply = value.toMap(); });
    auto monitorFailure = QObject::connect(&backend, &IRadioBackend::extensionError, &app,
        [&](quint64 id, const QString& error) { if (id == 987) monitorError = error; });
    QVariantMap monitorOptions{{"bands", QStringList{"40m", "30m", "20m", "10m"}},
        {"modes", QStringList{"FT8", "WSPR"}}, {"rate", 24000}, {"budget", 13}};
    backend.invokeExtension("digital-monitor", "plan", 987, monitorOptions);
    CHECK(monitorError.isEmpty() && monitorReply.value("fits").toBool());
    CHECK(monitorReply.value("outputs").toInt() == 8 && monitorReply.value("reserved").toInt() == 2);
    CHECK(backend.digitalCaptureCount() == 0 && backend.pendingDigitalCaptureCount() == 0);
    monitorOptions["budget"] = 2;
    backend.invokeExtension("digital-monitor", "plan", 987, monitorOptions);
    CHECK(!monitorReply.value("fits").toBool());
    backend.invokeExtension("digital-monitor", "start", 987, monitorOptions);
    CHECK(!monitorError.isEmpty() && backend.pendingDigitalCaptureCount() == 0);
    monitorError.clear(); monitorOptions["rate"] = 36000;
    backend.invokeExtension("digital-monitor", "plan", 987, monitorOptions);
    CHECK(!monitorError.isEmpty());
    backend.invokeExtension("digital-monitor", "status", 987);
    CHECK(monitorReply.value("timing").toString().contains("not armed"));
    backend.invokeExtension("digital-monitor", "stop", 987);
    CHECK(monitorReply.value("stopped").toBool());
    QObject::disconnect(monitorResult); QObject::disconnect(monitorFailure);

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
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Gain, 50})
          == ReceiveDispatch::Dispatched);
    CHECK(backend.requestSliceAudio(1, {SliceAudioRequest::Field::Gain, 100})
          == ReceiveDispatch::Unsupported);
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Gain, 101})
          == ReceiveDispatch::Unsupported);
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Pan, 50})
          == ReceiveDispatch::Unsupported);
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Mute, 1,
          SliceAudioRequest::Origin::ExternalReceiveSuppression})
          == ReceiveDispatch::Unsupported);
    emit backend.client->decodedAudioReady(pcm);
    CHECK(sliceFrame.stream().purpose == PcmPurpose::Slice && sliceFrame.stream().sliceId == 0);
    CHECK(speakerFrame.stream().purpose == PcmPurpose::Speaker);
    CHECK(sliceFrame.legacyStereo24() == pcm && speakerFrame.samples()[0] == 0.125f);
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Mute, 1})
          == ReceiveDispatch::Dispatched);
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
    CHECK(backend.requestSliceAudio(0, {SliceAudioRequest::Field::Gain, 50})
          == ReceiveDispatch::Unsupported);
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
    // The second lease is independently admitted, routed and retired.
    OfflineBackend dual;
    QMap<int, SliceDelta> slices;
    QMap<int, PcmFrame> frames;
    QMap<int, int> rows;
    int failures = 0;
    int dualSpeakers = 0;
    QObject::connect(&dual, &IRadioBackend::sliceChanged, &app,
                     [&](int id, const SliceDelta& d) { slices[id] = d; });
    QObject::connect(&dual, &IRadioBackend::sliceAudioFrameReady, &app,
                     [&](int id, const PcmFrame& f) { frames[id] = f; });
    QObject::connect(&dual, &IRadioBackend::spectrumFrameReady, &app,
                     [&](int id, const QByteArray&) { ++rows[id]; });
    QObject::connect(&dual, &IRadioBackend::sliceLifecycleFailed, &app,
                     [&](const QString&, int, const QString&) { ++failures; });
    QObject::connect(&dual, &IRadioBackend::audioFrameReady, &app,
                     [&](const PcmFrame&) { ++dualSpeakers; });
    CHECK(!dual.createPanadapter());
    dual.connectRadio(request);
    auto* primary = dual.client;
    emit primary->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(dual.createPanadapter());
    auto* secondary = dual.client;
    CHECK(primary != secondary && !dual.createPanadapter()); // pending consumes capacity
    emit secondary->decodedAudioReady(pcm);
    CHECK(frames.isEmpty());
    emit secondary->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(slices[1].panId == QStringLiteral("1") && slices[1].frequency == 14.074);
    CHECK(slices[0].active == true && slices[1].active == false);
    dual.setSliceFrequency(1, 21074000);
    dual.setSliceAudioGain(1, 35);
    CHECK(slices[1].frequency == 21.074 && slices[0].frequency == 7.1);
    emit primary->decodedAudioReady(pcm);
    emit secondary->decodedAudioReady(pcm);
    CHECK(frames[0].current() && frames[1].current() && dualSpeakers == 1);
    const auto retainedPrimary = frames[0];
    const auto retiredSecondary = frames[1];
    dual.setActiveSlice(1);
    CHECK(slices[0].active == false && slices[1].active == true);
    emit primary->decodedAudioReady(pcm);
    CHECK(dualSpeakers == 1);
    emit secondary->decodedAudioReady(pcm);
    CHECK(dualSpeakers == 2);
    emit primary->waterfallRowReady({}, {-80.0f}, 7000000, 7200000, 0);
    emit secondary->waterfallRowReady({}, {-90.0f}, 21000000, 21200000, 0);
    CHECK(rows[0] == 1 && rows[1] == 1);
    CHECK(!dual.removePanadapter(QStringLiteral("01")));
    CHECK(!dual.removePanadapter(QStringLiteral("0"))); // primary anchors connection
    // A queued callback from the removed lease must not reach a replacement.
    std::unique_ptr<QThread> late(QThread::create([&] { emit secondary->decodedAudioReady(pcm); }));
    late->start();
    CHECK(late->wait(5000));
    CHECK(dual.removePanadapter(QStringLiteral("1")));
    CHECK(retainedPrimary.current() && !retiredSecondary.current());
    CHECK(dual.isConnected() && slices[0].active == true);
    CHECK(dual.createPanadapter());
    auto* replacement = dual.client;
    emit replacement->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(slices[2].panId == QStringLiteral("1")); // slice identity is distinct from display slot
    emit replacement->waterfallRowReady({}, {-95.0f}, 14000000, 14200000, 0);
    CHECK(rows[1] == 2 && rows[2] == 0);
    QCoreApplication::processEvents();
    CHECK(!frames.contains(2) && dualSpeakers == 2);
    emit replacement->stateChanged(KiwiSdrClient::State::Busy, QStringLiteral("full"));
    CHECK(dual.isConnected() && retainedPrimary.current() && failures == 1);
    emit primary->decodedAudioReady(pcm);
    CHECK(dualSpeakers == 3);
    CHECK(dual.createPanadapter());
    auto* pending = dual.client;
    CHECK(dual.removePanadapter(QStringLiteral("1")));
    emit pending->stateChanged(KiwiSdrClient::State::Connected, {});
    CHECK(!slices.contains(3) && dual.isConnected());
    dual.disconnectRadio();
    CHECK(!retainedPrimary.current());

    // Drive the full production model wiring with socket-free backend observations.
    RadioModel model;
    auto owned = std::make_unique<OfflineBackend>();
    auto* modeled = owned.get();
    CHECK(model.rebuildBackendForTest(QStringLiteral("web888")));
    auto* modelCarrier = model.backend();
    QObject::connect(modeled, &IRadioBackend::panCenterBandwidthChanged,
                     modelCarrier, &IRadioBackend::panCenterBandwidthChanged);
    QObject::connect(modeled, &IRadioBackend::sliceChanged, modelCarrier, &IRadioBackend::sliceChanged);
    QObject::connect(modeled, &IRadioBackend::sliceRemoved, modelCarrier, &IRadioBackend::sliceRemoved);
    QObject::connect(modeled, &IRadioBackend::panRemoved, modelCarrier, &IRadioBackend::panRemoved);
    QObject::connect(modeled, &IRadioBackend::spectrumFrameReady,
                     modelCarrier, &IRadioBackend::spectrumFrameReady);
    modeled->connectRadio(request);
    emit modeled->client->stateChanged(KiwiSdrClient::State::Connected, {});
    emit modeled->client->waterfallRowReady({}, {-80.0f}, 7.0, 7.2, 0);
    CHECK(modeled->createPanadapter());
    emit modeled->client->stateChanged(KiwiSdrClient::State::Connected, {});
    emit modeled->client->waterfallRowReady({}, {-90.0f}, 14.0, 14.2, 0);
    CHECK(model.panadapters().size() == 2 && model.slice(1));
    const QString secondModelPan = model.slice(1)->panId();
    CHECK(modeled->removePanadapter(QStringLiteral("1")));
    CHECK(model.panadapters().size() == 1 && !model.slice(1));
    CHECK(modeled->createPanadapter());
    emit modeled->client->stateChanged(KiwiSdrClient::State::Connected, {});
    emit modeled->client->waterfallRowReady({}, {-95.0f}, 21.0, 21.2, 0);
    CHECK(model.panadapters().size() == 2 && model.slice(2));
    CHECK(model.slice(2)->panId() == secondModelPan);
    CHECK(model.panadapter(secondModelPan)->centerMhz() == 21.1);
    bool replacementOpened = false;
    QObject::connect(modeled, &IRadioBackend::sliceRemoved, &app, [&](int id) {
        if (id != 2) { return; }
        replacementOpened = modeled->createPanadapter();
        if (replacementOpened) {
            emit modeled->client->stateChanged(KiwiSdrClient::State::Connected, {});
            emit modeled->client->waterfallRowReady({}, {-96.0f}, 28.0, 28.2, 0);
        }
    });
    CHECK(modeled->removePanadapter(QStringLiteral("1")));
    CHECK(replacementOpened && model.slice(3));
    CHECK(model.panadapters().size() == 2);
    CHECK(model.panadapter(model.slice(3)->panId()));
    modeled->disconnectRadio();

    qInfo() << "Web-888 offline backend checks passed";
    return 0;
#endif
}
