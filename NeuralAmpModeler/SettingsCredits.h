#pragma once

#include "IControls.h"
#include "Colors.h"

namespace nam_ui
{
class SettingsCredits : public iplug::igraphics::IContainerBase
{
  using IRECT = iplug::igraphics::IRECT;
  using IText = iplug::igraphics::IText;

public:
  SettingsCredits(const IRECT& bounds, const IText& text, const char* version)
  : IContainerBase(bounds), mText(text), mVersion(version) {}

  void OnAttached() override
  {
    AddRow(0, {{"Puke Amp", "https://amp.puke.studio"}});
    AddRow(1, {{"NAM by Steven Atkinson", "https://www.neuralampmodeler.com/"}, {"/ adapted by the RATLab"}});
    AddRow(2, {{mVersion.Get()}});
    AddRow(3, {{"License", "https://github.com/ElectricGuitarInnovationLab/Puke-Amp/blob/main/LICENSE"},
               {"and"},
               {"Third party notices", "https://github.com/ElectricGuitarInnovationLab/Puke-Amp/blob/main/THIRD_PARTY.md"}});
  }

private:
  struct Segment
  {
    const char* label;
    const char* url = nullptr;
  };

  void AddRow(int index, std::initializer_list<Segment> segments)
  {
    using namespace iplug::igraphics;
    auto row = GetRECT().SubRectVertical(4, index);
    const float gap = mText.mSize * 0.3f;
    const auto width = [this](const char* label) {
      IRECT bounds;
      GetUI()->MeasureText(mText, label, bounds);
      return bounds.W();
    };
    if (mText.mAlign == EAlign::Center)
    {
      float total = -gap;
      for (const auto& segment : segments)
        total += width(segment.label) + gap;
      row = row.GetCentredInside(total, row.H());
    }

    for (const auto& segment : segments)
    {
      const auto bounds = row.ReduceFromLeft(width(segment.label));
      if (segment.url)
        AddChildControl(new IURLControl(bounds, segment.label, segment.url, mText, COLOR_TRANSPARENT,
                                        PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      else
        AddChildControl(new ITextControl(bounds, segment.label, mText));
      row.L += gap;
    }
  }

  IText mText;
  WDL_String mVersion;
};
} // namespace nam_ui
