#pragma once

#include "AssetLibrary.h"
#include "IPlugPaths.h"

namespace nam_library
{
inline AssetLibrary UserLibrary()
{
  WDL_String support;
  iplug::AppSupportPath(support, false);
  return AssetLibrary(std::filesystem::u8path(support.Get()) / PLUG_NAME / "Library");
}
} // namespace nam_library
