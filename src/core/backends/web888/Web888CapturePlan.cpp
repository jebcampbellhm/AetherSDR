#include "Web888CapturePlan.h"
#include <QSet>
#include <algorithm>

namespace AetherSDR {
QVector<Web888DigitalPreset> web888DigitalPresets(bool includeFt4, bool alternate80mWspr)
{
    struct Band { const char* name; double ft8, wspr, ft4; };
    constexpr Band bands[]{
        {"80m", 3573, 3568.6, 3575}, {"40m", 7074, 7038.6, 7047.5},
        {"30m", 10136, 10138.7, 10140}, {"20m", 14074, 14095.6, 14080},
        {"17m", 18100, 18104.6, 18104}, {"15m", 21074, 21094.6, 21140},
        {"12m", 24915, 24924.6, 24919}, {"10m", 28074, 28124.6, 28180}
    };
    QVector<Web888DigitalPreset> result;
    int id = 0;
    for (const auto& b : bands) {
        const auto band = QString::fromLatin1(b.name);
        result.append({band, QStringLiteral("FT8"), {id, b.ft8 * 1000, 0, 3000}});
        result.append({band, QStringLiteral("WSPR"), {id + 1, b.wspr * 1000, 1400, 1600}});
        if (includeFt4) result.append({band, QStringLiteral("FT4"), {id + 3, b.ft4 * 1000, 0, 3000}});
        id += 10;
    }
    if (alternate80mWspr)
        result.append({QStringLiteral("80m"), QStringLiteral("WSPR"), {2, 3592600, 1400, 1600}});
    return result;
}

Web888CapturePlan planWeb888Captures(const QVector<Web888DigitalPreset>& presets,
                                    double rate, int available, int listening)
{
    Web888CapturePlan plan;
    plan.reservedListening = listening;
    if (!std::isfinite(rate) || rate < 8000 || rate > 48000
        || available < 0 || available > 64 || listening < 0 || listening > available
        || presets.size() > 128) {
        plan.error = QStringLiteral("Invalid sample rate, receiver budget or profile size");
        return plan;
    }
    QSet<int> ids;
    for (const auto& p : presets) {
        Web888IqSplitter validator;
        if (p.band.isEmpty() || p.mode.isEmpty() || ids.contains(p.slice.id)
            || !validator.configure({p.slice}) || p.slice.dialHz < 10000
            || p.slice.dialHz + p.slice.highHz > 32000000) {
            plan.error = QStringLiteral("Invalid digital preset or duplicate slice identity");
            return plan;
        }
        ids.insert(p.slice.id);
    }
    auto sorted = presets;
    const auto low = [](const auto& p) { return p.slice.dialHz + p.slice.lowHz; };
    const auto high = [](const auto& p) { return p.slice.dialHz + p.slice.highHz; };
    std::sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) {
        if (low(a) != low(b)) return low(a) < low(b);
        if (high(a) != high(b)) return high(a) < high(b);
        return a.slice.id < b.slice.id;
    });
    const double usable = rate - 2000; // same margins enforced by the splitter
    double first = 0, last = 0;
    for (const auto& p : sorted) {
        if (high(p) - low(p) > usable) {
            plan.windows.clear();
            plan.error = QStringLiteral("A decoder passband exceeds usable capture width");
            return plan;
        }
        if (plan.windows.isEmpty() || high(p) - first > usable
            || plan.windows.last().presets.size() >= 16
            || plan.windows.last().presets.first().band != p.band) {
            first = low(p); last = high(p);
            plan.windows.append(Web888CaptureWindow{});
        } else last = std::max(last, high(p));
        auto& window = plan.windows.last();
        window.presets.append(p);
        window.centerHz = (first + last) / 2;
    }
    plan.fitsCapacity = plan.windows.size() + listening <= available;
    if (!plan.fitsCapacity)
        plan.error = QStringLiteral("Profile needs %1 captures plus %2 listening receivers; only %3 available")
            .arg(plan.windows.size()).arg(listening).arg(available);
    return plan;
}
} // namespace AetherSDR
