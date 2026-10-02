#include "Web888DecoderRouter.h"
#include <cmath>

namespace AetherSDR {
void Web888DecoderRouter::invalidate(Route& route)
{
    if (route.lifetime) route.lifetime->store(false, std::memory_order_release);
    route.lifetime.reset();
    route.waiting.reset();
    route.buffer.reset();
    route.captureId.reset();
}

int Web888DecoderRouter::disarmCapture(quint64 captureId)
{
    int count = 0;
    for (auto& [id, route] : m_routes) {
        if (!route.captureId || *route.captureId != captureId) continue;
        invalidate(route);
        ++count;
    }
    return count;
}

void Web888DecoderRouter::clear()
{
    for (auto& [id, route] : m_routes) invalidate(route);
    m_routes.clear();
    m_reservedBytes = 0;
    m_lastTaken.reset();
}

bool Web888DecoderRouter::configure(const QVector<Web888DigitalPreset>& presets,
                                   quint64 budget, QString* error)
{
    if (error) error->clear();
    auto fail = [&](const QString& reason) { if (error) *error = reason; return false; };
    if (presets.size() > 64 || budget > 256ULL * 1024 * 1024)
        return fail(QStringLiteral("Decoder route or memory limit exceeded"));
    std::map<int, Route> proposed;
    quint64 bytes = 0;
    for (const auto& preset : presets) {
        const int samples = preset.mode == "FT8" ? 180000 : preset.mode == "FT4" ? 90000
            : preset.mode == "WSPR" ? 1440000 : 0;
        const auto& slice = preset.slice;
        if (!samples || slice.id < 0 || !std::isfinite(slice.dialHz) || slice.dialHz <= 0
            || !std::isfinite(slice.lowHz) || !std::isfinite(slice.highHz)
            || slice.lowHz < 0 || slice.highHz > 5000 || slice.highHz - slice.lowHz < 100
            || proposed.contains(preset.slice.id))
            return fail(QStringLiteral("Invalid or duplicate decoder route"));
        bytes += quint64(samples) * sizeof(float) * 2; // assembling + one waiting job
        if (bytes > budget) return fail(QStringLiteral("Decoder sample-memory budget is insufficient"));
        Route route;
        route.preset = preset; route.samples = samples;
        proposed.emplace(preset.slice.id, std::move(route));
    }
    clear(); // validated replacement; explicitly revokes all old route jobs
    m_routes = std::move(proposed);
    m_reservedBytes = bytes;
    return true;
}

bool Web888DecoderRouter::arm(const Web888DecoderAudio& first, qint64 tick)
{
    const auto it = m_routes.find(first.sliceId);
    if (it == m_routes.end()) return false;
    auto& route = it->second;
    invalidate(route);
    if (first.dialHz != route.preset.slice.dialHz || !route.buffer.arm(first, tick, route.samples)) return false;
    route.lifetime = std::make_shared<std::atomic<bool>>(true);
    route.captureId = first.captureId;
    return true;
}

bool Web888DecoderRouter::push(const Web888DecoderAudio& audio)
{
    const auto it = m_routes.find(audio.sliceId);
    if (it == m_routes.end()) return false;
    auto& route = it->second;
    // Release the old job before assembly can finish a second full period.
    // Otherwise a boundary-crossing block can transiently retain three periods.
    if (route.waiting && route.buffer.bufferedSamples() + audio.mono.size() >= route.samples) {
        ++m_dropped;
        route.waiting.reset();
    }
    auto completed = route.buffer.push(audio);
    if (!route.buffer.armed()) { invalidate(route); return false; }
    if (completed) {
        if (route.waiting) ++m_dropped;
        route.waiting = Web888DecoderJob{std::move(*completed), route.preset.mode, route.lifetime};
    }
    return true;
}

int Web888DecoderRouter::queuedJobs() const
{
    int count = 0;
    for (const auto& [id, route] : m_routes) count += route.waiting && route.waiting->current();
    return count;
}

std::optional<Web888DecoderJob> Web888DecoderRouter::takeNext()
{
    if (m_routes.empty()) return std::nullopt;
    auto it = m_lastTaken ? m_routes.upper_bound(*m_lastTaken) : m_routes.begin();
    for (size_t n = 0; n < m_routes.size(); ++n) {
        if (it == m_routes.end()) it = m_routes.begin();
        auto& route = it->second;
        if (!route.buffer.armed()) invalidate(route);
        if (route.waiting && route.waiting->current()) {
            m_lastTaken = it->first;
            auto job = std::move(route.waiting);
            route.waiting.reset();
            return job;
        }
        ++it;
    }
    return std::nullopt;
}
} // namespace AetherSDR
