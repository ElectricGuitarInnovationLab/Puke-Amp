#pragma once

#include <cmath>
#include <stdexcept>

#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
#include "IRValidation.h"

namespace nam_library
{
inline void ValidateModel(const std::filesystem::path& asset)
{
  auto model = nam::get_dsp(asset);
  if (!model || model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
    throw std::runtime_error("Model must have one input and one output channel.");
}

} // namespace nam_library
