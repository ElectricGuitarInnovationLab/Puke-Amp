#!/usr/bin/env bash

set -Eeuo pipefail

report_error() {
  local exit_code=$?
  local line_number=$1
  local command=$2

  echo "error: command failed with exit code ${exit_code} at line ${line_number}: ${command}" >&2
  exit "${exit_code}"
}

trap 'report_error "${LINENO}" "${BASH_COMMAND}"' ERR

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
repository_dir="$(cd "${project_dir}/.." && pwd)"
bundle_only=0
build_archs=()
acoustic_archs="arm64;x86_64"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bundle-only) bundle_only=1; shift ;;
    --arch)
      case "${2:-}" in
        universal) build_archs=("ARCHS=arm64 x86_64" ONLY_ACTIVE_ARCH=NO); acoustic_archs="arm64;x86_64" ;;
        arm64|x86_64) build_archs=("ARCHS=$2" ONLY_ACTIVE_ARCH=NO); acoustic_archs="$2" ;;
        *) echo 'error: --arch requires universal, arm64, or x86_64.' >&2; exit 2 ;;
      esac
      shift 2 ;;
    *) echo "error: unknown argument: $1" >&2; exit 2 ;;
  esac
done
build_root="${project_dir}/build-vst3/mac"
products_dir="${build_root}/products"
plugin_path="${products_dir}/Puke Amp.vst3"
staging_dir="${build_root}/package-root"
acoustic_build="${build_root}/acoustic"
acoustic_vst3="${acoustic_build}/products/PukeAcoustic.vst3"
acoustic_au="${acoustic_build}/products/PukeAcoustic.component"
license_path="${repository_dir}/LicenseText.rtf"

version="$(sed -n 's/^#define PLUG_VERSION_STR "\([^"]*\)"/\1/p' "${project_dir}/config.h")"
if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: PLUG_VERSION_STR is missing or is not a three-part numeric version." >&2
  exit 1
fi

artifact_base="Puke-Amp-${version}-macOS"
archive_path="${build_root}/${artifact_base}.zip"
installer_path="${build_root}/${artifact_base}.pkg"
component_packages_dir="${build_root}/component-packages"
distribution_path="${build_root}/distribution.xml"
installer_resources_dir="${build_root}/installer-resources"
package_id="${MACOS_PACKAGE_ID:-org.theRATLAB.PukeAmp.VST3}"
acoustic_vst3_id="org.theRATLAB.PukeAcoustic.VST3"
acoustic_au_id="org.theRATLAB.PukeAcoustic.AUv2"
release_build="${RELEASE_BUILD:-0}"

if [[ ! -f "${license_path}" ]]; then
  echo "error: installer license was not found at ${license_path}." >&2
  exit 1
fi

if ! command -v xcodebuild >/dev/null 2>&1; then
  echo "error: xcodebuild was not found. Install Xcode and its command-line tools." >&2
  exit 1
fi

"${project_dir}/scripts/apply-iplug2-patches.sh"

if [[ ! -d "${project_dir}/../iPlug2/Dependencies/IPlug/VST3_SDK" ]]; then
  echo "error: the VST3 SDK is missing." >&2
  echo "Run iPlug2/Dependencies/IPlug/download-iplug-sdks.sh first." >&2
  exit 1
fi

# Combined installers carry matching architecture slices for both plugins.
if [[ ${bundle_only} == 0 && -z "${build_archs[0]:-}" ]]; then
  build_archs=("ARCHS=arm64 x86_64" ONLY_ACTIVE_ARCH=NO)
fi

# GitHub-hosted runners start from a fresh checkout. Do not pass the clean action:
# recent Xcode versions can finish the build successfully but return code 65 when
# cleaning a custom CONFIGURATION_BUILD_DIR outside DerivedData.
xcodebuild \
  -project "${project_dir}/projects/NeuralAmpModeler-macOS.xcodeproj" \
  -xcconfig "${project_dir}/config/NeuralAmpModeler-mac.xcconfig" \
  -target VST3 \
  -configuration Release \
  "SYMROOT=${build_root}/intermediates" \
  "CONFIGURATION_BUILD_DIR=${products_dir}" \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  ${build_archs[@]+"${build_archs[@]}"} \
  build

if [[ ! -d "${plugin_path}" ]]; then
  echo "error: build succeeded but ${plugin_path} was not created." >&2
  exit 1
fi

# The exporter stages resources and performs release signing itself.
if [[ "${bundle_only}" == 1 ]]; then
  echo "Built ${plugin_path}"
  exit 0
fi

cmake -S "${repository_dir}/Acoustic" -B "${acoustic_build}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="${acoustic_archs}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
cmake --build "${acoustic_build}" --config Release --parallel 2
python3 "${repository_dir}/Acoustic/validate_factory_presets.py" "${acoustic_vst3}" "${acoustic_au}"

plugins=("${plugin_path}" "${acoustic_vst3}" "${acoustic_au}")
sign_args=(--force)
if [[ "${release_build}" == "1" ]]; then
  : "${MACOS_APPLICATION_SIGNING_IDENTITY:?Set MACOS_APPLICATION_SIGNING_IDENTITY for a release build.}"
  : "${MACOS_INSTALLER_SIGNING_IDENTITY:?Set MACOS_INSTALLER_SIGNING_IDENTITY for a release build.}"
  sign_args+=(--options runtime --timestamp --sign "${MACOS_APPLICATION_SIGNING_IDENTITY}")
else
  sign_args+=(--sign -)
fi

rm -rf "${staging_dir}"
mkdir -p "${staging_dir}"
for plugin in "${plugins[@]}"; do
  codesign "${sign_args[@]}" "${plugin}"
  codesign --verify --deep --strict --verbose=2 "${plugin}"
  ditto "${plugin}" "${staging_dir}/$(basename "${plugin}")"
done
rm -f "${archive_path}"
ditto -c -k "${staging_dir}" "${archive_path}"

build_component() {
  local bundle=$1 identifier=$2 component_version=$3 location=$4
  local name payload component_plist
  name="$(basename "${bundle}")"
  payload="${staging_dir}/${name}"
  component_plist="${payload}.plist"
  mkdir -p "${payload}"
  ditto "${bundle}" "${payload}/${name}"
  # Each payload has one known top-level bundle. Supply its installer metadata
  # directly because pkgbuild --analyze can omit default-valued keys.
  python3 - "${component_plist}" "${name}" <<'PYPLIST'
import plistlib
import sys

with open(sys.argv[1], "wb") as output:
    plistlib.dump([{
        "RootRelativeBundlePath": sys.argv[2],
        "BundleIsRelocatable": False,
        "BundleOverwriteAction": "upgrade",
    }], output)
PYPLIST
  pkgbuild --root "${payload}" --component-plist "${component_plist}" \
    --identifier "${identifier}" --version "${component_version}" \
    --install-location "${location}" "${component_packages_dir}/${identifier}.pkg"
}

if [[ "${release_build}" == "1" ]]; then
  acoustic_version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "${acoustic_vst3}/Contents/Info.plist")"
  rm -rf "${staging_dir}" "${component_packages_dir}" "${installer_resources_dir}"
  mkdir -p "${component_packages_dir}" "${installer_resources_dir}"
  ditto "${license_path}" "${installer_resources_dir}/LicenseText.rtf"
  build_component "${plugin_path}" "${package_id}" "${version}" "/Library/Audio/Plug-Ins/VST3"
  build_component "${acoustic_vst3}" "${acoustic_vst3_id}" "${acoustic_version}" "/Library/Audio/Plug-Ins/VST3"
  build_component "${acoustic_au}" "${acoustic_au_id}" "${acoustic_version}" "/Library/Audio/Plug-Ins/Components"

  cat > "${distribution_path}" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Puke Amp + Puke Amp: Acoustic ${version}</title>
  <license file="LicenseText.rtf" mime-type="text/rtf"/>
  <options customize="never" require-scripts="false"/>
  <choices-outline>
    <line choice="default"/>
  </choices-outline>
  <choice id="default" visible="false">
    <pkg-ref id="${package_id}"/>
    <pkg-ref id="${acoustic_vst3_id}"/>
    <pkg-ref id="${acoustic_au_id}"/>
  </choice>
  <pkg-ref id="${package_id}" version="${version}" onConclusion="none">${package_id}.pkg</pkg-ref>
  <pkg-ref id="${acoustic_vst3_id}" version="${acoustic_version}" onConclusion="none">${acoustic_vst3_id}.pkg</pkg-ref>
  <pkg-ref id="${acoustic_au_id}" version="${acoustic_version}" onConclusion="none">${acoustic_au_id}.pkg</pkg-ref>
</installer-gui-script>
EOF

  productbuild \
    --distribution "${distribution_path}" \
    --resources "${installer_resources_dir}" \
    --package-path "${component_packages_dir}" \
    --sign "${MACOS_INSTALLER_SIGNING_IDENTITY}" \
    "${installer_path}"

  pkgutil --check-signature "${installer_path}"

  if [[ -n "${APPLE_NOTARY_KEY_PATH:-}" || -n "${APPLE_NOTARY_KEY_ID:-}" || -n "${APPLE_NOTARY_ISSUER_ID:-}" ]]; then
    : "${APPLE_NOTARY_KEY_PATH:?Set all APPLE_NOTARY_* variables to notarize the package.}"
    : "${APPLE_NOTARY_KEY_ID:?Set all APPLE_NOTARY_* variables to notarize the package.}"
    : "${APPLE_NOTARY_ISSUER_ID:?Set all APPLE_NOTARY_* variables to notarize the package.}"

    xcrun notarytool submit "${installer_path}" \
      --key "${APPLE_NOTARY_KEY_PATH}" \
      --key-id "${APPLE_NOTARY_KEY_ID}" \
      --issuer "${APPLE_NOTARY_ISSUER_ID}" \
      --wait
    xcrun stapler staple "${installer_path}"
    xcrun stapler validate "${installer_path}"
    spctl --assess --type install --verbose=4 "${installer_path}"
  elif [[ "${REQUIRE_NOTARIZATION:-1}" == "1" ]]; then
    echo "error: release builds require APPLE_NOTARY_KEY_PATH, APPLE_NOTARY_KEY_ID, and APPLE_NOTARY_ISSUER_ID." >&2
    exit 1
  fi

  rm -rf "${component_packages_dir}" "${installer_resources_dir}"
  rm -f "${distribution_path}"
  echo "Packaged ${installer_path}"
fi

echo "Built ${plugin_path}"
echo "Packaged ${archive_path}"
rm -rf "${staging_dir}"
