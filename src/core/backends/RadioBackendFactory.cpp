#include "RadioBackendFactory.h"

#include "anan/AnanBackend.h"
#include "flex/FlexBackend.h"
#include "hl2/Hl2Backend.h"
#include "icom/IcomCivBackend.h"
#include "sim/SimBackend.h"
#include "web888/Web888Backend.h"
#include "nrsp/NrspBackend.h"
#ifdef AETHER_BACKEND_RTL
#include "rtl/RtlSdrBackend.h"
#endif

namespace AetherSDR {
std::unique_ptr<IRadioBackend> createRadioBackend(const QString& family)
{
    const QString name = family.toLower();
    if (name == QStringLiteral("nrsp")) { return std::make_unique<NrspBackend>(); }
    if (name == QStringLiteral("hl2")) {
        return std::make_unique<hl2::Hl2Backend>();
    }
    if (name == QStringLiteral("anan")) {
        return std::make_unique<anan::AnanBackend>();
    }
    if (name == QStringLiteral("icom")) {
        return std::make_unique<icom::IcomCivBackend>();
    }
    if (name == QStringLiteral("sim")) {
        return std::make_unique<SimBackend>();
    }
    if (name == QStringLiteral("rtl")) {
#ifdef AETHER_BACKEND_RTL
        return std::make_unique<rtl::RtlSdrBackend>();
#else
        return nullptr;
#endif
    }
    if (name == QStringLiteral("web888")) {
#ifdef HAVE_WEBSOCKETS
        return std::make_unique<Web888Backend>();
#else
        return nullptr;
#endif
    }
    return std::make_unique<FlexBackend>();
}
}
