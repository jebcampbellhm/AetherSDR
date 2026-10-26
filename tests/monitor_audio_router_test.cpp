#include "models/MonitorAudioRouter.h"
#include <QCoreApplication>
#include <cstdio>
using namespace AetherSDR;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) { ++failures; } };
    {
        MonitorAudioRouter devices;
        int starts = 0, stops = 0;
        bool streaming = false;
        const auto start = [&] { ++starts; streaming = true; };
        const auto stop = [&] { ++stops; streaming = false; };
        const auto apply = [&](bool pc, bool restart = false, bool closing = false) {
            devices.requestPlayback(pc, restart, closing).dispatch(streaming, start, stop);
        };
        devices.setSourceAvailable(1, true);
        apply(true);
        check(starts == 0, "an unselected additional radio cannot open playback");
        devices.selectSource(1);
        apply(false);
        check(starts == 0, "PC Audio OFF prevents additional receiver device starts");
        check(stops == 2, "OFF cleans up residual output even when the main sink is already stopped");
        apply(true);
        check(starts == 1 && streaming, "selected additional receiver starts with the primary offline");
        stops = 0;
        devices.setSourceAvailable(0, true); apply(true);
        devices.setSourceAvailable(0, false); apply(true);
        check(starts == 1 && stops == 0 && streaming,
              "primary connect and disconnect do not restart or stop selected additional playback");
        const auto pendingStop = devices.requestPlayback(false);
        apply(true);
        pendingStop.dispatch(streaming, start, stop);
        check(stops == 0 && streaming, "superseded queued stop cannot close the current playback session");
        apply(true, true);
        check(starts == 2 && stops == 1, "explicit PC Audio recovery restarts only the local sink");
        devices.setSourceAvailable(1, false); apply(true);
        check(!streaming && stops == 2, "loss of the selected receiver closes playback");
        devices.setSourceAvailable(1, true);
        const auto pendingStart = devices.requestPlayback(true);
        devices.selectSource(-1);
        pendingStart.dispatch(streaming, start, stop);
        apply(true);
        check(starts == 2 && !streaming, "Off invalidates a queued receiver start despite another live source");
        devices.setMuted(true); apply(true);
        check(streaming && starts == 3, "an independent local playback owner retains the shared sink");
        apply(false);
        check(!streaming, "PC Audio OFF also stops an independent local playback owner");
        devices.setMuted(false); devices.selectSource(1);
        const auto beforeOff = devices.requestPlayback(true);
        apply(false);
        beforeOff.dispatch(streaming, start, stop);
        check(starts == 3 && !streaming, "late enable cannot undo a newer PC Audio OFF request");
        apply(true); const auto beforeClose = devices.requestPlayback(true, true);
        apply(true, false, true);
        beforeClose.dispatch(streaming, start, stop);
        check(starts == 4 && !streaming, "closing revokes pending restarts and stops selected additional audio");
        apply(true);
        const auto restarting = devices.requestPlayback(true, true);
        restarting.dispatch(streaming, start, [&] { stop(); devices.requestPlayback(false); });
        check(starts == 5 && !streaming, "a stop callback can cancel restart without opening the device again");
        MonitorPlaybackRequest destroyed;
        {
            MonitorAudioRouter temporary;
            temporary.setSourceAvailable(0, true);
            destroyed = temporary.requestPlayback(true);
        }
        destroyed.dispatch(streaming, start, stop);
        check(starts == 5, "router destruction revokes queued playback intents");
    }
    PcmProducer primary, additional;
    primary.start(); additional.start(PcmPurpose::Speaker, -1, {48000, PcmLayout::Mono});
    MonitorAudioRouter router;
    QVector<PcmFrame> outputs;
    QObject::connect(&router, &MonitorAudioRouter::pcmReady, &app, [&](const PcmFrame& frame) { outputs.append(frame); });
    const auto a = primary.produce(QVector<float>{0.1f, 0.2f});
    const auto b = additional.produce(QVector<float>{0.3f, 0.4f});
    router.feedPrimary(*a); router.feed(1, *b);
    check(outputs.size() == 1 && outputs[0].stream() == a->stream(), "primary alone selected; metadata unchanged");
    const auto primaryLease = outputs[0].epochLease();
    router.selectSource(1);
    check(!outputs[0].current() && !primaryLease.current() && a->current(), "switch revokes speaker copies without invalidating decoder input");
    router.feed(1, *b);
    check(outputs.size() == 2 && outputs[1].stream().format.sampleRateHz == 48000
        && outputs[1].samples() == b->samples() && outputs[1].discontinuity(), "additional PCM retains rate/layout and starts discontinuously");
    const auto additionalLease = outputs[1].epochLease();
    additional.invalidate();
    check(!outputs[1].current() && !additionalLease.current(), "producer retirement also revokes routed PCM");
    router.selectSource(0);
    check(!outputs[0].current(), "switching back cannot revive queued primary audio");
    router.feedPrimary(*a);
    check(outputs.size() == 2, "already delivered input is not replayed");
    const auto next = primary.produce(QVector<float>{0.5f, 0.6f});
    router.feedPrimary(*next);
    router.setMuted(true);
    check(!outputs.back().current() && next->current(), "playback mute revokes only speaker lifetime");
    const auto muted = primary.produce(QVector<float>{0.7f, 0.8f});
    router.feedPrimary(*muted);
    check(outputs.size() == 3, "muted route drops input");
    router.setMuted(false); router.feedPrimary(*muted);
    check(outputs.size() == 4 && outputs.back().discontinuity(), "unmute begins a fresh monitor epoch");
    const auto beforePcOff = outputs.back();
    router.requestPlayback(false);
    check(!beforePcOff.current() && muted->current(), "PC Audio OFF revokes queued speaker copies but preserves decoder input");
    router.feedPrimary(*primary.produce(QVector<float>{0.2f, 0.2f}));
    check(outputs.size() == 4, "PC Audio OFF does not accumulate new routed speaker frames");
    router.requestPlayback(true);
    router.feedPrimary(*primary.produce(QVector<float>{0.3f, 0.3f}));
    check(outputs.size() == 5 && outputs.back().discontinuity(), "PC Audio ON starts fresh speaker PCM without reviving off-period audio");
    router.selectSource(-1);
    check(!outputs.back().epochLease().current(), "Off revokes converted-buffer lease");
    router.feedPrimary(*primary.produce(QVector<float>{0.1f, 0.1f}));
    check(outputs.size() == 5, "Off never forwards audio");
    PcmFrame retained;
    {
        MonitorAudioRouter temporary;
        QObject::connect(&temporary, &MonitorAudioRouter::pcmReady, &app, [&](const PcmFrame& f) { retained = f; });
        temporary.feedPrimary(*primary.produce(QVector<float>{0.2f, 0.2f}));
    }
    check(!retained.current(), "router destruction retires downstream audio");
    return failures ? 1 : 0;
}
