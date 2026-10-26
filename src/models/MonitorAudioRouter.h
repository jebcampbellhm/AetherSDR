#pragma once
#include "core/PcmFrame.h"
#include <QObject>
#include <QSet>
#include <functional>

namespace AetherSDR {

// Revocable device intent: a queued request cannot start a sink after a newer
// selection, PC Audio toggle, shutdown, or router destruction supersedes it.
class MonitorPlaybackRequest final {
public:
    void dispatch(bool streaming, const std::function<void()>& start,
                  const std::function<void()>& stop) const;
private:
    friend class MonitorAudioRouter;
    bool current() const { return m_lifetime && m_lifetime->load(std::memory_order_acquire); }
    bool m_enabled{false}, m_restart{false};
    std::shared_ptr<const std::atomic<bool>> m_lifetime;
};

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
    bool playbackAvailable() const { return m_muted || m_available.contains(m_selected); }
    MonitorPlaybackRequest requestPlayback(bool pcAudioEnabled, bool restart = false, bool closing = false);
public slots:
    void selectSource(int source);
    void setMuted(bool muted);
    void setSourceAvailable(int source, bool available);
    void feedPrimary(const AetherSDR::PcmFrame& frame);
    void feed(int source, const AetherSDR::PcmFrame& frame);
signals:
    void playbackAvailabilityChanged(bool available);
    void pcmReady(const AetherSDR::PcmFrame& frame);
private:
    void retireRoute();
    void updatePlaybackAvailability(bool previous);
    int m_selected = 0;
    bool m_muted = false;
    bool m_monitorEnabled = true; // Non-device consumers retain the original data-router default.
    bool m_first = true;
    std::shared_ptr<std::atomic<bool>> m_route;
    PcmFrameGate m_ingress;
    QSet<int> m_available;
    std::shared_ptr<std::atomic<bool>> m_playbackRequest;
};
}
