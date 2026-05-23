# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Image = "lofibox-zero/dev-container:trixie"
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script = Join-Path $repo ".tmp\validate-packaging-skeleton.sh"

New-Item -ItemType Directory -Force -Path (Split-Path $script) | Out-Null

docker image inspect $Image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Required local Docker image '$Image' was not found. Build or import it explicitly before running this script."
}

$content = @'
set -euxo pipefail

if command -v desktop-file-validate >/dev/null 2>&1; then
  desktop-file-validate data/io.github.vicliu624.lofibox.desktop
else
  echo "desktop-file-validate not found in image; skipping desktop metadata validation" >&2
fi

if command -v appstreamcli >/dev/null 2>&1; then
  appstreamcli validate --no-net data/io.github.vicliu624.lofibox.metainfo.xml
else
  echo "appstreamcli not found in image; skipping AppStream validation" >&2
fi

cmake -S . -B build/container-install-skeleton -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLOFIBOX_BUILD_DEVICE=OFF \
  -DLOFIBOX_BUILD_X11=OFF \
  -DLOFIBOX_BUILD_WAYLAND=OFF \
  -DBUILD_TESTING=OFF

cmake --build build/container-install-skeleton --target lofibox_zero_tui_bin

rm -rf .tmp/container-install-skeleton
cmake --install build/container-install-skeleton --prefix /workspace/LoFiBox-Zero/.tmp/container-install-skeleton

test -x .tmp/container-install-skeleton/bin/lofibox-tui
test -f .tmp/container-install-skeleton/share/applications/io.github.vicliu624.lofibox.desktop
test -f .tmp/container-install-skeleton/share/metainfo/io.github.vicliu624.lofibox.metainfo.xml
test -f .tmp/container-install-skeleton/share/icons/hicolor/scalable/apps/io.github.vicliu624.lofibox.svg
test -f .tmp/container-install-skeleton/share/icons/hicolor/180x180/apps/io.github.vicliu624.lofibox.png
test -f .tmp/container-install-skeleton/share/mime/packages/io.github.vicliu624.lofibox.xml
test -f .tmp/container-install-skeleton/share/man/man1/lofibox.1
test -f .tmp/container-install-skeleton/share/doc/lofibox/architecture.md
'@

$content = $content -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($script, $content, [System.Text.UTF8Encoding]::new($false))

docker run --rm `
    -v "${repo}:/workspace/LoFiBox-Zero" `
    -w /workspace/LoFiBox-Zero `
    $Image `
    bash /workspace/LoFiBox-Zero/.tmp/validate-packaging-skeleton.sh
