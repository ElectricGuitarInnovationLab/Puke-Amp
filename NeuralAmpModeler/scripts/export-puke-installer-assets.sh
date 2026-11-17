#!/usr/bin/env bash
set -Eeuo pipefail
# Do not use xtrace: signing configuration may contain private values.
set +x
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "${script_dir}/../.." && pwd)"
arch=universal
while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch)
      arch="${2:-}"
      case "$arch" in universal|arm64|x86_64) ;; *) echo 'error: --arch requires universal, arm64, or x86_64.' >&2; exit 2 ;; esac
      shift 2 ;;
    --help|-h) echo 'Usage: export-puke-installer-assets.sh [--arch universal|arm64|x86_64]'; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'error: run this command in macOS Terminal on the Mac with Xcode and Developer ID credentials, outside the Linux container.' >&2
  exit 1
fi
for tool in python3 xcodebuild codesign security xcrun ditto lipo otool; do
  command -v "$tool" >/dev/null || { echo "error: required tool missing: $tool" >&2; exit 1; }
done
# Match the existing signing helper's configuration, and fail before building.
TEAM_ID="${TEAM_ID:-HSAYDGFEVC}"
IDENTITY="${IDENTITY:-Developer ID Application: Clear Blue Media LLC (${TEAM_ID})}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
if [[ -f "${repo}/CodesigningScripts/signing.env" ]]; then
  source "${repo}/CodesigningScripts/signing.env"
fi
if [[ "$IDENTITY" != 'Developer ID Application: '* ]] || ! security find-identity -v -p codesigning | grep -F -- "$IDENTITY" >/dev/null; then
  echo 'error: import your Developer ID Application certificate and private key into the login keychain, then set IDENTITY to its full Developer ID Application name. See CodesigningScripts/codesign-puke-amp.sh.' >&2
  exit 1
fi
if [[ -z "$NOTARY_PROFILE" ]]; then
  echo 'error: set NOTARY_PROFILE in the environment or CodesigningScripts/signing.env. Store credentials first with: xcrun notarytool store-credentials PROFILE (follow its secure prompts).' >&2
  exit 1
fi
helper="${script_dir}/installer_assets.py"
version="$(python3 "$helper" version)"
output="${repo}/dist/puke-installer-assets/packaging/vendor/puke-amp"
mkdir -p "$output"
work="$(mktemp -d "${output}/.macos.XXXXXX")"
trap 'rm -rf "$work"' EXIT
"${script_dir}/make-vst3-mac.sh" --bundle-only --arch "$arch"
bundle="${work}/staging/Puke Amp.vst3"
mkdir -p "${work}/staging" "${work}/macos" "${work}/extracted"
# ditto preserves executable modes and internal symlinks.
source_bundle="$(cd "${repo}/NeuralAmpModeler/build-vst3/mac/products/Puke Amp.vst3" && pwd -P)"
ditto "$source_bundle" "$bundle"
python3 "$helper" stage --bundle "$bundle"
python3 "$helper" mac-check --bundle "$bundle" --arch "$arch"
PLUGIN_BUNDLE="$bundle" OUTPUT_DIR="${work}/signing" REQUIRE_DEVELOPER_ID=1 \
  IDENTITY="$IDENTITY" NOTARY_PROFILE="$NOTARY_PROFILE" \
  "${repo}/CodesigningScripts/codesign-puke-amp.sh"
verify() {
  python3 "$helper" mac-check --bundle "$1" --arch "$arch"
  codesign --verify --deep --strict "$1"
  codesign --verify --strict -R '=anchor apple generic and certificate leaf[field.1.2.840.113635.100.6.1.13] exists' "$1"
  xcrun stapler validate "$1"
}
verify "$bundle"
archive="${work}/macos/Puke-Amp-${version}-macOS.zip"
# Avoid a second top-level __MACOSX metadata directory. The extracted ticket is
# verified below, so a ticket lost by archive creation can never be exported.
ditto -c -k --norsrc --noextattr --keepParent "$bundle" "$archive"
ditto -x -k "$archive" "${work}/extracted"
python3 "$helper" archive-check --bundle "$bundle" --archive "$archive" --extracted "${work}/extracted"
verify "${work}/extracted/Puke Amp.vst3"
python3 "$helper" manifest --bundle "${work}/extracted/Puke Amp.vst3" --archive "$archive" --platform macos --arch "$arch"
# Publish only a verified platform folder. Other platforms are untouched.
if [[ -e "${output}/macos" ]]; then mv "${output}/macos" "${work}/previous"; fi
if ! mv "${work}/macos" "${output}/macos"; then
  if [[ -e "${work}/previous" ]]; then mv "${work}/previous" "${output}/macos"; fi
  exit 1
fi
echo "Exported ${output}/macos"
