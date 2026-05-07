#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/build-github-pages-apt-repository.sh --gpg-key <key-id> --changes <file.changes> [options]

Build a signed static APT repository suitable for GitHub Pages.

Options:
  --suite <name>          Debian suite/codename to publish. Default: trixie
  --component <name>      APT component. Default: main
  --architectures <list>  Comma-separated architectures, such as amd64 or amd64,arm64.
  --output <dir>          Output directory for GitHub Pages artifact. Default: public
  --repo-name <name>      Internal aptly repository name. Default: lofibox-<suite>
  --origin <text>         Release Origin field. Default: LoFiBox
  --label <text>          Release Label field. Default: LoFiBox Preview
  --gpg-key <key-id>      GPG key id/fingerprint used to sign Release/InRelease.
  --gpg-passphrase-file <path>
                          Optional passphrase file for protected signing keys.
  --changes <path>        Debian .changes file to include. May be repeated.
  --help                  Show this help.
EOF
}

suite="trixie"
component="main"
architectures=""
output="public"
repo_name=""
origin="LoFiBox"
label="LoFiBox Preview"
gpg_key=""
gpg_passphrase_file=""
changes=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      suite="${2:?missing value for --suite}"
      shift 2
      ;;
    --component)
      component="${2:?missing value for --component}"
      shift 2
      ;;
    --architectures)
      architectures="${2:?missing value for --architectures}"
      shift 2
      ;;
    --output)
      output="${2:?missing value for --output}"
      shift 2
      ;;
    --repo-name)
      repo_name="${2:?missing value for --repo-name}"
      shift 2
      ;;
    --origin)
      origin="${2:?missing value for --origin}"
      shift 2
      ;;
    --label)
      label="${2:?missing value for --label}"
      shift 2
      ;;
    --gpg-key)
      gpg_key="${2:?missing value for --gpg-key}"
      shift 2
      ;;
    --gpg-passphrase-file)
      gpg_passphrase_file="${2:?missing value for --gpg-passphrase-file}"
      shift 2
      ;;
    --changes)
      changes+=("${2:?missing value for --changes}")
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "$repo_name" ]]; then
  repo_name="lofibox-${suite}"
fi

if [[ -z "$gpg_key" ]]; then
  echo "--gpg-key is required; APT repositories must be signed." >&2
  exit 2
fi

if [[ ${#changes[@]} -eq 0 ]]; then
  echo "At least one --changes file is required." >&2
  exit 2
fi

if [[ -z "$output" || "$output" == "/" ]]; then
  echo "Refusing unsafe output directory: $output" >&2
  exit 2
fi

for tool in aptly dpkg-parsechangelog gpg; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    exit 127
  fi
done

for changes_file in "${changes[@]}"; do
  if [[ ! -f "$changes_file" ]]; then
    echo "Changes file not found: $changes_file" >&2
    exit 2
  fi
done

if [[ -n "$gpg_passphrase_file" && ! -f "$gpg_passphrase_file" ]]; then
  echo "GPG passphrase file not found: $gpg_passphrase_file" >&2
  exit 2
fi

repo_root="$(pwd)"
aptly_root="${APTLY_ROOT_DIR:-${repo_root}/.tmp/aptly-publish}"
aptly_config="${aptly_root}/aptly.conf"

rm -rf "$aptly_root" "$output"
mkdir -p "$aptly_root"
cat > "$aptly_config" <<EOF
{
  "rootDir": "$aptly_root",
  "downloadConcurrency": 4,
  "downloadSpeedLimit": 0,
  "downloadRetries": 0,
  "downloader": "default",
  "databaseOpenAttempts": -1,
  "architectures": [],
  "dependencyFollowSuggests": false,
  "dependencyFollowRecommends": false,
  "dependencyFollowAllVariants": false,
  "dependencyFollowSource": false,
  "dependencyVerboseResolve": false,
  "gpgDisableSign": false,
  "gpgDisableVerify": false,
  "gpgProvider": "gpg",
  "downloadSourcePackages": false,
  "skipLegacyPool": true,
  "ppaDistributorID": "debian",
  "ppaCodename": "",
  "skipContentsPublishing": false,
  "skipBz2Publishing": false,
  "FileSystemPublishEndpoints": {},
  "S3PublishEndpoints": {},
  "SwiftPublishEndpoints": {},
  "AzurePublishEndpoints": {},
  "AsyncAPI": false,
  "enableMetricsEndpoint": false
}
EOF
aptly_cmd=(aptly -config="$aptly_config")

"${aptly_cmd[@]}" repo create \
  -distribution="$suite" \
  -component="$component" \
  "$repo_name"

for changes_file in "${changes[@]}"; do
  "${aptly_cmd[@]}" repo include \
    -repo="$repo_name" \
    -ignore-signatures=true \
    -accept-unsigned=true \
    "$changes_file"
done

package_version="$(dpkg-parsechangelog -S Version)"
snapshot_version="${package_version//[^A-Za-z0-9_.+~-]/_}"
snapshot_name="${repo_name}-${snapshot_version}"

"${aptly_cmd[@]}" snapshot create "$snapshot_name" from repo "$repo_name"

publish_args=(
  publish
  snapshot
  "-batch=true"
  "-distribution=${suite}"
  "-component=${component}"
  "-origin=${origin}"
  "-label=${label}"
  "-gpg-key=${gpg_key}"
)

if [[ -n "$gpg_passphrase_file" ]]; then
  publish_args+=("-passphrase-file=${gpg_passphrase_file}")
fi

if [[ -n "$architectures" ]]; then
  publish_args+=("-architectures=${architectures}")
fi

publish_args+=("$snapshot_name" debian)
"${aptly_cmd[@]}" "${publish_args[@]}"

mkdir -p "$output"
cp -a "$aptly_root/public/." "$output/"
touch "$output/.nojekyll"
gpg --export-options export-minimal --export "$gpg_key" > "$output/lofibox-archive-keyring.pgp"

test -s "$output/lofibox-archive-keyring.pgp"
test -f "$output/debian/dists/$suite/InRelease"
test -f "$output/debian/dists/$suite/Release"
