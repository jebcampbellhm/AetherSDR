#include "TabletUi.h"
#include "CanonIndicators.h"
#include "core/ThemeManager.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
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
    TabletWindowFit(QWidget* window, bool fillParent)
        : QObject(window), m_window(window), m_fillParent(fillParent)
    {
        window->installEventFilter(this);
        trackParentWindow();
        if (window->screen()) {
            connect(window->screen(), &QScreen::availableGeometryChanged, this,
                    [this] { schedule(); });
            connect(window->screen(), &QScreen::geometryChanged, this,
                    [this] { schedule(); });
        }
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_window && event->type() == QEvent::ParentChange) trackParentWindow();
        if ((watched == m_window && event->type() == QEvent::Show)
            || (watched == m_parentWindow && (event->type() == QEvent::Resize || event->type() == QEvent::Move))) {
            schedule();
        }
        return false;
    }
private:
    void trackParentWindow()
    {
        if (!m_fillParent) return;
        if (m_parentWindow) m_parentWindow->removeEventFilter(this);
        m_parentWindow = m_window->parentWidget();
        if (m_parentWindow) m_parentWindow->installEventFilter(this);
    }

    void schedule()
    {
        QTimer::singleShot(0, this, [this] {
            if (!m_window || !m_window->isVisible() || !m_window->screen()) return;
            QRect area = m_window->screen()->availableGeometry();
            if (m_parentWindow) {
                area &= QRect(m_parentWindow->mapToGlobal(QPoint()), m_parentWindow->size());
            }
            area.adjust(12, 12, -12, -12);
            if (!area.isValid()) return;
            const QSize size = m_fillParent ? area.size() : m_window->size().boundedTo(area.size());
            m_window->resize(size);
            m_window->move(area.center() - QPoint(m_window->width() / 2, m_window->height() / 2));
        });
    }
    QPointer<QWidget> m_window;
    QPointer<QWidget> m_parentWindow;
    bool m_fillParent;
};
}

void keepTabletWindowOnScreen(QWidget* window, bool fillParent)
{
    if (tabletUiEnabled()) new TabletWindowFit(window, fillParent);
}

QWidget* makeTabletPanelHeader(const QString& title, QWidget* panel)
{
    auto* header = new QWidget(panel);
    header->setObjectName(QStringLiteral("tabletPanelHeader"));
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(10);
    auto* heading = new QLabel(title, header);
    heading->setObjectName(QStringLiteral("tabletPanelTitle"));
    heading->setWordWrap(true);
    layout->addWidget(heading, 1);

    auto* close = new QPushButton(QObject::tr("Close"), header);
    close->setObjectName(QStringLiteral("tabletPanelClose"));
    close->setAccessibleName(QObject::tr("Close panel"));
    close->setAutoDefault(false);
    close->setMinimumHeight(48);
    close->setMinimumWidth(96);
    close->setIconSize(QSize(20, 20));
    const auto refreshIcon = [close] {
        QPixmap glyph(60, 60);
        glyph.setDevicePixelRatio(3);
        glyph.fill(Qt::transparent);
        QPainter painter(&glyph);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = ThemeManager::instance().color(close, QStringLiteral("color.canon.ink"));
        painter.setPen(QPen(color, 2.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(4, 4), QPointF(16, 16));
        painter.drawLine(QPointF(16, 4), QPointF(4, 16));
        painter.end();
        close->setIcon(QIcon(glyph));
    };
    auto& theme = ThemeManager::instance();
    QObject::connect(&theme, &ThemeManager::themeChanged, close, refreshIcon);
    refreshIcon();
    QObject::connect(close, &QPushButton::clicked, panel, [panel] { panel->window()->close(); });
    layout->addWidget(close, 0, Qt::AlignTop);
    applyCanonSheet(header, [] { return QStringLiteral(
        "QWidget#tabletPanelHeader { background: {{color.canon.raised}}; }"
        "QLabel#tabletPanelTitle { color: {{color.canon.ink}}; font-size: 18px; font-weight: bold; }"
        "QPushButton#tabletPanelClose { background: {{color.canon.control}}; color: {{color.canon.ink}};"
        " border: 1px solid {{color.canon.lineHi}}; border-radius: 10px;"
        " padding: 0 12px; font-size: 16px; font-weight: bold; }"
        "QPushButton#tabletPanelClose:pressed { background: {{color.canon.nested}}; }"
        "QPushButton#tabletPanelClose:focus { border: 2px solid {{color.canon.cyan}}; }"); });
    return header;
}

void styleTabletSurface(QWidget* surface)
{
    applyCanonSheet(surface, [] { return QStringLiteral(
        "QWidget#tabletHeader, QWidget#tabletDock, QWidget#tabletControls, QWidget#DspParamPopup {"
        " background: {{color.canon.raised}}; color: {{color.canon.ink}}; }"
        "QLabel { color: {{color.canon.inkSoft}}; font-size: 15px; }"
        "QPushButton, QComboBox, QLineEdit, QAbstractSpinBox { min-height: 44px;"
        " background: {{color.canon.control}}; color: {{color.canon.ink}};"
        " border: 1px solid {{color.canon.lineHi}}; border-radius: 8px;"
        " padding: 0 10px; font-size: 16px; }"
        "QPushButton:checked, QPushButton:pressed {"
        " border-color: {{color.canon.cyan}}; color: {{color.canon.cyan}}; }"
        "QPushButton:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { color: {{color.canon.muted}}; }"
        "QCheckBox, QRadioButton { color: {{color.canon.ink}}; font-size: 16px; spacing: 12px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 28px; height: 28px; }"
        "QPushButton:focus, QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus {"
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
