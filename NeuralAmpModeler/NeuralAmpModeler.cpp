#include <algorithm> // std::clamp, std::min
#include <cmath> // pow
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "CabinetPageControl.h"
#include "GUITrace.h"
#include "EditorSize.h"
#include "IPlugPaths.h"
#include "UserLibrary.h"
#include "UpdateChecker.h"

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Every page uses the cabinet selector's typography and neutral surfaces.
const IVStyle style = nam_ui::ControlStyle();
const IVStyle radioButtonStyle = style
  .WithColor(kON, PluginColors::NAM_THEMECOLOR)
  .WithColor(kOFF, PluginColors::SURFACE)
  .WithColor(kX1, PluginColors::NAM_THEMECOLOR);

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  for (auto& slot : mFXSlots)
    slot = std::make_unique<FXModelSlot>();
  nam::activations::Activation::enable_fast_tanh();
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 1.0, 0.0, 1.0, 0.01);
  GetParam(kFXChainEnabled)->InitBool("FX Chain Enabled", true);
  for (int slot = 0; slot < kMaxFXSlots; ++slot)
  {
    const std::string prefix = "FX " + std::to_string(slot + 1) + " ";
    GetParam(FXParamIndex(slot, kFXEnabledOffset))->InitBool((prefix + "Enabled").c_str(), true);
    GetParam(FXParamIndex(slot, kFXInputOffset))->InitGain((prefix + "Input").c_str(), 0.0, -20.0, 20.0, 0.1);
    GetParam(FXParamIndex(slot, kFXOutputOffset))->InitGain((prefix + "Output").c_str(), 0.0, -40.0, 40.0, 0.1);
    GetParam(FXParamIndex(slot, kFXEQActiveOffset))->InitBool((prefix + "EQ Enabled").c_str(), true);
    GetParam(FXParamIndex(slot, kFXBassOffset))->InitDouble((prefix + "Bass").c_str(), 5.0, 0.0, 10.0, 0.1);
    GetParam(FXParamIndex(slot, kFXMidOffset))->InitDouble((prefix + "Middle").c_str(), 5.0, 0.0, 10.0, 0.1);
    GetParam(FXParamIndex(slot, kFXTrebleOffset))->InitDouble((prefix + "Treble").c_str(), 5.0, 0.0, 10.0, 0.1);
  }

  GetParam(kMicALevel)->InitGain("Mic A Level", 0., -60., 12., 0.1);
  GetParam(kMicAPan)->InitDouble("Mic A Pan", 0., -100., 100., 1., "%");
  GetParam(kMicBLevel)->InitGain("Mic B Level", 0., -60., 12., 0.1);
  GetParam(kMicBPan)->InitDouble("Mic B Pan", 0., -100., 100., 1., "%");
  GetParam(kMicBInvert)->InitBool("Mic B Invert", false);

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    auto* graphics = MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
    graphics->SetScaleConstraints(static_cast<float>(PLUG_MIN_WIDTH) / PLUG_WIDTH,
                                  static_cast<float>(PLUG_MAX_WIDTH) / PLUG_WIDTH);
    return graphics;
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    // This is a fixed logical layout. Rescaling must never append another set
    // of controls (including the full-window modal pages and their children).
    if (pGraphics->NControls())
      return;
    pGraphics->SetLayoutOnResize(false);
#if !defined(OS_MAC) || !defined(VST3_API)
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
#endif
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->AttachPopupMenuControl(nam_ui::Text(13.f));
    auto* menu = pGraphics->GetPopupMenuControl();
    menu->SetPanelColor(PluginColors::PANEL);
    menu->SetCellBackgroundColor(PluginColors::SURFACE);
    menu->SetItemColor(PluginColors::TEXT);
    menu->SetItemMouseoverColor(PluginColors::TEXT);
    menu->SetDisabledItemColor(PluginColors::MUTED);
    menu->SetSeparatorColor(PluginColors::BORDER);

    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto savePresetSVG = pGraphics->LoadSVG(SAVE_ICON_FN);
    const auto openPresetSVG = pGraphics->LoadSVG(OPEN_ICON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto moveLeftSVG = pGraphics->LoadSVG(MOVE_LEFT_ICON_FN);
    const auto moveRightSVG = pGraphics->LoadSVG(MOVE_RIGHT_ICON_FN);
    const auto removePedalSVG = pGraphics->LoadSVG(REMOVE_PEDAL_ICON_FN);
    const auto effectOffSVG = pGraphics->LoadSVG(EFFECT_OFF_ICON_FN);
    const auto effectOnSVG = pGraphics->LoadSVG(EFFECT_ON_ICON_FN);
    const auto slimIconSVG = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);
    const auto logoSVG = pGraphics->LoadSVG(MY_LOGO_FN);
    const auto ratLogoSVG = pGraphics->LoadSVG(RAT_LOGO_FN);

    const auto b = pGraphics->GetBounds();
    const auto presetBrowserArea = IRECT(24.f, 70.f, 576.f, 104.f);
    const auto knobsArea = IRECT(48.f, 140.f, 552.f, 238.f);
    const auto inputKnobArea = knobsArea.GetGridCell(0, kInputLevel, 1, numKnobs);
    const auto noiseGateArea = knobsArea.GetGridCell(0, kNoiseGateThreshold, 1, numKnobs);
    const auto bassKnobArea = knobsArea.GetGridCell(0, kToneBass, 1, numKnobs);
    const auto midKnobArea = knobsArea.GetGridCell(0, kToneMid, 1, numKnobs);
    const auto trebleKnobArea = knobsArea.GetGridCell(0, kToneTreble, 1, numKnobs);
    const auto outputKnobArea = knobsArea.GetGridCell(0, kOutputLevel, 1, numKnobs);
    const auto ngToggleArea = IRECT(132.f, 243.f, 222.f, 266.f);
    const auto eqToggleArea = IRECT(300.f, 243.f, 390.f, 266.f);
    const auto modelArea = IRECT(96.f, 291.f, 526.f, 325.f);
    const auto slimIconArea = IRECT(538.f, 295.f, 576.f, 321.f);
    const auto irArea = IRECT(96.f, 342.f, 462.f, 376.f);
    const auto irToggleArea = IRECT(478.f, 345.f, 576.f, 373.f);
    const auto inputMeterArea = IRECT(33.f, 155.f, 41.f, 231.f);
    const auto outputMeterArea = IRECT(559.f, 155.f, 567.f, 231.f);
    const auto settingsButtonArea = IRECT(510.f, 20.f, 576.f, 46.f);
    const auto fxButtonArea = IRECT(428.f, 20.f, 502.f, 46.f);

    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Sets mNAMPath and mStagedNAM
        const std::string msg = _StageModel(fileName);
        // TODO error messages like the IR loader.
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load NAM model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
        }
        std::cout << "Loaded: " << fileName.Get() << std::endl;
      }
    };

    // IR loader button
    auto loadIRCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        auto* page = GetUI()->GetControlWithTag(kCtrlTagCabinetPage)->As<NAMCabinetPageControl>();
        const auto retCode = page->LoadPrimaryFile(fileName.Get()).status;
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load IR!", kMB_OK);
        }
      }
    };

    pGraphics->AttachPanelBackground(PluginColors::BACKGROUND);
    pGraphics->GetControl(0)->SetIgnoreMouse(true);
    pGraphics->AttachControl(new NAMPanelControl(IRECT(24.f, 116.f, 576.f, 274.f)));
    const auto addLabel = [pGraphics](const IRECT& bounds, const char* text, float size,
                                    const IColor& color = PluginColors::MUTED) {
      pGraphics->AttachControl(new IVLabelControl(bounds, text, nam_ui::LabelStyle(size, color, EAlign::Near)))
        ->SetIgnoreMouse(true);
    };
    addLabel(IRECT(62.f, 16.f, 270.f, 48.f), "PUKE AMP", 20.f, PluginColors::TEXT);
    addLabel(IRECT(24.f, 51.f, 200.f, 67.f), "PRESET", 10.f);
    addLabel(IRECT(36.f, 119.f, 220.f, 136.f), "AMPLIFIER", 10.f);
    addLabel(IRECT(24.f, 291.f, 90.f, 325.f), "AMP", 11.f);
    addLabel(IRECT(24.f, 342.f, 90.f, 376.f), "CABINET", 11.f);
    pGraphics->AttachControl(new ISVGButtonControl(
      IRECT(24.f, 20.f, 50.f, 46.f), [](IControl* pCaller) {
        pCaller->GetUI()->OpenURL("http://puke.studio");
      }, logoSVG, logoSVG))->SetTooltip("Visit puke.studio");
    pGraphics->AttachControl(
      new NAMPresetBrowserControl(presetBrowserArea, style, savePresetSVG, openPresetSVG),
      kCtrlTagPresetBrowser);
    pGraphics->AttachControl(new NAMTextButtonControl(
      fxButtonArea, "Pedals", [pGraphics](IControl*) {
        auto* page = pGraphics->GetControlWithTag(kCtrlTagFXPage)->As<NAMFXPageControl>();
        page->RefreshFromPlugin();
        page->HidePage(false);
      }), kCtrlTagFXButton)->SetTooltip("Open Drive Pedals FX chain");

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
#else
    const std::string defaultNamFileString = "Select model...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    pGraphics->AttachControl(
      new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam",
                                loadModelCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                globeSVG, "Get NAM Models", getUrl, "Amp"),
      kCtrlTagModelFileBrowser);

    auto hideSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(true);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(true);
      ui->SetAllControlsDirty();
    };
    auto showSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(false);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(false);
      ui->SetAllControlsDirty();
    };

    pGraphics
      ->AttachControl(
        new NAMSquareButtonControl(slimIconArea, DefaultClickActionFunc, slimIconSVG), kCtrlTagSlimmableIcon)
      ->SetAnimationEndActionFunction(showSlimOverlay)
      ->Hide(true);

    pGraphics->AttachControl(new NAMSwitchControl(irToggleArea, kIRToggle, "Enabled"))
      ->SetTooltip("Cabinet IR on/off");
    pGraphics->AttachControl(
      new NAMFileBrowserControl(irArea, kMsgTagClearIR, "Cabinet + mics...", "wav", loadIRCompletionHandler, style,
                                fileSVG, crossSVG, leftArrowSVG, rightArrowSVG, globeSVG,
                                "Get IRs", getUrl, "IR", true, [pGraphics](IControl*) {
                                  pGraphics->GetControlWithTag(kCtrlTagCabinetPage)->As<NAMCabinetPageControl>()->Open();
                                }),
      kCtrlTagIRFileBrowser);
    pGraphics->AttachControl(
      new NAMSwitchControl(ngToggleArea, kNoiseGateActive, "Gate"));
    pGraphics->AttachControl(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ"));

    // The knobs
    pGraphics->AttachControl(new NAMKnobControl(inputKnobArea, kInputLevel, "INPUT", style));
    pGraphics->AttachControl(new NAMKnobControl(noiseGateArea, kNoiseGateThreshold, "GATE", style));
    pGraphics->AttachControl(
      new NAMKnobControl(bassKnobArea, kToneBass, "BASS", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(midKnobArea, kToneMid, "MIDDLE", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(trebleKnobArea, kToneTreble, "TREBLE", style), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new NAMKnobControl(outputKnobArea, kOutputLevel, "OUTPUT", style));

    // The meters
    pGraphics->AttachControl(new NAMMeterControl<1>(inputMeterArea, style), kCtrlTagInputMeter);
    pGraphics->AttachControl(new NAMMeterControl<2>(outputMeterArea, style), kCtrlTagOutputMeter);

    pGraphics->AttachControl(new NAMTextButtonControl(
      settingsButtonArea, "Settings",
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      }));

    // Full-window pages must be above all main-page controls, including navigation.
    pGraphics->AttachControl(
      new NAMFXPageControl(b, crossSVG, fileSVG, leftArrowSVG, rightArrowSVG, globeSVG, moveLeftSVG,
                           moveRightSVG, removePedalSVG, effectOffSVG, effectOnSVG, style),
      kCtrlTagFXPage)->Hide(true);

    // Settings/help/about box
    pGraphics
      ->AttachControl(new NAMSettingsPageControl(b, ratLogoSVG, radioButtonStyle),
                      kCtrlTagSettingsBox)
      ->Hide(true);

    pGraphics->AttachControl(new NAMCabinetPageControl(b), kCtrlTagCabinetPage)->Hide(true);

    const auto slimKnobArea = b.GetCentredInside(120.f, 120.f);
    pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(b, hideSlimOverlay), kCtrlTagSlimOverlayBackdrop)
      ->Hide(true);
    pGraphics
      ->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "", style.WithShowLabel(false)), kCtrlTagSlimKnob)
      ->Hide(true);

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });

    // pGraphics->GetControlWithTag(kCtrlTagOutNorm)->SetMouseEventsWhenDisabled(false);
    // pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetMouseEventsWhenDisabled(false);
  };
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }

  // Pedal models run in series before the main amp. Two reusable mono buffers
  // avoid allocations and per-slot audio storage on the real-time thread.
  sample** ampInput = triggerOutput;
  sample* fxInputPointers[] = {mFXBufferA.data()};
  sample* fxOutputPointers[] = {mFXBufferB.data()};
  const double mixStep = sampleRate > 0.0 ? 1.0 / (0.01 * sampleRate) : 1.0;
  for (int slotIndex = 0; slotIndex < kMaxFXSlots; ++slotIndex)
  {
    auto& slot = *mFXSlots[slotIndex];
    if (!slot.model)
      continue;

    const bool targetActive = !slot.removing && GetParam(FXParamIndex(slotIndex, kFXEnabledOffset))->Bool();
    if (!targetActive && slot.wetMix <= 0.0)
    {
      if (slot.removing)
      {
        slot.model = nullptr;
        slot.removing = false;
        mFXPageNeedsRefresh = true;
        _UpdateLatency();
      }
      continue;
    }

    const double inputGain = DBToAmp(GetParam(FXParamIndex(slotIndex, kFXInputOffset))->Value());
    const double outputGain = DBToAmp(GetParam(FXParamIndex(slotIndex, kFXOutputOffset))->Value());
    for (size_t frame = 0; frame < numFrames; ++frame)
    {
      mFXDryBuffer[frame] = ampInput[0][frame];
      mFXBufferA[frame] = inputGain * mFXDryBuffer[frame];
    }

    slot.model->process(fxInputPointers, fxOutputPointers, nFrames);
    slot.toneStack->SetParam("bass", GetParam(FXParamIndex(slotIndex, kFXBassOffset))->Value());
    slot.toneStack->SetParam("middle", GetParam(FXParamIndex(slotIndex, kFXMidOffset))->Value());
    slot.toneStack->SetParam("treble", GetParam(FXParamIndex(slotIndex, kFXTrebleOffset))->Value());
    sample** pedalOutput = slot.toneStack->Process(fxOutputPointers, numChannelsInternal, nFrames);
    for (size_t frame = 0; frame < numFrames; ++frame)
    {
      slot.wetMix = std::clamp(slot.wetMix + (targetActive ? mixStep : -mixStep), 0.0, 1.0);
      const double wet = outputGain * pedalOutput[0][frame];
      mFXBufferA[frame] = ((1.0 - slot.wetMix) * mFXDryBuffer[frame]) + (slot.wetMix * wet);
    }
    ampInput = fxInputPointers;
  }

  if (mModel != nullptr)
  {
    mModel->process(ampInput, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(ampInput, mOutputPointers, numChannelsInternal, numFrames);
  }
  // Apply the noise gate after the NAM
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;

  if (numChannelsExternalOut > 0 && nFrames > 0)
  {
    const std::array<nam_cabinet::Mixer::Mix, 2> mix{{
      {DBToAmp(GetParam(kMicALevel)->Value()), GetParam(kMicAPan)->Value() / 100.},
      {DBToAmp(GetParam(kMicBLevel)->Value()) * (GetParam(kMicBInvert)->Bool() ? -1. : 1.),
       GetParam(kMicBPan)->Value() / 100.}
    }};
    mCabinet.Process(toneStackOutPointers[0], outputs, static_cast<int>(numChannelsExternalOut), nFrames,
                     mix, GetParam(kIRToggle)->Bool());
    auto** filtered = mHighPass.Process(outputs, numChannelsExternalOut, numFrames);
    _ProcessOutput(filtered, outputs, numFrames, numChannelsExternalOut);
    _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  }
  std::feupdateenv(&fe_state);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mHighPass.SetParams(recursive_linear_filter::HighPassParams(sampleRate, kDCBlockerFrequency));
  mHighPass.Prepare(std::clamp(NOutChansConnected(), 1, 2), std::max(1, maxBlockSize));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  if (auto* graphics = GetUI())
  {
    for (const auto tag : {kCtrlTagModelFileBrowser, kCtrlTagIRFileBrowser, kCtrlTagFXFileBrowser})
      if (auto* browser = dynamic_cast<NAMFileBrowserControl*>(graphics->GetControlWithTag(tag)))
        browser->PollImport();
  }
  mCabinet.Collect();
  if (GetUI() && mCabinetChanged.exchange(false)) _RefreshCabinet();
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

  if (mUpdateCheckState && mUpdateCheckState->resultReady && GetUI() &&
      !mUpdateCheckState->notificationClaimed.exchange(true))
    _PresentAvailableUpdate();

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mFXPageNeedsRefresh)
  {
    _RefreshFXPage();
    mFXPageNeedsRefresh = false;
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        p->Hide(true);
      if (auto* p = pGraphics->GetControlWithTag(kCtrlTagSlimKnob))
        p->Hide(true);
      pGraphics->SetAllControlsDirty();
      mModelCleared = false;
    }
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // State format version, so product versioning can evolve independently.
  WDL_String version(STATE_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  const auto namPath = _EncodePresetAssetPath(mNAMPath);
  const auto paths = CabinetPaths();
  chunk.PutStr(namPath.Get());
  for (const auto& path : paths)
    chunk.PutStr(_EncodePresetAssetPath(WDL_String(path.c_str())).Get());
  for (const auto& slot : mFXSlots)
  {
    const auto fxPath = _EncodePresetAssetPath(slot->path);
    chunk.PutStr(fxPath.Get());
  }
  return SerializeParams(chunk);
}

bool NeuralAmpModeler::SavePresetFile(const char* filePath, const char* bundledModelsRoot)
{
  mLastPresetError.Set("");
  mPresetModelsRoot.Set(bundledModelsRoot ? bundledModelsRoot : "");
  mSerializePortablePresetPaths = true;
  const bool saved = SavePresetAsFXP(filePath);
  mSerializePortablePresetPaths = false;
  mPresetModelsRoot.Set("");
  if (!saved)
    mLastPresetError.Set("The preset file could not be saved.");
  else
    mCurrentDiskPresetPath.Set(filePath);
  return saved;
}

bool NeuralAmpModeler::LoadPresetFile(const char* filePath, const char* bundledModelsRoot)
{
  mLastPresetError.Set("");
  mPresetModelsRoot.Set(bundledModelsRoot ? bundledModelsRoot : "");
  mLoadingDiskPreset = true;
  const bool loaded = LoadPresetFromFXP(filePath);
  mLoadingDiskPreset = false;
  mPresetModelsRoot.Set("");

  if (!loaded)
    mLastPresetError.Set("The selected file is not a valid Puke Amp preset.");
  const bool success = loaded && !CStringHasContents(mLastPresetError.Get());
  if (success)
    mCurrentDiskPresetPath.Set(filePath);
  return success;
}

WDL_String NeuralAmpModeler::_EncodePresetAssetPath(const WDL_String& path) const
{
  if (CStringHasContents(path.Get()))
  {
    try
    {
      const auto encoded = nam_library::UserLibrary().Encode(std::filesystem::u8path(path.Get()));
      if (encoded.rfind("library://", 0) == 0)
        return WDL_String(encoded.c_str());
    }
    catch (const std::filesystem::filesystem_error&)
    {
    }
  }
  if (!mSerializePortablePresetPaths || !CStringHasContents(path.Get()) || !CStringHasContents(mPresetModelsRoot.Get()))
    return path;

  try
  {
    const auto root = std::filesystem::weakly_canonical(std::filesystem::u8path(mPresetModelsRoot.Get()));
    const auto asset = std::filesystem::weakly_canonical(std::filesystem::u8path(path.Get()));
    const auto relative = asset.lexically_relative(root);
    if (!relative.empty() && *relative.begin() != "..")
      return WDL_String((std::string("bundle://") + relative.generic_string()).c_str());
  }
  catch (const std::filesystem::filesystem_error&)
  {
  }
  return path;
}

WDL_String NeuralAmpModeler::_ResolvePresetAssetPath(const std::string& storedPath) const
{
  if (storedPath.empty())
    return WDL_String("");

  if (storedPath.rfind("library://", 0) == 0)
  {
    try
    {
      return WDL_String(nam_library::PathString(nam_library::UserLibrary().Resolve(storedPath)).c_str());
    }
    catch (const std::exception&)
    {
      // Leave invalid references unresolved; the normal loader reports the failure.
      return WDL_String(storedPath.c_str());
    }
  }

  const std::string portablePrefix = "bundle://";
  try
  {
    if (storedPath.rfind(portablePrefix, 0) == 0 && CStringHasContents(mPresetModelsRoot.Get()))
    {
      const auto resolved = std::filesystem::u8path(mPresetModelsRoot.Get()) /
                            std::filesystem::u8path(storedPath.substr(portablePrefix.size()));
      return WDL_String(resolved.lexically_normal().string().c_str());
    }

    const auto original = std::filesystem::u8path(storedPath);
    if (std::filesystem::exists(original) || !CStringHasContents(mPresetModelsRoot.Get()))
      return WDL_String(storedPath.c_str());

    // Backward compatibility for factory presets created before portable paths:
    // preserve the portion beneath Models and resolve it in this installation.
    std::string normalized = storedPath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::string marker = "/Models/";
    const auto markerPos = normalized.rfind(marker);
    if (markerPos != std::string::npos)
    {
      const auto relative = normalized.substr(markerPos + marker.size());
      const auto resolved = std::filesystem::u8path(mPresetModelsRoot.Get()) / std::filesystem::u8path(relative);
      if (std::filesystem::exists(resolved))
        return WDL_String(resolved.lexically_normal().string().c_str());

        // Some factory presets refer to the original inch-suffixed IR names.
        // Keep those presets working after the filenames were normalized.
        const std::string legacySuffix = "in_from_center.wav";
        if (relative.size() > legacySuffix.size() &&
          relative.compare(relative.size() - legacySuffix.size(), legacySuffix.size(), legacySuffix) == 0)
        {
          const auto renamedRelative = relative.substr(0, relative.size() - legacySuffix.size()) +
                                       "_from_center.wav";
          const auto renamed = std::filesystem::u8path(mPresetModelsRoot.Get()) /
                               std::filesystem::u8path(renamedRelative);
          if (std::filesystem::exists(renamed))
            return WDL_String(renamed.lexically_normal().string().c_str());
        }
    }
  }
  catch (const std::filesystem::filesystem_error&)
  {
  }
  return WDL_String(storedPath.c_str());
}

void NeuralAmpModeler::_RecordPresetLoadError(const std::string& error)
{
  if (!mLoadingDiskPreset || error.empty())
    return;
  if (mLastPresetError.GetLength())
    mLastPresetError.Append("\n");
  mLastPresetError.Append(error.c_str());
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

bool NeuralAmpModeler::ConstrainEditorResize(int& width, int& height) const
{
  return nam_ui::ConstrainEditorSize(width, height, GetEditorWidth(), GetEditorHeight());
}

#if defined(OS_MAC) && defined(VST3_API)
void NeuralAmpModeler::OnParentWindowResize(int width, int height)
{
  PukeAmpGUITrace("plugin onSize requested=%dx%d ui=%p editor-before=%dx%d", width, height,
                  static_cast<void*>(GetUI()), GetEditorWidth(), GetEditorHeight());
  if (width <= 0 || height <= 0)
    return;

  // VST3/NSView sizes are Cocoa points, not backing pixels. The host's
  // checkSizeConstraint callback uses the same aspect-ratio constraint.
  ConstrainEditorResize(width, height);
  SetEditorSize(width, height);
  if (auto* graphics = GetUI())
  {
    // Round upward by one float ULP to avoid truncating an exact window size
    // one pixel short when IGraphics converts the scaled dimensions to ints.
    const float scale = std::nextafter(static_cast<float>(width) / PLUG_WIDTH,
                                      static_cast<float>(PLUG_MAX_WIDTH));
    // Mouse coordinates are divided by this same draw scale in IGraphicsMac.
    // Do not notify resizeView from inside the host's onSize callback.
    graphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, scale, false);
    PukeAmpGUITrace("plugin onSize applied editor=%dx%d logical=%dx%d draw-scale=%.4f window=%dx%d controls=%d",
                    GetEditorWidth(), GetEditorHeight(), graphics->Width(), graphics->Height(),
                    graphics->GetDrawScale(), graphics->WindowWidth(), graphics->WindowHeight(),
                    graphics->NControls());
  }
}

bool NeuralAmpModeler::EditorResizeFromUI(int width, int height, bool needsPlatformResize)
{
  // This editor is host-sized. In particular, SetScreenScale/OpenWindow must
  // not request a new host size: that can re-enter onSize during a Retina
  // transition. Host resize negotiation happens in ConstrainEditorResize.
  PukeAmpGUITrace("plugin ui-resize-request=%dx%d platform=%d editor-kept=%dx%d", width, height,
                  needsPlatformResize, GetEditorWidth(), GetEditorHeight());
  return true;
}
#endif

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();
#if defined(OS_MAC) && defined(VST3_API)
  WDL_String resourcePath;
  BundleResourcePath(resourcePath, GetBundleID());
  PukeAmpGUITrace("plugin ui-open version=%s ui=%p editor=%dx%d", PLUG_VERSION_STR,
                  static_cast<void*>(GetUI()), GetEditorWidth(), GetEditorHeight());
  PukeAmpGUITrace("plugin bundle-id=%s resources=%s", GetBundleID(), resourcePath.Get());
  // Also handles onSize before attachment and close/reopen with a retained
  // host size; IGEditorDelegate may have restored an older child draw scale.
  OnParentWindowResize(GetEditorWidth(), GetEditorHeight());
#endif
  _StartUpdateCheck();

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  _RefreshCabinet();

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
  _RefreshFXPage();
}

void NeuralAmpModeler::_StartUpdateCheck()
{
  if (mUpdateCheckStarted.exchange(true))
    return;

  static auto sharedState = std::make_shared<UpdateCheckState>();
  static std::once_flag startOnce;
  mUpdateCheckState = sharedState;

  std::call_once(startOnce, [state = mUpdateCheckState]() {
    std::thread([state]() {
      update_checker::Release release;
      if (!update_checker::FetchLatestRelease(release) ||
          !update_checker::IsNewerVersion(release.version, PLUG_VERSION_STR))
        return;

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->version = std::move(release.version);
        state->url = std::move(release.url);
      }
      state->resultReady = true;
    }).detach();
  });
}

void NeuralAmpModeler::_PresentAvailableUpdate()
{
  auto* graphics = GetUI();
  if (!graphics)
    return;

  std::string version;
  std::string url;
  {
    std::lock_guard<std::mutex> lock(mUpdateCheckState->mutex);
    version = mUpdateCheckState->version;
    url = mUpdateCheckState->url;
  }

  const std::string message = "Puke Amp " + version + " is available.\n\nOpen the GitHub release page?";
  auto completion = [graphics, url](EMsgBoxResult result) {
    if (result == kYES)
      graphics->OpenURL(url.c_str());
  };
#ifdef OS_MAC
  graphics->ShowMessageBox("Puke Amp Update", message.c_str(), kMB_YESNO, completion);
#else
  graphics->ShowMessageBox(message.c_str(), "Puke Amp Update", kMB_YESNO, completion);
#endif
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kSlim: _ApplySlimParamToLoadedNAMs(); break;
    case kFXChainEnabled: _UpdateLatency(); break;
    default:
      if (paramIdx >= kFXParamBase && paramIdx < kMicALevel && ((paramIdx - kFXParamBase) % kNumFXParamsPerSlot) == kFXEnabledOffset)
        _UpdateLatency();
      break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle: pGraphics->GetControlWithTag(kCtrlTagIRFileBrowser)->SetDisabled(!active); break;
      default: break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel: mShouldRemoveModel = true; return true;
    case kMsgTagClearIR: SelectCabinet({std::string{}, std::string{}}); return true;
    case kMsgTagClearFXModel: ClearFXModel(mFXEditorSlot.load()); return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

std::string NeuralAmpModeler::StageFXModel(int slot, const WDL_String& modelPath)
{
  return _StageFXModel(slot, modelPath);
}

void NeuralAmpModeler::ClearFXModel(int slot)
{
  if (slot < 0 || slot >= kMaxFXSlots)
    return;
  mFXSlots[slot]->shouldRemove = true;
  mFXSlots[slot]->path.Set("");
  mFXPageNeedsRefresh = true;
}

void NeuralAmpModeler::MoveFXModel(int from, int to)
{
  if (from < 0 || from >= kMaxFXSlots || to < 0 || to >= kMaxFXSlots || from == to)
    return;
  if (mPendingFXMove.load() != -1)
    return;

  for (int offset = 0; offset < kNumFXParamsPerSlot; ++offset)
  {
    const int fromParam = kFXParamBase + from * kNumFXParamsPerSlot + offset;
    const int toParam = kFXParamBase + to * kNumFXParamsPerSlot + offset;
    const double fromValue = GetParam(fromParam)->GetNormalized();
    const double toValue = GetParam(toParam)->GetNormalized();
    GetParam(fromParam)->SetNormalized(toValue);
    GetParam(toParam)->SetNormalized(fromValue);
    SendParameterValueFromDelegate(fromParam, toValue, true);
    SendParameterValueFromDelegate(toParam, fromValue, true);
  }
  mPendingFXMove = from * kMaxFXSlots + to;
}

const WDL_String& NeuralAmpModeler::GetFXModelPath(int slot) const
{
  static WDL_String empty;
  if (slot < 0 || slot >= kMaxFXSlots)
    return empty;
  return mFXSlots[slot]->path;
}

void NeuralAmpModeler::SetFXEditorSlot(int slot)
{
  mFXEditorSlot = std::clamp(slot, 0, kMaxFXSlots - 1);
}

// Private methods ============================================================

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  const int pendingMove = mPendingFXMove.exchange(-1);
  if (pendingMove >= 0)
  {
    const int from = pendingMove / kMaxFXSlots;
    const int to = pendingMove % kMaxFXSlots;
    std::swap(mFXSlots[from], mFXSlots[to]);
    mFXPageNeedsRefresh = true;
    _UpdateLatency();
  }

  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }

  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }

  for (auto& slot : mFXSlots)
  {
    if (slot->shouldRemove.exchange(false))
    {
      slot->stagedModel = nullptr;
      slot->path.Set("");
      slot->removing = true;
      mFXPageNeedsRefresh = true;
      _UpdateLatency();
    }
    if (slot->stagedModel)
    {
      slot->model = std::move(slot->stagedModel);
      slot->removing = false;
      slot->wetMix = 0.0;
      mFXPageNeedsRefresh = true;
      _UpdateLatency();
    }
  }
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      outputs[c][s] = inputs[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  for (auto& slot : mFXSlots)
  {
    if (slot->stagedModel)
      slot->stagedModel->Reset(sampleRate, maxBlockSize);
    else if (slot->model)
      slot->model->Reset(sampleRate, maxBlockSize);
    slot->toneStack->Reset(sampleRate, maxBlockSize);
  }

  mCabinet.Reset(sampleRate, maxBlockSize);
  mCabinetChanged = true;
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);

    // Check that the model has 1 input and 1 output channel
    if (model->NumInputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 input channel, but has " + std::to_string(model->NumInputChannels()));
    }
    if (model->NumOutputChannels() != 1)
    {
      throw std::runtime_error("Model must have 1 output channel, but has "
                               + std::to_string(model->NumOutputChannels()));
    }

    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = temp->GetSlimmableModel())
    {
      slimmable->SetSlimmableSize(GetParam(kSlim)->Value());
    }
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

std::string NeuralAmpModeler::_StageFXModel(int slotIndex, const WDL_String& modelPath)
{
  if (slotIndex < 0 || slotIndex >= kMaxFXSlots)
    return "Invalid pedal slot.";

  auto& slot = *mFXSlots[slotIndex];
  WDL_String previousPath = slot.path;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
      throw std::runtime_error("Pedal model must have one input and one output channel.");

    auto staged = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    staged->Reset(GetSampleRate(), GetBlockSize());
    if (nam::SlimmableModel* slimmable = staged->GetSlimmableModel())
      slimmable->SetSlimmableSize(1.0);

    slot.stagedModel = std::move(staged);
    slot.shouldRemove = false;
    slot.path = modelPath;
    mFXPageNeedsRefresh = true;
  }
  catch (const std::exception& e)
  {
    slot.stagedModel = nullptr;
    slot.path = previousPath;
    mFXPageNeedsRefresh = true;
    return e.what();
  }
  return "";
}

nam_cabinet::Mixer::Result NeuralAmpModeler::SelectCabinet(const nam_cabinet::Mixer::Changes& changes)
{
  auto result = mCabinet.Select(changes);
  if (result.status == nam_cabinet::Mixer::Status::SUCCESS) mCabinetChanged = true;
  return result;
}

void NeuralAmpModeler::_RefreshCabinet()
{
  if (!GetUI()) return;
  const auto paths = CabinetPaths();
  const auto& displayed = paths[0].empty() ? paths[1] : paths[0];
  auto* browser = GetUI()->GetControlWithTag(kCtrlTagIRFileBrowser)->As<NAMFileBrowserControl>();
  if (displayed.empty()) browser->ClearDisplayedFile();
  else if (browser->CurrentFile() != displayed)
    browser->OnMsgFromDelegate(kMsgTagLoadedIR, static_cast<int>(displayed.size()), displayed.c_str());
  auto* page = GetUI()->GetControlWithTag(kCtrlTagCabinetPage)->As<NAMCabinetPageControl>();
  page->Sync(paths);
  page->UpdateMixControls();
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool updateFrames = updateChannels || (_GetBufferNumFrames() != numFrames);
  //  if (!updateChannels && !updateFrames)  // Could we do this?
  //    return;

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (updateFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
    {
      mInputArray[c].resize(numFrames);
      std::fill(mInputArray[c].begin(), mInputArray[c].end(), 0.0);
    }
    for (auto c = 0; c < mOutputArray.size(); c++)
    {
      mOutputArray[c].resize(numFrames);
      std::fill(mOutputArray[c].begin(), mOutputArray[c].end(), 0.0);
    }
    mFXBufferA.resize(numFrames);
    mFXBufferB.resize(numFrames);
    mFXDryBuffer.resize(numFrames);
  }
  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
  {
    std::stringstream ss;
    ss << "Expected mono output, but " << nChansOut << " output channels are requested!";
    throw std::runtime_error(ss.str());
  }

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansOut)
{
  const double gain = mOutputGain;
  for (auto cout = 0; cout < nChansOut; cout++)
    for (auto s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * inputs[cout][s], -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * inputs[cout][s];
#endif
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
    {
      const bool show = mModel->GetSlimmableModel() != nullptr;
      pSlimIcon->Hide(!show);
    }
  }
}

void NeuralAmpModeler::_RefreshFXPage()
{
  if (auto* ui = GetUI())
  {
    if (auto* page = ui->GetControlWithTag(kCtrlTagFXPage))
      page->As<NAMFXPageControl>()->RefreshFromPlugin();
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  for (int slot = 0; slot < kMaxFXSlots; ++slot)
    if (mFXSlots[slot]->model && GetParam(FXParamIndex(slot, kFXEnabledOffset))->Bool())
      latency += mFXSlots[slot]->model->GetLatency();
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, static_cast<int>(nChansIn));
  sample* stereoMeter[] = {outputPointer[0], outputPointer[nChansOut > 1 ? 1 : 0]};
  mOutputSender.ProcessBlock(stereoMeter, static_cast<int>(nFrames), kCtrlTagOutputMeter, 2);
}

// HACK
#include "Unserialization.cpp"
