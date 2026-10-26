#include "NrspBackend.h"
#include "NrspWorker.h"
#include "core/AppActivity.h"

#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

Q_LOGGING_CATEGORY(lcNrsp, "aether.nrsp")

namespace AetherSDR {
namespace {
const QString kPanId = QStringLiteral("0xe2000000");
const QStringList kModes{QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("AM"),
    QStringLiteral("SAM"), QStringLiteral("FM"), QStringLiteral("CW"),
    QStringLiteral("DIGU"), QStringLiteral("DIGL")};
}

NrspBackend::NrspBackend(QObject* parent) : IRadioBackend(parent) {}
NrspBackend::~NrspBackend() { disconnectRadio(); }

RadioCapabilities NrspBackend::capabilities() const
{
    RadioCapabilities caps;
    caps.family = QStringLiteral("nrsp");
    caps.model = QStringLiteral("nRSP-ST");
    caps.manufacturer = QStringLiteral("SDRplay");
    caps.canTransmit = false;
    caps.hostModulates = false;
    caps.hasFmRepeaterOffset = false;
    caps.hasCwTune = false;
    caps.maxSlices = 1;
    caps.maxPanadapters = 1;
    caps.sampleRatesHz = {48000};
    caps.tuningMinHz = 1;
    caps.tuningMaxHz = 2000000000;
    const SliceFrequencyControl::Authority authority = SliceFrequencyControl::Authority::Engine;
    qint64 minimum = 1;
    qint64 maximum = 2000000000;
    if (m_connected && !m_source.value("owner").toBool()) {
        const qint64 frequency = m_source.value("frequencyHz").toLongLong();
        const qint64 radius = m_source.value("sampleRate").toLongLong() / 2 - 24000;
        minimum = std::max<qint64>(minimum, frequency - radius);
        maximum = std::min<qint64>(maximum, frequency + radius);
    }
    caps.sliceFrequencyControl = {authority, minimum, maximum};
    caps.receiveModeControl = ReceiveModeControl{authority, kModes};
    ReceiveFilterControl filters;
    filters.authority = authority;
    for (const QString& mode : kModes) {
        filters.modes.append(ReceiveFilterMode{mode, -22000, 21950, -21950, 22000, 50, 44000});
    }
    caps.receiveFilterControl = filters;
    caps.receiveAudioControl = ReceiveAudioControl{authority};
    caps.receiveAudioExport = ReceiveAudioExport{{24000}, 1};
    caps.receivePanCenterControl = ReceivePanRangeControl{authority, 1, 2000000000};
    caps.receivePanBandwidthControl = ReceivePanRangeControl{authority, 1000,
        m_source.isEmpty() ? 2000000 : m_source.value("sampleRate").toLongLong()};
    caps.panDisplayControl = PanDisplayControl{
        {PanDisplayField::Geometry, PanDisplayField::SpectrumPoints},
        {PanGeometryComponent::Center, PanGeometryComponent::Bandwidth}};
    caps.panSpanModel = PanSpanModel{false, false};
    caps.panAmplitude = PanAmplitudeModel{};
    caps.panAmplitude->binsAbsolute = true;
    caps.panAmplitude->clientPersistsDbmRange = false;
    caps.radioOwnsDbmScale = false;
    caps.hasAgcThreshold = true;
    caps.agcModes = {QStringLiteral("off"), QStringLiteral("slow"), QStringLiteral("med"), QStringLiteral("fast")};
    caps.extensionNamespaces = {QStringLiteral("nrsp")};
    return caps;
}

bool NrspBackend::accepts(quint64 generation) const
{
    return generation == m_generation && (m_connected || m_connecting);
}

void NrspBackend::connectRadio(const RadioConnectRequest& request)
{
    QPointer<NrspBackend> guard(this);
    disconnectRadio();
    if (!guard || m_connected || m_connecting) { return; }
    if (request.host.trimmed().isEmpty() || request.host.size() > 253) {
        emit connectionError(tr("Enter an nRSP-ST hostname or IP address."));
        return;
    }
    m_source.clear();
    m_reportedReceivePermission = false;
    m_connecting = true;
    const quint64 generation = ++m_generation;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("nRSP receive"));
    const std::shared_ptr<std::atomic<qsizetype>> deliveryBytes = std::make_shared<std::atomic<qsizetype>>(0);
    m_worker = new nrsp::NrspWorker(deliveryBytes);
    m_worker->moveToThread(m_thread);
    attachWorker(deliveryBytes);
    const QString host = request.host.trimmed();
    const quint16 port = request.port == 0 ? 50000 : request.port;
    const QHostAddress localAddress = request.localBindAddress;
    nrsp::NrspWorker* worker = m_worker;
    connect(m_thread, &QThread::started, worker,
        [worker, generation, host, port, localAddress,
         username = request.params.value(QStringLiteral("nrsp.username")).toString(),
         password = request.params.value(QStringLiteral("nrsp.password")).toString()]() mutable {
            worker->start(generation, host, port, localAddress, std::move(username), std::move(password));
        }, Qt::SingleShotConnection);
    startStreamThread(m_thread);
}

void NrspBackend::attachWorker(const std::shared_ptr<std::atomic<qsizetype>>& deliveryBytes)
{
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &nrsp::NrspWorker::ready, this,
        [this](quint64 token, const QVariantMap& source) {
            if (!accepts(token)) { return; }
            m_connecting = false;
            m_connected = true;
            m_source = source;
            m_center = source.value("frequencyHz").toDouble();
            m_bandwidth = source.value("sampleRate").toDouble() * nrsp::NrspWorker::kInitialSpanFraction;
            QPointer<NrspBackend> self(this);
            emit connected();
            if (!self || !accepts(token)) { return; }
            emit panCenterBandwidthChanged(kPanId, m_center / 1e6, m_bandwidth / 1e6);
            if (!self || !accepts(token)) { return; }
            publishIdentity();
            if (!self || !accepts(token)) { return; }
            applySource(source);
            if (!self || !accepts(token)) { return; }
            emit panRangeChanged(kPanId, -220, -100);
        });
    connect(m_worker, &nrsp::NrspWorker::sourceChanged, this,
        [this](quint64 token, const QVariantMap& source) {
            if (accepts(token)) { applySource(source); }
        });
    connect(m_worker, &nrsp::NrspWorker::sliceChanged, this,
        [this](quint64 token, const SliceDelta& delta) {
            if (accepts(token) && m_connected) { emit sliceChanged(0, delta); }
        });
    connect(m_worker, &nrsp::NrspWorker::panChanged, this,
        [this](quint64 token, double center, double bandwidth) {
            if (!accepts(token) || !m_connected) { return; }
            m_center = center;
            m_bandwidth = bandwidth;
            emit panCenterBandwidthChanged(kPanId, center / 1e6, bandwidth / 1e6);
        });
    connect(m_worker, &nrsp::NrspWorker::audioReady, this,
        [this, deliveryBytes](quint64 token, const QByteArray& pcm) {
            deliveryBytes->fetch_sub(pcm.size(), std::memory_order_relaxed);
            if (accepts(token) && m_connected) { publishAudio(pcm); }
        });
    connect(m_worker, &nrsp::NrspWorker::spectrumReady, this,
        [this, deliveryBytes](quint64 token, const QByteArray& bins) {
            deliveryBytes->fetch_sub(bins.size(), std::memory_order_relaxed);
            if (accepts(token) && m_connected) { emit spectrumFrameReady(0, bins); }
        });
    connect(m_worker, &nrsp::NrspWorker::failed, this,
        [this](quint64 token, const QString& reason) { if (accepts(token)) { fail(reason); } });
    connect(m_worker, &nrsp::NrspWorker::warning, this,
        [this](quint64 token, const QString& reason) {
            if (accepts(token)) { emit configurationWarning(reason); }
        });
}

void NrspBackend::disconnectRadio()
{
    const bool active = m_connected || m_connecting;
    m_connected = false;
    m_connecting = false;
    ++m_generation;
    retirePcmStreams();
    QThread* thread = std::exchange(m_thread, nullptr);
    const QPointer<nrsp::NrspWorker> worker = m_worker;
    m_worker.clear();
    if (thread) {
        if (worker) {
            QObject::disconnect(worker, nullptr, this, nullptr);
            QMetaObject::invokeMethod(worker, [worker] { if (worker) { worker->stop(); } }, Qt::QueuedConnection);
        } else {
            thread->quit();
        }
        if (!thread->wait(2000)) {
            // The forward gate is already closed. A slow native DSP teardown
            // must not hold the owner thread or be destroyed while running.
            qCWarning(lcNrsp) << "Receive worker is still draining after two seconds";
            thread->setParent(nullptr);
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            if (!thread->isRunning()) { thread->deleteLater(); }
        } else {
            thread->deleteLater();
        }
    }
    if (active) { emit disconnected(); }
}

void NrspBackend::fail(const QString& reason)
{
    qCWarning(lcNrsp).noquote() << reason;
    QPointer<NrspBackend> guard(this);
    const quint64 generation = m_generation;
    emit connectionError(reason);
    if (guard && generation == m_generation) { disconnectRadio(); }
}

void NrspBackend::post(std::function<void(nrsp::NrspWorker&)> action)
{
    if (!m_connected || !m_worker) { return; }
    nrsp::NrspWorker* worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, action = std::move(action)] { action(*worker); }, Qt::QueuedConnection);
}

void NrspBackend::publishIdentity()
{
    SliceDelta slice;
    slice.panId = kPanId;
    slice.frequency = m_source.value("frequencyHz").toDouble() / 1e6;
    slice.mode = QStringLiteral("USB");
    slice.filterLow = 300;
    slice.filterHigh = 2700;
    slice.letter = QStringLiteral("A");
    slice.active = true;
    slice.inUse = true;
    slice.txSlice = false;
    slice.modeList = kModes;
    slice.txAntenna = QString();
    slice.txAntennaList = QStringList{};
    slice.audioGain = m_gain;
    slice.audioPan = m_pan;
    slice.audioMute = m_muted;
    emit sliceChanged(0, slice);
}

void NrspBackend::applySource(const QVariantMap& source)
{
    const bool lostControl = m_reportedReceivePermission && m_source.value("owner").toBool()
        && !source.value("owner").toBool();
    m_source = source;
    QPointer<NrspBackend> guard(this);
    const quint64 generation = m_generation;
    emit capabilitiesChanged();
    if (!guard || !accepts(generation)) { return; }
    const QStringList antennas{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    const int index = source.value("antenna").toInt();
    if (index < 0 || index >= antennas.size()) { return; }
    SliceDelta delta;
    delta.rxAntenna = antennas[index];
    delta.rxAntennaList = source.value("owner").toBool() ? antennas : QStringList{antennas[index]};
    emit sliceChanged(0, delta);
    if (!guard || !accepts(generation)) { return; }
    emit panAntennaListChanged(kPanId, *delta.rxAntennaList);
    if (!guard || !accepts(generation)) { return; }
    emit panRxAntennaChanged(kPanId, antennas[index]);
    if (!guard || !accepts(generation)) { return; }
    emit panBandwidthLimitsChanged(kPanId, 0.001, source.value("sampleRate").toDouble() / 1e6);
    if (!guard || !accepts(generation)) { return; }
    emit extensionStatus(QStringLiteral("nrsp"), QStringLiteral("source"), source);
    if (!guard || !accepts(generation)) { return; }
    if (lostControl) {
        emit configurationWarning(tr("nRSP-ST source control was transferred. Reception continues; tuning is limited to the current capture."));
    }
}

void NrspBackend::publishMonitor()
{
    if (!m_connected) { return; }
    SliceDelta delta;
    delta.audioGain = m_gain;
    delta.audioPan = m_pan;
    delta.audioMute = m_muted;
    emit sliceChanged(0, delta);
}

void NrspBackend::publishAudio(const QByteArray& pcm)
{
    if (pcm.size() % 8 != 0) { return; }
    QPointer<NrspBackend> guard(this);
    const quint64 generation = m_generation;
    if (!m_reportedReceivePermission) {
        m_reportedReceivePermission = true;
        if (!m_source.value("owner").toBool()) {
            emit configurationWarning(tr("Receiving from nRSP-ST without source control. Tune within the current capture; antenna and shared RF settings require control permission."));
            if (!guard || !accepts(generation)) { return; }
        }
    }
    QByteArray speaker = pcm;
    const float gain = m_muted ? 0.0f : m_gain / 100.0f;
    const float leftGain = gain * std::min(1.0f, (100 - m_pan) / 50.0f);
    const float rightGain = gain * std::min(1.0f, m_pan / 50.0f);
    for (qsizetype offset = 0; offset < speaker.size(); offset += 8) {
        float values[2];
        std::memcpy(values, speaker.constData() + offset, sizeof(values));
        values[0] *= leftGain;
        values[1] *= rightGain;
        std::memcpy(speaker.data() + offset, values, sizeof(values));
    }
    publishLegacySliceAudio(0, pcm);
    if (!guard || !accepts(generation)) { return; }
    publishLegacyAudio(speaker);
}

void NrspBackend::setSliceFrequency(int id, double hz)
{
    requestReceiveTune(id, hz, ReceiveTuneView::Preserve);
}
bool NrspBackend::requestReceiveTune(int id, double hz, ReceiveTuneView view)
{
    const SliceFrequencyControl bounds = capabilities().sliceFrequencyControl;
    if (!m_connected || id != 0 || !std::isfinite(hz)
        || hz < bounds.minimumHz || hz > bounds.maximumHz) { return false; }
    nrsp::NrspWorker::TuneView placement = nrsp::NrspWorker::TuneView::Preserve;
    if (view == ReceiveTuneView::Reveal) { placement = nrsp::NrspWorker::TuneView::Reveal; }
    if (view == ReceiveTuneView::Center) { placement = nrsp::NrspWorker::TuneView::Center; }
    post([hz, placement](nrsp::NrspWorker& worker) { worker.tune(hz, placement); });
    return true;
}
void NrspBackend::setSliceMode(int id, const QString& mode)
{
    if (id == 0 && kModes.contains(mode)) { post([mode](nrsp::NrspWorker& worker) { worker.setMode(mode); }); }
}
void NrspBackend::setSliceFilter(int id, int low, int high)
{
    if (id == 0) { post([low, high](nrsp::NrspWorker& worker) { worker.setFilter(low, high); }); }
}
void NrspBackend::setSliceAgc(int id, const QString& mode, int threshold)
{
    if (id == 0) { post([mode, threshold](nrsp::NrspWorker& worker) { worker.setAgc(mode, threshold); }); }
}
void NrspBackend::setCwPitch(int hz)
{
    post([hz](nrsp::NrspWorker& worker) { worker.setCwPitch(hz); });
}
void NrspBackend::setSliceRxAntenna(int id, const QString& antenna)
{
    if (id == 0) { post([antenna](nrsp::NrspWorker& worker) { worker.setAntenna(antenna); }); }
}
ReceiveDispatch NrspBackend::requestSliceRxAntenna(int id, const QString& antenna)
{
    if (!m_connected || id != 0) { return ReceiveDispatch::Unsupported; }
    const QStringList antennas{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    const bool allowed = m_source.value("owner").toBool() && antennas.contains(antenna)
        && (antenna != "C" || m_source.value("frequencyHz").toULongLong() <= 200000000ull);
    // Reassert the observation while a command is pending or refused. A
    // successful command publishes its fresh hello readback.
    SliceDelta observed;
    observed.rxAntenna = antennas.value(m_source.value("antenna").toInt());
    QPointer<NrspBackend> guard(this);
    const quint64 generation = m_generation;
    emit sliceChanged(0, observed);
    if (!guard || !accepts(generation)) { return ReceiveDispatch::Unsupported; }
    if (!allowed) {
        const QString reason = !m_source.value("owner").toBool()
            ? tr("Antenna selection requires source control. This connection can receive but cannot change the shared antenna.")
            : antenna == "C"
                ? tr("Antenna C requires a source frequency at or below 200 MHz. Tune the source below that limit first.")
                : tr("The nRSP-ST antenna inputs are A, B, and C.");
        // Publish after the generic unsupported-dispatch notice, so the
        // actionable radio-specific explanation remains visible.
        QMetaObject::invokeMethod(this, [this, generation, reason] {
            if (accepts(generation)) { emit configurationWarning(reason); }
        }, Qt::QueuedConnection);
        return ReceiveDispatch::Unsupported;
    }
    setSliceRxAntenna(id, antenna);
    return ReceiveDispatch::Dispatched;
}
ReceiveDispatch NrspBackend::requestSliceAudio(int id, const SliceAudioRequest& request)
{
    if (!m_connected || id != 0 || !request.valid()) { return ReceiveDispatch::Unsupported; }
    switch (request.field) {
    case SliceAudioRequest::Field::Gain: setSliceAudioGain(id, request.value); break;
    case SliceAudioRequest::Field::Mute: setSliceAudioMute(id, request.value != 0); break;
    case SliceAudioRequest::Field::Pan: setSliceAudioPan(id, request.value); break;
    }
    return ReceiveDispatch::Dispatched;
}
void NrspBackend::setSliceAudioGain(int id, int gain)
{
    if (id == 0) { m_gain = std::clamp(gain, 0, 100); publishMonitor(); }
}
void NrspBackend::setSliceAudioMute(int id, bool mute)
{
    if (id == 0) { m_muted = mute; publishMonitor(); }
}
void NrspBackend::setSliceAudioPan(int id, int pan)
{
    if (id == 0) { m_pan = std::clamp(pan, 0, 100); publishMonitor(); }
}
void NrspBackend::setPanCenter(const QString& pan, double hz, PanCenterIntent)
{
    if (pan == kPanId) {
        post([hz](nrsp::NrspWorker& worker) { worker.setPanCenter(hz); });
    }
}
void NrspBackend::setPanBandwidth(const QString& pan, double hz)
{
    if (pan == kPanId) {
        post([hz](nrsp::NrspWorker& worker) { worker.setPanBandwidth(hz); });
    }
}
void NrspBackend::setPanPixelWidth(const QString& pan, int pixels)
{
    if (pan == kPanId) { post([pixels](nrsp::NrspWorker& worker) { worker.setDisplayWidth(pixels); }); }
}
void NrspBackend::setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&) {}

void NrspBackend::invokeExtension(const QString& ns, const QString& verb, quint64 id, const QVariant& argument)
{
    if (ns != "nrsp" || !m_connected) {
        emit extensionError(id, tr("No active nRSP-ST session."));
        return;
    }
    if (verb == "source") {
        emit extensionResult(id, m_source);
        return;
    }
    if ((verb != "rfAgc" && verb != "rfNotch" && verb != "dabNotch")
        || argument.metaType().id() != QMetaType::Bool || !m_source.value("owner").toBool()) {
        emit extensionError(id, tr("Unsupported nRSP-ST control or missing source ownership."));
        return;
    }
    const bool enabled = argument.toBool();
    post([verb, enabled](nrsp::NrspWorker& worker) { worker.frontEndControl(verb, enabled); });
    emit extensionResult(id, QVariantMap{{QStringLiteral("dispatched"), true}});
}

} // namespace AetherSDR
