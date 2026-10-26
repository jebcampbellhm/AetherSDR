#include "Web888Backend.h"
#include "Web888CapturePool.h"

#include "core/KiwiSdrClient.h"
#include "core/NetworkReceiverCredentials.h"
#include <QTimer>
#include <QPointer>

#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace AetherSDR {
namespace {
// Spectrum indices address reusable display slots, not monotonic slice IDs.
int displayIndex(int sliceId) { return sliceId == 0 ? 0 : 1; }
const QStringList kModes{QStringLiteral("AM"), QStringLiteral("USB"),
                         QStringLiteral("LSB"), QStringLiteral("CW"),
                         QStringLiteral("SAM"), QStringLiteral("FM")};
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
    caps.maxSlices = 13;
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
    caps.receiveAudioControl = ReceiveAudioControl{Authority::Engine, ReceiveAudioControl::SpeakerPan::Backend};
    caps.receiveAudioBuffering = ReceiveAudioBuffering{360};
    return caps;
}

KiwiSdrClient* Web888Backend::createClient()
{
    return new KiwiSdrClient(KiwiSdrClient::Streams::SoundAndWaterfall,
                            KiwiSdrClient::Admission::IndependentReceiver, this);
}

KiwiSdrClient* Web888Backend::createSoundClient()
{
    return new KiwiSdrClient(KiwiSdrClient::Streams::SoundOnly,
                            KiwiSdrClient::Admission::IndependentReceiver, this);
}

int Web888Backend::listeningReservation() const
{
    return 2 + static_cast<int>(m_listeners.size());
}

bool Web888Backend::canAddReceiver() const
{
    return listeningReservation()
        + digitalCaptureCount() + pendingDigitalCaptureCount() < m_receiverBudget;
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
    // Keep room for both waterfall owners plus all sound-only listeners.
    // Profile replacement also counts active + pending captures in the pool.
    const int previousBudget = m_receiverBudget;
    m_receiverBudget = budget;
    const bool accepted = m_capturePool->applyProfile(m_endpoint, presets, rate, budget,
                                                     listeningReservation(), m_password);
    if (!accepted) { m_receiverBudget = previousBudget; }
    return accepted;
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
    m_reportedVersion.clear();
    m_receiverBudget = 13;
    m_saveCredentials = false;
    m_endpoint = request.params.value(QStringLiteral("web888.endpoint")).toString();
    if (m_endpoint.isEmpty()) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(request.host);
        url.setPort(request.port == 0 ? 8073 : request.port);
        m_endpoint = url.toString();
    }
    if (request.params.contains(QStringLiteral("web888.password"))) {
        m_password = request.params.value(QStringLiteral("web888.password")).toString();
        NetworkReceiverCredentials::setSessionPassword(m_endpoint, m_password);
        m_saveCredentials = !m_password.isEmpty();
        openReceiver(0);
        return;
    }
    m_loadingCredentials = true;
    const auto generation = m_generation;
    QTimer::singleShot(30000, this, [this, generation] {
        if (generation != m_generation || !m_loadingCredentials) { return; }
        const QPointer<Web888Backend> self(this);
        emit connectionError(tr("Web-888 credential loading timed out."));
        if (self && generation == m_generation) { stop(true); }
    });
    NetworkReceiverCredentials::load(m_endpoint, this,
        [this, generation](const QString& password, const QString& error) {
            if (generation != m_generation || !m_loadingCredentials) { return; }
            if (!error.isEmpty()) {
                const QPointer<Web888Backend> self(this);
                emit connectionError(tr("Cannot load the Web-888 password: %1").arg(error));
                if (self && generation == m_generation) { stop(true); }
                return;
            }
            m_loadingCredentials = false;
            m_password = password;
            m_saveCredentials = true;
            openReceiver(0);
        });
}

void Web888Backend::openReceiver(int id)
{
    const auto r = receiver(id);
    const quint64 generation = r->generation = ++m_nextLease;
    r->initialRange = false;
    r->client = r->owner < 0 ? createClient() : createSoundClient();
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
                if (m_saveCredentials) {
                    NetworkReceiverCredentials::saveAccepted(m_endpoint, m_password, this,
                        [this, id, generation](const QString& error) {
                            if (current(id, generation) && !error.isEmpty()) {
                                emit configurationWarning(error);
                            }
                        });
                }
                if (!current(id, generation)) { return; }
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
    connect(client, &KiwiSdrClient::telemetryChanged, this,
            [this, id, generation](const KiwiSdrReceiverTelemetry& telemetry) {
        const QString version = telemetry.protocol.serverVersion;
        if (id != 0 || !current(id, generation) || !m_connected || version.isEmpty()
            || version.size() > 128 || version == m_reportedVersion) { return; }
        for (QChar ch : version) {
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-')
                && ch != QLatin1Char('_') && ch != QLatin1Char('+')) { return; }
        }
        m_reportedVersion = version;
        RadioDelta delta; delta.version = version;
        emit radioChanged(delta);
    });
    connect(client, &KiwiSdrClient::decodedAudioReady, this,
            [this, id, generation, r, panId](const QByteArray& pcm) {
        deliverAudio(id, generation, pcm);
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
        if (!r->initialRange && bins.size() > 32) {
            auto levels = bins;
            std::sort(levels.begin(), levels.end());
            const double floor = levels[levels.size() / 5];
            r->initialRange = true;
            const QPointer<Web888Backend> self(this);
            emit panRangeChanged(panId, floor - 25, floor + 85);
            if (!self || !current(id, generation)) { return; }
        }
        const double viewLow = (r->panCenterHz - r->panBandwidthHz / 2) / 1e6;
        const double viewWidth = r->panBandwidthHz / 1e6;
        QVector<float> view(bins.size(), -200.0f);
        for (qsizetype i = 0; i < view.size(); ++i) {
            const double frequency = viewLow + (i + 0.5) * viewWidth / view.size();
            const double position = (frequency - low) / (high - low) * bins.size() - 0.5;
            if (position < 0 || position > bins.size() - 1) { continue; }
            const auto left = static_cast<qsizetype>(position);
            const auto right = std::min(left + 1, bins.size() - 1);
            view[i] = bins[left] + (bins[right] - bins[left]) * (position - left);
        }
        emit panCenterBandwidthChanged(panId, r->panCenterHz / 1e6, viewWidth);
        if (current(id, generation)) {
            emit spectrumFrameReady(panIndex(id), QByteArray(reinterpret_cast<const char*>(view.constData()),
                                                  view.size() * sizeof(float)));
        }
    });
    connect(client, &KiwiSdrClient::waterfallAvailabilityChanged, this,
            [this, id, generation](bool available, const QString& detail) {
        if (current(id, generation) && receiver(id)->owner < 0 && receiver(id)->connected && !available && !detail.isEmpty()) {
            emit configurationWarning(detail);
        }
    });
    connect(client, &KiwiSdrClient::meterReadingReady, this,
            [this, id, generation, r, panId](const KiwiSdrProtocol::MeterReading& reading) {
        if (!current(id, generation) || !r->connected || !reading.valid
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
    QTimer::singleShot(30000, this, [this, id, generation] {
        if (!current(id, generation) || receiver(id)->connected) { return; }
        if (id == 0) {
            emit connectionError(tr("Web-888 connection timed out."));
            if (current(id, generation)) { stop(true); }
        } else {
            closeReceiver(id, true);
            emit sliceLifecycleFailed(QStringLiteral("create"), id, tr("Connection timed out."));
        }
    });
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
        const bool left = (offset / sizeof(float)) % 2 == 0;
        sample *= gain * std::min(1.0f, left ? 2 * (1 - r->pan / 100.0f) : 2 * r->pan / 100.0f);
        std::memcpy(speaker.data() + offset, &sample, sizeof(sample));
    }
    publishLegacyAudio(speaker);
}

void Web888Backend::closeReceiver(int id, bool notify)
{
    const auto r = receiver(id);
    if (!r || !r->client) { return; }
    const int index = panIndex(id);
    if (r->owner >= 0) {
        auto* client = r->client;
        r->client = nullptr;
        const bool hadSlice = r->connected;
        r->connected = false;
        m_listeners.erase(id);
        const bool active = m_activeSlice == id;
        if (active) { m_activeSlice = r->owner; }
        QObject::disconnect(client, nullptr, this, nullptr);
        client->disconnectFromEndpoint();
        client->deleteLater();
        if (notify && hadSlice) { emit sliceRemoved(id); }
        if (active && !m_stopping) { publishSlice(m_activeSlice); }
        return;
    }
    QVector<int> children;
    QVector<int> publishedChildren;
    for (const auto& [childId, child] : m_listeners) {
        if (child->owner == id) {
            children.append(childId);
            if (child->connected) { publishedChildren.append(childId); }
        }
    }
    auto* client = r->client;
    r->client = nullptr;
    const bool hadSlice = r->connected;
    const bool wasActive = m_activeSlice == id || children.contains(m_activeSlice);
    r->connected = false;
    r->meterDefined = false;
    if (id != 0) { m_receivers.erase(id); }
    // Retire every child before observers can open replacements on this pan.
    if (wasActive) { m_activeSlice = 0; }
    for (int childId : children) { closeReceiver(childId, false); }
    QObject::disconnect(client, nullptr, this, nullptr);
    client->disconnectFromEndpoint();
    client->deleteLater();
    if (notify && hadSlice) {
        const auto generation = m_generation;
        emit panRemoved(QString::number(index));
        for (int childId : publishedChildren) {
            if (generation != m_generation) { break; }
            emit sliceRemoved(childId);
        }
        if (generation == m_generation) { emit sliceRemoved(id); }
    }
    if (wasActive && id != 0 && !m_stopping) { publishSlice(0); }
}

void Web888Backend::stop(bool notify)
{
    if ((!receiver(0)->client && !m_loadingCredentials) || m_stopping) { return; }
    m_loadingCredentials = false;
    m_saveCredentials = false;
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
    if (!m_connected || m_stopping || m_receivers.size() + m_listeners.size()
        + digitalCaptureCount() + pendingDigitalCaptureCount() >= size_t(m_receiverBudget)
        || m_receivers.size() >= 2
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

bool Web888Backend::createSlice(const QString& panId, double hz)
{
    const int owner = sliceForPan(panId);
    const auto parent = receiver(owner);
    if (!m_connected || m_stopping || !parent || !parent->connected
        || !std::isfinite(hz) || hz <= 0 || hz > 64000000
        || m_nextId == std::numeric_limits<int>::max()) { return false; }
    const auto& metadata = parent->client->telemetry().metadata;
    if (metadata.hasCoverageBandwidth && hz > metadata.coverageBandwidthMhz * 1e6) {
        emit sliceLifecycleFailed(QStringLiteral("create"), -1,
            QStringLiteral("Frequency exceeds receiver coverage"));
        return false;
    }
    if (!canAddReceiver()) {
        emit sliceLifecycleFailed(QStringLiteral("create"), -1,
            QStringLiteral("No RX slot remains in the local receiver budget"));
        return false;
    }
    const int id = m_nextId++;
    auto listener = std::make_shared<Receiver>();
    listener->owner = owner;
    listener->frequencyHz = hz;
    listener->mode = parent->mode;
    listener->lowHz = parent->lowHz;
    listener->highHz = parent->highHz;
    m_listeners.emplace(id, listener); // Pending opens count against capacity.
    openReceiver(id); // Fresh /status preflight, followed by server admission.
    return true;
}

bool Web888Backend::removeSlice(int id)
{
    if (m_stopping || !m_listeners.contains(id)) { return false; }
    closeReceiver(id, true);
    return true;
}

void Web888Backend::publishSlice(int id)
{
    const auto r = receiver(id);
    if (!r || !r->connected || m_stopping) { return; }
    const QString panId = QString::number(panIndex(id));
    SliceDelta delta;
    delta.panId = panId;
    delta.letter = QString(QChar('A' + id % 26));
    delta.frequency = r->frequencyHz / 1e6;
    delta.mode = r->mode;
    delta.modeList = kModes;
    delta.filterLow = r->lowHz;
    delta.filterHigh = r->highHz;
    delta.active = id == m_activeSlice;
    delta.inUse = true;
    delta.txSlice = false;
    delta.audioPan = r->pan;
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
    // Desktop controls are optimistic. Restore the accepted state on refusal.
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

    const auto& metadata = r->client->telemetry().metadata;
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
        setSliceAudioPan(id, request.value);
        break;
    }
    return ReceiveDispatch::Dispatched;
}

void Web888Backend::setSliceAudioPan(int id, int pan)
{
    const auto r = receiver(id);
    if (r && r->connected && !m_stopping && pan >= 0 && pan <= 100) {
        r->pan = pan;
        publishSlice(id);
    }
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

void Web888Backend::setPanFrameRate(const QString& id, int fps)
{
    const auto r = receiver(sliceForPan(id));
    if (r && r->connected && !m_stopping && fps > 0) {
        r->client->setWaterfallRateOverride(fps >= 16 ? 4 : fps >= 8 ? 3 : fps >= 4 ? 2 : 1);
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
    const auto plan = planWeb888Captures(presets, rate, budget, listeningReservation());
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
           {QStringLiteral("reserved"), listeningReservation()},
           {QStringLiteral("fits"), plan.fitsCapacity},
           {QStringLiteral("error"), plan.error}});
}

} // namespace AetherSDR
