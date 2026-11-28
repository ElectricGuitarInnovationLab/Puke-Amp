#include "PukeAcoustic.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "UITheme.h"
#include "IPlugPaths.h"
#include <map>

namespace fs = std::filesystem;
namespace
{
const IColor panelBrown(255, 25, 22, 20);

enum { LevelA, PanA, LevelB, PanB, InvertB, Bypass, NumParams };

std::string DisplayName(const std::string& id)
{
  if (id.empty()) return "None";
  auto name = fs::u8path(id);
  name.replace_extension();
  auto label = name.generic_u8string();
  const auto prefix = label.rfind("Guitar_IR_");
  if (prefix != std::string::npos) label.erase(prefix, 10);
  return {label.begin(), label.end()};
}

class CaptureMenu final : public IControl
{
public:
  CaptureMenu(const IRECT& bounds, PukeAcoustic& plugin, int slot)
  : IControl(bounds), mPlugin(plugin), mSlot(slot) {}

  void Draw(IGraphics& g) override
  {
    const auto label = DisplayName(mPlugin.Selection(mSlot)) + "  ▾";
    g.FillRoundRect(IColor(245, 53, 47, 40), mRECT, 5.f);
    g.DrawText(IText(18, COLOR_WHITE), mFailed ? "Load failed • select again" : label.c_str(), mRECT);
  }
  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    mMenu.AddItem("None");
    for (const auto& id : mPlugin.Library()) mMenu.AddItem(DisplayName(id).c_str());
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }
  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (!menu) return;
    const int i = menu->GetChosenItemIdx();
    if (i < 0 || i > static_cast<int>(mPlugin.Library().size())) return;
    mFailed = !mPlugin.Choose(mSlot, i ? mPlugin.Library()[i-1] : "");
    if (!mFailed) { mPlugin.DirtyParametersFromUI(); GetUI()->SetAllControlsDirty(); }
    SetDirty(false);
  }
private:
  PukeAcoustic& mPlugin;
  int mSlot;
  IPopupMenu mMenu;
  bool mFailed = false;
};
}

PukeAcoustic::PukeAcoustic(const InstanceInfo& info) : Plugin(info, MakeConfig(NumParams, 1))
{
  GetParam(LevelA)->InitGain("A Level", 0., -60., 12., 0.1);
  GetParam(PanA)->InitDouble("A Pan", 0., -100., 100., 1., "%");
  GetParam(LevelB)->InitGain("B Level", 0., -60., 12., 0.1);
  GetParam(PanB)->InitDouble("B Pan", 0., -100., 100., 1., "%");
  GetParam(InvertB)->InitBool("B Invert polarity", false);
  GetParam(Bypass)->InitBool("IR Bypass", false);

  WDL_String resources;
#ifdef OS_WIN
  BundleResourcePath(resources, gHINSTANCE);
#elif defined OS_MAC
  BundleResourcePath(resources, GetBundleID());
#endif
  if (resources.GetLength())
  {
    mRoot = fs::u8path(resources.Get()) / "Models" / "Instruments";
    std::error_code ec;
    for (fs::recursive_directory_iterator it(mRoot, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && it->path().extension() == ".wav")
      {
        const auto id = it->path().lexically_relative(mRoot).generic_u8string();
        mLibrary.emplace_back(id.begin(), id.end());
      }
    std::sort(mLibrary.begin(), mLibrary.end());
    const auto initial = std::find_if(mLibrary.begin(), mLibrary.end(), [](const std::string& id) {
      const auto stem = fs::u8path(id).stem().string();
      return stem.size() >= 10 && stem.compare(stem.size() - 10, 10, "_Universal") == 0;
    });
    if (initial != mLibrary.end()) Choose(0, *initial);
  }

  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };
  mLayoutFunc = [&](IGraphics* g) {
    g->LoadFont("Roboto-Regular", ROBOTO_FN);
    g->LoadFont("Roboto-Bold", "Roboto-Bold.ttf");
    const auto image = g->LoadBitmap(GUITAR_FN);
    g->AttachControl(new ILambdaControl(g->GetBounds(), [image](ILambdaControl*, IGraphics& ctx, IRECT& bounds) {
      ctx.FillRect(IColor(255, 30, 26, 23), bounds);
      if (image.IsValid()) ctx.DrawFittedBitmap(image, bounds);
      ctx.FillRoundRect(panelBrown.WithOpacity(220.f / 255.f), IRECT(20, 20, 372, 430), 9.f);
    }, 0, false, false, kNoParameter, true));
    const auto logo = g->LoadSVG("puke.svg");
    g->AttachControl(new ISVGButtonControl(IRECT(38, 30, 122, 73), [](IControl* c) {
      c->GetUI()->OpenURL("https://puke.studio");
    }, logo, logo));
    g->AttachControl(new IVLabelControl(IRECT(125, 30, 368, 73), "Puke Amp Acoustic",
      nam_ui::TitleStyle()))->SetIgnoreMouse(true);

    struct MicrophoneArt
    {
      IBitmap bitmap;
      IRECT visible;
    };
    std::map<std::string, MicrophoneArt> microphones;
    for (const auto& id : mLibrary)
    {
      const auto stem = fs::u8path(id).stem().u8string();
      const auto first = stem.find('_'), last = stem.rfind('_');
      if (first == last) continue;
      const auto mic = stem.substr(first + 1, last - first - 1);
      auto path = mRoot.parent_path() / "GUI" / "mics" / fs::u8path(mic + ".png");
      std::error_code ec;
      if (fs::is_regular_file(path, ec))
      {
        const auto bitmap = g->LoadBitmap(path.u8string().c_str(), 1, false, 1);
        if (!bitmap.IsValid()) continue;
        IRECT visible(0, 0, bitmap.W(), bitmap.H());
        // The supplied M149 sits inside a wide transparent export canvas.
        if (mic == "m149" && bitmap.W() == 3892 && bitmap.H() == 2160)
          visible = IRECT(1575, 0, 2304, 2117);
        microphones.emplace(id, MicrophoneArt{bitmap, visible});
      }
    }
    const auto style = DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT)
      .WithLabelText(DEFAULT_LABEL_TEXT.WithFGColor(COLOR_WHITE))
      .WithValueText(DEFAULT_VALUE_TEXT.WithFGColor(COLOR_WHITE));
    const auto buttonStyle = style.WithLabelText(DEFAULT_LABEL_TEXT.WithFGColor(panelBrown))
      .WithValueText(DEFAULT_VALUE_TEXT.WithFGColor(panelBrown));
    const auto toggleStyle = buttonStyle.WithColor(kBG, IColor(255, 207, 195, 180));
    g->AttachControl(new ILambdaControl(IRECT(372, 58, PLUG_WIDTH, PLUG_HEIGHT),
      [this, microphones](ILambdaControl*, IGraphics& ctx, IRECT& bounds) {
        const auto a = microphones.find(Selection(0));
        const auto b = microphones.find(Selection(1));
        const bool paired = a != microphones.end() && b != microphones.end();
        int column = 0;
        for (const auto it : {a, b})
        {
          if (it == microphones.end()) continue;
          const auto area = paired ? bounds.SubRectHorizontal(2, column++) : bounds;
          const auto& art = it->second;
          const auto& src = art.visible;
          const float scale = std::min(area.W() / src.W(), area.H() / src.H());
          const auto dest = area.GetCentredInside(src.W() * scale, src.H() * scale);
          ctx.PathTransformSave();
          ctx.PathTransformTranslate(dest.L, dest.T);
          ctx.PathTransformScale(scale);
          ctx.DrawBitmap(art.bitmap, IRECT(0, 0, src.W(), src.H()), int(src.L), int(src.T), nullptr);
          ctx.PathTransformRestore();
        }
      }, 0, false, false, kNoParameter, true));
    for (int i = 0; i < 2; ++i)
    {
      const float y = 86.f + 145.f * i;
      g->AttachControl(new ITextControl(IRECT(34,y,94,y+32), i ? "IR B" : "IR A", IText(18, COLOR_WHITE)));
      g->AttachControl(new CaptureMenu(IRECT(98,y,352,y+32), *this, i));
      g->AttachControl(new IVKnobControl(IRECT(70,y+38,184,y+132), i ? LevelB : LevelA, "Level", style));
      g->AttachControl(new IVKnobControl(IRECT(205,y+38,319,y+132), i ? PanB : PanA, "Pan", style));
    }
    g->AttachControl(new IVToggleControl(IRECT(36,375,185,421), InvertB, "B polarity", toggleStyle, "Normal", "Inverted"));
    g->AttachControl(new IVToggleControl(IRECT(200,375,352,421), Bypass, "IR processing", toggleStyle, "Active", "Bypassed"));
    const int settingsStart = g->NControls() + 1;
    auto showSettings = [g, settingsStart](bool open) {
      for (int n = 1; n < g->NControls(); ++n)
        g->GetControl(n)->Hide(open ? n < settingsStart : n >= settingsStart);
      g->SetAllControlsDirty();
    };
    g->AttachControl(new IVButtonControl(IRECT(680,20,780,54), [showSettings](IControl*) {
      showSettings(true);
    }, "Settings", buttonStyle));
    g->AttachControl(new ILambdaControl(g->GetBounds(), [image](ILambdaControl*, IGraphics& ctx, IRECT& bounds) {
      if (image.IsValid()) ctx.DrawFittedBitmap(image, bounds);
      ctx.FillRoundRect(IColor(228,25,22,20), IRECT(20,20,605,430), 9.f);
    }, 0, false, false, kNoParameter, true));
    const auto text = IText(16, COLOR_WHITE);
    auto label = [g, text](int top, const char* value) {
      g->AttachControl(new ITextControl(IRECT(32,top,595,top+28), value, text));
    };
    label(34, PLUG_NAME " Settings");
    label(79, "Input calibration: direct mono piezo input");
    label(109, "Output mode: raw IR mix");
    g->AttachControl(new ILambdaControl(IRECT(32,144,595,199), [this, text](ILambdaControl*, IGraphics& ctx, IRECT& bounds) {
      for (int i = 0; i < 2; ++i)
        ctx.DrawText(text, ((i ? "IR B: " : "IR A: ") + DisplayName(Selection(i))).c_str(), bounds.SubRectVertical(2,i));
    }, 0, false, false, kNoParameter, true));
    const auto rat = g->LoadSVG("rat.svg");
    g->AttachControl(new ISVGButtonControl(IRECT(42,240,111,315), [](IControl* c) {
      c->GetUI()->OpenURL("https://theratlab.org");
    }, rat, rat));
    auto link = [g, text](int top, const char* name, const char* url) {
      g->AttachControl(new IURLControl(IRECT(122,top,575,top+24), name, url, text, COLOR_TRANSPARENT));
    };
    link(220, PLUG_NAME " / Neural Amp Modeler", "https://www.neuralampmodeler.com");
    label(246, "NAM by Steven Atkinson / adapted by the RATLab");
    WDL_String build;
    build.SetFormatted(128, "Version %s  %s  %s", PLUG_VERSION_STR, GetArchStr(), GetAPIStr());
    label(275, build.Get());
    link(308, "Plugin by Steven Atkinson & Oli Larkin", "https://github.com/sdatkinson/NeuralAmpModelerPlugin/graphs/contributors");
    link(343, "Third party notices", PLUG_URL_STR "/blob/HEAD/THIRD_PARTY.md");
    g->AttachControl(new IVButtonControl(IRECT(680,20,780,54), [showSettings](IControl*) {
      showSettings(false);
    }, "Close", style));
    showSettings(false);
  };
}

std::string PukeAcoustic::PathFor(const std::string& id) const
{
  if (id.empty()) return {};
  const auto path = (mRoot / fs::u8path(id)).u8string();
  return {path.begin(), path.end()};
}

std::string PukeAcoustic::Selection(int slot) const
{
  return RelativeId(mMixer.GetPaths()[slot]);
}

std::string PukeAcoustic::RelativeId(const std::string& path) const
{
  if (path.empty()) return {};
  const auto id = fs::u8path(path).lexically_relative(mRoot).generic_u8string();
  return {id.begin(), id.end()};
}

bool PukeAcoustic::Choose(int slot, const std::string& id)
{
  nam_cabinet::Mixer::Changes change{};
  change[slot] = PathFor(id);
  return mMixer.Select(change).status == nam_cabinet::Mixer::Status::SUCCESS;
}

void PukeAcoustic::OnReset()
{
  mMixer.Reset(GetSampleRate(), GetBlockSize());
  SetTailSize(8191);
}

void PukeAcoustic::ProcessBlock(sample** inputs, sample** outputs, int frames)
{
  const int channels = NOutChansConnected();
  if (!frames || !channels) return;
  auto* input = NInChansConnected() ? inputs[0] : outputs[0];
  if (!NInChansConnected()) std::fill_n(input, frames, 0.);
  const double signB = GetParam(InvertB)->Bool() ? -1. : 1.;
  const std::array<nam_cabinet::Mixer::Mix, 2> mix{{
    {DBToAmp(GetParam(LevelA)->Value()), GetParam(PanA)->Value() / 100.},
    {signB * DBToAmp(GetParam(LevelB)->Value()), GetParam(PanB)->Value() / 100.}
  }};
  mMixer.Process(input, outputs, channels, frames, mix, !GetParam(Bypass)->Bool());
}

void PukeAcoustic::OnIdle()
{
  mMixer.Collect();
  if (mRefresh.exchange(false) && GetUI()) GetUI()->SetAllControlsDirty();
}

bool PukeAcoustic::SerializeState(IByteChunk& chunk) const
{
  const int version = 1;
  chunk.Put(&version);
  for (const auto& path : mMixer.GetPaths()) chunk.PutStr(RelativeId(path).c_str());
  return SerializeParams(chunk);
}

int PukeAcoustic::UnserializeState(const IByteChunk& chunk, int pos)
{
  int version = 0;
  pos = chunk.Get(&version, pos);
  if (pos < 0 || version != 1) return -1;
  nam_cabinet::Mixer::Changes changes;
  for (auto& change : changes)
  {
    int length = 0;
    const int start = chunk.Get(&length, pos);
    if (start < 0 || length < 0 || length > chunk.Size() - start) return -1;
    WDL_String id;
    pos = chunk.GetStr(id, pos);
    if (pos < 0) return -1;
    if (id.GetLength() && !std::binary_search(mLibrary.begin(), mLibrary.end(), id.Get())) return -1;
    change = PathFor(id.Get());
  }
  if (chunk.Size() - pos < static_cast<int>(NumParams * sizeof(double))) return -1;
  if (mMixer.Select(changes).status != nam_cabinet::Mixer::Status::SUCCESS) return -1;
  mRefresh = true;
  return UnserializeParams(chunk, pos);
}
