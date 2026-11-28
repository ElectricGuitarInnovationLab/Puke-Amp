#include <cassert>
#include <cmath>
#include "../config.h"
#include "../EditorSize.h"
#include "IPlugEditorDelegate.h"

// Compile against the real framework API: an unpatched dependency must fail
// here, rather than letting helper-only tests miss a broken plugin override.
class ResizeOverrideCheck : public iplug::IEditorDelegate
{
public:
  bool ConstrainEditorResize(int& width, int& height) const override
  {
    return nam_ui::ConstrainEditorSize(width, height, GetEditorWidth(), GetEditorHeight());
  }
};

int main()
{
  auto check = [](int width, int height, int expectedWidth, int expectedHeight) {
    const bool alreadyValid = width == expectedWidth && height == expectedHeight;
    assert(nam_ui::ConstrainEditorSize(width, height, 600, 400) == alreadyValid);
    assert(width == expectedWidth && height == expectedHeight);
    assert(nam_ui::ConstrainEditorSize(width, height, width, height));
  };
  check(600, 400, 600, 400);
  check(900, 400, 900, 600); // Drag right edge.
  check(600, 600, 900, 600); // Drag bottom edge.
  check(450, 400, 450, 300); // Shrink using either axis.
  check(600, 300, 450, 300);
  check(901, 601, 903, 602); // Integral aspect-ratio steps.
  check(10, 10, 300, 200);
  check(9999, 9999, 2400, 1600);
  check(0, 0, 300, 200);

  // All accepted sizes must fill the native child view exactly, including
  // values prone to float-to-integer truncation in IGraphics::WindowWidth.
  for (int width = PLUG_MIN_WIDTH; width <= PLUG_MAX_WIDTH; width += 3)
  {
    int w = width, h = width * 2 / 3;
    assert(nam_ui::ConstrainEditorSize(w, h, 600, 400));
    const float scale = std::min(4.f, std::nextafter(float(w) / PLUG_WIDTH, float(PLUG_MAX_WIDTH)));
    assert(int(PLUG_WIDTH * scale) == w);
    assert(int(PLUG_HEIGHT * scale) == h);
  }

  for (int w = -100; w < 3000; w += 37)
    for (int h = -100; h < 2000; h += 29)
    {
      int constrainedW = w, constrainedH = h;
      nam_ui::ConstrainEditorSize(constrainedW, constrainedH, 900, 600);
      assert(constrainedW * 2 == constrainedH * 3);
      assert(constrainedW >= PLUG_MIN_WIDTH && constrainedW <= PLUG_MAX_WIDTH);
      assert(constrainedH >= PLUG_MIN_HEIGHT && constrainedH <= PLUG_MAX_HEIGHT);
      assert(nam_ui::ConstrainEditorSize(constrainedW, constrainedH, 900, 600));
    }
}
