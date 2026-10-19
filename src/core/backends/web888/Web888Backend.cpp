#include "Web888Backend.h"
#include "core/NetworkReceiverCredentials.h"

#include <QCoreApplication>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace AetherSDR {
namespace {
    const QString panId = QStringLiteral("0xe1000000");
    // The protocol client requires positive tuning and waterfall centers.
    constexpr double minHz = 1;
    constexpr double maxHz = 30'720'000;
}

Web888Backend::Web888Backend(QObject* parent)
    : IRadioBackend(parent)
    , m_client(new KiwiSdrClient(this))
{
    m_client->setReceiverFamily(KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
    m_connectTimeout.setSingleShot(true);
    m_connectTimeout.setInterval(30000);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        if (m_connecting)
            fail(tr(
                "Web-888 did not finish connecting. Check its address, port, and user password."));
    });
    connect(m_client, &KiwiSdrClient::stateChanged, this,
        [this](KiwiSdrClient::State state, const QString& detail) {
            if (!m_connecting && !m_connected)
                return;
            if (state == KiwiSdrClient::State::Connected && !m_connected) {
                m_connected = true;
                m_connecting = false;
                m_connectTimeout.stop();
                m_client->setAudioActive(true);
                const auto generation = m_generation;
                const QPointer<Web888Backend> self(this);
                emit connected();
                if (!self || !m_connected || generation != m_generation)
                    return;
                publishVersion(m_client->telemetry().protocol.serverVersion);
                if (!self || !m_connected || generation != m_generation)
                    return;
                publishPan();
                if (!self || !m_connected || generation != m_generation)
                    return;
                emit panBandwidthLimitsChanged(panId, 0.001, maxHz / 1e6);
                if (!self || !m_connected || generation != m_generation)
                    return;
                MeterDef meter;
                meter.index = 700;
                meter.source = QStringLiteral("SLC");
                meter.sourceIndex = 0;
                meter.name = QStringLiteral("LEVEL");
                meter.unit = QStringLiteral("dBm");
                meter.low = -160;
                meter.high = 0;
                meter.description = tr("Signal level reported by the Web-888 receiver.");
                emit meterDefined(meter);
                if (!self || !m_connected || generation != m_generation)
                    return;
                publishSlice();
                if (!self || !m_connected || generation != m_generation)
                    return;
                track();
                if (!self || !m_connected || generation != m_generation)
                    return;
                const QString acceptedPassword = std::exchange(m_connectPassword, QString { });
                NetworkReceiverCredentials::saveAccepted(
                    m_endpoint, acceptedPassword, this, [this, generation](const QString& error) {
                        if (generation == m_generation && m_connected && !error.isEmpty())
                            emit configurationWarning(
                                tr("Web-888 password could not be saved: %1").arg(error));
                    });
            } else if (state == KiwiSdrClient::State::Error || state == KiwiSdrClient::State::Busy
                || state == KiwiSdrClient::State::Disconnected
                || state == KiwiSdrClient::State::Waiting || state == KiwiSdrClient::State::Camping
                || state == KiwiSdrClient::State::CampDisconnected) {
                fail(detail.isEmpty() ? tr("Web-888 disconnected.") : detail);
            }
        });
    connect(m_client, &KiwiSdrClient::decodedAudioReady, this, [this](const QByteArray& pcm) {
        if (!m_connected)
            return;
        const auto generation = m_generation;
        const QPointer<Web888Backend> self(this);
        QByteArray speaker = pcm;
        const float level = m_muted ? 0 : m_gain / 100.0f;
        const float left = level * std::min(1.0f, 2 * (1 - m_pan / 100.0f));
        const float right = level * std::min(1.0f, 2 * m_pan / 100.0f);
        constexpr qsizetype stereoBytes = 2 * sizeof(float);
        for (qsizetype i = 0; i + stereoBytes <= speaker.size(); i += stereoBytes) {
            float values[2];
            std::memcpy(values, speaker.constData() + i, sizeof(values));
            values[0] *= left;
            values[1] *= right;
            std::memcpy(speaker.data() + i, values, sizeof(values));
        }
        publishLegacyAudio(speaker);
        if (!self || !m_connected || generation != m_generation)
            return;
        publishLegacySliceAudio(0, pcm);
    });
    connect(m_client, &KiwiSdrClient::meterReadingReady, this,
        [this](const KiwiSdrProtocol::MeterReading& reading) {
            if (m_connected && reading.valid && reading.hasDbm && std::isfinite(reading.dbm))
                emit meterUpdate(QStringLiteral("SLC0:LEVEL"), reading.dbm);
        });
    connect(m_client, &KiwiSdrClient::telemetryChanged, this,
        [this](const KiwiSdrReceiverTelemetry& telemetry) {
            publishVersion(telemetry.protocol.serverVersion);
        });
    connect(m_client, &KiwiSdrClient::waterfallRowReady, this,
        [this](const QString&, const QVector<float>& bins, double low, double high, quint32) {
            if (!m_connected || bins.isEmpty() || !(high > low))
                return;
            if (!m_initialRange) {
                QVector<float> levels;
                for (float bin : bins)
                    if (std::isfinite(bin))
                        levels.push_back(bin);
                if (levels.size() > 32) {
                    std::sort(levels.begin(), levels.end());
                    const double floor = levels[levels.size() / 5];
                    m_initialRange = true;
                    const auto generation = m_generation;
                    const QPointer<Web888Backend> self(this);
                    emit panRangeChanged(panId, floor - 25, floor + 85);
                    if (!self || !m_connected || generation != m_generation)
                        return;
                }
            }
            // The receiver may deliberately supply wider coverage at narrow zoom.
            // Resample only the viewport; never stretch the entire receiver row.
            QVector<float> view(bins.size());
            const double viewLow = (m_center - m_bandwidth / 2) / 1e6;
            for (qsizetype i = 0; i < view.size(); ++i) {
                const double frequency = viewLow + (i + 0.5) * m_bandwidth / 1e6 / view.size();
                const double position = (frequency - low) / (high - low) * bins.size() - 0.5;
                if (position < 0 || position > bins.size() - 1) {
                    view[i] = -200;
                    continue;
                }
                const auto left = static_cast<qsizetype>(position);
                const auto right = std::min(left + 1, bins.size() - 1);
                view[i] = bins[left] + (bins[right] - bins[left]) * (position - left);
            }
            QByteArray bytes(
                reinterpret_cast<const char*>(view.constData()), view.size() * sizeof(float));
            emit spectrumFrameReady(0, bytes);
        });
    connect(m_client, &KiwiSdrClient::waterfallAvailabilityChanged, this,
        [this](bool available, const QString& detail) {
            if (m_connected && !available && !detail.isEmpty())
                emit configurationWarning(detail);
        });
}

Web888Backend::~Web888Backend()
{
    disconnectRadio();
    // A seam consumer can retire the backend synchronously from a client
    // callback. Keep the stopped client alive until that callback unwinds.
    m_client->disconnect(this);
    auto* app = QCoreApplication::instance();
    m_client->setParent(app && app->thread() == m_client->thread() ? app : nullptr);
    m_client->deleteLater();
}

RadioCapabilities Web888Backend::capabilities() const
{
    RadioCapabilities c;
    c.family = QStringLiteral("web888");
    c.model = QStringLiteral("Web-888");
    c.canTransmit = false;
    c.hostModulates = false;
    c.hasFmRepeaterOffset = false;
    c.hasCwTune = false;
    c.maxSlices = 1;
    c.maxPanadapters = 1;
    c.sampleRatesHz = { 24000 };
    c.tuningMinHz = minHz;
    c.tuningMaxHz = maxHz;
    const auto authority = SliceFrequencyControl::Authority::Engine;
    c.sliceFrequencyControl = { authority, qint64(minHz), qint64(maxHz) };
    c.receiveModeControl = ReceiveModeControl { authority,
        { QStringLiteral("AM"), QStringLiteral("SAM"), QStringLiteral("USB"), QStringLiteral("LSB"),
            QStringLiteral("CW"), QStringLiteral("FM") } };
    c.receivePanCenterControl = ReceivePanRangeControl { authority, qint64(minHz), qint64(maxHz) };
    c.receivePanBandwidthControl = ReceivePanRangeControl { authority, 1000, qint64(maxHz) };
    c.receiveAudioExport = ReceiveAudioExport { { 24000 }, 1 };
    c.receiveAudioControl = ReceiveAudioControl { authority };
    c.receiveAudioBuffering = ReceiveAudioBuffering { 360 };
    c.panSpanModel = PanSpanModel { false, false };
    c.panAmplitude = PanAmplitudeModel { };
    c.panAmplitude->binsAbsolute = true;
    c.panAmplitude->clientPersistsDbmRange = false;
    c.radioOwnsDbmScale = false;
    c.agcModes = { QStringLiteral("off"), QStringLiteral("slow"), QStringLiteral("med"),
        QStringLiteral("fast") };
    return c;
}

void Web888Backend::connectRadio(const RadioConnectRequest& request)
{
    disconnectRadio();
    if (request.host.isEmpty() || request.port == 0) {
        emit connectionError(tr("Enter a Web-888 address and port."));
        return;
    }
    m_endpoint = NetworkReceiverCredentials::httpEndpoint(request.host, request.port);
    m_connecting = true;
    m_reportedVersion.clear();
    m_initialRange = false;
    m_connectTimeout.start();
    const auto generation = ++m_generation;
    NetworkReceiverCredentials::load(
        m_endpoint, this, [this, generation](const QString& password, const QString& error) {
            if (generation != m_generation || !m_connecting)
                return;
            if (!error.isEmpty()) {
                fail(tr("Cannot load the Web-888 password: %1").arg(error));
                return;
            }
            m_connectPassword = password;
            const QPointer<Web888Backend> self(this);
            track();
            if (!self || generation != m_generation || !m_connecting)
                return;
            m_client->connectToEndpoint(m_endpoint, password);
        });
}

void Web888Backend::disconnectRadio()
{
    const bool active = m_connected || m_connecting;
    m_connected = false;
    m_connecting = false;
    m_connectPassword.clear();
    ++m_generation;
    m_connectTimeout.stop();
    m_client->disconnectFromEndpoint();
    if (active)
        emit disconnected();
}

void Web888Backend::fail(const QString& detail)
{
    const auto generation = m_generation;
    const QPointer<Web888Backend> self(this);
    QString message = detail;
    message.replace(QStringLiteral("KiwiSDR"), QStringLiteral("Web-888"));
    emit connectionError(message);
    if (self && generation == m_generation)
        disconnectRadio();
}

void Web888Backend::track()
{
    const QPointer<Web888Backend> self(this);
    const auto generation = m_generation;
    m_client->setTrackedSlice(0, m_frequency / 1e6, m_mode, m_low, m_high, panId, { }, 600);
    if (!self || generation != m_generation)
        return;
    m_client->setWaterfallView(panId, m_center / 1e6, m_bandwidth / 1e6);
}

void Web888Backend::publishSlice()
{
    if (!m_connected)
        return;
    SliceDelta d;
    d.panId = panId;
    d.letter = QStringLiteral("A");
    d.frequency = m_frequency / 1e6;
    d.mode = m_mode;
    d.filterLow = m_low;
    d.filterHigh = m_high;
    d.modeList = capabilities().receiveModeControl->modes;
    d.active = true;
    d.inUse = true;
    d.txSlice = false;
    d.rxAntenna = QStringLiteral("RX");
    d.rxAntennaList = QStringList { QStringLiteral("RX") };
    d.txAntenna = QString();
    d.txAntennaList = QStringList { };
    d.audioGain = m_gain;
    d.audioMute = m_muted;
    d.audioPan = m_pan;
    emit sliceChanged(0, d);
}

void Web888Backend::publishPan()
{
    if (m_connected)
        emit panCenterBandwidthChanged(panId, m_center / 1e6, m_bandwidth / 1e6);
}

void Web888Backend::publishVersion(const QString& version)
{
    if (!m_connected || version.isEmpty() || version.size() > 128 || version == m_reportedVersion)
        return;
    for (QChar ch : version) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-')
            && ch != QLatin1Char('_') && ch != QLatin1Char('+'))
            return;
    }
    m_reportedVersion = version;
    RadioDelta delta;
    delta.version = version;
    emit radioChanged(delta);
}
void Web888Backend::setSliceFrequency(int id, double hz)
{
    if (!m_connected || id != 0 || !std::isfinite(hz) || hz < minHz || hz > maxHz)
        return;
    m_frequency = hz;
    const QPointer<Web888Backend> self(this);
    track();
    if (self)
        publishSlice();
}
void Web888Backend::setSliceMode(int id, const QString& mode)
{
    if (!m_connected || id != 0 || !capabilities().receiveModeControl->modes.contains(mode))
        return;
    m_mode = mode;
    const QPointer<Web888Backend> self(this);
    track();
    if (self)
        publishSlice();
}
void Web888Backend::setSliceFilter(int id, int low, int high)
{
    if (!m_connected || id != 0 || low < -12000 || high > 12000 || low >= high)
        return;
    m_low = low;
    m_high = high;
    const QPointer<Web888Backend> self(this);
    track();
    if (self)
        publishSlice();
}
void Web888Backend::setSliceAgc(int id, const QString& mode, int)
{
    if (!m_connected || id != 0 || !capabilities().agcModes.contains(mode))
        return;
    KiwiSdrReceiverControls controls;
    controls.agcEnabled = mode != QStringLiteral("off");
    controls.agcThresholdDb = -100;
    controls.agcDecayMs = mode == QStringLiteral("fast") ? 300
        : mode == QStringLiteral("slow")                 ? 3000
                                                         : 1000;
    m_client->setReceiverControls(controls);
}
ReceiveDispatch Web888Backend::requestSliceAudio(int id, const SliceAudioRequest& request)
{
    if (!m_connected || id != 0 || !request.valid())
        return ReceiveDispatch::Unsupported;
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
void Web888Backend::setSliceAudioGain(int id, int gain)
{
    if (!m_connected || id != 0 || gain < 0 || gain > 100)
        return;
    m_gain = gain;
    publishSlice();
}
void Web888Backend::setSliceAudioMute(int id, bool mute)
{
    if (!m_connected || id != 0)
        return;
    m_muted = mute;
    publishSlice();
}
void Web888Backend::setSliceAudioPan(int id, int pan)
{
    if (!m_connected || id != 0 || pan < 0 || pan > 100)
        return;
    m_pan = pan;
    publishSlice();
}
void Web888Backend::setPanCenter(const QString& id, double hz, PanCenterIntent)
{
    if (!m_connected || id != panId || !std::isfinite(hz) || hz < minHz || hz > maxHz)
        return;
    m_center = hz;
    const QPointer<Web888Backend> self(this);
    track();
    if (self)
        publishPan();
}
void Web888Backend::setPanBandwidth(const QString& id, double hz)
{
    if (!m_connected || id != panId || !std::isfinite(hz))
        return;
    m_bandwidth = std::clamp(hz, 1000.0, maxHz);
    const QPointer<Web888Backend> self(this);
    track();
    if (self)
        publishPan();
}
void Web888Backend::setPanFrameRate(const QString& id, int fps)
{
    if (id == panId)
        m_client->setWaterfallRateOverride(fps >= 16 ? 4 : fps >= 8 ? 3 : fps >= 4 ? 2 : 1);
}
void Web888Backend::setKeying(
    bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&)
{
}
void Web888Backend::invokeExtension(const QString&, const QString&, quint64 id, const QVariant&)
{
    emit extensionError(id, tr("Unsupported Web-888 operation."));
}

} // namespace AetherSDR
