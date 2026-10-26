#include "NrspData.h"

#include <zstd.h>

namespace AetherSDR::nrsp {
namespace {

std::optional<QByteArray> decompress(QByteArrayView bytes, qsizetype limit)
{
    if (bytes.isEmpty()) {
        return std::nullopt;
    }
    const size_t compressedSize = ZSTD_findFrameCompressedSize(bytes.data(), bytes.size());
    // One ordinary Zstd frame only: no skippable frames, concatenation, or
    // trailing data. Fixed output allocation bounds unknown-content-size input.
    if (bytes.size() < 4 || *readU32(bytes) != 0x28b52ffdu
        || ZSTD_isError(compressedSize) || compressedSize != size_t(bytes.size())) {
        return std::nullopt;
    }
    const unsigned long long contentSize = ZSTD_getFrameContentSize(bytes.data(), bytes.size());
    if (contentSize == ZSTD_CONTENTSIZE_ERROR
        || (contentSize != ZSTD_CONTENTSIZE_UNKNOWN && contentSize > quint64(limit))) {
        return std::nullopt;
    }
    QByteArray output(limit, Qt::Uninitialized);
    const size_t result = ZSTD_decompress(output.data(), size_t(output.size()), bytes.data(), size_t(bytes.size()));
    if (ZSTD_isError(result) || result == 0 || result > size_t(limit)) {
        return std::nullopt;
    }
    output.resize(qsizetype(result));
    return output;
}

} // namespace

std::optional<ReceiverData> decodeReceiverData(const Frame& frame)
{
    if ((frame.type != 212 && frame.type != 216 && frame.type != 217) || frame.payload.size() < 5) {
        return std::nullopt;
    }
    ReceiverData result;
    result.receiverId = *readU32(frame.payload);
    const QByteArrayView body = QByteArrayView(frame.payload).sliced(4);
    if (frame.type == 216) {
        if (body.size() > kMaxIqBytes || body.size() % 4 != 0) {
            return std::nullopt;
        }
        result.bytes = QByteArray(body.data(), body.size());
    } else {
        const std::optional<QByteArray> decoded = decompress(
            body, frame.type == 212 ? kMaxDisplayBins : kMaxIqBytes);
        if (!decoded || (frame.type == 217 && decoded->size() % 4 != 0)) {
            return std::nullopt;
        }
        result.bytes = *decoded;
    }
    return result;
}

} // namespace AetherSDR::nrsp
