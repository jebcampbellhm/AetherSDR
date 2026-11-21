#pragma once

#include <QtGlobal>

class QWidget;
class QString;

namespace AetherSDR {

inline bool tabletUiEnabled()
{
#ifdef Q_OS_IOS
    return true;
#else
    return qEnvironmentVariableIntValue("AETHER_TABLET_UI") == 1;
#endif
}

QWidget* makeTabletPanelHeader(const QString& title, QWidget* panel);
void styleTabletSurface(QWidget* surface);
void prepareTabletControls(QWidget* surface);
void keepTabletWindowOnScreen(QWidget* window, bool fillParent = false);

} // namespace AetherSDR
