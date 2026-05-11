# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Image = "lofibox-zero/package-build:trixie",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$parent = Split-Path $repo

$cmakeText = Get-Content -Path (Join-Path $repo "CMakeLists.txt") -Raw
if ($cmakeText -notmatch 'project\s*\(\s*LoFiBoxZero\s+VERSION\s+([0-9]+(?:\.[0-9]+){2})\s+LANGUAGES') {
    throw "Could not parse project version from CMakeLists.txt"
}
$projectVersion = $Matches[1]

$changelogHeader = Get-Content -Path (Join-Path $repo "debian\changelog") -TotalCount 1
if ($changelogHeader -notmatch '^\S+\s+\(([^)]+)\)') {
    throw "Could not parse Debian package version from debian/changelog"
}
$packageVersion = $Matches[1]
$upstreamVersion = ($packageVersion -split '-', 2)[0]
if ($upstreamVersion -ne $projectVersion) {
    throw "CMake project version '$projectVersion' does not match Debian upstream version '$upstreamVersion'"
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = $projectVersion
}
if ($Version -ne $projectVersion) {
    throw "Requested orig tarball version '$Version' does not match CMake project version '$projectVersion'"
}

$archive = Join-Path $parent "lofibox_$Version.orig.tar.xz"

docker image inspect $Image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Required local Docker image '$Image' was not found. Run scripts/build-debian-package-image.ps1 first."
}

if (Test-Path $archive) {
    Remove-Item -LiteralPath $archive -Force
}

$script = Join-Path $repo ".tmp\create-orig-tarball.sh"
New-Item -ItemType Directory -Force -Path (Split-Path $script) | Out-Null

$content = @'
set -euxo pipefail

version="$1"
archive="/workspace/lofibox_${version}.orig.tar.xz"
prefix="lofibox-${version}"

tar \
  --exclude='./.git' \
  --exclude='./.cache' \
  --exclude='./.tmp' \
  --exclude='./build' \
  --exclude='./out' \
  --exclude='./obj-*' \
  --exclude='./runs' \
  --exclude='./debian' \
  --exclude='./*.deb' \
  --exclude='./*.buildinfo' \
  --exclude='./*.changes' \
  --exclude='./*.dsc' \
  --exclude='./*.debian.tar.*' \
  --transform "s#^\./#${prefix}/#" \
  -cJf "$archive" .

manifest="$(mktemp)"
tar -tf "$archive" > "$manifest"
grep -q "^${prefix}/CMakeLists.txt$" "$manifest"
grep -q "^${prefix}/src/" "$manifest"
if grep -q "^${prefix}/debian/" "$manifest"; then
  echo "orig tarball must not contain debian/" >&2
  exit 1
fi
'@

$content = $content -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($script, $content, [System.Text.UTF8Encoding]::new($false))

docker run --rm `
    -v "${parent}:/workspace" `
    -w /workspace/LoFiBox-Zero `
    $Image `
    bash /workspace/LoFiBox-Zero/.tmp/create-orig-tarball.sh $Version

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
