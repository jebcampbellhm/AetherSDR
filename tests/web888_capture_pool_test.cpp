#include "TestSettingsProfile.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888CapturePool.h"
#include "core/backends/web888/Web888IqCapture.h"
#include "core/backends/web888/Web888IqProcessor.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)

// Injection into real capture processing; no HTTP request, socket or firmware peer.
class OfflineClient : public KiwiSdrClient {
public:
    explicit OfflineClient(QObject* parent)
        : KiwiSdrClient(Streams::SoundOnly, Admission::IndependentReceiver, SoundFormat::FilteredIq, parent) {}
#ifdef HAVE_WEBSOCKETS
protected:
    void startStatusPreflight(const QUrl&) override {}
#endif
};
class Capture : public Web888IqCapture {
public:
    Capture(Web888IqProcessor* worker, QObject* parent) : Web888IqCapture(worker, parent), processor(worker) {}
    Web888IqProcessor* processor{};
    OfflineClient* client{};
    double center{};
    quint32 sequence{};
    void data(double rate = 36000, int count = 1000)
    {
        if (!running()) return;
        const bool active = running();
        bool completed = false;
        const auto connection = QObject::connect(processor, &Web888IqProcessor::processed,
            QCoreApplication::instance(), [&](quint64, double, const auto&, const QString&) { completed = true; });
        KiwiIqBlock block; block.centerHz = center; block.sampleRateHz = rate;
        block.sequence = sequence++; block.discontinuity = sequence == 1;
        block.samples.fill(std::complex<float>(0.1f, 0.1f), count);
        emit client->filteredIqReady(block);
        QElapsedTimer timer; timer.start();
        while (active && !completed && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        QObject::disconnect(connection);
        if (active && !completed) { std::fprintf(stderr, "Timed out waiting for test DSP block\n"); std::abort(); }
    }
    void refuse() { emit client->stateChanged(KiwiSdrClient::State::Busy, "No free receiver"); }
protected:
    KiwiSdrClient* createClient() override
    {
        client = new OfflineClient(this);
        QObject::connect(client, &KiwiSdrClient::trackedSliceChanged, this,
            [this](int, double mhz, const QString&, int, int, const QString&) { center = mhz * 1e6; });
        return client;
    }
};
class Pool : public Web888CapturePool {
public:
    QVector<QPointer<Capture>> opened;
protected:
    Web888IqCapture* createCapture(Web888IqProcessor* processor) override
    {
        auto* c = new Capture(processor, this); opened.append(c); return c;
    }
};

int main(int argc, char** argv)
{
    TestSettingsProfile settings(QStringLiteral("web888-capture-pool"));
    CHECK(settings.isValid());
    QCoreApplication app(argc, argv);
#ifndef HAVE_WEBSOCKETS
    return 0; // lifecycle requires the transport build; pure planner covered separately
#else
    const QString endpoint = QStringLiteral("receiver.invalid:8073");
    const Web888DigitalPreset a{"80m", "FT8", {0, 3573000, 0, 3000}};
    const Web888DigitalPreset b{"40m", "FT8", {10, 7074000, 0, 3000}};
    const Web888DigitalPreset c{"20m", "FT8", {20, 14074000, 0, 3000}};
    Pool pool;
    int ready = 0, failed = 0, captureFailed = 0;
    QVector<Web888DecoderAudio> audio;
    QObject::connect(&pool, &Web888CapturePool::profileReady, [&] { ++ready; });
    QObject::connect(&pool, &Web888CapturePool::profileFailed, [&](const QString&) { ++failed; });
    QObject::connect(&pool, &Web888CapturePool::captureFailed, [&](quint64, const QString&) { ++captureFailed; });
    QObject::connect(&pool, &Web888CapturePool::decoderAudioReady, [&](const auto& out) { audio.append(out); });

    CHECK(!pool.applyProfile(endpoint, {a, b}, 36000, 2, 1));
    CHECK(pool.opened.isEmpty() && failed == 1); // listening reservation enforced
    CHECK(pool.applyProfile(endpoint, {a}, 36000, 4, 1));
    CHECK(pool.activeCount() == 0 && pool.pendingCount() == 1 && ready == 0);
    pool.opened[0]->data(36000, 100); // valid IQ confirms coverage, even before FIR output
    CHECK(pool.activeCount() == 1 && pool.pendingCount() == 0 && ready == 1);
    CHECK(audio.isEmpty());
    const auto originalId = pool.activeIds()[0];
    pool.opened[0]->data();
    CHECK(audio.size() == 1 && audio.last().current() && audio.last().discontinuity);
    CHECK(audio.last().captureId == originalId);
    const auto held = audio.last();

    CHECK(pool.applyProfile(endpoint, {a}, 36000, 4, 1));
    CHECK(pool.opened.size() == 1 && ready == 2 && held.current()); // exact reuse
    CHECK(!pool.applyProfile("another.invalid", {a}, 36000, 4, 1));
    CHECK(pool.activeIds()[0] == originalId && held.current());

    // Acquiring B and C uses the full transitional budget; A stays on air.
    CHECK(pool.applyProfile(endpoint, {a, b, c}, 36000, 4, 1));
    CHECK(pool.pendingCount() == 2 && pool.activeCount() == 1);
    CHECK(!pool.applyProfile(endpoint, {a}, 36000, 4, 1)); // no overlapping transaction
    const int before = audio.size();
    pool.opened[1]->data(); CHECK(audio.size() == before && ready == 2);
    pool.opened[0]->data(); CHECK(audio.size() == before + 1 && held.current());
    pool.opened[2]->refuse(); // rollback both new captures, not A
    CHECK(pool.pendingCount() == 0 && !pool.changingProfile());
    CHECK(pool.activeIds() == QVector<quint64>{originalId} && held.current());
    if (pool.opened[1]) pool.opened[1]->data();
    CHECK(audio.size() == before + 1); // stale source ignored or already destroyed

    CHECK(pool.applyProfile(endpoint, {a, b}, 36000, 4, 1));
    pool.opened.last()->data();
    CHECK(pool.activeCount() == 2 && ready == 3 && held.current());
    const auto idB = pool.activeIds()[1];
    CHECK(idB > originalId && audio.last().captureId == idB && audio.last().discontinuity);
    const auto heldB = audio.last();
    // Final profile C fits two slots, but retaining A+B while opening C does not.
    const auto openedBefore = pool.opened.size();
    CHECK(!pool.applyProfile(endpoint, {c}, 36000, 3, 1));
    CHECK(pool.opened.size() == openedBefore && held.current() && heldB.current());
    CHECK(pool.applyProfile(endpoint, {b}, 36000, 2, 1)); // removal can reduce to a lower budget
    CHECK(!held.current() && heldB.current() && pool.activeIds() == QVector<quint64>{idB});

    CHECK(pool.applyProfile(endpoint, {b, c}, 36000, 3, 1));
    auto timeoutCapture = pool.opened.last();
    auto* timer = pool.findChild<QTimer*>("captureAdmissionDeadline"); CHECK(timer && timer->isActive());
    CHECK(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    CHECK(!pool.changingProfile() && pool.pendingCount() == 0 && heldB.current());
    if (timeoutCapture) timeoutCapture->data();
    CHECK(pool.activeCount() == 1);

    // Losing a committed lease affects only that lease. A change relying on it
    // is rolled back; no identity is renumbered and no stale audio survives.
    CHECK(pool.applyProfile(endpoint, {b, c}, 36000, 3, 1));
    pool.opened[3]->refuse(); // B was the fourth allocated capture
    CHECK(captureFailed == 1 && pool.activeCount() == 0 && pool.pendingCount() == 0);
    CHECK(!heldB.current());

    CHECK(pool.applyProfile(endpoint, {a, b}, 36000, 3, 1));
    auto penultimate = pool.opened[pool.opened.size() - 2];
    auto last = pool.opened.last();
    penultimate->data();
    bool stopAtCommit = false;
    const auto connection = QObject::connect(&pool, &Web888CapturePool::profileReady, [&] {
        stopAtCommit = true; pool.stop();
    });
    const int priorAudio = audio.size();
    last->data();
    CHECK(stopAtCommit && pool.activeCount() == 0 && pool.pendingCount() == 0);
    CHECK(audio.size() == priorAudio); // no first PCM escapes after reentrant stop
    QObject::disconnect(connection);

    // 12 kHz can no longer cover this requested two-mode 20m window.
    const QVector<Web888DigitalPreset> wide{c, {"20m", "WSPR", {21, 14095600, 1400, 1600}}};
    CHECK(pool.applyProfile(endpoint, wide, 36000, 2, 1));
    pool.opened.last()->data(12000);
    CHECK(pool.activeCount() == 0 && pool.pendingCount() == 0);
    CHECK(!pool.changingProfile());

    CHECK(pool.applyProfile(endpoint, {a, b}, 36000, 3, 1));
    auto surviving = pool.opened[pool.opened.size() - 2];
    auto failing = pool.opened.last();
    surviving->data(); failing->data();
    surviving->data();
    const auto heldSurvivor = audio.last();
    CHECK(heldSurvivor.sliceId == a.slice.id && heldSurvivor.current());
    failing->refuse();
    CHECK(pool.activeCount() == 1 && heldSurvivor.current() && captureFailed == 2);
    const auto survivorId = pool.activeIds()[0];
    surviving->data(); CHECK(audio.last().captureId == survivorId);
    CHECK(pool.applyProfile(endpoint, {}, 36000, 1, 1));
    CHECK(pool.activeCount() == 0 && !heldSurvivor.current());
    CHECK(pool.applyProfile("another.invalid:8073", {a}, 36000, 2, 1));
    pool.stop();
    CHECK(pool.pendingCount() == 0 && !pool.changingProfile());
    const int failuresBeforeStart = failed;
    CHECK(!pool.applyProfile(endpoint, {a, b}, 36000, 3, 1, QString(300, QLatin1Char('x'))));
    CHECK(failed == failuresBeforeStart + 1); // synchronous refusal reports once
    CHECK(pool.activeCount() == 0 && pool.pendingCount() == 0 && !pool.changingProfile());
    std::fprintf(stdout, "Capture admission, reuse, capacity, rollback, timeout and lifetime checks passed\n");
    return 0;
#endif
}
