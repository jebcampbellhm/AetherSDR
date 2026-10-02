#pragma once

#include "Web888IqSplitter.h"

namespace AetherSDR {
struct Web888DigitalPreset {
    QString band;
    QString mode;
    Web888DigitalSlice slice;
};
struct Web888CaptureWindow {
    double centerHz{};
    QVector<Web888DigitalPreset> presets;
};
struct Web888CapturePlan {
    QVector<Web888CaptureWindow> windows;
    int reservedListening{};
    bool fitsCapacity{};
    QString error;
};

// Explicit receive-only presets from docs/web888-frequency-coverage.md.
QVector<Web888DigitalPreset> web888DigitalPresets(bool includeFt4 = false,
                                                 bool alternate80mWspr = false);
// Pure planning: never opens a session, changes an admin setting, or evicts a
// listener. A capacity failure retains the proposal for review but cannot run.
Web888CapturePlan planWeb888Captures(const QVector<Web888DigitalPreset>& presets,
                                    double sampleRateHz, int availableReceivers,
                                    int reservedListening = 1);
} // namespace AetherSDR
