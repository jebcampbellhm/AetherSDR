#include "TestSettingsProfile.h"
#include "core/NetworkReceiverCredentials.h"
#include "core/KiwiSdrCredentialStore.h"
#include "core/backends/web888/Web888Backend.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QSignalSpy>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace AetherSDR {
struct NetworkReceiverCredentialsTestAccess {
    static void useStore(std::shared_ptr<IKiwiSdrCredentialStore> store)
    {
        NetworkReceiverCredentials::credentialStore() = std::move(store);
    }
};

class TestCredentialStore final : public IKiwiSdrCredentialStore {
public:
    bool isPersistent() const override { return true; }
    void read(const QString&, QObject*, Callback callback) override { pendingRead = std::move(callback); }
    void write(const QString&, const QString& value, QObject*, Callback callback) override
    {
        ++writes;
        lastWritten = value;
        callback({ });
    }
    void remove(const QString&, QObject*, Callback callback) override
    {
        ++removals;
        callback({ });
    }
    Callback pendingRead;
    int writes = 0;
    int removals = 0;
    QString lastWritten;
};

// Inject decoded client observations, not a fake server. Live authentication
// and protocol compatibility are qualified separately against a real Web-888.
struct Web888BackendTestAccess {
    static void start(Web888Backend& backend)
    {
        backend.m_connected = true;
        backend.m_initialRange = true;
        backend.m_bandwidth = 4000;
        emit backend.connected();
    }
    static void row(Web888Backend& backend, const QVector<float>& bins, double low, double high)
    {
        emit backend.m_client->waterfallRowReady(QStringLiteral("0xe1000000"), bins, low, high, 0);
    }
    static void audio(Web888Backend& backend, const QByteArray& bytes)
    {
        emit backend.m_client->decodedAudioReady(bytes);
    }
    static void version(Web888Backend& backend, const QString& version)
    {
        KiwiSdrReceiverTelemetry telemetry;
        telemetry.protocol.serverVersion = version;
        emit backend.m_client->telemetryChanged(telemetry);
    }
};
}

using namespace AetherSDR;

int main(int argc, char** argv)
{
    TestSettingsProfile profile("web888_backend_test");
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](bool ok, const char* message) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", message);
        if (!ok)
            ++failures;
    };
    // No unit test reads or mutates the operator's operating-system keychain.
    auto credentials = std::make_shared<TestCredentialStore>();
    NetworkReceiverCredentialsTestAccess::useStore(credentials);
    const QString credentialEndpoint = QStringLiteral("http://192.0.2.21:18073");
    QString loadedPassword = QStringLiteral("not-delivered");
    NetworkReceiverCredentials::load(credentialEndpoint, &app,
        [&loadedPassword](const QString& password, const QString&) { loadedPassword = password; });
    bool forgotten = false;
    NetworkReceiverCredentials::forget(credentialEndpoint, &app,
        [&forgotten](const QString& error) { forgotten = error.isEmpty(); });
    credentials->pendingRead({ KiwiSdrCredentialResultCode::Success, QStringLiteral("obsolete"), { } });
    NetworkReceiverCredentials::saveAccepted(credentialEndpoint, QStringLiteral("obsolete"), &app,
        [](const QString&) { });
    check(forgotten && loadedPassword.isEmpty() && credentials->removals == 1 && credentials->writes == 0,
        "forget removes the endpoint credential and defeats an older read or authentication reply");
    NetworkReceiverCredentials::setSessionPassword(credentialEndpoint, QStringLiteral("replacement"));
    NetworkReceiverCredentials::saveAccepted(credentialEndpoint, QStringLiteral("replacement"), &app,
        [](const QString&) { });
    check(credentials->writes == 1 && credentials->lastWritten == QStringLiteral("replacement"),
        "a newly accepted replacement remains eligible for secure storage after forget");
    NetworkReceiverCredentials::load(credentialEndpoint, &app,
        [&loadedPassword](const QString& password, const QString&) { loadedPassword = password; });
    NetworkReceiverCredentials::forget(credentialEndpoint, &app, [](const QString&) { });
    QCoreApplication::processEvents();
    check(loadedPassword.isEmpty(), "forget also defeats an already queued session-cache read");
    Web888Backend backend;
    check(NetworkReceiverCredentials::httpEndpoint(QStringLiteral("127.0.0.1"), 8073)
                == QStringLiteral("http://127.0.0.1:8073")
            && NetworkReceiverCredentials::httpEndpoint(QStringLiteral("2001:db8::21"), 18073)
                == QStringLiteral("http://[2001:db8::21]:18073"),
        "IPv4 and IPv6 connections share valid canonical credential endpoints");
    const auto caps = backend.capabilities();
    check(caps.receiveAudioBuffering && caps.receiveAudioBuffering->jitterTargetMs == 360,
        "burst-delivered Web-888 audio declares a host playout reserve");
    check(caps.tuningMinHz == 1 && caps.sliceFrequencyControl.minimumHz == 1
            && caps.receivePanCenterControl->minimumHz == 1,
        "declared positive lower bounds match the protocol's tuning and pan-center contract");
    check(caps.family == QStringLiteral("web888") && caps.model == QStringLiteral("Web-888"),
        "standalone identity is Web-888");
    check(!caps.canTransmit && !caps.hostModulates && !caps.takesTxAudioOverSeam,
        "receive-only capabilities grant no microphone or transmit path");
    QSignalSpy spectrum(&backend, &IRadioBackend::spectrumFrameReady);
    QSignalSpy audio(&backend, &IRadioBackend::audioFrameReady);
    QSignalSpy decoder(&backend, &IRadioBackend::sliceAudioFrameReady);
    QSignalSpy slices(&backend, &IRadioBackend::sliceChanged);
    QSignalSpy pans(&backend, &IRadioBackend::panCenterBandwidthChanged);
    QSignalSpy disconnected(&backend, &IRadioBackend::disconnected);
    QSignalSpy identity(&backend, &IRadioBackend::radioChanged);
    Web888BackendTestAccess::start(backend);
    Web888BackendTestAccess::version(backend, QStringLiteral("2026.919"));
    Web888BackendTestAccess::version(backend, QStringLiteral("2026.919"));
    Web888BackendTestAccess::version(backend, QStringLiteral("<b>untrusted</b>"));
    check(identity.size() == 1
            && identity.first().at(0).value<RadioDelta>().version == QStringLiteral("2026.919"),
        "reported firmware crosses the neutral identity seam once, without a fabricated version");
    QVector<float> bins(1000);
    for (int i = 0; i < bins.size(); ++i)
        bins[i] = float(i);
    Web888BackendTestAccess::row(backend, bins, 14.0, 14.4);
    check(spectrum.size() == 1, "decoded receiver row reaches the normalized spectrum seam once");
    if (!spectrum.isEmpty()) {
        const QByteArray bytes = spectrum.first().at(1).toByteArray();
        QVector<float> view(bytes.size() / sizeof(float));
        std::memcpy(view.data(), bytes.constData(), bytes.size());
        check(view.size() == 1000 && std::abs(view.front() - 494.505f) < 0.01f
                && std::abs(view.back() - 504.495f) < 0.01f,
            "narrow viewport crops wider receiver RF coverage instead of stretching it");
    }
    backend.setSliceFrequency(0, std::numeric_limits<double>::quiet_NaN());
    backend.setSliceFrequency(0, 0);
    backend.setSliceFrequency(0, 31e6);
    backend.setSliceMode(0, QStringLiteral("INVALID"));
    backend.setSliceFilter(0, 3000, 2000);
    check(slices.isEmpty(), "invalid tuning, mode, and passband never publish an accepted state");
    backend.setPanCenter(QStringLiteral("0xe1000000"), 0, IRadioBackend::PanCenterIntent::Drag);
    check(pans.isEmpty(), "a zero-Hz pan center is not acknowledged when the protocol cannot apply it");
    backend.setPanCenter(
        QStringLiteral("0xe1000000"), 14.21e6, IRadioBackend::PanCenterIntent::Drag);
    check(pans.size() == 1 && slices.isEmpty(), "moving the pan never retunes the receiver");
    backend.setSliceFrequency(0, 14.205e6);
    check(slices.size() == 1, "a valid receive tune publishes the slice");
    QVector<float> samples(1024, 1.0f);
    QByteArray pcm(
        reinterpret_cast<const char*>(samples.constData()), samples.size() * sizeof(float));
    Web888BackendTestAccess::audio(backend, pcm);
    check(audio.size() == 1, "one decoded audio block produces one speaker block");
    backend.setSliceAudioMute(0, true);
    Web888BackendTestAccess::audio(backend, pcm);
    const auto muted = audio.last().at(0).value<PcmFrame>().legacyStereo24();
    check(muted == QByteArray(pcm.size(), '\0')
            && decoder.last().at(1).value<PcmFrame>().legacyStereo24() == pcm,
        "mute silences speakers while decoder audio remains unchanged");
    backend.setSliceAudioMute(0, false);
    backend.setSliceAudioGain(0, 100);
    backend.setSliceAudioPan(0, 0);
    Web888BackendTestAccess::audio(backend, pcm);
    const auto panned = audio.last().at(0).value<PcmFrame>().legacyStereo24();
    float first[2];
    std::memcpy(first, panned.constData(), sizeof(first));
    check(first[0] == 1 && first[1] == 0,
        "monitor pan changes speaker channels without touching decoder PCM");
    backend.disconnectRadio();
    backend.disconnectRadio();
    Web888BackendTestAccess::row(backend, bins, 14.0, 14.4);
    Web888BackendTestAccess::audio(backend, pcm);
    check(disconnected.size() == 1 && spectrum.size() == 1 && audio.size() == 3,
        "teardown is idempotent and rejects late audio and spectrum");
    NetworkReceiverCredentials::setSessionPassword(QStringLiteral("http://127.0.0.1:8073"), { });
    RadioConnectRequest request;
    request.host = QStringLiteral("127.0.0.1");
    request.port = 8073;
    backend.connectRadio(request);
    backend.disconnectRadio();
    QCoreApplication::processEvents();
    check(!backend.isConnected() && disconnected.size() == 2,
        "disconnect fences a pending credential completion before any socket opens");
    {
        auto closing = std::make_unique<Web888Backend>();
        QPointer<Web888Backend> alive(closing.get());
        Web888BackendTestAccess::start(*closing);
        int lateDecoderBlocks = 0;
        QObject::connect(closing.get(), &IRadioBackend::sliceAudioFrameReady, &app,
            [&lateDecoderBlocks](int, const PcmFrame&) { ++lateDecoderBlocks; });
        QObject::connect(closing.get(), &IRadioBackend::audioFrameReady, &app,
            [&closing](const PcmFrame&) { closing.reset(); });
        Web888BackendTestAccess::audio(*closing, pcm);
        check(!alive && lateDecoderBlocks == 0,
            "retiring a session inside speaker delivery publishes no trailing decoder block");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return failures ? 1 : 0;
}
