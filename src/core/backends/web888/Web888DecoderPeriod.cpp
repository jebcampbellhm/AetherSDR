#include "Web888DecoderPeriod.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace AetherSDR {
void Web888DecoderPeriodBuffer::reset()
{
    m_armed = false;
    m_first = false;
    m_source = {};
    m_samples = QVector<float>(); // release storage as well as discarding incomplete audio
}

bool Web888DecoderPeriodBuffer::arm(const Web888DecoderAudio& first, qint64 tick, int samples)
{
    reset();
    // 1..120 seconds. At most one completed period per bounded input block.
    if (!first.current() || tick < 0 || samples < 12000 || samples > 1440000
        || !std::isfinite(first.dialHz) || first.dialHz <= 0
        || !std::isfinite(first.filterDelaySeconds) || first.filterDelaySeconds < 0
        || tick > std::numeric_limits<qint64>::max() - samples) return false;
    m_source = first;
    m_source.mono = QVector<float>();
    m_nextSample = first.firstSampleIndex;
    m_nextTick = tick;
    m_periodSamples = samples;
    const auto remainder = tick % samples;
    m_periodStart = tick + (remainder ? samples - remainder : 0);
    m_armed = m_first = true;
    return true;
}

std::optional<Web888DecoderPeriod> Web888DecoderPeriodBuffer::push(const Web888DecoderAudio& audio)
{
    const auto count = audio.mono.size();
    if (!armed() || !audio.current() || audio.lifetime != m_source.lifetime
        || audio.captureId != m_source.captureId || audio.generation != m_source.generation
        || audio.sliceId != m_source.sliceId || audio.dialHz != m_source.dialHz
        || audio.filterDelaySeconds != m_source.filterDelaySeconds
        || (audio.discontinuity && !m_first) || audio.firstSampleIndex != m_nextSample
        || count <= 0 || count > 6144
        || m_nextSample > std::numeric_limits<quint64>::max() - quint64(count)
        || m_nextTick > std::numeric_limits<qint64>::max() - count
        || !std::all_of(audio.mono.cbegin(), audio.mono.cend(), [](float v) { return std::isfinite(v); })) {
        reset();
        return std::nullopt;
    }
    m_first = false;
    std::optional<Web888DecoderPeriod> result;
    for (qsizetype i = 0; i < count; ++i, ++m_nextTick) {
        if (m_nextTick < m_periodStart) continue; // discard initial partial period
        if (m_samples.isEmpty()) m_samples.reserve(m_periodSamples);
        m_samples.append(audio.mono[i]);
        if (m_samples.size() != m_periodSamples) continue;
        Web888DecoderPeriod period;
        period.audio = m_source;
        period.audio.firstSampleIndex = audio.firstSampleIndex + quint64(i) + 1 - quint64(m_periodSamples);
        period.audio.discontinuity = true; // each result is an independent decoder job
        period.audio.sourceSequence = audio.sourceSequence;
        period.audio.mono = std::move(m_samples);
        period.startUtcTicks = m_periodStart;
        result = std::move(period);
        m_samples = QVector<float>();
        if (m_periodStart > std::numeric_limits<qint64>::max() - m_periodSamples) {
            reset();
            return result;
        }
        m_periodStart += m_periodSamples;
    }
    m_nextSample += quint64(count);
    return result;
}
} // namespace AetherSDR
