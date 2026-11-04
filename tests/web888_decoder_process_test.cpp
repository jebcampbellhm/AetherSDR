#include "core/backends/web888/Web888DecoderProcess.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (false)

static bool waitFor(const std::function<bool()>& predicate, int milliseconds = 5000)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    // This program is also its own controlled child: testing our process
    // supervisor, never a substitute radio or an external decoder algorithm.
    if (args.contains("-8") || args.contains("-5") || args.contains("-H")) {
        const QByteArray behavior = qgetenv("WEB888_TEST_DECODER_BEHAVIOR");
        QFile wav(args.last());
        if (!wav.open(QIODevice::ReadOnly) || wav.read(4) != "RIFF") return 2;
        if (behavior == "hang") { QThread::sleep(10); return 0; }
        if (behavior == "fail") return 3;
        if (behavior == "flood") {
            const QByteArray large(100000, 'x');
            std::fwrite(large.data(), 1, size_t(large.size()), stdout);
            std::fflush(stdout);
            return 0;
        }
        std::puts(qPrintable(args.last()));
        std::puts("fixture completed");
        return 0;
    }

    Web888DecoderProcess decoder;
    int finished = 0;
    QByteArray output;
    QString failure;
    QObject::connect(&decoder, &Web888DecoderProcess::completed, &app,
        [&](const Web888DecoderJob&, const QByteArray& text, const QString& error) {
            ++finished; output = text; failure = error;
        });
    Web888DecoderJob job;
    job.mode = "FT8";
    job.period.audio.dialHz = 14074000;
    job.period.audio.lifetime = std::make_shared<std::atomic<bool>>(true);
    auto route = std::make_shared<std::atomic<bool>>(true);
    job.routeLifetime = route;
    job.period.audio.mono.fill(0.25f, 180000);
    const QString executable = QCoreApplication::applicationFilePath();

    if (args.size() == 7 && args[1] == "--real") {
        // Explicit opt-in only. Canonical PCM16 mono/12k fixture WAVs are
        // prepared separately with recorded provenance, never downloaded here.
        for (int n = 0; n < 3; ++n) {
            const QString mode = n == 0 ? "FT8" : n == 1 ? "FT4" : "WSPR";
            const int samples = n == 0 ? 180000 : n == 1 ? 90000 : 1440000;
            QFile wav(args[4 + n]); CHECK(wav.open(QIODevice::ReadOnly));
            const QByteArray bytes = wav.readAll();
            CHECK(bytes.size() == 44 + samples * 2 && bytes.mid(0, 4) == "RIFF");
            CHECK(qFromLittleEndian<quint16>(bytes.data() + 22) == 1);
            CHECK(qFromLittleEndian<quint32>(bytes.data() + 24) == 12000);
            CHECK(qFromLittleEndian<quint16>(bytes.data() + 34) == 16);
            Web888DecoderRouter router;
            const double dial = n == 2 ? 10138700 : 14074000;
            CHECK(router.configure({{"fixture", mode, {0, dial}}}, quint64(samples) * sizeof(float) * 2));
            Web888DecoderAudio audio;
            audio.dialHz = dial; audio.sliceId = 0;
            audio.lifetime = job.period.audio.lifetime;
            CHECK(router.arm(audio, 0));
            for (int start = 0; start < samples;) {
                const int count = std::min(997, samples - start);
                audio.firstSampleIndex = quint64(start); audio.mono.resize(count);
                for (int k = 0; k < count; ++k)
                    audio.mono[k] = qFromLittleEndian<qint16>(bytes.data() + 44 + (start + k) * 2) / 32768.0f;
                CHECK(router.push(audio)); start += count;
                audio.discontinuity = false;
            }
            auto routed = router.takeNext(); CHECK(routed && routed->current());
            const int before = finished;
            CHECK(decoder.start(*routed, n == 2 ? args[3] : args[2], 30000));
            CHECK(waitFor([&] { return finished == before + 1; }, 35000));
            CHECK(failure.isEmpty());
            std::printf("%s: %s\n", qPrintable(mode), output.constData());
            CHECK(output.contains(n == 0 ? "CQ K1ABC FN42" : n == 2 ? "K1ABC FN42 37" : "CQ RU AB5XS EM12"));
        }
        return 0;
    }
    CHECK(args.size() == 1);
    CHECK(!decoder.start(job, "relative.exe"));
    CHECK(!decoder.start(job, executable, 0));
    route->store(false); CHECK(!decoder.start(job, executable)); route->store(true);
    for (const QByteArray behavior : {QByteArray("ok"), QByteArray("fail"), QByteArray("flood"), QByteArray("hang")}) {
        qputenv("WEB888_TEST_DECODER_BEHAVIOR", behavior);
        const int before = finished;
        CHECK(decoder.start(job, executable, behavior == "hang" ? 200 : 5000));
        CHECK(!decoder.start(job, executable)); // bounded: no hidden queue
        CHECK(waitFor([&] { return finished == before + 1; }));
        CHECK(!decoder.busy());
        if (behavior == "ok") {
            CHECK(failure.isEmpty() && output.contains("fixture completed"));
            const QString path = QString::fromUtf8(output.split('\n').first()).trimmed();
            CHECK(!QFileInfo::exists(path) && !QFileInfo::exists(QFileInfo(path).absolutePath()));
        } else {
            CHECK(!failure.isEmpty() && output.isEmpty());
            if (behavior == "flood") CHECK(failure.contains("output limit"));
            if (behavior == "hang") CHECK(failure.contains("deadline"));
        }
    }
    for (const bool revoke : {false, true}) {
        qputenv("WEB888_TEST_DECODER_BEHAVIOR", "hang");
        const int before = finished;
        CHECK(decoder.start(job, executable));
        if (revoke) route->store(false); else decoder.cancel();
        CHECK(waitFor([&] { return finished == before + 1; }));
        CHECK(!failure.isEmpty() && output.isEmpty() && !decoder.busy());
        route->store(true);
    }
    // An existing but invalid executable reaches QProcess's failed-start path.
    QTemporaryDir badToolDirectory;
    CHECK(badToolDirectory.isValid());
    QFile badTool(badToolDirectory.filePath("decoder.exe"));
    CHECK(badTool.open(QIODevice::WriteOnly));
    CHECK(badTool.write("not an executable") > 0); badTool.close();
    CHECK(badTool.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    int before = finished;
    CHECK(decoder.start(job, badTool.fileName()));
    CHECK(waitFor([&] { return finished == before + 1; }));
    CHECK(!failure.isEmpty() && output.isEmpty() && !decoder.busy());
    // Completion may immediately dispatch the next ready route.
    qputenv("WEB888_TEST_DECODER_BEHAVIOR", "ok");
    bool resubmitted = false;
    const auto reentrant = QObject::connect(&decoder, &Web888DecoderProcess::completed, &app,
        [&](const Web888DecoderJob&, const QByteArray&, const QString&) {
            if (!resubmitted) { resubmitted = true; decoder.start(job, executable); }
        });
    before = finished;
    CHECK(decoder.start(job, executable));
    CHECK(waitFor([&] { return finished == before + 2; }));
    CHECK(failure.isEmpty() && !decoder.busy());
    QObject::disconnect(reentrant);
    qunsetenv("WEB888_TEST_DECODER_BEHAVIOR");
    std::puts("Decoder process success, bounds, timeout, cancellation and cleanup passed");
    return 0;
}
