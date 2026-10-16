#pragma once

#include "core/Resampler.h"
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace AetherSDR {
// Per-listener post-filter AGC. Never applied to the shared IQ or decoder feeds.
class Web888SsbAudio {
public:
    void reset() { m_envelope = 0; m_resampler.reset(); }
    void setMode(const QString& mode) { m_mode = mode; }
    QVector<float> level(const QVector<float>& input)
    {
        QVector<float> output;
        output.reserve(input.size());
        const double releaseSeconds = m_mode == QStringLiteral("fast") ? 0.1
            : m_mode == QStringLiteral("slow") ? 1.5 : 0.5;
        const double release = std::exp(-1.0 / (12000.0 * releaseSeconds));
        for (float sample : input) {
            if (!std::isfinite(sample)) { reset(); return {}; }
            // Immediate attack prevents a strong arrival from overshooting.
            m_envelope = std::max(double(std::abs(sample)), m_envelope * release);
            const double gain = m_mode == QStringLiteral("off") ? 1.0
                : std::min(1000.0, 0.15 / std::max(m_envelope, 0.00015));
            output.append(float(std::clamp(sample * gain, -1.0, 1.0)));
        }
        return output;
    }
    QByteArray process(const QVector<float>& input)
    {
        const auto output = level(input);
        if (output.isEmpty()) { return {}; }
        return m_resampler.processMonoToStereo(output.constData(), int(output.size()));
    }
private:
    QString m_mode{QStringLiteral("med")};
    double m_envelope{0};
    Resampler m_resampler{12000, 24000, 4096};
};
} // namespace AetherSDR
