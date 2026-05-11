#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/extract-release-notes.sh <upstream-version> <output-file>

Extract the matching Keep a Changelog section from CHANGELOG.md for use as
GitHub Release notes.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

version="${1:-}"
output_file="${2:-}"
if [[ -z "$version" || -z "$output_file" ]]; then
  usage >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

tmp_file="$(mktemp)"
awk -v version="$version" '
  index($0, "## [" version "]") == 1 {
    found = 1
    next
  }
  found && /^## \[/ {
    exit
  }
  found {
    print
  }
' CHANGELOG.md > "$tmp_file"

if [[ ! -s "$tmp_file" ]]; then
  echo "No release notes found for $version in CHANGELOG.md." >&2
  rm -f "$tmp_file"
  exit 1
fi

{
  printf 'Source release for LoFiBox Zero %s.\n\n' "$version"
  sed '/./,$!d' "$tmp_file"
} > "$output_file"

rm -f "$tmp_file"
echo "Release notes written to $output_file."
