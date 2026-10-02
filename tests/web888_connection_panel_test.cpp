#include "TestSettingsProfile.h"
#include "gui/ConnectionPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QLineEdit>
#include <QPixmap>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { qCritical() << "line" << __LINE__ << #c; return 1; } } while (false)

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("web888-connection-panel"));
    if (!profile.isValid()) {
        return 1;
    }
    QApplication app(argc, argv);
    ConnectionPanel panel;
    auto* type = panel.findChild<QComboBox*>(QStringLiteral("connectionManualRadioType"));
    auto* password = panel.findChild<QLineEdit*>(QStringLiteral("connectionManualWeb888Password"));
    CHECK(type && password && password->echoMode() == QLineEdit::Password);
    const int index = type->findData(QStringLiteral("web888"));
#ifdef HAVE_WEBSOCKETS
    CHECK(index >= 0);
    type->setCurrentIndex(index);
    password->setText(QStringLiteral("ephemeral-test"));
    int requests = 0;
    int legacyRequests = 0;
    RadioInfo requested;
    QVariantMap received;
    QObject::connect(&panel, &ConnectionPanel::connectWithOptionsRequested, &app,
                     [&](const RadioInfo& info, const QVariantMap& options) {
        ++requests;
        requested = info;
        received = options;
    });
    QObject::connect(&panel, &ConnectionPanel::connectRequested, &app,
                     [&](const RadioInfo&) { ++legacyRequests; });
    QString error;
    CHECK(panel.automationConnectByIp(QStringLiteral("192.0.2.10:8090"), QStringLiteral("web888"), &error));
    CHECK(requests == 1 && legacyRequests == 0 && requested.port == 8090);
    CHECK(requested.family == QStringLiteral("web888") && requested.maxSlices == 1);
    CHECK(received.value(QStringLiteral("web888.password")) == QStringLiteral("ephemeral-test"));
    CHECK(received.value(QStringLiteral("web888.endpoint")) == QStringLiteral("http://192.0.2.10:8090"));
    CHECK(password->text().isEmpty());
    CHECK(panel.automationConnectByIp(QStringLiteral("receiver.invalid"), QStringLiteral("web888"), &error));
    CHECK(requests == 2 && requested.port == 8073);
    for (const auto& invalid : {QStringLiteral("https://receiver.invalid"),
                               QStringLiteral("user:secret@receiver.invalid"),
                               QStringLiteral("receiver.invalid/path"),
                               QStringLiteral("receiver.invalid:0"),
                               QStringLiteral("receiver.invalid?password=x")}) {
        panel.probeRadio(invalid);
        CHECK(requests == 2 && legacyRequests == 0);
    }
    panel.probeRadio(QStringLiteral("192.0.2.10:8073"));
    CHECK(requests == 3);
    panel.show();
    QApplication::processEvents();
    CHECK(password->isVisible() && !password->accessibleName().isEmpty());
    if (argc > 1) {
        CHECK(panel.grab().save(QString::fromLocal8Bit(argv[1])));
    }
    password->setText(QStringLiteral("discard-on-switch"));
    type->setCurrentIndex(type->findData(QStringLiteral("flex")));
    CHECK(password->text().isEmpty() && !password->isVisible());
#else
    CHECK(index < 0);
    QString error;
    CHECK(!panel.automationConnectByIp(QStringLiteral("192.0.2.10"), QStringLiteral("web888"), &error));
#endif
    return 0;
}
