#pragma once

#include "NrspProtocol.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace AetherSDR::nrsp {

constexpr qsizetype kMaxIqBytes = 1048576;
constexpr qsizetype kMaxDisplayBins = 16384;

struct ReceiverData {
    quint32 receiverId = 0;
    QByteArray bytes;
};

// IQ bytes are interleaved signed-16 little-endian I,Q. Display bytes are
// unsigned 8-bit levels, low RF frequency first; neither is calibrated dBm.
std::optional<ReceiverData> decodeReceiverData(const Frame& frame);

} // namespace AetherSDR::nrsp
