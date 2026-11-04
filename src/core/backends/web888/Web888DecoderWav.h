#pragma once
#include "Web888DecoderRouter.h"
#include <QByteArray>

namespace AetherSDR {
// Bounded 12 kHz mono PCM16 RIFF serialization. No files, devices or processes.
// Caller retains the job and must recheck current() before handing bytes off.
std::optional<QByteArray> web888DecoderWav(const Web888DecoderJob& job, QString* error = nullptr);
}
