#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace AetherSDR::kiwi {

// RaspSDR/server a6610195, rx/rx_server.cpp: NEW_TSTAMP_SPACE pairs SND/W/F
// by ID even across source IPs. The URI parser uses signed %lld: keep bit 63
// clear. One allocator must serve every Kiwi/Web-888 client in the process.
class SessionIdAllocator final {
public:
    static constexpr std::uint64_t kNewSpace = std::uint64_t{1} << 62;
    static constexpr std::uint64_t kPayloadMask = kNewSpace - 1;
    static constexpr std::uint64_t kMaxId = (std::uint64_t{1} << 63) - 1;

    explicit SessionIdAllocator(std::uint64_t seed)
        : m_next(kNewSpace | (seed & kPayloadMask))
    {
    }

    // Clock-independent, unique until exhaustion. Never wrap and reuse an ID
    // that could still be leased remotely after a disconnect or timeout.
    [[nodiscard]] std::optional<std::uint64_t> next()
    {
        std::uint64_t candidate = m_next.load(std::memory_order_relaxed);
        while (candidate <= kMaxId) {
            if (m_next.compare_exchange_weak(candidate, candidate + 1,
                                             std::memory_order_relaxed)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

private:
    std::atomic<std::uint64_t> m_next;
};

} // namespace AetherSDR::kiwi
