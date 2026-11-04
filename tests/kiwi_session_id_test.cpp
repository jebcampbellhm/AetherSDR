#include "core/backends/kiwi/KiwiSessionId.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int fail(const char* message)
{
    std::fprintf(stderr, "kiwi_session_id_test: %s\n", message);
    return 1;
}
}

int main()
{
    using AetherSDR::kiwi::SessionIdAllocator;
    SessionIdAllocator ids(0);
    if (ids.next() != SessionIdAllocator::kNewSpace
        || ids.next() != SessionIdAllocator::kNewSpace + 1) {
        return fail("successive allocations must be distinct without clock advancement");
    }

    constexpr int kWorkers = 8;
    constexpr int kPerWorker = 10000;
    std::array<std::vector<std::uint64_t>, kWorkers> results;
    std::array<std::thread, kWorkers> workers;
    for (int i = 0; i < kWorkers; ++i) {
        workers[i] = std::thread([&ids, &results, i] {
            results[i].reserve(kPerWorker);
            for (int j = 0; j < kPerWorker; ++j) {
                // Zero makes any unexpected exhaustion visible after joining.
                results[i].push_back(ids.next().value_or(0));
            }
        });
    }
    std::vector<std::uint64_t> all;
    for (int i = 0; i < kWorkers; ++i) {
        workers[i].join();
        all.insert(all.end(), results[i].begin(), results[i].end());
    }
    std::sort(all.begin(), all.end());
    if (all.front() != SessionIdAllocator::kNewSpace + 2
        || all.back() != SessionIdAllocator::kNewSpace + 1 + kWorkers * kPerWorker
        || std::adjacent_find(all.begin(), all.end()) != all.end()) {
        return fail("concurrent clients reused or lost an ID");
    }

    SessionIdAllocator last(SessionIdAllocator::kPayloadMask - 1);
    if (last.next() != SessionIdAllocator::kMaxId - 1
        || last.next() != SessionIdAllocator::kMaxId
        || last.next().has_value() || last.next().has_value()) {
        return fail("exhaustion must be permanent and must not enter the signed bit");
    }
    SessionIdAllocator masked(~std::uint64_t{0});
    if (masked.next() != SessionIdAllocator::kMaxId) {
        return fail("seed must preserve new timestamp space and signed URI range");
    }
    return 0;
}
