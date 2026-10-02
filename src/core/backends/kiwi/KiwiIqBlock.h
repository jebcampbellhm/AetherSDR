#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QVector>
#include <QtEndian>
#include <complex>
#include <cmath>
#include <array>
#include <algorithm>

namespace AetherSDR {

// Filtered, post-AGC SND IQ, NOT raw ADC or phase-coherent multi-RX data.
// Wire provenance: RaspSDR/server a6610195 rx_sound.h snd_pkt_iq_t and
// rx_sound.cpp: I then Q, payload endianness in flag 0x80; sequence is LE.
struct KiwiIqBlock {
    QVector<std::complex<float>> samples;
    double sampleRateHz{};
    double centerHz{};
    quint64 generation{};
    quint32 sequence{};
    quint8 flags{};
    // Opaque bytes 10..19 of snd_pkt_iq_t: solution age, reserved byte,
    // seconds-of-week word and fractional-nanoseconds word. Not a UTC anchor
    // or a validity assertion; preserve for receiver qualification/recordings.
    std::array<quint8, 10> timingHeader{};
    bool discontinuity{true};
};

inline KiwiIqBlock decodeKiwiIq(const QByteArray& bytes, double rate, double center)
{
    KiwiIqBlock block;
    // Bound allocation before touching a payload. Timing bytes remain opaque:
    // validity, full epoch and latency need independent qualification.
    if (bytes.size() < 24 || bytes.size() > 20 + 4096 * 4
        || !bytes.startsWith("SND") || (bytes.size() - 20) % 4
        || !std::isfinite(rate) || rate < 8000 || rate > 48000
        || !std::isfinite(center) || center <= 0)
        return block;
    const auto* p = reinterpret_cast<const uchar*>(bytes.constData());
    block.flags = p[3];
    if (!(block.flags & 0x08) || (block.flags & 0x10)) return {};
    block.sequence = qFromLittleEndian<quint32>(p + 4);
    std::copy_n(p + 10, block.timingHeader.size(), block.timingHeader.begin());
    block.sampleRateHz = rate;
    block.centerHz = center;
    block.samples.reserve((bytes.size() - 20) / 4);
    const bool little = (block.flags & 0x80) != 0;
    for (qsizetype i = 20; i < bytes.size(); i += 4) {
        const auto read = [little](const uchar* at) {
            return (little ? qFromLittleEndian<qint16>(at)
                           : qFromBigEndian<qint16>(at)) / 32768.0f;
        };
        block.samples.append(std::complex<float>(read(p + i), read(p + i + 2)));
    }
    return block;
}
} // namespace AetherSDR
Q_DECLARE_METATYPE(AetherSDR::KiwiIqBlock)
