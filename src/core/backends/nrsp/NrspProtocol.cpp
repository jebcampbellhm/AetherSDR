#include "NrspProtocol.h"

#include <QtEndian>

namespace AetherSDR::nrsp {
namespace {

constexpr quint32 kDelta = 0x9e3779b9u;

void transform(QByteArray& bytes, const SessionKey& session, bool decrypt)
{
    // Only the first HALF of the padded payload is transformed. Whole-source
    // IQ (203) bypasses this operation; its header is still masked.
    for (qsizetype offset = 0; offset < bytes.size() / 2; offset += 8) {
        char* block = bytes.data() + offset;
        quint32 left = qFromLittleEndian<quint32>(block);
        quint32 right = qFromLittleEndian<quint32>(block + 4);
        quint32 sum = decrypt ? kDelta * 32u : 0u;
        for (int round = 0; round < 32; ++round) {
            if (decrypt) {
                right -= (((left << 4) ^ (left >> 5)) + left)
                         ^ (sum + session.words[(sum >> 11) & 3]);
                sum -= kDelta;
                left -= (((right << 4) ^ (right >> 5)) + right)
                        ^ (sum + session.words[sum & 3]);
            } else {
                left += (((right << 4) ^ (right >> 5)) + right)
                        ^ (sum + session.words[sum & 3]);
                sum += kDelta;
                right += (((left << 4) ^ (left >> 5)) + left)
                         ^ (sum + session.words[(sum >> 11) & 3]);
            }
        }
        qToLittleEndian(left, block);
        qToLittleEndian(right, block + 4);
    }
}

std::optional<SourceState> sourceAt(QByteArrayView bytes, qsizetype base)
{
    if (base < 0 || bytes.size() < base + 32) {
        return std::nullopt;
    }
    SourceState state;
    state.frequencyHz = *readU64(bytes, base);
    state.sampleRate = *readU32(bytes, base + 8);
    const quint32 agc = *readU32(bytes, base + 12);
    state.rfAgc = agc != 0;
    state.manualIfReduction = *readU32(bytes, base + 16);
    state.lnaIndex = *readU32(bytes, base + 20);
    state.antenna = *readU32(bytes, base + 24);
    const quint32 flags = *readU32(bytes, base + 28);
    state.owner = (flags & 1u) != 0;
    state.dabNotch = (flags & 2u) != 0;
    state.rfNotch = (flags & 4u) != 0;
    // The radio's reported RF state is authoritative, including during an
    // ownership transfer. Unknown flag bits are retained by neither control.
    if (state.frequencyHz > 2000000000ull || state.sampleRate < 2000000u
        || state.sampleRate > 10000000u || agc > 1u || state.antenna > 2u
        || state.manualIfReduction > 100u || state.lnaIndex > 63u) {
        return std::nullopt;
    }
    return state;
}

} // namespace

QByteArray u32(quint32 value)
{
    QByteArray bytes(4, Qt::Uninitialized);
    qToBigEndian(value, bytes.data());
    return bytes;
}

QByteArray u64(quint64 value)
{
    return u32(static_cast<quint32>(value)) + u32(static_cast<quint32>(value >> 32));
}

std::optional<quint32> readU32(QByteArrayView bytes, qsizetype offset)
{
    if (offset < 0 || bytes.size() < 4 || offset > bytes.size() - 4) {
        return std::nullopt;
    }
    return qFromBigEndian<quint32>(bytes.data() + offset);
}

std::optional<quint64> readU64(QByteArrayView bytes, qsizetype offset)
{
    if (offset < 0 || bytes.size() < 8 || offset > bytes.size() - 8) {
        return std::nullopt;
    }
    return quint64(*readU32(bytes, offset)) | (quint64(*readU32(bytes, offset + 4)) << 32);
}

std::optional<Hello> parseHello(QByteArrayView payload)
{
    if (payload.size() < 172 || payload.first(4) != QByteArrayView("SDRP", 4)) {
        return std::nullopt;
    }
    const std::optional<SourceState> source = sourceAt(payload, 8);
    if (!source) {
        return std::nullopt;
    }
    Hello hello;
    hello.source = *source;
    for (qsizetype i = 0; i < 4; ++i) {
        hello.session.words[static_cast<std::size_t>(i)] = *readU32(payload, 0x48 + 4 * i);
    }
    hello.session.mask = *readU32(payload, 0x58);
    hello.displayCadence = *readU32(payload, 0x2c);
    hello.requiresLogin = (*readU32(payload, 0x5c) & 0x80u) != 0;
    hello.loginNonce = QByteArray(payload.data() + 0x6c, 64);
    return hello;
}

std::optional<SourceState> parseSourceState(QByteArrayView payload)
{
    if (payload.size() < 64) {
        return std::nullopt;
    }
    return sourceAt(payload, 0);
}

std::optional<QByteArray> encodeFrame(quint8 type, QByteArrayView payload,
                                     const SessionKey& session)
{
    if (payload.size() > kMaxPayload) {
        return std::nullopt;
    }
    const qsizetype padding = (16 - payload.size() % 16) % 16;
    const qsizetype length = payload.size() + padding;
    if (length > kMaxPayload) {
        return std::nullopt;
    }
    QByteArray padded(payload.data(), payload.size());
    padded.append(padding, '\0');
    if (type != 203) {
        transform(padded, session, false);
    }
    const quint32 header = (quint32(type) << 24) | (quint32(length) << 4) | quint32(padding);
    return u32(header ^ session.mask) + padded;
}

bool FrameDecoder::append(QByteArrayView bytes)
{
    if (!m_error.isEmpty()) {
        return false;
    }
    if (bytes.size() > kMaxBuffered - m_buffer.size()) {
        m_error = QStringLiteral("nRSP receive buffer limit exceeded");
        m_buffer.clear();
        return false;
    }
    m_buffer.append(bytes.data(), bytes.size());
    return true;
}

FrameDecoder::Result FrameDecoder::next(Frame& frame)
{
    if (!m_error.isEmpty()) {
        return Result::Invalid;
    }
    if (m_buffer.size() < 4) {
        return Result::NeedMore;
    }
    const quint32 header = *readU32(m_buffer) ^ m_session.mask;
    const qsizetype length = (header >> 4) & 0xfffffu;
    const qsizetype padding = header & 15u;
    if (length > kMaxPayload || length % 16 != 0 || padding > length) {
        m_error = QStringLiteral("Invalid nRSP frame length or padding");
        m_buffer.clear();
        return Result::Invalid;
    }
    if (m_buffer.size() < length + 4) {
        return Result::NeedMore;
    }
    frame.type = static_cast<quint8>(header >> 24);
    frame.payload = m_buffer.mid(4, length);
    m_buffer.remove(0, length + 4);
    if (frame.type != 203) {
        transform(frame.payload, m_session, true);
    }
    frame.payload.chop(padding);
    return Result::Ready;
}

void FrameDecoder::reset()
{
    m_session = SessionKey{};
    m_buffer.clear();
    m_error.clear();
}

} // namespace AetherSDR::nrsp
