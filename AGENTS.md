# Puke Amp developer guide

## Scope and local guidance

This tracked guide applies across Puke Amp. Start here, then read the relevant
source and tests. Paths are relative to the repository root, and code symbols
are navigation anchors.

For personal workflow notes or machine-specific commands, create
`AGENTS.local.md` at the repository root. Git ignores it. Tell your agent to
read it as supplemental guidance if your tooling does not load it automatically.
Keep shared project guidance in this tracked file and keep credentials out of
both files.

Puke Amp is the iPlug2-based guitar amp modeling plugin and asset library from
Puke Studio. It uses Neural Amp Modeler for captured amps and pedals. The
`NeuralAmpModeler` directory and C++ class retain their historical names.
The native VST3 build pipeline covers macOS and Windows. Consult the relevant
project files when changing another format.

## File map and ownership

| Responsibility | Where to start |
| --- | --- |
| Host callbacks, parameters, DSP staging, and editor wiring | [NeuralAmpModeler.cpp](NeuralAmpModeler/NeuralAmpModeler.cpp), [NeuralAmpModeler.h](NeuralAmpModeler/NeuralAmpModeler.h), [config.h](NeuralAmpModeler/config.h) |
| Tone controls and NAM resampling | [ToneStack.cpp](NeuralAmpModeler/ToneStack.cpp), `ResamplingNAM` in the plugin header |
| State migration and preset recall | [Unserialization.cpp](NeuralAmpModeler/Unserialization.cpp), `SerializeState` and `UnserializeState` in the plugin implementation |
| Browser, pedal and settings controls, theme, and sizing | [NeuralAmpModelerControls.h](NeuralAmpModeler/NeuralAmpModelerControls.h), [UITheme.h](NeuralAmpModeler/UITheme.h), [EditorSize.h](NeuralAmpModeler/EditorSize.h) |
| Import, filesystem references, and model/WAV validation | [AssetLibrary.h](NeuralAmpModeler/AssetLibrary.h), [UserLibrary.h](NeuralAmpModeler/UserLibrary.h), [AssetValidation.h](NeuralAmpModeler/AssetValidation.h) |
| Acoustic-only VST3/AUv2 prototype, build and host checks | [Acoustic](Acoustic/README.md), [processor and editor](Acoustic/PukeAcoustic.cpp) |
| Cabinet capture metadata, page gestures, and DSP | [CabinetCatalog.h](NeuralAmpModeler/CabinetCatalog.h), [CabinetPageControl.h](NeuralAmpModeler/CabinetPageControl.h), [CabinetMixer.h](NeuralAmpModeler/CabinetMixer.h) |
| Bundled captures, cabinet artwork and presets | [Models](Models), [cabinet-library.json](Models/cabinet-library.json), [cabinet library guide](Models/CABINET_LIBRARY.md) |
| Update notifications | [UpdateChecker.h](NeuralAmpModeler/UpdateChecker.h), `OnIdle` and `_PresentAvailableUpdate` in the plugin implementation |
| Native build, packaging, and installers | [projects](NeuralAmpModeler/projects), [scripts](NeuralAmpModeler/scripts), [installer](NeuralAmpModeler/installer), [GitHub workflows](.github/workflows) |

## Audio and host boundaries

`ProcessBlock` scales and folds host input to mono and applies staged DSP
changes. The gate detects the input before the pedal chain. Up to eight NAM
pedal slots run in order before the amp model or fallback DSP. Each has its own
input/output gain, three-band tone stack and smoothed bypass. Gate gain and amp
EQ follow the amp. The cabinet mixer processes up to two IRs and feeds the
connected outputs, then the high-pass DC blocker, output processing and meters
finish the block.

- Keep file I/O, parsing, and preparation out of the audio callback. Follow the
  existing staging paths for model and cabinet changes. Check ownership and
  destruction as well as allocation when moving work across that boundary.
- Check `OnReset` when changing processing that depends on sample rate, block
  size, output channels, resampling, latency, or tail reporting. Exercise
  replacement, clear, bypass and reload while audio runs.
- Use existing meter and refresh handoffs. `OnIdle` polls import completion,
  collects retired cabinet state, updates controls, and presents update
  notifications. Guard editor-dependent work for hosts that close the editor
  while the processor continues running.
- Preserve slot order, automation behavior, amp calibration, and cabinet
  mono/stereo behavior when editing shared processing. Test unloaded, bypassed
  and loaded paths for the stage you change.

## State, files, and editor behavior

- Treat parameter identity and state layout as compatibility contracts for DAW
  sessions and FXP presets. Review parameter indices, defaults, write order,
  current reader and version-specific readers together. `PLUG_VERSION_STR`
  describes the product release and `STATE_VERSION_STR` describes the saved
  state format. Exercise old and new state, including an empty preset recalled
  over an active pedal or cabinet configuration.
- Factory presets use references under bundled `Models`. User-library references
  resolve against the shared library root. The preset loader also handles older
  bundled paths. Preserve portable references and recall behavior when working
  on menus, imports, or file resolution. Use the existing helpers for path
  joining and resolution.
- Imports copy supported files into the category library and use the same real
  loaders for validation. Preserve source files, collision handling, per-file
  reporting, and the active sound on failed imports. Keep external audition
  independent of library import. Check multiple plugin instances and standalone
  when changing library visibility or location.
- The cabinet page maps positions to recorded captures from the JSON catalog.
  Preserve snapping to available positions, cancellation of a drag, and audio
  changes on commit. Keep catalog, WAVs and artwork in sync. Check custom IRs,
  missing resources, A/B levels and pan, B polarity, and older single-IR presets.
- Build control changes on the shared theme and sizing code. Check all pages
  and host-driven resize, including standard and Retina displays on macOS.
  When changing a packaged image or font, check resource declarations,
  [Windows resource entries](NeuralAmpModeler/resources/main.rc), macOS project
  resources, and the installed build.

## Dependencies and native builds

Initialize the pinned dependencies and follow the
[native build instructions](README.md#building) for the SDK and prebuilt libraries.

```sh
git submodule update --init --recursive
bash NeuralAmpModeler/scripts/apply-iplug2-patches.sh
```

The patches in [NeuralAmpModeler/patches](NeuralAmpModeler/patches) implement
the editor resize hook and macOS Retina VST3 handling. The native scripts apply
them automatically. Apply them before direct IDE builds as well. An applied
patch can make the iPlug2 working tree appear modified. Check its diff before
editing or updating it.

Keep downstream iPlug2 changes reproducible from the pinned base. Review both
patches when updating iPlug2, retain the ones still required, and verify the
new revision is fetchable from the configured submodule remote. Use
[.gitmodules](.gitmodules) to find the NAM core, AudioDSPTools, and Eigen sources.
Avoid committing build products or local SDK and signing configuration.

## Validation by change type

Run the focused C++ suite with a C++20 toolchain from the repository root.
Initialize the submodules and apply the patches first.

```sh
cmake -S NeuralAmpModeler/tests -B build-amp-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-amp-tests --parallel 2
ctest --test-dir build-amp-tests --output-on-failure
```

| Change | Relevant checks |
| --- | --- |
| Asset import, relocation, or validation | `asset_library`, `asset_validation`, then import and recall in a host |
| Cabinet assets, gestures, or processing | `cabinet_catalog`, `cabinet_mixer`, then preview, recall, and installed resource checks |
| Editor dimensions or controls | `editor_size`, native build and host resize/interaction smoke checks |
| DSP, parameters, or state | Native build, audio and automation smoke tests, legacy FXP and session recall |
| Installer asset collection or packaging | Installer asset Python tests, native package build, and installed artifact checks |
| Documentation | Check paths, headings, commands, and the diff |

See the [test guide](NeuralAmpModeler/tests/README.md) for manual scenarios.
The C++ tests exercise selected components and do not launch a DAW. Report
the format, platform, and host you actually checked, along with any unavailable
validation.

## Release and installer integration

- [Build Puke Amp VST3](.github/workflows/build-native.yml) builds routine
  macOS and Windows artifacts. [Release Puke Amp](.github/workflows/release-native.yml)
  validates a version tag against `config.h`, packages installers, generates
  checksums and creates a draft release for review. Follow the
  [release instructions](README.md#publishing-a-release) when releasing.
- The updater looks at this repository's latest published GitHub release.
  When changing releases, naming, URLs or packaging, verify asset links and
  version comparison. Keep signing and notarization credentials in their
  designated secret stores. [Installer and signing guide](NeuralAmpModeler/installer/README.md)
- The separate Puke installer consumes signed bundles and manifests exported
  by this repository. Preserve file names and manifest contracts across
  coordinated changes. [Installer asset integration](NeuralAmpModeler/installer/PUKE_INSTALLER_ASSETS.md)

## Maintaining this guide

Keep changes in the layer that owns the behavior and preserve existing callers
when changing shared interfaces. Update this guide when an entry point,
persisted format, dependency patch, or validation command changes. Record the
contract and how to check it. For contributions and redistribution, consult
[CONTRIBUTING.md](CONTRIBUTING.md), the [software license](LICENSE),
[asset license](ASSETS_LICENSE.md), [third-party notices](THIRD_PARTY.md),
and [branding guidelines](TRADEMARK.md).
