#pragma once

#include "Web888CapturePlan.h"
#include "Web888DecoderPeriod.h"
#include <map>

namespace AetherSDR {
// Explicit post-filter sample/UTC association. A producer must independently
// qualify the mapping; packet arrival and opaque SND fields are not evidence.
struct Web888DecoderClockAnchor {
    enum class Source { Unqualified, VerifiedFixture, QualifiedSampleClock };
    Source source{Source::Unqualified};
    quint64 captureId{}, generation{}, firstSampleIndex{};
    qint64 firstSampleUtcTicks{}; // Unix epoch, 12000 ticks/s; delay already corrected
    double filterDelaySeconds{};
    quint64 validUntilSampleIndex{}; // exclusive, same uninterrupted output run
    qint64 maximumErrorTicks{-1}; // worst-case bound THROUGH validUntil, includes drift
    std::shared_ptr<const std::atomic<bool>> lifetime; // revoke on clock loss/jump
};

struct Web888DecoderJob {
    Web888DecoderPeriod period;
    QString mode;
    std::shared_ptr<const std::atomic<bool>> routeLifetime;
    std::shared_ptr<const std::atomic<bool>> clockLifetime;
    bool current() const {
        return (!clockLifetime || clockLifetime->load(std::memory_order_acquire))
            && period.audio.current() && routeLifetime
            && routeLifetime->load(std::memory_order_acquire);
    }
};

// Owner-thread engine primitive. No decoder process or device is started here.
// One assembling period and one waiting job per route. Consumer owns any taken job.
class Web888DecoderRouter {
public:
    ~Web888DecoderRouter() { clear(); }
    Web888DecoderRouter() = default;
    Web888DecoderRouter(const Web888DecoderRouter&) = delete;
    Web888DecoderRouter& operator=(const Web888DecoderRouter&) = delete;
    bool configure(const QVector<Web888DigitalPreset>& presets, quint64 sampleByteBudget,
                   QString* error = nullptr);
    bool arm(const Web888DecoderAudio& first, qint64 firstSampleUtcTicks);
    // The raw-tick overload is a fixture primitive. Production uses this gate.
    bool arm(const Web888DecoderAudio& first, const Web888DecoderClockAnchor& anchor);
    bool push(const Web888DecoderAudio& audio);
    // Clock/source invalidation without replacing the profile or disturbing
    // other captures. Preserves reservations; matching routes need new anchors.
    int disarmCapture(quint64 captureId);
    std::optional<Web888DecoderJob> takeNext();
    void clear();
    quint64 reservedSampleBytes() const { return m_reservedBytes; }
    quint64 droppedJobs() const { return m_dropped; }
    int queuedJobs() const;
private:
    struct Route {
        Web888DigitalPreset preset;
        int samples{};
        Web888DecoderPeriodBuffer buffer;
        std::optional<Web888DecoderJob> waiting;
        std::shared_ptr<std::atomic<bool>> lifetime;
        std::optional<quint64> captureId;
        std::optional<Web888DecoderClockAnchor> clock;
    };
    static void invalidate(Route& route);
    std::map<int, Route> m_routes;
    std::optional<int> m_lastTaken;
    quint64 m_reservedBytes{}, m_dropped{};
};
} // namespace AetherSDR
