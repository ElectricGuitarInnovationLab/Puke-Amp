#pragma once

#include "../AudioDSPTools/dsp/wav.h"
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace nam_library
{
inline void ValidateIR(const std::filesystem::path& asset)
{
  std::vector<float> audio;
  double sampleRate = 0.0;
  const auto state = dsp::wav::Load(asset.string().c_str(), audio, sampleRate);
  if (state != dsp::wav::LoadReturnCode::SUCCESS)
    throw std::runtime_error(dsp::wav::GetMsgForLoadReturnCode(state));
  if (audio.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::runtime_error("IR must contain audio with a valid sample rate.");
}
} // namespace nam_library
