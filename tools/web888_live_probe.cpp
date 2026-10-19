#include "TestSettingsProfile.h"
#include "core/KiwiSdrClient.h"
#include "core/backends/web888/Web888IqCapture.h"
#include "core/backends/web888/Web888Backend.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>
#include <QtEndian>
#include <cmath>
#include <array>
#include <cstdio>
#include <memory>

using namespace AetherSDR;

class ObservedCapture final : public Web888IqCapture {
public:
    quint64 inputSamples{}, inputBlocks{}, sequenceBreaks{};
    quint64 zeroTiming{}, validTiming{}, invalidTiming{}, timingSteps{}, timingRegressions{}, timingRepeats{};
    int maxSolutionAge{};
    double previousStamp{-1}, maxTimingStepError{};
    qsizetype previousSamples{};
    double advertisedRate{};
    qint64 firstBlockNs{}, lastBlockNs{};
    qsizetype firstBlockSamples{};
    QElapsedTimer clock;
    ObservedCapture() { clock.start(); }
protected:
    KiwiSdrClient* createClient() override
    {
        KiwiSdrClient* client = Web888IqCapture::createClient();
        connect(client, &KiwiSdrClient::filteredIqReady, this, [this](const KiwiIqBlock& block) {
            const auto& h = block.timingHeader;
            // Diagnostic only: this ARM firmware writes packed native little-endian words.
            // No week/UTC conversion or decoder anchor is inferred here.
            if (h.size() == 10) {
                const auto* bytes = reinterpret_cast<const uchar*>(h.data());
                const auto sec = qFromLittleEndian<quint32>(bytes + 2);
                const auto ns = qFromLittleEndian<quint32>(bytes + 6);
                if (sec == 0 && ns == 0 && bytes[0] == 0) { ++zeroTiming; previousStamp = -1; }
                else if (sec >= 604800 || ns >= 1000000000 || bytes[0] == 255) {
                    ++invalidTiming; previousStamp = -1;
                } else {
                    ++validTiming;
                    if (validTiming <= 6) std::printf("timing_example_age=%u sec=%u ns=%u samples=%lld\n",
                        unsigned(bytes[0]), unsigned(sec), unsigned(ns), static_cast<long long>(block.samples.size()));
                    maxSolutionAge = qMax(maxSolutionAge, int(bytes[0]));
                    const double stamp = sec + ns / 1e9;
                    if (previousStamp >= 0 && !block.discontinuity && advertisedRate > 0) {
                        double step = stamp - previousStamp;
                        if (step < -302400) step += 604800;
                        if (step < 0) ++timingRegressions;
                        if (step == 0) ++timingRepeats;
                        maxTimingStepError = qMax(maxTimingStepError, std::abs(step - previousSamples / advertisedRate));
                        ++timingSteps;
                    }
                    previousStamp = stamp;
                }
            } else { ++invalidTiming; previousStamp = -1; }
            previousSamples = block.samples.size();
            lastBlockNs = clock.nsecsElapsed();
            if (inputBlocks == 0) {
                firstBlockNs = lastBlockNs;
                firstBlockSamples = block.samples.size();
            }
            ++inputBlocks;
            inputSamples += block.samples.size();
            sequenceBreaks += block.discontinuity ? 1 : 0;
        });
        return client;
    }
};

static int runBackendProbe(QCoreApplication& app, const QString& endpoint,
                           const QString& password, int seconds)
{
    Web888Backend backend;
    QMap<int, quint64> samples, rows;
    QMap<int, int> displayOwners;
    QMap<int, quint64> digitalSamples;
    QElapsedTimer progressClock;
    progressClock.start();
    QMap<int, qint64> pcmLast, waterfallLast, digitalLast;
    QMap<int, qint64> pcmGap, waterfallGap, digitalGap;
    const auto progress = [&](int id, auto& last, auto& gap) {
        const qint64 now = progressClock.elapsed();
        if (last.contains(id)) { gap[id] = qMax(gap.value(id), now - last[id]); }
        last[id] = now;
    };
    const auto fresh = [&](int id, const auto& last) {
        return last.contains(id) && progressClock.elapsed() - last.value(id) <= 5000;
    };
    bool failed = false;
    bool stopping = false;
    const auto finish = [&] {
        if (stopping) { return; }
        stopping = true;
        for (int id : {0, 1, 2}) {
            std::printf("backend_slice=%d frames=%llu waterfall_rows=%llu\n", id, samples[id], rows[id]);
            std::printf("backend_slice=%d max_pcm_gap_ms=%lld max_waterfall_gap_ms=%lld\n",
                id, static_cast<long long>(pcmGap.value(id)), static_cast<long long>(waterfallGap.value(id)));
            if (samples[id] == 0 || rows[id] == 0 || pcmGap.value(id) > 5000
                || waterfallGap.value(id) > 5000) { failed = true; }
            if (id != 1 && (!fresh(id, pcmLast) || !fresh(id, waterfallLast))) { failed = true; }
        }
        for (int id : {100, 101}) {
            std::printf("backend_digital_slice=%d frames=%llu\n", id, digitalSamples[id]);
            std::printf("backend_digital_slice=%d max_gap_ms=%lld\n",
                id, static_cast<long long>(digitalGap.value(id)));
            if (digitalSamples[id] == 0 || digitalGap.value(id) > 5000
                || !fresh(id, digitalLast)) { failed = true; }
        }
        backend.disconnectRadio();
        if (backend.digitalCaptureCount() || backend.pendingDigitalCaptureCount()) { failed = true; }
        QTimer::singleShot(500, &app, [&] { app.exit(failed ? 1 : 0); });
    };
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &app,
        [&](int id, const PcmFrame& frame) {
            if (frame.current() && frame.frameCount() > 0) {
                samples[id] += frame.frameCount();
                progress(id, pcmLast, pcmGap);
            }
        });
    QObject::connect(&backend, &IRadioBackend::spectrumFrameReady, &app,
        [&](int id, const QByteArray& bins) {
            if (!bins.isEmpty()) {
                const int owner = displayOwners.value(id, -1);
                ++rows[owner];
                progress(owner, waterfallLast, waterfallGap);
            }
        });
    QObject::connect(&backend, &IRadioBackend::sliceChanged, &app,
        [&](int id, const SliceDelta& delta) {
            if (delta.panId) { displayOwners[delta.panId->toInt()] = id; }
            std::printf("backend_slice_present=%d\n", id);
        });
    QObject::connect(&backend, &IRadioBackend::sliceRemoved, &app,
        [&](int id) { std::printf("backend_slice_removed=%d\n", id); });
    QObject::connect(&backend, &IRadioBackend::connectionError, &app,
        [&](const QString&) { failed = true; QTimer::singleShot(0, &app, finish); });
    QObject::connect(&backend, &IRadioBackend::sliceLifecycleFailed, &app,
        [&](const QString&, int, const QString&) { failed = true; QTimer::singleShot(0, &app, finish); });
    QObject::connect(&backend, &Web888Backend::digitalAudioReady, &app,
        [&](const Web888DecoderAudio& audio) {
            if (audio.current() && !audio.mono.isEmpty()) {
                digitalSamples[audio.sliceId] += audio.mono.size();
                progress(audio.sliceId, digitalLast, digitalGap);
            }
        });
    QObject::connect(&backend, &Web888Backend::digitalProfileFailed, &app,
        [&](const QString&) { failed = true; QTimer::singleShot(0, &app, finish); });
    QObject::connect(&backend, &Web888Backend::digitalCaptureFailed, &app,
        [&](quint64, const QString&) { failed = true; QTimer::singleShot(0, &app, finish); });
    QObject::connect(&backend, &IRadioBackend::connected, &app, [&] {
        QTimer::singleShot(0, &app, [&] {
            if (stopping) { return; }
            const QVector<Web888DigitalPreset> profile{
                {QStringLiteral("30m"), QStringLiteral("FT8"), {100, 10136000, 0, 3000}},
                {QStringLiteral("30m"), QStringLiteral("WSPR"), {101, 10138700, 1400, 1600}}};
            if (!backend.applyDigitalProfile(profile, 24000, 3)
                || !backend.createPanadapter()) { failed = true; finish(); }
        });
    });
    QTimer::singleShot(20000, &app, [&] {
        if (stopping) { return; }
        backend.setSliceFrequency(1, 21074000);
        backend.setPanCenter(QStringLiteral("1"), 21074000, IRadioBackend::PanCenterIntent::Drag);
        backend.setPanBandwidth(QStringLiteral("1"), 50000);
        backend.setActiveSlice(1);
    });
    QTimer::singleShot(40000, &app, [&] {
        if (stopping) { return; }
        if (!fresh(1, pcmLast) || !fresh(1, waterfallLast)) { failed = true; }
        if (!backend.removePanadapter(QStringLiteral("1"))) { failed = true; finish(); }
    });
    QTimer::singleShot(50000, &app, [&] {
        if (stopping) { return; }
        if (!backend.createPanadapter()) { failed = true; finish(); }
    });
    QTimer::singleShot(seconds * 1000, &app, finish);
    RadioConnectRequest request;
    request.params.insert(QStringLiteral("web888.endpoint"), endpoint);
    request.params.insert(QStringLiteral("web888.password"), password);
    backend.connectRadio(request);
    return app.exec();
}

static int runIndependentListenerProbe(QCoreApplication& app, const QString& endpoint,
                             const QString& password, int seconds)
{
    Web888Backend backend;
    QMap<int, quint64> samples;
    quint64 rows = 0;
    double width = 0;
    bool shared = false, failed = false, stopping = false, restored = false;
    quint64 primaryBeforeRestore = 0, childAtRestore = 0;
    QObject::connect(&backend, &IRadioBackend::connectionError, &app,
        [&](const QString& reason) {
            failed = true;
            QString safe = reason;
            if (!password.isEmpty()) { safe.replace(password, QStringLiteral("[redacted]")); }
            std::fprintf(stderr, "listener_connection_error=%s\n", qPrintable(safe));
        });
    QObject::connect(&backend, &IRadioBackend::sliceLifecycleFailed, &app,
        [&](const QString& operation, int id, const QString& reason) {
            if (!stopping) { failed = true; }
            QString safe = reason;
            if (!password.isEmpty()) { safe.replace(password, QStringLiteral("[redacted]")); }
            std::fprintf(stderr, "listener_error id=%d operation=%s reason=%s\n", id,
                         qPrintable(operation), qPrintable(safe));
        });
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &app,
        [&](int id, const PcmFrame& frame) { if (frame.current()) { samples[id] += frame.frameCount(); } });
    QObject::connect(&backend, &IRadioBackend::spectrumFrameReady, &app,
        [&](int, const QByteArray&, const SpectrumCoverage&) { ++rows; });
    QObject::connect(&backend, &IRadioBackend::panCenterBandwidthChanged, &app,
        [&](const QString&, double, double span) { width = span * 1e6; });
    QObject::connect(&backend, &IRadioBackend::connected, &app, [&] {
        QTimer::singleShot(1000, &backend, [&] {
            backend.setSliceFrequency(0, 14200000);
            backend.setSliceMode(0, QStringLiteral("USB"));
            backend.setPanCenter("0", 14175000, IRadioBackend::PanCenterIntent::Drag);
            backend.setPanBandwidth("0", 350000);
            // Let the initial hardware retune settle before admitting another receiver.
            QTimer::singleShot(1000, &backend, [&] {
                shared = backend.createSlice("0", 21200000);
                if (!shared) { failed = true; }
                backend.setSliceAgc(0, "slow", 0);
                backend.setSliceAgc(1, "fast", 0);
                samples.clear();
            });
        });
    });
    QTimer::singleShot(seconds * 500, &backend, [&] {
        if (!shared) { return; }
        backend.setSliceFrequency(1, 28200000);
        backend.setSliceFilter(1, 200, 2400);
        backend.setActiveSlice(1);
    });
    QTimer::singleShot((seconds - 4) * 1000, &backend, [&] {
        primaryBeforeRestore = samples[0]; childAtRestore = samples[1];
        restored = backend.removeSlice(1);
    });
    QTimer::singleShot(seconds * 1000, &app, [&] {
        stopping = true;
        failed |= !shared || samples[0] == 0 || samples[1] == 0 || rows == 0
            || !restored || samples[0] <= primaryBeforeRestore || samples[1] != childAtRestore;
        std::printf("independent_listeners=%d primary_frames=%llu secondary_frames=%llu waterfall_rows=%llu waterfall_span_hz=%.0f failed=%d\n",
            int(shared), samples[0], samples[1], rows, width, int(failed));
        backend.disconnectRadio();
        QTimer::singleShot(500, &app, [&] { app.exit(failed ? 1 : 0); });
    });
    RadioConnectRequest request;
    request.params.insert(QStringLiteral("web888.endpoint"), endpoint);
    request.params.insert(QStringLiteral("web888.password"), password);
    backend.connectRadio(request);
    return app.exec();
}

// Explicit combined-load diagnostic; all leases are released on the timer.
static int runCombinedProbe(QCoreApplication& app, const QString& endpoint,
                            const QString& password, int seconds)
{
    Web888Backend backend;
    QMap<int, quint64> audio, digital, rows, audioBeforeStop, digitalAtStop;
    bool monitoringStopped = false;
    QMap<int, qint64> lastAudio, lastDigital, lastRow;
    QElapsedTimer clock; clock.start();
    bool failed = false, stopping = false;
    QVector<Web888DigitalPreset> presets;
    for (const auto& preset : web888DigitalPresets()) {
        if (QStringList{"40m", "30m", "20m", "10m"}.contains(preset.band)) { presets.append(preset); }
    }
    QObject::connect(&backend, &IRadioBackend::sliceAudioFrameReady, &app,
        [&](int id, const PcmFrame& frame) {
            if (frame.current()) { audio[id] += frame.frameCount(); lastAudio[id] = clock.elapsed(); }
        });
    QObject::connect(&backend, &Web888Backend::digitalAudioReady, &app,
        [&](const Web888DecoderAudio& frame) {
            if (frame.current()) { digital[frame.sliceId] += frame.mono.size(); lastDigital[frame.sliceId] = clock.elapsed(); }
        });
    QObject::connect(&backend, &IRadioBackend::spectrumFrameReady, &app,
        [&](int id, const QByteArray& bins) {
            if (!bins.isEmpty()) { ++rows[id]; lastRow[id] = clock.elapsed(); }
        });
    QObject::connect(&backend, &IRadioBackend::connectionError, &app, [&](const QString&) { failed = true; });
    QObject::connect(&backend, &IRadioBackend::sliceLifecycleFailed, &app,
        [&](const QString&, int, const QString&) { if (!stopping) { failed = true; } });
    QObject::connect(&backend, &Web888Backend::digitalCaptureFailed, &app,
        [&](quint64, const QString&) { if (!stopping) { failed = true; } });
    QObject::connect(&backend, &Web888Backend::digitalProfileFailed, &app,
        [&](const QString&) { if (!stopping) { failed = true; } });
    QObject::connect(&backend, &IRadioBackend::connected, &app, [&] {
        QTimer::singleShot(1000, &backend, [&] {
            backend.setSliceMode(0, "USB"); backend.setSliceFrequency(0, 14200000);
            failed |= !backend.createPanadapter();
            failed |= !backend.applyDigitalProfile(presets, 24000, 11);
            QTimer::singleShot(2000, &backend, [&] {
                backend.setSliceFrequency(1, 7150000);
                QTimer::singleShot(1000, &backend, [&] {
                    failed |= !backend.createSlice("0", 21200000);
                    failed |= !backend.createSlice("1", 7155000);
                    audio.clear(); digital.clear(); rows.clear();
                });
            });
        });
    });
    QTimer::singleShot(seconds * 500, &backend, [&] {
        backend.setSliceMode(2, "LSB"); backend.setSliceFrequency(2, 14207000);
        backend.setSliceFilter(2, -2400, -200); backend.setSliceAgc(2, "fast", 0);
        backend.setSliceFrequency(3, 7157000); backend.setSliceAgc(3, "slow", 0);
        backend.setActiveSlice(3);
    });
    QTimer::singleShot((seconds - 4) * 1000, &app, [&] {
        failed |= backend.digitalCaptureCount() != 7 || backend.pendingDigitalCaptureCount() != 0;
        audioBeforeStop = audio; digitalAtStop = digital;
        backend.stopDigitalProfile(); monitoringStopped = true;
    });
    QTimer::singleShot(seconds * 1000, &app, [&] {
        const auto fresh = [&](const auto& map, int id) {
            return map.contains(id) && clock.elapsed() - map.value(id) < 5000;
        };
        for (int id : {0, 1, 2, 3}) {
            std::printf("combined_listener=%d frames=%llu\n", id, audio[id]);
            failed |= audio[id] == 0 || !fresh(lastAudio, id) || audio[id] <= audioBeforeStop[id];
        }
        for (int id : {0, 1}) {
            std::printf("combined_pan=%d rows=%llu\n", id, rows[id]);
            failed |= rows[id] == 0 || !fresh(lastRow, id);
        }
        for (const auto& preset : presets) {
            const int id = preset.slice.id;
            std::printf("combined_digital=%d samples=%llu\n", id, digital[id]);
            failed |= digital[id] == 0 || !fresh(lastDigital, id);
        }
        failed |= !monitoringStopped || digital != digitalAtStop || backend.digitalCaptureCount() != 0;
        failed |= backend.takeDigitalDecoderJob().has_value(); // no qualified UTC anchor
        stopping = true; backend.disconnectRadio();
        failed |= backend.digitalCaptureCount() != 0 || backend.pendingDigitalCaptureCount() != 0;
        std::printf("combined_failed=%d\n", int(failed));
        QTimer::singleShot(500, &app, [&] { app.exit(failed ? 1 : 0); });
    });
    RadioConnectRequest request;
    request.params.insert("web888.endpoint", endpoint); request.params.insert("web888.password", password);
    backend.connectRadio(request);
    return app.exec();
}

// Manual hardware diagnostic. No decoder execution, reporting, or admin commands.
int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("web888-live-probe"));
    if (!profile.isValid()) { return 2; }
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Receive-only Web-888 transport diagnostic; uses three RX slots and two waterfalls."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("endpoint"), QStringLiteral("Explicit receiver host:port (required)"), QStringLiteral("host:port")});
    parser.addOption({QStringLiteral("seconds"), QStringLiteral("Duration, 10..3600 seconds"), QStringLiteral("seconds"), QStringLiteral("45")});
    parser.addOption({QStringLiteral("exercise"), QStringLiteral("Retune and reconnect the second listener; requires at least 100 seconds")});
    parser.addOption({QStringLiteral("backend"), QStringLiteral("Exercise dual-pan backend with one shared digital capture; requires at least 70 seconds")});
    parser.addOption({QStringLiteral("independent-listeners"), QStringLiteral("One waterfall with two independent RX leases")});
    parser.addOption({QStringLiteral("combined"), QStringLiteral("Four independent listeners plus seven digital captures; use at least 20 seconds")});
    parser.process(app);
    bool validDuration = false;
    const int seconds = parser.value(QStringLiteral("seconds")).toInt(&validDuration);
    const QString endpoint = parser.value(QStringLiteral("endpoint"));
    if (endpoint.isEmpty() || !validDuration || seconds < 10 || seconds > 3600 || (parser.isSet(QStringLiteral("exercise")) && seconds < 100) || (parser.isSet(QStringLiteral("backend")) && seconds < 70)) {
        std::fprintf(stderr, "%s", qPrintable(parser.helpText()));
        return 2;
    }
    QString password = qEnvironmentVariable("AETHER_WEB888_PASSWORD");
    qunsetenv("AETHER_WEB888_PASSWORD");
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=false"));
    if (parser.isSet(QStringLiteral("combined"))) {
        if (seconds < 20) { return 2; }
        return runCombinedProbe(app, endpoint, password, seconds);
    }
    if (parser.isSet(QStringLiteral("independent-listeners"))) {
        return runIndependentListenerProbe(app, endpoint, password, seconds);
    }
    if (parser.isSet(QStringLiteral("backend"))) {
        return runBackendProbe(app, endpoint, password, seconds);
    }
    struct Counts {
        quint64 pcmBlocks{}, pcmFrames{}, waterfallRows{}, gaps{};
        int rate{};
    };
    std::array<Counts, 2> counts{};
    std::array<std::unique_ptr<KiwiSdrClient>, 2> clients;
    const std::array<double, 2> frequencies{7.074, 14.074};
    ObservedCapture capture;
    std::array<quint64, 2> decoderFrames{};
    std::array<quint64, 2> decoderBreaks{};
    double iqRate{};
    bool stopping = false;
    bool failed = false;
    bool reconnectingSecond = false;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto summary = [&] {
        std::printf("elapsed_ms=%lld iq_rate=%.6f ft8_frames=%llu wspr_frames=%llu ft8_breaks=%llu wspr_breaks=%llu\n",
            static_cast<long long>(elapsed.elapsed()), iqRate, decoderFrames[0], decoderFrames[1], decoderBreaks[0], decoderBreaks[1]);
        const double observedRate = capture.lastBlockNs > capture.firstBlockNs
            ? double(capture.inputSamples - capture.firstBlockSamples) * 1e9 / double(capture.lastBlockNs - capture.firstBlockNs) : 0;
        std::printf("iq_blocks=%llu iq_samples=%llu iq_breaks=%llu observed_iq_samples_per_second=%.3f\n",
            capture.inputBlocks, capture.inputSamples, capture.sequenceBreaks, observedRate);
        std::printf("timing_zero=%llu timing_valid_shape=%llu timing_invalid=%llu timing_steps=%llu timing_regressions=%llu timing_repeats=%llu max_solution_age=%d max_step_error_seconds=%.9f\n",
            capture.zeroTiming, capture.validTiming, capture.invalidTiming, capture.timingSteps,
            capture.timingRegressions, capture.timingRepeats, capture.maxSolutionAge, capture.maxTimingStepError);
        for (int i = 0; i < 2; ++i) {
            const Counts& c = counts[i];
            std::printf("listener=%d pcm_blocks=%llu pcm_frames=%llu output_rate=%d waterfall_rows=%llu sound_gaps=%llu\n",
                i, c.pcmBlocks, c.pcmFrames, c.rate, c.waterfallRows, c.gaps);
        }
        std::fflush(stdout);
    };
    const auto finish = [&] {
        if (stopping) { return; }
        stopping = true;
        if (capture.inputBlocks == 0 || decoderFrames[0] == 0 || decoderFrames[1] == 0
            || counts[0].pcmFrames == 0 || counts[1].pcmFrames == 0
            || counts[0].waterfallRows == 0 || counts[1].waterfallRows == 0) {
            failed = true;
            std::fprintf(stderr, "Required stream delivered no data\n");
        }
        summary();
        capture.stop();
        for (const auto& client : clients) {
            if (client) { client->disconnectFromEndpoint(); }
        }
        QTimer::singleShot(500, &app, [&] { app.exit(failed ? 1 : 0); });
    };
    for (int i = 0; i < 2; ++i) {
        clients[i] = std::make_unique<KiwiSdrClient>(KiwiSdrClient::Streams::SoundAndWaterfall,
            KiwiSdrClient::Admission::IndependentReceiver);
        KiwiSdrClient* client = clients[i].get();
        client->setReceiverFamily(KiwiSdrProtocol::KiwiSdrReceiverFamily::Web888);
        client->setOperatorCallsign(QStringLiteral("AetherSDR transport diagnostic"));
        const QString pan = QStringLiteral("probe-%1").arg(i);
        client->setTrackedSlice(i, frequencies[i], QStringLiteral("USB"), 300, 2800, pan, {}, 0);
        client->setWaterfallView(pan, frequencies[i], 0.1);
        client->setDecodeAudioWhenInactive(true);
        QObject::connect(client, &KiwiSdrClient::pcmFrameReady, &app, [&, i](const PcmFrame& frame) {
            if (frame.current()) {
                ++counts[i].pcmBlocks;
                counts[i].pcmFrames += frame.frameCount();
                counts[i].rate = frame.stream().format.sampleRateHz;
            }
        });
        QObject::connect(client, &KiwiSdrClient::waterfallRowReady, &app,
            [&, i](const QString&, const QVector<float>&, double, double, quint32) { ++counts[i].waterfallRows; });
        QObject::connect(client, &KiwiSdrClient::telemetryChanged, &app,
            [&, i](const KiwiSdrReceiverTelemetry& value) { counts[i].gaps = value.soundSequenceGaps; });
        QObject::connect(client, &KiwiSdrClient::stateChanged, &app,
            [&, i](KiwiSdrClient::State state, const QString&) {
                std::printf("listener=%d state=%d\n", i, int(state));
                if (i == 1 && reconnectingSecond && state == KiwiSdrClient::State::Connected) {
                    reconnectingSecond = false;
                    std::printf("second_listener_reconnected=1\n");
                }
                if (!stopping && (state == KiwiSdrClient::State::Error || state == KiwiSdrClient::State::Busy)) {
                    failed = true;
                    QTimer::singleShot(0, &app, finish);
                }
            });
        client->connectToEndpoint(endpoint, password);
    }
    QObject::connect(&capture, &Web888IqCapture::ready, &app, [&](double rate) { iqRate = rate; capture.advertisedRate = rate; });
    QObject::connect(&capture, &Web888IqCapture::decoderAudioReady, &app, [&](const Web888DecoderAudio& audio) {
        if (audio.current() && audio.sliceId >= 0 && audio.sliceId < 2) {
            decoderFrames[audio.sliceId] += audio.mono.size();
            decoderBreaks[audio.sliceId] += audio.discontinuity ? 1 : 0;
        }
    });
    QObject::connect(&capture, &Web888IqCapture::failed, &app, [&](const QString& reason) {
        // Backend errors contain no credentials; also redact the provided secret defensively.
        QString safeReason = reason;
        if (!password.isEmpty()) { safeReason.replace(password, QStringLiteral("<redacted>")); }
        std::fprintf(stderr, "IQ capture failed: %s\n", qPrintable(safeReason));
        failed = true;
        QTimer::singleShot(0, &app, finish);
    });
    capture.start(endpoint, 10138150, {{0, 10136000, 0, 3000}, {1, 10138700, 1400, 1600}}, password);
    if (parser.isSet(QStringLiteral("exercise"))) {
        QTimer::singleShot(20000, &app, [&] {
            if (stopping) { return; }
            clients[1]->setTrackedSlice(1, 21.074, QStringLiteral("USB"), 300, 2800,
                                       QStringLiteral("probe-1"), {}, 0);
            clients[1]->setWaterfallView(QStringLiteral("probe-1"), 21.074, 0.05);
            std::printf("exercise=retune_second_15m\n");
        });
        QTimer::singleShot(40000, &app, [&] {
            if (stopping) { return; }
            clients[1]->disconnectFromEndpoint();
            std::printf("exercise=disconnect_second\n");
        });
        QTimer::singleShot(50000, &app, [&] {
            if (stopping) { return; }
            reconnectingSecond = true;
            clients[1]->connectToEndpoint(endpoint, password);
            std::printf("exercise=reconnect_second\n");
        });
        QTimer::singleShot(70000, &app, [&] {
            if (stopping) { return; }
            clients[0]->setWaterfallView(QStringLiteral("probe-0"), 7.074, 0.05);
            std::printf("exercise=zoom_first\n");
        });
    }
    QTimer progress;
    QObject::connect(&progress, &QTimer::timeout, &app, [&] { if (!stopping) { summary(); } });
    progress.start(15000);
    QTimer::singleShot(seconds * 1000, &app, finish);
    return app.exec();
}
