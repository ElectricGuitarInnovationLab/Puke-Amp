# Puke Amp: Acoustic

A mono piezo input feeds two parallel impulse responses. Slot A opens with Universal. Each slot has its own capture menu, level (−60 to +12 dB) and pan. Selecting None clears a slot. Two empty slots or IR Bypass pass the input through. Pan applies on stereo outputs. The DAW session stores both library-relative capture names and all six mix parameters.

## Build the prototype

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

The build copies WAVs recursively from `Models/Instruments`, including its `AcousticGuitar` subdirectory. Preserve relative filenames for recall. Name captures `Instrument-Name_Microphone-type_PlayingStyle.wav` and place the matching `Microphone-type.png` in `Models/GUI/mics`. Each loaded slot displays its microphone on the right. For example, `BR_m149_Fingerstyle.wav` uses `m149.png`. Both editor pages use `Models/GUI/instruments/peararchtop.png`. Settings shows the active captures, signal routing, build information, credits and notices.

On Linux the same commands build the headless mixer check. Run `ctest --test-dir build-acoustic --output-on-failure` with CMake 3.20 or newer, or run `ctest --output-on-failure` inside the build directory.

## Host checks

Insert the effect on a mono guitar track and confirm Universal loads on first use. Route to stereo, choose a second capture, and pan the slots to opposite sides. Listen while changing captures, clearing either slot, inverting B and toggling bypass. Save and reopen the session with two captures and again with both slots empty. Repeat at 48 and 96 kHz. On macOS, the installed Audio Unit can also be checked using `auval -v aufx PkAc SDAa`.
