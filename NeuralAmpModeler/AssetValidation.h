#pragma once

#include <cmath>
#include <stdexcept>

#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
#include "../AudioDSPTools/dsp/wav.h"

namespace nam_library
{
inline void ValidateModel(const std::filesystem::path& asset)
{
  auto model = nam::get_dsp(asset);
  if (!model || model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
    throw std::runtime_error("Model must have one input and one output channel.");
}

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
