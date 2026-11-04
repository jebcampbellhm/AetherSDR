#pragma once

#include "NrspData.h"
#include "NrspProtocol.h"

#include <QObject>
#include <QMap>

#include <functional>
#include <optional>

namespace AetherSDR::nrsp {

struct ReceiverConfig {
    quint32 id = 1;
    quint64 frequencyHz = 0;
    quint32 outputRate = 48000;
    quint32 displayBins = 1024;
};

// Transport-independent state machine. Its owner supplies monotonic time and
// writes sendBytes to TCP. Tests inject captured bytes without a fake radio.
class NrspSession final : public QObject
{
    Q_OBJECT
public:
    using ChallengeFactory = std::function<QByteArray()>;
    explicit NrspSession(QObject* parent = nullptr, ChallengeFactory factory = {});
    void begin(qint64 nowMs, QString username = {}, QString password = {});
    void stop();
    void receive(QByteArrayView bytes, qint64 nowMs);
    void tick(qint64 nowMs);
    bool isReady() const { return m_state == State::Ready; }
    bool isActive() const { return m_state != State::Stopped; }
    const std::optional<Hello>& hello() const { return m_hello; }
    // A separate, read-only hello confirms shared settings. Its owner bit is
    // for that observer connection, so retain this session's own authority.
    void observeSharedSource(const SourceState& source);

    bool createReceiver(const ReceiverConfig& config, qint64 nowMs);
    bool removeReceiver(quint32 id);
    bool tuneReceiver(quint32 id, quint64 hz);
    bool setDisplayWidth(quint32 id, quint32 bins);
    bool setDisplayWindow(quint32 id, quint32 zoom, qint64 offsetHz);
    bool setSourceFrequency(quint64 hz);
    bool setAntenna(quint32 antenna);
    bool setRfAgc(bool enabled);
    bool setRfNotch(bool enabled);
    bool setDabNotch(bool enabled);
    bool setDisplayCadence(quint32 cadence);

signals:
    void sendBytes(const QByteArray& bytes);
    void ready();
    void sourceChanged();
    void receiverData(quint8 type, const AetherSDR::nrsp::ReceiverData& data);
    void statusReceived(quint8 type, const QByteArray& payload);
    void failed(const QString& reason);

private:
    enum class State { Stopped, AwaitHello, AwaitLogin, Verifying, Ready };
    struct Receiver {
        ReceiverConfig config;
        qint64 lastIqMs = 0;
    };
    void dispatch(const Frame& frame, qint64 nowMs);
    void challenge(qint64 nowMs);
    void fail(const QString& reason);
    bool send(quint8 type, const QByteArray& payload);
    bool sourceCommand(quint8 type, quint32 value);
    quint32 decimationFor(quint32 outputRate) const;

    ChallengeFactory m_challengeFactory;
    FrameDecoder m_decoder;
    State m_state = State::Stopped;
    std::optional<Hello> m_hello;
    QMap<quint32, Receiver> m_receivers;
    QByteArray m_pendingChallenge;
    QString m_username;
    QString m_password;
    qint64 m_loginMs = 0;
    qint64 m_startedMs = 0;
    qint64 m_challengeMs = 0;
    qint64 m_verifiedMs = 0;
    qint64 m_pingMs = 0;
};

} // namespace AetherSDR::nrsp
