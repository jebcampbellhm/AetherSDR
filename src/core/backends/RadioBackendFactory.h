#pragma once

#include <QString>
#include <memory>

namespace AetherSDR {
class IRadioBackend;
// Composition boundary: concrete vendor types remain below IRadioBackend.
// Unsupported optional families return null, never a different radio protocol.
std::unique_ptr<IRadioBackend> createRadioBackend(const QString& family);
}
