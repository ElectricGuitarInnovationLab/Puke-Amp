#pragma once

#include "CabinetCatalog.h"

class NAMCabinetPageControl : public IContainerBase
{
public:
  explicit NAMCabinetPageControl(const IRECT& bounds)
  : IContainerBase(bounds)
  {
  }

  void OnAttached() override
  {
    const auto style = nam_ui::ControlStyle();
    for (int i = 0; i < 2; ++i)
    {
      const float y = RowTop(i) + 30;
      mLevel[i] =
        AddChildControl(new NAMKnobControl(Box(314, y, 386, y + 70), i ? kMicBLevel : kMicALevel, "LEVEL", style));
      mPan[i] = AddChildControl(new NAMKnobControl(Box(392, y, 464, y + 70), i ? kMicBPan : kMicAPan, "PAN", style));
      auto selectRow = [this, i](IControl*) {
        mActive = i;
        SetDirty(false);
      };
      mLevel[i]->SetActionFunction(selectRow);
      mPan[i]->SetActionFunction(selectRow);
    }
    mInvert = AddChildControl(
      new NAMSwitchControl(Box(466, RowTop(1) + 41, 582, RowTop(1) + 69), kMicBInvert, "Invert polarity"));
  }

  void Open()
  {
    EnsureCatalog();
    Hide(false);
    Sync(Plugin()->CabinetPaths(), true);
    GetUI()->SetAllControlsDirty();
  }

  nam_cabinet::Mixer::Result LoadPrimaryFile(const std::string& path)
  {
    // The main window's file picker edits A. Resolve the cabinet of an existing
    // second mic before changing A, even if this editor has never been opened.
    const auto paths = Plugin()->CabinetPaths();
    if (!paths[1].empty())
    {
      EnsureCatalog();
      Sync(paths, true);
    }
    return SelectFile(0, path);
  }

  void Sync(const nam_cabinet::Mixer::Paths& paths, bool force = false)
  {
    if (!force && paths == mPaths)
      return;
    mPaths = paths;
    mDragging = false;
    mStatus.clear();
    if (mCatalogReady)
    {
      for (int i = 0; i < 2; ++i)
        mPreview[i] = mLoaded[i] = mCatalog.Find(std::filesystem::u8path(paths[i]));
      int fallback = mLoaded[0] >= 0 ? mLoaded[0] : mLoaded[1];
      if (fallback < 0)
        fallback = std::max(0, mCatalog.Nearest(0, 0, 0, 0.f, 0.f));
      for (auto& index : mPreview)
        if (index < 0)
          index = fallback;
      if (!IsHidden())
        LoadArtwork();
    }
    UpdateMixControls();
    SetDirty(false);
  }

  void UpdateMixControls()
  {
    if (!mInvert)
      return;
    const bool mono = Plugin()->NOutChansConnected() == 1;
    for (int i = 0; i < 2; ++i)
    {
      mLevel[i]->SetMouseEventsWhenDisabled(false);
      mPan[i]->SetMouseEventsWhenDisabled(false);
      const bool off = mPaths[i].empty();
      mLevel[i]->SetDisabled(off);
      mPan[i]->SetDisabled(off || mono);
      mPan[i]->SetTooltip(mono ? "Pan is available on stereo outputs" : "Microphone pan");
    }
    mInvert->SetMouseEventsWhenDisabled(false);
    mInvert->SetDisabled(mPaths[1].empty());
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(PluginColors::BACKGROUND, mRECT);
    Text(g, "CABINET & MICROPHONES", Box(24, 12, 308, 50), 20.f);
    Button(g, CloseBounds(), "Close");
    Button(g, LibraryBounds(), mActive ? "Mic B IR library / import..." : "Mic A IR library / import...");
    Button(g, ClearBounds(), mActive ? "Clear Mic B" : "Clear Mic A");
    const bool catalog = mCatalogReady && mError.empty();
    if (catalog)
    {
      const auto& cab = mCatalog.cabinets[Recording(mActive).cabinet];
      Text(g, "SPEAKER", Box(22, 55, 84, 82), 11.f, PluginColors::MUTED);
      MenuButton(g, SpeakerBounds(), cab.name.c_str());
      DrawStage(g);
    }
    else
      Text(g, "Cabinet library unavailable", StageBounds(), 15.f);

    for (int i = 0; i < 2; ++i)
    {
      const auto row = RowBounds(i);
      g.FillRoundRect(PluginColors::PANEL, row, 5.f);
      if (mActive == i)
        g.DrawRoundRect(PluginColors::NAM_THEMECOLOR, row, 5.f);
      Text(g, i ? "Mic B" : "Mic A", row.GetFromLeft(46).GetFromTop(30), 13.f);
      std::string mic = mPaths[i].empty() ? "Off" : "Custom IR";
      if (catalog && mLoaded[i] >= 0)
        mic = mCatalog.microphones[Recording(i).microphone].name;
      MenuButton(g, MicBounds(i), mic.c_str());
      if (catalog && mLoaded[i] >= 0)
      {
        const auto& r = Recording(i);
        if (r.placement == Placement::Measured)
          MenuButton(g, AngleBounds(i), (std::to_string(r.angle) + "°").c_str());
        else
          Text(g, r.placement == Placement::Inside ? "Inside" : "Outside", AngleBounds(i), 12.f);
      }
    }
    const bool rooms = catalog && CanPlace() && HasRoomPositions();
    if (rooms)
    {
      Text(g, "Atmosphere", Box(22, 310, 97, 334), 12.f);
      for (auto place : {Placement::Inside, Placement::Outside})
      {
        if (RoomIndex(place) < 0)
          continue;
        const auto bounds = RoomBounds(place);
        Button(g, bounds, place == Placement::Inside ? "Inside booth" : "Outside booth");
        if (mLoaded[mActive] >= 0 && Recording(mActive).placement == place)
          g.DrawRoundRect(PluginColors::NAM_THEMECOLOR, bounds, 4.f);
      }
    }
    std::string position;
    if (catalog && (mLoaded[mActive] >= 0 || mDragging))
    {
      const auto& r = Recording(mActive);
      if (r.placement == Placement::Measured)
        position = Inches(r.center) + " from center  /  " + Inches(r.grill) + " from grille";
      else
        position = r.placement == Placement::Inside ? "Inside the isolation booth" : "Outside the isolation booth, door open";
      if (!rooms)
        Text(g, position.c_str(), Box(22, 310, 295, 333), 12.f);
    }
    const char* hint = !mStatus.empty() ? mStatus.c_str()
                       : mDragging      ? "Release to load this position"
                       : rooms          ? position.c_str()
                       : !catalog       ? "Use the IR library to select a custom recording."
                                        : "Select a mic row to edit. Drag, click a dot, or use the arrow keys.";
    Text(g, hint, Box(20, 334, 580, 359), 12.f, PluginColors::MUTED);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (CloseBounds().Contains(x, y))
    {
      Close();
      return;
    }
    if (ClearBounds().Contains(x, y))
    {
      SelectFile(mActive, "");
      return;
    }
    if (LibraryBounds().Contains(x, y))
    {
      const int slot = mActive;
      auto* browser = GetUI()->GetControlWithTag(kCtrlTagIRFileBrowser)->As<NAMFileBrowserControl>();
      browser->OpenLibraryMenu(
        LibraryBounds(), [this, slot](const WDL_String& file, const WDL_String&) { SelectFile(slot, file.Get()); },
        &mPaths[slot]);
      return;
    }
    const bool catalog = mCatalogReady && mError.empty();
    for (int i = 0; i < 2; ++i)
      if (RowBounds(i).Contains(x, y))
      {
        mActive = i;
        SetDirty(false);
        if (catalog && MicBounds(i).Contains(x, y))
          ShowMenu(Menu::Mic);
        else if (catalog && mLoaded[i] >= 0 && Recording(i).placement == Placement::Measured
                 && AngleBounds(i).Contains(x, y))
          ShowMenu(Menu::Angle);
        return;
      }
    if (!catalog)
      return;
    if (SpeakerBounds().Contains(x, y))
    {
      ShowMenu(Menu::Speaker);
      return;
    }
    if (!CanPlace())
      return;
    for (auto place : {Placement::Inside, Placement::Outside})
      if (RoomBounds(place).Contains(x, y))
      {
        const int index = RoomIndex(place);
        if (index >= 0)
          SelectFile(mActive, Path(index));
        return;
      }
    if (!StageBounds().Contains(x, y))
      return;
    if (GridAngle(Recording(mActive)) < 0 && y < RoomY() - 10.f)
      return;
    mDragging = true;
    const auto& r = Recording(mActive);
    bool onDot = false;
    for (auto index : mCatalog.Positions(r.cabinet, r.microphone, GridAngle(r)))
      onDot |= std::hypot(x - X(mCatalog.recordings[index]), y - Y(mCatalog.recordings[index])) < 6.f;
    for (auto place : {Placement::Inside, Placement::Outside})
      if (RoomIndex(place) >= 0)
        onDot |= std::hypot(x - RoomBounds(place).MW(), y - RoomY()) < 6.f;
    const auto micBounds = IRECT(X(r) - 15, Y(r) - 15, X(r) + (r.angle ? 85 : 15), Y(r) + 85);
    mDragX = !onDot && micBounds.Contains(x, y) ? x - X(r) : 0.f;
    mDragY = !onDot && micBounds.Contains(x, y) ? y - Y(r) : 0.f;
    PreviewAt(x - mDragX, y - mDragY);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override
  {
    if (mDragging)
      PreviewAt(x - mDragX, y - mDragY);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    if (mDragging)
    {
      mDragging = false;
      CommitPosition();
    }
  }

  bool OnKeyDown(float, float, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      Close();
      return true;
    }
    if (!mCatalogReady || !mError.empty() || !CanPlace())
      return false;
    const int dx = key.VK == kVK_RIGHT ? 1 : key.VK == kVK_LEFT ? -1 : 0;
    const int dy = key.VK == kVK_DOWN ? 1 : key.VK == kVK_UP ? -1 : 0;
    if (!dx && !dy)
      return false;
    const auto current = mCatalog.recordings[mPreview[mActive]];
    if (current.placement != Placement::Measured)
    {
      int index = -1;
      if (dx)
        index = RoomIndex(dx < 0 ? Placement::Inside : Placement::Outside);
      else if (dy < 0)
        index = mCatalog.Nearest(current.cabinet, current.microphone, GridAngle(current),
                                 (X(current) - OriginX()) / mStep, (RoomY() - OriginY()) / mStep);
      if (index >= 0 && index != mLoaded[mActive])
        SelectFile(mActive, Path(index));
      return true;
    }
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
          mPreview[mActive] = static_cast<int>(index);
        }
      }
    }
    if (best < std::numeric_limits<float>::max())
      CommitPosition();
    else if (dy > 0 && HasRoomPositions())
      SelectFile(mActive, Path(NearestRoom(X(current))));
    return true;
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (!menu || menu->GetChosenItemIdx() < 0)
      return;
    const int choice = mValues.at(menu->GetChosenItemIdx());
    if (mMenuKind == Menu::Mic && choice < 0)
    {
      SelectFile(mActive, "");
      return;
    }
    const auto& r = Recording(mActive);
    if (mMenuKind == Menu::Speaker)
    {
      nam_cabinet::Mixer::Changes edits;
      for (int i = 0; i < 2; ++i)
        if (mLoaded[i] >= 0 || (mLoaded[1 - i] < 0 && i == mActive))
          edits[i] = Path(mCatalog.Match(Recording(i), choice, Recording(i).microphone));
      Apply(edits);
      return;
    }
    const int index = mMenuKind == Menu::Mic ? mCatalog.Match(r, r.cabinet, choice)
                                             : mCatalog.Nearest(r.cabinet, r.microphone, choice, r.center, r.grill);
    SelectFile(mActive, Path(index));
  }

private:
  enum class Menu
  {
    Speaker,
    Mic,
    Angle
  };
  NeuralAmpModeler* Plugin() { return static_cast<NeuralAmpModeler*>(GetDelegate()); }
  const nam_cabinet::Recording& Recording(int i) const { return mCatalog.recordings[mPreview[i]]; }
  std::string Path(int index) const { return nam_cabinet::PathString(mCatalog.recordings[index].path); }
  bool CanPlace() const { return mLoaded[mActive] >= 0 || (!mActive && mPaths[0].empty()); }

  using Placement = nam_cabinet::Placement;

  int RoomIndex(Placement place) const
  {
    const auto& r = Recording(mActive);
    return mCatalog.RoomPosition(r.cabinet, r.microphone, place);
  }

  bool HasRoomPositions() const { return RoomIndex(Placement::Inside) >= 0 || RoomIndex(Placement::Outside) >= 0; }

  int NearestRoom(float x) const
  {
    const int inside = RoomIndex(Placement::Inside), outside = RoomIndex(Placement::Outside);
    if (inside < 0 || outside < 0)
      return std::max(inside, outside);
    return x < (RoomBounds(Placement::Inside).MW() + RoomBounds(Placement::Outside).MW()) / 2 ? inside : outside;
  }

  int GridAngle(const nam_cabinet::Recording& r) const
  {
    if (r.placement == Placement::Measured)
      return r.angle;
    for (const auto& item : mCatalog.recordings)
      if (item.cabinet == r.cabinet && item.microphone == r.microphone && item.placement == Placement::Measured)
        return item.angle;
    return -1;
  }

  nam_cabinet::Mixer::Result SelectFile(int slot, const std::string& file)
  {
    nam_cabinet::Mixer::Changes edits;
    edits[slot] = file;
    const int index = mCatalogReady ? mCatalog.Find(std::filesystem::u8path(file)) : -1;
    const int other = 1 - slot;
    if (index >= 0 && mLoaded[other] >= 0)
    {
      const auto& r = Recording(other);
      const auto cab = mCatalog.recordings[index].cabinet;
      if (r.cabinet != cab)
        edits[other] = Path(mCatalog.Match(r, cab, r.microphone));
    }
    return Apply(edits);
  }

  nam_cabinet::Mixer::Result Apply(const nam_cabinet::Mixer::Changes& edits)
  {
    const auto result = Plugin()->SelectCabinet(edits);
    Sync(Plugin()->CabinetPaths(), true);
    if (result.status != nam_cabinet::Mixer::Status::SUCCESS)
      mStatus = std::string(result.slot ? "Mic B" : "Mic A") + " failed to load. Previous sound retained.";
    return result;
  }

  void CommitPosition() { SelectFile(mActive, Path(mPreview[mActive])); }
  void Close()
  {
    mDragging = false;
    Hide(true);
    GetUI()->SetAllControlsDirty();
  }

  void PreviewAt(float x, float y)
  {
    const auto& r = Recording(mActive);
    const int angle = GridAngle(r);
    const int room = NearestRoom(x);
    const bool roomRow = room >= 0 && (angle < 0 || y >= RoomY() - 10.f);
    mPreview[mActive] = roomRow ? room : mCatalog.Nearest(r.cabinet, r.microphone, angle,
                                                          (x - OriginX()) / mStep, (y - OriginY()) / mStep);
    if (Recording(mActive).angle != r.angle)
      LoadArtwork();
    SetDirty(false);
  }

  void ShowMenu(Menu kind)
  {
    mMenuKind = kind;
    const auto& r = Recording(mActive);
    std::set<int> values;
    if (kind == Menu::Mic)
      values.insert(-1);
    for (const auto& item : mCatalog.recordings)
    {
      if (kind == Menu::Speaker)
        values.insert(static_cast<int>(item.cabinet));
      if (kind == Menu::Mic && item.cabinet == r.cabinet)
        values.insert(static_cast<int>(item.microphone));
      if (kind == Menu::Angle && item.placement == Placement::Measured && item.cabinet == r.cabinet && item.microphone == r.microphone)
        values.insert(item.angle);
    }
    mValues.assign(values.begin(), values.end());
    mMenu.Clear();
    for (int v : mValues)
    {
      const auto label = v < 0                   ? std::string("Off")
                         : kind == Menu::Mic     ? mCatalog.microphones[v].name
                         : kind == Menu::Speaker ? mCatalog.cabinets[v].name
                                                 : std::to_string(v) + "°";
      const int current = kind == Menu::Speaker     ? static_cast<int>(r.cabinet)
                          : kind == Menu::Angle     ? r.angle
                          : mLoaded[mActive] >= 0   ? static_cast<int>(r.microphone)
                          : mPaths[mActive].empty() ? -1
                                                    : -2;
      mMenu.AddItem(label.c_str())->SetChecked(v == current);
    }
    GetUI()->CreatePopupMenu(*this, mMenu,
                             kind == Menu::Speaker ? SpeakerBounds()
                             : kind == Menu::Mic   ? MicBounds(mActive)
                                                   : AngleBounds(mActive));
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

      mError.clear();
      mCatalogReady = true;
    }
    catch (const std::exception& error)
    {
      mError = error.what();
      SetTooltip(mError.c_str());
    }
  }

  void LoadArtwork()
  {
    const auto cab = Recording(mActive).cabinet;
    float extent = 5.f;
    for (const auto& r : mCatalog.recordings)
      if (r.cabinet == cab)
        extent = std::max({extent, r.center, r.grill});
    mStep = 60.f / extent;
    auto load = [this](const nam_cabinet::Artwork& art) {
      const auto path = nam_cabinet::PathString(art.path);
      auto bitmap = GetUI()->LoadBitmap(path.c_str(), 1, false, 1);
      if (!bitmap.IsValid() || bitmap.W() <= 0 || bitmap.H() <= 0)
      {
        mError = "Cabinet artwork could not be loaded: " + path;
        SetTooltip(mError.c_str());
      }
      return bitmap;
    };
    mCabBitmap = load(mCatalog.cabinets[cab].image);
    for (int i = 0; i < 2; ++i)
    {
      const auto& r = Recording(i);
      mMicBitmap[i] = load(mCatalog.microphones[r.microphone].images.at(r.angle));
    }
  }

  void DrawStage(IGraphics& g)
  {
    const auto stage = StageBounds();
    const auto& selected = Recording(mActive);
    const auto& cab = mCatalog.cabinets[selected.cabinet];
    auto artwork = [&](const IBitmap& bitmap, const nam_cabinet::Artwork& art, float x, float y) {
      if (!bitmap.IsValid())
        return;
      const float scale = mStep / cab.pixelsPerInch * art.scale;
      const float left = x - art.anchorX * scale, top = y - art.anchorY * scale;
      g.DrawFittedBitmap(bitmap, IRECT(left, top, left + bitmap.W() * scale, top + bitmap.H() * scale));
    };
    g.FillRoundRect(PluginColors::PANEL, stage, 6.f);
    g.PathClipRegion(stage);
    artwork(mCabBitmap, cab.image, OriginX(), OriginY());
    const int gridAngle = GridAngle(selected);
    if (gridAngle >= 0)
    {
      g.DrawLine(PluginColors::MUTED, OriginX(), OriginY(), OriginX() + 60, OriginY());
      g.DrawLine(PluginColors::MUTED, OriginX(), OriginY(), OriginX(), OriginY() + 60);
    }
    if (CanPlace())
    {
      auto dot = [&](const nam_cabinet::Recording& r) {
        g.FillCircle(COLOR_BLACK, X(r), Y(r), 3.f);
        g.DrawCircle(COLOR_WHITE, X(r), Y(r), 3.f);
      };
      for (auto index : mCatalog.Positions(selected.cabinet, selected.microphone, gridAngle))
        dot(mCatalog.recordings[index]);
      for (auto place : {Placement::Inside, Placement::Outside})
      {
        const int index = RoomIndex(place);
        if (index >= 0)
          dot(mCatalog.recordings[index]);
      }
    }
    const int order[] = {1 - mActive, mActive};
    for (int i : order)
    {
      if (mLoaded[i] < 0 && !(mDragging && i == mActive))
        continue;
      const auto& r = Recording(i);
      artwork(mMicBitmap[i], mCatalog.microphones[r.microphone].images.at(r.angle), X(r), Y(r));
    }
    for (int i : order)
    {
      if (mLoaded[i] < 0 && !(mDragging && i == mActive))
        continue;
      const auto& r = Recording(i);
      g.DrawCircle(i == mActive ? PluginColors::NAM_THEMECOLOR : COLOR_WHITE, X(r), Y(r), 6.f, nullptr, 2.f);
      const float x = X(r) + (i ? 10.f : -18.f);
      const IRECT badge(x - 7, Y(r) - 17, x + 7, Y(r) - 3);
      g.FillRoundRect(COLOR_BLACK, badge, 3);
      Text(g, i ? "B" : "A", badge, 11.f);
    }
    g.PathClipRegion();
  }

  IRECT Box(float l, float t, float r, float b) const
  { return IRECT(mRECT.L + l, mRECT.T + t, mRECT.L + r, mRECT.T + b); }
  float RowTop(int i) const { return 92.f + 116.f * i; }
  IRECT RowBounds(int i) const { return Box(302, RowTop(i) - 2, 580, RowTop(i) + 107); }
  IRECT MicBounds(int i) const { return Box(348, RowTop(i) + 3, 512, RowTop(i) + 29); }
  IRECT AngleBounds(int i) const { return Box(516, RowTop(i) + 3, 575, RowTop(i) + 29); }
  IRECT SpeakerBounds() const { return Box(86, 55, 378, 82); }
  IRECT StageBounds() const { return Box(22, 91, 294, 309); }
  IRECT CloseBounds() const { return Box(510, 20, 576, 46); }
  IRECT LibraryBounds() const { return Box(24, 365, 212, 390); }
  IRECT ClearBounds() const { return Box(466, 365, 576, 390); }
  float OriginX() const { return mRECT.L + 150.f; }
  float OriginY() const { return mRECT.T + 165.f; }
  IRECT RoomBounds(Placement place) const
  { return place == Placement::Inside ? Box(99, 310, 191, 334) : Box(196, 310, 294, 334); }
  float RoomY() const { return mRECT.T + 246.f; }
  float X(const nam_cabinet::Recording& r) const
  { return r.placement == Placement::Measured ? OriginX() + r.center * mStep : RoomBounds(r.placement).MW(); }
  float Y(const nam_cabinet::Recording& r) const
  { return r.placement == Placement::Measured ? OriginY() + r.grill * mStep : RoomY(); }
  static std::string Inches(float value)
  {
    std::ostringstream text;
    text << value << "\"";
    return text.str();
  }
  static void Text(IGraphics& g, const char* text, const IRECT& bounds, float size, const IColor& color = COLOR_WHITE)
  { g.DrawText(nam_ui::Text(size, color), text, bounds); }
  static void Button(IGraphics& g, const IRECT& bounds, const char* label, float size = 12.f)
  { nam_ui::DrawButton(g, bounds, label, false, size); }

  static void MenuButton(IGraphics& g, const IRECT& bounds, const char* label)
  {
    g.FillRoundRect(PluginColors::SURFACE, bounds, 4.f);
    Text(g, label, bounds.GetReducedFromRight(14.f), 12.f);
    const float x = bounds.R - 11.f, y = bounds.MH();
    g.DrawLine(PluginColors::MUTED, x - 3.f, y - 1.5f, x, y + 1.5f);
    g.DrawLine(PluginColors::MUTED, x, y + 1.5f, x + 3.f, y - 1.5f);
  }

  nam_cabinet::Catalog mCatalog;
  nam_cabinet::Mixer::Paths mPaths;
  std::array<int, 2> mPreview{-1, -1}, mLoaded{-1, -1};
  int mActive = 0;
  bool mCatalogReady = false, mDragging = false;
  float mStep = 12.f, mDragX = 0.f, mDragY = 0.f;
  std::string mError, mStatus;
  IBitmap mCabBitmap, mMicBitmap[2];
  IControl* mLevel[2]{};
  IControl* mPan[2]{};
  IControl* mInvert = nullptr;
  IPopupMenu mMenu;
  Menu mMenuKind = Menu::Mic;
  std::vector<int> mValues;
};
