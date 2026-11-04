#include "Web888DecoderWav.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace AetherSDR {
std::optional<QByteArray> web888DecoderWav(const Web888DecoderJob& job, QString* error)
{
    if (error) error->clear();
    auto fail = [&](const QString& reason) -> std::optional<QByteArray> {
        if (error) *error = reason;
        return std::nullopt;
    };
    const int count = job.mode == "FT8" ? 180000 : job.mode == "FT4" ? 90000
        : job.mode == "WSPR" ? 1440000 : 0;
    const auto& period = job.period;
    if (!job.current()) return fail(QStringLiteral("Decoder job was revoked"));
    if (!count || period.audio.mono.size() != count || period.startUtcTicks < 0
        || period.startUtcTicks % count != 0)
        return fail(QStringLiteral("Decoder period size or timing is invalid"));
    if (!std::all_of(period.audio.mono.cbegin(), period.audio.mono.cend(), [](float v) { return std::isfinite(v); }))
        return fail(QStringLiteral("Decoder audio contains nonfinite samples"));
    QByteArray wav(44 + count * 2, '\0');
    wav.replace(0, 4, "RIFF"); wav.replace(8, 4, "WAVE");
    wav.replace(12, 4, "fmt "); wav.replace(36, 4, "data");
    auto put16 = [&](int at, quint16 v) { qToLittleEndian(v, wav.data() + at); };
    auto put32 = [&](int at, quint32 v) { qToLittleEndian(v, wav.data() + at); };
    put32(4, 36 + count * 2); put32(16, 16);
    put16(20, 1); put16(22, 1); put32(24, 12000); put32(28, 24000);
    put16(32, 2); put16(34, 16); put32(40, count * 2);
    for (int n = 0; n < count; ++n) {
        // Fixed gain; no normalization, speaker-volume adjustment or resampling.
        const double scaled = std::clamp(double(period.audio.mono[n]), -1.0, 1.0) * 32768;
        const auto pcm = qint16(std::clamp(std::lround(scaled), -32768L, 32767L));
        qToLittleEndian(pcm, wav.data() + 44 + n * 2);
    }
    if (!job.current()) return fail(QStringLiteral("Decoder job was revoked during serialization"));
    return wav;
}
}
