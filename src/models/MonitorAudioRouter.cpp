#include "models/MonitorAudioRouter.h"
#include <QThread>

namespace AetherSDR {
MonitorAudioRouter::MonitorAudioRouter(QObject* parent) : QObject(parent) { retireRoute(); }
MonitorAudioRouter::~MonitorAudioRouter()
{
    m_route->store(false, std::memory_order_release);
    if (m_playbackRequest) { m_playbackRequest->store(false, std::memory_order_release); }
}

void MonitorPlaybackRequest::dispatch(bool streaming, const std::function<void()>& start,
                                     const std::function<void()>& stop) const
{
    if (!current()) { return; }
    if (!m_enabled) {
        // The main sink may already have failed while sidetone or buffered
        // output remains. OFF still performs complete local cleanup.
        stop();
    } else {
        if (m_restart) { stop(); }
        if (current() && (m_restart || !streaming)) { start(); }
    }
}

MonitorPlaybackRequest MonitorAudioRouter::requestPlayback(bool pcAudioEnabled, bool restart, bool closing)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool monitorEnabled = pcAudioEnabled && !closing;
    if (monitorEnabled != m_monitorEnabled) {
        m_monitorEnabled = monitorEnabled;
        retireRoute(); // Retire queued speaker PCM without touching decoder producers.
    }
    if (m_playbackRequest) { m_playbackRequest->store(false, std::memory_order_release); }
    m_playbackRequest = std::make_shared<std::atomic<bool>>(true);
    MonitorPlaybackRequest request;
    request.m_lifetime = m_playbackRequest;
    request.m_enabled = pcAudioEnabled && !closing && playbackAvailable();
    request.m_restart = restart;
    return request;
}

void MonitorAudioRouter::updatePlaybackAvailability(bool previous)
{
    if (previous == playbackAvailable()) { return; }
    if (m_playbackRequest) { m_playbackRequest->store(false, std::memory_order_release); }
    emit playbackAvailabilityChanged(playbackAvailable());
}

void MonitorAudioRouter::setSourceAvailable(int source, bool available)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (source < 0) { return; }
    const bool previous = playbackAvailable();
    if (available) { m_available.insert(source); } else { m_available.remove(source); }
    updatePlaybackAvailability(previous);
}
void MonitorAudioRouter::retireRoute()
{
    if (m_route) { m_route->store(false, std::memory_order_release); }
    m_route = std::make_shared<std::atomic<bool>>(true);
    m_first = true;
}
void MonitorAudioRouter::selectSource(int source)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (source < -1 || source == m_selected) { return; }
    const bool previous = playbackAvailable();
    m_selected = source;
    retireRoute();
    updatePlaybackAvailability(previous);
}
void MonitorAudioRouter::setMuted(bool muted)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (muted == m_muted) { return; }
    const bool previous = playbackAvailable();
    m_muted = muted;
    retireRoute();
    updatePlaybackAvailability(previous);
}
void MonitorAudioRouter::feedPrimary(const PcmFrame& frame) { feed(0, frame); }
void MonitorAudioRouter::feed(int source, const PcmFrame& frame)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_monitorEnabled || m_muted || source < 0 || source != m_selected || frame.m_routeLease
        || frame.stream().purpose != PcmPurpose::Speaker || !m_ingress.accept(frame)) { return; }
    PcmFrame routed = frame;
    routed.m_routeLease = m_route;
    routed.m_discontinuity = routed.m_discontinuity || m_first;
    m_first = false;
    emit pcmReady(routed);
}
}
