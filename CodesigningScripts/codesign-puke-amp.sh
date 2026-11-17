#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
project_dir="${repo_root}/NeuralAmpModeler"
PRODUCT_NAME="${PRODUCT_NAME:-Puke Amp}"
OUTPUT_DIR="${OUTPUT_DIR:-${project_dir}/build-vst3/mac}"
PLUGIN_BUNDLE="${PLUGIN_BUNDLE:-${OUTPUT_DIR}/products/${PRODUCT_NAME}.vst3}"
ENTITLEMENTS_PATH="${ENTITLEMENTS_PATH:-}"
TEAM_ID="${TEAM_ID:-HSAYDGFEVC}"
IDENTITY="${IDENTITY:-Developer ID Application: Clear Blue Media LLC (${TEAM_ID})}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

if [[ -f "${script_dir}/signing.env" ]]; then
  # shellcheck disable=SC1091
  source "${script_dir}/signing.env"
fi

if [[ "${REQUIRE_DEVELOPER_ID:-0}" == 1 && "${IDENTITY}" != 'Developer ID Application: '* ]]; then
  echo 'error: installer asset exports require a Developer ID Application identity (IDENTITY); ad-hoc signing is forbidden.' >&2
  exit 1
fi

for command_name in codesign ditto security xcrun; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "error: required command not found: ${command_name}" >&2
    exit 1
  fi
done

if [[ -z "${NOTARY_PROFILE:-}" ]]; then
  echo "error: NOTARY_PROFILE is not set in ${script_dir}/signing.env or the environment." >&2
  exit 1
fi

if [[ ! -d "${PLUGIN_BUNDLE}" ]]; then
  echo "error: VST3 bundle not found at ${PLUGIN_BUNDLE}" >&2
  exit 1
fi

if ! security find-identity -v -p codesigning | grep -F -- "${IDENTITY}" >/dev/null; then
  echo "error: signing identity is not available in the keychain: ${IDENTITY}" >&2
  exit 1
fi

echo "🧹 Removing legacy CodeResources and old signatures..."
rm -f "${PLUGIN_BUNDLE}/Contents/CodeResources"
rm -rf "${PLUGIN_BUNDLE}/Contents/_CodeSignature"

VERSION="unknown"
if [[ -f "${PLUGIN_BUNDLE}/Contents/Info.plist" ]]; then
  VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "${PLUGIN_BUNDLE}/Contents/Info.plist" 2>/dev/null || echo "unknown")
fi

mkdir -p "${OUTPUT_DIR}"

NOTARIZE_ZIP="${OUTPUT_DIR}/${PRODUCT_NAME}-VST3-${VERSION}-NOTARIZE.zip"
FINAL_ZIP="${OUTPUT_DIR}/${PRODUCT_NAME}-VST3-macOS-${VERSION}.zip"

echo "🧼 Cleaning previous zip files..."
rm -f "$NOTARIZE_ZIP" "$FINAL_ZIP"

SIGN_ARGS=(--force --timestamp --options runtime --sign "${IDENTITY}")
if [[ -n "${ENTITLEMENTS_PATH}" && -f "${ENTITLEMENTS_PATH}" ]]; then
  SIGN_ARGS+=(--entitlements "${ENTITLEMENTS_PATH}")
elif [[ -n "${ENTITLEMENTS_PATH}" ]]; then
  echo "error: entitlements file not found: ${ENTITLEMENTS_PATH}" >&2
  exit 1
fi

echo "🔏 Signing nested frameworks..."
while IFS= read -r -d '' FRAMEWORK; do
  echo "Signing $FRAMEWORK"
  codesign "${SIGN_ARGS[@]}" "$FRAMEWORK"
done < <(find "${PLUGIN_BUNDLE}/Contents/Frameworks" -type d -name "*.framework" -print0 2>/dev/null || true)

echo "🔏 Signing bundles..."
while IFS= read -r -d '' BUNDLE; do
  echo "Signing $BUNDLE"
  codesign "${SIGN_ARGS[@]}" "$BUNDLE"
done < <(find "${PLUGIN_BUNDLE}/Contents" -type d -name "*.bundle" -print0 2>/dev/null || true)

echo "🔏 Signing dylibs and executables..."
while IFS= read -r -d '' BIN; do
  echo "Signing $BIN"
  codesign "${SIGN_ARGS[@]}" "$BIN"
done < <(
  find "${PLUGIN_BUNDLE}/Contents" -type f \( -name "*.dylib" -o -name "*.so" -o -path "*/MacOS/*" \) -print0 2>/dev/null || true
)

echo "🔏 Signing VST3 bundle..."
codesign "${SIGN_ARGS[@]}" --deep "${PLUGIN_BUNDLE}"

if [[ -f "${PLUGIN_BUNDLE}/Contents/CodeResources" ]]; then
  echo "❌ Unexpected legacy CodeResources detected after signing."
  echo "Remove ${PLUGIN_BUNDLE}/Contents/CodeResources and re-sign."
  exit 1
fi

echo "🔍 Verifying codesign..."
codesign --verify --deep --strict --verbose=2 "${PLUGIN_BUNDLE}"
if [[ "${REQUIRE_DEVELOPER_ID:-0}" == 1 ]]; then
  codesign --verify --strict -R '=anchor apple generic and certificate leaf[field.1.2.840.113635.100.6.1.13] exists' "${PLUGIN_BUNDLE}"
fi

echo "📦 Creating zip for notarization..."
ditto -c -k --keepParent "${PLUGIN_BUNDLE}" "${NOTARIZE_ZIP}"

echo "☁️ Submitting to Apple for notarization..."
xcrun notarytool submit "$NOTARIZE_ZIP" \
  --keychain-profile "$NOTARY_PROFILE" \
  --wait

echo "📎 Stapling ticket to VST3..."
xcrun stapler staple -v "${PLUGIN_BUNDLE}"
xcrun stapler validate -v "${PLUGIN_BUNDLE}"
codesign --verify --deep --strict "${PLUGIN_BUNDLE}"

echo "📦 Creating stapled zip for distribution..."
ditto -c -k --keepParent "${PLUGIN_BUNDLE}" "${FINAL_ZIP}"

echo "🧹 Cleaning up notarization zip..."
rm -f "${NOTARIZE_ZIP}"

echo "✅ Done!"
echo "Notarized VST3 bundle: ${PLUGIN_BUNDLE}"
echo "Final stapled zip for distribution: ${FINAL_ZIP}"
