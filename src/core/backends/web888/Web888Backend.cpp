#include "Web888Backend.h"
#include "Web888CapturePool.h"
#include "Web888IqProcessor.h"

#include "core/KiwiSdrClient.h"

#include <QUrl>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace AetherSDR {
namespace {
// Spectrum indices address reusable display slots, not monotonic slice IDs.
int displayIndex(int sliceId) { return sliceId == 0 ? 0 : 1; }
const QStringList kModes{QStringLiteral("AM"), QStringLiteral("USB"),
                         QStringLiteral("LSB"), QStringLiteral("CW")};
}

Web888Backend::Web888Backend(QObject* parent) : IRadioBackend(parent)
{
    m_receivers.emplace(0, std::make_shared<Receiver>());
}
Web888Backend::~Web888Backend() { stop(false); }

RadioCapabilities Web888Backend::capabilities() const
{
    RadioCapabilities caps;
    caps.family = QStringLiteral("web888");
    caps.connectionRecovery = ConnectionRecovery{ConnectionRecovery::Mode::Manual};
    caps.model = QStringLiteral("Web-888 (experimental)");
    caps.canTransmit = false;
    caps.extensionNamespaces.append(QStringLiteral("digital-monitor"));
    caps.maxSlices = 4;
    caps.canCreateSlices = true;
    caps.maxPanadapters = 2;
    caps.hasFmRepeaterOffset = false;
    caps.hasCwTune = false;
    caps.cwTextHasProgress = false;
    caps.cwTextHasStoredMacros = false;
    caps.cwTextSupportsLive = false;
    caps.cwTextSupportsSpeedModifiers = false;
    caps.radioOwnsDbmScale = false;
    caps.panAmplitude = PanAmplitudeModel{false, true};
    caps.panSpanModel = PanSpanModel{false, false};
    // These are engine setpoints, not an acknowledgement from the firmware.
    using Authority = SliceFrequencyControl::Authority;
    caps.sliceFrequencyControl = SliceFrequencyControl{Authority::Engine, 0, 0};
    caps.receiveModeControl = ReceiveModeControl{Authority::Engine, kModes};
    caps.receiveAudioControl = ReceiveAudioControl{Authority::Engine};
    return caps;
}

KiwiSdrClient* Web888Backend::createClient()
{
    return new KiwiSdrClient(KiwiSdrClient::Streams::SoundAndWaterfall,
                            KiwiSdrClient::Admission::IndependentReceiver, this);
}

Web888CapturePool* Web888Backend::createCapturePool()
{
    return new Web888CapturePool(this);
}

bool Web888Backend::applyDigitalProfile(const QVector<Web888DigitalPreset>& presets,
                                       double rate, int budget)
{
    if (!m_connected || m_stopping) { return false; }
    if (!m_capturePool) {
        m_capturePool = createCapturePool();
        connect(m_capturePool, &Web888CapturePool::profileReady,
                this, &Web888Backend::digitalProfileReady);
        connect(m_capturePool, &Web888CapturePool::profileFailed, this, [this](const QString& reason) {
            m_monitorError = reason;
            emit digitalProfileFailed(reason);
        });
        connect(m_capturePool, &Web888CapturePool::captureFailed, this,
                [this](quint64 id, const QString& reason) {
            m_monitorError = reason;
            m_decoderRouter.disarmCapture(id);
            emit digitalCaptureFailed(id, reason);
        });
        connect(m_capturePool, &Web888CapturePool::decoderAudioReady, this,
                [this](const Web888DecoderAudio& audio) {
            if (!m_connected || m_stopping || !audio.current()) { return; }
            const auto generation = m_generation;
            m_monitorSamples += audio.mono.size();
            emit digitalAudioReady(audio);
            if (m_connected && !m_stopping && generation == m_generation && audio.current()) {
                m_decoderRouter.push(audio);
            }
        });
    }
    // Reserve both supported listeners even while only the primary is open.
    // Profile replacement also counts active + pending captures in the pool.
    return m_capturePool->applyProfile(m_endpoint, presets, rate, budget, 2, m_password);
}

void Web888Backend::stopDigitalProfile()
{
    m_monitorSamples = 0;
    m_monitorError.clear();
    m_decoderRouter.clear();
    if (m_capturePool) { m_capturePool->stop(); }
}

bool Web888Backend::configureDigitalDecoders(const QVector<Web888DigitalPreset>& presets,
                                            quint64 budget, QString* error)
{
    if (!m_connected || m_stopping) { return false; }
    return m_decoderRouter.configure(presets, budget, error);
}

bool Web888Backend::armDigitalDecoder(const Web888DecoderAudio& first, const Web888DecoderClockAnchor& anchor)
{
    if (!m_connected || m_stopping || !m_capturePool || !first.current()
        || !m_capturePool->activeIds().contains(first.captureId)) { return false; }
    return m_decoderRouter.arm(first, anchor);
}

std::optional<Web888DecoderJob> Web888Backend::takeDigitalDecoderJob()
{
    if (!m_connected || m_stopping) { return std::nullopt; }
    return m_decoderRouter.takeNext();
}

int Web888Backend::digitalCaptureCount() const
{
    return m_capturePool ? m_capturePool->activeCount() : 0;
}

int Web888Backend::pendingDigitalCaptureCount() const
{
    return m_capturePool ? m_capturePool->pendingCount() : 0;
}

std::shared_ptr<Web888Backend::Receiver> Web888Backend::receiver(int id) const
{
    const auto it = m_receivers.find(id);
    if (it != m_receivers.end()) { return it->second; }
    const auto listener = m_listeners.find(id);
    return listener == m_listeners.end() ? nullptr : listener->second;
}

bool Web888Backend::current(int id, quint64 generation) const
{
    const auto r = receiver(id);
    return r && !m_stopping && r->generation == generation
        && (r->client || (r->owner >= 0 && receiver(r->owner) && receiver(r->owner)->client));
}

void Web888Backend::connectRadio(const RadioConnectRequest& request)
{
    if (m_stopping) {
        return;
    }
    const auto beforeStop = m_generation;
    stop(true);
    // A disconnected observer may already have started a replacement session.
    if (receiver(0)->client || m_generation > beforeStop + 1) {
        return;
    }
    ++m_generation;
    m_endpoint = request.params.value(QStringLiteral("web888.endpoint")).toString();
    if (m_endpoint.isEmpty()) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(request.host);
        url.setPort(request.port == 0 ? 8073 : request.port);
        m_endpoint = url.toString();
    }
    m_password = request.params.value(QStringLiteral("web888.password")).toString();
    openReceiver(0);
}

void Web888Backend::openReceiver(int id)
{
    const auto r = receiver(id);
    const quint64 generation = r->generation = ++m_nextLease;
    r->client = createClient();
    auto* client = r->client;
    const QString panId = QString::number(panIndex(id));
    client->setReceiverFamily(KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
    client->setDecodeAudioWhenInactive(true);
    client->setAudioActive(true);
    auto controls = client->receiverControls();
    controls.agcEnabled = r->agcMode != QStringLiteral("off");
    controls.agcDecayMs = r->agcMode == QStringLiteral("fast") ? KiwiSdrProtocol::kAgcFastDecayMs
        : r->agcMode == QStringLiteral("slow") ? KiwiSdrProtocol::kAgcSlowDecayMs
                                              : KiwiSdrProtocol::kAgcMedDecayMs;
    client->setReceiverControls(controls);

    connect(client, &KiwiSdrClient::stateChanged, this,
            [this, id, generation, r, panId](KiwiSdrClient::State state, const QString& detail) {
        if (!current(id, generation)) {
            return;
        }
        if (state == KiwiSdrClient::State::Connected) {
            if (r->connected) {
                return;
            }
            r->connected = true;
            if (id == 0) {
                m_connected = true;
                emit connected();
            }
            if (current(id, generation)) {
                publishSlice(id);
            }
        } else if (state != KiwiSdrClient::State::Connecting) {
            if (id == 0) {
                if (state != KiwiSdrClient::State::Disconnected) {
                    emit connectionError(detail);
                }
                if (current(id, generation)) {
                    stop(true);
                }
            } else {
                closeReceiver(id, true);
                emit sliceLifecycleFailed(QStringLiteral("create"), id, detail);
            }
        }
    });
    connect(client, &KiwiSdrClient::decodedAudioReady, this,
            [this, id, generation, r, panId](const QByteArray& pcm) {
        if (!r->sharedIq) { deliverAudio(id, generation, pcm); }
    });
    connect(client, &KiwiSdrClient::filteredIqReady, this,
            [this, id, generation, r](const KiwiIqBlock& block) {
        if (!current(id, generation) || !r->sharedIq || !m_listenerProcessor) { return; }
        if (block.sampleRateHz < 23900 || block.sampleRateHz > 24100
            || std::abs(block.centerHz - r->captureHz) > 1) {
            emit sliceLifecycleFailed(QStringLiteral("receive"), id,
                QStringLiteral("Shared SSB requires a 24 kHz IQ capture at the locked center"));
            return;
        }
        if (r->dspSession) { m_listenerProcessor->submit(r->dspSession, block); }
        for (const auto& [childId, child] : m_listeners) {
            if (child->owner == id && child->dspSession) {
                m_listenerProcessor->submit(child->dspSession, block);
            }
        }
    });
    connect(client, &KiwiSdrClient::waterfallRowReady, this,
            [this, id, generation, r, panId](const QString&, const QVector<float>& bins,
                               double low, double high, quint32) {
        if (!current(id, generation) || !r->connected || bins.isEmpty()
            || bins.size() > 65536 || !std::isfinite(low) || !std::isfinite(high)
            || high <= low || !std::all_of(bins.cbegin(), bins.cend(),
                                          [](float bin) { return std::isfinite(bin); })) {
            return;
        }
        // Bind this frame's geometry before delivery, including late rows
        // from the previous zoom. Never label it with the requested geometry.
        emit panCenterBandwidthChanged(panId, (low + high) / 2.0, high - low);
        if (current(id, generation)) {
            emit spectrumFrameReady(panIndex(id), QByteArray(reinterpret_cast<const char*>(bins.constData()),
                                                  bins.size() * sizeof(float)));
        }
    });
    connect(client, &KiwiSdrClient::meterReadingReady, this,
            [this, id, generation, r, panId](const KiwiSdrProtocol::MeterReading& reading) {
        if (!current(id, generation) || !r->connected || r->sharedIq || !reading.valid
            || !reading.hasDbm || !std::isfinite(reading.dbm)) {
            return;
        }
        if (!r->meterDefined) {
            r->meterDefined = true;
            MeterDef meter;
            meter.index = id;
            meter.source = QStringLiteral("SLC");
            meter.sourceIndex = id;
            meter.name = QStringLiteral("LEVEL");
            meter.unit = QStringLiteral("dBm");
            meter.low = -160;
            meter.high = 0;
            meter.description = QStringLiteral("Receiver-reported level; hardware calibration unverified");
            emit meterDefined(meter);
        }
        if (current(id, generation)) {
            emit meterUpdate(QStringLiteral("SLC%1:LEVEL").arg(id), reading.dbm);
        }
    });
    applyTune(id);
    if (!current(id, generation)) {
        return;
    }
    client->setWaterfallView(panId, r->panCenterHz / 1e6, r->panBandwidthHz / 1e6);
    if (!current(id, generation)) {
        return;
    }
    client->connectToEndpoint(m_endpoint, m_password);
}

void Web888Backend::deliverAudio(int id, quint64 generation, const QByteArray& pcm)
{
    const auto r = receiver(id);
    if (!current(id, generation) || !r->connected || pcm.isEmpty()
        || pcm.size() % (2 * sizeof(float)) != 0
        || pcm.size() > PcmFrame::kMaxFrames * 2 * qsizetype(sizeof(float))) {
        return;
    }
    // Decode/TCI receives clean slice PCM even with the speaker muted.
    // Reject non-finite samples before either route sees them.
    for (qsizetype offset = 0; offset < pcm.size(); offset += sizeof(float)) {
        float sample;
        std::memcpy(&sample, pcm.constData() + offset, sizeof(sample));
        if (!std::isfinite(sample)) {
            return;
        }
    }
    publishLegacySliceAudio(id, pcm);
    if (!current(id, generation)) {
        return;
    }
    if (id != m_activeSlice) {
        return;
    }
    QByteArray speaker = pcm;
    const float gain = r->muted ? 0.0f : r->gain / 100.0f;
    for (qsizetype offset = 0; offset < speaker.size(); offset += sizeof(float)) {
        float sample;
        std::memcpy(&sample, speaker.constData() + offset, sizeof(sample));
        sample *= gain;
        std::memcpy(speaker.data() + offset, &sample, sizeof(sample));
    }
    publishLegacyAudio(speaker);
}

void Web888Backend::closeReceiver(int id, bool notify)
{
    const auto r = receiver(id);
    if (!r || !r->client) { return; }
    QVector<int> retiredChildren;
    for (auto it = m_listeners.begin(); it != m_listeners.end();) {
        if (it->second->owner != id) { ++it; continue; }
        if (m_listenerProcessor) { m_listenerProcessor->close(it->second->dspSession); }
        it->second->dspSession = 0;
        it->second->connected = false;
        retiredChildren.append(it->first);
        it = m_listeners.erase(it);
    }
    if (m_listenerProcessor && r->dspSession) { m_listenerProcessor->close(r->dspSession); }
    r->dspSession = 0;
    r->sharedIq = false;
    auto* client = r->client;
    r->client = nullptr;
    const bool hadSlice = r->connected;
    const bool wasActive = m_activeSlice == id || retiredChildren.contains(m_activeSlice);
    if (wasActive) { m_activeSlice = 0; }
    r->connected = false;
    r->meterDefined = false;
    if (id != 0) { m_receivers.erase(id); }
    QObject::disconnect(client, nullptr, this, nullptr);
    client->disconnectFromEndpoint();
    client->deleteLater();
    if (notify && hadSlice) {
        const auto generation = m_generation;
        // Retire the reusable display slot before observers can replace its slice.
        emit panRemoved(QString::number(panIndex(id)));
        for (int childId : retiredChildren) {
            if (generation != m_generation) { break; }
            emit sliceRemoved(childId);
        }
        if (generation == m_generation) { emit sliceRemoved(id); }
    }
    if (wasActive && id != 0 && !m_stopping) { publishSlice(0); }
}

void Web888Backend::stop(bool notify)
{
    if (!receiver(0)->client || m_stopping) { return; }
    m_stopping = true;
    ++m_generation;
    retirePcmStreams();
    stopDigitalProfile();
    m_connected = false;
    m_password.clear();
    m_endpoint.clear();
    while (m_receivers.size() > 1) {
        closeReceiver(m_receivers.rbegin()->first, notify);
    }
    closeReceiver(0, notify);
    m_activeSlice = 0;
    m_stopping = false;
    if (notify) { emit disconnected(); }
}

bool Web888Backend::createPanadapter()
{
    if (!m_connected || m_stopping || m_receivers.size() >= 2
        || m_nextId == std::numeric_limits<int>::max()) { return false; }
    const int id = m_nextId++;
    auto r = std::make_shared<Receiver>();
    r->frequencyHz = r->panCenterHz = 14074000;
    r->mode = QStringLiteral("USB");
    r->lowHz = 300;
    r->highHz = 2800;
    m_receivers.emplace(id, r);
    openReceiver(id);
    return true;
}

int Web888Backend::sliceForPan(const QString& panId) const
{
    if (panId == QStringLiteral("0")) { return 0; }
    if (panId == QStringLiteral("1") && m_receivers.size() > 1) {
        return m_receivers.rbegin()->first;
    }
    return -1;
}

bool Web888Backend::removePanadapter(const QString& panId)
{
    const int id = sliceForPan(panId);
    if (id <= 0 || m_stopping) { return false; }
    closeReceiver(id, true);
    return true;
}

ReceiveDispatch Web888Backend::requestSliceControl(int id, const SliceControlRequest& request)
{
    if (!request.valid() || request.field != SliceControlRequest::Field::ActiveSlice) {
        return ReceiveDispatch::Unsupported;
    }
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) { return ReceiveDispatch::Unsupported; }
    if (!std::get<bool>(request.value)) { return ReceiveDispatch::LocalOnly; }
    setActiveSlice(id);
    return ReceiveDispatch::Dispatched;
}

void Web888Backend::setActiveSlice(int id)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping || id == m_activeSlice) { return; }
    const int old = m_activeSlice;
    m_activeSlice = id;
    publishSlice(old);
    if (m_activeSlice == id) { publishSlice(id); }
}

void Web888Backend::disconnectRadio() { stop(true); }

int Web888Backend::panIndex(int id) const
{
    const auto r = receiver(id);
    return displayIndex(r && r->owner >= 0 ? r->owner : id);
}

bool Web888Backend::fitsCapture(const Receiver& r, double hz, int low, int high) const
{
    const auto parent = r.owner < 0 ? &r : receiver(r.owner).get();
    if (!parent || !parent->client) { return false; }
    const double half = parent->client->filteredIqSampleRate() / 2 - 1000;
    return std::isfinite(hz) && hz > 0 && half > 0
        && hz + low >= parent->captureHz - half
        && hz + high <= parent->captureHz + half;
}

void Web888Backend::configureListener(int id)
{
    const auto r = receiver(id);
    if (!r || !m_listenerProcessor) { return; }
    if (r->dspSession) { m_listenerProcessor->close(r->dspSession); }
    r->audio.reset();
    r->audio.setMode(r->agcMode);
    QString error;
    r->dspSession = m_listenerProcessor->open({{id, r->frequencyHz,
        double(r->lowHz), double(r->highHz), r->mode == QStringLiteral("LSB")}}, &error);
    if (!r->dspSession) { emit sliceLifecycleFailed(QStringLiteral("configure"), id, error); }
}

bool Web888Backend::createSlice(const QString& panId, double hz)
{
    const int owner = sliceForPan(panId);
    const auto parent = receiver(owner);
    if (!m_connected || m_stopping || !parent || !parent->connected || parent->addingListener
        || (parent->mode != QStringLiteral("USB") && parent->mode != QStringLiteral("LSB"))
        || m_nextId == std::numeric_limits<int>::max()) { return false; }
    for (const auto& [id, listener] : m_listeners) {
        if (listener->owner == owner) { return false; }
    }
    // Admission uses the negotiated firmware rate, never the requested rate.
    const double rate = parent->client->filteredIqSampleRate();
    if (rate < 23900 || rate > 24100) {
        emit sliceLifecycleFailed(QStringLiteral("create"), -1,
            QStringLiteral("Shared SSB listening requires the receiver's 24 kHz setting"));
        return false;
    }
    if (!parent->sharedIq) { parent->captureHz = parent->frequencyHz; }
    if (!fitsCapture(*parent, hz, parent->lowHz, parent->highHz)
        || parent->highHz - parent->lowHz < 100 || parent->lowHz < -5000 || parent->highHz > 5000
        || (parent->mode == QStringLiteral("LSB") ? parent->highHz > 0 : parent->lowHz < 0)) {
        emit sliceLifecycleFailed(QStringLiteral("create"), -1,
            QStringLiteral("SSB passbands must fit inside the shared 24 kHz capture"));
        return false;
    }
    parent->addingListener = true;
    const auto releaseAdmission = qScopeGuard([parent] { parent->addingListener = false; });
    const auto parentGeneration = parent->generation;
    if (!m_listenerProcessor) {
        m_listenerProcessor = new Web888IqProcessor(this);
        connect(m_listenerProcessor, &Web888IqProcessor::processed, this,
            [this](quint64 session, double, const QVector<Web888DecoderAudio>& outputs, const QString& error) {
            int id = -1;
            for (const auto& [key, r] : m_receivers) { if (r->dspSession == session) { id = key; } }
            for (const auto& [key, r] : m_listeners) { if (r->dspSession == session) { id = key; } }
            const auto r = receiver(id);
            if (!r || !r->connected || m_stopping) { return; }
            if (!error.isEmpty()) { emit sliceLifecycleFailed(QStringLiteral("receive"), id, error); return; }
            const auto generation = r->generation;
            for (const auto& audio : outputs) {
                if (!current(id, generation) || r->dspSession != session) { return; }
                if (audio.discontinuity) { r->audio.reset(); }
                const auto pcm = r->audio.process(audio.mono);
                deliverAudio(id, generation, pcm);
            }
        });
    }
    if (!parent->sharedIq) {
        parent->sharedIq = true;
        auto controls = parent->client->receiverControls();
        controls.agcEnabled = false;
        parent->client->setReceiverControls(controls);
        parent->client->enableFilteredIq();
        configureListener(owner);
        publishSlice(owner);
        if (!m_connected || !current(owner, parentGeneration) || !parent->connected) { return false; }
    }
    const int id = m_nextId++;
    auto listener = std::make_shared<Receiver>();
    listener->owner = owner;
    listener->sharedIq = true;
    listener->connected = true;
    listener->generation = ++m_nextLease;
    listener->frequencyHz = hz;
    listener->mode = parent->mode;
    listener->lowHz = parent->lowHz;
    listener->highHz = parent->highHz;
    m_listeners.emplace(id, listener);
    configureListener(id);
    publishSlice(id);
    return true;
}

bool Web888Backend::removeSlice(int id)
{
    const auto it = m_listeners.find(id);
    if (it == m_listeners.end()) { return false; }
    const auto r = it->second;
    const auto generation = m_generation;
    if (m_listenerProcessor) { m_listenerProcessor->close(r->dspSession); }
    r->dspSession = 0;
    r->connected = false;
    m_listeners.erase(it);
    const bool active = m_activeSlice == id;
    if (active) { m_activeSlice = r->owner; }
    const auto parent = receiver(r->owner);
    if (parent && parent->client && parent->connected && !m_stopping) {
        if (m_listenerProcessor) { m_listenerProcessor->close(parent->dspSession); }
        parent->dspSession = 0;
        parent->sharedIq = false;
        // Restore the primary listener's own tuning before returning to audio.
        applyTune(r->owner);
        parent->client->disableFilteredIq();
        setSliceAgc(r->owner, parent->agcMode, 0);
    }
    if (!m_connected || generation != m_generation) { return true; }
    emit sliceRemoved(id);
    if (active && !m_stopping) { publishSlice(m_activeSlice); }
    return true;
}

void Web888Backend::publishSlice(int id)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) { return; }
    const QString panId = QString::number(panIndex(id));
    SliceDelta delta;
    delta.panId = panId;
    delta.letter = QString(QChar('A' + (r->owner < 0 ? panIndex(id) : 2 + panIndex(id))));
    delta.frequency = r->frequencyHz / 1e6;
    delta.mode = r->mode;
    delta.modeList = r->sharedIq ? QStringList{QStringLiteral("USB"), QStringLiteral("LSB")} : kModes;
    delta.filterLow = r->lowHz;
    delta.filterHigh = r->highHz;
    delta.active = id == m_activeSlice;
    delta.inUse = true;
    delta.txSlice = false;
    delta.audioGain = r->gain;
    delta.audioMute = r->muted;
    delta.agcMode = r->agcMode;
    emit sliceChanged(id, delta);
}

void Web888Backend::applyTune(int id)
{
    const auto r = receiver(id);
    if (!r) { return; }
    const QString panId = QString::number(panIndex(id));
    if (r->sharedIq) { configureListener(id); return; }
    if (r->client) {
        r->client->setTrackedSlice(id, r->frequencyHz / 1e6, r->mode, r->lowHz,
                                 r->highHz, panId, {}, 600);
    }
}

void Web888Backend::rejectSliceControl(int id, const QString& operation, const QString& reason)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) { return; }
    const auto generation = r->generation;
    // Desktop controls are optimistic. Restore the actual DSP state on refusal.
    publishSlice(id);
    if (current(id, generation)) { emit sliceLifecycleFailed(operation, id, reason); }
}

void Web888Backend::setSliceFrequency(int id, double hz)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) {
        return;
    }
    if (!std::isfinite(hz) || hz <= 0 || hz > std::numeric_limits<int>::max()) {
        rejectSliceControl(id, QStringLiteral("tune"), QStringLiteral("Invalid receive frequency"));
        return;
    }
    if (r->sharedIq && !fitsCapture(*r, hz, r->lowHz, r->highHz)) {
        rejectSliceControl(id, QStringLiteral("tune"),
            QStringLiteral("Listener passband is outside the shared 24 kHz capture"));
        return;
    }
    const auto parent = r->owner < 0 ? r : receiver(r->owner);
    const auto& metadata = parent->client->telemetry().metadata;
    if (metadata.hasCoverageBandwidth && hz > metadata.coverageBandwidthMhz * 1e6) {
        rejectSliceControl(id, QStringLiteral("tune"), QStringLiteral("Frequency exceeds receiver coverage"));
        return;
    }
    const auto generation = r->generation;
    r->frequencyHz = hz;
    applyTune(id);
    if (current(id, generation)) {
        publishSlice(id);
    }
}

void Web888Backend::setSliceMode(int id, const QString& mode)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) {
        return;
    }
    if (!kModes.contains(mode)) {
        rejectSliceControl(id, QStringLiteral("mode"), QStringLiteral("Unsupported receive mode"));
        return;
    }
    if (r->sharedIq && mode != QStringLiteral("USB") && mode != QStringLiteral("LSB")) {
        rejectSliceControl(id, QStringLiteral("mode"), QStringLiteral("Shared listeners support USB and LSB"));
        return;
    }
    const int nextLow = mode == QStringLiteral("LSB") ? -2800 : 300;
    const int nextHigh = mode == QStringLiteral("LSB") ? -300 : 2800;
    if (r->sharedIq && !fitsCapture(*r, r->frequencyHz, nextLow, nextHigh)) {
        rejectSliceControl(id, QStringLiteral("mode"), QStringLiteral("Mode passband exceeds the shared capture"));
        return;
    }
    const auto generation = r->generation;
    r->mode = mode;
    if (mode == QStringLiteral("LSB")) {
        r->lowHz = -2800;
        r->highHz = -300;
    } else if (mode == QStringLiteral("USB")) {
        r->lowHz = 300;
        r->highHz = 2800;
    } else if (mode == QStringLiteral("CW")) {
        r->lowHz = 300;
        r->highHz = 900;
    } else {
        r->lowHz = -3000;
        r->highHz = 3000;
    }
    applyTune(id);
    if (current(id, generation)) {
        publishSlice(id);
    }
}

void Web888Backend::setSliceFilter(int id, int low, int high)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) {
        return;
    }
    if (low >= high || low < -6000 || high > 6000) {
        rejectSliceControl(id, QStringLiteral("filter"), QStringLiteral("Invalid receive filter"));
        return;
    }
    if (r->sharedIq && (!fitsCapture(*r, r->frequencyHz, low, high)
        || high - low < 100 || low < -5000 || high > 5000
        || (r->mode == QStringLiteral("LSB") ? high > 0 : low < 0))) {
        rejectSliceControl(id, QStringLiteral("filter"), QStringLiteral("Filter does not fit the shared SSB capture"));
        return;
    }
    const auto generation = r->generation;
    r->lowHz = low;
    r->highHz = high;
    applyTune(id);
    if (current(id, generation)) {
        publishSlice(id);
    }
}

void Web888Backend::setSliceAgc(int id, const QString& mode, int thresholdDb)
{
    const auto r = receiver(id);
    Q_UNUSED(thresholdDb); // shared UI scale is not a Web-888 threshold in dBm
    if (!r || !r->connected || m_stopping || !capabilities().agcModes.contains(mode)) {
        return;
    }
    if (r->sharedIq) {
        r->agcMode = mode;
        r->audio.setMode(mode);
        publishSlice(id);
        return;
    }
    auto controls = r->client->receiverControls();
    controls.agcEnabled = mode != QStringLiteral("off");
    controls.agcDecayMs = mode == QStringLiteral("fast") ? KiwiSdrProtocol::kAgcFastDecayMs
        : mode == QStringLiteral("slow") ? KiwiSdrProtocol::kAgcSlowDecayMs
                                         : KiwiSdrProtocol::kAgcMedDecayMs;
    const auto generation = r->generation;
    r->agcMode = mode;
    r->client->setReceiverControls(controls);
    if (current(id, generation)) {
        publishSlice(id);
    }
}

ReceiveDispatch Web888Backend::requestSliceAudio(int id, const SliceAudioRequest& request)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping || !request.valid()
        || request.origin != SliceAudioRequest::Origin::Operator) {
        return ReceiveDispatch::Unsupported;
    }
    switch (request.field) {
    case SliceAudioRequest::Field::Gain:
        setSliceAudioGain(id, request.value);
        break;
    case SliceAudioRequest::Field::Mute:
        setSliceAudioMute(id, request.value != 0);
        break;
    case SliceAudioRequest::Field::Pan:
        return ReceiveDispatch::Unsupported;
    }
    return ReceiveDispatch::Dispatched;
}

void Web888Backend::setSliceAudioMute(int id, bool mute)
{
    const auto r = receiver(id);
    if (r && r->connected && !m_stopping) {
        r->muted = mute;
        publishSlice(id);
    }
}

void Web888Backend::setSliceAudioGain(int id, int gain)
{
    const auto r = receiver(id);
    if (r && r->connected && !m_stopping && gain >= 0 && gain <= 100) {
        r->gain = gain;
        publishSlice(id);
    }
}

void Web888Backend::setPanCenter(const QString& id, double hz, PanCenterIntent intent)
{
    const auto r = receiver(sliceForPan(id));
    const QString panId = id;
    Q_UNUSED(intent);
    if (r && r->connected && !m_stopping && std::isfinite(hz) && hz > 0) {
        r->panCenterHz = hz;
        r->client->setWaterfallView(panId, hz / 1e6, r->panBandwidthHz / 1e6);
    }
}

void Web888Backend::setPanBandwidth(const QString& id, double hz)
{
    const auto r = receiver(sliceForPan(id));
    const QString panId = id;
    if (r && r->connected && !m_stopping && std::isfinite(hz) && hz > 0) {
        r->panBandwidthHz = hz;
        r->client->setWaterfallView(panId, r->panCenterHz / 1e6, hz / 1e6);
    }
}

void Web888Backend::setKeying(bool key, const TxCoordinator::Operation& operation,
                            const TxCoordinator::Completion& completion)
{
    Q_UNUSED(key);
    Q_UNUSED(operation);
    completion.finish(); // RX-only: never forwards a keying command
}

void Web888Backend::invokeExtension(const QString& ns, const QString& verb,
                                  quint64 requestId, const QVariant& arg)
{
    const auto fail = [&](const QString& error) {
        if (requestId) { emit extensionError(requestId, error); }
    };
    const auto reply = [&](const QVariantMap& value) {
        if (requestId) { emit extensionResult(requestId, value); }
    };
    if (ns != QStringLiteral("digital-monitor")) {
        fail(QStringLiteral("Unsupported extension"));
        return;
    }
    if (!m_connected || m_stopping) {
        fail(QStringLiteral("Connect a receiver before monitoring"));
        return;
    }
    if (verb == QStringLiteral("stop")) {
        stopDigitalProfile();
        reply({{QStringLiteral("stopped"), true}});
        return;
    }
    if (verb == QStringLiteral("status")) {
        reply({{QStringLiteral("active"), digitalCaptureCount()},
               {QStringLiteral("pending"), pendingDigitalCaptureCount()},
               {QStringLiteral("samples"), QVariant::fromValue(m_monitorSamples)},
               {QStringLiteral("error"), m_monitorError},
               {QStringLiteral("timing"), QStringLiteral("Unqualified: timed decoding is not armed")}});
        return;
    }
    if (verb != QStringLiteral("plan") && verb != QStringLiteral("start")) {
        fail(QStringLiteral("Unknown monitoring action"));
        return;
    }
    const auto options = arg.toMap();
    const auto bands = options.value(QStringLiteral("bands")).toStringList();
    const auto modes = options.value(QStringLiteral("modes")).toStringList();
    const int rate = options.value(QStringLiteral("rate")).toInt();
    const int budget = options.value(QStringLiteral("budget")).toInt();
    const QStringList validBands{QStringLiteral("80m"), QStringLiteral("40m"), QStringLiteral("30m"),
        QStringLiteral("20m"), QStringLiteral("17m"), QStringLiteral("15m"), QStringLiteral("12m"), QStringLiteral("10m")};
    const QStringList validModes{QStringLiteral("FT8"), QStringLiteral("WSPR"), QStringLiteral("FT4")};
    if (bands.isEmpty() || modes.isEmpty() || bands.size() > 8 || modes.size() > 3
        || (rate != 12000 && rate != 24000) || budget < 2 || budget > 13
        || !std::all_of(bands.begin(), bands.end(), [&](const auto& b) { return validBands.contains(b); })
        || !std::all_of(modes.begin(), modes.end(), [&](const auto& m) { return validModes.contains(m); })) {
        fail(QStringLiteral("Select bands and modes, a 12/24 kHz receiver rate, and a budget of 2–13 RX slots"));
        return;
    }
    QVector<Web888DigitalPreset> presets;
    for (const auto& preset : web888DigitalPresets(true)) {
        if (bands.contains(preset.band) && modes.contains(preset.mode)) { presets.append(preset); }
    }
    const auto plan = planWeb888Captures(presets, rate, budget, 2);
    if (verb == QStringLiteral("start")) {
        if (!plan.fitsCapacity) { fail(plan.error); return; }
        m_monitorError.clear();
        if (!applyDigitalProfile(presets, rate, budget)) {
            fail(m_monitorError.isEmpty() ? QStringLiteral("Capture profile could not start") : m_monitorError);
            return;
        }
    }
    reply({{QStringLiteral("captures"), plan.windows.size()},
           {QStringLiteral("outputs"), presets.size()},
           {QStringLiteral("reserved"), 2},
           {QStringLiteral("fits"), plan.fitsCapacity},
           {QStringLiteral("error"), plan.error}});
}

} // namespace AetherSDR
