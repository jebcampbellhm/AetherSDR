#include "core/backends/nrsp/NrspProtocol.h"
#include "core/backends/nrsp/NrspAuthentication.h"
#include "core/backends/nrsp/NrspData.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cstdio>
#include <limits>

using namespace AetherSDR::nrsp;

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "nrsp_protocol_test: %s\n", message);
    }
    return condition;
}

SessionKey sessionFrom(const QJsonObject& object)
{
    SessionKey key;
    const QJsonArray words = object.value("session_key_words_hex").toArray();
    for (qsizetype i = 0; i < 4; ++i) {
        key.words[static_cast<std::size_t>(i)] = words.at(i).toString().toUInt(nullptr, 16);
    }
    key.mask = object.value("session_mask_hex").toString().toUInt(nullptr, 16);
    return key;
}

QByteArray bytes(const QJsonObject& object, const char* name)
{
    return QByteArray::fromHex(object.value(QLatin1String(name)).toString().toLatin1());
}

bool capturedFrames(const QJsonObject& fixture)
{
    const QJsonObject sessions = fixture.value("sessions").toObject();
    int count = 0;
    for (const QJsonValue& value : fixture.value("examples").toArray()) {
        const QJsonObject example = value.toObject();
        const QJsonObject session = sessions.value(example.value("session").toString()).toObject();
        const QByteArray wire = bytes(example, "wire_hex");
        const QByteArray expected = bytes(example, "expected_payload_hex");
        const QByteArray hash = QCryptographicHash::hash(wire, QCryptographicHash::Sha256).toHex();
        if (!check(hash == example.value("wire_sha256").toString().toLatin1(), "fixture checksum")) {
            return false;
        }
        FrameDecoder decoder;
        if (example.value("expected_type").toInt() != 201) {
            decoder.setSession(sessionFrom(session));
        }
        Frame frame;
        // Fragment the header and body independently of capture segmentation.
        const qsizetype split = qMin<qsizetype>(wire.size(), 3);
        if (!check(decoder.append(QByteArrayView(wire).first(split))
                   && decoder.next(frame) == FrameDecoder::Result::NeedMore,
                   "partial header must wait")) {
            return false;
        }
        if (!check(decoder.append(QByteArrayView(wire).sliced(split))
                   && decoder.next(frame) == FrameDecoder::Result::Ready
                   && frame.type == example.value("expected_type").toInt()
                   && frame.payload == expected && decoder.bufferedBytes() == 0,
                   qPrintable(example.value("id").toString()))) {
            return false;
        }
        if (example.value("direction").toString() == "client_to_radio") {
            const std::optional<QByteArray> encoded = encodeFrame(frame.type, expected, sessionFrom(session));
            // SDRconnect captures contain nonzero ignored padding. Direct
            // client captures use canonical zero padding and pin exact bytes.
            if (!check(encoded && (example.value("session").toString() != "direct" || *encoded == wire),
                       "canonical outgoing command must match original capture")) {
                return false;
            }
            FrameDecoder encodedDecoder;
            encodedDecoder.setSession(sessionFrom(session));
            Frame encodedFrame;
            if (!check(encodedDecoder.append(*encoded)
                       && encodedDecoder.next(encodedFrame) == FrameDecoder::Result::Ready
                       && encodedFrame.type == frame.type && encodedFrame.payload == expected,
                       "outgoing payload must match decoded capture")) {
                return false;
            }
        }
        if (frame.type == 212 || frame.type == 216 || frame.type == 217) {
            const std::optional<ReceiverData> data = decodeReceiverData(frame);
            const QJsonObject known = example.value("decoded_data").toObject();
            if (!check(data && data->bytes.size() == known.value("bytes").toInt()
                       && QCryptographicHash::hash(data->bytes, QCryptographicHash::Sha256).toHex()
                           == known.value("sha256").toString().toLatin1(),
                       "captured decompressed IQ/display checksum")) {
                return false;
            }
            if (frame.type == 212 || frame.type == 217) {
                Frame malformed = frame;
                malformed.payload.append('x');
                if (!check(!decodeReceiverData(malformed), "trailing compressed data refused")) {
                    return false;
                }
                malformed = frame;
                malformed.payload += frame.payload.mid(4);
                if (!check(!decodeReceiverData(malformed), "concatenated compressed frames refused")) {
                    return false;
                }
                malformed = frame;
                malformed.payload.chop(1);
                if (!check(!decodeReceiverData(malformed), "truncated compressed data refused")) {
                    return false;
                }
            }
        }
        ++count;
    }
    return check(count == 48, "expected all 48 captured frames");
}

bool helloTransition(const QJsonObject& fixture)
{
    const QJsonObject direct = fixture.value("sessions").toObject().value("direct").toObject();
    const QByteArray helloWire = bytes(direct, "hello_wire_hex");
    const QJsonObject command = fixture.value("examples").toArray().at(1).toObject();
    const QByteArray nextWire = bytes(command, "wire_hex");
    // All split points include every header and XTEA-block boundary. Also
    // retain the second frame across the bootstrap -> session-key switch.
    for (qsizetype split = 0; split < helloWire.size(); ++split) {
        FrameDecoder decoder;
        Frame frame;
        if (!check(decoder.append(QByteArrayView(helloWire).first(split))
                   && decoder.next(frame) == FrameDecoder::Result::NeedMore,
                   "partial hello must wait")) {
            return false;
        }
        if (!check(decoder.append(helloWire.mid(split) + nextWire)
                   && decoder.next(frame) == FrameDecoder::Result::Ready && frame.type == 201
                   && frame.payload == bytes(direct, "hello_decoded_hex"), "captured hello")) {
            return false;
        }
        const std::optional<Hello> hello = parseHello(frame.payload);
        if (!check(hello && hello->session == sessionFrom(direct)
                   && hello->source.frequencyHz == 910550000ull
                   && hello->source.sampleRate == 2000000 && hello->source.owner
                   && hello->displayCadence == 12, "hello source and session fields")) {
            return false;
        }
        decoder.setSession(hello->session);
        if (!check(decoder.next(frame) == FrameDecoder::Result::Ready
                   && frame.type == 54 && frame.payload == bytes(command, "expected_payload_hex")
                   && decoder.next(frame) == FrameDecoder::Result::NeedMore,
                   "coalesced session frame after hello")) {
            return false;
        }
    }
    return true;
}

bool authentication(const QJsonObject& fixture)
{
    QByteArray challenge;
    QByteArray response;
    for (const QJsonValue& value : fixture.value("examples").toArray()) {
        const QJsonObject example = value.toObject();
        if (example.value("session").toString() == "direct") {
            if (example.value("expected_type").toInt() == 51) {
                challenge = bytes(example, "expected_payload_hex");
            } else if (example.value("expected_type").toInt() == 52) {
                response = bytes(example, "expected_payload_hex");
            }
        }
    }
    if (!check(verifyChallengeResponse(challenge, response), "captured RSA response must verify")) {
        return false;
    }
    QByteArray altered = challenge;
    altered[8] ^= 1;
    if (!check(!verifyChallengeResponse(altered, response), "signature must bind nonce")) {
        return false;
    }
    altered = response;
    altered[0] ^= 1;
    if (!check(!verifyChallengeResponse(challenge, altered), "response must echo timestamp")) {
        return false;
    }
    altered = response;
    altered[12] = '!';
    if (!check(!verifyChallengeResponse(challenge, altered), "invalid base64 refused")) {
        return false;
    }
    altered = response;
    const QByteArray signature = QByteArray::fromBase64(response.mid(8));
    QByteArray changedSignature = signature;
    changedSignature[100] ^= 1;
    altered = response.first(8) + changedSignature.toBase64();
    if (!check(!verifyChallengeResponse(challenge, altered), "forged RSA signature refused")) {
        return false;
    }
    if (!check(!verifyChallengeResponse(challenge.first(263), response)
               && !verifyChallengeResponse(challenge, response.first(8))
               && !verifyChallengeResponse(challenge, QByteArray(4097, 'x')),
               "authentication length boundaries")) {
        return false;
    }
    const QByteArray fresh = makeChallenge();
    const QByteArray another = makeChallenge();
    return check(fresh.size() == 264 && another.size() == 264 && fresh.mid(8) != another.mid(8)
                 && !verifyChallengeResponse(fresh, response), "fresh challenge rejects captured reply");
}

bool malformedInput()
{
    // Independently generated with Python 3.14 compression.zstd: all-zero
    // outputs of 1 MiB + 4 IQ bytes and 16385 display bins respectively.
    const Frame oversizedIq{217, u32(1) + QByteArray::fromHex(
        "28b52ffda0040010005400001000000100fbff39c002020010000200100002001000020010"
        "0002001000020010000200100021000000000000")};
    const Frame oversizedDisplay{212, u32(1) + QByteArray::fromHex(
        "28b52ffd60013f4d00001000000100fc9f0758")};
    if (!check(!decodeReceiverData(oversizedIq) && !decodeReceiverData(oversizedDisplay)
               && !decodeReceiverData(Frame{216, u32(1) + QByteArray(3, 'x')})
               && !decodeReceiverData(Frame{212, u32(1)}), "decoded output and IQ alignment limits")) {
        return false;
    }
    FrameDecoder decoder;
    Frame frame;
    for (const quint32 invalid : {0x01000001u, 0x01000010u, 0x01fffff0u}) {
        decoder.reset();
        if (!check(decoder.append(u32(invalid ^ SessionKey{}.mask))
                   && decoder.next(frame) == FrameDecoder::Result::Invalid
                   && !decoder.error().isEmpty() && !decoder.append("x")
                   && decoder.next(frame) == FrameDecoder::Result::Invalid,
                   "malformed header must latch error")) {
            return false;
        }
    }
    decoder.reset();
    const QByteArray oversized(kMaxBuffered + 1, 'x');
    if (!check(!decoder.append(oversized) && decoder.bufferedBytes() == 0,
               "receive buffer allocation cap")) {
        return false;
    }
    decoder.reset();
    const QByteArray payload(kMaxPayload, '\x7f');
    const std::optional<QByteArray> maxFrame = encodeFrame(203, payload, SessionKey{});
    if (!check(maxFrame && decoder.append(*maxFrame)
               && decoder.next(frame) == FrameDecoder::Result::Ready && frame.payload == payload
               && maxFrame->mid(4) == payload, "whole IQ bypass and largest legal payload")) {
        return false;
    }
    if (!check(!encodeFrame(51, QByteArray(kMaxPayload + 1, 'x'), SessionKey{}), "oversized encode")) {
        return false;
    }
    const QByteArray integer = QByteArray::fromHex("5566778811223344");
    if (!check(u64(0x1122334455667788ull) == integer
               && readU64(integer) == 0x1122334455667788ull
               && !readU64(integer, 1) && !readU32(integer, -1)
               && !readU64(integer, std::numeric_limits<qsizetype>::max()), "integer bounds and word order")) {
        return false;
    }
    for (int size = 0; size < 172; ++size) {
        if (!check(!parseHello(QByteArray(size, '\0')), "truncated hello")) {
            return false;
        }
    }
    return check(!parseSourceState(QByteArray(64, '\0')), "invalid source sample rate");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        return 1;
    }
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) {
        return 1;
    }
    QJsonParseError error;
    const QJsonDocument fixture = QJsonDocument::fromJson(input.readAll(), &error);
    if (!check(error.error == QJsonParseError::NoError && fixture.isObject(), "fixture JSON")) {
        return 1;
    }
    return capturedFrames(fixture.object()) && helloTransition(fixture.object())
        && authentication(fixture.object()) && malformedInput() ? 0 : 1;
}
