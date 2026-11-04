#include "core/backends/web888/Web888DecoderPeriod.h"
#include <cstdio>
#include <limits>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)

int main()
{
    Web888DecoderAudio block;
    auto lifetime = std::make_shared<std::atomic<bool>>(true);
    block.lifetime = lifetime; block.captureId = 7; block.generation = 2;
    block.sliceId = 42; block.dialHz = 14074000; block.filterDelaySeconds = 256.0 / 36000;
    Web888DecoderPeriodBuffer buffer;
    block.mono.fill(0.25f, 1000);
    CHECK(!buffer.push(block) && !buffer.armed()); // no inferred arrival-time clock
    CHECK(!buffer.arm(block, 0, 0) && !buffer.arm(block, -1, 180000));
    CHECK(!buffer.arm(block, 0, 1440001));
    // 7.5, 15 and 120 second jobs, with initial partial period discarded.
    for (const int length : {90000, 180000, 1440000}) {
        constexpr int offset = 173;
        const qint64 epoch = qint64(length) * 100000;
        block.firstSampleIndex = 0; block.discontinuity = true;
        CHECK(buffer.arm(block, epoch + offset, length));
        int jobs = 0;
        const quint64 total = quint64(3 * length - offset);
        while (block.firstSampleIndex < total) {
            const int count = int(std::min<quint64>(503 + block.firstSampleIndex % 701, total - block.firstSampleIndex));
            block.mono.resize(count);
            for (int i = 0; i < count; ++i) block.mono[i] = float((block.firstSampleIndex + i) % 101) / 101;
            const auto result = buffer.push(block);
            if (result) {
                ++jobs;
                CHECK(result->startUtcTicks == epoch + qint64(jobs) * length);
                CHECK(result->audio.mono.size() == length && result->audio.current());
                CHECK(result->audio.firstSampleIndex == quint64(jobs * length - offset));
                for (int i = 0; i < length; ++i)
                    CHECK(result->audio.mono[i] == float((result->audio.firstSampleIndex + i) % 101) / 101);
            }
            CHECK(buffer.bufferedSamples() < length);
            block.firstSampleIndex += count; block.discontinuity = false;
        }
        CHECK(jobs == 2 && buffer.bufferedSamples() == 0);
    }
    // All continuity/source failures require a fresh explicit anchor.
    for (int failure = 0; failure < 10; ++failure) {
        block.firstSampleIndex = 100; block.discontinuity = true; block.mono.fill(0.25f, 1000);
        CHECK(buffer.arm(block, 0, 180000));
        CHECK(!buffer.push(block) && buffer.bufferedSamples() == 1000);
        auto bad = block; bad.firstSampleIndex += 1000; bad.discontinuity = false;
        switch (failure) {
        case 0: ++bad.firstSampleIndex; break;
        case 1: bad.discontinuity = true; break;
        case 2: ++bad.captureId; break;
        case 3: ++bad.generation; break;
        case 4: ++bad.sliceId; break;
        case 5: ++bad.dialHz; break;
        case 6: bad.lifetime = std::make_shared<std::atomic<bool>>(true); break;
        case 7: bad.mono[0] = std::numeric_limits<float>::quiet_NaN(); break;
        case 8: bad.mono.resize(6145); break;
        case 9: bad.filterDelaySeconds = 0; break;
        }
        CHECK(!buffer.push(bad) && !buffer.armed() && !buffer.bufferedSamples());
        CHECK(!buffer.push(block));
    }
    block.firstSampleIndex = 0; block.discontinuity = true; block.mono.fill(0.5f, 6000);
    CHECK(buffer.arm(block, 0, 12000));
    CHECK(!buffer.push(block));
    block.firstSampleIndex = 6000; block.discontinuity = false;
    const auto held = buffer.push(block);
    CHECK(held && held->audio.current());
    lifetime->store(false);
    CHECK(!held->audio.current() && !buffer.armed());
    CHECK(!buffer.push(block) && buffer.bufferedSamples() == 0);
    CHECK(!buffer.arm(block, 0, 12000));
    lifetime->store(true);
    CHECK(!buffer.arm(block, std::numeric_limits<qint64>::max(), 12000));
    block.firstSampleIndex = std::numeric_limits<quint64>::max();
    CHECK(buffer.arm(block, 0, 12000));
    CHECK(!buffer.push(block) && !buffer.armed());
    std::puts("Decoder period alignment, content, bounds, gaps and revocation passed");
    return 0;
}
