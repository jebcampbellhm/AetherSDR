#pragma once

#include "core/backends/IRadioBackend.h"

namespace AetherSDR {

class KiwiSdrClient;

// Local first-light engine implementation. One owned receiver with a paired
// waterfall; selected through the neutral backend factory.
class Web888Backend : public IRadioBackend {
    Q_OBJECT
public:
    explicit Web888Backend(QObject* parent = nullptr);
    ~Web888Backend() override;
    RadioCapabilities capabilities() const override;
    bool ownsRxAudio() const override { return true; }
    bool isConnected() const override { return m_connected; }
    void connectRadio(const RadioConnectRequest& request) override;
    void disconnectRadio() override;
    void setSliceFrequency(int sliceId, double hz) override;
    void setSliceMode(int sliceId, const QString& mode) override;
    void setSliceFilter(int sliceId, int lowHz, int highHz) override;
    void setSliceAgc(int sliceId, const QString& mode, int thresholdDb) override;
    void setSliceAudioMute(int sliceId, bool mute) override;
    void setSliceAudioGain(int sliceId, int gainPercent) override;
    void setPanCenter(const QString& panId, double hz, PanCenterIntent intent) override;
    void setPanBandwidth(const QString& panId, double hz) override;
    void setKeying(bool key, const TxCoordinator::Operation& operation,
                   const TxCoordinator::Completion& completion = {}) override;
    void invokeExtension(const QString& ns, const QString& verb,
                         quint64 requestId, const QVariant& arg = {}) override;

protected:
    // Production uses the shared wire implementation. Tests replace transport
    // opening and inject observations, without a listening firmware substitute.
    virtual KiwiSdrClient* createClient();

private:
    void publishSlice();
    void applyTune();
    void stop(bool notify);
    bool current(quint64 generation) const;
    KiwiSdrClient* m_client{nullptr};
    quint64 m_generation{0};
    bool m_connected{false};
    bool m_stopping{false};
    bool m_meterDefined{false};
    double m_frequencyHz{7100000.0};
    QString m_mode{QStringLiteral("LSB")};
    int m_lowHz{-2800};
    int m_highHz{-300};
    QString m_agcMode{QStringLiteral("med")};
    int m_gain{100};
    bool m_muted{false};
    double m_panCenterHz{7100000.0};
    double m_panBandwidthHz{100000.0};
};

} // namespace AetherSDR
