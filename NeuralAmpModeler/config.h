#define PLUG_NAME "Puke Amp"
#define PLUG_MFR "Puke"
#define PLUG_VERSION_HEX 0x0000020D
#define PLUG_VERSION_STR "0.2.13"
#define STATE_VERSION_STR "0.7.17"
#define PLUG_UNIQUE_ID 'PkAm'
#define PLUG_MFR_ID 'SDAa'
#define PLUG_URL_STR "https://github.com/ElectricGuitarInnovationLab/Puke-Amp"
#define PLUG_EMAIL_STR "spam@me.com"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Puke Amp and contributors"
#define PLUG_CLASS_NAME NeuralAmpModeler
#define BUNDLE_NAME "PukeAmp"
#define BUNDLE_MFR "theRATLAB"
#define BUNDLE_DOMAIN "org"

#define SHARED_RESOURCES_SUBPATH "NeuralAmpModeler"

#ifdef APP_API
  #define PLUG_CHANNEL_IO "1-2"
#else
  #define PLUG_CHANNEL_IO "1-1 1-2 2-2"
#endif

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 600
#define PLUG_HEIGHT 400
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
// The macOS VST3 editor fits its fixed artwork into the host's content area.
#if defined(__APPLE__) && defined(VST3_API)
#define PLUG_HOST_RESIZE 1
#else
#define PLUG_HOST_RESIZE 0
#endif
#define PLUG_MIN_WIDTH (PLUG_WIDTH / 2)
#define PLUG_MIN_HEIGHT (PLUG_HEIGHT / 2)
#define PLUG_MAX_WIDTH (PLUG_WIDTH * 4)
#define PLUG_MAX_HEIGHT (PLUG_HEIGHT * 4)

#define AUV2_ENTRY NeuralAmpModeler_Entry
#define AUV2_ENTRY_STR "NeuralAmpModeler_Entry"
#define AUV2_FACTORY NeuralAmpModeler_Factory
#define AUV2_VIEW_CLASS NeuralAmpModeler_View
#define AUV2_VIEW_CLASS_STR "NeuralAmpModeler_View"

#define AAX_TYPE_IDS 'ITP1'
#define AAX_TYPE_IDS_AUDIOSUITE 'ITA1'
#define AAX_PLUG_MFR_STR "Acme"
#define AAX_PLUG_NAME_STR "Puke Amp\nPkAm"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
#define MY_LOGO_FN "puke.svg"
#define RAT_LOGO_FN "rat.svg"
#define ROBOTO_FN "Roboto-Regular.ttf"
#define MICHROMA_FN "Michroma-Regular.ttf"

#define GEAR_FN "Gear.svg"
#define FILE_FN "File.svg"
#define CLOSE_BUTTON_FN "Cross.svg"
#define SAVE_ICON_FN "save.svg"
#define OPEN_ICON_FN "open.svg"
#define LEFT_ARROW_FN "ArrowLeft.svg"
#define RIGHT_ARROW_FN "ArrowRight.svg"
#define AMP_ICON_FN "amp.svg"
#define SPEAKER_ICON_FN "speaker.svg"
#define PEDAL_ICON_FN "pedal.svg"
#define ADD_PEDAL_ICON_FN "add.svg"
#define MOVE_LEFT_ICON_FN "left.svg"
#define MOVE_RIGHT_ICON_FN "right.svg"
#define REMOVE_PEDAL_ICON_FN "remove.svg"
#define EFFECT_OFF_ICON_FN "effect_off.svg"
#define EFFECT_ON_ICON_FN "effect_on.svg"
#define GLOBE_ICON_FN "Globe.svg"
#define SLIMMABLE_ICON_FN "SlimmableIcon.svg"

#define BACKGROUND_FN "Background.jpg"
#define BACKGROUND2X_FN "Background@2x.jpg"
#define BACKGROUND3X_FN "Background@3x.jpg"
#define KNOBBACKGROUND_FN "KnobBackground.png"
#define KNOBBACKGROUND2X_FN "KnobBackground@2x.png"
#define KNOBBACKGROUND3X_FN "KnobBackground@3x.png"
#define FILEBACKGROUND_FN "FileBackground.png"
#define FILEBACKGROUND2X_FN "FileBackground@2x.png"
#define FILEBACKGROUND3X_FN "FileBackground@3x.png"
#define INPUTLEVELBACKGROUND_FN "InputLevelBackground.png"
#define INPUTLEVELBACKGROUND2X_FN "InputLevelBackground@2x.png"
#define INPUTLEVELBACKGROUND3X_FN "InputLevelBackground@3x.png"
#define LINES_FN "Lines.png"
#define LINES2X_FN "Lines@2x.png"
#define LINES3X_FN "Lines@3x.png"
#define SLIDESWITCHHANDLE_FN "SlideSwitchHandle.png"
#define SLIDESWITCHHANDLE2X_FN "SlideSwitchHandle@2x.png"
#define SLIDESWITCHHANDLE3X_FN "SlideSwitchHandle@3x.png"

#define METERBACKGROUND_FN "MeterBackground.png"
#define METERBACKGROUND2X_FN "MeterBackground@2x.png"
#define METERBACKGROUND3X_FN "MeterBackground@3x.png"

// Issue 291
// On the macOS standalone, we might not have permissions to traverse the file directory, so we have the app ask the
// user to pick a directory instead of the file in the directory.
// Everyone else is fine though.
#if defined(APP_API) && defined(__APPLE__)
  #define NAM_PICK_DIRECTORY
#endif
