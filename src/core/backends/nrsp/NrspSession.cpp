#include "NrspSession.h"
#include "NrspAuthentication.h"

#include <QPointer>
#include <QDateTime>

#include <algorithm>
#include <utility>

namespace AetherSDR::nrsp {
namespace {
constexpr qint64 kTimeoutMs = 5000;
constexpr qint64 kRenewalMs = 300000;
constexpr quint32 kMaximumReceivers = 2;
}

NrspSession::NrspSession(QObject* parent, ChallengeFactory factory)
    : QObject(parent), m_challengeFactory(factory ? std::move(factory) : makeChallenge)
{
}

void NrspSession::begin(qint64 nowMs, QString username, QString password)
{
    stop();
    m_username = std::move(username);
    m_password = std::move(password);
    m_state = State::AwaitHello;
    m_startedMs = nowMs;
    m_pingMs = nowMs;
}

void NrspSession::stop()
{
    m_state = State::Stopped;
    m_decoder.reset();
    m_hello.reset();
    m_receivers.clear();
    m_pendingChallenge.clear();
    m_username.clear();
    m_password.clear();
}

void NrspSession::fail(const QString& reason)
{
    if (!isActive()) {
        return;
    }
    stop();
    emit failed(reason);
}

void NrspSession::receive(QByteArrayView bytes, qint64 nowMs)
{
    QPointer<NrspSession> guard(this);
    while (!bytes.isEmpty() && isActive()) {
        const qsizetype size = std::min<qsizetype>(bytes.size(), 65536);
        if (!m_decoder.append(bytes.first(size))) {
            fail(m_decoder.error());
            return;
        }
        bytes = bytes.sliced(size);
        for (;;) {
            Frame frame;
            const FrameDecoder::Result result = m_decoder.next(frame);
            if (result == FrameDecoder::Result::NeedMore) {
                break;
            }
            if (result == FrameDecoder::Result::Invalid) {
                fail(m_decoder.error());
                return;
            }
            dispatch(frame, nowMs);
            if (!guard || !isActive()) {
                return;
            }
        }
    }
}

void NrspSession::dispatch(const Frame& frame, qint64 nowMs)
{
    if (m_state == State::AwaitHello) {
        if (frame.type != 201 || !(m_hello = parseHello(frame.payload))) {
            fail(tr("Invalid nRSP-ST hello."));
            return;
        }
        m_decoder.setSession(m_hello->session);
        if (m_hello->requiresLogin) {
            if (m_username.isEmpty() || m_password.isEmpty()) {
                fail(tr("This nRSP-ST requires a username and password. Enter them in Connect by IP."));
                return;
            }
            const std::optional<QByteArray> login = makeLoginResponse(
                m_hello->loginNonce, m_username, m_password);
            m_username.clear();
            m_password.clear();
            if (!login) {
                fail(tr("Invalid nRSP-ST login fields or server nonce. The username must fit 16 UTF-8 bytes."));
                return;
            }
            m_state = State::AwaitLogin;
            m_loginMs = nowMs;
            send(58, *login);
            return;
        }
        m_username.clear();
        m_password.clear();
        m_state = State::Verifying;
        challenge(nowMs);
        return;
    }
    if (frame.type == 201) {
        fail(tr("Unexpected nRSP-ST hello during a session."));
        return;
    }
    if (frame.type == 221) {
        if (m_state != State::AwaitLogin || frame.payload.size() != 4) {
            fail(tr("Unexpected nRSP-ST login response."));
            return;
        }
        const quint32 status = *readU32(frame.payload);
        if (status != 1) {
            fail(status == 0
                ? tr("nRSP-ST login rejected. Check the username and password.")
                : tr("nRSP-ST login failed (status %1).").arg(status));
            return;
        }
        // Login acceptance does not grant source control. Only the source
        // state received on this connection can update that authority.
        m_state = State::Verifying;
        challenge(nowMs);
        return;
    }
    if (frame.type == 52) {
        if (m_pendingChallenge.isEmpty()
            || !verifyChallengeResponse(m_pendingChallenge, frame.payload)) {
            fail(tr("nRSP-ST server signature verification failed."));
            return;
        }
        m_pendingChallenge.clear();
        m_verifiedMs = nowMs;
        if (m_state == State::Verifying) {
            m_state = State::Ready;
            emit ready();
        }
        return;
    }
    if (frame.type == 202) {
        const std::optional<SourceState> source = parseSourceState(frame.payload);
        if (!source) {
            fail(tr("Invalid nRSP-ST source state."));
            return;
        }
        m_hello->source = *source;
        emit sourceChanged();
        return;
    }
    if (frame.type == 222) {
        const std::optional<quint32> status = readU32(frame.payload);
        if (!status || *status == 1) {
            fail(tr("nRSP-ST session verification expired or was rejected."));
            return;
        }
    }
    if (frame.type == 212 || frame.type == 216 || frame.type == 217) {
        if (!isReady()) {
            return;
        }
        const std::optional<ReceiverData> data = decodeReceiverData(frame);
        if (!data) {
            fail(tr("Invalid nRSP-ST IQ or display frame."));
            return;
        }
        QMap<quint32, Receiver>::iterator receiver = m_receivers.find(data->receiverId);
        if (receiver == m_receivers.end()) {
            return;
        }
        if (frame.type != 212) {
            receiver->lastIqMs = nowMs;
        }
        emit receiverData(frame.type, *data);
        return;
    }
    if (isReady() && frame.type != 207) {
        emit statusReceived(frame.type, frame.payload);
    }
}

bool NrspSession::send(quint8 type, const QByteArray& payload)
{
    if (!m_hello || !isActive()) {
        return false;
    }
    const std::optional<QByteArray> bytes = encodeFrame(type, payload, m_hello->session);
    if (!bytes) {
        return false;
    }
    QPointer<NrspSession> guard(this);
    emit sendBytes(*bytes);
    return guard && isActive();
}

void NrspSession::challenge(qint64 nowMs)
{
    m_pendingChallenge = m_challengeFactory();
    if (m_pendingChallenge.size() != 264) {
        fail(tr("Unable to generate nRSP-ST verification challenge."));
        return;
    }
    m_challengeMs = nowMs;
    send(51, m_pendingChallenge);
}

void NrspSession::tick(qint64 nowMs)
{
    if (!isActive()) {
        return;
    }
    if (m_state == State::AwaitHello) {
        if (nowMs - m_startedMs >= kTimeoutMs) {
            fail(tr("Timed out waiting for the nRSP-ST hello."));
        }
        return;
    }
    if (m_state == State::AwaitLogin) {
        if (nowMs - m_loginMs >= kTimeoutMs) {
            fail(tr("Timed out waiting for the nRSP-ST login response."));
        }
        return;
    }
    if (!m_pendingChallenge.isEmpty() && nowMs - m_challengeMs >= kTimeoutMs) {
        fail(tr("Timed out verifying the nRSP-ST server."));
        return;
    }
    for (const Receiver& receiver : std::as_const(m_receivers)) {
        if (nowMs - receiver.lastIqMs >= kTimeoutMs) {
            fail(tr("nRSP-ST IQ stopped for receiver %1.").arg(receiver.config.id));
            return;
        }
    }
    QPointer<NrspSession> guard(this);
    if (isReady() && m_pendingChallenge.isEmpty() && nowMs - m_verifiedMs >= kRenewalMs) {
        challenge(nowMs);
        if (!guard || !isActive()) {
            return;
        }
    }
    if (nowMs - m_pingMs >= 1000) {
        m_pingMs = nowMs;
        send(17, u64(static_cast<quint64>(QDateTime::currentMSecsSinceEpoch())));
    }
}

quint32 NrspSession::decimationFor(quint32 outputRate) const
{
    quint32 decimation = 0;
    while (decimation < 6 && (m_hello->source.sampleRate >> (decimation + 1)) >= outputRate) {
        ++decimation;
    }
    return decimation;
}

bool NrspSession::createReceiver(const ReceiverConfig& config, qint64 nowMs)
{
    if (!isReady() || m_receivers.contains(config.id) || m_receivers.size() >= kMaximumReceivers
        || config.id == 0 || config.frequencyHz > 2000000000ull
        || (config.outputRate != 48000 && config.outputRate != 192000)
        || config.displayBins < 512 || config.displayBins > 4096) {
        return false;
    }
    m_receivers.insert(config.id, Receiver{config, nowMs});
    // State is installed before the first output, so a synchronously injected
    // failure can retire it. Every following command checks the active session.
    QPointer<NrspSession> guard(this);
    const QList<std::pair<quint8, QByteArray>> commands{
        {54, u32(config.id)},
        {55, u32(config.id) + u32(decimationFor(config.outputRate))},
        {56, u32(config.id) + u32(config.outputRate)},
        {36, u32(config.id) + u32(0) + u64(0)},
        {31, u32(config.id) + u32(config.displayBins)},
        {37, u32(config.id) + u32(quint32(-100))},
        {38, u32(config.id) + u32(quint32(-220))},
        {30, u32(config.id) + u32(1)},
        {32, u32(config.id) + u64(config.frequencyHz)}
    };
    for (const std::pair<quint8, QByteArray>& command : commands) {
        if (!send(command.first, command.second) || !guard || !isReady()) {
            return false;
        }
    }
    return true;
}

bool NrspSession::removeReceiver(quint32 id)
{
    if (!isReady() || !m_receivers.remove(id)) {
        return false;
    }
    return send(29, u32(id));
}

bool NrspSession::tuneReceiver(quint32 id, quint64 hz)
{
    if (!isReady() || !m_receivers.contains(id) || hz > 2000000000ull) {
        return false;
    }
    m_receivers[id].config.frequencyHz = hz;
    return send(32, u32(id) + u64(hz));
}

bool NrspSession::setDisplayWidth(quint32 id, quint32 bins)
{
    if (!isReady() || !m_receivers.contains(id) || bins < 512 || bins > 4096) {
        return false;
    }
    m_receivers[id].config.displayBins = bins;
    return send(31, u32(id) + u32(bins));
}

bool NrspSession::setDisplayWindow(quint32 id, quint32 zoom, qint64 offsetHz)
{
    if (!isReady() || !m_receivers.contains(id) || zoom > 2
        || offsetHz < -qint64(m_hello->source.sampleRate) / 2
        || offsetHz > qint64(m_hello->source.sampleRate) / 2) {
        return false;
    }
    return send(36, u32(id) + u32(zoom) + u64(static_cast<quint64>(offsetHz)));
}

bool NrspSession::sourceCommand(quint8 type, quint32 value)
{
    return isReady() && m_hello->source.owner && send(type, u32(value));
}

void NrspSession::observeSharedSource(const SourceState& source)
{
    if (!isReady() || !m_hello) { return; }
    const bool owner = m_hello->source.owner;
    m_hello->source = source;
    m_hello->source.owner = owner;
    emit sourceChanged();
}

bool NrspSession::setSourceFrequency(quint64 hz)
{
    return isReady() && m_hello->source.owner && hz <= 2000000000ull
        && (m_hello->source.antenna != 2 || hz <= 200000000ull) && send(2, u64(hz));
}

bool NrspSession::setAntenna(quint32 antenna)
{
    return isReady() && antenna <= 2
        && (antenna != 2 || m_hello->source.frequencyHz <= 200000000ull)
        && sourceCommand(12, antenna);
}

bool NrspSession::setRfAgc(bool enabled) { return sourceCommand(7, enabled ? 1 : 0); }
bool NrspSession::setRfNotch(bool enabled) { return sourceCommand(11, enabled ? 1 : 0); }
bool NrspSession::setDabNotch(bool enabled) { return sourceCommand(10, enabled ? 1 : 0); }

bool NrspSession::setDisplayCadence(quint32 cadence)
{
    return (cadence == 3 || cadence == 12 || cadence == 24) && sourceCommand(42, cadence);
}

} // namespace AetherSDR::nrsp
