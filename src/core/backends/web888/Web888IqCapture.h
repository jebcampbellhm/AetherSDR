#pragma once

#include "Web888IqSplitter.h"
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace AetherSDR {
class KiwiSdrClient;
class Web888IqProcessor;

// One sound-only hardware lease feeding multiple software USB decoder slices.
// Engine primitive; the background scheduler and decoder adapters own these.
class Web888IqCapture : public QObject {
    Q_OBJECT
public:
    explicit Web888IqCapture(QObject* parent = nullptr);
    Web888IqCapture(Web888IqProcessor* processor, QObject* parent);
    ~Web888IqCapture() override;
    bool start(const QString& endpoint, double centerHz,
               const QVector<Web888DigitalSlice>& slices, const QString& password = {});
    void stop();
    bool running() const { return m_processingSession != 0; }
signals:
    // Admission is confirmed by a valid, coverage-checked IQ block, not a socket open.
    void ready(double sampleRateHz);
    void decoderAudioReady(const AetherSDR::Web888DecoderAudio& audio);
    void failed(const QString& reason);
protected:
    virtual KiwiSdrClient* createClient();
private:
    KiwiSdrClient* m_client{};
    QPointer<Web888IqProcessor> m_processor;
    quint64 m_processingSession{};
    quint64 m_generation{};
    std::shared_ptr<std::atomic<bool>> m_lifetime;
    bool m_ready{false};
    QTimer m_progressDeadline;
    QTimer m_startupDeadline;
};
} // namespace AetherSDR
