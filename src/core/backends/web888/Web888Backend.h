#pragma once

#include "core/KiwiSdrClient.h"
#include "core/backends/IRadioBackend.h"
#include <QTimer>

namespace AetherSDR {

// Standalone receive session. The shared open-protocol client owns the two
// asynchronous WebSockets; no Flex or simulator connection exists in this path.
class Web888Backend final : public IRadioBackend {
    Q_OBJECT
public:
    explicit Web888Backend(QObject* parent = nullptr);
    ~Web888Backend() override;
    RadioCapabilities capabilities() const override;
    bool ownsRxAudio() const override { return true; }
    void connectRadio(const RadioConnectRequest& request) override;
    void disconnectRadio() override;
    bool isConnected() const override { return m_connected; }
    void setSliceFrequency(int sliceId, double hz) override;
    void setSliceMode(int sliceId, const QString& mode) override;
    void setSliceFilter(int sliceId, int lowHz, int highHz) override;
    void setSliceAgc(int sliceId, const QString& mode, int thresholdDb) override;
    ReceiveDispatch requestSliceAudio(int sliceId, const SliceAudioRequest& request) override;
    void setSliceAudioGain(int sliceId, int gain) override;
    void setSliceAudioMute(int sliceId, bool mute) override;
    void setSliceAudioPan(int sliceId, int pan) override;
    void setPanCenter(const QString& panId, double hz, PanCenterIntent intent) override;
    void setPanBandwidth(const QString& panId, double hz) override;
    void setPanFrameRate(const QString& panId, int fps) override;
    void setKeying(
        bool, const TxCoordinator::Operation&, const TxCoordinator::Completion& = { }) override;
    void invokeExtension(
        const QString&, const QString&, quint64 requestId, const QVariant& = { }) override;

private:
    friend struct Web888BackendTestAccess;
    void track();
    void publishSlice();
    void publishPan();
    void publishVersion(const QString& version);
    void fail(const QString& detail);
    KiwiSdrClient* m_client;
    QTimer m_connectTimeout;
    bool m_initialRange = false;
    bool m_connected = false;
    bool m_connecting = false;
    quint64 m_generation = 0;
    QString m_endpoint;
    QString m_connectPassword;
    QString m_reportedVersion;
    double m_frequency = 14'200'000;
    double m_center = 14'200'000;
    double m_bandwidth = 96'000;
    QString m_mode = QStringLiteral("USB");
    int m_low = 300;
    int m_high = 2700;
    int m_gain = 50;
    int m_pan = 50;
    bool m_muted = false;
};

} // namespace AetherSDR
