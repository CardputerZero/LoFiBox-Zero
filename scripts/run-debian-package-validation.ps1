# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Image = "lofibox-zero/package-build:trixie",
    [string]$BuildArgs = "-us -uc",
    [switch]$SkipOrigTarball,
    [switch]$AllowNetwork
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$parent = Split-Path $repo
$changelogHeader = Get-Content -Path (Join-Path $repo "debian\changelog") -TotalCount 1
if ($changelogHeader -notmatch '^\S+\s+\(([^)]+)\)') {
    throw "Could not parse Debian package version from debian/changelog"
}
$packageVersion = $Matches[1]
$packageArch = "amd64"
$changesName = "lofibox_${packageVersion}_${packageArch}.changes"
$changes = Join-Path $parent $changesName

if (-not $SkipOrigTarball) {
    & (Join-Path $PSScriptRoot "create-orig-tarball.ps1")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& (Join-Path $PSScriptRoot "run-dpkg-buildpackage.ps1") -Image $Image -BuildArgs $BuildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $changes)) {
    throw "Expected Debian changes file was not produced: $changes"
}

docker image inspect $Image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Required local Docker image '$Image' was not found. Run scripts/build-debian-package-image.ps1 first."
}

$networkArgs = @()
if (-not $AllowNetwork) {
    $networkArgs = @("--network", "none")
}

& docker run --rm `
    @networkArgs `
    -v "${parent}:/workspace" `
    -w /workspace `
    $Image `
    bash -lc "set -euxo pipefail; lintian '$changesName'; autopkgtest '$changesName' -- null"

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
