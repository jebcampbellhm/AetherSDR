#pragma once

#include "Web888CapturePlan.h"
#include "Web888DecoderPeriod.h"
#include <map>

namespace AetherSDR {
struct Web888DecoderJob {
    Web888DecoderPeriod period;
    QString mode;
    std::shared_ptr<const std::atomic<bool>> routeLifetime;
    bool current() const {
        return period.audio.current() && routeLifetime
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
    };
    static void invalidate(Route& route);
    std::map<int, Route> m_routes;
    std::optional<int> m_lastTaken;
    quint64 m_reservedBytes{}, m_dropped{};
};
} // namespace AetherSDR
