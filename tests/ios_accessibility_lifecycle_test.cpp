// Reproduce the iOS Qt 6.12.0 failure: an accessibility client asks for the
// role of a cached button interface while QWidget is destroying the button.
#include "gui/ios/IOSAccessibility.h"

#include <QAccessible>
#include <QApplication>
#include <QPushButton>

#include <cstdio>

namespace {
int destroyed = 0;
int nameChanges = 0;
bool valid = true;
QObject* liveButton = nullptr;
bool observingName = false;

void observe(QAccessibleEvent* event)
{
    if (event->type() == QAccessible::ObjectDestroyed) {
        ++destroyed;
        valid = valid && event->object() == QCoreApplication::instance();
        // Calling role() here crashes against the unmodified Qt iOS path
        // once the QPushButton destructor has reached QWidget.
        auto* interface = event->accessibleInterface();
        valid = valid && interface && interface->isValid()
            && interface->role() == QAccessible::Application;
    } else if (observingName && event->type() == QAccessible::NameChanged) {
        ++nameChanges;
        valid = valid && event->object() == liveButton;
    }
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QAccessible::setActive(true);
    if (!QAccessible::isActive()) {
        std::puts("SKIP: no accessibility backend on this platform");
        return 77;
    }
    const auto original = QAccessible::installUpdateHandler(observe);
    AetherSDR::installIOSAccessibilityWorkaround();
    for (int i = 0; i < 3; ++i) {
        QPushButton button(QStringLiteral("Receive filter"));
        liveButton = &button;
        valid = valid && QAccessible::queryAccessibleInterface(&button);
        QAccessibleEvent change(&button, QAccessible::NameChanged);
        observingName = true;
        QAccessible::updateAccessibility(&change);
        observingName = false;
    }
    QAccessible::installUpdateHandler(original);
    if (!valid || destroyed != 3 || nameChanges != 3) {
        std::fprintf(stderr, "accessibility lifecycle failed: valid=%d destroyed=%d names=%d\n",
                     valid, destroyed, nameChanges);
        return 1;
    }
    std::puts("Button destruction stays safe; live accessibility updates retain their target.");
    return 0;
}
