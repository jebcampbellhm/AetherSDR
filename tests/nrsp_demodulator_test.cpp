#include "core/backends/nrsp/NrspDemodulator.h"

#include <QCoreApplication>
#include <QtEndian>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>

using AetherSDR::nrsp::NrspDemodulator;

namespace {
bool check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "nrsp_demodulator_test: %s\n", message); }
    return value;
}

QByteArray tone(int samples, double hz, double amplitude = 0.05)
{
    QByteArray iq(samples * 4, Qt::Uninitialized);
    for (int n = 0; n < samples; ++n) {
        const double phase = 2 * std::numbers::pi * hz * n / NrspDemodulator::kInputRate;
        qToLittleEndian(qint16(std::lround(32767 * amplitude * std::cos(phase))), iq.data() + n * 4);
        qToLittleEndian(qint16(std::lround(32767 * amplitude * std::sin(phase))), iq.data() + n * 4 + 2);
    }
    return iq;
}

QByteArray modulatedTone(int samples, bool fm)
{
    QByteArray iq(samples * 4, Qt::Uninitialized);
    for (int n = 0; n < samples; ++n) {
        const double audioPhase = 2 * std::numbers::pi * 1000 * n / NrspDemodulator::kInputRate;
        const double phase = fm ? 2.5 * std::sin(audioPhase) : 0;
        const double amplitude = fm ? 0.1 : 0.1 * (1 + 0.5 * std::cos(audioPhase));
        qToLittleEndian(qint16(std::lround(32767 * amplitude * std::cos(phase))), iq.data() + n * 4);
        qToLittleEndian(qint16(std::lround(32767 * amplitude * std::sin(phase))), iq.data() + n * 4 + 2);
    }
    return iq;
}

double toneFraction(const QByteArray& stereo, double hz)
{
    const qsizetype start = stereo.size() / 2 / 8 * 8;
    double real = 0;
    double imaginary = 0;
    double total = 0;
    int count = 0;
    for (qsizetype n = start; n + 8 <= stereo.size(); n += 8) {
        float sample = 0;
        std::memcpy(&sample, stereo.constData() + n, sizeof(sample));
        const double phase = 2 * std::numbers::pi * hz * count / NrspDemodulator::kOutputRate;
        real += sample * std::cos(phase);
        imaginary += sample * std::sin(phase);
        total += sample * sample;
        ++count;
    }
    return count && total > 0 ? 2 * (real * real + imaginary * imaginary) / (count * total) : 0;
}

double energy(const QByteArray& stereo)
{
    const qsizetype start = stereo.size() / 2 / 8 * 8;
    double sum = 0;
    int count = 0;
    for (qsizetype n = start; n + 8 <= stereo.size(); n += 8) {
        float sample = 0;
        std::memcpy(&sample, stereo.constData() + n, sizeof(sample));
        if (!std::isfinite(sample)) { return -1; }
        sum += sample * sample;
        ++count;
    }
    return count ? sum / count : -1;
}

std::optional<QByteArray> demodulate(const QString& mode, int low, int high,
                                   const QByteArray& iq, bool fragment = false)
{
    NrspDemodulator demod;
    QString error;
    if (!demod.open(mode, low, high, error) || !demod.setAgc("off", 0)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return std::nullopt;
    }
    QByteArray audio;
    for (qsizetype offset = 0; offset < iq.size();) {
        const qsizetype size = qMin<qsizetype>(fragment ? 632 * 4 : 1024 * 4, iq.size() - offset);
        const std::optional<QByteArray> block = demod.process(QByteArrayView(iq).sliced(offset, size), error);
        if (!block) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return std::nullopt;
        }
        audio += *block;
        offset += size;
    }
    return audio;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    constexpr int samples = 48000;
    const QByteArray positive = tone(samples, 1000);
    const QByteArray negative = tone(samples, -1000);
    const std::optional<QByteArray> usb = demodulate("USB", 300, 2700, positive);
    const std::optional<QByteArray> rejection = demodulate("USB", 300, 2700, negative);
    const std::optional<QByteArray> lsb = demodulate("LSB", -2700, -300, negative);
    const std::optional<QByteArray> blocked = demodulate("USB", 1500, 2700, positive);
    const std::optional<QByteArray> fragmented = demodulate("USB", 300, 2700, positive, true);
    if (!check(usb && rejection && lsb && blocked && fragmented, "DSP output available")) { return 1; }
    const double passed = energy(*usb);
    std::printf("USB power=%g opposite=%g LSB=%g filtered=%g fragmented=%g frames=%lld\n",
                passed, energy(*rejection), energy(*lsb), energy(*blocked), energy(*fragmented),
                static_cast<long long>(usb->size() / 8));
    if (!check(passed > 1e-7 && energy(*lsb) > 1e-7
               && energy(*rejection) < passed * 0.001 && energy(*blocked) < passed * 0.001,
               "sideband and passband selectivity must exceed 30 dB")) { return 1; }
    if (!check(usb->size() > 20000 * 8 && usb->size() <= 24000 * 8
               && fragmented->size() == usb->size()
               && std::abs(energy(*fragmented) / passed - 1) < 0.02,
               "24k audio rate independent of wire frame fragmentation")) { return 1; }
    const std::optional<QByteArray> cw = demodulate("CW", -250, 250, tone(samples, 0));
    if (!check(cw && energy(*cw) > 1e-7 && toneFraction(*cw, 600) > 0.95,
               "CW BFO makes tuned carrier audible at the requested pitch")) { return 1; }
    const std::optional<QByteArray> am = demodulate("AM", -5000, 5000, modulatedTone(samples * 2, false));
    const std::optional<QByteArray> sam = demodulate("SAM", -5000, 5000, modulatedTone(samples * 2, false));
    const std::optional<QByteArray> fm = demodulate("FM", -8000, 8000, modulatedTone(samples * 2, true));
    if (!check(am && sam && fm, "AM, SAM and FM produce audio")) { return 1; }
    std::printf("AM tone=%g SAM tone=%g FM tone=%g\n",
                toneFraction(*am, 1000), toneFraction(*sam, 1000), toneFraction(*fm, 1000));
    if (!check(energy(*am) > 1e-7 && energy(*sam) > 1e-7 && energy(*fm) > 1e-7
               && toneFraction(*am, 1000) > 0.95 && toneFraction(*sam, 1000) > 0.95
               && toneFraction(*fm, 1000) > 0.95,
               "AM, SAM and FM recover the modulating tone without excessive distortion")) { return 1; }
    NrspDemodulator demod;
    QString error;
    if (!check(!demod.open("INVALID", 300, 2700, error)
               && !NrspDemodulator::validPassband(std::numeric_limits<int>::max(), std::numeric_limits<int>::min())
               && !demod.open("CW", 21000, 22000, error)
               && demod.open("USB", 300, 2700, error)
               && !demod.process(QByteArray(3, 'x'), error), "mode and input boundaries")) { return 1; }
    return 0;
}
