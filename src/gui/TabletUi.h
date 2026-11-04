#pragma once

#include <QtGlobal>

class QWidget;

namespace AetherSDR {

inline bool tabletUiEnabled()
{
#ifdef Q_OS_IOS
    return true;
#else
    return qEnvironmentVariableIntValue("AETHER_TABLET_UI") == 1;
#endif
}

void styleTabletSurface(QWidget* surface);
void prepareTabletControls(QWidget* surface);
void keepTabletWindowOnScreen(QWidget* window);

} // namespace AetherSDR
