#include "TestSettingsProfile.h"
#include "core/backends/RadioBackendFactory.h"
#include "core/backends/IRadioBackend.h"
#include "models/RadioModel.h"

#include <QCoreApplication>
#include <QDebug>

namespace AetherSDR {
class RadioModelWeb888TestAccess {
public:
    static bool retryPending(const RadioModel& model) { return model.m_reconnectTimer.isActive(); }
};
}
using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { qCritical() << "line" << __LINE__ << #c; return 1; } } while (false)

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("web888-connection"));
    if (!profile.isValid()) {
        return 1;
    }
    QCoreApplication app(argc, argv);
    CHECK(RadioCapabilities{}.automaticReconnectAllowed());
    RadioModel model;
    int failures = 0;
    QObject::connect(&model, &RadioModel::connectionError, &app, [&](const QString&) { ++failures; });
#ifdef HAVE_WEBSOCKETS
    CHECK(model.rebuildBackendForTest(QStringLiteral("web888")));
    CHECK(model.backend() && model.backendCapabilities().family == QStringLiteral("web888"));
    CHECK(!model.backendCapabilities().automaticReconnectAllowed());
    RadioInfo info;
    info.family = QStringLiteral("web888");
    info.model = QStringLiteral("Web-888");
    info.address = QHostAddress(QStringLiteral("127.0.0.1"));
    info.port = 8073;
    // Production transport rejects an oversized password before HTTP/socket
    // opening. Exercises UI-options -> RadioModel -> real backend refusal.
    const QVariantMap options{{QStringLiteral("web888.password"), QString(10000, QLatin1Char('x'))}};
    model.connectToRadioWithOptions(info, options);
    CHECK(failures == 1 && !model.isConnected() && !model.isConnectAttemptInFlight());
    CHECK(!RadioModelWeb888TestAccess::retryPending(model));
    model.triggerAutoReconnectForTest();
    CHECK(failures == 1 && !model.isConnectAttemptInFlight());
    // The disconnect path also used to arm retry independently of errors.
    emit model.backend()->disconnected();
    CHECK(!RadioModelWeb888TestAccess::retryPending(model));
    model.connectToRadioWithOptions(info, options);
    CHECK(failures == 2 && !RadioModelWeb888TestAccess::retryPending(model));
    CHECK(model.rebuildBackendForTest(QStringLiteral("sim")));
    CHECK(model.backendCapabilities().automaticReconnectAllowed());
    CHECK(model.rebuildBackendForTest(QStringLiteral("web888")));
    CHECK(!model.backendCapabilities().canTransmit && !model.backendCapabilities().automaticReconnectAllowed());
#else
    CHECK(!createRadioBackend(QStringLiteral("web888")));
    CHECK(!model.rebuildBackendForTest(QStringLiteral("web888")));
    CHECK(!model.backend());
#endif
    qInfo() << "Web-888 factory and manual recovery checks passed";
    return 0;
}
