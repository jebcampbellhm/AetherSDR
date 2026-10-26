#include "models/MonitorAudioRouter.h"
#include <QCoreApplication>
#include <cstdio>
using namespace AetherSDR;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) { ++failures; } };
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
    router.selectSource(-1);
    check(!outputs.back().epochLease().current(), "Off revokes converted-buffer lease");
    router.feedPrimary(*primary.produce(QVector<float>{0.1f, 0.1f}));
    check(outputs.size() == 4, "Off never forwards audio");
    PcmFrame retained;
    {
        MonitorAudioRouter temporary;
        QObject::connect(&temporary, &MonitorAudioRouter::pcmReady, &app, [&](const PcmFrame& f) { retained = f; });
        temporary.feedPrimary(*primary.produce(QVector<float>{0.2f, 0.2f}));
    }
    check(!retained.current(), "router destruction retires downstream audio");
    return failures ? 1 : 0;
}
