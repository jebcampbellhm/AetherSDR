// Socket-free seam/lifecycle proof. Inject acquisition events through the real
// worker relays; protocol and DSP behavior have separate capture/signal tests.
#include "SeamThreadAffinityProbe.h"
#include "TestSettingsProfile.h"
#include "core/backends/nrsp/NrspBackend.h"
#include "core/backends/nrsp/NrspWorker.h"
#include "models/RadioModel.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace AetherSDR {
class NrspBackendTestAccess
{
public:
    struct Acquisition {
        nrsp::NrspWorker* worker;
        quint64 generation;
        std::shared_ptr<std::atomic<qsizetype>> credit;
    };
    static Acquisition prepare(NrspBackend& backend)
    {
        backend.disconnectRadio();
        backend.m_connecting = true;
        const quint64 generation = ++backend.m_generation;
        backend.m_thread = new QThread(&backend);
        const auto credit = std::make_shared<std::atomic<qsizetype>>(0);
        backend.m_worker = new nrsp::NrspWorker(credit);
        backend.m_worker->moveToThread(backend.m_thread);
        backend.attachWorker(credit);
        backend.m_thread->start(); // No socket start, timer, firmware peer or DSP.
        return {backend.m_worker, generation, credit};
    }
};

class RadioModelSliceLifecycleTestAccess
{
public:
    static void prepareRetry(RadioModel& radio)
    {
        radio.m_lastInfo.address = QHostAddress(QStringLiteral("192.0.2.1"));
        radio.m_intentionalDisconnect = false;
        radio.m_reconnectTimer.stop();
    }
    static void error(RadioModel& radio) { radio.onConnectionError(QStringLiteral("injected session closure")); }
    static void disconnected(RadioModel& radio) { radio.onDisconnected(); }
    static bool retryScheduled(const RadioModel& radio) { return radio.m_reconnectTimer.isActive(); }
    static void staleRetry(RadioModel& radio)
    {
        radio.m_reconnectTimer.start(60000);
        QMetaObject::invokeMethod(&radio.m_reconnectTimer, "timeout", Qt::DirectConnection);
    }
};
} // namespace AetherSDR

using namespace AetherSDR;
namespace {
int failures = 0;
void check(bool value, const char* message)
{
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", message);
    failures += !value;
}
void drain()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}
QVariantMap source(bool owner = true)
{
    return {{"frequencyHz", 14100000}, {"sampleRate", 2000000}, {"antenna", 0},
            {"owner", owner}, {"rfAgc", true}, {"rfNotch", false}, {"dabNotch", false}};
}
void ready(const NrspBackendTestAccess::Acquisition& acquisition)
{
    QMetaObject::invokeMethod(acquisition.worker, [acquisition] {
        emit acquisition.worker->ready(acquisition.generation, source());
    }, Qt::BlockingQueuedConnection);
    drain();
}
void enqueueAudio(const NrspBackendTestAccess::Acquisition& acquisition, quint64 token = 0)
{
    QMetaObject::invokeMethod(acquisition.worker, [acquisition, token] {
        const float samples[]{0.4f, -0.2f, 0.3f, -0.1f};
        const QByteArray bytes(reinterpret_cast<const char*>(samples), sizeof(samples));
        acquisition.credit->fetch_add(bytes.size());
        emit acquisition.worker->audioReady(token ? token : acquisition.generation, bytes);
    }, Qt::BlockingQueuedConnection);
}
float firstSample(const QByteArray& pcm)
{
    float value = 0;
    if (pcm.size() >= qsizetype(sizeof(value))) { std::memcpy(&value, pcm.constData(), sizeof(value)); }
    return value;
}
void relayAndRetirement()
{
    NrspBackend backend;
    test::SeamThreadAffinityProbe probe(&backend);
    test::attachAllSeamSignals(probe);
    QByteArray speaker;
    QByteArray slice;
    QObject::connect(&backend, &IRadioBackend::audioFrameReady, &backend,
                     [&](const PcmFrame& frame) { speaker = frame.legacyStereo24(); });
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &backend,
                     [&](int, const PcmFrame& frame) { slice = frame.legacyStereo24(); });
    auto acquisition = NrspBackendTestAccess::prepare(backend);
    ready(acquisition);
    check(backend.isConnected() && probe.count("connected") == 1, "worker readiness reaches owner-thread seam");
    check(backend.requestReceiveTune(0, 14100000, IRadioBackend::ReceiveTuneView::Center)
          && !backend.requestReceiveTune(1, 14100000, IRadioBackend::ReceiveTuneView::Center),
          "confirmed desktop tuning reaches the implemented receiver verb");
    enqueueAudio(acquisition);
    drain();
    check(std::abs(firstSample(slice) - 0.4f) < 1e-6f
          && std::abs(firstSample(speaker) - 0.2f) < 1e-6f,
          "independent slice audio precedes local monitor gain");
    backend.setSliceAudioMute(0, true);
    enqueueAudio(acquisition);
    drain();
    check(std::abs(firstSample(slice) - 0.4f) < 1e-6f && firstSample(speaker) == 0,
          "monitor mute leaves receiver export intact");
    QMetaObject::invokeMethod(acquisition.worker, [acquisition] {
        emit acquisition.worker->sourceChanged(acquisition.generation, source(false));
        const QByteArray spectrum(16, '\0');
        acquisition.credit->fetch_add(spectrum.size());
        emit acquisition.worker->spectrumReady(acquisition.generation, spectrum);
    }, Qt::BlockingQueuedConnection);
    drain();
    check(backend.requestSliceRxAntenna(0, "B") == ReceiveDispatch::Unsupported
          && backend.capabilities().sliceFrequencyControl.minimumHz > 10000000,
          "ownership loss withdraws shared controls and bounds tuning to capture");
    check(probe.count("spectrumFrameReady") == 1 && acquisition.credit->load() == 0,
          "display delivery returns queue credit");
    const int audioCount = probe.count("audioFrameReady");
    enqueueAudio(acquisition); // Already queued to owner, then retire before dispatch.
    backend.disconnectRadio();
    drain();
    check(probe.count("audioFrameReady") == audioCount && probe.count("disconnected") == 1,
          "queued audio from retired worker is not published");
    check(probe.afterDisconnect().isEmpty() && probe.violations().isEmpty(),
          "every exercised seam emission obeys thread affinity and retirement");
    const quint64 retired = acquisition.generation;
    probe.resetDisconnectGate();
    acquisition = NrspBackendTestAccess::prepare(backend);
    ready(acquisition);
    enqueueAudio(acquisition, retired);
    drain();
    check(probe.count("audioFrameReady") == audioCount, "old generation cannot enter a new session");
    enqueueAudio(acquisition);
    drain();
    check(probe.count("audioFrameReady") == audioCount + 1, "new generation resumes exactly one audio route");
    backend.disconnectRadio();
    drain();
    check(probe.afterDisconnect().isEmpty() && probe.violations().isEmpty(), "second session remains conformant");
}

void reentrantRetirement()
{
    NrspBackend backend;
    test::SeamThreadAffinityProbe probe(&backend);
    test::attachAllSeamSignals(probe);
    QObject::connect(&backend, &IRadioBackend::connected, &backend, [&] { backend.disconnectRadio(); });
    ready(NrspBackendTestAccess::prepare(backend));
    check(!backend.isConnected() && probe.count("connected") == 1
          && probe.count("disconnected") == 1 && probe.afterDisconnect().isEmpty(),
          "disconnect inside connected callback aborts subsequent identity publication");

    auto doomed = std::make_unique<NrspBackend>();
    const QPointer<NrspBackend> guard(doomed.get());
    QObject::connect(doomed.get(), &IRadioBackend::connected, QCoreApplication::instance(), [&] { doomed.reset(); });
    ready(NrspBackendTestAccess::prepare(*doomed));
    check(guard.isNull(), "backend can be destroyed by its connected observer");
}

void antennaRefusal()
{
    NrspBackend backend;
    auto acquisition = NrspBackendTestAccess::prepare(backend);
    ready(acquisition);
    QVariantMap uhf = source();
    uhf["frequencyHz"] = 910550000;
    QMetaObject::invokeMethod(acquisition.worker, [acquisition, uhf] {
        emit acquisition.worker->sourceChanged(acquisition.generation, uhf);
    }, Qt::BlockingQueuedConnection);
    drain();
    QString warning;
    QString observedAntenna;
    QObject::connect(&backend, &IRadioBackend::configurationWarning, &backend,
                     [&](const QString& reason) { warning = reason; });
    QObject::connect(&backend, &IRadioBackend::sliceChanged, &backend,
                     [&](int, const SliceDelta& delta) {
                         if (delta.rxAntenna) { observedAntenna = *delta.rxAntenna; }
                     });
    check(backend.requestSliceRxAntenna(0, "C") == ReceiveDispatch::Unsupported
          && observedAntenna == "A" && warning.isEmpty(),
          "UHF antenna C refusal preserves observed antenna and defers its specific explanation");
    drain();
    check(warning.contains("200 MHz"), "antenna C explanation survives the synchronous generic dispatch notice");
    warning.clear();
    backend.requestSliceRxAntenna(0, "C");
    backend.disconnectRadio();
    drain();
    check(warning.isEmpty(), "queued antenna warning is retired with its connection");
}

void explicitReconnectPolicy()
{
    RadioModel radio;
    auto backend = std::make_unique<NrspBackend>();
    check(backend->reconnectPolicy() == IRadioBackend::ReconnectPolicy::OperatorInitiated,
          "nRSP disconnect requires operator intent");
    radio.setBackendForTest(std::move(backend), QStringLiteral("nrsp"));
    RadioModelSliceLifecycleTestAccess::prepareRetry(radio);
    RadioModelSliceLifecycleTestAccess::error(radio);
    check(!RadioModelSliceLifecycleTestAccess::retryScheduled(radio), "connection error does not schedule re-entry");
    RadioModelSliceLifecycleTestAccess::disconnected(radio);
    check(!RadioModelSliceLifecycleTestAccess::retryScheduled(radio), "remote closure does not schedule re-entry");
    RadioModelSliceLifecycleTestAccess::staleRetry(radio);
    check(!RadioModelSliceLifecycleTestAccess::retryScheduled(radio), "stale retry from an earlier family is canceled");
}

void nonOwnerModelState()
{
    RadioModel model;
    auto owned = std::make_unique<NrspBackend>();
    NrspBackend* backend = owned.get();
    model.setBackendForTest(std::move(owned), QStringLiteral("nrsp"));
    auto acquisition = NrspBackendTestAccess::prepare(*backend);
    ready(acquisition);
    QMetaObject::invokeMethod(acquisition.worker, [acquisition] {
        emit acquisition.worker->sourceChanged(acquisition.generation, source(false));
    }, Qt::BlockingQueuedConnection);
    drain();
    SliceModel* slice = model.slice(0);
    check(slice && slice->rxAntenna() == "A", "non-owner session retains an observed receive slice");
    if (!slice) { return; }
    PanadapterModel* pan = model.panadapter(slice->panId());
    check(pan && model.requestPanDisplay(pan,
              {PanDisplayField::Geometry, PanGeometryRequest{14.2, 0.5}}) == ReceiveDispatch::Dispatched,
          "listener pan and zoom requests cross the typed desktop display route");
    check(pan && model.requestPanDimensions(pan, 1200, 600, 1024) == ReceiveDispatch::Dispatched,
          "desktop display width reaches the implemented spectrum-points control");
    check(pan && model.requestPanDisplay(pan, {PanDisplayField::FrameRate, 20}) == ReceiveDispatch::Unsupported
          && model.requestPanDisplay(pan,
              {PanDisplayField::Geometry, PanGeometryRequest{14.2, -0.5}}) == ReceiveDispatch::Unsupported
          && pan->centerMhz() == 14.1 && pan->bandwidthMhz() == 1.6,
          "unsupported or invalid display requests preserve confirmed pan geometry");
    slice->setRxAntenna(QStringLiteral("C"));
    check(slice->rxAntenna() == "A", "model-level refusal cannot display an antenna that was never selected");
    check(backend->requestReceiveTune(0, 14200000, IRadioBackend::ReceiveTuneView::Preserve)
          && !backend->requestReceiveTune(0, 430500000, IRadioBackend::ReceiveTuneView::Preserve),
          "non-owner admits in-window slice tuning and refuses shared source movement");
    QString tuneNotice;
    const QMetaObject::Connection noticeConnection = QObject::connect(
        &model, &RadioModel::configurationWarning, &model,
        [&](const QString& text) { tuneNotice = text; });
    check(!model.requestConfirmedReceiveTune(0, 430.5, IRadioBackend::ReceiveTuneView::Preserve)
          && tuneNotice.contains("13.124") && tuneNotice.contains("15.076")
          && slice->frequency() == 14.1,
          "out-of-capture desktop tuning explains the current allowed range and preserves frequency");
    QObject::disconnect(noticeConnection);
    int audio = 0;
    QStringList permissionNotices;
    QObject::connect(backend, &IRadioBackend::configurationWarning, backend,
                     [&](const QString& text) { permissionNotices.append(text); });
    QObject::connect(backend, &IRadioBackend::audioFrameReady, backend, [&](const PcmFrame&) { ++audio; });
    enqueueAudio(acquisition);
    drain();
    check(audio == 1 && backend->isConnected(), "control refusal does not interrupt receive audio");
    check(permissionNotices.size() == 1 && permissionNotices.first().contains("without source control"),
          "receiving without control has a distinct operator notice");
    enqueueAudio(acquisition);
    drain();
    check(audio == 2 && permissionNotices.size() == 1, "permission notice does not repeat for each audio packet");

    QMetaObject::invokeMethod(acquisition.worker, [acquisition] {
        emit acquisition.worker->sourceChanged(acquisition.generation, source());
    }, Qt::BlockingQueuedConnection);
    drain();
    check(slice->rxAntennaList() == QStringList{"A", "B", "C"}
          && backend->capabilities().sliceFrequencyControl.maximumHz == 2000000000
          && permissionNotices.size() == 1,
          "control grant updates available controls without reconnecting or repeating the listener notice");

    QMetaObject::invokeMethod(acquisition.worker, [acquisition] {
        QVariantMap observed = source(false);
        observed["antenna"] = 1;
        emit acquisition.worker->sourceChanged(acquisition.generation, observed);
        emit acquisition.worker->sourceChanged(acquisition.generation, observed);
    }, Qt::BlockingQueuedConnection);
    drain();
    check(permissionNotices.size() == 2 && permissionNotices.last().contains("control was transferred")
          && slice->rxAntenna() == "B" && slice->rxAntennaList() == QStringList{"B"}
          && !backend->requestReceiveTune(0, 430500000, IRadioBackend::ReceiveTuneView::Preserve),
          "control loss is announced once and adopts the radio's antenna and capture limits");
    enqueueAudio(acquisition);
    drain();
    check(audio == 3 && backend->isConnected() && permissionNotices.size() == 2,
          "audio continues after control is revoked during a session");
}
} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("nrsp-backend"));
    QCoreApplication app(argc, argv);
    relayAndRetirement();
    reentrantRetirement();
    antennaRefusal();
    explicitReconnectPolicy();
    nonOwnerModelState();
    return failures ? 1 : 0;
}
