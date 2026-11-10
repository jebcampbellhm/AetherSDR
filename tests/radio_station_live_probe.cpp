#include "TestSettingsProfile.h"
#include "models/RadioStation.h"
#include "models/MonitorAudioRouter.h"
#include "core/RadioDiscovery.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <cstdio>

// Manual receive-only diagnostic: no AudioEngine and no shared radio controls.
int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("station-live"));
    if (!settings.isValid()) { return 2; }
    QCoreApplication app(argc, argv);
    // Evidence contains counters and fixed diagnostics only, never wire text.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"web-host", "Operator-selected Web-888 IP", "ip"});
    parser.addOption({"nrsp-host", "Operator-selected nRSP-ST IP", "ip"});
    parser.addOption({"username", "nRSP username (password from keychain)", "user"});
    parser.addOption({"output", "Sanitized evidence output", "path"});
    parser.addOption({"reconnect", "Reconnect one receiver while the other stays live (web or nrsp)", "receiver"});
    parser.process(app);
    QHostAddress webAddress(parser.value("web-host")), nrspAddress(parser.value("nrsp-host"));
    if (webAddress.isNull() || nrspAddress.isNull() || parser.value("output").isEmpty()) { parser.showHelp(2); }
    using namespace AetherSDR;
    RadioStation station;
    MonitorAudioRouter monitor;
    monitor.selectSource(-1);
    const int web = station.addSession(QStringLiteral("Web-888"));
    const int nrsp = station.addSession(QStringLiteral("nRSP-ST"));
    const QString reconnect = parser.value("reconnect");
    if (!reconnect.isEmpty() && reconnect != QStringLiteral("web") && reconnect != QStringLiteral("nrsp")) { parser.showHelp(2); }
    const int reconnectId = reconnect.isEmpty() ? -1 : reconnect == QStringLiteral("web") ? web : nrsp;
    RadioInfo a;
    a.family = QStringLiteral("web888"); a.address = webAddress; a.port = 8073;
    a.serial = QStringLiteral("web888:%1:8073").arg(webAddress.toString());
    a.model = QStringLiteral("Web-888"); a.maxSlices = 4; a.maxPanadapters = 2;
    RadioInfo b;
    b.family = QStringLiteral("nrsp"); b.address = nrspAddress; b.port = 50000;
    b.serial = QStringLiteral("nrsp:%1").arg(nrspAddress.toString());
    b.model = QStringLiteral("nRSP-ST"); b.maxSlices = 1; b.maxPanadapters = 1;
    const auto connectOne = [&](int id) {
        if (id == web) { station.session(id)->radioModel().connectToRadio(a); }
        else { station.session(id)->radioModel().connectToRadioWithOptions(b,
            {{QStringLiteral("nrsp.username"), parser.value("username")}}); }
    };
    struct Counts { int connects = 0; qint64 frames = 0; int spectra = 0; } counts[2];
    bool finishing = false, retired = false, started = false;
    qint64 webAtRetirement = 0;
    qint64 monitorFrames[2] = {};
    PcmFrame routedWeb, routedNrsp;
    bool switchRevoked = false, removalRevoked = false, offLeaked = false;
    bool reconnectIssued = false, reconnectConfirmed = false, disconnectRevoked = false;
    bool availabilityDropped = false, availabilityRestored = false, freshEpoch = false;
    bool firstReconnectedDiscontinuity = false;
    qint64 otherAtDisconnect = 0, otherFramesDuringReconnect = 0, reconnectedFrames = 0;
    PcmFrame beforeReconnect;
    QObject::connect(&station, &RadioStation::audioSelectionChanged, &monitor,
        [&](int id, quint64) { monitor.selectSource(id < 0 ? -1 : id + 1); });
    QObject::connect(&station, &RadioStation::sessionRemoved, &monitor,
        [&](int id) { monitor.setSourceAvailable(id + 1, false); });
    QObject::connect(&station, &RadioStation::audioFrameReady, &monitor,
        [&](int id, quint64 revision, const PcmFrame& frame) {
            if (station.acceptsAudio(id, revision, frame)) { monitor.feed(id + 1, frame); }
        });
    QObject::connect(&monitor, &MonitorAudioRouter::pcmReady, &app,
        [&](const PcmFrame& frame) {
            const int id = monitor.selectedSource() - 1;
            if (id != web && id != nrsp) { offLeaked = true; return; }
            monitorFrames[id] += frame.frameCount();
            if (id == web) { routedWeb = frame; } else { routedNrsp = frame; }
            if (reconnectIssued && id == reconnectId && frame.stream() != beforeReconnect.stream()) {
                if (!freshEpoch) { firstReconnectedDiscontinuity = frame.discontinuity(); }
                freshEpoch = true;
                reconnectedFrames += frame.frameCount();
            }
        });
    QString error;
    auto finish = [&] {
        if (finishing) { return; }
        finishing = true;
        QTimer::singleShot(0, &app, [&] {
        const bool reconnectPassed = reconnectId < 0 || (reconnectConfirmed && disconnectRevoked
            && availabilityDropped && availabilityRestored && freshEpoch && firstReconnectedDiscontinuity
            && reconnectedFrames > 1000 && otherFramesDuringReconnect > 1000 && !beforeReconnect.current());
        const bool success = error.isEmpty() && retired && reconnectPassed
            && counts[0].connects == (reconnectId == web ? 2 : 1)
            && counts[1].connects == (reconnectId == nrsp ? 2 : 1) && counts[0].frames > webAtRetirement + 1000
            && counts[1].frames > 1000 && counts[0].spectra > 3 && counts[1].spectra > 3
            && monitorFrames[web] > 1000 && monitorFrames[nrsp] > 1000
            && switchRevoked && removalRevoked && !offLeaked && monitor.selectedSource() == -1;
        station.removeSession(nrsp);
        station.removeSession(web);
        QJsonObject result{{"success", success}, {"error", error},
            {"webAudioFrames", counts[0].frames}, {"nrspAudioFrames", counts[1].frames},
            {"webSpectrumFrames", counts[0].spectra}, {"nrspSpectrumFrames", counts[1].spectra},
            {"webFramesAfterNrspRemoval", counts[0].frames - webAtRetirement},
            {"webSelectedMonitorFrames", monitorFrames[web]},
            {"nrspSelectedMonitorFrames", monitorFrames[nrsp]},
            {"switchRevokedOldMonitor", switchRevoked},
            {"removalRevokedOldMonitor", removalRevoked}, {"offLeakedAudio", offLeaked},
            {"reconnectRequested", reconnect}, {"reconnectConfirmed", reconnectConfirmed},
            {"disconnectRevokedOldMonitor", disconnectRevoked},
            {"selectedAvailabilityDropped", availabilityDropped}, {"selectedAvailabilityRestored", availabilityRestored},
            {"freshReconnectedEpoch", freshEpoch}, {"firstReconnectedDiscontinuity", firstReconnectedDiscontinuity},
            {"reconnectedMonitorFrames", reconnectedFrames}, {"otherFramesDuringReconnect", otherFramesDuringReconnect},
            {"pcAudioDeviceOpened", false}, {"sharedSettingsChanged", false}};
        const QByteArray json = QJsonDocument(result).toJson();
        QSaveFile file(parser.value("output"));
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
        std::fwrite(json.data(), 1, size_t(json.size()), stdout);
        app.exit(success && saved ? 0 : 1);
        });
    };
    const auto retireNrsp = [&] {
        if (finishing) { return; }
        webAtRetirement = counts[0].frames;
        const bool wasLive = routedNrsp.current();
        retired = station.removeSession(nrsp);
        removalRevoked = wasLive && !routedNrsp.current();
        QTimer::singleShot(5000, &app, finish);
    };
    for (int id : {web, nrsp}) {
        auto& model = station.session(id)->radioModel();
        QObject::connect(&model, &RadioModel::rxDemodAudioReady, &app,
            [&, id](const PcmFrame& frame) { if (frame.current()) { counts[id].frames += frame.frameCount(); } });
        QObject::connect(&model, &RadioModel::panFeedSpectrumReady, &app,
            [&, id] { ++counts[id].spectra; });
        QObject::connect(&model, &RadioModel::connectionError, &app,
            [&, id](const QString&) {
                if (!finishing) { error = QStringLiteral("Receiver %1 connection failed").arg(id); finish(); }
            });
        QObject::connect(&model, &RadioModel::connectionStateChanged, &app,
            [&, id](bool connected) {
                if (!station.session(id)) { return; }
                monitor.setSourceAvailable(id + 1, connected);
                if (reconnectIssued && id == reconnectId && !connected) {
                    availabilityDropped = !monitor.playbackAvailable();
                }
                if (!connected) { return; }
                ++counts[id].connects;
                if (reconnectIssued && id == reconnectId && counts[id].connects == 2) {
                    reconnectConfirmed = true;
                    availabilityRestored = monitor.playbackAvailable();
                    otherFramesDuringReconnect = counts[id == web ? nrsp : web].frames - otherAtDisconnect;
                    QTimer::singleShot(3000, &app, [&] { if (!finishing) { station.selectAudioSession(nrsp); } });
                    QTimer::singleShot(5000, &app, retireNrsp);
                }
                if (!started && counts[0].connects && counts[1].connects) {
                    started = true;
                    station.selectAudioSession(web);
                    QTimer::singleShot(5000, &app, [&] {
                        if (finishing) { return; }
                        const bool wasLive = routedWeb.current();
                        station.selectAudioSession(nrsp);
                        switchRevoked = wasLive && !routedWeb.current();
                    });
                    QTimer::singleShot(10000, &app, [&] {
                        if (finishing) { return; }
                        if (reconnectId < 0) { retireNrsp(); return; }
                        station.selectAudioSession(reconnectId);
                        QTimer::singleShot(1000, &app, [&] {
                            if (finishing) { return; }
                            beforeReconnect = reconnectId == web ? routedWeb : routedNrsp;
                            const bool wasLive = beforeReconnect.current();
                            reconnectIssued = true;
                            otherAtDisconnect = counts[reconnectId == web ? nrsp : web].frames;
                            station.session(reconnectId)->radioModel().disconnectFromRadio();
                            disconnectRevoked = wasLive && !beforeReconnect.current();
                            QTimer::singleShot(750, &app, [&] { if (!finishing) { connectOne(reconnectId); } });
                        });
                    });
                }
            });
    }
    QTimer::singleShot(0, &app, [&] {
        connectOne(web);
        if (!finishing) { connectOne(nrsp); }
    });
    QTimer::singleShot(reconnectId < 0 ? 40000 : 60000, &app,
        [&] { if (!finishing) { error = QStringLiteral("Station probe deadline"); finish(); } });
    return app.exec();
}
