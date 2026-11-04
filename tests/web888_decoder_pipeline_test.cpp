#include "core/backends/web888/Web888IqProcessor.h"
#include "core/backends/web888/Web888DecoderRouter.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QThread>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)
constexpr double tau = 2 * std::numbers::pi;

double amplitude(const QVector<float>& samples, double hz)
{
    std::complex<double> sum{};
    // A bounded one-second measurement, away from either period edge.
    for (int n = 12000; n < 24000; ++n)
        sum += double(samples[n]) * std::polar(1.0, -tau * hz * n / 12000);
    return 2 * std::abs(sum) / 12000;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QVector<Web888DigitalPreset> presets;
    for (const auto& preset : web888DigitalPresets(true))
        if (preset.band == "30m") presets.append(preset);
    CHECK(presets.size() == 3);
    const auto plan = planWeb888Captures(presets, 36000, 2, 1);
    CHECK(plan.fitsCapacity && plan.windows.size() == 1);
    QVector<Web888DigitalSlice> slices;
    QHash<int, QString> modes;
    for (const auto& preset : presets) { slices.append(preset.slice); modes[preset.slice.id] = preset.mode; }
    Web888IqProcessor processor;
    Web888DecoderRouter router;
    CHECK(router.configure(presets, 16 * 1024 * 1024));
    const auto session = processor.open(slices); CHECK(session);
    auto lifetime = std::make_shared<std::atomic<bool>>(true);
    int completed = 0, failures = 0, routingFailures = 0;
    bool ownerThread = true;
    QSet<int> anchored;
    // A fixture-defined epoch for the first post-filter sample of every slice.
    // This does not infer UTC from real packet arrival or receiver GPS fields.
    constexpr qint64 epoch = 1440000LL * 100000;
    QObject::connect(&processor, &Web888IqProcessor::processed,
        [&](quint64 id, double rate, const auto& outputs, const QString& error) {
            ++completed;
            ownerThread &= QThread::currentThread() == app.thread();
            failures += id != session || rate != 36000 || !error.isEmpty();
            for (auto audio : outputs) {
                audio.captureId = 1; audio.lifetime = lifetime;
                if (!anchored.contains(audio.sliceId)) {
                    Web888DecoderClockAnchor anchor;
                    anchor.source = Web888DecoderClockAnchor::Source::VerifiedFixture;
                    anchor.captureId = audio.captureId; anchor.generation = audio.generation;
                    anchor.firstSampleIndex = audio.firstSampleIndex;
                    anchor.firstSampleUtcTicks = epoch;
                    anchor.filterDelaySeconds = audio.filterDelaySeconds;
                    anchor.validUntilSampleIndex = audio.firstSampleIndex + 12000 * 240;
                    anchor.maximumErrorTicks = 0; anchor.lifetime = lifetime;
                    failures += !router.arm(audio, anchor);
                    anchored.insert(audio.sliceId);
                }
                routingFailures += !router.push(audio);
            }
        });
    KiwiIqBlock block; block.sampleRateHz = 36000; block.centerHz = plan.windows[0].centerHz;
    block.generation = 1; block.samples.resize(3600);
    QHash<QString, int> jobs;
    QHash<QString, Web888DecoderJob> retained;
    for (int round = 0; round < 1210; ++round) { // 121 represented seconds, accelerated
        block.sequence = round; block.discontinuity = round == 0;
        for (int n = 0; n < block.samples.size(); ++n) {
            const double t = (qint64(round) * 3600 + n) / 36000.0;
            block.samples[n] = std::complex<float>(
                0.20 * std::polar(1.0, tau * (10137000 - block.centerHz) * t)
                + 0.15 * std::polar(1.0, tau * (10141100 - block.centerHz) * t)
                + 0.10 * std::polar(1.0, tau * (10140200 - block.centerHz) * t));
        }
        CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Accepted);
        QElapsedTimer wait; wait.start();
        while (completed != round + 1 && wait.elapsed() < 10000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        CHECK(completed == round + 1 && !failures && !routingFailures);
        while (auto job = router.takeNext()) {
            CHECK(job->current() && job->mode == modes[job->period.audio.sliceId]);
            const int size = job->mode == "FT8" ? 180000 : job->mode == "FT4" ? 90000 : 1440000;
            CHECK(job->period.audio.mono.size() == size);
            CHECK(job->period.startUtcTicks == epoch + qint64(jobs[job->mode]) * size);
            const double hz = job->mode == "FT8" ? 1000 : job->mode == "FT4" ? 1100 : 1500;
            const double level = job->mode == "FT8" ? 0.20 : job->mode == "FT4" ? 0.15 : 0.10;
            CHECK(std::abs(amplitude(job->period.audio.mono, hz) - level) < 0.015);
            ++jobs[job->mode]; retained[job->mode] = std::move(*job);
        }
    }
    CHECK(jobs["FT8"] == 8 && jobs["FT4"] == 16 && jobs["WSPR"] == 1);
    CHECK(ownerThread && router.droppedJobs() == 0 && router.queuedJobs() == 0);
    CHECK(!processor.stats().droppedInputs && !processor.stats().droppedResults);
    // A transport sequence gap flows through the worker and invalidates all routes.
    block.sequence += 2; block.discontinuity = false;
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Accepted);
    QElapsedTimer wait; wait.start();
    while (completed != 1211 && wait.elapsed() < 10000) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    CHECK(completed == 1211 && routingFailures == 3 && !failures);
    for (const auto& job : retained) CHECK(!job.current());
    processor.close(session); lifetime->store(false);
    CHECK(processor.submit(session, block) == Web888IqProcessor::Submission::Inactive);
    std::puts("Shared IQ -> worker -> three routed modes: 25 complete tone periods, no drops, gap revocation passed");
    return 0;
}
