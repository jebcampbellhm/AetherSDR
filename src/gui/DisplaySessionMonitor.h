#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

class QWidget;

namespace AetherSDR {

// A locked Windows desktop cannot present the QRhi-backed widget tree.
// Suspend painting, not event delivery or reception, until the session returns.
class DisplaySessionMonitor final : public QObject {
    Q_OBJECT
public:
    explicit DisplaySessionMonitor(QObject* parent = nullptr, bool nativeNotifications = true);
    ~DisplaySessionMonitor() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void setSessionLocked(bool locked);

private:
    friend struct DisplaySessionMonitorTestAccess;
    bool m_locked = false;
    QVector<QPointer<QWidget>> m_suspendedWindows;
    void* m_nativeWindow = nullptr;
    bool m_registered = false;
};

} // namespace AetherSDR
