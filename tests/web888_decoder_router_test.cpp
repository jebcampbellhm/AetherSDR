#include "core/backends/web888/Web888DecoderRouter.h"
#include <cstdio>
#include <limits>
#include <QSet>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)

bool feed(Web888DecoderRouter& router, Web888DecoderAudio& block, int samples)
{
    for (int n = 0; n < samples; n += 6000) {
        block.mono.fill(0.25f, std::min(6000, samples - n));
        if (!router.push(block)) return false;
        block.firstSampleIndex += block.mono.size(); block.discontinuity = false;
    }
    return true;
}

int main()
{
    Web888DecoderRouter router;
    const auto full = web888DigitalPresets(true, true);
    CHECK(full.size() == 25);
    CHECK(!router.configure(full, 64ULL * 1024 * 1024));
    CHECK(router.configure(full, 128ULL * 1024 * 1024));
    CHECK(router.reservedSampleBytes() == 120960000);
    CHECK(!router.takeNext());
    const QVector<Web888DigitalPreset> routes{{"20m", "FT4", {1, 14080000}},
        {"20m", "FT8", {2, 14074000}}};
    constexpr quint64 budget = (90000 + 180000) * sizeof(float) * 2;
    CHECK(router.configure(routes, budget));
    Web888DecoderAudio a, b;
    auto lifeA = std::make_shared<std::atomic<bool>>(true);
    auto lifeB = std::make_shared<std::atomic<bool>>(true);
    a.lifetime = lifeA; a.sliceId = 1; a.dialHz = 14080000; a.captureId = 4;
    b.lifetime = lifeB; b.sliceId = 2; b.dialHz = 14074000; b.captureId = 5;
    CHECK(!router.push(a)); // never infer a clock anchor
    CHECK(router.arm(a, 0) && router.arm(b, 0));
    CHECK(feed(router, a, 90000) && feed(router, b, 180000));
    CHECK(router.queuedJobs() == 2);
    CHECK(!router.configure(routes, budget - 1)); // rejected edits preserve current work
    auto duplicates = routes; duplicates.append(routes[0]);
    CHECK(!router.configure(duplicates, budget * 2));
    auto unknown = routes; unknown[0].mode = "unknown";
    CHECK(!router.configure(unknown, budget));
    for (int invalid = 0; invalid < 6; ++invalid) {
        auto bad = routes;
        switch (invalid) {
        case 0: bad[0].slice.id = -1; break;
        case 1: bad[0].slice.lowHz = std::numeric_limits<double>::quiet_NaN(); break;
        case 2: bad[0].slice.highHz = std::numeric_limits<double>::infinity(); break;
        case 3: bad[0].slice.lowHz = -1; break;
        case 4: bad[0].slice.highHz = 5001; break;
        case 5: bad[0].slice.highHz = 99; break;
        }
        QString error;
        CHECK(!router.configure(bad, budget, &error) && !error.isEmpty());
        CHECK(router.queuedJobs() == 2 && router.reservedSampleBytes() == budget);
    }
    CHECK(router.queuedJobs() == 2 && router.reservedSampleBytes() == budget);
    CHECK(feed(router, a, 90000)); // latest complete job replaces older waiting period
    CHECK(router.droppedJobs() == 1 && router.queuedJobs() == 2);
    auto first = router.takeNext();
    CHECK(first && first->current() && first->mode == "FT4" && first->period.startUtcTicks == 90000);
    CHECK(feed(router, a, 90000));
    auto second = router.takeNext(); // round robin prevents the busier FT4 route starving FT8
    CHECK(second && second->mode == "FT8" && second->current());
    lifeB->store(false);
    CHECK(!second->current());
    // Reanchoring invalidates already-taken jobs even while the hardware source lives.
    CHECK(router.arm(a, 270000));
    CHECK(!first->current() && router.queuedJobs() == 0);
    CHECK(feed(router, a, 90000));
    auto anchored = router.takeNext(); CHECK(anchored && anchored->current());
    ++a.firstSampleIndex;
    CHECK(!router.push(a) && !anchored->current());
    CHECK(!router.takeNext());
    CHECK(router.arm(a, 360000) && feed(router, a, 90000));
    auto replaced = router.takeNext(); CHECK(replaced && replaced->current());
    CHECK(router.configure(routes, budget));
    CHECK(!replaced->current() && router.queuedJobs() == 0);
    CHECK(router.arm(a, 450000) && feed(router, a, 90000));
    lifeA->store(false);
    CHECK(router.queuedJobs() == 0 && !router.takeNext());
    router.clear(); CHECK(router.reservedSampleBytes() == 0);
    std::optional<Web888DecoderJob> afterDestruction;
    {
        Web888DecoderRouter crossing;
        CHECK(crossing.configure({routes[0]}, 90000 * sizeof(float) * 2));
        a.lifetime = std::make_shared<std::atomic<bool>>(true);
        a.firstSampleIndex = 0; a.discontinuity = true;
        CHECK(crossing.arm(a, 0));
        CHECK(feed(crossing, a, 90000));
        CHECK(feed(crossing, a, 89999));
        CHECK(crossing.queuedJobs() == 1 && crossing.droppedJobs() == 0);
        CHECK(feed(crossing, a, 6000)); // complete a replacement and begin the next period
        CHECK(crossing.queuedJobs() == 1 && crossing.droppedJobs() == 1);
        afterDestruction = crossing.takeNext();
        CHECK(afterDestruction && afterDestruction->current());
        CHECK(afterDestruction->period.startUtcTicks == 90000);
    }
    CHECK(!afterDestruction->current());
    // Loss of timing on one capture must revoke its taken/waiting jobs while
    // other captures continue. The source itself can remain current.
    CHECK(router.configure(routes, budget));
    a.lifetime = std::make_shared<std::atomic<bool>>(true);
    b.lifetime = std::make_shared<std::atomic<bool>>(true);
    a.firstSampleIndex = b.firstSampleIndex = 0;
    a.discontinuity = b.discontinuity = true;
    CHECK(router.arm(a, 0) && router.arm(b, 0));
    CHECK(feed(router, a, 90000) && feed(router, b, 180000));
    auto affected = router.takeNext(); CHECK(affected && affected->mode == "FT4");
    CHECK(feed(router, a, 90000));
    CHECK(router.disarmCapture(a.captureId) == 1);
    CHECK(a.current() && !affected->current());
    CHECK(router.queuedJobs() == 1 && router.reservedSampleBytes() == budget);
    CHECK(router.disarmCapture(a.captureId) == 0 && router.disarmCapture(999) == 0);
    auto survivor = router.takeNext(); CHECK(survivor && survivor->mode == "FT8" && survivor->current());
    CHECK(!router.push(a)); // no implicit clock recovery
    CHECK(feed(router, b, 180000));
    auto continued = router.takeNext(); CHECK(continued && continued->current());
    CHECK(router.arm(a, 180000) && feed(router, a, 90000));
    auto resumed = router.takeNext(); CHECK(resumed && resumed->current());
    // Multiple slices sharing one hardware capture are invalidated together.
    b.captureId = a.captureId;
    CHECK(router.arm(b, 360000));
    CHECK(router.disarmCapture(a.captureId) == 2);
    CHECK(!router.push(a) && !router.push(b) && router.queuedJobs() == 0);
    // Four minutes of audio on every route with no consumer: retain one latest
    // period per route, not every completed period or a growing dispatch queue.
    Web888DecoderRouter stalledConsumer;
    CHECK(stalledConsumer.configure(full, 128ULL * 1024 * 1024));
    auto fullLifetime = std::make_shared<std::atomic<bool>>(true);
    for (const auto& preset : full) {
        Web888DecoderAudio audio;
        audio.sliceId = preset.slice.id; audio.dialHz = preset.slice.dialHz;
        audio.captureId = 77; audio.lifetime = fullLifetime;
        CHECK(stalledConsumer.arm(audio, 0));
        CHECK(feed(stalledConsumer, audio, 240 * 12000));
        CHECK(stalledConsumer.queuedJobs() <= 25);
    }
    CHECK(stalledConsumer.queuedJobs() == 25 && stalledConsumer.droppedJobs() == 377);
    CHECK(stalledConsumer.reservedSampleBytes() == 120960000);
    QSet<int> received;
    quint64 retainedSamples = 0;
    while (auto job = stalledConsumer.takeNext()) {
        CHECK(job->current() && !received.contains(job->period.audio.sliceId));
        received.insert(job->period.audio.sliceId);
        const int samples = job->mode == "FT8" ? 180000 : job->mode == "FT4" ? 90000 : 1440000;
        CHECK(job->period.audio.mono.size() == samples);
        CHECK(job->period.startUtcTicks == 240 * 12000 - samples);
        CHECK(job->period.audio.firstSampleIndex == quint64(240 * 12000 - samples));
        retainedSamples += job->period.audio.mono.size();
    }
    CHECK(received.size() == 25 && retainedSamples == 15120000 && !stalledConsumer.queuedJobs());
    CHECK(stalledConsumer.disarmCapture(77) == 25);
    // Qualified anchors bind the exact post-filter stream and have finite coverage.
    CHECK(router.configure({routes[0]}, budget));
    a.firstSampleIndex = 0; a.discontinuity = true;
    a.lifetime = std::make_shared<std::atomic<bool>>(true);
    auto clockLife = std::make_shared<std::atomic<bool>>(true);
    Web888DecoderClockAnchor anchor;
    anchor.source = Web888DecoderClockAnchor::Source::VerifiedFixture;
    anchor.captureId = a.captureId; anchor.generation = a.generation;
    anchor.filterDelaySeconds = a.filterDelaySeconds;
    anchor.validUntilSampleIndex = 180000;
    anchor.maximumErrorTicks = 1; anchor.lifetime = clockLife;
    for (int invalid = 0; invalid < 10; ++invalid) {
        auto bad = anchor;
        switch (invalid) {
        case 0: bad.source = Web888DecoderClockAnchor::Source::Unqualified; break;
        case 1: ++bad.captureId; break;
        case 2: ++bad.generation; break;
        case 3: ++bad.firstSampleIndex; break;
        case 4: bad.filterDelaySeconds = 0.25; break;
        case 5: bad.maximumErrorTicks = -1; break;
        case 6: bad.maximumErrorTicks = 1201; break;
        case 7: bad.validUntilSampleIndex = 89999; break;
        case 8: bad.lifetime.reset(); break;
        case 9: bad.firstSampleUtcTicks = -1; break;
        }
        CHECK(!router.arm(a, bad) && !router.push(a));
    }
    CHECK(router.arm(a, anchor) && feed(router, a, 90000));
    auto clockJob = router.takeNext(); CHECK(clockJob && clockJob->current());
    clockLife->store(false); // revoke even a job already handed to the consumer
    CHECK(!clockJob->current() && !router.push(a));
    CHECK(!router.arm(a, anchor));
    clockLife->store(true);
    a.firstSampleIndex = 0; a.discontinuity = true;
    CHECK(router.arm(a, anchor) && feed(router, a, 180000));
    auto expires = router.takeNext(); CHECK(expires && expires->current());
    CHECK(!feed(router, a, 6000) && !expires->current());
    a.firstSampleIndex = 0;
    auto partial = anchor; partial.firstSampleUtcTicks = 1;
    partial.validUntilSampleIndex = 90000;
    CHECK(!router.arm(a, partial)); // horizon must include discarded partial period
    partial.validUntilSampleIndex = 180000;
    CHECK(router.arm(a, partial));
    std::puts("Decoder routing budgets, fairness, replacement, gaps and revocation passed");
    return 0;
}
