#pragma once
#include "CanonWindow.h"
namespace AetherSDR {
class RadioModel;
class DigitalMonitoringDialog final : public CanonWindow {
public:
    explicit DigitalMonitoringDialog(RadioModel* radio, QWidget* parent = nullptr);
};
}
