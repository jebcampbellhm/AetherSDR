#include "NrspAuthentication.h"
#include "NrspProtocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QtGlobal>

#include <cstring>
#include <memory>
#include <type_traits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#else
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#endif

namespace AetherSDR::nrsp {
std::optional<QByteArray> makeLoginResponse(QByteArrayView helloNonce,
                                          const QString& username, const QString& password)
{
    QByteArray user = username.toUtf8();
    if (user.isEmpty() || user.size() > 16 || user.contains('\0') || password.isEmpty()
        || password.size() > 1024 || helloNonce.size() != 64) {
        return std::nullopt;
    }
    for (const char c : helloNonce) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return std::nullopt;
        }
    }
    QByteArray digest = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex();
    QByteArray input(helloNonce.data(), helloNonce.size());
    input += digest;
    const QByteArray response = QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex();
    digest.fill('\0');
    input.fill('\0');
    user.resize(16, '\0');
    return user + response;
}

namespace {

QByteArray publicModulus()
{
    // Public verification key, exponent 65537; no private key is held here.
    // Capture-backed signature fixture and provenance: docs/nrsp-st-protocol.md.
    return QByteArray::fromBase64(
        "32J2nFB/zXLk7RftBcVDT0W0lqMtlXxrU44cEoOHvY3c/7BjtxPuuAASU/itOhFB/BBB"
        "1gZypS/LnGcc9D/1aB+yxu3OpWYDhzpr6Bjwuk3jzdTRCwTIWyps8L1HO2mUOFExaymG"
        "OM2F0kGaSxaeL85Kx/IE9hFMDjQX5CL/l3V32G6Y/ALtVdxY/WgXBU1zw4snTH+IfaB/"
        "M+o3dYN2fUfbyYIAJRFR70f/n+KGtNX60TZ2bwk9FB2KfooV3XGrhHZRJrEhHL7Lz7Mcn"
        "30chdvdwQ20/KJxIOX9j/PnYrQ6qZ9LeySTGaDI4EF6XD+0HU9jzCmFvyX4VuW67F8QzQ==");
}

#ifdef Q_OS_WIN
struct RsaProvider {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~RsaProvider()
    {
        if (handle) {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }
};
struct RsaKey {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~RsaKey()
    {
        if (handle) {
            BCryptDestroyKey(handle);
        }
    }
};
#endif

bool verifySignature(QByteArrayView nonce, QByteArray signature)
{
#ifdef Q_OS_WIN
    RsaProvider provider;
    if (BCryptOpenAlgorithmProvider(&provider.handle, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }
    const BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC, 2048, 3, 256, 0, 0};
    QByteArray blob(reinterpret_cast<const char*>(&header), sizeof(header));
    blob += QByteArray::fromHex("010001");
    blob += publicModulus();
    RsaKey key;
    if (BCryptImportKeyPair(provider.handle, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.handle,
                           reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0) < 0) {
        return false;
    }
    QByteArray digest = QCryptographicHash::hash(nonce, QCryptographicHash::Sha256);
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    return BCryptVerifySignature(key.handle, &padding,
               reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()),
               reinterpret_cast<PUCHAR>(signature.data()), ULONG(signature.size()), BCRYPT_PAD_PKCS1) == 0;
#elif defined(Q_OS_MACOS)
    // SecKey imports RSA public keys in PKCS#1 format. Keep the system
    // provider so the macOS deployment floor does not depend on Homebrew.
    const QByteArray der = QByteArray::fromHex("3082010a0282010100")
        + publicModulus() + QByteArray::fromHex("0203010001");
    const auto release = [](CFTypeRef object) { if (object) { CFRelease(object); } };
    const std::unique_ptr<std::remove_pointer_t<CFDataRef>, decltype(release)> keyData(
        CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(der.constData()), der.size()), release);
    const void* keys[]{kSecAttrKeyType, kSecAttrKeyClass};
    const void* values[]{kSecAttrKeyTypeRSA, kSecAttrKeyClassPublic};
    const std::unique_ptr<std::remove_pointer_t<CFDictionaryRef>, decltype(release)> attributes(
        CFDictionaryCreate(nullptr, keys, values, 2,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks), release);
    if (!keyData || !attributes) { return false; }
    const std::unique_ptr<std::remove_pointer_t<SecKeyRef>, decltype(release)> key(
        SecKeyCreateWithData(keyData.get(), attributes.get(), nullptr), release);
    const std::unique_ptr<std::remove_pointer_t<CFDataRef>, decltype(release)> message(
        CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(nonce.data()), nonce.size()), release);
    const std::unique_ptr<std::remove_pointer_t<CFDataRef>, decltype(release)> signedData(
        CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(signature.constData()), signature.size()), release);
    return key && message && signedData && SecKeyVerifySignature(
        key.get(), kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256,
        message.get(), signedData.get(), nullptr);
#else
    // Fixed RSA SubjectPublicKeyInfo envelope around the unsigned modulus.
    const QByteArray der = QByteArray::fromHex(
        "30820122300d06092a864886f70d01010105000382010f003082010a0282010100")
        + publicModulus() + QByteArray::fromHex("0203010001");
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(der.constData());
    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        d2i_PUBKEY(nullptr, &cursor, long(der.size())), EVP_PKEY_free);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
        EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* keyContext = nullptr;
    if (!key || !context
        || EVP_DigestVerifyInit(context.get(), &keyContext, EVP_sha256(), nullptr, key.get()) != 1
        || EVP_PKEY_CTX_set_rsa_padding(keyContext, RSA_PKCS1_PADDING) <= 0) {
        return false;
    }
    return EVP_DigestVerify(context.get(),
               reinterpret_cast<const unsigned char*>(signature.constData()), signature.size(),
               reinterpret_cast<const unsigned char*>(nonce.data()), nonce.size()) == 1;
#endif
}

} // namespace

QByteArray makeChallenge()
{
    QByteArray challenge = u64(static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()));
    challenge.resize(264);
    for (qsizetype offset = 8; offset < challenge.size(); offset += 4) {
        const quint32 random = QRandomGenerator::system()->generate();
        std::memcpy(challenge.data() + offset, &random, sizeof(random));
    }
    return challenge;
}

bool verifyChallengeResponse(QByteArrayView challenge, QByteArrayView response)
{
    if (challenge.size() != 264 || response.size() <= 8 || response.size() > 4096
        || response.first(8) != challenge.first(8)) {
        return false;
    }
    QByteArray encoded;
    encoded.reserve(response.size() - 8);
    for (const char ch : response.sliced(8)) {
        if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t') {
            encoded.append(ch);
        }
    }
    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != 256) {
        return false;
    }
    return verifySignature(challenge.sliced(8), decoded.decoded);
}

} // namespace AetherSDR::nrsp
