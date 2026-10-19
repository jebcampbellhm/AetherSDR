#pragma once

#include "core/backends/IRadioBackend.h"
#include "Web888CapturePlan.h"
#include "Web888DecoderRouter.h"
#include "Web888SsbAudio.h"
#include <map>
#include <memory>

namespace AetherSDR {

class KiwiSdrClient;
class Web888CapturePool;
class Web888IqProcessor;

// Two independently owned listening receivers with paired waterfalls.
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
    bool createPanadapter() override;
    bool removePanadapter(const QString& panId) override;
    bool createSlice(const QString& panId, double frequencyHz) override;
    bool removeSlice(int sliceId) override;
    void setActiveSlice(int sliceId) override;
    ReceiveDispatch requestSliceControl(int sliceId, const SliceControlRequest& request) override;
    // Engine-only monitoring entry point; budget includes both reserved listeners.
    bool applyDigitalProfile(const QVector<Web888DigitalPreset>& presets,
                             double sampleRateHz, int receiverBudget);
    void stopDigitalProfile();
    bool configureDigitalDecoders(const QVector<Web888DigitalPreset>& presets,
                                  quint64 sampleByteBudget, QString* error = nullptr);
    // Call from digitalAudioReady for its first block, using a qualified mapping
    // corrected for filter delay. Receipt time is not a sample UTC anchor.
    bool armDigitalDecoder(const Web888DecoderAudio& first, const Web888DecoderClockAnchor& anchor);
    std::optional<Web888DecoderJob> takeDigitalDecoderJob();
    int digitalCaptureCount() const;
    int pendingDigitalCaptureCount() const;
    void setSliceFrequency(int sliceId, double hz) override;
    void setSliceMode(int sliceId, const QString& mode) override;
    void setSliceFilter(int sliceId, int lowHz, int highHz) override;
    void setSliceAgc(int sliceId, const QString& mode, int thresholdDb) override;
    ReceiveDispatch requestSliceAudio(int sliceId, const SliceAudioRequest& request) override;
    void setSliceAudioMute(int sliceId, bool mute) override;
    void setSliceAudioGain(int sliceId, int gainPercent) override;
    void setSliceAudioPan(int sliceId, int pan) override;
    void setPanCenter(const QString& panId, double hz, PanCenterIntent intent) override;
    void setPanBandwidth(const QString& panId, double hz) override;
    void setPanFrameRate(const QString& panId, int fps) override;
    void setKeying(bool key, const TxCoordinator::Operation& operation,
                   const TxCoordinator::Completion& completion = {}) override;
    void invokeExtension(const QString& ns, const QString& verb,
                         quint64 requestId, const QVariant& arg = {}) override;

signals:
    void digitalProfileReady();
    void digitalProfileFailed(const QString& reason);
    void digitalCaptureFailed(quint64 captureId, const QString& reason);
    void digitalAudioReady(const AetherSDR::Web888DecoderAudio& audio);

protected:
    // Production uses the shared wire implementation. Tests replace transport
    // opening and inject observations, without a listening firmware substitute.
    virtual KiwiSdrClient* createClient();
    virtual Web888CapturePool* createCapturePool();

private:
    struct Receiver {
        int owner{-1}; // -1 owns a hardware lease; otherwise a shared software listener
        bool sharedIq{false};
        bool addingListener{false};
        double captureHz{0};
        quint64 dspSession{0};
        Web888SsbAudio audio;
        KiwiSdrClient* client{nullptr};
        quint64 generation{0};
        bool connected{false};
        bool meterDefined{false};
        bool initialRange{false};
        double frequencyHz{7100000.0};
        QString mode{QStringLiteral("LSB")};
        int lowHz{-2800};
        int highHz{-300};
        QString agcMode{QStringLiteral("med")};
        int gain{100};
        int pan{50};
        bool muted{false};
        double panCenterHz{7100000.0};
        double panBandwidthHz{100000.0};
    };
    std::shared_ptr<Receiver> receiver(int id) const;
    int sliceForPan(const QString& panId) const;
    void openReceiver(int id);
    void closeReceiver(int id, bool notify);
    void publishSlice(int id);
    void rejectSliceControl(int id, const QString& operation, const QString& reason);
    void applyTune(int id);
    int panIndex(int id) const;
    bool fitsCapture(const Receiver& r, double hz, int low, int high) const;
    void configureListener(int id);
    void deliverAudio(int id, quint64 generation, const QByteArray& pcm);
    void stop(bool notify);
    bool current(int id, quint64 generation) const;
    std::map<int, std::shared_ptr<Receiver>> m_receivers;
    std::map<int, std::shared_ptr<Receiver>> m_listeners;
    Web888IqProcessor* m_listenerProcessor{nullptr};
    Web888CapturePool* m_capturePool{nullptr};
    Web888DecoderRouter m_decoderRouter;
    QString m_reportedVersion;
    QString m_monitorError;
    quint64 m_monitorSamples{0};
    QString m_endpoint;
    QString m_password; // session-only; cleared at disconnect
    quint64 m_generation{0};
    quint64 m_nextLease{0};
    int m_nextId{1};
    int m_activeSlice{0};
    bool m_loadingCredentials{false};
    bool m_saveCredentials{false};
    bool m_connected{false};
    bool m_stopping{false};
};

} // namespace AetherSDR
