#include "core/backends/nrsp/NrspBackend.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

// Manual live-radio diagnostic. Not a default CTest and never a fake peer.
// This version changes no shared source or RF setting.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"host", "Operator-selected nRSP-ST hostname/IP (required).", "host"});
    parser.addOption({"seconds", "Receive duration after verification (1..3600).", "seconds", "15"});
    parser.addOption({"output", "JSON diagnostic output file (required).", "path"});
    parser.addOption({"username", "Optional streaming username; password comes from NRSP_TEST_PASSWORD.", "username"});
    parser.process(app);
    bool ok = false;
    const int seconds = parser.value("seconds").toInt(&ok);
    if (!ok || seconds < 1 || seconds > 3600 || parser.value("host").isEmpty() || parser.value("output").isEmpty()) {
        parser.showHelp(2);
    }
    AetherSDR::NrspBackend backend;
    QElapsedTimer clock;
    QJsonArray sourceUpdates;
    bool connected = false;
    bool finishing = false;
    qint64 audioFrames = 0;
    qint64 speakerPackets = 0;
    qint64 slicePackets = 0;
    qint64 displayFrames = 0;
    qint64 displayBins = 0;
    double sumSquares = 0;
    double peak = 0;
    QString failure;
    std::function<void()> finish = [&] {
        if (finishing) { return; }
        finishing = true;
        const qint64 elapsed = clock.isValid() ? clock.elapsed() : 0;
        backend.disconnectRadio();
        const bool success = failure.isEmpty() && connected && audioFrames > 1000 && displayFrames >= 3
            && speakerPackets == slicePackets;
        const QJsonObject result{
            {"host", parser.value("host")}, {"success", success}, {"error", failure},
            {"connected", connected}, {"receiveMilliseconds", elapsed},
            {"audioFrames", audioFrames}, {"speakerPackets", speakerPackets}, {"slicePackets", slicePackets},
            {"spectrumFrames", displayFrames}, {"lastSpectrumBins", displayBins},
            {"audioRms", audioFrames ? std::sqrt(sumSquares / double(audioFrames)) : 0},
            {"audioPeak", peak}, {"sourceUpdates", sourceUpdates},
            {"sharedSettingsChanged", false}
        };
        const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
        QSaveFile output(parser.value("output"));
        const bool saved = output.open(QIODevice::WriteOnly) && output.write(json) == json.size() && output.commit();
        std::fwrite(json.constData(), 1, std::size_t(json.size()), stdout);
        app.exit(success && saved ? 0 : 1);
    };
    QObject::connect(&backend, &AetherSDR::IRadioBackend::connected, &app, [&] {
        connected = true;
        clock.start();
        QTimer::singleShot(seconds * 1000, &app, finish);
    });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::connectionError, &app, [&](const QString& error) {
        failure = error;
        finish();
    });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::disconnected, &app, [&] {
        if (!finishing) { failure = QStringLiteral("Unexpected disconnect"); finish(); }
    });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::extensionStatus, &app,
        [&](const QString&, const QString& kind, const QVariantMap& fields) {
            if (kind == "source") { sourceUpdates.append(QJsonObject::fromVariantMap(fields)); }
        });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::audioFrameReady, &app,
        [&](const AetherSDR::PcmFrame& frame) {
            const QByteArray pcm = frame.legacyStereo24();
            ++speakerPackets;
            audioFrames += pcm.size() / 8;
            for (qsizetype offset = 0; offset + 8 <= pcm.size(); offset += 8) {
                float value = 0;
                std::memcpy(&value, pcm.constData() + offset, sizeof(value));
                if (!std::isfinite(value)) { failure = QStringLiteral("Non-finite audio"); finish(); return; }
                sumSquares += double(value) * value;
                peak = std::max(peak, double(std::abs(value)));
            }
        });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::sliceAudioFrameReady, &app,
        [&](int, const AetherSDR::PcmFrame&) { ++slicePackets; });
    QObject::connect(&backend, &AetherSDR::IRadioBackend::spectrumFrameReady, &app,
        [&](int, const QByteArray& bins, const AetherSDR::SpectrumCoverage&) {
            ++displayFrames;
            displayBins = bins.size() / 4;
        });
    QTimer::singleShot(20000, &app, [&] {
        if (!connected) { failure = QStringLiteral("Connection deadline"); finish(); }
    });
    AetherSDR::RadioConnectRequest request;
    request.host = parser.value("host");
    request.port = 50000;
    request.params.insert(QStringLiteral("nrsp.username"), parser.value("username"));
    request.params.insert(QStringLiteral("nrsp.password"), QString::fromUtf8(qgetenv("NRSP_TEST_PASSWORD")));
    qunsetenv("NRSP_TEST_PASSWORD");
    backend.connectRadio(request);
    return app.exec();
}
