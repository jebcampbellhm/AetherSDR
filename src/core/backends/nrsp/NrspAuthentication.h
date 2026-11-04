#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace AetherSDR::nrsp {

// Type 51: current Unix milliseconds in word-swapped order and 256 fresh bytes.
QByteArray makeChallenge();
// Type 52: echoed timestamp and base64 RSA-2048/SHA-256 signature of the nonce.
bool verifyChallengeResponse(QByteArrayView challenge, QByteArrayView response);

// Type 58: 16-byte zero-padded UTF-8 username, then the 64-byte ASCII hex
// SHA-256 of helloNonce + hex(SHA-256(password UTF-8)). No reusable hash is kept.
std::optional<QByteArray> makeLoginResponse(QByteArrayView helloNonce,
                                          const QString& username, const QString& password);

} // namespace AetherSDR::nrsp
