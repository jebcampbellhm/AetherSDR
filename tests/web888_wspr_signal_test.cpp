#include "core/WsprBeacon.h"
#include "core/backends/web888/Web888DecoderWav.h"
#include <QtEndian>
#include <QSaveFile>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)
constexpr double tau = 2 * std::numbers::pi;

int main(int argc, char** argv)
{
    double rate = 24000;
    QString outputPath;
    for (int i = 1; i < argc; i += 2) {
        CHECK(i + 1 < argc);
        if (QByteArray(argv[i]) == "--write-wav") outputPath = QString::fromLocal8Bit(argv[i + 1]);
        else if (QByteArray(argv[i]) == "--sample-rate") {
            bool ok = false;
            rate = QString::fromLatin1(argv[i + 1]).toDouble(&ok);
            CHECK(ok && std::isfinite(rate) && rate >= 8000 && rate <= 48000);
        } else CHECK(false);
    }
    // Only the pure encoder is used. No beacon instance, TX model or radio exists.
    const auto encoded = WsprBeacon::encode("K1ABC", "FN42", 37);
    CHECK(encoded);
    const QVector<Web888DigitalPreset> presets{{"30m", "FT8", {1, 10136000, 0, 3000}},
        {"30m", "WSPR", {2, 10138700, 1400, 1600}}};
    Web888IqSplitter splitter;
    CHECK(splitter.configure({presets[0].slice, presets[1].slice}));
    Web888DecoderRouter router;
    CHECK(router.configure(presets, 16 * 1024 * 1024));
    auto lifetime = std::make_shared<std::atomic<bool>>(true);
    bool armed[3]{};
    int ft8Periods = 0, wsprPeriods = 0;
    std::optional<Web888DecoderJob> wspr;
    KiwiIqBlock block; block.sampleRateHz = rate; block.centerHz = 10139000;
    block.generation = 1;
    double phase = 0;
    qint64 inputIndex = 0;
    const auto totalInput = qint64(std::ceil(121 * rate));
    while (inputIndex < totalInput) {
        const int count = int(std::min<qint64>(997 + block.sequence % 19, totalInput - inputIndex));
        block.samples.resize(count);
        for (int n = 0; n < count; ++n, ++inputIndex) {
            // Encoder symbols use a 24 kHz reference clock, independent of input rate.
            const double messageIndex = double(inputIndex) * 24000 / rate - 24000;
            const bool active = messageIndex >= 0 && messageIndex < WsprBeacon::kMessageFrames;
            const int symbol = active ? int(messageIndex / WsprBeacon::kFramesPerSymbol) : 0;
            const double frequency = 10140200 - block.centerHz
                + (active ? encoded.symbols[symbol] * WsprBeacon::kToneSpacingHz : 0);
            // Adjacent FT8-dial tone is four times the WSPR voltage amplitude.
            auto value = 0.8 * std::polar(1.0, tau * (10137000 - block.centerHz) * inputIndex / rate);
            if (active) value += 0.2 * std::polar(1.0, phase);
            phase = std::remainder(phase + tau * frequency / rate, tau);
            block.samples[n] = std::complex<float>(value);
        }
        QString error;
        const auto outputs = splitter.process(block, &error);
        CHECK(error.isEmpty());
        for (auto audio : outputs) {
            audio.captureId = 1; audio.lifetime = lifetime;
            if (!armed[audio.sliceId]) {
                // Synthetic clock origin is the first post-filter output sample.
                CHECK(router.arm(audio, 0)); armed[audio.sliceId] = true;
            }
            CHECK(router.push(audio));
        }
        while (auto job = router.takeNext()) {
            if (job->mode == "WSPR") { ++wsprPeriods; wspr = std::move(job); }
            else ++ft8Periods;
        }
        ++block.sequence; block.discontinuity = false;
    }
    CHECK(wspr && wsprPeriods == 1 && ft8Periods == 8 && !router.droppedJobs());
    const auto wav = web888DecoderWav(*wspr);
    CHECK(wav && wav->size() == 2880044);
    const auto first = qint64(wspr->period.audio.firstSampleIndex);
    const auto delay = qint64(std::llround(wspr->period.audio.filterDelaySeconds * 12000));
    double weakest = 1;
    for (int symbol = 0; symbol < WsprBeacon::kSymbolCount; ++symbol) {
        const qint64 center = 12000 + qint64(symbol) * 8192 + 4096 + delay - first;
        constexpr int window = 6144; // avoid filter transients near symbol boundaries
        double best = -1; int detected = -1;
        for (int tone = 0; tone < 4; ++tone) {
            std::complex<double> sum{};
            const double hz = 1500 + tone * WsprBeacon::kToneSpacingHz;
            for (int n = 0; n < window; ++n) {
                const qint64 index = center - window / 2 + n;
                CHECK(index >= 0 && index < 1440000);
                const double value = qFromLittleEndian<qint16>(wav->data() + 44 + index * 2) / 32768.0;
                sum += value * std::polar(1.0, -tau * hz * n / 12000);
            }
            const double strength = 2 * std::abs(sum) / window;
            if (strength > best) { best = strength; detected = tone; }
        }
        CHECK(detected == encoded.symbols[symbol]);
        CHECK(best > 0.18 && best < 0.22);
        weakest = std::min(weakest, best);
    }
    // Explicit opt-in local artifact for independent decoder acceptance later.
    // Default CTest remains entirely in memory.
    if (!outputPath.isEmpty()) {
        QSaveFile output(outputPath);
        CHECK(output.open(QIODevice::WriteOnly));
        CHECK(wspr->current() && output.write(*wav) == wav->size());
        CHECK(wspr->current() && output.commit());
    }
    lifetime->store(false);
    CHECK(!web888DecoderWav(*wspr));
    std::printf("WSPR encoded fixture: all 162 symbols preserved through shared IQ, routing and PCM16 WAV; input_rate=%.3f weakest=%.6f\n", rate, weakest);
    return 0;
}
