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

usage() {
  cat >&2 <<'USAGE'
usage: prepare-release.sh VERSION

Example:
  NeuralAmpModeler/scripts/prepare-release.sh 0.2.12

Updates config.h, generated macOS plist metadata, and installer/changelog.txt,
then commits, pushes, creates an annotated release tag, and pushes the tag.
USAGE
}

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

version=$1
if [[ ! "${version}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  echo "error: version must use major.minor.patch, for example 0.2.12." >&2
  exit 2
fi

major=${BASH_REMATCH[1]}
minor=${BASH_REMATCH[2]}
patch=${BASH_REMATCH[3]}

if (( major > 65535 || minor > 255 || patch > 255 )); then
  echo "error: version parts exceed iPlug's packed hex limits." >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
repository_dir="$(cd "${project_dir}/.." && pwd)"
config_path="${project_dir}/config.h"
changelog_path="${project_dir}/installer/changelog.txt"
tag="v${version}"
version_hex=$(printf '0x%04X%02X%02X' "${major}" "${minor}" "${patch}")
today=$(date '+%m/%d/%Y')

cd "${repository_dir}"

if [[ -n "$(git status --porcelain --untracked-files=no -- "${config_path}" "${changelog_path}")" ]]; then
  echo "error: config.h or installer/changelog.txt already has unstaged changes." >&2
  echo "Commit, stash, or discard those changes before preparing a release." >&2
  exit 1
fi

if git rev-parse "${tag}" >/dev/null 2>&1; then
  echo "error: tag ${tag} already exists locally." >&2
  exit 1
fi

if git ls-remote --exit-code --tags origin "refs/tags/${tag}" >/dev/null 2>&1; then
  echo "error: tag ${tag} already exists on origin." >&2
  exit 1
fi

LC_ALL=C perl -0pi -e 's/^#define PLUG_VERSION_HEX 0x[0-9A-Fa-f]+$/#define PLUG_VERSION_HEX '"${version_hex}"'/m; s/^#define PLUG_VERSION_STR "[^"]+"$/#define PLUG_VERSION_STR "'"${version}"'"/m' "${config_path}"

if ! grep -q " - ${tag}" "${changelog_path}"; then
  LC_ALL=C perl -0pi -e 's{\A([^\n]*\n[^\n]*\n)}{$1\n'"${today}"' - '"${tag}"'\n}' "${changelog_path}"
fi

(
  cd "${script_dir}"
  python3 ./update_version-mac.py
)

git add \
  NeuralAmpModeler/config.h \
  NeuralAmpModeler/installer/changelog.txt \
  NeuralAmpModeler/resources/NeuralAmpModeler-AAX-Info.plist \
  NeuralAmpModeler/resources/NeuralAmpModeler-AU-Info.plist \
  NeuralAmpModeler/resources/NeuralAmpModeler-VST3-Info.plist \
  NeuralAmpModeler/resources/NeuralAmpModeler-macOS-AUv3-Info.plist \
  NeuralAmpModeler/resources/NeuralAmpModeler-macOS-Info.plist

git commit -m "Prepare release ${version}"
git push
git tag -a "${tag}" -m "Puke Amp ${version}"
git push origin "${tag}"

echo "Prepared and pushed release ${version} (${version_hex})."
