#include "Web888Backend.h"

#include "core/KiwiSdrClient.h"

#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace AetherSDR {
namespace {
const QString kPanId = QStringLiteral("0");
const QStringList kModes{QStringLiteral("AM"), QStringLiteral("USB"),
                         QStringLiteral("LSB"), QStringLiteral("CW")};
}

Web888Backend::Web888Backend(QObject* parent) : IRadioBackend(parent) {}
Web888Backend::~Web888Backend() { stop(false); }

RadioCapabilities Web888Backend::capabilities() const
{
    RadioCapabilities caps;
    caps.family = QStringLiteral("web888");
    caps.connectionRecovery = ConnectionRecovery{ConnectionRecovery::Mode::Manual};
    caps.model = QStringLiteral("Web-888 (experimental)");
    caps.canTransmit = false;
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

bool Web888Backend::current(quint64 generation) const
{
    return m_client && !m_stopping && m_generation == generation;
}

void Web888Backend::connectRadio(const RadioConnectRequest& request)
{
    if (m_stopping) {
        return;
    }
    const auto beforeStop = m_generation;
    stop(true);
    // A disconnected observer may already have started a replacement session.
    if (m_client || m_generation > beforeStop + 1) {
        return;
    }
    const quint64 generation = ++m_generation;
    m_client = createClient();
    auto* client = m_client;
    client->setReceiverFamily(KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
    client->setDecodeAudioWhenInactive(true);
    client->setAudioActive(true);
    auto controls = client->receiverControls();
    controls.agcEnabled = m_agcMode != QStringLiteral("off");
    controls.agcDecayMs = m_agcMode == QStringLiteral("fast") ? KiwiSdrProtocol::kAgcFastDecayMs
        : m_agcMode == QStringLiteral("slow") ? KiwiSdrProtocol::kAgcSlowDecayMs
                                              : KiwiSdrProtocol::kAgcMedDecayMs;
    client->setReceiverControls(controls);

    connect(client, &KiwiSdrClient::stateChanged, this,
            [this, generation](KiwiSdrClient::State state, const QString& detail) {
        if (!current(generation)) {
            return;
        }
        if (state == KiwiSdrClient::State::Connected) {
            if (m_connected) {
                return;
            }
            m_connected = true;
            emit connected();
            if (!current(generation)) {
                return;
            }
            publishSlice();
        } else if (state != KiwiSdrClient::State::Connecting) {
            // Manual recovery is declared through the neutral capability.
            if (state != KiwiSdrClient::State::Disconnected) {
                emit connectionError(detail);
            }
            if (current(generation)) {
                stop(true);
            }
        }
    });
    connect(client, &KiwiSdrClient::decodedAudioReady, this,
            [this, generation](const QByteArray& pcm) {
        if (!current(generation) || !m_connected || pcm.isEmpty()
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
        publishLegacySliceAudio(0, pcm);
        if (!current(generation)) {
            return;
        }
        QByteArray speaker = pcm;
        const float gain = m_muted ? 0.0f : m_gain / 100.0f;
        for (qsizetype offset = 0; offset < speaker.size(); offset += sizeof(float)) {
            float sample;
            std::memcpy(&sample, speaker.constData() + offset, sizeof(sample));
            sample *= gain;
            std::memcpy(speaker.data() + offset, &sample, sizeof(sample));
        }
        publishLegacyAudio(speaker);
    });
    connect(client, &KiwiSdrClient::waterfallRowReady, this,
            [this, generation](const QString&, const QVector<float>& bins,
                               double low, double high, quint32) {
        if (!current(generation) || !m_connected || bins.isEmpty()
            || bins.size() > 65536 || !std::isfinite(low) || !std::isfinite(high)
            || high <= low || !std::all_of(bins.cbegin(), bins.cend(),
                                          [](float bin) { return std::isfinite(bin); })) {
            return;
        }
        // Bind this frame's geometry before delivery, including late rows
        // from the previous zoom. Never label it with the requested geometry.
        emit panCenterBandwidthChanged(kPanId, (low + high) / 2.0, high - low);
        if (current(generation)) {
            emit spectrumFrameReady(0, QByteArray(reinterpret_cast<const char*>(bins.constData()),
                                                  bins.size() * sizeof(float)));
        }
    });
    connect(client, &KiwiSdrClient::meterReadingReady, this,
            [this, generation](const KiwiSdrProtocol::MeterReading& reading) {
        if (!current(generation) || !m_connected || !reading.valid
            || !reading.hasDbm || !std::isfinite(reading.dbm)) {
            return;
        }
        if (!m_meterDefined) {
            m_meterDefined = true;
            MeterDef meter;
            meter.index = 0;
            meter.source = QStringLiteral("SLC");
            meter.sourceIndex = 0;
            meter.name = QStringLiteral("LEVEL");
            meter.unit = QStringLiteral("dBm");
            meter.low = -160;
            meter.high = 0;
            meter.description = QStringLiteral("Receiver-reported level; hardware calibration unverified");
            emit meterDefined(meter);
        }
        if (current(generation)) {
            emit meterUpdate(QStringLiteral("SLC0:LEVEL"), reading.dbm);
        }
    });
    applyTune();
    if (!current(generation)) {
        return;
    }
    client->setWaterfallView(kPanId, m_panCenterHz / 1e6, m_panBandwidthHz / 1e6);
    if (!current(generation)) {
        return;
    }
    QString endpoint = request.params.value(QStringLiteral("web888.endpoint")).toString();
    if (endpoint.isEmpty()) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(request.host);
        url.setPort(request.port == 0 ? 8073 : request.port);
        endpoint = url.toString();
    }
    client->connectToEndpoint(endpoint, request.params.value(QStringLiteral("web888.password")).toString());
}

void Web888Backend::stop(bool notify)
{
    if (!m_client || m_stopping) {
        return;
    }
    m_stopping = true;
    ++m_generation;
    retirePcmStreams();
    auto* client = m_client;
    m_client = nullptr;
    const bool hadSlice = m_connected;
    m_connected = false;
    m_meterDefined = false;
    QObject::disconnect(client, nullptr, this, nullptr);
    client->disconnectFromEndpoint();
    // A terminal status may be delivered from inside the client's parser.
    // Keep it alive until that stack unwinds; its parent bounds its lifetime.
    client->deleteLater();
    if (notify && hadSlice) {
        emit sliceRemoved(0);
        emit panRemoved(kPanId);
    }
    m_stopping = false;
    if (notify) {
        emit disconnected();
    }
}

void Web888Backend::disconnectRadio() { stop(true); }

void Web888Backend::publishSlice()
{
    SliceDelta delta;
    delta.panId = kPanId;
    delta.letter = QStringLiteral("A");
    delta.frequency = m_frequencyHz / 1e6;
    delta.mode = m_mode;
    delta.modeList = kModes;
    delta.filterLow = m_lowHz;
    delta.filterHigh = m_highHz;
    delta.active = true;
    delta.inUse = true;
    delta.txSlice = false;
    delta.audioGain = m_gain;
    delta.audioMute = m_muted;
    delta.agcMode = m_agcMode;
    emit sliceChanged(0, delta);
}

void Web888Backend::applyTune()
{
    if (m_client) {
        m_client->setTrackedSlice(0, m_frequencyHz / 1e6, m_mode, m_lowHz,
                                 m_highHz, kPanId, {}, 600);
    }
}

void Web888Backend::setSliceFrequency(int id, double hz)
{
    if (id != 0 || !m_connected || !std::isfinite(hz) || hz <= 0
        || hz > std::numeric_limits<int>::max()) {
        return;
    }
    const auto& metadata = m_client->telemetry().metadata;
    if (metadata.hasCoverageBandwidth && hz > metadata.coverageBandwidthMhz * 1e6) {
        return;
    }
    const auto generation = m_generation;
    m_frequencyHz = hz;
    applyTune();
    if (current(generation)) {
        publishSlice();
    }
}

void Web888Backend::setSliceMode(int id, const QString& mode)
{
    if (id != 0 || !m_connected || !kModes.contains(mode)) {
        return;
    }
    const auto generation = m_generation;
    m_mode = mode;
    if (mode == QStringLiteral("LSB")) {
        m_lowHz = -2800;
        m_highHz = -300;
    } else if (mode == QStringLiteral("USB")) {
        m_lowHz = 300;
        m_highHz = 2800;
    } else if (mode == QStringLiteral("CW")) {
        m_lowHz = 300;
        m_highHz = 900;
    } else {
        m_lowHz = -3000;
        m_highHz = 3000;
    }
    applyTune();
    if (current(generation)) {
        publishSlice();
    }
}

void Web888Backend::setSliceFilter(int id, int low, int high)
{
    if (id != 0 || !m_connected || low >= high || low < -6000 || high > 6000) {
        return;
    }
    const auto generation = m_generation;
    m_lowHz = low;
    m_highHz = high;
    applyTune();
    if (current(generation)) {
        publishSlice();
    }
}

void Web888Backend::setSliceAgc(int id, const QString& mode, int thresholdDb)
{
    Q_UNUSED(thresholdDb); // shared UI scale is not a Web-888 threshold in dBm
    if (id != 0 || !m_connected || !capabilities().agcModes.contains(mode)) {
        return;
    }
    auto controls = m_client->receiverControls();
    controls.agcEnabled = mode != QStringLiteral("off");
    controls.agcDecayMs = mode == QStringLiteral("fast") ? KiwiSdrProtocol::kAgcFastDecayMs
        : mode == QStringLiteral("slow") ? KiwiSdrProtocol::kAgcSlowDecayMs
                                         : KiwiSdrProtocol::kAgcMedDecayMs;
    const auto generation = m_generation;
    m_agcMode = mode;
    m_client->setReceiverControls(controls);
    if (current(generation)) {
        publishSlice();
    }
}

ReceiveDispatch Web888Backend::requestSliceAudio(int id, const SliceAudioRequest& request)
{
    if (id != 0 || !m_connected || !request.valid()
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
    if (id == 0 && m_connected) {
        m_muted = mute;
        publishSlice();
    }
}

void Web888Backend::setSliceAudioGain(int id, int gain)
{
    if (id == 0 && m_connected && gain >= 0 && gain <= 100) {
        m_gain = gain;
        publishSlice();
    }
}

void Web888Backend::setPanCenter(const QString& id, double hz, PanCenterIntent intent)
{
    Q_UNUSED(intent);
    if (id == kPanId && m_connected && std::isfinite(hz) && hz > 0) {
        m_panCenterHz = hz;
        m_client->setWaterfallView(kPanId, hz / 1e6, m_panBandwidthHz / 1e6);
    }
}

void Web888Backend::setPanBandwidth(const QString& id, double hz)
{
    if (id == kPanId && m_connected && std::isfinite(hz) && hz > 0) {
        m_panBandwidthHz = hz;
        m_client->setWaterfallView(kPanId, m_panCenterHz / 1e6, hz / 1e6);
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
    Q_UNUSED(ns);
    Q_UNUSED(verb);
    Q_UNUSED(arg);
    if (requestId != 0) {
        emit extensionError(requestId, QStringLiteral("Web-888: unsupported extension"));
    }
}

} // namespace AetherSDR
