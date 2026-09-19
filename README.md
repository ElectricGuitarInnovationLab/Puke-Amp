# Puke Amp

**Puke Amp** is an open-source guitar amplifier modeling plugin and asset library developed by [the RATLab](http://theratlab.org) as part of [Puke Studio](http://puke.studio) and adapted from the excellent [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) project by Steven Atkinson.

This repository contains:

- The Puke Amp plugin source and build configuration based on **iPlug2**.
- A collection of `.nam` amplifier models (in `Models/Amp`)
- A collection of `.nam` pedal models (in `Models/FX`)
- A collection of `.wav` impulse responses (IRs) (in `Models/IR`)
- Factory `.fxp` presets (in `Models/Presets`)
- Supporting assets and documentation developed as part of the **Puke Studio** project.

The included amplifier models and impulse responses were created by the RATLab and are intended for research, education, and music production.

> **Licensing**
>
> - The **Puke Amp software** is licensed under the GNU Affero General Public License v3.0 (AGPLv3). See `LICENSE`.
> - Portions of this project are derived from **Neural Amp Modeler**, which is licensed under the MIT License. See `THIRD_PARTY.md`.
> - The included `.nam` models, `.wav` impulse responses, and other audio assets are licensed separately under **CC BY 4.0**. See `ASSETS_LICENSE.md`.

## Pedal FX chain

Put pedal capture `.nam` files under `Models/FX` to make them available in the pedal browser. Click the pedal icon in the upper-left of the plugin to open the FX-chain screen. The chain supports up to eight ordered slots before the main amp model and IR. Each slot has bypass, input and output trim, and an independent three-band EQ. Pedal model CPU/Quality is fixed at `1.0`.

The arrow buttons move the selected pedal in the chain, and the global FX CHAIN switch bypasses all pedal slots. Pedal paths, order, bypass states, trims, and EQ settings are included in DAW state and `.fxp` presets.

## Presets

The preset browser in the plug-in header combines three locations:

- Factory presets shipped in `Models/Presets`
- User presets saved by the plug-in
- Additional folders selected with **Add Preset Folder...** in the preset menu

The save and open icons sit on either side of the preset name. Saving defaults to
the user preset folder. Opening or saving a preset elsewhere adds its containing
folder to the menu, and imported folder locations persist across plug-in instances.
The menu is rescanned whenever it opens and supports subfolders.

User presets are stored by default in:

- macOS: `~/Library/Application Support/Puke Amp/Presets`
- Windows: `%LOCALAPPDATA%\Puke Amp\Presets`

Factory presets use portable references for amp, pedal, and IR files beneath the
bundled `Models` directory. External assets retain their absolute paths. Puke Amp
also remaps missing bundled paths from older `.fxp` files when their `Models/...`
suffix matches an asset in the current installation.

## Building

Puke Amp inherits much of its build system from the original Neural Amp Modeler project.

The build scripts are located in:

- `NeuralAmpModeler/scripts/`

The original GitHub Actions workflows in the Neural Amp Modeler project also serve as useful build references:

- https://github.com/sdatkinson/NeuralAmpModelerPlugin/tree/main/.github/workflows

### Building Puke Amp VST3

The easiest option is **Actions → Build Puke Amp VST3 → Run workflow** on GitHub. When both jobs finish, download the `Puke-Amp-VST3-macOS` or `Puke-Amp-VST3-Windows` artifact. Each artifact contains a ZIP with the complete `Puke Amp.vst3` bundle, including `Models/Amp`, `Models/FX`, `Models/IR`, and `Models/Presets`. The Windows artifact also contains a versioned `Puke-Amp-<version>-Windows-Setup-Unsigned.exe` installer.

#### Compiling the VST3 locally on macOS

Requirements:

- A Mac running a supported version of macOS.
- Xcode and its command-line tools.
- Git, including submodule support.
- A clone of this repository. A source ZIP does not contain Git submodule metadata.

Run all build commands in the macOS Terminal application. A prompt that starts
with something like `root@container:/workspaces/...` is a Linux container and
cannot access Xcode on the Mac. Confirm that the shell is running directly on
macOS:

```sh
uname -s
```

The output must be:

```text
Darwin
```

If you have not cloned the repository yet, clone it with all submodules:

```sh
git clone --recursive https://github.com/ElectricGuitarInnovationLab/Puke-Amp.git
cd NeuralAmpModelerPlugin
```

If you already cloned it with Git, do not clone it again. Change to the existing
repository root and initialize any missing submodules:

```sh
cd "/path/to/Puke-Amp"
git submodule update --init --recursive
```

If you originally downloaded a source ZIP, make a recursive Git clone instead.

Verify that Xcode is available:

```sh
xcodebuild -version
xcode-select -p
```

If `xcodebuild` is not found and Xcode is installed at
`/Applications/Xcode.app`, select it and complete its first-launch setup:

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
sudo xcodebuild -runFirstLaunch
```

From the repository root, download the VST3 SDK and iPlug2 prebuilt libraries.
The SDK downloader must run from its own directory because it invokes helper
scripts using relative paths:

```sh
(cd iPlug2/Dependencies/IPlug && ./download-iplug-sdks.sh)
(cd iPlug2/Dependencies && ./download-prebuilt-libs.sh)
```

These dependency commands normally need to be run only once. Then, from the repository root, compile,
ad-hoc sign, and package the VST3:

```sh
./NeuralAmpModeler/scripts/make-vst3-mac.sh
```

Successful builds produce:

```text
NeuralAmpModeler/build-vst3/mac/products/Puke Amp.vst3
NeuralAmpModeler/build-vst3/mac/Puke-Amp-0.1.0-macOS.zip
```

The `.vst3` path is a plugin bundle, even though Finder displays it like a
single file. To install it for the current user:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
ditto "NeuralAmpModeler/build-vst3/mac/products/Puke Amp.vst3" \
  "$HOME/Library/Audio/Plug-Ins/VST3/Puke Amp.vst3"
```

Restart the DAW or rescan its plugins after installation.

#### Testing macOS release signing

The release workflow can exercise the complete macOS signing path without
creating a tag or GitHub release. The required Apple certificates,
notarization key, and GitHub Actions secrets are documented in
[`NeuralAmpModeler/installer/README.md`](NeuralAmpModeler/installer/README.md).

After the workflow and secrets are present on the default branch:

1. Open **Actions → Release Puke Amp → Run workflow** on GitHub.
2. Leave **Also build the Windows installer** unchecked.
3. Run the workflow and wait for **Build signed macOS installer** to finish.
4. Download the `release-macos` workflow artifact.

The test builds and Developer ID-signs the VST3, creates and signs the installer
package, submits it to Apple with `notarytool`, staples the ticket, and validates
the result. It does not create a GitHub release.

Verify the downloaded package on a Mac if desired:

```sh
pkgutil --check-signature "Puke-Amp-0.1.0-macOS.pkg"
spctl --assess --type install --verbose=4 "Puke-Amp-0.1.0-macOS.pkg"
xcrun stapler validate "Puke-Amp-0.1.0-macOS.pkg"
```

#### Publishing a release

Releases are initiated by version tags. For example, to publish version `0.2.11`:

1. Update the product version in `NeuralAmpModeler/config.h`:

   ```c
   #define PLUG_VERSION_HEX 0x00000211
   #define PLUG_VERSION_STR "0.2.11"
   ```

   Do not change `STATE_VERSION_STR` unless the serialized plugin-state format
   changes.

2. Update the changelog or other release notes, then commit and push:

   ```sh
   git add NeuralAmpModeler/config.h NeuralAmpModeler/installer/changelog.txt
   git commit -m "Prepare release 0.2.11"
   git push
   ```

3. Create and push an annotated tag matching `PLUG_VERSION_STR` exactly:

   ```sh
   git tag -a v0.2.11 -m "Puke Amp 0.2.11"
   git push origin v0.2.11
   ```

The **Release Puke Amp** workflow then validates the tag, builds both platforms,
signs and notarizes macOS, generates `SHA256SUMS.txt`, and creates a draft GitHub
release. Without optional Windows signing credentials, the Windows installer is
clearly labeled `-Unsigned` and the draft contains an unknown-publisher warning.

Expected draft assets are:

```text
Puke-Amp-0.2.1-macOS.pkg
Puke-Amp-0.2.1-Windows-Setup-Unsigned.exe
SHA256SUMS.txt
```

Download and test the draft assets, then select **Publish release** on GitHub.
Publishing makes the release visible to GitHub clients and the in-app update
checker. Drafts and prereleases do not trigger a plug-in notification. The
workflow never publishes a release automatically.

See Apple's [custom notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
for background on signing, notarization, and stapling.

To update and rebuild later:

```sh
git pull
git submodule update --init --recursive
./NeuralAmpModeler/scripts/make-vst3-mac.sh
```

If the build reports that the VST3 SDK is missing, rerun the two dependency
download commands above. If it reports `xcodebuild was not found`, make sure the
command is running in macOS Terminal rather than a container and repeat the
`xcode-select` setup. For other Xcode failures, read the first `error:` above
`BUILD FAILED`; exit code 65 is Xcode's general build-failure code.

Generated dependencies under `Build/`, local `build-vst3/` output, and macOS
`.DS_Store` metadata are ignored by Git.

For local Windows builds, first download the iPlug2 dependencies from Git Bash:

```sh
(cd iPlug2/Dependencies/IPlug && ./download-iplug-sdks.sh)
(cd iPlug2/Dependencies && ./download-prebuilt-libs.sh)
```

Install [Inno Setup 6](https://jrsoftware.org/isinfo.php), then run this from a
Visual Studio Developer Command Prompt:

```bat
NeuralAmpModeler\scripts\make-vst3-win.bat
```

The 64-bit build, distributable ZIP, and Windows installer are written to:

```text
NeuralAmpModeler\build-vst3\windows\Puke Amp.vst3
NeuralAmpModeler\build-vst3\windows\Puke-Amp-0.1.0-Windows-Unsigned.zip
NeuralAmpModeler\build-vst3\windows\Puke-Amp-0.1.0-Windows-Setup-Unsigned.exe
```

Run the installer as an administrator to install the complete VST3 bundle in
`C:\Program Files\Common Files\VST3\Puke Amp.vst3`, the standard system-wide
location for a 64-bit Windows VST3. The installer also adds a normal Windows
uninstall entry. Restart the DAW or rescan its plugins after installation.

The generated installer is not digitally signed. Windows may display an
Unknown Publisher or SmartScreen warning. If optional Windows signing secrets
are configured later, the build scripts sign the VST3 and installer and omit
the `-Unsigned` filename suffix.


## Trademark Notice

The included .nam models and impulse responses were independently created by the RATLab from recordings of physical amplifiers and speaker cabinets. References to amplifier and speaker model names are provided solely to identify the equipment used during profiling and do not imply endorsement, affiliation, or sponsorship by their respective manufacturers.

Puke Amp, the RATLab, and the Puke Studio project are not affiliated with, endorsed by, sponsored by, or associated with any amplifier, speaker, or equipment manufacturer.

All trademarks, trade names, and product names are the property of their respective owners.
