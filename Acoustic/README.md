# Puke Amp: Acoustic

A mono piezo input feeds two parallel impulse responses. Slot A opens with Universal. Each slot has its own capture menu, level (−60 to +12 dB) and pan. Selecting None clears a slot. Two empty slots or IR Bypass pass the input through. Pan applies on stereo outputs. The DAW session stores both library-relative capture names and all seven mix parameters. Dry Mix blends the direct piezo signal into both outputs, from 0% (IR mix) to 100% (direct). Earlier sessions recall with Dry Mix at 0%.

## IR library and presets

Each IR menu can load a mono WAV directly into that slot or import a WAV or folder of WAVs into My IR Library. Folder import scans subfolders and copies valid captures into the shared acoustic user library. Both slots can browse these copies. Direct loads reference the source file, so keep that WAV at its original path for recall.

Use the Presets menu to open or save an Acoustic `.fxp` file. Files in the default save folder show up in the menu. A preset stores both IR selections and the full mix, including Dry Mix. Imported IRs resolve relative to the acoustic user library.

The **Factory Presets** submenu reads from the installed Acoustic bundle. Add portable FXPs under [Models/Instruments/Presets](../Models/Instruments/Presets), using bundled IRs in each populated slot. New files and subfolders are picked up by the next build. Check references from the repository root with `python3 Acoustic/validate_factory_presets.py`. Native packaging also runs this check against each staged Acoustic bundle.

## Build Acoustic

Use the pinned submodules, iPlug2 patches and VST3 SDK described in the [repository build guide](../README.md#building). Run the following from the repository root with CMake and a native compiler. On Windows use a Visual Studio x64 developer prompt and add `-A x64` to the configure command.

```sh
cmake -S Acoustic -B build-acoustic -DCMAKE_BUILD_TYPE=Release
cmake --build build-acoustic --config Release --parallel 2
```

The bundles are in `build-acoustic/products`. macOS builds PukeAcoustic.vst3 and PukeAcoustic.component (AUv2). Windows builds PukeAcoustic.vst3. Copy the complete bundle to the matching DAW plugin folder. For local testing on macOS, sign the bundles before copying them.

```sh
codesign --force --sign - build-acoustic/products/PukeAcoustic.vst3
codesign --force --sign - build-acoustic/products/PukeAcoustic.component
```

The build copies WAVs recursively from `Models/Instruments`, including its `AcousticGuitar` subdirectory. Preserve relative filenames for recall. Name captures `Guitar_Microphone_PlayingStyle_Position.wav`, with `Left`, `Right` or `Center` for the microphone position during capture. For example, selecting `BR_C414_Strumming_Left.wav` shows guitar BR, mic C414, strumming style and left mic position. Each selected slot has its own capture details and uses matching artwork from `Models/GUI/mics` (here, `c414.png`). Three-field capture names also work and omit the position. Both editor pages use `Models/GUI/instruments/peararchtop.png`. Settings shows the active captures, signal routing, build information, credits and notices.

On Linux the same commands build the headless mixer check. Run `ctest --test-dir build-acoustic --output-on-failure` with CMake 3.20 or newer, or run `ctest --output-on-failure` inside the build directory.

## Host checks

Insert the effect on a mono guitar track and confirm Universal loads on first use. Route to stereo, choose a second capture, and pan the slots to opposite sides. Listen while changing captures, clearing either slot, inverting B and toggling bypass. Save and reopen the session with two captures and again with both slots empty. Repeat at 48 and 96 kHz. On macOS, the installed Audio Unit can also be checked using `auval -v aufx PkAc SDAa`.
