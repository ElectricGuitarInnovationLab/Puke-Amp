#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
repository_dir="$(cd "${project_dir}/.." && pwd)"
iplug2_dir="${repository_dir}/iPlug2"
patch_path="${project_dir}/patches/iplug2-macos-retina-vst3.patch"

if [[ ! -d "${iplug2_dir}/.git" && ! -f "${iplug2_dir}/.git" ]]; then
  echo "error: iPlug2 submodule is not initialized at ${iplug2_dir}." >&2
  exit 1
fi

if grep -q "Cocoa owns backing density" "${iplug2_dir}/IPlug/VST3/IPlugVST3_View.h" \
  && grep -q "updateBackingScale" "${iplug2_dir}/IGraphics/Platforms/IGraphicsMac_view.h"; then
  echo "iPlug2 macOS Retina VST3 patch already applied."
  exit 0
fi

git -C "${iplug2_dir}" apply --recount "${patch_path}"
echo "Applied iPlug2 macOS Retina VST3 patch."
