#include "core/backends/web888/Web888IqProcessor.h"
#include "core/backends/web888/Web888CapturePlan.h"
#include "core/backends/web888/Web888DecoderRouter.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)

bool waitFor(const std::function<bool()>& done, bool deliver = true)
{
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 10000) {
        if (deliver) QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return done();
}

int benchmark()
{
    Web888IqProcessor processor;
    const auto plan = planWeb888Captures(web888DigitalPresets(true, true), 36000, 13, 1);
    CHECK(plan.fitsCapacity && plan.windows.size() == 12);
    QVector<quint64> ids;
    int sliceCount = 0;
    for (const auto& window : plan.windows) {
        QVector<Web888DigitalSlice> slices;
        for (const auto& preset : window.presets) slices.append(preset.slice);
        ids.append(processor.open(slices)); CHECK(ids.last() != 0);
        sliceCount += int(slices.size());
    }
    int completed = 0, errors = 0;
    QObject::connect(&processor, &Web888IqProcessor::processed,
        [&](quint64, double, const auto&, const QString& error) { ++completed; errors += !error.isEmpty(); });
    QElapsedTimer timer; timer.start();
    KiwiIqBlock block; block.sampleRateHz = 36000;
    block.samples.fill(std::complex<float>(0.1f, 0.1f), 1000);
    constexpr int rounds = 180; // five seconds of IQ per capture, accelerated
    for (int round = 0; round < rounds; ++round) {
        block.sequence = round; block.discontinuity = round == 0;
        for (qsizetype n = 0; n < ids.size(); ++n) {
            block.centerHz = plan.windows[n].centerHz;
            CHECK(processor.submit(ids[n], block) == Web888IqProcessor::Submission::Accepted);
        }
        CHECK(waitFor([&] { return completed == (round + 1) * ids.size(); }));
    }
    const auto stats = processor.stats();
    CHECK(errors == 0 && stats.droppedInputs == 0 && stats.droppedResults == 0);
    std::fprintf(stdout,
        "Synthetic benchmark: captures=%lld slices=%d represented_seconds=5 wall_ms=%lld DSP_ms=%.3f peak_inputs=%d peak_results=%d dropped=0\n",
        static_cast<long long>(ids.size()), sliceCount, static_cast<long long>(timer.elapsed()),
        double(stats.processingNanoseconds) / 1e6, stats.peakInputs, stats.peakResults);
    return 0;
}

int pacedBenchmark(bool routePeriods = false)
{
    Web888IqProcessor processor;
    const auto plan = planWeb888Captures(web888DigitalPresets(true, true), 36000, 13, 1);
    CHECK(plan.fitsCapacity && plan.windows.size() == 12);
    QVector<quint64> ids;
    int sliceCount = 0;
    for (const auto& window : plan.windows) {
        QVector<Web888DigitalSlice> slices;
        for (const auto& preset : window.presets) slices.append(preset.slice);
        ids.append(processor.open(slices)); CHECK(ids.last());
        sliceCount += int(slices.size());
    }
    const int seconds = routePeriods ? 121 : 60;
    const int rounds = seconds * 36; // 1000-sample packets at 36 kHz
    int submitted = 0, completed = 0, errors = 0, discontinuities = 0, continuityErrors = 0;
    QHash<int, quint64> nextSample;
    Web888DecoderRouter router;
    auto lifetime = std::make_shared<std::atomic<bool>>(true);
    QHash<int, int> routeJobs;
    QHash<QString, int> modeJobs;
    int peakJobs = 0;
    constexpr qint64 epoch = 1440000LL * 100000;
    if (routePeriods) CHECK(router.configure(web888DigitalPresets(true, true), 128ULL * 1024 * 1024));
    QObject::connect(&processor, &Web888IqProcessor::processed,
        [&](quint64 session, double, const auto& audio, const QString& error) {
            ++completed; errors += !error.isEmpty();
            for (const auto& out : audio) {
                discontinuities += out.discontinuity;
                if (nextSample.contains(out.sliceId) && nextSample.value(out.sliceId) != out.firstSampleIndex)
                    ++continuityErrors;
                if (routePeriods) {
                    auto routed = out; routed.captureId = session; routed.lifetime = lifetime;
                    if (!nextSample.contains(out.sliceId)) errors += !router.arm(routed, epoch);
                    errors += !router.push(routed);
                }
                nextSample[out.sliceId] = out.firstSampleIndex + quint64(out.mono.size());
            }
            if (routePeriods) {
                peakJobs = std::max(peakJobs, router.queuedJobs());
                while (auto job = router.takeNext()) {
                    const int size = job->mode == "FT8" ? 180000 : job->mode == "FT4" ? 90000 : 1440000;
                    const int id = job->period.audio.sliceId;
                    errors += !job->current() || job->period.audio.mono.size() != size
                        || job->period.startUtcTicks != epoch + qint64(routeJobs[id]) * size;
                    ++routeJobs[id]; ++modeJobs[job->mode];
                }
            }
        });
    QEventLoop loop;
    QElapsedTimer clock; clock.start();
    QTimer producer;
    producer.setTimerType(Qt::PreciseTimer);
    producer.setInterval(1);
    KiwiIqBlock block; block.sampleRateHz = 36000;
    block.samples.fill(std::complex<float>(0.1f, 0.1f), 1000);
    int round = 0;
    qint64 maxLatenessNs = 0;
    bool timedOut = false;
    QObject::connect(&producer, &QTimer::timeout, [&] {
        const qint64 now = clock.nsecsElapsed();
        // Follow elapsed time, not completion of previous work. Delays produce
        // catch-up bursts and expose real queue pressure instead of slowing input.
        while (round < rounds && now >= (qint64(round) + 1) * 1000000000LL / 36) {
            maxLatenessNs = std::max(maxLatenessNs, now - (qint64(round) + 1) * 1000000000LL / 36);
            block.sequence = round; block.discontinuity = round == 0;
            for (qsizetype n = 0; n < ids.size(); ++n) {
                block.centerHz = plan.windows[n].centerHz;
                submitted += processor.submit(ids[n], block) == Web888IqProcessor::Submission::Accepted;
            }
            ++round;
        }
        const auto stats = processor.stats();
        if (round == rounds && !stats.queuedInputs && !stats.processingInputs && !stats.queuedResults) loop.quit();
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [&] { timedOut = true; loop.quit(); });
    deadline.start((seconds + 5) * 1000);
    producer.start();
    loop.exec();
    producer.stop(); deadline.stop();
    const auto stats = processor.stats();
    std::fprintf(stdout,
        "Paced synthetic run: captures=%lld slices=%d represented_seconds=%d wall_ms=%lld DSP_ms=%.3f max_producer_lateness_ms=%.3f submitted=%d completed=%d input_drops=%llu result_drops=%llu discontinuities=%d continuity_errors=%d peak_inputs=%d peak_results=%d\n",
        static_cast<long long>(ids.size()), sliceCount, seconds, static_cast<long long>(clock.elapsed()),
        double(stats.processingNanoseconds) / 1e6, double(maxLatenessNs) / 1e6, submitted, completed,
        static_cast<unsigned long long>(stats.droppedInputs), static_cast<unsigned long long>(stats.droppedResults),
        discontinuities, continuityErrors, stats.peakInputs, stats.peakResults);
    CHECK(!timedOut && errors == 0 && !stats.droppedInputs && !stats.droppedResults);
    CHECK(completed == rounds * ids.size() && submitted == completed);
    CHECK(discontinuities == sliceCount && continuityErrors == 0);
    CHECK(nextSample.size() == sliceCount && stats.peakInputs <= 32 && stats.peakResults <= 32);
    if (routePeriods) {
        std::fprintf(stdout, "Routed periods: FT8=%d FT4=%d WSPR=%d routes=%lld dropped_jobs=%llu peak_jobs=%d reserved_sample_bytes=%llu\n",
            modeJobs["FT8"], modeJobs["FT4"], modeJobs["WSPR"], static_cast<long long>(routeJobs.size()),
            static_cast<unsigned long long>(router.droppedJobs()), peakJobs,
            static_cast<unsigned long long>(router.reservedSampleBytes()));
        CHECK(modeJobs["FT8"] == 64 && modeJobs["FT4"] == 128 && modeJobs["WSPR"] == 9);
        CHECK(routeJobs.size() == 25 && !router.droppedJobs() && !router.queuedJobs());
    }
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 2 && QByteArray(argv[1]) == "--benchmark") return benchmark();
    if (argc == 2 && QByteArray(argv[1]) == "--paced") return pacedBenchmark();
    if (argc == 2 && QByteArray(argv[1]) == "--paced-routes") return pacedBenchmark(true);
    Web888IqProcessor processor;
    QVector<Web888DigitalSlice> slices;
    for (int i = 0; i < 16; ++i) slices.append({i, 14074000, 0, 3000});
    const auto session = processor.open(slices);
    CHECK(session != 0);
    bool ownerThread = true;
    int completions = 0, errors = 0;
    QVector<Web888DecoderAudio> last;
    QObject::connect(&processor, &Web888IqProcessor::processed,
        [&](quint64, double, const auto& audio, const QString& error) {
            ownerThread &= QThread::currentThread() == app.thread();
            ++completions; last = audio; errors += !error.isEmpty();
        });
    KiwiIqBlock block; block.centerHz = 14075500; block.sampleRateHz = 36000;
    block.samples.fill(std::complex<float>(0.1f, 0.1f), 4096);
    quint64 accepted = 0;
    for (int i = 0; i < 1000; ++i) {
        block.sequence = i; block.discontinuity = i == 0;
        accepted += processor.submit(session, block) == Web888IqProcessor::Submission::Accepted;
    }
    CHECK(processor.stats().droppedInputs > 0 && processor.stats().peakInputs <= 2);
    // Do not run the owner event loop: completed audio must remain bounded.
    CHECK(waitFor([&] { return processor.stats().processedBlocks == accepted; }, false));
    CHECK(completions == 0 && processor.stats().queuedResults <= 2);
    block.sequence = 1000; block.discontinuity = false;
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Accepted);
    CHECK(waitFor([&] { return processor.stats().processedBlocks == accepted + 1; }, false));
    CHECK(processor.stats().droppedResults > 0 && processor.stats().peakResults <= 2);
    CHECK(waitFor([&] { return completions > 0; }));
    const int previous = completions;
    block.sequence = 1001;
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Accepted);
    CHECK(waitFor([&] { return completions > previous; }));
    CHECK(ownerThread && errors == 0 && last.size() == 16);
    for (const auto& audio : last) CHECK(audio.discontinuity);

    // Close must invalidate both pending and already-computed results.
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Accepted);
    processor.close(session);
    const auto countAtClose = completions;
    CHECK(processor.stats().sessions == 0 && processor.stats().queuedInputs == 0 && processor.stats().queuedResults == 0);
    QCoreApplication::processEvents();
    CHECK(completions == countAtClose);
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Inactive);
    CHECK(waitFor([&] { return processor.stats().processingInputs == 0; }, false));

    const auto second = processor.open({{1, 14074000, 0, 3000}});
    CHECK(second > session);
    block.discontinuity = true;
    for (int i = 0; i < 2; ++i) {
        const auto before = processor.stats().processedBlocks;
        block.sequence = i;
        CHECK(processor.submit(second, block) == Web888IqProcessor::Submission::Accepted);
        CHECK(waitFor([&] { return processor.stats().processedBlocks > before; }, false));
    }
    CHECK(processor.stats().queuedResults == 2);
    const auto beforeError = processor.stats().processedBlocks;
    block.sampleRateHz = 0; // terminal error must bypass a full result queue
    CHECK(processor.submit(second, block) == Web888IqProcessor::Submission::Accepted);
    CHECK(waitFor([&] { return processor.stats().processedBlocks > beforeError; }, false));
    CHECK(waitFor([&] { return errors == 1; }));
    CHECK(last.isEmpty());
    processor.close(second);

    // Many independent sessions test the global bound, not just a single source.
    QVector<quint64> ids;
    for (int i = 0; i < 64; ++i) { ids.append(processor.open(slices)); CHECK(ids.last() != 0); }
    CHECK(processor.open(slices) == 0);
    block.sampleRateHz = 36000;
    for (const auto id : ids) processor.submit(id, block);
    CHECK(processor.stats().peakInputs <= 32 && processor.stats().peakResults <= 32);
    for (const auto id : ids) processor.close(id);
    CHECK(processor.stats().sessions == 0 && processor.stats().queuedInputs == 0 && processor.stats().queuedResults == 0);
    const auto stats = processor.stats();
    {
        Web888IqProcessor nested;
        const auto id = nested.open({{1, 14074000, 0, 3000}});
        int depth = 0, maxDepth = 0, calls = 0;
        bool admitted = false, queued = false;
        QObject::connect(&nested, &Web888IqProcessor::processed,
            [&](quint64, double, const auto&, const QString&) {
                ++depth; maxDepth = std::max(maxDepth, depth);
                if (++calls == 1) {
                    block.sequence = 2;
                    admitted = nested.submit(id, block) == Web888IqProcessor::Submission::Accepted;
                    queued = waitFor([&] { return nested.stats().queuedResults == 1; }, false);
                    QCoreApplication::processEvents(); // must not recurse into another audio batch
                }
                --depth;
            });
        CHECK(nested.submit(id, block) == Web888IqProcessor::Submission::Accepted);
        CHECK(waitFor([&] { return calls == 2; }));
        CHECK(admitted && queued && maxDepth == 1);
    }
    std::fprintf(stdout, "Bounded IQ worker passed: input drops=%llu result drops=%llu peak inputs=%d peak results=%d\n",
        static_cast<unsigned long long>(stats.droppedInputs), static_cast<unsigned long long>(stats.droppedResults),
        stats.peakInputs, stats.peakResults);
    return 0;
}
