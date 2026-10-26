#pragma once

#include "core/backends/IRadioBackend.h"

#include <QPointer>
#include <QThread>

#include <functional>
#include <atomic>
#include <memory>

namespace AetherSDR {
namespace nrsp { class NrspWorker; }

class NrspBackend final : public IRadioBackend
{
    Q_OBJECT
public:
    explicit NrspBackend(QObject* parent = nullptr);
    ~NrspBackend() override;
    RadioCapabilities capabilities() const override;
    bool ownsRxAudio() const override { return true; }
    ReceiveControlPolicy receiveControlPolicy() const override { return ReceiveControlPolicy::Confirmed; }
    ReconnectPolicy reconnectPolicy() const override { return ReconnectPolicy::OperatorInitiated; }
    void connectRadio(const RadioConnectRequest& request) override;
    void disconnectRadio() override;
    bool isConnected() const override { return m_connected; }
    void setSliceFrequency(int sliceId, double hz) override;
    bool requestReceiveTune(int sliceId, double hz, ReceiveTuneView view) override;
    void setSliceMode(int sliceId, const QString& mode) override;
    void setSliceFilter(int sliceId, int lowHz, int highHz) override;
    void setSliceAgc(int sliceId, const QString& mode, int threshold) override;
    void setCwPitch(int hz) override;
    void setSliceRxAntenna(int sliceId, const QString& antenna) override;
    ReceiveDispatch requestSliceRxAntenna(int sliceId, const QString& antenna) override;
    ReceiveDispatch requestSliceAudio(int sliceId, const SliceAudioRequest& request) override;
    void setSliceAudioGain(int sliceId, int gain) override;
    void setSliceAudioMute(int sliceId, bool mute) override;
    void setSliceAudioPan(int sliceId, int pan) override;
    void setPanCenter(const QString& panId, double hz, PanCenterIntent intent) override;
    void setPanBandwidth(const QString& panId, double hz) override;
    void setPanPixelWidth(const QString& panId, int pixels) override;
    void setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion& = {}) override;
    void invokeExtension(const QString& ns, const QString& verb, quint64 requestId,
                         const QVariant& argument = {}) override;

private:
    friend class NrspBackendTestAccess;
    void attachWorker(const std::shared_ptr<std::atomic<qsizetype>>& deliveryBytes);
    void post(std::function<void(nrsp::NrspWorker&)> action);
    bool accepts(quint64 generation) const;
    void applySource(const QVariantMap& source);
    void publishIdentity();
    void publishMonitor();
    void publishAudio(const QByteArray& pcm);
    void fail(const QString& reason);

    QThread* m_thread = nullptr;
    QPointer<nrsp::NrspWorker> m_worker;
    quint64 m_generation = 0;
    bool m_connected = false;
    bool m_connecting = false;
    bool m_reportedReceivePermission = false;
    QVariantMap m_source;
    double m_center = 0;
    double m_bandwidth = 2000000;
    int m_gain = 50;
    int m_pan = 50;
    bool m_muted = false;
};

} // namespace AetherSDR
