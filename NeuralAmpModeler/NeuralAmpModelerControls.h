#pragma once

#include <chrono>
#include <cmath> // std::round
#include <cstdio> // FILE, fclose
#include <filesystem> // std::filesystem
#include <fstream> // std::ifstream, std::ofstream
#include <future>
#include <sstream> // std::stringstream
#include <unordered_map> // std::unordered_map
#include <vector> // std::vector
#include "IControls.h"
#include "IPlugPaths.h"
#include "UserLibrary.h"
#include "AssetValidation.h"
#include "json.hpp"

#ifdef OS_WIN
  #include <Windows.h>
  #include <Shellapi.h>
#endif

#define PLUG() static_cast<PLUG_CLASS_NAME*>(GetDelegate())
#define NAM_KNOB_HEIGHT 120.0f
#define NAM_SWTICH_HEIGHT 50.0f

using namespace iplug;
using namespace igraphics;

enum class NAMBrowserState
{
  Empty, // when no file loaded, show "Get" button
  Loaded // when file loaded, show "Clear" button
};

// Where the corner button on the plugin (settings, close settings) goes
// :param rect: Rect for the whole plugin's UI
IRECT CornerButtonArea(const IRECT& rect)
{
  const auto mainArea = rect.GetPadded(-20);
  return mainArea.GetFromTRHC(50, 50).GetCentredInside(20, 20);
};

class NAMSquareButtonControl : public ISVGButtonControl
{
public:
  NAMSquareButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 2.f);

    ISVGButtonControl::Draw(g);
  }
};

class NAMCircleButtonControl : public ISVGButtonControl
{
public:
  NAMCircleButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    ISVGButtonControl::Draw(g);
  }
};

class NAMEffectSlotButtonControl : public NAMSquareButtonControl
{
public:
  NAMEffectSlotButtonControl(const IRECT& bounds, IActionFunction action, const ISVG& offSVG, const ISVG& onSVG)
  : NAMSquareButtonControl(bounds, action, offSVG)
  , mEmptySVG(offSVG)
  , mLoadedSVG(onSVG)
  {
  }

  void SetLoaded(bool loaded)
  {
    const ISVG& svg = loaded ? mLoadedSVG : mEmptySVG;
    mOffSVG = svg;
    mOnSVG = svg;
    SetDirty(false);
  }

  void SetSelected(bool selected)
  {
    mSelected = selected;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    NAMSquareButtonControl::Draw(g);
    if (mSelected)
      g.DrawRoundRect(PluginColors::NAM_THEMECOLOR, mRECT, 2.f, nullptr, 1.5f);
  }

private:
  ISVG mEmptySVG, mLoadedSVG;
  bool mSelected = false;
};

/// Full-window dim layer; click dismisses (used for Slim overlay).
class NAMSlimOverlayBackdropControl : public IControl
{
public:
  NAMSlimOverlayBackdropControl(const IRECT& bounds, IActionFunction dismiss)
  : IControl(bounds, dismiss)
  , mDismiss(dismiss)
  {
  }

  void Draw(IGraphics& g) override { g.FillRect(COLOR_BLACK.WithOpacity(0.45f), mRECT); }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (mDismiss)
      mDismiss(this);
  }

private:
  IActionFunction mDismiss;
};

class NAMKnobControl : public IVKnobControl, public IBitmapBase
{
public:
  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , IBitmapBase(bitmap)
  {
    mInnerPointerFrac = 0.55;
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  void DrawWidget(IGraphics& g) override
  {
    float widgetRadius = GetRadius() * 0.73;
    auto knobRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W());
    const float cx = knobRect.MW(), cy = knobRect.MH();
    const float angle = mAngle1 + (static_cast<float>(GetValue()) * (mAngle2 - mAngle1));
    DrawIndicatorTrack(g, angle, cx + 0.5, cy, widgetRadius);
    g.DrawFittedBitmap(mBitmap, knobRect);
    float data[2][2];
    RadialPoints(angle, cx, cy, mInnerPointerFrac * widgetRadius, mInnerPointerFrac * widgetRadius, 2, data);
    g.PathCircle(data[1][0], data[1][1], 3);
    g.PathFill(IPattern::CreateRadialGradient(data[1][0], data[1][1], 4.0f,
                                              {{GetColor(mMouseIsOver ? kX3 : kX1), 0.f},
                                               {GetColor(mMouseIsOver ? kX3 : kX1), 0.8f},
                                               {COLOR_TRANSPARENT, 1.0f}}),
               {}, &mBlend);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), data[1][0], data[1][1], 3, &mBlend);
  }
};

class NAMSwitchControl : public IVSlideSwitchControl, public IBitmapBase
{
public:
  NAMSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVSlideSwitchControl(bounds, paramIdx, label,
                         style.WithRoundness(0.666f)
                           .WithShowValue(false)
                           .WithEmboss(true)
                           .WithShadowOffset(1.5f)
                           .WithDrawShadows(false)
                           .WithColor(kFR, COLOR_BLACK)
                           .WithFrameThickness(0.5f)
                           .WithWidgetFrac(0.5f)
                           .WithLabelOrientation(EOrientation::South))
  , IBitmapBase(bitmap)
  {
  }

  void DrawWidget(IGraphics& g) override
  {
    DrawTrack(g, mWidgetBounds);
    DrawHandle(g, mHandleBounds);
  }

  void DrawTrack(IGraphics& g, const IRECT& bounds) override
  {
    IRECT handleBounds = GetAdjustedHandleBounds(bounds);
    handleBounds = IRECT(handleBounds.L, handleBounds.T, handleBounds.R, handleBounds.T + mBitmap.H());
    IRECT centreBounds = handleBounds.GetPadded(-mStyle.shadowOffset);
    IRECT shadowBounds = handleBounds.GetTranslated(mStyle.shadowOffset, mStyle.shadowOffset);
    //    const float contrast = mDisabled ? -GRAYED_ALPHA : 0.f;
    float cR = 7.f;
    const float tlr = cR;
    const float trr = cR;
    const float blr = cR;
    const float brr = cR;

    // outer shadow
    if (mStyle.drawShadows)
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr, &mBlend);

    // Embossed style unpressed
    if (mStyle.emboss)
    {
      // Positive light
      g.FillRoundRect(GetColor(kPR), handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Negative light
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr /*, &blend*/);

      // Fill in foreground
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, centreBounds, tlr, trr, blr, brr, &mBlend);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), centreBounds, tlr, trr, blr, brr, &mBlend);
    }
    else
    {
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), handleBounds, tlr, trr, blr, brr, &mBlend);
    }

    if (mStyle.drawFrame)
      g.DrawRoundRect(GetColor(kFR), handleBounds, tlr, trr, blr, brr, &mBlend, mStyle.frameThickness);
  }

  void DrawHandle(IGraphics& g, const IRECT& filledArea) override
  {
    IRECT r;
    if (GetSelectedIdx() == 0)
    {
      r = filledArea.GetFromLeft(mBitmap.W());
    }
    else
    {
      r = filledArea.GetFromRight(mBitmap.W());
    }

    g.DrawBitmap(mBitmap, r, 0, 0, nullptr);
  }
};

class NAMFileNameControl : public IVButtonControl
{
public:
  NAMFileNameControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : IVButtonControl(bounds, DefaultClickActionFunc, label, style)
  {
  }

  void SetLabelAndTooltip(const char* str)
  {
    SetLabelStr(str);
    SetTooltip(str);
  }

  void SetLabelAndTooltipEllipsizing(const WDL_String& fileName)
  {
    auto EllipsizeFilePath = [](const char* filePath, size_t prefixLength, size_t suffixLength, size_t maxLength) {
      const std::string ellipses = "...";
      assert(maxLength <= (prefixLength + suffixLength + ellipses.size()));
      std::string str{filePath};

      if (str.length() <= maxLength)
      {
        return str;
      }
      else
      {
        return str.substr(0, prefixLength) + ellipses + str.substr(str.length() - suffixLength);
      }
    };

    auto ellipsizedFileName = EllipsizeFilePath(fileName.get_filepart(), 22, 22, 45);
    SetLabelStr(ellipsizedFileName.c_str());
    SetTooltip(fileName.get_filepart());
  }
};

// URL control for the "Get" models/irs links
class NAMGetButtonControl : public NAMSquareButtonControl
{
public:
  NAMGetButtonControl(const IRECT& bounds, const char* label, const char* url, const ISVG& globeSVG)
  : NAMSquareButtonControl(
      bounds,
      [url](IControl* pCaller) {
        WDL_String fullURL(url);
        pCaller->GetUI()->OpenURL(fullURL.Get());
      },
      globeSVG)
  {
    SetTooltip(label);
  }
};

class NAMFileBrowserControl : public IDirBrowseControlBase
{
public:
  NAMFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
                        IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
                        const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, const IBitmap& bitmap,
                        const ISVG& globeSVG, const char* getButtonLabel, const char* getButtonURL,
                        const char* bundledModelsSubdirectory, bool scanRecursively = true)
  : IDirBrowseControlBase(bounds, fileExtension, false, scanRecursively)
  , mClearMsgTag(clearMsgTag)
  , mDefaultLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mStyle(style.WithColor(kFG, COLOR_TRANSPARENT).WithDrawFrame(false))
  , mBitmap(bitmap)
  , mLoadSVG(loadSVG)
  , mClearSVG(clearSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mGlobeSVG(globeSVG)
  , mGetButtonLabel(getButtonLabel)
  , mGetButtonURL(getButtonURL)
  , mBundledModelsSubdirectory(bundledModelsSubdirectory)
  , mBrowserState(NAMBrowserState::Empty)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.DrawFittedBitmap(mBitmap, mRECT); }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    mMenuOpen = false;
    if (!pSelectedMenu || !pSelectedMenu->GetChosenItem())
      return;
    auto* item = pSelectedMenu->GetChosenItem();
    if (item == mImportFileItem)
      PromptImport(false);
    else if (item == mImportFolderItem)
      PromptImport(true);
    else if (item == mRevealLibraryItem)
    {
      try
      {
        const auto folder = nam_library::UserLibrary().Category(LibraryCategory());
        std::filesystem::create_directories(folder);
        WDL_String path(nam_library::PathString(folder).c_str());
        GetUI()->RevealPathInExplorerOrFinder(path);
      }
      catch (const std::exception& error)
      {
        GetUI()->ShowMessageBox(error.what(), "User library", kMB_OK);
      }
    }
    else if (item == mRefreshLibraryItem)
      RefreshLibraryMenu();
    else
    {
      mSelectedItemIndex = mItems.Find(item);
      LoadFileAtCurrentIndex();
    }
  }

  void PollImport()
  {
    if (mMenuOpen || !mImport.valid() || mImport.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    const auto result = mImport.get();
    RefreshLibraryMenu();
    std::ostringstream message;
    message << "Imported " << result.imported << " file(s) into My Library.";
    if (result.skipped)
      message << "\nSkipped " << result.skipped << " file(s) with other extensions.";
    if (!result.errors.empty())
    {
      message << "\n\nCould not import " << result.errors.size() << " item(s):";
      for (const auto& error : result.errors)
        message << "\n" << error;
    }
    GetUI()->ShowMessageBox(message.str().c_str(), "Library import", kMB_OK);
  }

  void OnAttached() override
  {
    RefreshLibraryMenu();

    auto prevFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex--;

      if (mSelectedItemIndex < 0)
        mSelectedItemIndex = nItems - 1;

      LoadFileAtCurrentIndex();
    };

    auto nextFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex++;

      if (mSelectedItemIndex >= nItems)
        mSelectedItemIndex = 0;

      LoadFileAtCurrentIndex();
    };

    auto loadFileFunc = [&](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path;
      GetSelectedFileDirectory(path);
#ifdef NAM_PICK_DIRECTORY
      pCaller->GetUI()->PromptForDirectory(path, [&](const WDL_String& fileName, const WDL_String& path) {
        if (path.GetLength())
        {
          ClearPathList();
          AddPath(path.Get(), "");
          SetupMenu();
          SelectFirstFile();
          LoadFileAtCurrentIndex();
        }
      });
#else
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, mExtension.Get(), [&](const WDL_String& fileName, const WDL_String& path) {
          if (fileName.GetLength())
          {
            ClearPathList();
            AddPath(path.Get(), "");
            SetupMenu();
            SetSelectedFile(fileName.Get());
            LoadFileAtCurrentIndex();
          }
        });
#endif
    };

    auto clearFileFunc = [&](IControl* pCaller) {
      pCaller->GetDelegate()->SendArbitraryMsgFromUI(mClearMsgTag);
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
      mCurrentFile.clear();
      SetBrowserState(NAMBrowserState::Empty);
      // FIXME disabling output mode...
      //      pCaller->GetUI()->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
    };

    auto chooseFileFunc = [&](IControl* pCaller) {
      RefreshLibraryMenu();
      CheckSelectedItem();
      mMenuOpen = true;
      pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
    };

    IRECT padded = mRECT.GetPadded(-6.f).GetHPadded(-2.f);
    const auto buttonWidth = padded.H();
    const auto loadFileButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto clearAndGetButtonBounds = padded.ReduceFromRight(buttonWidth);
    const auto leftButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto rightButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto fileNameButtonBounds = padded;

    AddChildControl(new NAMSquareButtonControl(loadFileButtonBounds, DefaultClickActionFunc, mLoadSVG))
      ->SetAnimationEndActionFunction(loadFileFunc);
    AddChildControl(new NAMSquareButtonControl(leftButtonBounds, DefaultClickActionFunc, mLeftSVG))
      ->SetAnimationEndActionFunction(prevFileFunc);
    AddChildControl(new NAMSquareButtonControl(rightButtonBounds, DefaultClickActionFunc, mRightSVG))
      ->SetAnimationEndActionFunction(nextFileFunc);
    AddChildControl(mFileNameControl = new NAMFileNameControl(fileNameButtonBounds, mDefaultLabelStr.Get(), mStyle))
      ->SetAnimationEndActionFunction(chooseFileFunc);

    // creates both right-side controls but only show one based on state
    mClearButton = new NAMSquareButtonControl(clearAndGetButtonBounds, DefaultClickActionFunc, mClearSVG);
    mClearButton->SetAnimationEndActionFunction(clearFileFunc);
    AddChildControl(mClearButton);

    mGetButton = new NAMGetButtonControl(clearAndGetButtonBounds, mGetButtonLabel, mGetButtonURL, mGlobeSVG);
    AddChildControl(mGetButton);

    // initialize control visibility
    SetBrowserState(NAMBrowserState::Empty);
  }

  void ClearDisplayedFile()
  {
    if (mFileNameControl)
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
    mCurrentFile.clear();
    mSelectedItemIndex = -1;
    SetBrowserState(NAMBrowserState::Empty);
  }

  void SetDisplayedFile(const WDL_String& fileName)
  {
    if (!mFileNameControl)
      return;
    OnMsgFromDelegate(kMsgTagLoadedModel, fileName.GetLength(), fileName.Get());
  }

  void LoadFileAtCurrentIndex()
  {
    if (mSelectedItemIndex > -1 && mSelectedItemIndex < NItems())
    {
      WDL_String fileName, path;
      GetSelectedFile(fileName);
      mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
      mCompletionHandlerFunc(fileName, path);
    }
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    switch (msgTag)
    {
      case kMsgTagLoadFailed:
        // Honestly, not sure why I made a big stink of it before. Why not just say it failed and move on? :)
        {
          std::string label(std::string("(FAILED) ") + std::string(mFileNameControl->GetLabelStr()));
          mFileNameControl->SetLabelAndTooltip(label.c_str());
          SetBrowserState(NAMBrowserState::Empty);
        }
        break;
      case kMsgTagLoadedModel:
      case kMsgTagLoadedIR:
      {
        WDL_String fileName(reinterpret_cast<const char*>(pData));
        mCurrentFile = fileName.Get();
        RefreshLibraryMenu();
        mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
        SetBrowserState(NAMBrowserState::Loaded);
      }
      break;
      default: break;
    }
  }

private:
  std::string LibraryCategory() const
  {
    const std::string bundled = mBundledModelsSubdirectory.Get();
    return bundled == "Amp" ? "Amps" : bundled == "FX" ? "Pedals" : "IRs";
  }

  void RefreshLibraryMenu()
  {
    ClearPathList();
    AddBundledModelsPath(false);
    const auto directory = nam_library::UserLibrary().Category(LibraryCategory());
    AddPath(nam_library::PathString(directory).c_str(), "My Library");
    SetupMenu();
    SetSelectedFile(mCurrentFile.c_str());
    // Keep the existing ability to audition files without importing them.
    if (!mCurrentFile.empty() && mSelectedItemIndex == -1)
    {
      const auto parent = std::filesystem::u8path(mCurrentFile).parent_path();
      AddPath(nam_library::PathString(parent).c_str(), "Current Folder");
      SetupMenu();
      SetSelectedFile(mCurrentFile.c_str());
    }
    mMainMenu.AddSeparator();
    mImportFileItem = mMainMenu.AddItem("Import File…");
    mImportFolderItem = mMainMenu.AddItem("Import Folder…");
    mRevealLibraryItem = mMainMenu.AddItem("Reveal Library Folder");
    mRefreshLibraryItem = mMainMenu.AddItem("Refresh");
  }

  void PromptImport(bool folder)
  {
    if (mImport.valid())
    {
      GetUI()->ShowMessageBox("An import is still running. Its results will appear when it finishes.",
                             "Library import", kMB_OK);
      return;
    }
    WDL_String file, path;
    GetSelectedFileDirectory(path);
    auto completion = [this, folder](const WDL_String& file, const WDL_String& directory) {
      const auto source = std::string(folder ? directory.Get() : file.Get());
      if (source.empty() || mImport.valid())
        return;
      const auto library = nam_library::UserLibrary();
      const auto category = LibraryCategory();
      const auto extension = std::string(mExtension.Get());
      // Capture values only: the worker never touches the UI or active DSP.
      try
      {
        mImport = std::async(std::launch::async, [library, category, extension, source] {
          return library.Import(std::filesystem::u8path(source), category, extension,
                                extension == "nam" ? nam_library::ValidateModel : nam_library::ValidateIR);
        });
      }
      catch (const std::exception& error)
      {
        GetUI()->ShowMessageBox(error.what(), "Could not start import", kMB_OK);
      }
    };
    if (folder)
      GetUI()->PromptForDirectory(path, completion);
    else
      GetUI()->PromptForFile(file, path, EFileAction::Open, mExtension.Get(), completion);
  }

  static bool DirectoryExists(const WDL_String& path)
  {
    if (!CStringHasContents(path.Get()))
      return false;

    try
    {
      return std::filesystem::is_directory(std::filesystem::u8path(path.Get()));
    }
    catch (const std::filesystem::filesystem_error&)
    {
      return false;
    }
  }

  static void AppendPathComponent(WDL_String& path, const char* component)
  {
    if (!CStringHasContents(path.Get()) || !CStringHasContents(component))
      return;

    if (!WDL_IS_DIRCHAR(path.Get()[path.GetLength() - 1]))
      path.Append(WDL_DIRCHAR_STR);
    path.Append(component);
  }

  bool ResolveBundledModelsPath(WDL_String& path)
  {
    path.Set("");
    if (!CStringHasContents(mBundledModelsSubdirectory.Get()) || GetUI() == nullptr)
      return false;

    BundleResourcePath(path,
#ifdef OS_WIN
                       static_cast<PluginIDType>(GetUI()->GetWinModuleHandle())
#elif defined OS_MAC || defined OS_IOS
                       GetUI()->GetBundleID()
#else
                       nullptr
#endif
    );

    if (!CStringHasContents(path.Get()))
    {
#ifdef OS_WIN
      PluginPath(path, static_cast<PluginIDType>(GetUI()->GetWinModuleHandle()));
#endif
    }

    AppendPathComponent(path, "Models");
    AppendPathComponent(path, mBundledModelsSubdirectory.Get());
    return DirectoryExists(path);
  }

  bool AddBundledModelsPath(bool setupMenu = true)
  {
    if (!ResolveBundledModelsPath(mBundledModelsPath))
      return false;

    AddPath(mBundledModelsPath.Get(), "Included");
    if (setupMenu)
      SetupMenu();
    return true;
  }

  void SelectFirstFile() { mSelectedItemIndex = mFiles.GetSize() ? 0 : -1; }

  void GetSelectedFileDirectory(WDL_String& path)
  {
    GetSelectedFile(path);
    path.remove_filepart();
    return;
  }

  // set the state of the browser and the visibility of the "Get" vs. "Clear" buttons
  void SetBrowserState(NAMBrowserState newState)
  {
    mBrowserState = newState;

    switch (mBrowserState)
    {
      case NAMBrowserState::Empty:
        mClearButton->Hide(true);
        mGetButton->Hide(true);
        break;
      case NAMBrowserState::Loaded:
        mClearButton->Hide(false);
        mGetButton->Hide(true);
        break;
    }
  }

  bool mMenuOpen = false;
  std::string mCurrentFile;
  std::future<nam_library::ImportResult> mImport;
  IPopupMenu::Item* mImportFileItem = nullptr;
  IPopupMenu::Item* mImportFolderItem = nullptr;
  IPopupMenu::Item* mRevealLibraryItem = nullptr;
  IPopupMenu::Item* mRefreshLibraryItem = nullptr;
  WDL_String mDefaultLabelStr;
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  NAMFileNameControl* mFileNameControl = nullptr;
  IVStyle mStyle;
  IBitmap mBitmap;
  ISVG mLoadSVG, mClearSVG, mLeftSVG, mRightSVG, mGlobeSVG;
  int mClearMsgTag;

  // new members for the "Get" button
  const char* mGetButtonLabel;
  const char* mGetButtonURL;
  WDL_String mBundledModelsSubdirectory;
  WDL_String mBundledModelsPath;
  NAMBrowserState mBrowserState;
  NAMSquareButtonControl* mClearButton = nullptr;
  NAMGetButtonControl* mGetButton = nullptr;
};

class NAMPresetBrowserControl : public IDirBrowseControlBase
{
public:
  NAMPresetBrowserControl(const IRECT& bounds, const IVStyle& style, const IBitmap& bitmap, const ISVG& saveSVG,
                          const ISVG& openSVG)
  : IDirBrowseControlBase(bounds, "fxp", false, true)
  , mStyle(style.WithColor(kFG, COLOR_TRANSPARENT).WithDrawFrame(false))
  , mBitmap(bitmap)
  , mSaveSVG(saveSVG)
  , mOpenSVG(openSVG)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.DrawFittedBitmap(mBitmap, mRECT); }

  void OnAttached() override
  {
    ResolvePaths();
    LoadImportedDirectories();
    mCurrentPresetPath = PLUG()->GetCurrentPresetFile();
    RefreshMenu();

    auto savePreset = [this](IControl* pCaller) {
      EnsureUserPresetDirectory();
      WDL_String fileName("Puke Amp Preset.fxp");
      WDL_String path(mUserPresetDirectory.c_str());
      auto* ui = pCaller->GetUI();
      auto* plugin = PLUG();
      ui->PromptForFile(
        fileName, path, EFileAction::Save, "fxp",
        [this, plugin, ui](const WDL_String& selectedFile, const WDL_String&) {
          if (!selectedFile.GetLength())
            return;
          if (!plugin->SavePresetFile(selectedFile.Get(), mBundledModelsRoot.c_str()))
          {
            ui->ShowMessageBox(plugin->GetLastPresetError(), "Failed to save preset", kMB_OK);
            return;
          }
          AddParentAsImportedIfNeeded(selectedFile.Get());
          mCurrentPresetPath = selectedFile.Get();
          RefreshMenu();
          UpdateLabel();
        });
    };

    auto openPreset = [this](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path(mUserPresetDirectory.c_str());
      auto* ui = pCaller->GetUI();
      ui->PromptForFile(
        fileName, path, EFileAction::Open, "fxp",
        [this, ui](const WDL_String& selectedFile, const WDL_String&) {
          if (!selectedFile.GetLength())
            return;
          if (LoadPreset(selectedFile.Get()))
          {
            AddParentAsImportedIfNeeded(selectedFile.Get());
            RefreshMenu();
          }
        });
    };

    auto choosePreset = [this](IControl* pCaller) {
      RefreshMenu();
      CheckSelectedItem();
      pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
    };

    auto padded = mRECT.GetPadded(-4.f).GetHPadded(-2.f);
    const auto iconSize = 22.f;
    const auto saveArea = padded.ReduceFromLeft(28.f).GetCentredInside(iconSize, iconSize);
    const auto openArea = padded.ReduceFromRight(28.f).GetCentredInside(iconSize, iconSize);

    auto* saveButton = AddChildControl(new NAMSquareButtonControl(saveArea, DefaultClickActionFunc, mSaveSVG));
    saveButton->SetTooltip("Save preset");
    saveButton->SetAnimationEndActionFunction(savePreset);

    auto* openButton = AddChildControl(new NAMSquareButtonControl(openArea, DefaultClickActionFunc, mOpenSVG));
    openButton->SetTooltip("Open preset file");
    openButton->SetAnimationEndActionFunction(openPreset);

    mPresetNameControl = new NAMFileNameControl(padded, "Choose Preset...", mStyle);
    AddChildControl(mPresetNameControl);
    mPresetNameControl->SetTooltip("Choose a factory, user, or imported preset");
    mPresetNameControl->SetAnimationEndActionFunction(choosePreset);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (!pSelectedMenu)
      return;
    auto* item = pSelectedMenu->GetChosenItem();
    if (!item)
      return;

    const int fileIndex = mItems.Find(item);
    if (fileIndex >= 0)
    {
      mSelectedItemIndex = fileIndex;
      WDL_String fileName;
      GetSelectedFile(fileName);
      LoadPreset(fileName.Get());
      return;
    }

    if (item == mAddFolderItem)
      PromptToAddFolder();
    else if (item == mRevealUserFolderItem)
    {
      WDL_String path(mUserPresetDirectory.c_str());
      GetUI()->RevealPathInExplorerOrFinder(path);
    }
    else if (item == mRefreshItem)
      RefreshMenu();
    else
    {
      for (const auto& removable : mRemoveFolderItems)
      {
        if (item == removable.first)
        {
          RemoveImportedDirectory(removable.second);
          break;
        }
      }
    }
  }

private:
  static void AppendPathComponent(WDL_String& path, const char* component)
  {
    if (!CStringHasContents(path.Get()) || !CStringHasContents(component))
      return;
    if (!WDL_IS_DIRCHAR(path.Get()[path.GetLength() - 1]))
      path.Append(WDL_DIRCHAR_STR);
    path.Append(component);
  }

  static bool IsDirectory(const std::string& path)
  {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(std::filesystem::u8path(path), error);
  }

  static bool SamePath(const std::string& lhs, const std::string& rhs)
  {
    if (lhs.empty() || rhs.empty())
      return false;
    std::error_code error;
    const auto left = std::filesystem::weakly_canonical(std::filesystem::u8path(lhs), error);
    if (error)
      return false;
    const auto right = std::filesystem::weakly_canonical(std::filesystem::u8path(rhs), error);
    return !error && left == right;
  }

  void ResolvePaths()
  {
    WDL_String resourcePath;
    BundleResourcePath(resourcePath,
#ifdef OS_WIN
                       static_cast<PluginIDType>(GetUI()->GetWinModuleHandle())
#elif defined OS_MAC || defined OS_IOS
                       GetUI()->GetBundleID()
#else
                       nullptr
#endif
    );
#ifdef OS_WIN
    if (!resourcePath.GetLength())
      PluginPath(resourcePath, static_cast<PluginIDType>(GetUI()->GetWinModuleHandle()));
#endif
    AppendPathComponent(resourcePath, "Models");
    mBundledModelsRoot = resourcePath.Get();
    const auto factoryPath = std::filesystem::u8path(mBundledModelsRoot) / "Presets";
    mFactoryPresetDirectory = factoryPath.string();

    WDL_String appSupport;
    AppSupportPath(appSupport, false);
    AppendPathComponent(appSupport, PLUG_NAME);
    mPreferencesDirectory = appSupport.Get();
    const auto preferencesPath = std::filesystem::u8path(mPreferencesDirectory);
    mUserPresetDirectory = (preferencesPath / "Presets").string();
    mImportedDirectoriesFile = (preferencesPath / "preset-folders.json").string();
    EnsureUserPresetDirectory();
  }

  void EnsureUserPresetDirectory()
  {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::u8path(mUserPresetDirectory), error);
  }

  void LoadImportedDirectories()
  {
    mImportedDirectories.clear();
    std::ifstream input(std::filesystem::u8path(mImportedDirectoriesFile));
    if (!input)
      return;
    try
    {
      const auto json = nlohmann::json::parse(input);
      if (!json.is_array())
        return;
      for (const auto& entry : json)
      {
        if (entry.is_string())
          AddImportedDirectory(entry.get<std::string>(), false);
      }
    }
    catch (const nlohmann::json::exception&)
    {
    }
  }

  void SaveImportedDirectories() const
  {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::u8path(mPreferencesDirectory), error);
    std::ofstream output(std::filesystem::u8path(mImportedDirectoriesFile), std::ios::trunc);
    if (output)
      output << nlohmann::json(mImportedDirectories).dump(2);
  }

  void AddImportedDirectory(const std::string& directory, bool save = true)
  {
    if (!IsDirectory(directory) || SamePath(directory, mFactoryPresetDirectory) ||
        SamePath(directory, mUserPresetDirectory))
      return;
    for (const auto& existing : mImportedDirectories)
      if (SamePath(existing, directory))
        return;
    mImportedDirectories.push_back(directory);
    if (save)
      SaveImportedDirectories();
  }

  void RemoveImportedDirectory(const std::string& directory)
  {
    mImportedDirectories.erase(
      std::remove_if(mImportedDirectories.begin(), mImportedDirectories.end(),
                     [&](const std::string& entry) { return SamePath(entry, directory); }),
      mImportedDirectories.end());
    SaveImportedDirectories();
    RefreshMenu();
  }

  void AddParentAsImportedIfNeeded(const char* filePath)
  {
    const auto parent = std::filesystem::u8path(filePath).parent_path().string();
    const auto oldSize = mImportedDirectories.size();
    AddImportedDirectory(parent);
    if (mImportedDirectories.size() != oldSize)
      RefreshMenu();
  }

  void PromptToAddFolder()
  {
    WDL_String directory(mUserPresetDirectory.c_str());
    GetUI()->PromptForDirectory(directory, [this](const WDL_String&, const WDL_String& selectedDirectory) {
      if (selectedDirectory.GetLength())
      {
        AddImportedDirectory(selectedDirectory.Get());
        RefreshMenu();
      }
    });
  }

  void RefreshMenu()
  {
    ClearPathList();
    if (IsDirectory(mFactoryPresetDirectory))
      AddPath(mFactoryPresetDirectory.c_str(), "Factory Presets");
    if (IsDirectory(mUserPresetDirectory))
      AddPath(mUserPresetDirectory.c_str(), "User Presets");
    for (const auto& directory : mImportedDirectories)
    {
      if (!IsDirectory(directory))
        continue;
      const auto name = std::filesystem::u8path(directory).filename().string();
      const auto label = std::string("Imported: ") + (name.empty() ? directory : name);
      AddPath(directory.c_str(), label.c_str());
    }
    SetupMenu();
    if (!mCurrentPresetPath.empty())
      SetSelectedFile(mCurrentPresetPath.c_str());

    mMainMenu.AddSeparator();
    mAddFolderItem = mMainMenu.AddItem("Add Preset Folder...");
    mRevealUserFolderItem = mMainMenu.AddItem("Reveal User Presets Folder");
    mRefreshItem = mMainMenu.AddItem("Refresh Presets");
    mRemoveFolderItems.clear();
    if (!mImportedDirectories.empty())
    {
      auto* removeMenu = new IPopupMenu();
      for (const auto& directory : mImportedDirectories)
      {
        auto* item = removeMenu->AddItem(directory.c_str());
        mRemoveFolderItems.emplace_back(item, directory);
      }
      mMainMenu.AddItem("Remove Imported Folder", removeMenu);
    }
  }

  bool LoadPreset(const char* filePath)
  {
    auto* plugin = PLUG();
    if (!plugin->LoadPresetFile(filePath, mBundledModelsRoot.c_str()))
    {
      GetUI()->ShowMessageBox(plugin->GetLastPresetError(), "Preset loaded with errors", kMB_OK);
      return false;
    }
    mCurrentPresetPath = filePath;
    UpdateLabel();
    return true;
  }

  void UpdateLabel()
  {
    if (mPresetNameControl && !mCurrentPresetPath.empty())
      mPresetNameControl->SetLabelAndTooltipEllipsizing(WDL_String(mCurrentPresetPath.c_str()));
  }

  IVStyle mStyle;
  IBitmap mBitmap;
  ISVG mSaveSVG, mOpenSVG;
  NAMFileNameControl* mPresetNameControl = nullptr;
  std::string mBundledModelsRoot;
  std::string mFactoryPresetDirectory;
  std::string mUserPresetDirectory;
  std::string mPreferencesDirectory;
  std::string mImportedDirectoriesFile;
  std::string mCurrentPresetPath;
  std::vector<std::string> mImportedDirectories;
  IPopupMenu::Item* mAddFolderItem = nullptr;
  IPopupMenu::Item* mRevealUserFolderItem = nullptr;
  IPopupMenu::Item* mRefreshItem = nullptr;
  std::vector<std::pair<IPopupMenu::Item*, std::string>> mRemoveFolderItems;
};

class NAMMeterControl : public IVPeakAvgMeterControl<>, public IBitmapBase
{
  static constexpr float KMeterMin = -70.0f;
  static constexpr float KMeterMax = -0.01f;

public:
  NAMMeterControl(const IRECT& bounds, const IBitmap& bitmap, const IVStyle& style)
  : IVPeakAvgMeterControl<>(bounds, "", style.WithShowValue(false).WithDrawFrame(false).WithWidgetFrac(0.8),
                            EDirection::Vertical, {}, 0, KMeterMin, KMeterMax, {})
  , IBitmapBase(bitmap)
  {
    SetPeakSize(1.0f);
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  virtual void OnResize() override
  {
    SetTargetRECT(MakeRects(mRECT));
    mWidgetBounds = mWidgetBounds.GetMidHPadded(5).GetVPadded(10);
    MakeTrackRects(mWidgetBounds);
    MakeStepRects(mWidgetBounds, mNSteps);
    SetDirty(false);
  }

  void DrawBackground(IGraphics& g, const IRECT& r) override { g.DrawFittedBitmap(mBitmap, r); }

  void DrawTrackHandle(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    if (r.H() > 2)
      g.FillRect(GetColor(kX1), r, &mBlend);
  }

  void DrawPeak(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    g.DrawGrid(COLOR_BLACK, mTrackBounds.Get()[chIdx], 10, 2);
    g.FillRect(GetColor(kX3), r, &mBlend);
  }
};

// Container where we can refer to children by names instead of indices
class IContainerBaseWithNamedChildren : public IContainerBase
{
public:
  IContainerBaseWithNamedChildren(const IRECT& bounds)
  : IContainerBase(bounds) {};
  ~IContainerBaseWithNamedChildren() = default;

protected:
  IControl* AddNamedChildControl(IControl* control, std::string name, int ctrlTag = kNoTag, const char* group = "")
  {
    // Make sure we haven't already used this name
    assert(mChildNameIndexMap.find(name) == mChildNameIndexMap.end());
    mChildNameIndexMap[name] = NChildren();
    return AddChildControl(control, ctrlTag, group);
  };

  IControl* GetNamedChild(std::string name)
  {
    const int index = mChildNameIndexMap[name];
    return GetChild(index);
  };


private:
  std::unordered_map<std::string, int> mChildNameIndexMap;
}; // class IContainerBaseWithNamedChildren


struct PossiblyKnownParameter
{
  bool known = false;
  double value = 0.0;
};

struct ModelInfo
{
  PossiblyKnownParameter sampleRate;
  PossiblyKnownParameter inputCalibrationLevel;
  PossiblyKnownParameter outputCalibrationLevel;
};

class ModelInfoControl : public IContainerBaseWithNamedChildren
{
public:
  ModelInfoControl(const IRECT& bounds, const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mStyle(style) {};

  void ClearModelInfo()
  {
    static_cast<IVLabelControl*>(GetNamedChild(mControlNames.sampleRate))->SetStr("");
    mHasInfo = false;
  };

  void Hide(bool hide) override
  {
    // Don't show me unless I have info to show!
    IContainerBase::Hide(hide || (!mHasInfo));
  };

  void OnAttached() override
  {
    AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 0), "Model information:", mStyle));
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 1), "", mStyle), mControlNames.sampleRate);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 2), "", mStyle), mControlNames.inputCalibrationLevel);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 3), "", mStyle), mControlNames.outputCalibrationLevel);
  };

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto SetControlStr = [&](const std::string& name, const PossiblyKnownParameter& p, const std::string& units,
                             const std::string& childName) {
      std::stringstream ss;
      ss << name << ": ";
      if (p.known)
      {
        ss << p.value << " " << units;
      }
      else
      {
        ss << "(Unknown)";
      }
      static_cast<IVLabelControl*>(GetNamedChild(childName))->SetStr(ss.str().c_str());
    };

    SetControlStr("Sample rate", modelInfo.sampleRate, "Hz", mControlNames.sampleRate);
    // SetControlStr(
    //   "Input calibration level", modelInfo.inputCalibrationLevel, "dBu", mControlNames.inputCalibrationLevel);
    // SetControlStr(
    //   "Output calibration level", modelInfo.outputCalibrationLevel, "dBu", mControlNames.outputCalibrationLevel);

    mHasInfo = true;
  };

private:
  const IVStyle mStyle;
  struct
  {
    const std::string sampleRate = "sampleRate";
    // const std::string inputCalibrationLevel = "inputCalibrationLevel";
    // const std::string outputCalibrationLevel = "outputCalibrationLevel";
  } mControlNames;
  // Do I have info?
  bool mHasInfo = false;
};

class OutputModeControl : public IVRadioButtonControl
{
public:
  OutputModeControl(const IRECT& bounds, int paramIdx, const IVStyle& style, float buttonSize)
  : IVRadioButtonControl(
      bounds, paramIdx, {}, "Output Mode", style, EVShape::Ellipse, EDirection::Vertical, buttonSize) {};

  void SetNormalizedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Normalized";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(1)->Set(ss.str().c_str());
  };
  void SetCalibratedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Calibrated";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(2)->Set(ss.str().c_str());
  };
};

class NAMFXPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMFXPageControl(const IRECT& bounds, const IBitmap& background, const IBitmap& fileBackground,
                   const IBitmap& knobBackground, const IBitmap& switchBitmap, const ISVG& closeSVG,
                   const ISVG& fileSVG, const ISVG& leftSVG, const ISVG& rightSVG, const ISVG& globeSVG,
                   const ISVG& addPedalSVG, const ISVG& moveLeftSVG, const ISVG& moveRightSVG,
                   const ISVG& removePedalSVG, const ISVG& effectOffSVG, const ISVG& effectOnSVG,
                   const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mBackground(background)
  , mFileBackground(fileBackground)
  , mKnobBackground(knobBackground)
  , mSwitchBitmap(switchBitmap)
  , mCloseSVG(closeSVG)
  , mFileSVG(fileSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mGlobeSVG(globeSVG)
  , mAddPedalSVG(addPedalSVG)
  , mMoveLeftSVG(moveLeftSVG)
  , mMoveRightSVG(moveRightSVG)
  , mRemovePedalSVG(removePedalSVG)
  , mEffectOffSVG(effectOffSVG)
  , mEffectOnSVG(effectOnSVG)
  , mStyle(style)
  {
    mIgnoreMouse = false;
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HidePage(true);
      return true;
    }
    return false;
  }

  void HidePage(bool hide)
  {
    Hide(hide);
    ForAllChildrenFunc([hide](int, IControl* child) { child->Hide(hide); });
    if (!hide)
      RefreshFromPlugin();
    if (GetUI())
    {
      GetUI()->SetAllControlsDirty();
      GetUI()->UpdateTooltips();
    }
  }

  void RefreshFromPlugin()
  {
    auto* plugin = PLUG();
    for (int slot = 0; slot < kMaxFXSlots; ++slot)
    {
      if (mSlotButtons[slot])
      {
        const bool loaded = plugin->GetFXModelPath(slot).GetLength() > 0;
        mSlotButtons[slot]->SetLoaded(loaded);
        mSlotButtons[slot]->SetSelected(slot == mSelectedSlot);
        std::stringstream tooltip;
        tooltip << "Effect slot " << (slot + 1) << (loaded ? " (loaded)" : " (empty)");
        mSlotButtons[slot]->SetTooltip(tooltip.str().c_str());
      }
    }

    const WDL_String& path = plugin->GetFXModelPath(mSelectedSlot);
    if (mBrowser)
    {
      if (path.GetLength())
        mBrowser->SetDisplayedFile(path);
      else
        mBrowser->ClearDisplayedFile();
    }
    RebindEditorControls();
  }

  void SelectSlot(int slot)
  {
    mSelectedSlot = std::clamp(slot, 0, kMaxFXSlots - 1);
    PLUG()->SetFXEditorSlot(mSelectedSlot);
    RefreshFromPlugin();
  }

  void OnAttached() override
  {
    const auto bounds = GetRECT();
    const auto content = bounds.GetPadded(-22.f);
    const auto titleStyle = DEFAULT_STYLE.WithValueText(IText(25, COLOR_WHITE, "Michroma-Regular"))
                              .WithDrawFrame(false);
    AddChildControl(new IBitmapControl(bounds, mBackground))->SetIgnoreMouse(true);
    AddChildControl(new IVLabelControl(content.GetFromTop(42.f), "DRIVE PEDAL FX CHAIN", titleStyle));
    auto* closeButton = AddChildControl(new NAMSquareButtonControl(
      CornerButtonArea(bounds).GetScaledAboutCentre(1.4f).GetHShifted(-40.f),
      [this](IControl*) { HidePage(true); }, mCloseSVG));
    closeButton->SetTooltip("Close Drive Pedal FX chain");

    auto slotArea = content.GetFromTop(34.f).GetVShifted(48.f);
    for (int slot = 0; slot < kMaxFXSlots; ++slot)
    {
      auto cell = slotArea.GetGridCell(0, slot, 1, kMaxFXSlots).GetCentredInside(32.f, 32.f);
      mSlotButtons[slot] = new NAMEffectSlotButtonControl(
        cell, [this, slot](IControl*) { SelectSlot(slot); }, mEffectOffSVG, mEffectOnSVG);
      AddChildControl(mSlotButtons[slot]);
    }

    auto browserArea = content.GetFromTop(34.f).GetVShifted(92.f).GetMidHPadded(235.f);
    auto completion = [this](const WDL_String& fileName, const WDL_String&) {
      if (!fileName.GetLength())
        return;
      const std::string error = PLUG()->StageFXModel(mSelectedSlot, fileName);
      if (!error.empty())
      {
        std::stringstream message;
        message << "Failed to load pedal NAM model:\n\n" << error;
        GetUI()->ShowMessageBox(message.str().c_str(), "Failed to load pedal", kMB_OK);
      }
      RefreshFromPlugin();
    };
    mBrowser = new NAMFileBrowserControl(
      browserArea, kMsgTagClearFXModel, "Select pedal model...", "nam", completion, mStyle, mFileSVG,
      mCloseSVG, mLeftSVG, mRightSVG, mFileBackground, mGlobeSVG, "", "", "FX", true);
    AddChildControl(mBrowser, kCtrlTagFXFileBrowser);

    const auto controlsArea = content.GetFromTop(NAM_KNOB_HEIGHT).GetVShifted(137.f);
    const char* labels[] = {"INPUT", "OUTPUT", "BASS", "MIDDLE", "TREBLE"};
    const EFXSlotParam offsets[] = {kFXInputOffset, kFXOutputOffset, kFXBassOffset, kFXMidOffset, kFXTrebleOffset};
    for (int i = 0; i < 5; ++i)
    {
      auto area = controlsArea.GetGridCell(0, i, 1, 5).GetPadded(-3.f);
      mKnobs[i] = new NAMKnobControl(area, FXParamIndex(0, offsets[i]), labels[i], mStyle, mKnobBackground);
      AddChildControl(mKnobs[i]);
    }

    const auto switchesArea = content.GetFromTop(48.f).GetVShifted(269.f);
    mSlotSwitch = new NAMSwitchControl(switchesArea.GetGridCell(0, 0, 1, 4).GetPadded(-5.f),
                                       FXParamIndex(0, kFXEnabledOffset), "Off/On", mStyle, mSwitchBitmap);
    AddChildControl(mSlotSwitch);

    const auto editButtons = switchesArea.GetGridCell(0, 3, 1, 4).GetPadded(-3.f);
    AddChildControl(new NAMSquareButtonControl(editButtons.GetGridCell(0, 0, 1, 3).GetPadded(-2.f),
      [this](IControl*) { if (mSelectedSlot > 0) { PLUG()->MoveFXModel(mSelectedSlot, mSelectedSlot - 1); SelectSlot(mSelectedSlot - 1); } },
      mMoveLeftSVG))->SetTooltip("Move pedal left");
    AddChildControl(new NAMSquareButtonControl(editButtons.GetGridCell(0, 1, 1, 3).GetPadded(-2.f),
      [this](IControl*) { if (mSelectedSlot + 1 < kMaxFXSlots) { PLUG()->MoveFXModel(mSelectedSlot, mSelectedSlot + 1); SelectSlot(mSelectedSlot + 1); } },
      mMoveRightSVG))->SetTooltip("Move pedal right");
    AddChildControl(new NAMSquareButtonControl(editButtons.GetGridCell(0, 2, 1, 3).GetPadded(-2.f),
      [this](IControl*) { PLUG()->ClearFXModel(mSelectedSlot); RefreshFromPlugin(); }, mRemovePedalSVG))
      ->SetTooltip("Remove pedal");

    const auto addArea = content.GetFromBottom(32.f).GetFromLeft(150.f);
    AddChildControl(new NAMSquareButtonControl(addArea, [this](IControl*) {
      for (int slot = 0; slot < kMaxFXSlots; ++slot)
      {
        if (!PLUG()->GetFXModelPath(slot).GetLength())
        {
          SelectSlot(slot);
          return;
        }
      }
    }, mAddPedalSVG))->SetTooltip("Add pedal");

    SelectSlot(0);
  }

private:
  void RebindEditorControls()
  {
    const EFXSlotParam offsets[] = {kFXInputOffset, kFXOutputOffset, kFXBassOffset, kFXMidOffset, kFXTrebleOffset};
    for (int i = 0; i < 5; ++i)
    {
      const int paramIdx = FXParamIndex(mSelectedSlot, offsets[i]);
      mKnobs[i]->SetParamIdx(paramIdx);
      mKnobs[i]->SetValueFromDelegate(PLUG()->GetParam(paramIdx)->GetNormalized());
    }
    const int enabledIdx = FXParamIndex(mSelectedSlot, kFXEnabledOffset);
    mSlotSwitch->SetParamIdx(enabledIdx);
    mSlotSwitch->SetValueFromDelegate(PLUG()->GetParam(enabledIdx)->GetNormalized());
  }

  IBitmap mBackground, mFileBackground, mKnobBackground, mSwitchBitmap;
  ISVG mCloseSVG, mFileSVG, mLeftSVG, mRightSVG, mGlobeSVG;
  ISVG mAddPedalSVG, mMoveLeftSVG, mMoveRightSVG, mRemovePedalSVG, mEffectOffSVG, mEffectOnSVG;
  IVStyle mStyle;
  int mSelectedSlot = 0;
  std::array<NAMEffectSlotButtonControl*, kMaxFXSlots> mSlotButtons{};
  std::array<NAMKnobControl*, 5> mKnobs{};
  NAMSwitchControl* mSlotSwitch = nullptr;
  NAMFileBrowserControl* mBrowser = nullptr;
};

class NAMSettingsPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMSettingsPageControl(const IRECT& bounds, const IBitmap& bitmap, const IBitmap& inputLevelBackgroundBitmap,
                         const IBitmap& switchBitmap, ISVG closeSVG, ISVG ratLogoSVG,
                         const IVStyle& style, const IVStyle& radioButtonStyle)
  : IContainerBaseWithNamedChildren(bounds)
  , mAnimationTime(0)
  , mBitmap(bitmap)
  , mInputLevelBackgroundBitmap(inputLevelBackgroundBitmap)
  , mSwitchBitmap(switchBitmap)
  , mStyle(style)
  , mRadioButtonStyle(radioButtonStyle)
  , mCloseSVG(closeSVG)
  , mRatLogoSVG(ratLogoSVG)
  {
    mIgnoreMouse = false;
  }

  void ClearModelInfo()
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->ClearModelInfo();
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }

    return false;
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;

    if (hide == false)
    {
      mHide = false;
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle = DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular"))
                                 .WithDrawFrame(false)
                                 .WithShadowOffset(2.f);
    const auto text = IText(DEFAULT_TEXT_SIZE, EAlign::Center, PluginColors::HELP_TEXT);
    const auto leftText = text.WithAlign(EAlign::Near);
    const auto style = mStyle.WithDrawFrame(false).WithValueText(text);
    const IVStyle leftStyle = style.WithValueText(leftText);

    AddNamedChildControl(new IBitmapControl(GetRECT(), mBitmap), mControlNames.bitmap)->SetIgnoreMouse(true);
    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "SETTINGS", titleStyle), mControlNames.title);

    // Attach input/output calibration controls
    {
      const float height = NAM_KNOB_HEIGHT + NAM_SWTICH_HEIGHT + 10.0f;
      const float width = titleArea.W();
      const auto inputOutputArea = titleArea.GetFromBottom(height).GetTranslated(0.0f, height);
      const auto inputArea = inputOutputArea.GetFromLeft(0.5f * width);
      const auto outputArea = inputOutputArea.GetFromRight(0.5f * width);

      const float knobWidth = 87.0f; // HACK based on looking at the main page knobs.
      const auto inputLevelArea =
        inputArea.GetFromTop(NAM_KNOB_HEIGHT).GetFromBottom(25.0f).GetMidHPadded(0.5f * knobWidth);
      const auto inputSwitchArea = inputArea.GetFromBottom(NAM_SWTICH_HEIGHT).GetMidHPadded(0.5f * knobWidth);

      auto* inputLevelControl = AddNamedChildControl(
        new InputLevelControl(inputLevelArea, kInputCalibrationLevel, mInputLevelBackgroundBitmap, text),
        mControlNames.inputCalibrationLevel, kCtrlTagInputCalibrationLevel);
      inputLevelControl->SetTooltip(
        "The analog level, in dBu RMS, that corresponds to digital level of 0 dBFS peak in the host as its signal "
        "enters this plugin.");
      AddNamedChildControl(
        new NAMSwitchControl(inputSwitchArea, kCalibrateInput, "Calibrate Input", mStyle, mSwitchBitmap),
        mControlNames.calibrateInput, kCtrlTagCalibrateInput);

      // Same-ish height & width as input controls
      const auto outputRadioArea = outputArea.GetFromBottom(
        1.1f * (inputLevelArea.H() + inputSwitchArea.H())); // .GetMidHPadded(0.55f * knobWidth);
      const float buttonSize = 10.0f;
      auto* outputModeControl =
        AddNamedChildControl(new OutputModeControl(outputRadioArea, kOutputMode, mRadioButtonStyle, buttonSize),
                             mControlNames.outputMode, kCtrlTagOutputMode);
      outputModeControl->SetTooltip(
        "How to adjust the level of the output.\nRaw=No adjustment.\nNormalized=Adjust the level so that all models "
        "are about the same loudness.\nCalibrated=Match the input's digital-analog calibration.");
    }

    const float halfWidth = PLUG_WIDTH / 2.0f - pad;
    const auto bottomArea = GetRECT().GetPadded(-pad).GetFromBottom(78.0f);
    const float lineHeight = 15.0f;
    const auto ratLogoArea = bottomArea.GetFromLeft(90.0f).GetFromBottom(60.0f);
    const auto modelInfoArea =
      bottomArea.GetFromLeft(halfWidth).GetReducedFromLeft(100.0f).GetFromTop(4 * lineHeight);
    const auto aboutArea = bottomArea.GetFromRight(halfWidth).GetFromTop(5 * lineHeight);
    auto* ratLogo = AddNamedChildControl(
      new ISVGButtonControl(
        ratLogoArea,
        [](IControl* pCaller) {
          WDL_String url("https://theratlab.org");
          pCaller->GetUI()->OpenURL(url.Get());
        },
        mRatLogoSVG, mRatLogoSVG),
      mControlNames.ratLogo);
    ratLogo->SetTooltip("Visit the RATLab website");
    AddNamedChildControl(new ModelInfoControl(modelInfoArea, leftStyle), mControlNames.modelInfo);
    AddNamedChildControl(new AboutControl(aboutArea, leftStyle, leftText), mControlNames.about);

    auto closeAction = [&](IControl* pCaller) {
      static_cast<NAMSettingsPageControl*>(pCaller->GetParent())->HideAnimated(true);
    };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->SetModelInfo(modelInfo);
  };

private:
  IBitmap mBitmap;
  IBitmap mInputLevelBackgroundBitmap;
  IBitmap mSwitchBitmap;
  IVStyle mStyle;
  IVStyle mRadioButtonStyle;
  ISVG mCloseSVG, mRatLogoSVG;
  int mAnimationTime = 200;
  bool mWillHide = false;

  // Names for controls
  // Make sure that these are all unique and that you use them with AddNamedChildControl
  struct ControlNames
  {
    const std::string about = "About";
    const std::string bitmap = "Bitmap";
    const std::string calibrateInput = "CalibrateInput";
    const std::string close = "Close";
    const std::string inputCalibrationLevel = "InputCalibrationLevel";
    const std::string modelInfo = "ModelInfo";
    const std::string outputMode = "OutputMode";
    const std::string ratLogo = "RatLogo";
    const std::string title = "Title";
  } mControlNames;

  class InputLevelControl : public IEditableTextControl
  {
  public:
    InputLevelControl(const IRECT& bounds, int paramIdx, const IBitmap& bitmap, const IText& text = DEFAULT_TEXT,
                      const IColor& BGColor = DEFAULT_BGCOLOR)
    : IEditableTextControl(bounds, "", text, BGColor)
    , mBitmap(bitmap)
    {
      SetParamIdx(paramIdx);
    };

    void Draw(IGraphics& g) override
    {
      g.DrawFittedBitmap(mBitmap, mRECT);
      ITextControl::Draw(g);
    };

    void SetValueFromUserInput(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromUserInput(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

    void SetValueFromDelegate(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromDelegate(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      SetStr(s.c_str());
      SetDirty(false);
    };

  private:
    std::string ConvertToString(const double normalizedValue)
    {
      const double naturalValue = GetParam()->FromNormalized(normalizedValue);
      // And make the value to display
      std::stringstream ss;
      ss << naturalValue << " dBu";
      std::string s = ss.str();
      return s;
    };

    IBitmap mBitmap;
  };

  class AboutControl : public IContainerBase
  {
  public:
    AboutControl(const IRECT& bounds, const IVStyle& style, const IText& text)
    : IContainerBase(bounds)
    , mStyle(style)
    , mText(text) {};

    void OnAttached() override
    {
      WDL_String verStr, buildInfoStr;
      PLUG()->GetPluginVersionStr(verStr);

      buildInfoStr.SetFormatted(100, "Version %s %s %s", verStr.Get(), PLUG()->GetArchStr(),
                                PLUG()->GetAPIStr());

      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 0), "Puke Amp adapted from Neural Amp Modeler",
                                      "https://www.neuralampmodeler.com", mText, COLOR_TRANSPARENT,
                                      PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 1), "NAM created by Steven Atkinson, adapted by the RATLab", mStyle));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 2), buildInfoStr.Get(), mStyle));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 3),
                                      "Plug-in adapted from Steve Atkinson, Oli Larkin",
                                      "https://github.com/sdatkinson/NeuralAmpModelerPlugin/graphs/contributors", mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new ThirdPartyNoticesControl(GetRECT().SubRectVertical(5, 4), mText));
    };

  private:
    class ThirdPartyNoticesControl : public IURLControl
    {
    public:
      ThirdPartyNoticesControl(const IRECT& bounds, const IText& text)
      : IURLControl(bounds, "Third party notices", "", text, COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO,
                    PluginColors::HELP_TEXT_CLICKED)
      {
      }

      void OnMouseDown(float x, float y, const IMouseMod& mod) override
      {
        WDL_String path;
        bool opened = false;

        if (ResolveNoticesPath(GetUI(), path))
          opened = OpenNoticesPath(GetUI(), path);

        if (!opened)
          ShowOpenError(GetUI());

        GetUI()->ReleaseMouseCapture();
        mClicked = true;
        SetDirty(false);
      }

    private:
      static bool FileExists(const WDL_String& path)
      {
        if (!CStringHasContents(path.Get()))
          return false;

        FILE* file = WDL_fopenA(path.Get(), "rb");
        if (file == nullptr)
          return false;

        fclose(file);
        return true;
      }

      static bool TryNoticePathInDirectory(WDL_String& result, const WDL_String& directory)
      {
        if (!CStringHasContents(directory.Get()))
          return false;

        WDL_String candidate(directory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];

        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      // AAX (and similar) load the binary from Contents\x64 or Contents\Win32 while notices live in
      // Contents\Resources. Same layout as a VST3 bundle; this path is not covered by BundleResourcePath
      // when the plug-in is built as AAX_API only (no VST3_API).
      static bool TryNoticePathSiblingResources(WDL_String& result, const WDL_String& moduleDirectory)
      {
        if (!CStringHasContents(moduleDirectory.Get()))
          return false;

        WDL_String candidate(moduleDirectory);
        const char lastChar = candidate.Get()[candidate.GetLength() - 1];
        if (!WDL_IS_DIRCHAR(lastChar))
          candidate.Append(WDL_DIRCHAR_STR);

        candidate.Append("..");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append("Resources");
        candidate.Append(WDL_DIRCHAR_STR);
        candidate.Append(kNoticesFileName);

        if (!FileExists(candidate))
          return false;

        result.Set(candidate.Get());
        return true;
      }

      static bool ResolveNoticesPath(IGraphics* pGraphics, WDL_String& path)
      {
        path.Set("");

        if (pGraphics == nullptr)
          return false;

#ifdef OS_WIN
        WDL_String directory;
        const auto moduleHandle = static_cast<PluginIDType>(pGraphics->GetWinModuleHandle());

        BundleResourcePath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        directory.Set("");
        PluginPath(directory, moduleHandle);
        if (TryNoticePathInDirectory(path, directory))
          return true;

        if (TryNoticePathSiblingResources(path, directory))
          return true;
#endif

        const auto resourceLocation =
          LocateResource(kNoticesFileName, "txt", path, pGraphics->GetBundleID(), pGraphics->GetWinModuleHandle(),
                         pGraphics->GetSharedResourcesSubPath());

        return resourceLocation == EResourceLocation::kAbsolutePath && FileExists(path);
      }

      static bool OpenNoticesPath(IGraphics* pGraphics, const WDL_String& path)
      {
        if (pGraphics == nullptr || !CStringHasContents(path.Get()))
          return false;

#ifdef OS_WIN
        WCHAR pathWide[IPLUG_WIN_MAX_WIDE_PATH];
        UTF8ToUTF16(pathWide, path.Get(), IPLUG_WIN_MAX_WIDE_PATH);

        if (pathWide[0] == 0)
          return false;

        WCHAR canon[IPLUG_WIN_MAX_WIDE_PATH];
        const DWORD nCanon = GetFullPathNameW(pathWide, IPLUG_WIN_MAX_WIDE_PATH, canon, nullptr);
        const WCHAR* const launchPath = (nCanon > 0 && nCanon < IPLUG_WIN_MAX_WIDE_PATH) ? canon : pathWide;

        return ShellExecuteW(nullptr, L"open", launchPath, nullptr, nullptr, SW_SHOWNORMAL) > HINSTANCE(32);
#else
        return pGraphics->OpenURL(path.Get());
#endif
      }

      static void ShowOpenError(IGraphics* pGraphics)
      {
        if (pGraphics == nullptr)
          return;

        const char* const title = "Third party notices";
        const char* const message = "Could not open ThirdPartyNotices.txt.";

#ifdef OS_MAC
        pGraphics->ShowMessageBox(title, message, kMB_OK);
#else
        pGraphics->ShowMessageBox(message, title, kMB_OK);
#endif
      }

      static constexpr const char* kNoticesFileName = "ThirdPartyNotices.txt";
    };

    IVStyle mStyle;
    IText mText;
  };
};
