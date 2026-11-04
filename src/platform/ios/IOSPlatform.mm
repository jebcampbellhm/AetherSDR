#include "core/AppActivity.h"

namespace AetherSDR {

// iOS controls process lifetime. App Nap's macOS activity API is unavailable;
// foreground audio is scheduled by Qt Multimedia's native iOS audio backend.
void keepAppActive()
{
}

} // namespace AetherSDR
