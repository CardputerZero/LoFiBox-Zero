<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Packaging

Normative archive rules live in `docs/specification/debian-official-archive-spec.md`.
The temporary pre-archive GitHub Pages APT repository is governed by
`docs/specification/github-pages-apt-repository-spec.md`.

## Standard Build

```sh
cmake -B build -S .
cmake --build build
cmake --install build --prefix /usr
```

## Debian Source Package Target

The repository should provide:

- `debian/changelog`
- `debian/control`
- `debian/copyright`
- `debian/rules`
- `debian/install`
- `debian/watch`
- `debian/source/format`
- `debian/tests/control`
- `debian/tests/smoke`

`debian/source/format` must be `3.0 (quilt)`.

## Local Debian Build Dependency Check

The repository provides a no-pull helper for checking declared Debian build dependencies against an already available local image:

```powershell
scripts/check-debian-builddeps.ps1
```

The helper intentionally refuses to build or pull images. If it fails, refresh or replace the local image explicitly before attempting `dpkg-buildpackage`.

## Local Debian Package Build Image

The package build image is intentionally separate from runtime and device images:

```powershell
scripts/build-debian-package-image.ps1
scripts/run-dpkg-buildpackage.ps1
```

`scripts/build-debian-package-image.ps1` requires the local base image `lofibox-zero/dev-container:trixie` to exist first. It may update apt metadata and install package-build tooling inside the derived image. `scripts/run-dpkg-buildpackage.ps1` does not install packages; it only validates declared build dependencies and runs `dpkg-buildpackage -us -uc -b` by default.

## Local Source Package Flow

Before an upstream release tarball exists, a local source-package check can create the `3.0 (quilt)` upstream tarball from the current working tree:

```powershell
scripts/create-orig-tarball.ps1
scripts/run-dpkg-buildpackage.ps1 -BuildArgs "-us -uc"
```

The generated archive is written next to the repository as `lofibox_0.2.0.orig.tar.xz`. It excludes `debian/`, VCS metadata, build directories, and generated package artifacts. This is a local validation helper; actual archive uploads should use release-tag tarballs tracked by `debian/watch`.

`debian/source/options` also ignores local build/debug directories such as `build/`, `.tmp/`, `out/`, `runs/`, and `obj-*` so source package checks are not polluted by generated debug output or temporary media-debug artifacts. These directories are not project source and must not be represented in Debian source diffs.

## Local Package Validation Gate

Local package validation should use the same package-level gates as CI:

```powershell
scripts/run-debian-package-validation.ps1
```

The helper creates the local upstream tarball, builds the Debian package through the dedicated package-build image, runs `lintian`, and runs `autopkgtest` against the generated `.changes` file. It intentionally depends on a pre-existing local image and does not pull or build images implicitly. By default the final `lintian`/`autopkgtest` container is run with Docker networking disabled so routine validation proves the prepared package-build image is self-contained; pass `-AllowNetwork` only when intentionally refreshing or diagnosing the image.

## Preview APT Repository

Before LoFiBox is accepted into the official Debian archive, early users may
install from the LoFiBox-maintained preview APT repository published through
GitHub Pages.

User setup:

```sh
sudo install -d -m 0755 /etc/apt/keyrings

curl -fsSL https://vicliu624.github.io/lofibox-apt/lofibox-archive-keyring.pgp \
  | sudo tee /etc/apt/keyrings/lofibox-archive-keyring.pgp >/dev/null

sudo chmod 0644 /etc/apt/keyrings/lofibox-archive-keyring.pgp

sudo tee /etc/apt/sources.list.d/lofibox.sources >/dev/null <<'EOF'
Types: deb
URIs: https://vicliu624.github.io/lofibox-apt/debian
Suites: trixie
Components: main
Signed-By: /etc/apt/keyrings/lofibox-archive-keyring.pgp
EOF

sudo apt update
sudo apt install lofibox
```

The repository must be signed and consumed through `Signed-By`. Do not use
`apt-key`.

Preview package versions should use a local suffix such as:

```text
0.1.0-1~lofibox1
```

This keeps future official Debian versions such as `0.1.0-1` able to supersede
the preview package naturally.

Publication is manual through:

```text
lofibox-apt/.github/workflows/publish.yml
```

The workflow requires protected GitHub secrets:

```text
LOFIBOX_APT_GPG_PRIVATE_KEY
LOFIBOX_APT_GPG_KEY_ID
```

Local repository generation from existing `.changes` files uses:

```sh
scripts/build-github-pages-apt-repository.sh \
  --suite trixie \
  --component main \
  --architectures amd64,arm64,armhf \
  --output public \
  --gpg-key "$LOFIBOX_APT_GPG_KEY_ID" \
  --changes ../lofibox_0.2.0-1~lofibox1_amd64.changes \
  --changes ../lofibox_0.2.0-1~lofibox1_arm64.changes \
  --changes ../lofibox_0.2.0-1~lofibox1_armhf.changes
```

The GitHub-hosted preview workflow builds `amd64`, cross-builds `arm64`, and
cross-builds Raspberry Pi ARMv6 hard-float `armhf`. Packaging must keep
build-machine tools and target libraries separate: build tools such as
`pkgconf` are native build dependencies, while target development libraries are
resolved for the selected host architecture. `debian/rules` must select
host-triplet compilers whenever `DEB_BUILD_GNU_TYPE` differs from
`DEB_HOST_GNU_TYPE`. GitHub-hosted cross-build jobs may use
`DEB_BUILD_OPTIONS=nocheck` because they cannot execute target architecture test
binaries on the x86_64 runner. The cross-build environment must also install
target runtime libraries needed by `dh_shlibdeps`, including target
`libstdc++6`. The Raspberry Pi CM0 `armhf` package is built in ARM mode with
ARM1176JZF-S/VFP hard-float flags; do not use plain `-march=armv6` for that
publisher.

## Autopkgtest

The package smoke test is intentionally limited to installed runtime behavior and desktop metadata, but it is not marked `superficial` because it is the package-level gate for CLI availability and desktop/AppStream metadata validity:

```sh
autopkgtest lofibox_0.2.0-1_amd64.changes -- null
```

The local Docker-based package-build image excludes `/usr/share/man/*` through dpkg path-exclude rules, so autopkgtest must not assert manpage installation from the testbed filesystem. Manpage presence is validated through package contents and `lintian`.

## Validation Commands

Packaging work should support:

- `lofibox --help`
- `lofibox --version`
- `lofibox-tui --help`
- `dpkg-checkbuilddeps debian/control`
- `desktop-file-validate`
- `appstreamcli validate`
- `lintian`
- `autopkgtest`
- `sbuild` or `pbuilder`
- install/uninstall smoke tests

## Install Paths

The package should install into standard paths:

- `/usr/bin/lofibox`
- `/usr/bin/lofibox-tui`
- `/usr/lib/lofibox/`
- `/usr/lib/lofibox/tag_writer.py`
- `/usr/lib/lofibox/remote_media_tool.py`
- `/usr/lib/lofibox/plugins/`
- `/usr/share/lofibox/`
- `/usr/share/applications/io.github.vicliu624.lofibox.desktop`
- `/usr/share/metainfo/io.github.vicliu624.lofibox.metainfo.xml`
- `/usr/share/icons/hicolor/scalable/apps/io.github.vicliu624.lofibox.svg`
- `/usr/share/man/man1/lofibox.1`
- `/usr/share/doc/lofibox/`

## Runtime Paths

Installed package paths are not writable application state. Runtime data must follow XDG conventions:

- config: `$XDG_CONFIG_HOME/lofibox` or `~/.config/lofibox`
- data: `$XDG_DATA_HOME/lofibox` or `~/.local/share/lofibox`
- cache: `$XDG_CACHE_HOME/lofibox` or `~/.cache/lofibox`
- state: `$XDG_STATE_HOME/lofibox` or `~/.local/state/lofibox`
- runtime: `$XDG_RUNTIME_DIR/lofibox` for ephemeral locks where available

Package tests must not rely on the user's real `HOME`, real music library, real audio device, or external network.
