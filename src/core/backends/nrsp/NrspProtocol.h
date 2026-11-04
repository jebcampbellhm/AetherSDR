#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

namespace AetherSDR::nrsp {

// Firmware 1.0.15, observed TCP protocol. See docs/nrsp-st-protocol.md.
constexpr qsizetype kMaxPayload = 0xffff0;
constexpr qsizetype kMaxBuffered = 2 * (kMaxPayload + 4);

struct SessionKey {
    std::array<quint32, 4> words{0xfe45a343u, 0x2ef57affu, 0xaa500913u, 0xfec3326au};
    quint32 mask = 0x3f5e220au;
    bool operator==(const SessionKey&) const = default;
};

struct Frame {
    quint8 type = 0;
    QByteArray payload;
};

struct SourceState {
    quint64 frequencyHz = 0;
    quint32 sampleRate = 0;
    bool rfAgc = false;
    quint32 manualIfReduction = 0;
    quint32 lnaIndex = 0;
    quint32 antenna = 0;
    bool owner = false;
    bool dabNotch = false;
    bool rfNotch = false;
};

struct Hello {
    SessionKey session;
    SourceState source;
    quint32 displayCadence = 0;
    bool requiresLogin = false;
    QByteArray loginNonce;
};

// Integers are big-endian except 64-bit values, which put the low BE word first.
QByteArray u32(quint32 value);
QByteArray u64(quint64 value);
std::optional<quint32> readU32(QByteArrayView bytes, qsizetype offset = 0);
std::optional<quint64> readU64(QByteArrayView bytes, qsizetype offset = 0);
std::optional<Hello> parseHello(QByteArrayView payload);
std::optional<SourceState> parseSourceState(QByteArrayView payload);
std::optional<QByteArray> encodeFrame(quint8 type, QByteArrayView payload,
                                     const SessionKey& session);

// Pull one frame at a time: a hello changes the key for the very next frame,
// even when both arrive in the same TCP read. Errors latch until reset().
class FrameDecoder final
{
public:
    enum class Result { NeedMore, Ready, Invalid };
    bool append(QByteArrayView bytes);
    Result next(Frame& frame);
    void setSession(const SessionKey& session) { m_session = session; }
    void reset();
    const QString& error() const { return m_error; }
    qsizetype bufferedBytes() const { return m_buffer.size(); }

private:
    SessionKey m_session;
    QByteArray m_buffer;
    QString m_error;
};

} // namespace AetherSDR::nrsp
