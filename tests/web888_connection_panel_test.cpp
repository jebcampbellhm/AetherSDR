#include "TestSettingsProfile.h"
#include "gui/ConnectionPanel.h"
#include "gui/DigitalMonitoringDialog.h"
#include "models/RadioModel.h"
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include "core/backends/web888/Web888Backend.h"
#include "core/KiwiSdrClient.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QLineEdit>
#include <QPixmap>

using namespace AetherSDR;
#define CHECK(c) do { if (!(c)) { qCritical() << "line" << __LINE__ << #c; return 1; } } while (false)

class MonitoringClient final : public KiwiSdrClient {
public:
    explicit MonitoringClient(QObject* parent) : KiwiSdrClient(Streams::SoundAndWaterfall,
        Admission::IndependentReceiver, parent) {}
#ifdef HAVE_WEBSOCKETS
protected:
    void startStatusPreflight(const QUrl&) override {}
#endif
};
class MonitoringBackend final : public Web888Backend {
public:
    MonitoringClient* client{};
protected:
    KiwiSdrClient* createClient() override { client = new MonitoringClient(this); return client; }
};

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("web888-connection-panel"));
    if (!profile.isValid()) {
        return 1;
    }
    QApplication app(argc, argv);
    RadioModel radio;
    DigitalMonitoringDialog monitoring(&radio);
    monitoring.show();
    QApplication::processEvents();
    int disabledActions = 0;
    for (auto* button : monitoring.findChildren<QPushButton*>()) {
        if (button->text() == "Start monitoring" || button->text() == "Stop") {
            CHECK(!button->isEnabled()); ++disabledActions;
        }
    }
    CHECK(disabledActions == 2);
    bool reasonVisible = false;
    for (auto* label : monitoring.findChildren<QLabel*>()) {
        if (label->text().contains("Connect a receiver")) reasonVisible = label->isVisible();
    }
    CHECK(reasonVisible);
    if (argc > 2) CHECK(monitoring.grab().save(QString::fromLocal8Bit(argv[2])));
    monitoring.hide();
#ifdef HAVE_WEBSOCKETS
    auto ownedMonitor = std::make_unique<MonitoringBackend>();
    auto* monitoringBackend = ownedMonitor.get();
    radio.setBackendForTest(std::move(ownedMonitor), QStringLiteral("web888"));
    RadioConnectRequest monitorRequest;
    monitorRequest.params.insert(QStringLiteral("web888.password"), QString{});
    monitorRequest.host = QStringLiteral("receiver.invalid"); monitorRequest.port = 8073;
    monitoringBackend->connectRadio(monitorRequest);
    emit monitoringBackend->client->stateChanged(KiwiSdrClient::State::Connected, {});
    DigitalMonitoringDialog connectedMonitoring(&radio);
    connectedMonitoring.show(); QApplication::processEvents();
    QPushButton* monitorStart = nullptr;
    for (auto* button : connectedMonitoring.findChildren<QPushButton*>())
        if (button->text() == "Start monitoring") monitorStart = button;
    CHECK(monitorStart && monitorStart->isEnabled());
    auto* monitorBudget = connectedMonitoring.findChild<QSpinBox*>();
    CHECK(monitorBudget);
    monitorBudget->setValue(2);
    CHECK(!monitorStart->isEnabled());
    monitorBudget->setValue(13);
    CHECK(monitorStart->isEnabled());
    if (argc > 2) CHECK(connectedMonitoring.grab().save(QString::fromLocal8Bit(argv[2])));
    monitoringBackend->disconnectRadio();
    emit radio.connectionStateChanged(false);
    CHECK(!monitorStart->isEnabled());
    connectedMonitoring.hide();
#endif
    ConnectionPanel panel;
    auto* type = panel.findChild<QComboBox*>(QStringLiteral("connectionManualRadioType"));
    auto* password = panel.findChild<QLineEdit*>(QStringLiteral("connectionWeb888Password"));
    CHECK(type && password && password->echoMode() == QLineEdit::Password);
    const int index = type->findData(QStringLiteral("web888"));
#ifdef HAVE_WEBSOCKETS
    CHECK(index >= 0);
    CHECK(panel.automationManualFamilies().contains(QStringLiteral("web888")));
    type->setCurrentIndex(index);
    password->setText(QStringLiteral("ephemeral-test"));
    int requests = 0;
    RadioInfo requested;
    QObject::connect(&panel, &ConnectionPanel::connectRequested, &app,
        [&](const RadioInfo& info) { ++requests; requested = info; });
    auto* port = panel.findChild<QSpinBox*>(QStringLiteral("connectionWeb888Port"));
    CHECK(port); port->setValue(8090);
    QString error;
    CHECK(panel.automationConnectByIp(QStringLiteral("192.0.2.10"), QStringLiteral("web888"), &error));
    CHECK(requests == 1 && requested.port == 8090);
    CHECK(requested.maxSlices == 4 && requested.maxPanadapters == 2);
    CHECK(password->text().isEmpty());
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
    CHECK(!panel.automationManualFamilies().contains(QStringLiteral("web888")));
    QString error;
    CHECK(!panel.automationConnectByIp(QStringLiteral("192.0.2.10"), QStringLiteral("web888"), &error));
#endif
    return 0;
}
