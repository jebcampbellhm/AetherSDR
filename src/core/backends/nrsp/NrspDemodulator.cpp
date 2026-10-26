#include "NrspDemodulator.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <numbers>
#include <string>
#include <utility>

namespace AetherSDR::nrsp {

std::optional<WdspChannel::Mode> NrspDemodulator::modeFor(const QString& mode)
{
    if (mode == "USB" || mode == "DIGU") { return WdspChannel::Mode::Usb; }
    if (mode == "LSB" || mode == "DIGL") { return WdspChannel::Mode::Lsb; }
    if (mode == "AM") { return WdspChannel::Mode::Am; }
    if (mode == "SAM") { return WdspChannel::Mode::Sam; }
    if (mode == "FM") { return WdspChannel::Mode::Fm; }
    if (mode == "CW") { return WdspChannel::Mode::Cwu; }
    return std::nullopt;
}

bool NrspDemodulator::validPassband(int lowHz, int highHz)
{
    return lowHz >= -22000 && lowHz <= 22000 && highHz >= -22000
        && highHz <= 22000 && highHz - lowHz >= 50;
}

bool NrspDemodulator::open(const QString& mode, int lowHz, int highHz, QString& error)
{
    const std::optional<WdspChannel::Mode> selected = modeFor(mode);
    if (!selected || !validPassband(lowHz, highHz)) {
        error = QStringLiteral("Unsupported nRSP receive mode or passband.");
        return false;
    }
    WdspChannel::Config config;
    config.inputBlockSize = kBlockSize;
    config.dspBlockSize = kBlockSize;
    config.inputSampleRate = kInputRate;
    config.dspSampleRate = kInputRate;
    config.outputSampleRate = kOutputRate;
    config.mode = *selected;
    const int bfo = mode == "CW" ? m_cwPitchHz : 0;
    if (!validPassband(lowHz + bfo, highHz + bfo)) {
        error = QStringLiteral("nRSP CW passband exceeds the IQ channel bandwidth.");
        return false;
    }
    config.filterLowHz = lowHz + bfo;
    config.filterHighHz = highHz + bfo;
    config.blockForOutput = true;
    config.agcMode = m_agcMode;
    config.maximumAgcGainDb = m_agcMaximumDb;
    config.rxPanelGain = 1.0;
    if (mode == "FM") { config.fmReceive = WdspChannel::FmReceive{}; }
    std::string detail;
    std::unique_ptr<WdspChannel> channel = WdspChannel::create(config, &detail);
    if (!channel) {
        error = QString::fromStdString(detail);
        return false;
    }
    if (!channel->setShift(mode == "CW" ? -m_cwPitchHz : 0)) {
        error = QStringLiteral("Unable to configure nRSP receive oscillator.");
        return false;
    }
    close();
    m_channel = std::move(channel);
    m_mode = mode;
    m_lowHz = lowHz;
    m_highHz = highHz;
    m_left.resize(m_channel->outputBlockSize());
    m_right.resize(m_channel->outputBlockSize());
    return true;
}

void NrspDemodulator::close()
{
    m_channel.reset();
    m_pending = 0;
    std::fill(std::begin(m_dcInput), std::end(m_dcInput), 0.0f);
    std::fill(std::begin(m_dcOutput), std::end(m_dcOutput), 0.0f);
}

bool NrspDemodulator::setModeAndFilter(const QString& mode, int lowHz, int highHz)
{
    const std::optional<WdspChannel::Mode> selected = modeFor(mode);
    if (!m_channel || !selected || !validPassband(lowHz, highHz)) {
        return false;
    }
    if ((m_mode == "FM") != (mode == "FM")) {
        QString error;
        return open(mode, lowHz, highHz, error);
    }
    const int bfo = mode == "CW" ? m_cwPitchHz : 0;
    if (!validPassband(lowHz + bfo, highHz + bfo)) { return false; }
    if (!m_channel->setMode(*selected) || !m_channel->setFilter(lowHz + bfo, highHz + bfo)
        || !m_channel->setShift(mode == "CW" ? -m_cwPitchHz : 0)) {
        const int oldBfo = m_mode == "CW" ? m_cwPitchHz : 0;
        if (!m_channel->setMode(*modeFor(m_mode))
            || !m_channel->setFilter(m_lowHz + oldBfo, m_highHz + oldBfo)
            || !m_channel->setShift(-oldBfo)) {
            close();
        }
        return false;
    }
    m_mode = mode;
    m_lowHz = lowHz;
    m_highHz = highHz;
    return true;
}

bool NrspDemodulator::setAgc(const QString& mode, int maximumGainDb)
{
    const int index = mode == "off" ? 0 : mode == "slow" ? 2 : mode == "med" ? 3 : mode == "fast" ? 4 : -1;
    if (!m_channel || index < 0 || maximumGainDb < 0 || maximumGainDb > 100
        || !m_channel->setAgc(index, maximumGainDb)) { return false; }
    m_agcMode = index;
    m_agcMaximumDb = maximumGainDb;
    return true;
}

bool NrspDemodulator::setCwPitch(int pitchHz)
{
    if (!m_channel || pitchHz < 100 || pitchHz > 6000
        || (m_mode == "CW" && !validPassband(m_lowHz + pitchHz, m_highHz + pitchHz))) {
        return false;
    }
    if (m_mode == "CW") {
        if (!m_channel->setFilter(m_lowHz + pitchHz, m_highHz + pitchHz)
            || !m_channel->setShift(-pitchHz)) {
            if (!m_channel->setFilter(m_lowHz + m_cwPitchHz, m_highHz + m_cwPitchHz)
                || !m_channel->setShift(-m_cwPitchHz)) { close(); }
            return false;
        }
    }
    m_cwPitchHz = pitchHz;
    return true;
}

std::optional<QByteArray> NrspDemodulator::process(QByteArrayView iq, QString& error)
{
    if (!m_channel || iq.size() % 4 != 0 || iq.size() > 1048576) {
        error = QStringLiteral("Invalid IQ block for nRSP demodulation.");
        return std::nullopt;
    }
    QByteArray output;
    output.reserve(qsizetype((iq.size() / 4 + m_pending) / kBlockSize * m_left.size() * 2 * sizeof(float)));
    const float dcPole = float(std::exp(-2.0 * std::numbers::pi * 20.0 / kOutputRate));
    for (qsizetype offset = 0; offset < iq.size(); offset += 4) {
        m_i[m_pending] = qFromLittleEndian<qint16>(iq.data() + offset) / 32768.0f;
        // WDSP's USB detector expects negative rotation for RF above the VFO.
        m_q[m_pending] = -qFromLittleEndian<qint16>(iq.data() + offset + 2) / 32768.0f;
        if (++m_pending != kBlockSize) {
            continue;
        }
        m_pending = 0;
        const WdspChannel::ProcessResult result = m_channel->processIq(m_i, m_q, m_left, m_right);
        if (result == WdspChannel::ProcessResult::Underrun) {
            continue;
        }
        if (result != WdspChannel::ProcessResult::Ok) {
            error = QStringLiteral("nRSP receive DSP failed (%1).").arg(int(result));
            return std::nullopt;
        }
        for (std::size_t sample = 0; sample < m_left.size(); ++sample) {
            float stereo[2]{m_left[sample], m_right[sample]};
            for (int channel = 0; channel < 2; ++channel) {
                if (!std::isfinite(stereo[channel])) {
                    error = QStringLiteral("Non-finite nRSP receive audio.");
                    return std::nullopt;
                }
                const float filtered = stereo[channel] - m_dcInput[channel] + dcPole * m_dcOutput[channel];
                m_dcInput[channel] = stereo[channel];
                m_dcOutput[channel] = std::abs(filtered) > 1e-20f ? filtered : 0.0f;
                stereo[channel] = std::clamp(m_dcOutput[channel], -1.0f, 1.0f);
            }
            output.append(reinterpret_cast<const char*>(stereo), sizeof(stereo));
        }
    }
    return output;
}

} // namespace AetherSDR::nrsp
