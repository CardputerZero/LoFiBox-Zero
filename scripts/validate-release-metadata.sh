#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/validate-release-metadata.sh <upstream-version>

Validate that release-bearing metadata agrees on the upstream version before a
source release tag is created.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

version="${1:-}"
if [[ -z "$version" ]]; then
  usage >&2
  exit 2
fi

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Release version must be X.Y.Z, got: $version" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

project_version="$(
  sed -nE 's/^project\(LoFiBoxZero VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES C CXX\)$/\1/p' CMakeLists.txt
)"
if [[ "$project_version" != "$version" ]]; then
  echo "CMakeLists.txt version mismatch: expected $version, got ${project_version:-<none>}" >&2
  exit 1
fi

if ! command -v dpkg-parsechangelog >/dev/null 2>&1; then
  echo "Required tool not found: dpkg-parsechangelog" >&2
  exit 127
fi

debian_version="$(dpkg-parsechangelog -S Version)"
debian_upstream_version="${debian_version%%-*}"
if [[ "$debian_upstream_version" != "$version" ]]; then
  echo "debian/changelog upstream version mismatch: expected $version, got $debian_upstream_version from $debian_version" >&2
  exit 1
fi

if [[ ! "$debian_version" =~ ^${version}-[0-9]+$ ]]; then
  echo "debian/changelog version must be ${version}-N, got: $debian_version" >&2
  exit 1
fi

if ! grep -Eq "^## \\[$version\\]([[:space:]]|$)" CHANGELOG.md; then
  echo "CHANGELOG.md is missing a ## [$version] release section." >&2
  exit 1
fi

metainfo_release="$(
  sed -nE 's/.*<release version="([^"]+)".*/\1/p' data/io.github.vicliu624.lofibox.metainfo.xml | head -n 1
)"
if [[ "$metainfo_release" != "$version" ]]; then
  echo "AppStream top release mismatch: expected $version, got ${metainfo_release:-<none>}" >&2
  exit 1
fi

if ! grep -Eq 'github\.com/vicliu624/LoFiBox-Zero/tags' debian/watch; then
  echo "debian/watch must point at the LoFiBox-Zero GitHub tags page." >&2
  exit 1
fi

if ! grep -Eq 'v\?\(\\d\\S\*\)' debian/watch; then
  echo "debian/watch must accept v-prefixed upstream release tags." >&2
  exit 1
fi

echo "Release metadata validated for $version."
