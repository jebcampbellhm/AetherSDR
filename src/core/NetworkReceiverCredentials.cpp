#include "NetworkReceiverCredentials.h"
#include "KiwiSdrCredentialStore.h"
#include "SettingsCredentialPolicy.h"

#include <QCryptographicHash>
#include <QHash>
#include <QTimer>
#include <QUrl>

namespace AetherSDR {
namespace {
    QHash<QString, QString> passwords;
    QString key(const QString& endpoint)
    {
        return QString::fromLatin1(SettingsCredentialPolicy::kWeb888PasswordKeyPrefix)
            + QString::fromLatin1(
                QCryptographicHash::hash(endpoint.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
}

std::shared_ptr<IKiwiSdrCredentialStore>& NetworkReceiverCredentials::credentialStore()
{
    static auto value = createDefaultKiwiSdrCredentialStore();
    return value;
}

QString NetworkReceiverCredentials::httpEndpoint(const QString& host, quint16 port)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(host);
    url.setPort(port);
    return url.toString(QUrl::FullyEncoded);
}

bool NetworkReceiverCredentials::persistentStoreAvailable()
{
    return credentialStore()->isPersistent();
}

void NetworkReceiverCredentials::setSessionPassword(
    const QString& endpoint, const QString& password)
{
    passwords.insert(endpoint, password);
}

void NetworkReceiverCredentials::load(const QString& endpoint, QObject* context,
    std::function<void(const QString&, const QString&)> callback)
{
    if (passwords.contains(endpoint)) {
        QTimer::singleShot(0, context, [callback, endpoint] { callback(passwords.value(endpoint), { }); });
        return;
    }
    credentialStore()->read(
        key(endpoint), context, [endpoint, callback](const KiwiSdrCredentialResult& result) {
            if (passwords.contains(endpoint)) {
                callback(passwords.value(endpoint), { });
                return;
            }
            if (result.code == KiwiSdrCredentialResultCode::Error) {
                callback({ }, result.error);
                return;
            }
            // An interactive edit made while this read was pending wins. The
            // caller separately fences its own obsolete connection completion.
            passwords.insert(endpoint, result.value);
            callback(result.value, { });
        });
}

void NetworkReceiverCredentials::saveAccepted(const QString& endpoint, const QString& password,
    QObject* context, std::function<void(const QString&)> callback)
{
    // A replacement or forget action wins over an older authentication reply.
    if (passwords.contains(endpoint) && passwords.value(endpoint) != password) {
        callback({ });
        return;
    }
    if (!credentialStore()->isPersistent()) {
        callback(QStringLiteral(
            "Password is available for this session only; this build has no keychain support."));
        return;
    }
    credentialStore()->write(key(endpoint), password, context,
        [callback](const KiwiSdrCredentialResult& result) { callback(result.error); });
}

void NetworkReceiverCredentials::forget(const QString& endpoint, QObject* context,
    std::function<void(const QString&)> callback)
{
    // Keep an empty session entry so an outstanding read cannot revive it.
    passwords.insert(endpoint, { });
    credentialStore()->remove(key(endpoint), context,
        [callback](const KiwiSdrCredentialResult& result) { callback(result.error); });
}

} // namespace AetherSDR
