#include "gui/DisplaySessionMonitor.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QWidget>
#include <cstdio>
#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace AetherSDR {
struct DisplaySessionMonitorTestAccess {
    static void locked(DisplaySessionMonitor& monitor, bool locked) {
        monitor.setSessionLocked(locked);
    }
#ifdef Q_OS_WIN
    static HWND nativeWindow(const DisplaySessionMonitor& monitor) {
        return static_cast<HWND>(monitor.m_nativeWindow);
    }
    static bool registered(const DisplaySessionMonitor& monitor) {
        return monitor.m_registered;
    }
#endif
};
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    AetherSDR::DisplaySessionMonitor monitor(nullptr, false);
    int failures = 0;
    const auto check = [&failures](bool ok, const char* label) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", label);
        if (!ok) ++failures;
    };
    QWidget normal;
    QWidget disabled;
    normal.show();
    disabled.setUpdatesEnabled(false);
    disabled.show();
    QCoreApplication::processEvents();
    AetherSDR::DisplaySessionMonitorTestAccess::locked(monitor, true);
    check(!normal.updatesEnabled() && !disabled.updatesEnabled(),
        "locked session stops painting existing windows");
    bool delivered = false;
    QMetaObject::invokeMethod(&normal, [&delivered] { delivered = true; }, Qt::QueuedConnection);
    QCoreApplication::processEvents();
    check(delivered, "control/event delivery continues while rendering is suspended");
    QWidget late;
    late.show();
    QCoreApplication::processEvents();
    check(!late.updatesEnabled(), "windows opened during a locked session do not start rendering");
    auto transient = std::make_unique<QWidget>();
    transient->show();
    transient.reset();
    AetherSDR::DisplaySessionMonitorTestAccess::locked(monitor, false);
    check(normal.updatesEnabled() && late.updatesEnabled(),
        "unlock restores painting for surviving windows");
    check(!disabled.updatesEnabled(), "unlock preserves windows whose updates were already disabled");
    AetherSDR::DisplaySessionMonitorTestAccess::locked(monitor, false);
    check(normal.updatesEnabled(), "repeated availability notifications are harmless");
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        AetherSDR::DisplaySessionMonitor native(nullptr, true);
        const HWND window = AetherSDR::DisplaySessionMonitorTestAccess::nativeWindow(native);
        check(window && AetherSDR::DisplaySessionMonitorTestAccess::registered(native),
            "native Windows session notification registration succeeds");
        if (window && AetherSDR::DisplaySessionMonitorTestAccess::registered(native)) {
            QWidget marker;
            marker.show();
            // Exercise only this test's message window. These notifications
            // change the test monitor, never the actual Windows session.
            SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);
            QCoreApplication::processEvents();
            check(marker.updatesEnabled(), "native unlock callback restores widget updates");
            SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
            QCoreApplication::processEvents();
            check(!marker.updatesEnabled(), "native lock callback suspends widget updates");
            SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);
            QCoreApplication::processEvents();
            check(marker.updatesEnabled(), "native callback restores updates after a complete cycle");
        }
    }
#endif
    return failures ? 1 : 0;
}
