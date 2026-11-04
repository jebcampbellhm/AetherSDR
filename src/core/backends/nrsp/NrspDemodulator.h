#pragma once

#include "core/dsp/WdspChannel.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace AetherSDR::nrsp {

// One IQ Lite receiver, used only on its acquisition worker. Shared WDSP
// owns filtering/demodulation/AGC; this adapter handles wire sample blocks.
class NrspDemodulator final
{
public:
    static constexpr int kInputRate = 48000;
    static constexpr int kOutputRate = 24000;
    static constexpr std::size_t kBlockSize = 1024;
    bool open(const QString& mode, int lowHz, int highHz, QString& error);
    void close();
    bool setModeAndFilter(const QString& mode, int lowHz, int highHz);
    bool setAgc(const QString& mode, int maximumGainDb);
    bool setCwPitch(int pitchHz);
    std::optional<QByteArray> process(QByteArrayView iq, QString& error);
    static bool validPassband(int lowHz, int highHz);
    static std::optional<WdspChannel::Mode> modeFor(const QString& mode);

private:
    std::unique_ptr<WdspChannel> m_channel;
    std::array<float, kBlockSize> m_i{};
    std::array<float, kBlockSize> m_q{};
    std::vector<float> m_left;
    std::vector<float> m_right;
    std::size_t m_pending = 0;
    QString m_mode;
    int m_lowHz = 300;
    int m_highHz = 2700;
    int m_cwPitchHz = 600;
    int m_agcMode = 3;
    int m_agcMaximumDb = 80;
    float m_dcInput[2]{};
    float m_dcOutput[2]{};
};

} // namespace AetherSDR::nrsp
