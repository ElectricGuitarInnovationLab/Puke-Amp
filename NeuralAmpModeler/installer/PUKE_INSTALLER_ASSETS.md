# Prebuilt VST3 inputs for Puke's installer

These commands export only a release-signed **Puke Amp.vst3** bundle and a
manifest. They do not create an Amp installer, tag, commit, GitHub release, or
write to the sibling Puke repository. Existing release commands retain their
default behavior. Puke Amp's version comes from `NeuralAmpModeler/config.h`,
independently of Puke's version; `STATE_VERSION_STR` is not the product version.

## Prerequisites

Use a recursive Git checkout (`git submodule update --init --recursive`). From
the repository root, download dependencies once using macOS Terminal or Windows
Git Bash:

```sh
(cd iPlug2/Dependencies/IPlug && ./download-iplug-sdks.sh)
(cd iPlug2/Dependencies && ./download-prebuilt-libs.sh)
```

Python 3.9 or later is required (`python3` on macOS, `python.exe` on Windows).
Run platform builds on the native OS; a Linux development container cannot
perform either platform's release validation. No ad-hoc or unsigned fallback
is supported. Signing and timestamp/notarization services require network access.

## macOS

Install full Xcode and select its command-line tools. Import a **Developer ID
Application** certificate and its private key into the keychain. An installer
certificate, `appdmg`, and Inno Setup are not needed for these exports.

The command reuses `CodesigningScripts/codesign-puke-amp.sh`, including its
`CodesigningScripts/signing.env` configuration. Configure `IDENTITY` to the full
Developer ID Application identity available in `security find-identity -v -p
codesigning`. The existing helper defaults to the project's Clear Blue Media
identity and `TEAM_ID`. Optionally set `ENTITLEMENTS_PATH` using the existing
helper's configuration. Never put private keys or passwords in the repository.

Set `NOTARY_PROFILE` to an existing `notarytool` keychain profile, either in the
environment or in `signing.env` (that file is sourced and takes precedence).
Create a profile using Apple's secure interactive prompts:

```sh
xcrun notarytool store-credentials PROFILE
export NOTARY_PROFILE=PROFILE
# Only if your signing identity differs from the project's configured default:
export IDENTITY='Developer ID Application: Your Organization (TEAMID)'
```

**Run this exact export command on the Mac, from the Puke-Amp repository root:**

```sh
./NeuralAmpModeler/scripts/export-puke-installer-assets.sh --arch universal
```

`--arch arm64` and `--arch x86_64` are also supported. Universal exports require
both actual Mach-O slices. The exporter builds the Release VST3, stages the
complete bundle and resources, verifies versions and architectures, then invokes
the existing Developer ID signing/notarization/stapling helper. It checks the
Apple Developer ID certificate requirement, strict recursive signatures and the
stapled ticket before and after creating/extracting the final ZIP. Nested Mach-O
files must support all requested slices and their non-system dependencies must
resolve inside the bundle. No resources are changed after signing.

The existing helper uses a keychain notarization profile for bundle submissions.
The `APPLE_NOTARY_*` credentials used by the separate PKG release workflow are
not automatically substituted for that profile.

## Windows x64

Use native Windows with Visual Studio 2022's **Desktop development with C++**
workload, the Windows SDK signing tools, and Python 3.9+. Open a Visual Studio
Developer PowerShell so `msbuild.exe` and `dumpbin.exe` are on PATH. Only x64 is
supported: the project has Win32 and x64 configurations, with no native ARM64
configuration. Inno Setup is not required for this export.

Reuse the existing Windows release signing variables:

- `WINDOWS_CERTIFICATE_PATH`: path to a PFX containing a trusted code-signing
  certificate and its private key.
- `WINDOWS_CERTIFICATE_PASSWORD`: set securely in the current process environment;
  do not write the password into a command saved in history or a committed file.
- `WINDOWS_SIGNTOOL_PATH`: optional full path to `signtool.exe`; otherwise the
  exporter locates the latest installed x64 Windows SDK signing tool.

For example, after setting the certificate path and optional signing tool path,
read the password without echoing it:

```powershell
$secret = Read-Host 'PFX password' -AsSecureString
$env:WINDOWS_CERTIFICATE_PASSWORD = [System.Net.NetworkCredential]::new('', $secret).Password
try {
    .\NeuralAmpModeler\scripts\export-puke-installer-assets.ps1 -Architecture x64
} finally {
    Remove-Item Env:\WINDOWS_CERTIFICATE_PASSWORD
    $secret.Dispose()
}
```

The exporter imports the PFX into the current user's certificate store and signs
by thumbprint, keeping the password out of child-process arguments. Certificates
newly imported by this command are removed in `finally`; preexisting store
certificates are retained. SHA-256 signatures use DigiCert's RFC 3161 timestamp
service, matching the release script. Every bundled DLL/VST3 executable is
checked for PE machine `0x8664`, signed, and verified with both SignTool and
Authenticode (including expected certificate and timestamp), before and after
ZIP extraction. DLL imports must resolve beside their importing binary or in
Windows System32; Windows API-set imports are supplied by the OS. The project's
MSVC runtime is statically linked. Windows version resources now derive from
`PLUG_VERSION_HEX`/`PLUG_VERSION_STR`, and both numeric and string versions are
checked against the built plug-in.

## Outputs and validation

Each successful command replaces only its own platform folder under:

```text
dist/puke-installer-assets/packaging/vendor/puke-amp/
  macos/
    manifest.json
    Puke-Amp-<version>-macOS.zip
  windows-x64/
    manifest.json
    Puke-Amp-<version>-Windows-x64.zip
```

A platform folder appears only after successful verification. Existing exports
are retained on build/signing/verification failure; other platforms are untouched.
Temporary working directories are removed on exit. Do not run two exports for
the same platform concurrently.

Each ZIP contains exactly one top-level `Puke Amp.vst3`, including all original
bundle contents and required dependencies. Before signing, the exporter copies
the complete `Models` tree and adds `LICENSE`, `LicenseText.rtf`,
`ASSETS_LICENSE.md`, `THIRD_PARTY.md`, and `ThirdPartyNotices.txt` under
`Contents/Resources`. Validation compares all shipped models, pedal models, IRs,
factory presets, and notices with source hashes, rejects external/broken symlinks,
credentials/build intermediates and absolute developer paths in factory presets.
Factory presets must retain portable asset references.

A fresh extraction must match the signed bundle byte for byte, including internal
symlinks and (on macOS) POSIX permissions. Windows ZIP creation includes hidden
files and encodes symlinks explicitly; checkouts containing symlinks require
Windows permission to create symlinks (for example Developer Mode). Failures return
a nonzero status. No manifest marked signed is written before these checks pass.
The manifest records the actual Puke Amp version, actual requested/verified
architectures, final ZIP SHA-256, and signing/notarization status using schema 1.
Only macOS reports `notarized: true`.

Portable resource/archive tests run with:

```sh
python3 -m unittest discover -s NeuralAmpModeler/scripts/tests -v
```

Real Xcode builds, Developer ID verification, Apple submission/stapling, and
macOS ZIP ticket preservation require a Mac and valid credentials. MSBuild,
resource compilation, PE dependency checks, certificate import, trusted
Authenticode verification and timestamping require a native Windows machine and
valid credentials. Portable tests do not establish either platform's release
signature validity. A final DAW scan/load test on each target OS is recommended
before incorporating these prebuilt inputs into Puke's installer.
