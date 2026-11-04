#include "TestSettingsProfile.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/kiwi/KiwiSessionId.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <cstring>
#include <QSet>
#include <QUrl>

#ifdef HAVE_WEBSOCKETS
namespace AetherSDR {

class StatusReply final : public QNetworkReply {
public:
    StatusReply(const QByteArray& payload) : m_payload(payload) {
        setUrl(QUrl("http://receiver.example:8073/status"));
        setRequest(QNetworkRequest(url()));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_payload.size() - m_offset; }
protected:
    qint64 readData(char* out, qint64 size) override {
        const auto count = std::min(size, qint64(m_payload.size() - m_offset));
        if (!count) { return -1; }
        std::memcpy(out, m_payload.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }
private:
    QByteArray m_payload;
    qint64 m_offset{};
};

// Drive the actual startup/URL/cleanup path, substituting only socket opening
// and HTTP preflight. No listener, firmware model or external endpoint.
class KiwiSdrSessionTest final : public KiwiSdrClient {
public:
    explicit KiwiSdrSessionTest(Streams streams = Streams::SoundAndWaterfall)
        : KiwiSdrClient(streams)
    {
    }

    explicit KiwiSdrSessionTest(Admission admission)
        : KiwiSdrClient(Streams::SoundAndWaterfall, admission) {}

    void offerMonitor(const QString& token)
    {
        handleTextMessage(StreamKind::Sound, QStringLiteral("MSG ") + token);
    }

    void status(const QByteArray& payload) {
        m_statusReply = new StatusReply(payload);
        handleStatusPreflightFinished(m_statusReply);
    }
    bool requestedMonitor() const { return m_monitorQueueRequested || m_monitorMode; }

    QStringList soundCommands;
    void sampleRateSetup()
    {
        m_soundAudioRateAcked = true;
        m_soundSampleRateCommandsSent = false;
        sendSoundSampleRateCommands();
    }

    QNetworkRequest sound;
    QNetworkRequest waterfall;
    int soundOpens = 0;
    int waterfallOpens = 0;
    bool cancelOnSoundOpen = false;

    void open(KiwiSdrProtocol::KiwiSdrReceiverFamily family, bool secure = false)
    {
        setReceiverFamily(family);
        connectToEndpoint(QStringLiteral("receiver.example:8073"));
        m_secureWebSocket = secure;
        m_webSocketPort = secure ? 443 : 8073;
        openWebSockets();
    }

    void reportWaterfallCapacity()
    {
        m_waterfallRxChannel = 0;
        m_waterfallChannelCount = 13;
        updateWaterfallAvailability();
    }

    bool queueSecureRetry()
    {
        connectToEndpoint(QStringLiteral("receiver.proxy.kiwisdr.com:8073"));
        return retryWithSecureWebSocket(false);
    }

protected:
    void startStatusPreflight(const QUrl&) override {}
    void sendSoundCommand(const QString& command) override { soundCommands.append(command); }
    void openSoundTransport(const QNetworkRequest& request) override
    {
        sound = request;
        ++soundOpens;
        if (cancelOnSoundOpen) {
            disconnectFromEndpoint();
        }
    }
    void openWaterfallTransport(const QNetworkRequest& request) override
    {
        waterfall = request;
        ++waterfallOpens;
    }
};
}
#endif

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("kiwi-sdr-session"));
    if (!profile.isValid()) {
        return 1;
    }
    QCoreApplication app(argc, argv);
#ifdef HAVE_WEBSOCKETS
    using namespace AetherSDR;
    using Family = KiwiSdrProtocol::KiwiSdrReceiverFamily;
    QSet<QString> ids;
    for (const auto family : {Family::Kiwi, Family::Web888}) {
        KiwiSdrSessionTest setup;
        setup.setReceiverFamily(family);
        setup.sampleRateSetup();
        const bool expectGeneratorReset = family == Family::Kiwi;
        if (setup.soundCommands.contains(QStringLiteral("SET genattn=0")) != expectGeneratorReset
            || setup.soundCommands.contains(QStringLiteral("SET gen=0 mix=-1")) != expectGeneratorReset
            || !setup.soundCommands.contains(QStringLiteral("SERVER DE CLIENT AetherSDR SND"))) {
            qCritical() << "sample-rate setup sends unsupported generator commands";
            return 1;
        }
    }
    for (const auto& token : {QStringLiteral("monitor=1"), QStringLiteral("qpos=1"),
                             QStringLiteral("camp=0,1"), QStringLiteral("audio_camp=1")}) {
        KiwiSdrSessionTest independent(KiwiSdrClient::Admission::IndependentReceiver);
        independent.open(Family::Web888);
        independent.offerMonitor(token);
        if (independent.state() != KiwiSdrClient::State::Busy || independent.requestedMonitor()) {
            qCritical() << "native admission accepted a monitor session" << token;
            return 1;
        }
    }
    KiwiSdrSessionTest full(KiwiSdrClient::Admission::IndependentReceiver);
    full.setReceiverFamily(Family::Web888);
    full.connectToEndpoint("receiver.example:8073");
    full.status("status=active\nusers=13\nusers_max=13\npreempt=0\next_api=13\n");
    if (full.state() != KiwiSdrClient::State::Busy || full.soundOpens || full.waterfallOpens) {
        qCritical() << "full status opened an independent receiver";
        return 1;
    }
    KiwiSdrSessionTest available(KiwiSdrClient::Admission::IndependentReceiver);
    available.setReceiverFamily(Family::Web888);
    available.connectToEndpoint("receiver.example:8073");
    available.status("status=active\nusers=12\nusers_max=13\npreempt=0\next_api=13\n");
    if (available.soundOpens != 1 || available.waterfallOpens != 1) {
        qCritical() << "available status did not proceed to server admission";
        return 1;
    }
    KiwiSdrSessionTest cancelled;
    cancelled.cancelOnSoundOpen = true;
    cancelled.open(Family::Web888);
    if (cancelled.soundOpens != 1 || cancelled.waterfallOpens != 0
        || cancelled.state() != KiwiSdrClient::State::Disconnected) {
        qCritical() << "cancelled sound startup still opened a partner";
        return 1;
    }
    KiwiSdrSessionTest replaced;
    if (!replaced.queueSecureRetry()) {
        qCritical() << "test did not queue the production secure retry";
        return 1;
    }
    replaced.open(Family::Web888);
    QCoreApplication::processEvents();
    if (replaced.soundOpens != 1 || replaced.waterfallOpens != 1) {
        qCritical() << "old queued retry opened transports for the replacement session";
        return 1;
    }
    for (Family family : {Family::Kiwi, Family::Web888}) {
        for (bool secure : {false, true}) {
            KiwiSdrSessionTest paired;
            paired.open(family, secure);
            const QString prefix = family == Family::Web888
                ? QStringLiteral("/kiwi/") : QStringLiteral("/ws/kiwi/");
            const QString soundPath = paired.sound.url().path();
            const QString id = soundPath.section('/', -2, -2);
            bool numeric = false;
            const quint64 value = id.toULongLong(&numeric);
            if (paired.soundOpens != 1 || paired.waterfallOpens != 1
                || !soundPath.startsWith(prefix) || !soundPath.endsWith("/SND")
                || paired.waterfall.url().path() != prefix + id + "/W/F"
                || ids.contains(id) || !numeric
                || value < kiwi::SessionIdAllocator::kNewSpace
                || value > kiwi::SessionIdAllocator::kMaxId
                || paired.sound.url().scheme() != (secure ? "wss" : "ws")
                || paired.sound.url().port() != (secure ? 443 : 8073)
                || paired.sound.rawHeader("Origin")
                    != (secure ? "https://receiver.example:443" : "http://receiver.example:8073")
                || paired.sound.rawHeader("Origin") != paired.waterfall.rawHeader("Origin")) {
                qCritical() << "paired session paths, identity or headers incorrect";
                return 1;
            }
            ids.insert(id);
            paired.disconnectFromEndpoint();
            paired.open(family, secure);
            const QString replacement = paired.sound.url().path().section('/', -2, -2);
            if (ids.contains(replacement) || paired.soundOpens != 2
                || paired.waterfallOpens != 2) {
                qCritical() << "reconnect reused a session identity";
                return 1;
            }
            ids.insert(replacement);

            KiwiSdrSessionTest soundOnly(KiwiSdrClient::Streams::SoundOnly);
            if (soundOnly.waterfallAvailable()) {
                return 1;
            }
            soundOnly.open(family, secure);
            const QString onlyPrefix = family == Family::Web888
                ? QStringLiteral("/no_wf/") : QStringLiteral("/ws/no_wf/");
            if (soundOnly.soundOpens != 1 || soundOnly.waterfallOpens != 0
                || !soundOnly.sound.url().path().startsWith(onlyPrefix)
                || !soundOnly.sound.url().path().endsWith("/SND")
                || soundOnly.waterfallAvailable()) {
                qCritical() << "sound-only session allocated a waterfall";
                return 1;
            }
            const QString onlyId = soundOnly.sound.url().path().section('/', -2, -2);
            if (ids.contains(onlyId)) {
                return 1;
            }
            ids.insert(onlyId);
            soundOnly.reportWaterfallCapacity();
            soundOnly.disconnectFromEndpoint();
            if (soundOnly.waterfallAvailable()) {
                qCritical() << "metadata/cleanup invented a waterfall subscription";
                return 1;
            }
        }
    }
#else
    AetherSDR::KiwiSdrClient client;
    client.connectToEndpoint(QStringLiteral("receiver.example:8073"));
    if (client.state() != AetherSDR::KiwiSdrClient::State::Error) {
        qCritical() << "WebSockets-disabled build did not refuse connection";
        return 1;
    }
#endif
    return 0;
}
