#include "DisplaySessionMonitor.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QWidget>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>

namespace {
constexpr wchar_t kWindowClass[] = L"AetherSDRDisplaySessionMonitor";

LRESULT CALLBACK sessionWindowProc(HWND window, UINT message, WPARAM parameter, LPARAM detail)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(detail);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (message == WM_WTSSESSION_CHANGE
        && (parameter == WTS_SESSION_LOCK || parameter == WTS_SESSION_UNLOCK)) {
        auto* monitor = reinterpret_cast<QObject*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (monitor)
            QMetaObject::invokeMethod(monitor, "setSessionLocked", Qt::QueuedConnection,
                Q_ARG(bool, parameter == WTS_SESSION_LOCK));
    }
    return DefWindowProcW(window, message, parameter, detail);
}
}
#endif

namespace AetherSDR {

DisplaySessionMonitor::DisplaySessionMonitor(QObject* parent, bool nativeNotifications)
    : QObject(parent)
{
    qApp->installEventFilter(this);
#ifdef Q_OS_WIN
    // Offscreen/raster test platforms have no Windows swap chain to stall.
    if (!nativeNotifications || QGuiApplication::platformName() != QStringLiteral("windows"))
        return;
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = sessionWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        qWarning() << "Display session monitor could not register its message window:" << GetLastError();
        return;
    }
    const HWND window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, windowClass.hInstance, this);
    m_nativeWindow = window;
    if (window)
        m_registered = WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
    if (!m_registered) {
        const DWORD error = GetLastError();
        qWarning() << "Display session notifications unavailable:" << error;
        // Without an unlock notification there is no safe way to restore
        // painting. Leave updates alone rather than strand a locked startup.
        return;
    }

    // Startup may itself be unattended while the desktop is already locked.
    // Read only SessionFlags, not the account or domain fields of this record.
    LPWSTR data = nullptr;
    DWORD bytes = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
            WTSSessionInfoEx, &data, &bytes)) {
        const auto* info = reinterpret_cast<const WTSINFOEXW*>(data);
        if (bytes >= sizeof(WTSINFOEXW) && info->Level == 1)
            setSessionLocked(info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK);
        WTSFreeMemory(data);
    }
#else
    Q_UNUSED(nativeNotifications);
#endif
}

DisplaySessionMonitor::~DisplaySessionMonitor()
{
    qApp->removeEventFilter(this);
#ifdef Q_OS_WIN
    const auto window = static_cast<HWND>(m_nativeWindow);
    if (m_registered)
        WTSUnRegisterSessionNotification(window);
    if (window)
        DestroyWindow(window);
#endif
    setSessionLocked(false);
}

void DisplaySessionMonitor::setSessionLocked(bool locked)
{
    if (locked == m_locked)
        return;
    m_locked = locked;
    if (locked) {
        // Snapshot before disabling a parent: Qt may propagate its disabled
        // update state to child windows, obscuring their original state.
        for (QWidget* window : QApplication::topLevelWidgets()) {
            if (window->updatesEnabled())
                m_suspendedWindows.push_back(window);
        }
        for (const auto& window : std::as_const(m_suspendedWindows)) {
            if (window)
                window->setUpdatesEnabled(false);
        }
    } else {
        const auto windows = std::exchange(m_suspendedWindows, {});
        for (const auto& window : windows) {
            if (window)
                window->setUpdatesEnabled(true);
        }
    }
    qInfo() << "Desktop session rendering" << (locked ? "suspended" : "resumed");
}

bool DisplaySessionMonitor::eventFilter(QObject* watched, QEvent* event)
{
    if (m_locked && (event->type() == QEvent::Show || event->type() == QEvent::UpdateRequest)) {
        auto* window = qobject_cast<QWidget*>(watched);
        if (window && window->isWindow() && window->updatesEnabled()) {
            m_suspendedWindows.push_back(window);
            window->setUpdatesEnabled(false);
        }
        // A repaint posted before locking must not enter the QRhi compositor.
        // Meta-calls, timers, receiver I/O, and ordinary control events pass on.
        if (window && event->type() == QEvent::UpdateRequest)
            return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace AetherSDR
