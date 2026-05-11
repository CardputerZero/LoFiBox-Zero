# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Image = "lofibox-zero/package-build:trixie",
    [string]$BuildArgs = "-us -uc -b"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$parent = Split-Path $repo
$script = Join-Path $repo ".tmp\run-dpkg-buildpackage.sh"

New-Item -ItemType Directory -Force -Path (Split-Path $script) | Out-Null

docker image inspect $Image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Required local Docker image '$Image' was not found. Run scripts/build-debian-package-image.ps1 first."
}

$content = @'
set -euxo pipefail

build_args="$*"
workspace="/tmp/lofibox-package-work"
source_dir="$workspace/LoFiBox-Zero"

rm -rf "$workspace"
mkdir -p "$source_dir"

tar \
  --exclude='./.git' \
  --exclude='./.cache' \
  --exclude='./.tmp' \
  --exclude='./build' \
  --exclude='./out' \
  --exclude='./obj-*' \
  --exclude='./runs' \
  --exclude='./debian/.debhelper' \
  --exclude='./debian/debhelper-build-stamp' \
  --exclude='./debian/files' \
  --exclude='./debian/lofibox' \
  --exclude='./*.deb' \
  --exclude='./*.buildinfo' \
  --exclude='./*.changes' \
  --exclude='./*.dsc' \
  --exclude='./*.debian.tar.*' \
  -cf - . | tar -xf - -C "$source_dir"

package_version="$(dpkg-parsechangelog -l "$source_dir/debian/changelog" -S Version)"
upstream_version="${package_version%%-*}"
if [ -f "/workspace/lofibox_${upstream_version}.orig.tar.xz" ]; then
  cp "/workspace/lofibox_${upstream_version}.orig.tar.xz" "$workspace/"
fi

cd "$source_dir"
find . -type d -exec chmod 0755 {} +
find . -type f -exec chmod 0644 {} +
chmod 0755 debian/rules
if [ -f debian/tests/smoke ]; then
  chmod 0755 debian/tests/smoke
fi

dpkg-checkbuilddeps debian/control
dpkg-buildpackage $build_args

cp "$workspace"/lofibox_* /workspace/
if compgen -G "$workspace/lofibox-dbgsym_*" >/dev/null; then
  cp "$workspace"/lofibox-dbgsym_* /workspace/
fi
'@

$content = $content -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($script, $content, [System.Text.UTF8Encoding]::new($false))

docker run --rm `
    -v "${parent}:/workspace" `
    -w /workspace/LoFiBox-Zero `
    $Image `
    bash /workspace/LoFiBox-Zero/.tmp/run-dpkg-buildpackage.sh $BuildArgs

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
