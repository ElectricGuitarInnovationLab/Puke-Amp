#pragma once

#include "Colors.h"
#include "IGraphics.h"

namespace nam_ui
{
inline iplug::igraphics::IText Text(float size = 12.f,
                                  const iplug::igraphics::IColor& color = PluginColors::TEXT,
                                  iplug::igraphics::EAlign align = iplug::igraphics::EAlign::Center)
{
  using namespace iplug::igraphics;
  return IText(size, color, "Roboto-Regular", align, EVAlign::Middle, 0.f,
               PluginColors::SURFACE, PluginColors::TEXT);
}

inline iplug::igraphics::IVStyle ControlStyle()
{
  using namespace iplug::igraphics;
  return DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT)
    .WithColor(kFG, PluginColors::SURFACE)
    .WithColor(kPR, PluginColors::NAM_THEMECOLOR.WithOpacity(0.25f))
    .WithColor(kFR, PluginColors::BORDER)
    .WithColor(kHL, PluginColors::MOUSEOVER)
    .WithColor(kSH, COLOR_TRANSPARENT)
    .WithColor(kX1, PluginColors::NAM_THEMECOLOR)
    .WithColor(kX2, PluginColors::CLIP)
    .WithColor(kX3, PluginColors::TEXT)
    .WithLabelText(Text(11.f, PluginColors::MUTED))
    .WithValueText(Text(13.f))
    .WithDrawFrame(false).WithDrawShadows(false).WithEmboss(false)
    .WithShadowOffset(0.f).WithRoundness(0.25f).WithWidgetFrac(0.85f);
}

inline iplug::igraphics::IVStyle LabelStyle(float size = 12.f,
                                           const iplug::igraphics::IColor& color = PluginColors::TEXT,
                                           iplug::igraphics::EAlign align = iplug::igraphics::EAlign::Center)
{
  return ControlStyle().WithValueText(Text(size, color, align));
}

inline void DrawButton(iplug::igraphics::IGraphics& g, const iplug::igraphics::IRECT& bounds,
                       const char* label, bool hover = false, float size = 12.f,
                       const iplug::igraphics::IBlend* blend = nullptr)
{
  g.FillRoundRect(hover ? PluginColors::MOUSEOVER : PluginColors::SURFACE, bounds, 4.f, blend);
  g.DrawText(Text(size), label, bounds.GetHPadded(-8.f), blend);
}
} // namespace nam_ui
