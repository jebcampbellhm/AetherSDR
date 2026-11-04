#include "NrspWorker.h"

#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace AetherSDR::nrsp {

NrspWorker::NrspWorker(std::shared_ptr<std::atomic<qsizetype>> deliveryBytes, QObject* parent)
    : QObject(parent), m_socket(this), m_readbackSocket(this), m_session(this), m_timer(this),
      m_deliveryBytes(std::move(deliveryBytes))
{
    m_socket.setReadBufferSize(kMaxBuffered);
    m_readbackSocket.setReadBufferSize(4096);
    connect(&m_readbackSocket, &QTcpSocket::readyRead, this, &NrspWorker::readSourceHello);
    connect(&m_readbackSocket, &QTcpSocket::errorOccurred, this, [this] {
        // Retry on the timer until the bounded readback deadline. This socket
        // never writes a command or creates a receiver.
        m_readbackSocket.abort();
    });
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &NrspWorker::onTick);
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        while (m_active && m_socket.bytesAvailable() > 0) {
            const QByteArray bytes = m_socket.read(65536);
            if (bytes.isEmpty()) { break; }
            m_session.receive(bytes, m_clock.elapsed());
        }
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            fail(tr("nRSP-ST closed the session. Reconnect manually when ready."));
        } else {
            fail(tr("nRSP-ST connection: %1").arg(m_socket.errorString()));
        }
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        fail(tr("nRSP-ST disconnected. Reconnect manually when ready."));
    });
    connect(&m_session, &NrspSession::sendBytes, this, [this](const QByteArray& bytes) {
        if (!m_active || m_socket.state() != QAbstractSocket::ConnectedState) { return; }
        if (m_socket.bytesToWrite() > 65536 || m_socket.write(bytes) != bytes.size()) {
            fail(tr("nRSP-ST command output is stalled."));
        }
    });
    connect(&m_session, &NrspSession::ready, this, &NrspWorker::onReady);
    connect(&m_session, &NrspSession::sourceChanged, this, &NrspWorker::onSource);
    connect(&m_session, &NrspSession::receiverData, this, &NrspWorker::onData);
    connect(&m_session, &NrspSession::failed, this, &NrspWorker::fail);
}

void NrspWorker::start(quint64 generation, const QString& host, quint16 port,
                       const QHostAddress& localAddress, QString username, QString password)
{
    m_host = host;
    m_port = port;
    m_localAddress = localAddress;
    m_generation = generation;
    m_active = true;
    m_clock.start();
    connect(&m_socket, &QTcpSocket::connected, this,
        [this, username = std::move(username), password = std::move(password)]() mutable {
            m_session.begin(m_clock.elapsed(), std::move(username), std::move(password));
        }, Qt::SingleShotConnection);
    m_timer.start();
    if (!localAddress.isNull() && !m_socket.bind(localAddress, 0)) {
        fail(tr("Cannot bind the selected nRSP-ST source address: %1").arg(m_socket.errorString()));
        return;
    }
    m_socket.connectToHost(host, port);
}

void NrspWorker::stop()
{
    m_active = false;
    m_receiving = false;
    m_pendingTune.reset();
    m_timer.stop();
    m_session.stop();
    m_readbackRequired = false;
    m_readbackSocket.abort();
    m_socket.abort();
    m_demod.close();
    QThread::currentThread()->quit();
}

void NrspWorker::fail(const QString& reason)
{
    if (!m_active) { return; }
    m_active = false;
    m_receiving = false;
    m_timer.stop();
    m_session.stop();
    m_readbackRequired = false;
    m_readbackSocket.abort();
    m_socket.abort();
    m_demod.close();
    emit failed(m_generation, reason);
}

QVariantMap NrspWorker::sourceFields() const
{
    if (!m_session.hello()) { return {}; }
    const SourceState& source = m_session.hello()->source;
    return {{QStringLiteral("frequencyHz"), QVariant::fromValue(source.frequencyHz)},
            {QStringLiteral("sampleRate"), source.sampleRate},
            {QStringLiteral("owner"), source.owner},
            {QStringLiteral("antenna"), source.antenna},
            {QStringLiteral("rfAgc"), source.rfAgc},
            {QStringLiteral("rfNotch"), source.rfNotch},
            {QStringLiteral("dabNotch"), source.dabNotch},
            {QStringLiteral("lnaIndex"), source.lnaIndex},
            {QStringLiteral("manualIfReduction"), source.manualIfReduction}};
}

void NrspWorker::onReady()
{
    QString error;
    if (!m_demod.open(m_mode, m_low, m_high, error)) {
        fail(tr("Cannot start nRSP-ST audio: %1").arg(error));
        return;
    }
    const SourceState& source = m_session.hello()->source;
    m_sourceRate = source.sampleRate;
    m_frequency = double(source.frequencyHz);
    m_center = m_frequency;
    m_bandwidth = source.sampleRate * kInitialSpanFraction;
    if (!m_session.createReceiver(ReceiverConfig{1, source.frequencyHz, 48000, 1024}, m_clock.elapsed())) {
        fail(tr("Cannot create nRSP-ST IQ Lite receiver."));
        return;
    }
    m_receiving = true;
    emit ready(m_generation, sourceFields());
    emit panChanged(m_generation, m_center, m_bandwidth);
    publishSlice();
}

bool NrspWorker::inCapture(double hz) const
{
    return m_session.hello() && std::abs(hz - double(m_session.hello()->source.frequencyHz))
        + NrspDemodulator::kInputRate / 2.0 <= m_session.hello()->source.sampleRate / 2.0;
}

void NrspWorker::onSource()
{
    if (!m_receiving) { return; }
    const SourceState source = m_session.hello()->source;
    if (source.sampleRate != m_sourceRate) {
        fail(tr("nRSP-ST source rate changed. Reconnect to adopt its new IQ timing."));
        return;
    }
    emit sourceChanged(m_generation, sourceFields());
    if (m_pendingTune) {
        finishTune();
    } else if (inCapture(m_frequency)) {
        m_session.tuneReceiver(1, quint64(std::llround(m_frequency)));
    }
    if (!m_active) { return; }
    const double half = std::max(0.0, (source.sampleRate - m_bandwidth) / 2.0);
    m_center = std::clamp(m_center, double(source.frequencyHz) - half, double(source.frequencyHz) + half);
    emit panChanged(m_generation, m_center, m_bandwidth);
    publishSlice();
}

void NrspWorker::publishSlice()
{
    if (!m_receiving) { return; }
    SliceDelta delta;
    delta.frequency = m_frequency / 1e6;
    delta.mode = m_mode;
    delta.filterLow = m_low;
    delta.filterHigh = m_high;
    delta.agcMode = m_agc;
    delta.agcThreshold = m_agcThreshold;
    delta.inCapture = inCapture(m_frequency);
    emit sliceChanged(m_generation, delta);
}

void NrspWorker::tune(double hz, TuneView view)
{
    if (!m_receiving || !std::isfinite(hz) || hz < 1 || hz > 2000000000) { return; }
    if (inCapture(hz) && !m_readbackRequired) {
        if (m_session.tuneReceiver(1, quint64(std::llround(hz)))) {
            m_frequency = hz;
            m_pendingTune.reset();
            if (view == TuneView::Center
                || (view == TuneView::Reveal && std::abs(hz - m_center) > m_bandwidth * 0.4)) {
                setPan(hz, m_bandwidth);
            }
            publishSlice();
        }
        return;
    }
    if (!m_session.hello()->source.owner) {
        emit warning(m_generation, tr("This session has no source control. Tune inside the current capture or request control in NRSPAdministrator."));
        publishSlice();
        return;
    }
    m_pendingTune = hz;
    m_pendingView = view;
    m_tuneDeadline = m_clock.elapsed() + 5000;
    if (!m_readbackRequired) { finishTune(); }
}

void NrspWorker::finishTune()
{
    if (!m_pendingTune || !m_receiving) { return; }
    const double hz = *m_pendingTune;
    const SourceState& source = m_session.hello()->source;
    if (inCapture(hz)) {
        const TuneView view = m_pendingView;
        m_pendingTune.reset();
        tune(hz, view);
    } else if (!source.owner) {
        m_pendingTune.reset();
        emit warning(m_generation, tr("nRSP-ST source ownership changed before tuning completed."));
    } else if (source.antenna == 2 && hz > 200000000) {
        if (m_session.setAntenna(0)) { requireSourceReadback(); }
    } else {
        if (m_session.setSourceFrequency(quint64(std::llround(hz)))) { requireSourceReadback(); }
    }
}

void NrspWorker::setMode(const QString& mode)
{
    int low = 300;
    int high = 2700;
    if (mode == "LSB" || mode == "DIGL") { low = -2700; high = -300; }
    if (mode == "AM" || mode == "SAM") { low = -5000; high = 5000; }
    if (mode == "FM") { low = -8000; high = 8000; }
    if (mode == "CW") { low = -250; high = 250; }
    if (!m_receiving || !m_demod.setModeAndFilter(mode, low, high)) {
        emit warning(m_generation, tr("nRSP-ST receive mode was not applied."));
        return;
    }
    m_mode = mode;
    m_low = low;
    m_high = high;
    publishSlice();
}

void NrspWorker::setFilter(int lowHz, int highHz)
{
    if (!m_receiving || !m_demod.setModeAndFilter(m_mode, lowHz, highHz)) {
        emit warning(m_generation, tr("nRSP-ST receive passband was not applied."));
        return;
    }
    m_low = lowHz;
    m_high = highHz;
    publishSlice();
}

void NrspWorker::setAgc(const QString& mode, int threshold)
{
    if (!m_receiving || !m_demod.setAgc(mode, threshold)) {
        emit warning(m_generation, tr("nRSP-ST audio AGC was not applied."));
        return;
    }
    m_agc = mode;
    m_agcThreshold = threshold;
    publishSlice();
}

void NrspWorker::setCwPitch(int hz)
{
    if (m_receiving && !m_demod.setCwPitch(hz)) {
        emit warning(m_generation, tr("nRSP-ST CW pitch was not applied."));
    }
}

void NrspWorker::setAntenna(const QString& antenna)
{
    const int index = QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}.indexOf(antenna);
    if (index < 0 || !m_session.setAntenna(quint32(index))) {
        emit warning(m_generation, tr("Antenna selection requires source control; antenna C also requires a source at or below 200 MHz."));
    } else {
        requireSourceReadback();
    }
}

void NrspWorker::setPan(double centerHz, double bandwidthHz)
{
    if (!m_receiving || !std::isfinite(centerHz) || !std::isfinite(bandwidthHz)) { return; }
    const SourceState& source = m_session.hello()->source;
    m_bandwidth = std::clamp(bandwidthHz, 1000.0, double(source.sampleRate));
    const double half = (source.sampleRate - m_bandwidth) / 2;
    m_center = std::clamp(centerHz, double(source.frequencyHz) - half, double(source.frequencyHz) + half);
    emit panChanged(m_generation, m_center, m_bandwidth);
}

void NrspWorker::setDisplayWidth(int bins)
{
    if (m_receiving) { m_session.setDisplayWidth(1, quint32(std::clamp(bins, 512, 4096))); }
}

void NrspWorker::frontEndControl(const QString& verb, bool enabled)
{
    bool accepted = false;
    if (verb == "rfAgc") { accepted = m_session.setRfAgc(enabled); }
    if (verb == "rfNotch") { accepted = m_session.setRfNotch(enabled); }
    if (verb == "dabNotch") { accepted = m_session.setDabNotch(enabled); }
    if (!accepted) { emit warning(m_generation, tr("nRSP-ST front-end control requires source ownership.")); }
    else { requireSourceReadback(); }
}

void NrspWorker::requireSourceReadback()
{
    m_readbackRequired = true;
    m_readbackDeadline = m_clock.elapsed() + 5000;
    // Retire an older query; its hello may predate this latest command.
    m_readbackSocket.abort();
}

void NrspWorker::startSourceReadback()
{
    m_readbackDecoder.reset();
    if (!m_localAddress.isNull() && !m_readbackSocket.bind(m_localAddress, 0)) {
        fail(tr("Cannot bind nRSP-ST source-state readback."));
        return;
    }
    m_readbackSocket.connectToHost(m_host, m_port);
}

void NrspWorker::readSourceHello()
{
    if (!m_active || !m_readbackRequired) { return; }
    if (!m_readbackDecoder.append(m_readbackSocket.readAll())) {
        fail(tr("Invalid nRSP-ST source-state readback."));
        return;
    }
    Frame frame;
    const FrameDecoder::Result result = m_readbackDecoder.next(frame);
    if (result == FrameDecoder::Result::NeedMore) { return; }
    const std::optional<Hello> hello = result == FrameDecoder::Result::Ready && frame.type == 201
        ? parseHello(frame.payload) : std::nullopt;
    if (!hello) {
        fail(tr("Invalid nRSP-ST source-state hello."));
        return;
    }
    m_readbackRequired = false;
    m_readbackSocket.abort();
    m_session.observeSharedSource(hello->source);
}

void NrspWorker::onTick()
{
    if (!m_active) { return; }
    const qint64 now = m_clock.elapsed();
    if (m_readbackRequired) {
        if (now >= m_readbackDeadline) {
            fail(tr("nRSP-ST source change could not be read back. Reconnect to recover its current state."));
            return;
        }
        if (m_readbackSocket.state() == QAbstractSocket::UnconnectedState) { startSourceReadback(); }
        if (!m_active) { return; }
    }
    if (m_socket.state() != QAbstractSocket::ConnectedState && now >= 10000) {
        fail(tr("Timed out connecting to nRSP-ST."));
        return;
    }
    if (m_pendingTune && now >= m_tuneDeadline) {
        m_pendingTune.reset();
        emit warning(m_generation, tr("nRSP-ST did not confirm the requested source tuning."));
        publishSlice();
    }
    m_session.tick(now);
}

void NrspWorker::onData(quint8 type, const ReceiverData& data)
{
    if (!m_receiving || m_readbackRequired || data.receiverId != 1) { return; }
    if (type == 212) {
        const qsizetype count = data.bytes.size();
        if (count < 2) { return; }
        QByteArray bins(count * qsizetype(sizeof(float)), Qt::Uninitialized);
        const double sourceLow = double(m_session.hello()->source.frequencyHz) - m_sourceRate / 2.0;
        const double viewLow = m_center - m_bandwidth / 2;
        for (qsizetype i = 0; i < count; ++i) {
            const double frequency = viewLow + (double(i) + 0.5) * m_bandwidth / double(count);
            const double position = std::clamp((frequency - sourceLow) / m_sourceRate * double(count) - 0.5,
                                               0.0, double(count - 1));
            const qsizetype left = qsizetype(position);
            const qsizetype right = std::min(left + 1, count - 1);
            const float first = quint8(data.bytes[left]);
            const float second = quint8(data.bytes[right]);
            const float value = -220.0f + float(first + (second - first) * (position - left)) * (120.0f / 255.0f);
            std::memcpy(bins.data() + i * sizeof(float), &value, sizeof(value));
        }
        if (reserveDelivery(bins.size())) { emit spectrumReady(m_generation, bins); }
    } else if (inCapture(m_frequency)) {
        QString error;
        const std::optional<QByteArray> audio = m_demod.process(data.bytes, error);
        if (!audio) {
            fail(error);
        } else if (!audio->isEmpty() && reserveDelivery(audio->size())) {
            emit audioReady(m_generation, *audio);
        }
    }
}

bool NrspWorker::reserveDelivery(qsizetype bytes)
{
    constexpr qsizetype kMaximumQueuedBytes = 4 * 1024 * 1024;
    const qsizetype previous = m_deliveryBytes->fetch_add(bytes, std::memory_order_relaxed);
    if (bytes > kMaximumQueuedBytes || previous > kMaximumQueuedBytes - bytes) {
        m_deliveryBytes->fetch_sub(bytes, std::memory_order_relaxed);
        fail(tr("nRSP-ST audio/display delivery stalled. Reconnect after the application responds."));
        return false;
    }
    return true;
}

} // namespace AetherSDR::nrsp
