#include "TestSettingsProfile.h"
#include "models/RadioStation.h"
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
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"web-host", "Operator-selected Web-888 IP", "ip"});
    parser.addOption({"nrsp-host", "Operator-selected nRSP-ST IP", "ip"});
    parser.addOption({"username", "nRSP username (password from keychain)", "user"});
    parser.addOption({"output", "Sanitized evidence output", "path"});
    parser.process(app);
    QHostAddress webAddress(parser.value("web-host")), nrspAddress(parser.value("nrsp-host"));
    if (webAddress.isNull() || nrspAddress.isNull() || parser.value("output").isEmpty()) { parser.showHelp(2); }
    using namespace AetherSDR;
    RadioStation station;
    const int web = station.addSession(QStringLiteral("Web-888"));
    const int nrsp = station.addSession(QStringLiteral("nRSP-ST"));
    struct Counts { int connects = 0; qint64 frames = 0; int spectra = 0; } counts[2];
    bool finishing = false, retired = false, started = false;
    qint64 webAtRetirement = 0;
    QString error;
    auto finish = [&] {
        if (finishing) { return; }
        finishing = true;
        QTimer::singleShot(0, &app, [&] {
        const bool success = error.isEmpty() && retired && counts[0].connects == 1
            && counts[1].connects == 1 && counts[0].frames > webAtRetirement + 1000
            && counts[1].frames > 1000 && counts[0].spectra > 3 && counts[1].spectra > 3;
        station.removeSession(nrsp);
        station.removeSession(web);
        QJsonObject result{{"success", success}, {"error", error},
            {"webAudioFrames", counts[0].frames}, {"nrspAudioFrames", counts[1].frames},
            {"webSpectrumFrames", counts[0].spectra}, {"nrspSpectrumFrames", counts[1].spectra},
            {"webFramesAfterNrspRemoval", counts[0].frames - webAtRetirement},
            {"pcAudioDeviceOpened", false}, {"sharedSettingsChanged", false}};
        const QByteArray json = QJsonDocument(result).toJson();
        QSaveFile file(parser.value("output"));
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
        std::fwrite(json.data(), 1, size_t(json.size()), stdout);
        app.exit(success && saved ? 0 : 1);
        });
    };
    for (int id : {web, nrsp}) {
        auto& model = station.session(id)->radioModel();
        QObject::connect(&model, &RadioModel::backendAudioFrameReady, &app,
            [&, id](const PcmFrame& frame) { if (frame.current()) { counts[id].frames += frame.frameCount(); } });
        QObject::connect(&model, &RadioModel::panFeedSpectrumReady, &app,
            [&, id] { ++counts[id].spectra; });
        QObject::connect(&model, &RadioModel::connectionError, &app,
            [&](const QString& reason) { if (!finishing) { error = reason; finish(); } });
        QObject::connect(&model, &RadioModel::connectionStateChanged, &app,
            [&, id](bool connected) {
                if (!connected) { return; }
                ++counts[id].connects;
                if (!started && counts[0].connects && counts[1].connects) {
                    started = true;
                    QTimer::singleShot(10000, &app, [&] {
                        if (finishing) { return; }
                        webAtRetirement = counts[0].frames;
                        retired = station.removeSession(nrsp);
                    });
                    QTimer::singleShot(15000, &app, finish);
                }
            });
    }
    RadioInfo a;
    a.family = QStringLiteral("web888"); a.address = webAddress; a.port = 8073;
    a.serial = QStringLiteral("web888:%1:8073").arg(webAddress.toString());
    a.model = QStringLiteral("Web-888"); a.maxSlices = 4; a.maxPanadapters = 2;
    RadioInfo b;
    b.family = QStringLiteral("nrsp"); b.address = nrspAddress; b.port = 50000;
    b.serial = QStringLiteral("nrsp:%1").arg(nrspAddress.toString());
    b.model = QStringLiteral("nRSP-ST"); b.maxSlices = 1; b.maxPanadapters = 1;
    QTimer::singleShot(0, &app, [&] {
        station.session(web)->radioModel().connectToRadio(a);
        if (!finishing) {
            station.session(nrsp)->radioModel().connectToRadioWithOptions(b,
                {{QStringLiteral("nrsp.username"), parser.value("username")}});
        }
    });
    QTimer::singleShot(40000, &app, [&] { if (!finishing) { error = QStringLiteral("Station probe deadline"); finish(); } });
    return app.exec();
}
