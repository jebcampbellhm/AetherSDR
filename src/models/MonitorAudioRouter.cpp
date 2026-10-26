#include "models/MonitorAudioRouter.h"
#include <QThread>

namespace AetherSDR {
MonitorAudioRouter::MonitorAudioRouter(QObject* parent) : QObject(parent) { retireRoute(); }
MonitorAudioRouter::~MonitorAudioRouter() { m_route->store(false, std::memory_order_release); }
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
    m_selected = source;
    retireRoute();
}
void MonitorAudioRouter::setMuted(bool muted)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (muted == m_muted) { return; }
    m_muted = muted;
    retireRoute();
}
void MonitorAudioRouter::feedPrimary(const PcmFrame& frame) { feed(0, frame); }
void MonitorAudioRouter::feed(int source, const PcmFrame& frame)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_muted || source < 0 || source != m_selected || frame.m_routeLease
        || frame.stream().purpose != PcmPurpose::Speaker || !m_ingress.accept(frame)) { return; }
    PcmFrame routed = frame;
    routed.m_routeLease = m_route;
    routed.m_discontinuity = routed.m_discontinuity || m_first;
    m_first = false;
    emit pcmReady(routed);
}
}
