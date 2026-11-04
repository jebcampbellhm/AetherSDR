#include "IOSAccessibility.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformaccessibility.h>
#include <qpa/qplatformintegration.h>

namespace AetherSDR {
namespace {

QAccessible::UpdateHandler previousHandler = nullptr;

void forwardAccessibilityEvent(QAccessibleEvent* event)
{
    auto forward = [](QAccessibleEvent* update) {
        if (previousHandler) {
            previousHandler(update);
        } else if (auto* integration = QGuiApplicationPrivate::platformIntegration()) {
            if (auto* platform = integration->accessibility()) {
                platform->notifyAccessibilityUpdate(update);
            }
        }
    };
    if (event->type() == QAccessible::ObjectDestroyed) {
        // Qt 6.12.0's iOS bridge asks role() during QWidget destruction, after
        // the derived button has gone. Its cache invalidation is global, so
        // use the surviving application interface for that notification.
        // The Qt 6.12 branch fixes this by never reading the destroyed role.
        QAccessibleEvent safe(QCoreApplication::instance(), QAccessible::ObjectDestroyed);
        forward(&safe);
        return;
    }
    forward(event);
}

} // namespace

void installIOSAccessibilityWorkaround()
{
#if QT_VERSION == QT_VERSION_CHECK(6, 12, 0)
    previousHandler = QAccessible::installUpdateHandler(forwardAccessibilityEvent);
#endif
}

} // namespace AetherSDR
