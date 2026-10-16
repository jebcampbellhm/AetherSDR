#include "Web888IqSplitter.h"
#include <QSet>
#include <algorithm>
#include <numbers>

namespace AetherSDR {
namespace {
constexpr double tau = 2 * std::numbers::pi;
bool fail(QString* error, const char* message)
{
    if (error) *error = QString::fromLatin1(message);
    return false;
}
}

bool Web888IqSplitter::configure(const QVector<Web888DigitalSlice>& slices, QString* error)
{
    if (error) error->clear();
    if (slices.isEmpty() || slices.size() > 16)
        return fail(error, "A capture needs between 1 and 16 software slices");
    QSet<int> ids;
    for (const auto& s : slices) {
        if (s.id < 0 || ids.contains(s.id) || !std::isfinite(s.dialHz) || s.dialHz <= 0
            || !std::isfinite(s.lowHz) || !std::isfinite(s.highHz)
            || (s.lowerSideband ? (s.lowHz < -5000 || s.highHz > 0)
                                : (s.lowHz < 0 || s.highHz > 5000))
            || s.highHz - s.lowHz < 100)
            return fail(error, "Invalid or duplicate USB decoder slice");
        ids.insert(s.id);
    }
    m_channels.clear();
    for (const auto& s : slices) { Channel c; c.spec = s; m_channels.append(c); }
    reset();
    return true;
}

void Web888IqSplitter::reset()
{
    m_valid = false;
    for (auto& c : m_channels) { const auto spec = c.spec; c = Channel{}; c.spec = spec; }
}

QVector<Web888DecoderAudio> Web888IqSplitter::process(const KiwiIqBlock& block, QString* error)
{
    if (error) error->clear();
    const auto reject = [&](const char* message) {
        reset(); fail(error, message); return QVector<Web888DecoderAudio>{};
    };
    if (m_channels.isEmpty() || block.samples.isEmpty() || block.samples.size() > 4096
        || !std::isfinite(block.sampleRateHz) || block.sampleRateHz < 8000 || block.sampleRateHz > 48000
        || !std::isfinite(block.centerHz) || block.centerHz <= 0)
        return reject("Invalid IQ block or unconfigured splitter");
    for (const auto sample : block.samples)
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
            return reject("Nonfinite IQ sample");
    // 500 Hz capture guard plus 500 Hz transition allowance inside the server FIR.
    for (const auto& c : m_channels) {
        const double low = c.spec.dialHz + c.spec.lowHz - block.centerHz;
        const double high = c.spec.dialHz + c.spec.highHz - block.centerHz;
        if (low < -block.sampleRateHz / 2 + 1000 || high > block.sampleRateHz / 2 - 1000)
            return reject("Decoder passband does not fit the negotiated IQ capture");
    }
    if (!m_valid || block.discontinuity || block.sampleRateHz != m_rate
        || block.centerHz != m_center || block.generation != m_generation
        || block.sequence != quint32(m_sequence + 1)) {
        reset();
        m_rate = block.sampleRateHz; m_center = block.centerHz; m_generation = block.generation;
        for (auto& c : m_channels) {
            // Extra 150 Hz puts requested edges inside the passband, not at -6 dB.
            const double cutoff = ((c.spec.highHz - c.spec.lowHz) / 2 + 150) / m_rate;
            double sum = 0;
            for (int k = 0; k < taps; ++k) {
                const int n = k - (taps - 1) / 2;
                const double sinc = n == 0 ? 2 * cutoff : std::sin(tau * cutoff * n) / (std::numbers::pi * n);
                const double window = 0.42 - 0.5 * std::cos(tau * k / (taps - 1))
                    + 0.08 * std::cos(2 * tau * k / (taps - 1));
                c.kernel[k] = sinc * window; sum += c.kernel[k];
            }
            for (auto& coefficient : c.kernel) coefficient /= sum;
        }
        m_valid = true;
    }
    m_sequence = block.sequence;
    QVector<Web888DecoderAudio> result;
    for (auto& c : m_channels) {
        Web888DecoderAudio out;
        out.sliceId = c.spec.id; out.dialHz = c.spec.dialHz;
        out.generation = block.generation; out.sourceSequence = block.sequence;
        out.discontinuity = c.pendingDiscontinuity;
        out.filterDelaySeconds = double(taps - 1) / (2 * m_rate);
        const double audioCenter = (c.spec.lowHz + c.spec.highHz) / 2;
        const double step = -tau * (c.spec.dialHz + audioCenter - m_center) / m_rate;
        for (const auto sample : block.samples) {
            c.history[c.cursor] = std::complex<double>(sample) * std::polar(1.0, c.phase);
            c.phase = std::remainder(c.phase + step, tau);
            std::complex<double> filtered{};
            for (int k = 0; k < taps; ++k)
                filtered += c.history[(c.cursor + taps - k) % taps] * c.kernel[k];
            c.cursor = (c.cursor + 1) % taps;
            const double index = static_cast<double>(c.inputCount++);
            while (c.nextOutput <= index) {
                const double fraction = std::clamp(c.nextOutput - (index - 1), 0.0, 1.0);
                const auto value = c.previous + fraction * (filtered - c.previous);
                // Discard startup convolution rather than feed zero-padding to a decoder.
                if (index >= taps) {
                    if (out.mono.isEmpty()) out.firstSampleIndex = c.outputCount;
                    out.mono.append(static_cast<float>((value * std::polar(1.0, c.outputPhase)).real()));
                }
                ++c.outputCount;
                c.outputPhase = std::remainder(c.outputPhase + tau * audioCenter / 12000, tau);
                c.nextOutput += m_rate / 12000;
            }
            c.previous = filtered;
        }
        if (!out.mono.isEmpty()) {
            c.pendingDiscontinuity = false;
            result.append(std::move(out));
        }
    }
    return result;
}
} // namespace AetherSDR
