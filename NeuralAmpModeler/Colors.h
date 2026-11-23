#pragma once

#include "IGraphicsStructs.h"

// Shared with the cabinet page. Keep accents for selection and signal activity;
// surfaces and typography stay neutral across the amp, pedals, and settings.
namespace PluginColors
{
const iplug::igraphics::IColor BACKGROUND(255, 24, 26, 30);
const iplug::igraphics::IColor PANEL(255, 34, 37, 42);
const iplug::igraphics::IColor SURFACE(255, 48, 52, 58);
const iplug::igraphics::IColor BORDER(255, 66, 71, 79);
const iplug::igraphics::IColor TEXT = iplug::igraphics::COLOR_WHITE;
const iplug::igraphics::IColor MUTED(255, 170, 175, 182);
const iplug::igraphics::IColor CLIP(255, 242, 104, 104);
const iplug::igraphics::IColor NAM_THEMECOLOR(255, 143, 55, 255);
const iplug::igraphics::IColor NAM_THEMEFONTCOLOR = TEXT;
const iplug::igraphics::IColor MOUSEOVER(255, 60, 65, 73);
const iplug::igraphics::IColor HELP_TEXT = MUTED;
const iplug::igraphics::IColor HELP_TEXT_MO = TEXT;
const iplug::igraphics::IColor HELP_TEXT_CLICKED = NAM_THEMECOLOR;
} // namespace PluginColors
