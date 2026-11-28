#include "PukeAcoustic.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "UITheme.h"
#include "IPlugPaths.h"
#include "IRValidation.h"
#include <fstream>
#include <sstream>
#include <map>

namespace fs = std::filesystem;
namespace
{
const IColor panelBrown(255, 25, 22, 20);

enum { LevelA, PanA, LevelB, PanB, InvertB, Bypass, DryMix, NumParams };

constexpr char externalPrefix[] = "external://";
constexpr char libraryPrefix[] = "library://";
constexpr char irCategory[] = "IR";
constexpr int stateVersion = 2;
using nam_library::PathString;

std::vector<fs::path> ListFiles(const fs::path& root, const char* extension)
{
  std::vector<fs::path> files;
  if (root.empty()) return files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec))
  {
    const auto name = PathString(it->path().filename());
    if (name.rfind(".import-", 0) == 0)
      it.disable_recursion_pending();
    else
    {
      std::error_code fileError;
      auto suffix = PathString(it->path().extension());
      std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (suffix == extension && it->is_regular_file(fileError)) files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string DisplayName(const std::string& id)
{
  if (id.empty()) return "None";
  const auto stem = PathString(fs::u8path(id).stem());
  return stem.rfind("Guitar_IR_", 0) == 0 ? stem.substr(10) : stem;
}

struct CaptureInfo
{
  std::string guitar, microphone, style, position;

  std::string Label() const
  {
    auto label = guitar + " · " + microphone + " · " + style;
    if (!position.empty()) label += " · " + position;
    return label;
  }
};

std::string Lowercase(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

CaptureInfo ParseCapture(const std::string& id)
{
  const auto stem = PathString(fs::u8path(id).stem());
  std::istringstream fields(stem);
  CaptureInfo capture;
  if (!std::getline(fields, capture.guitar, '_') || capture.guitar.empty() ||
      !std::getline(fields, capture.microphone, '_') || capture.microphone.empty() ||
      !std::getline(fields, capture.style, '_') || capture.style.empty())
    return {};
  if (!fields.eof())
  {
    std::getline(fields, capture.position);
    if (capture.position != "Left" && capture.position != "Right" && capture.position != "Center")
      return {};
  }
  return capture;
}

std::string CaptureLabel(const std::string& id)
{
  const auto capture = ParseCapture(id);
  return capture.microphone.empty() ? DisplayName(id) : capture.Label();
}

void DrawMenu(IGraphics& g, const IRECT& bounds, const std::string& text)
{
  g.FillRoundRect(IColor(245, 53, 47, 40), bounds, 5.f);
  auto label = text + "  ▾";
  auto textBounds = bounds.GetHPadded(-6.f);
  IRECT measured;
  const auto style = IText(15, COLOR_WHITE);
  g.MeasureText(style, label.c_str(), measured);
  if (!text.empty() && measured.W() > textBounds.W())
  {
    auto shortened = text;
    do
    {
      size_t end = shortened.size() - 1;
      while (end > 0 && (static_cast<unsigned char>(shortened[end]) & 0xc0) == 0x80) --end;
      shortened.resize(end);
      label = shortened + "…  ▾";
      g.MeasureText(style, label.c_str(), measured);
    } while (!shortened.empty() && measured.W() > textBounds.W());
  }
  g.DrawText(style, label.c_str(), textBounds);
}

class CaptureMenu final : public IControl
{
public:
  CaptureMenu(const IRECT& bounds, PukeAcoustic& plugin, int slot)
  : IControl(bounds), mPlugin(plugin), mSlot(slot) {}

  void Draw(IGraphics& g) override
  {
    DrawMenu(g, mRECT, CaptureLabel(mPlugin.Selection(mSlot)));
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    mChoices = {""};
    mMenu.AddItem("None")->SetChecked(mPlugin.Selection(mSlot).empty());
    mMenu.AddItem("Load WAV…");
    mMenu.AddItem("Import WAV to Library…");
    mMenu.AddItem("Import IR Folder…");
    auto* factory = new IPopupMenu();
    auto* library = new IPopupMenu();
    const auto selected = mPlugin.Selection(mSlot);
    auto addCapture = [&](IPopupMenu& menu, const std::string& id, const std::string& label) {
      menu.AddItem(new IPopupMenu::Item(label.c_str(), IPopupMenu::Item::kNoFlags,
                                        static_cast<int>(mChoices.size())))->SetChecked(id == selected);
      mChoices.push_back(id);
    };
    for (const auto& id : mPlugin.Library())
      addCapture(*factory, id, id);
    for (const auto& path : ListFiles(mPlugin.IRLibraryDirectory(), ".wav"))
    {
      const auto relative = PathString(path.lexically_relative(mPlugin.IRLibraryDirectory()), true);
      addCapture(*library, std::string(libraryPrefix) + irCategory + "/" + relative, relative);
    }
    mMenu.AddItem("Factory IRs", factory);
    mMenu.AddItem("My IR Library", library);
    mMenu.RemoveEmptySubmenus();
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    auto* item = menu ? menu->GetChosenItem() : nullptr;
    if (!item) return;
    if (menu != &mMenu || menu->GetChosenItemIdx() == 0)
    {
      const int choice = menu == &mMenu ? 0 : item->GetTag();
      if (choice >= 0 && choice < static_cast<int>(mChoices.size()) && !mPlugin.Choose(mSlot, mChoices[choice]))
        GetUI()->ShowMessageBox("The selected IR could not be loaded.", "IR load", kMB_OK);
      return;
    }

    const int action = menu->GetChosenItemIdx();
    WDL_String file, directory;
    auto completion = [plugin = &mPlugin, slot = mSlot, action](const WDL_String& file, const WDL_String& directory) {
      if (action == 3)
        plugin->ImportIRs(directory.Get());
      else if (action == 2)
        plugin->ImportIRs(file.Get());
      else if (file.GetLength() && !plugin->LoadIR(slot, file.Get()) && plugin->GetUI())
        plugin->GetUI()->ShowMessageBox("Choose a readable mono WAV impulse response.", "IR load", kMB_OK);
    };
    if (action == 3)
      GetUI()->PromptForDirectory(directory, completion);
    else
      GetUI()->PromptForFile(file, directory, EFileAction::Open, "wav", completion);
  }

private:
  PukeAcoustic& mPlugin;
  int mSlot;
  IPopupMenu mMenu;
  std::vector<std::string> mChoices;
};

class PresetMenu final : public IControl
{
public:
  PresetMenu(const IRECT& bounds, PukeAcoustic& plugin) : IControl(bounds), mPlugin(plugin) {}

  void Draw(IGraphics& g) override
  {
    DrawMenu(g, mRECT, mPlugin.GetPresetNameText());
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    mMenu.AddItem("Open Preset…");
    mMenu.AddItem("Save Preset As…");
    mFiles = ListFiles(mPlugin.PresetDirectory(), ".fxp");
    if (!mFiles.empty()) mMenu.AddSeparator();
    for (size_t i = 0; i < mFiles.size(); ++i)
    {
      const auto label = PathString(mFiles[i].lexically_relative(mPlugin.PresetDirectory()).replace_extension(), true);
      mMenu.AddItem(new IPopupMenu::Item(label.c_str(), IPopupMenu::Item::kNoFlags, static_cast<int>(i)));
    }
    GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    auto* item = menu ? menu->GetChosenItem() : nullptr;
    if (!item) return;
    if (item->GetTag() >= 0)
    {
      if (!mPlugin.LoadPreset(mFiles.at(item->GetTag())))
        GetUI()->ShowMessageBox("The preset or one of its IRs could not be loaded.", "Preset", kMB_OK);
      return;
    }

    const bool save = menu->GetChosenItemIdx() == 1;
    std::error_code ec;
    if (save && !mPlugin.PresetDirectory().empty()) fs::create_directories(mPlugin.PresetDirectory(), ec);
    WDL_String file(save ? "Acoustic Preset.fxp" : "");
    WDL_String directory(PathString(mPlugin.PresetDirectory()).c_str());
    GetUI()->PromptForFile(file, directory, save ? EFileAction::Save : EFileAction::Open, "fxp",
      [plugin = &mPlugin, save](const WDL_String& file, const WDL_String&) {
        if (!file.GetLength()) return;
        auto path = fs::u8path(file.Get());
        if (save && path.extension().empty()) path += ".fxp";
        const bool success = save ? plugin->SavePreset(path) : plugin->LoadPreset(path);
        if (!success && plugin->GetUI())
          plugin->GetUI()->ShowMessageBox(save ? "The preset could not be saved." :
            "The preset or one of its IRs could not be loaded.", "Preset", kMB_OK);
      });
  }

private:
  PukeAcoustic& mPlugin;
  IPopupMenu mMenu;
  std::vector<fs::path> mFiles;
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

  GetParam(DryMix)->InitDouble("Dry Mix", 0., 0., 100., 1., "%");

  WDL_String support;
  AppSupportPath(support, false);
  if (support.GetLength())
  {
    const auto userRoot = fs::u8path(support.Get()) / BUNDLE_NAME;
    mLibraryRoot = userRoot / "Library";
    mPresetDirectory = userRoot / "Presets";
  }

  WDL_String resources;
#ifdef OS_WIN
  BundleResourcePath(resources, gHINSTANCE);
#elif defined OS_MAC
  BundleResourcePath(resources, GetBundleID());
#endif
  if (resources.GetLength())
  {
    mRoot = fs::u8path(resources.Get()) / "Models" / "Instruments";
    for (const auto& path : ListFiles(mRoot, ".wav"))
      mLibrary.push_back(PathString(path.lexically_relative(mRoot), true));
    std::sort(mLibrary.begin(), mLibrary.end());
    auto initial = mLibrary.end();
    for (auto it = mLibrary.begin(); it != mLibrary.end(); ++it)
    {
      const auto capture = ParseCapture(*it);
      if (capture.style != "Universal") continue;
      if (initial == mLibrary.end()) initial = it;
      if (capture.position == "Center")
      {
        initial = it;
        break;
      }
    }
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
    if (!mRoot.empty())
    {
      for (const auto& path : ListFiles(mRoot.parent_path() / "GUI" / "mics", ".png"))
      {
        const auto mic = Lowercase(PathString(path.stem()));
        const auto bitmap = g->LoadBitmap(PathString(path).c_str(), 1, false, 1);
        if (!bitmap.IsValid()) continue;
        IRECT visible(0, 0, bitmap.W(), bitmap.H());
        // Fit the visible microphone within each supplied transparent canvas.
        if (mic == "m149" && bitmap.W() == 3892 && bitmap.H() == 2160)
          visible = IRECT(1575, 0, 2304, 2117);
        else if (mic == "c414" && bitmap.W() == 3892 && bitmap.H() == 2160)
          visible = IRECT(1616, 0, 2305, 2009);
        microphones.emplace(mic, MicrophoneArt{bitmap, visible});
      }
    }
    g->AttachControl(new PresetMenu(IRECT(36, 78, 352, 108), *this));
    const auto style = DEFAULT_STYLE.WithColor(kBG, COLOR_TRANSPARENT)
      .WithLabelText(DEFAULT_LABEL_TEXT.WithFGColor(COLOR_WHITE))
      .WithValueText(DEFAULT_VALUE_TEXT.WithFGColor(COLOR_WHITE));
    const auto buttonStyle = style.WithLabelText(DEFAULT_LABEL_TEXT.WithFGColor(panelBrown))
      .WithValueText(DEFAULT_VALUE_TEXT.WithFGColor(panelBrown));
    const auto toggleStyle = buttonStyle.WithColor(kBG, IColor(255, 207, 195, 180));
    g->AttachControl(new ILambdaControl(IRECT(372, 58, PLUG_WIDTH, PLUG_HEIGHT),
      [this, microphones](ILambdaControl*, IGraphics& ctx, IRECT& bounds) {
        const std::array<std::string, 2> ids{Selection(0), Selection(1)};
        const bool paired = !ids[0].empty() && !ids[1].empty();
        for (int slot = 0; slot < 2; ++slot)
        {
          if (ids[slot].empty()) continue;
          const auto capture = ParseCapture(ids[slot]);
          if (capture.microphone.empty()) continue;
          auto area = paired ? bounds.SubRectHorizontal(2, slot) : bounds;
          auto card = area.ReduceFromBottom(108.f).GetPadded(-8.f);
          ctx.FillRoundRect(panelBrown.WithOpacity(0.94f), card, 8.f);
          card = card.GetHPadded(-12.f);
          const auto detail = IText(14, COLOR_WHITE, "Roboto-Regular", EAlign::Near);
          const auto title = IText(20, IColor(255, 239, 207, 159), "Roboto-Bold", EAlign::Near);
          const auto heading = std::string(slot ? "B  /  " : "A  /  ") + capture.microphone;
          ctx.DrawText(title, heading.c_str(), card.ReduceFromTop(30.f));
          ctx.DrawText(detail, ("Guitar   " + capture.guitar).c_str(), card.ReduceFromTop(19.f));
          ctx.DrawText(detail, ("Style   " + capture.style).c_str(), card.ReduceFromTop(19.f));
          if (!capture.position.empty())
            ctx.DrawText(detail, ("Mic position   " + capture.position).c_str(), card.ReduceFromTop(19.f));

          const auto it = microphones.find(Lowercase(capture.microphone));
          if (it == microphones.end()) continue;
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
      const float y = 120.f + 126.f * i;
      g->AttachControl(new ITextControl(IRECT(34,y,94,y+32), i ? "IR B" : "IR A", IText(18, COLOR_WHITE)));
      g->AttachControl(new CaptureMenu(IRECT(98,y,352,y+32), *this, i));
      g->AttachControl(new IVKnobControl(IRECT(38,y+36,131,y+118), i ? LevelB : LevelA, "Level", style));
      g->AttachControl(new IVKnobControl(IRECT(140,y+36,233,y+118), i ? PanB : PanA, "Pan", style));
    }
    g->AttachControl(new IVKnobControl(IRECT(247, 282, 350, 364), DryMix, "Dry Mix", style));
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
        ctx.DrawText(text, ((i ? "IR B: " : "IR A: ") + CaptureLabel(Selection(i))).c_str(), bounds.SubRectVertical(2,i));
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

std::filesystem::path PukeAcoustic::IRLibraryDirectory() const
{
  return mLibraryRoot.empty() ? fs::path{} : nam_library::AssetLibrary(mLibraryRoot).Category(irCategory);
}

std::string PukeAcoustic::PathFor(const std::string& id) const
{
  if (id.empty()) return {};
  if (id.compare(0, sizeof(libraryPrefix) - 1, libraryPrefix) == 0 && !mLibraryRoot.empty())
  {
    const auto path = nam_library::AssetLibrary(mLibraryRoot).Resolve(id);
    if (nam_library::AssetLibrary::IsWithin(path, IRLibraryDirectory())) return PathString(path);
  }
  else if (id.compare(0, sizeof(externalPrefix) - 1, externalPrefix) == 0)
  {
    const auto path = fs::u8path(id.substr(sizeof(externalPrefix) - 1));
    if (path.is_absolute()) return PathString(path);
  }
  else if (std::binary_search(mLibrary.begin(), mLibrary.end(), id))
    return PathString(mRoot / fs::u8path(id));

  throw std::runtime_error("Invalid impulse response reference");
}

std::string PukeAcoustic::Selection(int slot) const
{
  return RelativeId(mMixer.GetPaths()[slot]);
}

std::string PukeAcoustic::RelativeId(const std::string& path) const
{
  if (path.empty()) return {};
  const auto asset = fs::u8path(path);
  if (!mRoot.empty())
  {
    const auto id = PathString(asset.lexically_relative(mRoot), true);
    if (std::binary_search(mLibrary.begin(), mLibrary.end(), id)) return id;
  }
  if (!mLibraryRoot.empty())
  {
    const auto relative = asset.lexically_relative(IRLibraryDirectory());
    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
      return std::string(libraryPrefix) + irCategory + "/" + PathString(relative, true);
  }
  return externalPrefix + path;
}

bool PukeAcoustic::Choose(int slot, const std::string& id)
{
  try
  {
    nam_cabinet::Mixer::Changes change{};
    change[slot] = PathFor(id);
    if (mMixer.Select(change).status != nam_cabinet::Mixer::Status::SUCCESS) return false;
  }
  catch (const std::exception&)
  {
    return false;
  }
  if (GetUI())
  {
    DirtyParametersFromUI();
    GetUI()->SetAllControlsDirty();
  }
  return true;
}

bool PukeAcoustic::LoadIR(int slot, const char* path)
{
  return Choose(slot, externalPrefix + std::string(path));
}

void PukeAcoustic::ImportIRs(const char* path)
{
  if (!*path || !GetUI()) return;
  if (mImport.valid() || mLibraryRoot.empty())
  {
    GetUI()->ShowMessageBox(mImport.valid() ? "An import is already running." : "User library is unavailable.",
                            "Library import", kMB_OK);
    return;
  }
  try
  {
    const auto source = fs::u8path(path);
    const nam_library::AssetLibrary library(mLibraryRoot);
    mImport = std::async(std::launch::async, [source, library] {
      return library.Import(source, irCategory, "wav", nam_library::ValidateIR);
    });
  }
  catch (const std::exception& error)
  {
    GetUI()->ShowMessageBox(error.what(), "Library import", kMB_OK);
  }
}

bool PukeAcoustic::SavePreset(const fs::path& path)
{
  IByteChunk state;
  IByteChunk::InitChunkWithIPlugVer(state);
  if (!SerializeState(state)) return false;
  // Standard VST chunk preset header, followed by the same state used for host recall.
  const uint32_t fields[] = {'CcnK', static_cast<uint32_t>(state.Size()) + 52u, 'FPCh', kFXPVersionNum,
    static_cast<uint32_t>(GetUniqueID()), static_cast<uint32_t>(GetPluginVersion(true)),
    static_cast<uint32_t>(NParams())};
  std::ofstream file(path, std::ios::binary);
  for (auto field : fields)
  {
    field = WDL_bswap_if_le(field);
    file.write(reinterpret_cast<const char*>(&field), sizeof(field));
  }
  const char name[28] = "Puke Amp Acoustic";
  file.write(name, sizeof(name));
  const auto size = WDL_bswap_if_le(static_cast<uint32_t>(state.Size()));
  file.write(reinterpret_cast<const char*>(&size), sizeof(size));
  file.write(reinterpret_cast<const char*>(state.GetData()), state.Size());
  file.close();
  if (!file) return false;
  mPresetName = DisplayName(PathString(path));
  if (GetUI()) GetUI()->SetAllControlsDirty();
  return true;
}

bool PukeAcoustic::LoadPreset(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  uint32_t header[15]{};
  if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
  if (WDL_bswap_if_le(header[0]) != 'CcnK' || WDL_bswap_if_le(header[2]) != 'FPCh' ||
      WDL_bswap_if_le(header[3]) != kFXPVersionNum ||
      WDL_bswap_if_le(header[4]) != static_cast<uint32_t>(GetUniqueID())) return false;
  const auto size = WDL_bswap_if_le(header[14]);
  // This preset format carries two paths and a small fixed parameter block.
  if (!size || size > 1024 * 1024) return false;
  IByteChunk state;
  state.Resize(static_cast<int>(size));
  if (!file.read(reinterpret_cast<char*>(state.GetData()), size)) return false;
  int pos = 0;
  IByteChunk::GetIPlugVerFromChunk(state, pos);
  if (UnserializeState(state, pos) < 0) return false;
  mPresetName = DisplayName(PathString(path));
  OnRestoreState();
  InformHostOfPresetChange();
  if (GetUI())
  {
    DirtyParametersFromUI();
    GetUI()->SetAllControlsDirty();
  }
  return true;
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
  mMixer.Process(input, outputs, channels, frames, mix, !GetParam(Bypass)->Bool(),
                 GetParam(DryMix)->Value() / 100.);
}

void PukeAcoustic::OnIdle()
{
  mMixer.Collect();
  auto* ui = GetUI();
  if (!ui) return;
  if (mRefresh.exchange(false)) ui->SetAllControlsDirty();
  if (mImport.valid() && mImport.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
  {
    const auto result = mImport.get();
    std::ostringstream message;
    message << "Imported " << result.imported << " IR(s) into My IR Library.";
    if (result.skipped) message << "\nSkipped " << result.skipped << " file(s) with other extensions.";
    for (const auto& error : result.errors) message << "\n" << error;
    ui->ShowMessageBox(message.str().c_str(), "Library import", kMB_OK);
  }
}

bool PukeAcoustic::SerializeState(IByteChunk& chunk) const
{
  chunk.Put(&stateVersion);
  for (const auto& path : mMixer.GetPaths()) chunk.PutStr(RelativeId(path).c_str());
  return SerializeParams(chunk);
}

int PukeAcoustic::UnserializeState(const IByteChunk& chunk, int pos)
{
  int version = 0;
  pos = chunk.Get(&version, pos);
  if (pos < 0 || (version != 1 && version != stateVersion)) return -1;
  nam_cabinet::Mixer::Changes changes;
  try
  {
    for (auto& change : changes)
    {
      int length = 0;
      const int start = chunk.Get(&length, pos);
      if (start < 0 || length < 0 || length > chunk.Size() - start) return -1;
      WDL_String id;
      pos = chunk.GetStr(id, pos);
      if (pos < 0 || id.GetLength() != length) return -1;
      if (version == 1 && length && !std::binary_search(mLibrary.begin(), mLibrary.end(), id.Get()))
        return -1;
      change = PathFor(id.Get());
    }
  }
  catch (const std::exception&)
  {
    return -1;
  }
  const int count = version == 1 ? DryMix : NumParams;
  std::array<double, NumParams> values{};
  for (int i = 0; i < count; ++i)
  {
    pos = chunk.Get(&values[i], pos);
    if (pos < 0 || !std::isfinite(values[i])) return -1;
  }
  if (mMixer.Select(changes).status != nam_cabinet::Mixer::Status::SUCCESS) return -1;
  for (int i = 0; i < NumParams; ++i) GetParam(i)->Set(values[i]);
  OnParamReset(kPresetRecall);
  mRefresh = true;
  return pos;
}
