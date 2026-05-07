<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Debian And Ubuntu Official Archive Specification

## 1. Purpose

This document makes official Debian and Ubuntu archive readiness an engineering constraint for `LoFiBox Zero`.

The product goal is not a third-party `.deb` download.
The long-term installation target is:

```sh
sudo apt install lofibox
```

Before official archive acceptance, LoFiBox may provide a temporary signed APT
repository hosted through GitHub Pages. That preview repository is governed by
`github-pages-apt-repository-spec.md` and must remain compatible with eventual
official archive migration.

This constraint must shape source layout, build behavior, dependency handling, runtime paths, tests, licensing, and packaging.
It must not be used as a reason to delete product capabilities.

## 2. Authority

For product meaning, use `lofibox-zero-final-product-spec.md`.
For code ownership and layering, use `project-architecture-spec.md`.
For dependency admission, use `dependency-policy-spec.md`.
For version control and version propagation, use `lofibox-zero-version-control-spec.md`.
For Linux desktop files and runtime integration, use `linux-desktop-integration-spec.md`.
For copyright and resource governance, use `copyright-resource-governance-spec.md`.
For tests and CI, use `testing-ci-spec.md`.
For the temporary pre-archive GitHub Pages APT repository, use `github-pages-apt-repository-spec.md`.

This document controls distribution-facing engineering rules.

## 3. Distribution Invariants

- `LoFiBox Zero` is a pure C++ Linux desktop music player.
- The build system is CMake.
- The project must support a normal source build:
  - `cmake -B build -S .`
  - `cmake --build build`
  - `cmake --install build --prefix /usr`
- The install target must install files into distribution-standard locations.
- Build-time network access is forbidden.
- Build-time `git clone`, `curl`, `wget`, `pip install`, `npm install`, `cargo fetch`, `go mod download`, or equivalent dependency fetching is forbidden.
- Dependencies must come from system packages, Debian `Build-Depends`, or explicitly governed source dependencies.
- Clean chroot builds must be a supported target.
- Builds should be reproducible and must not intentionally embed local usernames, local source paths, random IDs, or current wall-clock time into binaries.

## 4. Required Installed Paths

The installed package should converge on:

- `/usr/bin/lofibox`
- `/usr/lib/lofibox/`
- `/usr/lib/lofibox/plugins/`
- `/usr/share/lofibox/`
- `/usr/share/applications/io.github.vicliu624.lofibox.desktop`
- `/usr/share/metainfo/io.github.vicliu624.lofibox.metainfo.xml`
- `/usr/share/icons/hicolor/scalable/apps/io.github.vicliu624.lofibox.svg`
- `/usr/share/doc/lofibox/`

The project must not install or write runtime data into:

- `/opt/lofibox`
- `/usr/local`
- the current working directory
- `/usr/share/lofibox` as runtime cache
- `/usr/lib/lofibox` as user configuration

## 5. Required Debian Directory

The repository should contain a reviewable initial `debian/` directory:

- `debian/changelog`
- `debian/control`
- `debian/copyright`
- `debian/rules`
- `debian/install`
- `debian/watch`
- `debian/source/format`
- `debian/tests/control`
- `debian/tests/smoke`

`debian/source/format` must be:

```text
3.0 (quilt)
```

`debian/rules` should prefer a simple `dh` plus CMake form unless a documented packaging need requires otherwise:

```make
#!/usr/bin/make -f

%:
	dh $@ --buildsystem=cmake
```

`debian/install` should rely on the upstream install target as much as possible.
It must not become a second hand-maintained installation system.

## 6. Debian Control Requirements

`debian/control` must accurately declare:

- `Source`
- `Section: sound`
- `Priority: optional`
- `Maintainer`
- `Build-Depends`
- `Standards-Version`
- `Homepage`
- `Vcs-Browser`
- `Vcs-Git`
- `Rules-Requires-Root: no`
- binary `Package`
- `Architecture`
- `Depends`
- `Description`

The exact Debian Policy `Standards-Version` must be checked and updated at packaging time rather than guessed in product code.

## 7. Debian Review Posture

Sponsor review must be able to answer:

- Can it build without network access?
- Are dependencies declared in `Build-Depends` and runtime `Depends`?
- Are large archive-available libraries not vendored?
- Are licenses clear for code, assets, fixtures, screenshots, and documentation?
- Are user data and cache paths XDG-compliant?
- Are desktop and AppStream files valid?
- Are tests runnable without real user music, real network services, real audio hardware, or external internet?

## 8. AI Constraints

- Do not solve archive difficulty by deleting feature domains.
- Do not hide dependencies behind build scripts.
- Do not add build-time download steps.
- Do not hand-copy installed files around an absent CMake install target.
- Do not confuse “can generate a `.deb` locally” with “acceptable for official archive review”.
