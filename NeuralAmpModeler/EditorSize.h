#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nam_ui
{
// Use whole aspect-ratio steps from the including plugin's configured dimensions
// so both dimensions are integral and there is no leftover
// strip of host background. Follow whichever drag axis changed proportionally
// more, so resizing from either a side or the bottom remains responsive.
inline bool ConstrainEditorSize(int& width, int& height, int currentWidth, int currentHeight)
{
  constexpr int divisor = std::gcd(PLUG_WIDTH, PLUG_HEIGHT);
  constexpr int unitWidth = PLUG_WIDTH / divisor;
  constexpr int unitHeight = PLUG_HEIGHT / divisor;
  constexpr int minSteps = std::max((PLUG_MIN_WIDTH + unitWidth - 1) / unitWidth,
                                    (PLUG_MIN_HEIGHT + unitHeight - 1) / unitHeight);
  constexpr int maxSteps = std::min(PLUG_MAX_WIDTH / unitWidth, PLUG_MAX_HEIGHT / unitHeight);
  const double widthSteps = static_cast<double>(width) / unitWidth;
  const double heightSteps = static_cast<double>(height) / unitHeight;
  const bool useWidth = std::abs(widthSteps - static_cast<double>(currentWidth) / unitWidth)
                     >= std::abs(heightSteps - static_cast<double>(currentHeight) / unitHeight);
  const int steps = static_cast<int>(std::round(std::clamp(useWidth ? widthSteps : heightSteps,
                                                         double(minSteps), double(maxSteps))));
  const int constrainedWidth = steps * unitWidth;
  const int constrainedHeight = steps * unitHeight;
  const bool unchanged = width == constrainedWidth && height == constrainedHeight;
  width = constrainedWidth;
  height = constrainedHeight;
  return unchanged;
}
} // namespace nam_ui
