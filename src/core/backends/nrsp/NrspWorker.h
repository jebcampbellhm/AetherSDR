#pragma once

#include "NrspDemodulator.h"
#include "NrspSession.h"
#include "core/backends/SliceDelta.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

#include <optional>
#include <atomic>
#include <memory>

namespace AetherSDR::nrsp {

// Private acquisition worker. Every outward event carries its connection
// generation; the backend must reject retired deliveries before publication.
class NrspWorker final : public QObject
{
    Q_OBJECT
public:
    // The central 80% avoids the measured source-filter roll-off at 2 MS/s.
    // Full source span remains available through the normal zoom controls.
    static constexpr double kInitialSpanFraction = 0.8;
    explicit NrspWorker(std::shared_ptr<std::atomic<qsizetype>> deliveryBytes, QObject* parent = nullptr);
    void start(quint64 generation, const QString& host, quint16 port,
               const QHostAddress& localAddress = {}, QString username = {}, QString password = {});
    void stop();
    enum class TuneView { Preserve, Reveal, Center };
    void tune(double hz, TuneView view = TuneView::Preserve);
    void setMode(const QString& mode);
    void setFilter(int lowHz, int highHz);
    void setAgc(const QString& mode, int threshold);
    void setCwPitch(int hz);
    void setAntenna(const QString& antenna);
    void setPan(double centerHz, double bandwidthHz);
    void setPanCenter(double hz) { setPan(hz, m_bandwidth); }
    void setPanBandwidth(double hz) { setPan(m_center, hz); }
    void setDisplayWidth(int bins);
    void frontEndControl(const QString& verb, bool enabled);

signals:
    void ready(quint64 generation, const QVariantMap& source);
    void sourceChanged(quint64 generation, const QVariantMap& source);
    void sliceChanged(quint64 generation, const AetherSDR::SliceDelta& delta);
    void panChanged(quint64 generation, double centerHz, double bandwidthHz);
    void audioReady(quint64 generation, const QByteArray& pcm);
    void spectrumReady(quint64 generation, const QByteArray& bins);
    void failed(quint64 generation, const QString& reason);
    void warning(quint64 generation, const QString& reason);

private:
    QVariantMap sourceFields() const;
    void onReady();
    void onSource();
    void onData(quint8 type, const ReceiverData& data);
    void onTick();
    void fail(const QString& reason);
    void publishSlice();
    void finishTune();
    bool inCapture(double hz) const;
    bool reserveDelivery(qsizetype bytes);
    void requireSourceReadback();
    void startSourceReadback();
    void readSourceHello();

    QTcpSocket m_socket;
    QTcpSocket m_readbackSocket;
    FrameDecoder m_readbackDecoder;
    NrspSession m_session;
    QTimer m_timer;
    QElapsedTimer m_clock;
    NrspDemodulator m_demod;
    std::shared_ptr<std::atomic<qsizetype>> m_deliveryBytes;
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_receiving = false;
    quint32 m_sourceRate = 0;
    double m_frequency = 0;
    double m_center = 0;
    double m_bandwidth = 0;
    std::optional<double> m_pendingTune;
    TuneView m_pendingView = TuneView::Preserve;
    qint64 m_tuneDeadline = 0;
    QString m_host;
    quint16 m_port = 50000;
    QHostAddress m_localAddress;
    bool m_readbackRequired = false;
    qint64 m_readbackDeadline = 0;
    QString m_mode = QStringLiteral("USB");
    int m_low = 300;
    int m_high = 2700;
    QString m_agc = QStringLiteral("med");
    int m_agcThreshold = 80;
};

} // namespace AetherSDR::nrsp
