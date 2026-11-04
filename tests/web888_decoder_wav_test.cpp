#include "core/backends/web888/Web888DecoderWav.h"
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <limits>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)
int main()
{
    Web888DecoderJob job;
    auto source = std::make_shared<std::atomic<bool>>(true);
    auto route = std::make_shared<std::atomic<bool>>(true);
    job.period.audio.lifetime = source; job.routeLifetime = route;
    QString error;
    for (const QString mode : {QString("FT4"), QString("FT8"), QString("WSPR")}) {
        job.mode = mode;
        const int count = mode == "FT4" ? 90000 : mode == "FT8" ? 180000 : 1440000;
        job.period.startUtcTicks = qint64(count) * 100000;
        job.period.audio.mono.fill(0, count);
        const float values[]{-2, -1, -0.5f, 0, 0.5f, 1, 2};
        const qint16 expected[]{-32768, -32768, -16384, 0, 16384, 32767, 32767};
        for (int n = 0; n < 7; ++n) job.period.audio.mono[n] = values[n];
        auto wav = web888DecoderWav(job, &error);
        CHECK(wav && error.isEmpty() && wav->size() == 44 + count * 2);
        CHECK(wav->mid(0, 4) == "RIFF" && wav->mid(8, 8) == "WAVEfmt " && wav->mid(36, 4) == "data");
        CHECK(qFromLittleEndian<quint32>(wav->data() + 4) == wav->size() - 8);
        CHECK(qFromLittleEndian<quint32>(wav->data() + 16) == 16);
        CHECK(qFromLittleEndian<quint16>(wav->data() + 20) == 1 && qFromLittleEndian<quint16>(wav->data() + 22) == 1);
        CHECK(qFromLittleEndian<quint32>(wav->data() + 24) == 12000 && qFromLittleEndian<quint32>(wav->data() + 28) == 24000);
        CHECK(qFromLittleEndian<quint16>(wav->data() + 32) == 2 && qFromLittleEndian<quint16>(wav->data() + 34) == 16);
        CHECK(qFromLittleEndian<quint32>(wav->data() + 40) == count * 2);
        for (int n = 0; n < 7; ++n) CHECK(qFromLittleEndian<qint16>(wav->data() + 44 + n * 2) == expected[n]);
        CHECK(qFromLittleEndian<qint16>(wav->data() + wav->size() - 2) == 0);
        ++job.period.startUtcTicks;
        CHECK(!web888DecoderWav(job, &error) && !error.isEmpty());
        --job.period.startUtcTicks;
    }
    source->store(false); CHECK(!web888DecoderWav(job)); source->store(true);
    route->store(false); CHECK(!web888DecoderWav(job)); route->store(true);
    job.period.audio.mono[0] = std::numeric_limits<float>::infinity();
    CHECK(!web888DecoderWav(job));
    job.period.audio.mono[0] = 0; job.period.audio.mono.removeLast();
    CHECK(!web888DecoderWav(job));
    job.mode = "unknown"; CHECK(!web888DecoderWav(job));
    // Exercise real routed jobs, including a discarded partial starting period
    // and cancellation after a consumer has already serialized the audio.
    for (const QString mode : {QString("FT4"), QString("FT8"), QString("WSPR")}) {
        const int count = mode == "FT4" ? 90000 : mode == "FT8" ? 180000 : 1440000;
        Web888DecoderRouter router;
        CHECK(router.configure({{"20m", mode, {7, 14074000}}}, quint64(count) * sizeof(float) * 2));
        Web888DecoderAudio audio;
        audio.sliceId = 7; audio.dialHz = 14074000; audio.captureId = 81;
        audio.lifetime = std::make_shared<std::atomic<bool>>(true);
        audio.discontinuity = true;
        constexpr int offset = 173;
        CHECK(router.arm(audio, offset));
        const quint64 total = quint64(2 * count - offset);
        while (audio.firstSampleIndex < total) {
            const int size = int(std::min<quint64>(997, total - audio.firstSampleIndex));
            audio.mono.fill(0.25f, size);
            CHECK(router.push(audio));
            audio.firstSampleIndex += size;
            audio.discontinuity = false;
        }
        auto routed = router.takeNext();
        CHECK(routed && routed->current() && routed->mode == mode);
        CHECK(routed->period.startUtcTicks == count);
        CHECK(routed->period.audio.firstSampleIndex == quint64(count - offset));
        auto bytes = web888DecoderWav(*routed, &error);
        CHECK(bytes && error.isEmpty() && bytes->size() == 44 + count * 2);
        for (int n = 0; n < count; ++n)
            CHECK(qFromLittleEndian<qint16>(bytes->data() + 44 + n * 2) == 8192);
        CHECK(router.disarmCapture(81) == 1 && audio.current() && !routed->current());
        CHECK(!web888DecoderWav(*routed, &error) && !error.isEmpty());
        // Bytes already handed out cannot revoke themselves. A consumer must
        // retain the associated job and check current() before external use.
        CHECK(bytes->size() == 44 + count * 2);
    }
    std::puts("Decoder WAV headers, duration, quantization, clipping and refusal checks passed");
}
