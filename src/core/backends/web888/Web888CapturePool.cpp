#include "Web888CapturePool.h"
#include "Web888IqCapture.h"
#include "Web888IqProcessor.h"
#include "core/KiwiSdrClient.h"
#include <QThread>
#include <QTimer>
#include <limits>

namespace AetherSDR {
Web888CapturePool::Web888CapturePool(QObject* parent) : QObject(parent)
{
    m_processor = new Web888IqProcessor(this);
    m_deadline = new QTimer(this);
    m_deadline->setObjectName(QStringLiteral("captureAdmissionDeadline"));
    m_deadline->setSingleShot(true);
    m_deadline->setInterval(30000);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        if (m_changing) abortPending(QStringLiteral("Timed out waiting for valid IQ from the new captures"));
    });
}
Web888CapturePool::~Web888CapturePool() { stop(); }
Web888IqCapture* Web888CapturePool::createCapture(Web888IqProcessor* processor)
{
    return new Web888IqCapture(processor, this);
}

bool Web888CapturePool::sameWindow(const Web888CaptureWindow& a, const Web888CaptureWindow& b)
{
    if (a.centerHz != b.centerHz || a.presets.size() != b.presets.size()) return false;
    for (qsizetype n = 0; n < a.presets.size(); ++n) {
        const auto& x = a.presets[n]; const auto& y = b.presets[n];
        if (x.band != y.band || x.mode != y.mode || x.slice.id != y.slice.id
            || x.slice.dialHz != y.slice.dialHz || x.slice.lowHz != y.slice.lowHz
            || x.slice.highHz != y.slice.highHz) return false;
    }
    return true;
}

QVector<quint64> Web888CapturePool::activeIds() const
{
    QVector<quint64> ids;
    for (const auto& [id, entry] : m_active) ids.append(id);
    return ids;
}

bool Web888CapturePool::applyProfile(const QString& endpoint,
                                    const QVector<Web888DigitalPreset>& presets,
                                    double rate, int budget, int listening,
                                    const QString& password)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto reject = [this](const QString& reason) { emit profileFailed(reason); return false; };
    if (m_changing) return reject(QStringLiteral("A capture profile change is already pending"));
    const auto normalized = KiwiSdrClient::normalizeEndpoint(endpoint);
    if (normalized.isEmpty() || (!m_endpoint.isEmpty() && m_endpoint != normalized))
        return reject(QStringLiteral("Stop captures before changing the receiver endpoint"));
    const auto plan = planWeb888Captures(presets, rate, budget, listening);
    if (!plan.fitsCapacity) return reject(plan.error);

    QVector<Web888CaptureWindow> additions;
    QSet<quint64> desired;
    for (const auto& window : plan.windows) {
        bool reused = false;
        for (const auto& [id, entry] : m_active) {
            if (sameWindow(window, entry.window)) { desired.insert(id); reused = true; break; }
        }
        if (!reused) additions.append(window);
    }
    if (!additions.isEmpty() && activeCount() + additions.size() + listening > budget)
        return reject(QStringLiteral("The profile fits after replacement, but acquiring it without interrupting current captures exceeds the receiver budget"));
    if (m_nextId > std::numeric_limits<quint64>::max() - quint64(additions.size()))
        return reject(QStringLiteral("Capture identity space exhausted"));

    m_endpoint = normalized;
    m_desired = desired;
    m_changing = true;
    const auto transaction = ++m_transaction;
    QVector<quint64> opens;
    for (const auto& window : additions) {
        const auto id = m_nextId++;
        auto* capture = createCapture(m_processor);
        m_pending.emplace(id, Entry{capture, window});
        m_desired.insert(id);
        opens.append(id);
        connect(capture, &Web888IqCapture::ready, this, [this, id](double) {
            const auto it = m_pending.find(id);
            if (it == m_pending.end()) return;
            it->second.ready = true;
            commitIfReady();
        });
        connect(capture, &Web888IqCapture::failed, this,
                [this, id](const QString& reason) { onFailure(id, reason); });
        connect(capture, &Web888IqCapture::decoderAudioReady, this,
                [this, id](const Web888DecoderAudio& audio) {
            const auto it = m_active.find(id);
            if (it == m_active.end() || !audio.current()) return;
            auto out = audio;
            out.captureId = id;
            out.discontinuity |= !it->second.publishedSlices.contains(out.sliceId);
            it->second.publishedSlices.insert(out.sliceId);
            emit decoderAudioReady(out);
        });
    }
    m_deadline->start();
    // Reserve every pending open before issuing the first connection request.
    for (const auto id : opens) {
        if (transaction != m_transaction || !m_changing) return false;
        auto& entry = m_pending.at(id);
        QVector<Web888DigitalSlice> slices;
        for (const auto& p : entry.window.presets) slices.append(p.slice);
        auto* capture = entry.capture;
        const double center = entry.window.centerHz;
        const bool accepted = capture->start(normalized, center, slices, password);
        if (transaction != m_transaction) return false;
        if (!accepted) { abortPending(QStringLiteral("A new capture could not start")); return false; }
    }
    commitIfReady();
    return transaction == m_transaction;
}

void Web888CapturePool::release(Entry& entry)
{
    QObject::disconnect(entry.capture, nullptr, this, nullptr);
    entry.capture->stop();
    entry.capture->deleteLater();
}

void Web888CapturePool::abortPending(const QString& reason)
{
    ++m_transaction;
    m_deadline->stop();
    m_changing = false;
    m_desired.clear();
    auto discarded = std::move(m_pending);
    m_pending.clear();
    for (auto& [id, entry] : discarded) release(entry);
    if (m_active.empty()) m_endpoint.clear();
    emit profileFailed(reason);
}

void Web888CapturePool::commitIfReady()
{
    if (!m_changing) return;
    for (const auto& [id, entry] : m_pending) if (!entry.ready) return;
    m_deadline->stop();
    m_changing = false;
    m_active.merge(m_pending);
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (m_desired.contains(it->first)) { ++it; continue; }
        auto old = it++;
        release(old->second);
        m_active.erase(old);
    }
    m_desired.clear();
    if (m_active.empty()) m_endpoint.clear();
    emit profileReady();
}

void Web888CapturePool::onFailure(quint64 id, const QString& reason)
{
    if (m_pending.contains(id)) { abortPending(reason); return; }
    const auto it = m_active.find(id);
    if (it == m_active.end()) return;
    const bool neededByChange = m_changing && m_desired.contains(id);
    release(it->second);
    m_active.erase(it);
    if (neededByChange) abortPending(QStringLiteral("An existing capture required by the pending profile failed: ") + reason);
    if (m_active.empty() && !m_changing) m_endpoint.clear();
    emit captureFailed(id, reason);
}

void Web888CapturePool::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_transaction;
    m_deadline->stop();
    m_changing = false;
    m_desired.clear();
    for (auto& [id, entry] : m_pending) release(entry);
    for (auto& [id, entry] : m_active) release(entry);
    m_pending.clear(); m_active.clear(); m_endpoint.clear();
}
} // namespace AetherSDR
