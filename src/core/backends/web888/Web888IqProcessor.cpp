#include "Web888IqProcessor.h"
#include <QThread>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace AetherSDR {
struct Web888IqProcessor::Impl {
    static constexpr int maxSessions = 64, queueLimit = 32, perSessionLimit = 2;
    struct Session {
        quint64 id{};
        Web888IqSplitter splitter; // worker-only after open()
        bool active{true}, inputGap{}, outputGap{}, failed{}; // mutex-protected
        QString terminalError;
        double errorRate{};
        int inputs{}, results{};
    };
    struct Job { std::shared_ptr<Session> session; KiwiIqBlock block; };
    struct Result {
        std::shared_ptr<Session> session;
        double rate{};
        QVector<Web888DecoderAudio> audio;
        QString error;
    };
    Web888IqProcessor& owner;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<quint64, std::shared_ptr<Session>> sessions;
    std::deque<Job> jobs;
    std::deque<Result> results;
    Stats counters;
    quint64 nextId{1};
    bool stopping{}, drainPosted{};
    bool draining{}; // owner-thread only; prevents nested event-loop delivery
    std::thread worker;

    explicit Impl(Web888IqProcessor& p) : owner(p), worker([this] { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            for (auto& [id, session] : sessions) session->active = false;
            jobs.clear(); results.clear(); sessions.clear();
        }
        wake.notify_one();
        worker.join(); // at most one bounded block can still be processing
    }
    void run()
    {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping) return;
                job = std::move(jobs.front()); jobs.pop_front();
                --job.session->inputs;
                if (!job.session->active || job.session->failed) continue;
                counters.processingInputs = 1;
            }
            Result result; result.session = job.session; result.rate = job.block.sampleRateHz;
            const auto start = std::chrono::steady_clock::now();
            result.audio = job.session->splitter.process(job.block, &result.error);
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            bool notify = false;
            {
                std::lock_guard lock(mutex);
                ++counters.processedBlocks;
                counters.processingInputs = 0;
                counters.processingNanoseconds += static_cast<quint64>(elapsed);
                if (!job.session->active || stopping) continue;
                if (!result.error.isEmpty()) {
                    // A full audio queue must never hide a terminal protocol or
                    // coverage error. One bounded error slot per live session.
                    job.session->failed = true;
                    job.session->terminalError = result.error;
                    job.session->errorRate = result.rate;
                } else if (results.size() >= queueLimit || job.session->results >= perSessionLimit) {
                    ++counters.droppedResults;
                    job.session->outputGap = true;
                    continue;
                } else {
                    if (job.session->outputGap && !result.audio.isEmpty()) {
                        for (auto& audio : result.audio) audio.discontinuity = true;
                        job.session->outputGap = false;
                    }
                    ++job.session->results;
                    results.push_back(std::move(result));
                    counters.peakResults = std::max(counters.peakResults, int(results.size()));
                }
                if (!drainPosted) { drainPosted = true; notify = true; }
            }
            if (notify) QMetaObject::invokeMethod(&owner, [&owner = owner] { owner.drain(); }, Qt::QueuedConnection);
        }
    }
};

Web888IqProcessor::Web888IqProcessor(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(*this)) {}
Web888IqProcessor::~Web888IqProcessor() = default;

quint64 Web888IqProcessor::open(const QVector<Web888DigitalSlice>& slices, QString* error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto session = std::make_shared<Impl::Session>();
    if (!session->splitter.configure(slices, error)) return 0;
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->sessions.size() >= Impl::maxSessions || m_impl->nextId == std::numeric_limits<quint64>::max()) {
        if (error) *error = QStringLiteral("IQ processing session limit reached");
        return 0;
    }
    session->id = m_impl->nextId++;
    m_impl->sessions.emplace(session->id, session);
    return session->id;
}

void Web888IqProcessor::close(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    std::lock_guard lock(m_impl->mutex);
    const auto it = m_impl->sessions.find(id);
    if (it == m_impl->sessions.end()) return;
    it->second->active = false;
    std::erase_if(m_impl->jobs, [id](const auto& job) { return job.session->id == id; });
    std::erase_if(m_impl->results, [id](const auto& result) { return result.session->id == id; });
    m_impl->sessions.erase(it);
}

Web888IqProcessor::Submission Web888IqProcessor::submit(quint64 id, const KiwiIqBlock& block)
{
    Q_ASSERT(QThread::currentThread() == thread());
    std::lock_guard lock(m_impl->mutex);
    const auto it = m_impl->sessions.find(id);
    if (it == m_impl->sessions.end()) return Submission::Inactive;
    auto& session = *it->second;
    if (session.failed) return Submission::Inactive;
    if (block.samples.isEmpty() || block.samples.size() > 4096) {
        session.inputGap = true;
        return Submission::Invalid;
    }
    if (m_impl->jobs.size() >= Impl::queueLimit || session.inputs >= Impl::perSessionLimit) {
        ++m_impl->counters.droppedInputs;
        session.inputGap = true;
        return Submission::Backpressure;
    }
    auto copy = block;
    copy.discontinuity |= session.inputGap;
    session.inputGap = false;
    m_impl->jobs.push_back({it->second, std::move(copy)});
    ++session.inputs;
    m_impl->counters.peakInputs = std::max(m_impl->counters.peakInputs, int(m_impl->jobs.size()));
    m_impl->wake.notify_one();
    return Submission::Accepted;
}

Web888IqProcessor::Stats Web888IqProcessor::stats() const
{
    std::lock_guard lock(m_impl->mutex);
    auto stats = m_impl->counters;
    stats.sessions = int(m_impl->sessions.size());
    stats.queuedInputs = int(m_impl->jobs.size());
    stats.queuedResults = int(m_impl->results.size());
    return stats;
}

void Web888IqProcessor::drain()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_impl->draining) return;
    m_impl->draining = true;
    std::deque<Impl::Result> completed;
    std::deque<Impl::Result> errors;
    {
        std::lock_guard lock(m_impl->mutex);
        completed.swap(m_impl->results);
        for (auto& result : completed) --result.session->results;
        for (auto& [id, session] : m_impl->sessions) {
            if (session->terminalError.isEmpty()) continue;
            errors.push_back({session, session->errorRate, {}, std::exchange(session->terminalError, {})});
        }
    }
    errors.insert(errors.end(), std::make_move_iterator(completed.begin()), std::make_move_iterator(completed.end()));
    QPointer<Web888IqProcessor> self(this);
    for (const auto& result : errors) {
        bool active;
        { std::lock_guard lock(m_impl->mutex); active = result.session->active && (!result.session->failed || !result.error.isEmpty()); }
        if (active) emit processed(result.session->id, result.rate, result.audio, result.error);
        if (!self) return;
    }
    m_impl->draining = false;
    bool again = false;
    {
        std::lock_guard lock(m_impl->mutex);
        again = !m_impl->results.empty();
        for (const auto& [id, session] : m_impl->sessions)
            again |= !session->terminalError.isEmpty();
        m_impl->drainPosted = again;
    }
    if (again) QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
}
} // namespace AetherSDR
