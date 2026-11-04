#pragma once

#include <QString>
#include <functional>
#include <memory>

class QObject;

namespace AetherSDR {
class IKiwiSdrCredentialStore;

// Endpoint-bound credentials for standalone network receive sessions. Passwords
// never enter RadioInfo, persisted route documents, or diagnostic snapshots.
class NetworkReceiverCredentials {
public:
    // Use one canonical URL for both the connection and its credential key.
    static QString httpEndpoint(const QString& host, quint16 port);
    static bool persistentStoreAvailable();
    static void setSessionPassword(const QString& endpoint, const QString& password);
    static void load(const QString& endpoint, QObject* context,
        std::function<void(const QString&, const QString&)> callback);
    static void saveAccepted(const QString& endpoint, const QString& password, QObject* context,
        std::function<void(const QString&)> callback);
    static void forget(const QString& endpoint, QObject* context,
        std::function<void(const QString&)> callback);

private:
    friend struct NetworkReceiverCredentialsTestAccess;
    static std::shared_ptr<IKiwiSdrCredentialStore>& credentialStore();
};

} // namespace AetherSDR
