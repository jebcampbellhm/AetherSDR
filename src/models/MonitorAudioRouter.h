#pragma once
#include "core/PcmFrame.h"
#include <QObject>

namespace AetherSDR {

// One speaker selection model, independent of receive/decoder lifetimes. Call on the
// owning thread. Source 0 is primary, positive IDs are additional receivers,
// -1 is silent. Converted/queued samples retain both producer and route leases.
class MonitorAudioRouter final : public QObject {
    Q_OBJECT
public:
    explicit MonitorAudioRouter(QObject* parent = nullptr);
    ~MonitorAudioRouter() override;
    int selectedSource() const { return m_selected; }
    bool muted() const { return m_muted; }
public slots:
    void selectSource(int source);
    void setMuted(bool muted);
    void feedPrimary(const AetherSDR::PcmFrame& frame);
    void feed(int source, const AetherSDR::PcmFrame& frame);
signals:
    void pcmReady(const AetherSDR::PcmFrame& frame);
private:
    void retireRoute();
    int m_selected = 0;
    bool m_muted = false;
    bool m_first = true;
    std::shared_ptr<std::atomic<bool>> m_route;
    PcmFrameGate m_ingress;
};
}
