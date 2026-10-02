#pragma once

#include "Web888IqSplitter.h"
#include <optional>

namespace AetherSDR {
struct Web888DecoderPeriod {
    Web888DecoderAudio audio;
    qint64 startUtcTicks{}; // ticks since Unix epoch, at 12000 ticks/second
};

// One route, owner-thread only. No queue, clock inference, padding or decoding.
// Caller must supply a verified mapping corrected for FIR/recording delay.
class Web888DecoderPeriodBuffer {
public:
    bool arm(const Web888DecoderAudio& first, qint64 firstSampleUtcTicks, int periodSamples);
    std::optional<Web888DecoderPeriod> push(const Web888DecoderAudio& audio);
    void reset();
    bool armed() const { return m_armed && m_source.current(); }
    qsizetype bufferedSamples() const { return m_samples.size(); }
private:
    Web888DecoderAudio m_source;
    QVector<float> m_samples;
    quint64 m_nextSample{};
    qint64 m_nextTick{}, m_periodStart{};
    int m_periodSamples{};
    bool m_armed{}, m_first{};
};
} // namespace AetherSDR
