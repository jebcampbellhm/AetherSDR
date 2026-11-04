#include "TabletUi.h"
#include "CanonIndicators.h"
#include "core/ThemeManager.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QScrollArea>
#include <QScroller>
#include <QSlider>
#include <QWidget>

namespace AetherSDR {

namespace {
class TabletWindowFit final : public QObject {
public:
    explicit TabletWindowFit(QWidget* window) : QObject(window), m_window(window)
    {
        window->installEventFilter(this);
        if (window->screen()) {
            connect(window->screen(), &QScreen::availableGeometryChanged, this,
                    [this] { schedule(); });
            connect(window->screen(), &QScreen::geometryChanged, this,
                    [this] { schedule(); });
        }
    }
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Show) schedule();
        return false;
    }
    void schedule()
    {
        QTimer::singleShot(0, this, [this] {
            if (!m_window || !m_window->isVisible() || !m_window->screen()) return;
            const QRect area = m_window->screen()->availableGeometry().adjusted(12, 12, -12, -12);
            const QSize size = m_window->size().boundedTo(area.size());
            m_window->resize(size);
            m_window->move(area.center() - QPoint(m_window->width() / 2, m_window->height() / 2));
        });
    }
private:
    QPointer<QWidget> m_window;
};
}

void keepTabletWindowOnScreen(QWidget* window)
{
    if (tabletUiEnabled()) new TabletWindowFit(window);
}

void styleTabletSurface(QWidget* surface)
{
    applyCanonSheet(surface, [] { return QStringLiteral(
        "QWidget#tabletHeader, QWidget#tabletControls, QWidget#DspParamPopup {"
        " background: {{color.canon.raised}}; color: {{color.canon.ink}}; }"
        "QLabel { color: {{color.canon.inkSoft}}; font-size: 15px; }"
        "QPushButton, QComboBox, QLineEdit { min-height: 44px;"
        " background: {{color.canon.control}}; color: {{color.canon.ink}};"
        " border: 1px solid {{color.canon.lineHi}}; border-radius: 8px;"
        " padding: 0 10px; font-size: 16px; }"
        "QPushButton:checked, QPushButton:pressed {"
        " border-color: {{color.canon.cyan}}; color: {{color.canon.cyan}}; }"
        "QPushButton:disabled, QComboBox:disabled { color: {{color.canon.muted}}; }"
        "QCheckBox, QRadioButton { color: {{color.canon.ink}}; font-size: 16px; spacing: 12px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 28px; height: 28px; }"
        "QPushButton:focus, QLineEdit:focus, QComboBox:focus {"
        " border: 2px solid {{color.canon.cyan}}; }"
        "QComboBox QAbstractItemView { background: {{color.canon.control}};"
        " color: {{color.canon.ink}}; selection-background-color: {{color.canon.raised}}; }"
        "QComboBox QAbstractItemView::item { min-height: 44px; }"
        "QSlider { min-height: 48px; }"
        "QSlider::groove:horizontal { height: 8px; background: {{color.canon.control}};"
        " border-radius: 4px; }"
        "QSlider::handle:horizontal { width: 32px; margin: -12px 0;"
        " background: {{color.canon.cyan}}; border-radius: 16px; }")
        + canonIndicatorRules()
        + QStringLiteral("QCheckBox::indicator, QRadioButton::indicator { width: 28px; height: 28px; }"); });
}


void prepareTabletControls(QWidget* surface)
{
    if (!tabletUiEnabled()) {
        return;
    }
    for (QWidget* control : surface->findChildren<QWidget*>()) {
        const bool interactive = qobject_cast<QAbstractButton*>(control)
            || qobject_cast<QComboBox*>(control) || qobject_cast<QLineEdit*>(control)
            || qobject_cast<QAbstractSpinBox*>(control) || qobject_cast<QSlider*>(control);
        if (interactive) {
            control->setMaximumHeight(qMax(48, control->maximumHeight()));
            control->setMinimumHeight(48);
            QFont font = control->font();
            font.setPointSize(qMax(14, font.pointSize()));
            control->setFont(font);
        }
        if (auto* scroll = qobject_cast<QScrollArea*>(control)) {
            QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
        }
    }
}

} // namespace AetherSDR
