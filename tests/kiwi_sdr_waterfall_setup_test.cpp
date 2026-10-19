#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "core/KiwiSdrClient.h"

#include <QCoreApplication>
#include <QDebug>

namespace AetherSDR {

// Inject only the transport boundary; production metadata handling, request
// caching and setup formatting run unchanged. No socket or firmware peer.
class KiwiSdrWaterfallSetupTest final : public KiwiSdrClient {
public:
    bool run()
    {
        using Family = KiwiSdrProtocol::KiwiSdrReceiverFamily;
        setReceiverFamily(Family::Web888);
        setWaterfallView(QStringLiteral("pan0"), 14.104, 0.2);
        sendWaterfallSetupCommands();
        const QString initialView = lastView();
        if (initialView.isEmpty()) {
            return fail("initial setup did not send a view");
        }
        commands.clear();
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG max_camp=4"));
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG freq_offset=0"));
        handleTextMessage(StreamKind::Sound, QStringLiteral("MSG wf_setup"));
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup=1"));
        if (!commands.isEmpty() || m_waterfallSetupResent) {
            return fail("a preamble, sound message or valued token consumed the replay");
        }
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (!m_waterfallSetupResent || lastView() != initialView
            || commands.count(QStringLiteral("SERVER DE CLIENT AetherSDR W/F")) != 1) {
            return fail("wf_setup did not replay the unchanged cached view");
        }
        commands.clear();
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (!commands.isEmpty()) {
            return fail("repeated marker replayed setup twice");
        }

        cleanupSockets();
        commands.clear();
        // Marker first deliberately: the replay must use metadata later in
        // this same message, not the previous connection's default scale.
        handleTextMessage(StreamKind::Waterfall, QStringLiteral(
            "MSG wf_setup center_freq=15360000 bandwidth=30720000 zoom_max=11"));
        const qsizetype greeting = commands.indexOf(
            QStringLiteral("SERVER DE CLIENT AetherSDR W/F"));
        const QString replayView = lastView();
        if (greeting < 0 || commands.lastIndexOf(replayView) <= greeting
            || m_waterfallZoomCap != 11
            || replayView != QStringLiteral("SET zoom=%1 start=%2")
                                 .arg(m_waterfallRequestZoom)
                                 .arg(m_waterfallRequestStart)
            || m_waterfallRequestStart >= (1024u << 11)) {
            return fail("reconnect replay did not use the complete metadata burst");
        }

        cleanupSockets();
        commands.clear();
        connected = false;
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (m_waterfallSetupResent || !commands.isEmpty()) {
            return fail("disconnected input consumed replay");
        }
        connected = true;
        m_monitorMode = true;
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (m_waterfallSetupResent || !commands.isEmpty()) {
            return fail("monitor input emitted setup commands");
        }
        m_monitorMode = false;
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (!m_waterfallSetupResent || lastView().isEmpty()) {
            return fail("ignored input prevented subsequent valid replay");
        }

        cleanupSockets();
        commands.clear();
        setReceiverFamily(Family::Kiwi);
        handleTextMessage(StreamKind::Waterfall, QStringLiteral("MSG wf_setup"));
        if (m_waterfallSetupResent || !commands.isEmpty()) {
            return fail("Kiwi family acquired Web-888 replay behavior");
        }
        return verifyFlatRowRecovery();
    }

protected:
    bool waterfallTransportConnected() const override { return connected; }
    void sendWaterfallCommand(const QString& command) override
    {
        if (connected) {
            commands.append(command);
        }
    }

private:
    bool verifyFlatRowRecovery()
    {
        using Family = KiwiSdrProtocol::KiwiSdrReceiverFamily;
        cleanupSockets();
        connected = true;
        setReceiverFamily(Family::Web888);
        setWaterfallView(QStringLiteral("pan0"), 14.2, 0.008);
        handleTextMessage(StreamKind::Waterfall, QStringLiteral(
            "MSG center_freq=15360000 bandwidth=30720000 zoom_max=14 wf_setup"));
        if (m_waterfallRequestZoom != 11) {
            return fail("narrow Web-888 view did not start at zoom 11");
        }
        const quint32 originalStart = m_waterfallRequestStart;
        for (int i = 0; i < 19; ++i) {
            feedRow(55);
        }
        if (m_waterfallRequestZoom != 11) {
            return fail("transient flat rows reduced the zoom prematurely");
        }
        feedRow(55, true);
        for (int i = 0; i < 19; ++i) {
            feedRow(55);
        }
        if (m_waterfallRequestZoom != 11) {
            return fail("a valid row did not reset the flat-row streak");
        }
        feedRow(55);
        if (m_waterfallRequestZoom != 10 || m_waterfallRecoveryZoomCap != 10
            || m_waterfallZoomMax != 14 || effectiveWaterfallZoomCap() != 14) {
            return fail("flat Web-888 rows did not reduce only the request ceiling");
        }
        // The header still uses the advertised 2^24 scale, and queued rows
        // from zoom 11 must not be reinterpreted as a four-byte-header frame.
        handleWaterfallFrame(makeRow(originalStart, 11, 55, false));
        if (m_waterfallFftBins != 1024 || m_waterfallFlatRows != 0) {
            return fail("stale high-zoom row changed the stream layout or recovery streak");
        }
        // On the live receiver zoom 10 repeats byte 83 except at its DC edge.
        for (int i = 0; i < 20; ++i) {
            feedRow(83);
        }
        if (m_waterfallRequestZoom != 9) {
            return fail("non-floor constant Web-888 rows did not recover");
        }
        feedRow(101, true);
        if (m_waterfallFlatRows != 0 || m_waterfallViewBandwidthMhz != 0.008
            || m_waterfallViewCenterMhz != 14.2) {
            return fail("recovery changed the viewport or rejected varying bins");
        }
        cleanupSockets();
        if (m_waterfallRecoveryZoomCap != 20 || m_waterfallFlatRows != 0) {
            return fail("reconnect retained the previous receiver's learned ceiling");
        }
        setReceiverFamily(Family::Kiwi);
        sendWaterfallViewToServer();
        const int kiwiZoom = m_waterfallRequestZoom;
        for (int i = 0; i < 40; ++i) {
            feedRow(55);
        }
        return m_waterfallRequestZoom == kiwiZoom
            && m_waterfallRecoveryZoomCap == 20;
    }
    static QByteArray makeRow(quint32 start, int zoom, char level, bool varying)
    {
        QByteArray frame("W/F ", 4);
        for (quint32 value : {start, static_cast<quint32>(zoom), quint32(1)}) {
            for (int shift = 0; shift < 32; shift += 8) {
                frame.append(static_cast<char>((value >> shift) & 0xffu));
            }
        }
        frame.append(QByteArray(1024, level));
        frame[16] = 55; // Receiver DC suppression is not useful variation.
        if (varying) {
            frame[16 + 512] = static_cast<char>(level + 1);
        }
        return frame;
    }
    void feedRow(char level, bool varying = false)
    {
        handleWaterfallFrame(makeRow(m_waterfallRequestStart,
                                     m_waterfallRequestZoom, level, varying));
    }
    QString lastView() const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it) {
            if (it->startsWith(QLatin1String("SET zoom="))) {
                return *it;
            }
        }
        return {};
    }
    static bool fail(const char* message)
    {
        qCritical() << message;
        return false;
    }
    bool connected{true};
    QStringList commands;
};

} // namespace AetherSDR

int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("kiwi-waterfall-setup"));
    if (!settings.isValid()) {
        return 1;
    }
    QCoreApplication app(argc, argv);
    AetherSDR::AppSettings::instance().load();
    AetherSDR::KiwiSdrWaterfallSetupTest test;
    return test.run() ? 0 : 1;
}
