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
build_root="${project_dir}/build-vst3/mac"
products_dir="${build_root}/products"
plugin_path="${products_dir}/Puke Amp.vst3"
staging_dir="${build_root}/package-root"
license_path="${repository_dir}/LicenseText.rtf"

version="$(sed -n 's/^#define PLUG_VERSION_STR "\([^"]*\)"/\1/p' "${project_dir}/config.h")"
if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: PLUG_VERSION_STR is missing or is not a three-part numeric version." >&2
  exit 1
fi

artifact_base="Puke-Amp-${version}-macOS"
archive_path="${build_root}/${artifact_base}.zip"
installer_path="${build_root}/${artifact_base}.pkg"
component_package_path="${build_root}/${artifact_base}-component.pkg"
distribution_path="${build_root}/distribution.xml"
installer_resources_dir="${build_root}/installer-resources"
package_id="${MACOS_PACKAGE_ID:-org.theRATLAB.PukeAmp.VST3}"
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
  build

if [[ ! -d "${plugin_path}" ]]; then
  echo "error: build succeeded but ${plugin_path} was not created." >&2
  exit 1
fi

# Release packages must contain a Developer ID-signed plug-in. Local builds use
# an ad-hoc signature and still produce a ZIP for development/testing.
if [[ "${release_build}" == "1" ]]; then
  : "${MACOS_APPLICATION_SIGNING_IDENTITY:?Set MACOS_APPLICATION_SIGNING_IDENTITY for a release build.}"
  : "${MACOS_INSTALLER_SIGNING_IDENTITY:?Set MACOS_INSTALLER_SIGNING_IDENTITY for a release build.}"

  codesign \
    --force \
    --deep \
    --strict \
    --options runtime \
    --timestamp \
    --sign "${MACOS_APPLICATION_SIGNING_IDENTITY}" \
    "${plugin_path}"
  codesign --verify --deep --strict --verbose=2 "${plugin_path}"
else
  codesign --force --deep --sign - "${plugin_path}"
fi

ditto -c -k --keepParent "${plugin_path}" "${archive_path}"

if [[ "${release_build}" == "1" ]]; then
  rm -rf "${staging_dir}"
  rm -rf "${installer_resources_dir}"
  rm -f "${component_package_path}" "${distribution_path}"
  mkdir -p "${staging_dir}" "${installer_resources_dir}"
  ditto "${plugin_path}" "${staging_dir}/Puke Amp.vst3"
  ditto "${license_path}" "${installer_resources_dir}/LicenseText.rtf"

  pkgbuild \
    --root "${staging_dir}" \
    --identifier "${package_id}" \
    --version "${version}" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "${component_package_path}"

  cat > "${distribution_path}" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Puke Amp ${version}</title>
  <license file="LicenseText.rtf" mime-type="text/rtf"/>
  <options customize="never" require-scripts="false"/>
  <choices-outline>
    <line choice="default"/>
  </choices-outline>
  <choice id="default" visible="false">
    <pkg-ref id="${package_id}"/>
  </choice>
  <pkg-ref id="${package_id}" version="${version}" onConclusion="none">$(basename "${component_package_path}")</pkg-ref>
</installer-gui-script>
EOF

  productbuild \
    --distribution "${distribution_path}" \
    --resources "${installer_resources_dir}" \
    --package-path "${build_root}" \
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
  elif [[ "${REQUIRE_NOTARIZATION:-1}" == "1" ]]; then
    echo "error: release builds require APPLE_NOTARY_KEY_PATH, APPLE_NOTARY_KEY_ID, and APPLE_NOTARY_ISSUER_ID." >&2
    exit 1
  fi

  rm -rf "${staging_dir}" "${installer_resources_dir}"
  rm -f "${component_package_path}" "${distribution_path}"
  echo "Packaged ${installer_path}"
fi

echo "Built ${plugin_path}"
echo "Packaged ${archive_path}"
