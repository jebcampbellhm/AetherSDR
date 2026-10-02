#pragma once

#include "core/backends/kiwi/KiwiIqBlock.h"
#include <QString>
#include <array>
#include <atomic>
#include <memory>

namespace AetherSDR {

struct Web888DigitalSlice {
    int id{};
    double dialHz{}; // USB reference; RF = dial + audio frequency
    double lowHz{0};
    double highHz{3000};
};

struct Web888DecoderAudio {
    static constexpr int sampleRateHz = 12000;
    int sliceId{};
    double dialHz{};
    QVector<float> mono; // 12000 Hz; independent of speaker volume/mute
    bool discontinuity{true};
    quint64 generation{};
    quint64 captureId{}; // pool-scoped, never reused; zero for a standalone capture
    quint32 sourceSequence{};
    quint64 firstSampleIndex{}; // relative to this uninterrupted run, including discarded startup
    double filterDelaySeconds{}; // FIR group delay; not transport latency or UTC
    std::shared_ptr<const std::atomic<bool>> lifetime;
    bool current() const { return lifetime && lifetime->load(std::memory_order_acquire); }
};

// Bounded streaming reference implementation. No threads or hidden work queue.
// Each USB slice: complex mix -> windowed FIR -> fractional resample -> real USB.
// CPU qualification/worker extraction precedes enabling a multi-band scheduler.
class Web888IqSplitter {
public:
    bool configure(const QVector<Web888DigitalSlice>& slices, QString* error = nullptr);
    QVector<Web888DecoderAudio> process(const KiwiIqBlock& block, QString* error = nullptr);
    void reset();
private:
    static constexpr int taps = 513;
    struct Channel {
        Web888DigitalSlice spec;
        std::array<double, taps> kernel{};
        std::array<std::complex<double>, taps> history{};
        int cursor{};
        quint64 inputCount{};
        quint64 outputCount{};
        double phase{}, outputPhase{}, nextOutput{};
        std::complex<double> previous{};
        bool pendingDiscontinuity{true};
    };
    QVector<Channel> m_channels;
    double m_rate{}, m_center{};
    quint64 m_generation{};
    quint32 m_sequence{};
    bool m_valid{};
};
} // namespace AetherSDR
Q_DECLARE_METATYPE(AetherSDR::Web888DecoderAudio)
