#pragma once

#include "CabinetCatalog.h"

// Included after NeuralAmpModelerControls.h. This page owns only editor state;
// the selected WAV continues through the existing IR loader and preset format.
class NAMCabinetPageControl : public IControl
{
public:
  NAMCabinetPageControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc load)
  : IControl(bounds), mLoad(load) {}

  void Open()
  {
    EnsureCatalog();
    auto* browser = Browser();
    if (browser)
      SyncFile(browser->CurrentFile());
    Hide(false);
    LoadArtwork();
    GetUI()->SetAllControlsDirty();
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* data) override
  {
    if (msgTag == kMsgTagLoadedIR && data)
    {
      mLoadFailed = false;
      SyncFile(std::string(static_cast<const char*>(data), dataSize));
    }
    else if (msgTag == kMsgTagClearIR)
    {
      mLoadFailed = false;
      SyncFile("");
    }
    else if (msgTag == kMsgTagLoadFailed)
    {
      mLoadFailed = true;
      SyncFile(mCurrentFile);
    }
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const IColor muted(255, 170, 175, 182);
    g.FillRect(IColor(255, 24, 26, 30), mRECT);
    Text(g, "CABINET & MICROPHONE", Box(24, 16, 500, 48), 20.f);
    Button(g, CloseBounds(), "Close");
    Button(g, LibraryBounds(), "My IRs / import...");
    Button(g, ClearBounds(), "Clear IR");
    if (!mError.empty())
    {
      Text(g, "Cabinet library unavailable", Box(25, 135, 575, 165), 18.f);
      Text(g, "Check Models/cabinet-library.json and its assets.", Box(25, 175, 575, 200), 14.f);
      Text(g, "Custom IRs are still available below.", Box(25, 205, 575, 230), 14.f);
      SetTooltip(mError.c_str());
      return;
    }
    if (mPreview < 0)
      return;
    const auto& recording = mCatalog.recordings[mPreview];
    const auto& cabinet = mCatalog.cabinets[recording.cabinet];
    const auto& mic = mCatalog.microphones[recording.microphone];
    const std::string labels[] = {cabinet.name, mic.name, std::to_string(recording.angle) + " degrees"};
    const char* headings[] = {"SPEAKER", "MICROPHONE", "OFFSET"};
    for (int i = 0; i < 3; ++i)
    {
      const auto bounds = MenuBounds(i);
      Text(g, headings[i], bounds.GetTranslated(0, -18).GetFromTop(16), 10.f, muted);
      Button(g, bounds, labels[i].c_str(), i == 0 ? 11.f : 12.f);
      Text(g, "+", bounds.GetFromRight(16), 12.f, muted);
    }

    const auto stage = StageBounds();
    g.FillRoundRect(IColor(255, 34, 37, 42), stage, 6.f);
    g.PathClipRegion(stage);
    DrawArtwork(g, mCabinetBitmap, cabinet.image, OriginX(), OriginY());
    const auto color = PluginColors::NAM_THEMECOLOR;
    const float step = Step();
    // The front-view cabinet uses an explicit two-axis placement projection:
    // right = distance from center; down = distance away from the grille.
    g.DrawLine(color.WithOpacity(0.7f), OriginX(), OriginY(), OriginX() + MaxCenter() * step, OriginY());
    g.DrawLine(color.WithOpacity(0.7f), OriginX(), OriginY(), OriginX(), OriginY() + MaxGrill() * step);
    for (auto index : mCatalog.Positions(recording.cabinet, recording.microphone, recording.angle))
    {
      const auto& position = mCatalog.recordings[index];
      g.FillCircle(COLOR_BLACK.WithOpacity(0.65f), X(position), Y(position), 4.f);
      g.DrawCircle(COLOR_WHITE.WithOpacity(0.8f), X(position), Y(position), 3.f);
    }
    DrawArtwork(g, mMicBitmap, mic.images.at(recording.angle), X(recording), Y(recording));
    g.FillCircle(COLOR_BLACK, X(recording), Y(recording), 5.f);
    g.DrawCircle(color, X(recording), Y(recording), 6.f, nullptr, 2.f);
    g.PathClipRegion();

    Text(g, "MIC POSITION", Box(355, 128, 572, 150), 12.f, muted);
    Text(g, (Inches(recording.center) + " from center").c_str(), Box(355, 156, 572, 182), 19.f);
    Text(g, (Inches(recording.grill) + " from grille").c_str(), Box(355, 184, 572, 210), 19.f);
    Text(g, "Right = from speaker center", Box(355, 223, 572, 242), 12.f, muted);
    Text(g, "Down = away from grille", Box(355, 243, 572, 262), 12.f, muted);
    Text(g, "Drag the mic or click a dot.", Box(355, 274, 572, 294), 12.f);
    Text(g, "Release to load. Arrow keys also move.", Box(345, 295, 582, 314), 11.f, muted);
    const char* status = mLoadFailed ? "IR failed to load; previous sound retained."
      : mDragging ? "Release to load this recorded position"
      : mLoaded == mPreview ? "Recorded position loaded"
      : mCurrentFile.empty() ? "Select a position to load an IR"
      : mLoaded < 0 ? "Custom IR loaded; select a position to replace it"
      : "Select a position to load an IR";
    Text(g, status, Box(24, 340, 576, 359), 12.f, muted);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (CloseBounds().Contains(x, y))
      Close();
    else if (LibraryBounds().Contains(x, y))
    {
      if (auto* browser = Browser())
        browser->OpenLibraryMenu(LibraryBounds());
    }
    else if (ClearBounds().Contains(x, y))
    {
      GetDelegate()->SendArbitraryMsgFromUI(kMsgTagClearIR);
      if (auto* browser = Browser())
        browser->ClearDisplayedFile();
    }
    else if (mError.empty() && mPreview >= 0)
    {
      for (int menu = 0; menu < 3; ++menu)
        if (MenuBounds(menu).Contains(x, y))
        {
          ShowMenu(menu);
          return;
        }
      if (StageBounds().Contains(x, y))
      {
        mDragging = true;
        const auto& r = mCatalog.recordings[mPreview];
        // Grab the image without jumping its capsule to the mouse. A click on
        // any recorded marker takes precedence over the image's hit rectangle.
        bool onDot = false;
        for (auto i : mCatalog.Positions(r.cabinet, r.microphone, r.angle))
          onDot |= std::hypot(x - X(mCatalog.recordings[i]), y - Y(mCatalog.recordings[i])) < 6.f;
        const auto micBounds = IRECT(X(r) - 15, Y(r) - 15, X(r) + (r.angle ? 85 : 15), Y(r) + 85);
        mDragX = !onDot && micBounds.Contains(x, y) ? x - X(r) : 0.f;
        mDragY = !onDot && micBounds.Contains(x, y) ? y - Y(r) : 0.f;
        PreviewAt(x - mDragX, y - mDragY);
      }
    }
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (mDragging)
      PreviewAt(x - mDragX, y - mDragY);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    if (mDragging)
    {
      mDragging = false;
      Commit();
    }
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      Close();
      return true;
    }
    if (mPreview < 0 || !mError.empty())
      return false;
    const int dx = key.VK == kVK_RIGHT ? 1 : key.VK == kVK_LEFT ? -1 : 0;
    const int dy = key.VK == kVK_DOWN ? 1 : key.VK == kVK_UP ? -1 : 0;
    if (!dx && !dy)
      return false;
    const auto current = mCatalog.recordings[mPreview];
    float best = std::numeric_limits<float>::max();
    for (auto index : mCatalog.Positions(current.cabinet, current.microphone, current.angle))
    {
      const auto& next = mCatalog.recordings[index];
      const float xDistance = next.center - current.center, yDistance = next.grill - current.grill;
      if ((dx && xDistance * dx > 0 && yDistance == 0) || (dy && yDistance * dy > 0 && xDistance == 0))
      {
        const float distance = std::abs(xDistance) + std::abs(yDistance);
        if (distance < best)
        {
          best = distance;
          mPreview = static_cast<int>(index);
        }
      }
    }
    if (best < std::numeric_limits<float>::max())
      Commit();
    return true;
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int valIdx) override
  {
    if (!menu || menu->GetChosenItemIdx() < 0 || mPreview < 0)
      return;
    const auto choice = static_cast<size_t>(menu->GetChosenItemIdx());
    if (choice >= mMenuValues.size())
      return;
    auto previous = mCatalog.recordings[mPreview];
    auto cabinet = previous.cabinet, microphone = previous.microphone;
    int angle = previous.angle;
    if (mMenuKind == 0) cabinet = mMenuValues[choice];
    if (mMenuKind == 1) microphone = mMenuValues[choice];
    if (mMenuKind == 2) angle = mMenuValues[choice];
    int nearest = mCatalog.Nearest(cabinet, microphone, angle, previous.center, previous.grill);
    if (nearest < 0)
    {
      // A newly chosen cabinet/mic may not offer the previous angle or mic.
      for (const auto& candidate : mCatalog.recordings)
        if (candidate.cabinet == cabinet && (mMenuKind == 0 || candidate.microphone == microphone))
        {
          nearest = mCatalog.Nearest(cabinet, candidate.microphone, candidate.angle, previous.center, previous.grill);
          break;
        }
    }
    if (nearest >= 0)
    {
      mPreview = nearest;
      LoadArtwork();
      Commit();
    }
  }

private:
  NAMFileBrowserControl* Browser() const
  {
    return GetUI()->GetControlWithTag(kCtrlTagIRFileBrowser)->As<NAMFileBrowserControl>();
  }

  void EnsureCatalog()
  {
    if (mCatalogReady)
      return;
    try
    {
      WDL_String root;
      BundleResourcePath(root,
#ifdef OS_WIN
                         static_cast<PluginIDType>(GetUI()->GetWinModuleHandle())
#elif defined OS_MAC || defined OS_IOS
                         GetUI()->GetBundleID()
#else
                         nullptr
#endif
      );
#ifdef OS_WIN
      if (!root.GetLength())
        PluginPath(root, static_cast<PluginIDType>(GetUI()->GetWinModuleHandle()));
#endif
      mCatalog = nam_cabinet::Catalog::Load(std::filesystem::u8path(root.Get()) / "Models");
      mPreview = mCatalog.Nearest(0, 0, 0, 0.f, 0.f);
      if (mPreview < 0)
        mPreview = 0;
      mError.clear();
      mCatalogReady = true;
    }
    catch (const std::exception& error)
    {
      mError = error.what();
    }
  }

  void SyncFile(const std::string& file)
  {
    mCurrentFile = file;
    SetTooltip(file.empty() ? "Choose a recorded microphone position" : file.c_str());
    if (!mCatalogReady)
      return;
    mLoaded = mCatalog.Find(std::filesystem::u8path(file));
    if (mLoaded >= 0)
      mPreview = mLoaded;
    if (!IsHidden())
      LoadArtwork();
    SetDirty(false);
  }

  void Close()
  {
    mDragging = false;
    Hide(true);
    GetUI()->SetAllControlsDirty();
  }

  void Commit()
  {
    if (mPreview < 0)
      return;
    const auto path = nam_cabinet::PathString(mCatalog.recordings[mPreview].path);
    // Loading is deliberately deferred until release, outside the audio thread.
    // OnMsgFromDelegate confirms the new path or rolls the preview back on error.
    if (path != mCurrentFile || mLoadFailed)
      mLoad(WDL_String(path.c_str()), WDL_String());
    SetDirty(false);
  }

  void PreviewAt(float x, float y)
  {
    const auto& r = mCatalog.recordings[mPreview];
    const int nearest = mCatalog.Nearest(r.cabinet, r.microphone, r.angle,
                                        (x - OriginX()) / Step(), (y - OriginY()) / Step());
    if (nearest >= 0)
      mPreview = nearest;
    SetDirty(false);
  }

  void ShowMenu(int kind)
  {
    const auto& selected = mCatalog.recordings[mPreview];
    std::set<int> values;
    for (const auto& r : mCatalog.recordings)
    {
      if (kind == 0) values.insert(static_cast<int>(r.cabinet));
      if (kind == 1 && r.cabinet == selected.cabinet) values.insert(static_cast<int>(r.microphone));
      if (kind == 2 && r.cabinet == selected.cabinet && r.microphone == selected.microphone) values.insert(r.angle);
    }
    mMenuKind = kind;
    mMenuValues.assign(values.begin(), values.end());
    mMenu.Clear();
    for (int value : mMenuValues)
    {
      const auto label = kind == 0 ? mCatalog.cabinets[value].name
        : kind == 1 ? mCatalog.microphones[value].name : std::to_string(value) + " degrees";
      auto* item = mMenu.AddItem(label.c_str());
      item->SetChecked(value == (kind == 0 ? static_cast<int>(selected.cabinet)
                                : kind == 1 ? static_cast<int>(selected.microphone) : selected.angle));
    }
    GetUI()->CreatePopupMenu(*this, mMenu, MenuBounds(kind));
  }

  void LoadArtwork()
  {
    if (mPreview < 0 || !mError.empty())
      return;
    const auto& r = mCatalog.recordings[mPreview];
    auto load = [this](const nam_cabinet::Artwork& artwork) {
      const auto path = nam_cabinet::PathString(artwork.path);
      std::error_code error;
      if (!std::filesystem::is_regular_file(artwork.path, error))
      {
        mError = "Cabinet artwork is missing: " + path;
        return IBitmap();
      }
      // Explicit source scale avoids creating huge Retina copies of source art.
      const auto bitmap = GetUI()->LoadBitmap(path.c_str(), 1, false, 1);
      if (!bitmap.IsValid() || bitmap.W() <= 0 || bitmap.H() <= 0)
        mError = "Cabinet artwork could not be decoded: " + path;
      return bitmap;
    };
    mCabinetBitmap = load(mCatalog.cabinets[r.cabinet].image);
    mMicBitmap = load(mCatalog.microphones[r.microphone].images.at(r.angle));
  }

  void DrawArtwork(IGraphics& g, const IBitmap& bitmap, const nam_cabinet::Artwork& art, float x, float y)
  {
    if (!bitmap.IsValid())
      return;
    const auto& cabinet = mCatalog.cabinets[mCatalog.recordings[mPreview].cabinet];
    const float scale = Step() / cabinet.pixelsPerInch * art.scale;
    const float left = x - art.anchorX * scale, top = y - art.anchorY * scale;
    g.DrawFittedBitmap(bitmap, IRECT(left, top, left + bitmap.W() * scale, top + bitmap.H() * scale));
  }

  IRECT Box(float l, float t, float r, float b) const { return IRECT(mRECT.L + l, mRECT.T + t, mRECT.L + r, mRECT.T + b); }
  IRECT MenuBounds(int menu) const
  {
    return menu == 0 ? Box(24, 77, 290, 108) : menu == 1 ? Box(298, 77, 480, 108) : Box(488, 77, 576, 108);
  }
  IRECT StageBounds() const { return Box(24, 116, 335, 333); }
  IRECT CloseBounds() const { return Box(510, 20, 576, 46); }
  IRECT LibraryBounds() const { return Box(24, 365, 182, 390); }
  IRECT ClearBounds() const { return Box(490, 365, 576, 390); }
  float OriginX() const { return mRECT.L + 170.f; }
  float OriginY() const { return mRECT.T + 190.f; }
  float MaxCenter() const { return Extent(true); }
  float MaxGrill() const { return Extent(false); }
  float Extent(bool center) const
  {
    float value = 5.f;
    const auto& selected = mCatalog.recordings[mPreview];
    for (auto i : mCatalog.Positions(selected.cabinet, selected.microphone, selected.angle))
      value = std::max(value, center ? mCatalog.recordings[i].center : mCatalog.recordings[i].grill);
    return value;
  }
  float Step() const { return 60.f / std::max(MaxCenter(), MaxGrill()); }
  float X(const nam_cabinet::Recording& r) const { return OriginX() + r.center * Step(); }
  float Y(const nam_cabinet::Recording& r) const { return OriginY() + r.grill * Step(); }
  static std::string Inches(float value)
  {
    std::ostringstream text;
    text << value << "\"";
    return text.str();
  }
  static void Text(IGraphics& g, const char* text, const IRECT& bounds, float size, const IColor& color = COLOR_WHITE)
  {
    g.DrawText(IText(size, color, "Roboto-Regular"), text, bounds);
  }
  static void Button(IGraphics& g, const IRECT& bounds, const char* label, float size = 12.f)
  {
    g.FillRoundRect(IColor(255, 48, 52, 58), bounds, 4.f);
    Text(g, label, bounds.GetHPadded(-8), size);
  }

  nam_cabinet::Catalog mCatalog;
  bool mCatalogReady = false, mDragging = false, mLoadFailed = false;
  int mPreview = -1, mLoaded = -1, mMenuKind = 0;
  float mDragX = 0.f, mDragY = 0.f;
  std::string mError, mCurrentFile;
  IBitmap mCabinetBitmap, mMicBitmap;
  IPopupMenu mMenu;
  std::vector<int> mMenuValues;
  IFileDialogCompletionHandlerFunc mLoad;
};
