#include "Web888IqCapture.h"
#include "Web888IqProcessor.h"
#include "core/KiwiSdrClient.h"
#include <QThread>

namespace AetherSDR {
Web888IqCapture::Web888IqCapture(QObject* parent) : Web888IqCapture(nullptr, parent) {}
Web888IqCapture::Web888IqCapture(Web888IqProcessor* processor, QObject* parent)
    : QObject(parent), m_progressDeadline(this), m_startupDeadline(this)
{
    m_startupDeadline.setObjectName(QStringLiteral("captureStartupDeadline"));
    m_startupDeadline.setSingleShot(true);
    m_startupDeadline.setInterval(30000);
    connect(&m_startupDeadline, &QTimer::timeout, this, [this] {
        if (!m_processingSession || m_ready) return;
        stop();
        emit failed(QStringLiteral("IQ capture startup timed out without valid samples"));
    });
    m_progressDeadline.setObjectName(QStringLiteral("captureProgressDeadline"));
    m_progressDeadline.setSingleShot(true);
    m_progressDeadline.setInterval(10000);
    connect(&m_progressDeadline, &QTimer::timeout, this, [this] {
        if (!m_processingSession || !m_ready) return;
        stop();
        emit failed(QStringLiteral("IQ capture stopped making progress for 10 seconds"));
    });
    m_processor = processor ? processor : new Web888IqProcessor(this);
    Q_ASSERT(m_processor->thread() == thread());
    connect(m_processor, &QObject::destroyed, this, [this] {
        m_processor = nullptr; // its derived worker state has already been destroyed
        if (!m_processingSession) return;
        stop();
        emit failed(QStringLiteral("IQ processing service was destroyed"));
    });
    connect(m_processor, &Web888IqProcessor::processed, this,
        [this](quint64 session, double rate, const QVector<Web888DecoderAudio>& outputs, const QString& error) {
            if (!session || session != m_processingSession) return;
            if (!error.isEmpty()) { stop(); emit failed(error); return; }
            const auto generation = m_generation;
            // Admit on valid IQ, allowing initial FIR warmup. Thereafter require
            // actual slice audio: repeated short discontinuous blocks can keep
            // resetting the FIR forever without producing anything decodable.
            if (!m_ready || !outputs.isEmpty()) m_progressDeadline.start();
            if (!m_ready) { m_startupDeadline.stop(); m_ready = true; emit ready(rate); }
            for (auto out : outputs) {
                if (generation != m_generation) break;
                out.generation = generation;
                out.lifetime = m_lifetime;
                emit decoderAudioReady(out);
            }
        });
}
Web888IqCapture::~Web888IqCapture() { stop(); }

KiwiSdrClient* Web888IqCapture::createClient()
{
    return new KiwiSdrClient(KiwiSdrClient::Streams::SoundOnly,
        KiwiSdrClient::Admission::IndependentReceiver,
        KiwiSdrClient::SoundFormat::FilteredIq, this);
}

bool Web888IqCapture::start(const QString& endpoint, double centerHz,
                           const QVector<Web888DigitalSlice>& slices, const QString& password)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // Validate replacement intent before releasing a working capture.
    Web888IqSplitter splitter;
    QString error;
    if (endpoint.trimmed().isEmpty() || !std::isfinite(centerHz) || centerHz < 10000 || centerHz > 32000000
        || !splitter.configure(slices, &error)) {
        emit failed(error.isEmpty() ? QStringLiteral("Invalid IQ capture endpoint or center") : error);
        return false;
    }
    stop();
    if (!m_processor) { emit failed(QStringLiteral("IQ processing service is unavailable")); return false; }
    m_processingSession = m_processor->open(slices, &error);
    if (!m_processingSession) { emit failed(error); return false; }
    m_lifetime = std::make_shared<std::atomic<bool>>(true);
    const auto generation = m_generation;
    auto* client = createClient();
    m_client = client;
    client->setReceiverFamily(KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
    client->setTrackedSlice(0, centerHz / 1e6, QStringLiteral("IQ"), -5500, 5500, {}, {}, 0);
    connect(client, &KiwiSdrClient::filteredIqReady, this, [this, generation](const KiwiIqBlock& block) {
        if (generation != m_generation) return;
        if (!m_processor) { stop(); emit failed(QStringLiteral("IQ processing service was destroyed")); return; }
        const auto submission = m_processor->submit(m_processingSession, block);
        if (submission == Web888IqProcessor::Submission::Invalid) {
            stop(); emit failed(QStringLiteral("Invalid IQ processing block"));
        }
    });
    connect(client, &KiwiSdrClient::stateChanged, this,
        [this, generation](KiwiSdrClient::State state, const QString& detail) {
            if (generation != m_generation) return;
            if (state == KiwiSdrClient::State::Error || state == KiwiSdrClient::State::Busy
                || state == KiwiSdrClient::State::Disconnected) {
                stop(); emit failed(detail);
            }
        });
    m_startupDeadline.start();
    client->connectToEndpoint(endpoint, password);
    return generation == m_generation;
}

void Web888IqCapture::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_generation;
    m_progressDeadline.stop();
    m_startupDeadline.stop();
    if (m_lifetime) m_lifetime->store(false, std::memory_order_release);
    m_lifetime.reset();
    m_ready = false;
    if (m_processor && m_processingSession) m_processor->close(m_processingSession);
    m_processingSession = 0;
    auto* client = m_client;
    m_client = nullptr;
    if (client) {
        QObject::disconnect(client, nullptr, this, nullptr);
        client->disconnectFromEndpoint();
        client->deleteLater();
    }
}
} // namespace AetherSDR
