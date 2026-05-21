<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero

<p align="center">
  <img src="docs/assets/logo.png" alt="LoFiBox Zero logo" width="120">
</p>

LoFiBox Zero is a Linux-first music player for compact keyboard-driven devices and chromeless desktop-widget sessions. It presents the same focused `320x170` player surface on the Cardputer Zero class of hardware, the Linux framebuffer device target, and the direct Linux X11 target.

It is built around local library playback, durable media roots, metadata and artwork enrichment, lyrics, queue control, remote media sources, and a DSP-ready audio pipeline. The goal is a small music appliance that feels deliberate rather than a resized desktop app.

## Player Surface

| Now Playing | Library Menus |
|:--:|:--:|
| <img src="docs/assets/playing.png" alt="LoFiBox Zero now-playing page with album art, transport state, progress, and spectrum visualization" width="420"> | <img src="docs/assets/menus.png" alt="LoFiBox Zero menu and library navigation pages on the compact player surface" width="420"> |
| Lyrics | Equalizer |
| <img src="docs/assets/lyric.png" alt="LoFiBox Zero lyrics page with timed lyric focus and playback context" width="420"> | <img src="docs/assets/EQ.png" alt="LoFiBox Zero equalizer page with band controls and spectrum visualization" width="420"> |

The screenshots show the product surface, not a mock desktop simulator. LoFiBox keeps one runtime path and lets the target adapters provide Linux presentation, input, audio, filesystem, and service integration.

## Highlights

- Compact chromeless player UI for a `320x170` logical screen
- Local library indexing from durable configured media roots, defaulting to `~/Music`
- Queue, search, lyrics, artwork, metadata enrichment, and tag writeback services
- Remote media profiles for Jellyfin, Emby, OpenSubsonic/Navidrome-style servers, playlists, WebDAV, DLNA/UPnP, and other governed source families
- Runtime command surface for live playback, queue, EQ, diagnostics, and library refresh
- Direct CLI, TUI, Linux X11, and Linux framebuffer/device targets sharing the same app semantics
- Debian-oriented build, packaging, AppStream, desktop integration, and release discipline

The repository intentionally keeps one product runtime path:

- shared product code in `src/app` and `src/core`
- Linux runtime adapters in `src/platform/host` and `src/platform/device`
- a single Linux device executable: `lofibox_zero_device`
- a direct Linux X11 desktop-widget executable: `lofibox_zero_x11`
- a containerized Linux build environment for repeatable builds

There is no SDL desktop simulator in this project. The app should be validated through the Linux device target, a real framebuffer/input environment, or a container wired to those Linux devices.

## Repository Layout

- `src/app`: product state, pages, controllers, playback/library semantics
- `src/core`: canvas, font, display primitives, platform-neutral utilities
- `src/platform/host`: host services such as metadata, artwork, lyrics, audio process launch, logging, caching, and single-instance lock
- `src/platform/device`: Linux framebuffer and evdev/xkb input adapters
- `src/targets/device_main.cpp`: Linux product entry point
- `assets`: icons, logo, fonts, and other product assets
- `docs/assets`: README and documentation screenshots
- `docker/dev-container.Dockerfile`: container image for Linux builds
- `scripts/run-dev-container.*`: build/run helper for the Linux device target inside the container

## Build In Container

Build the development image:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-dev-container.ps1
```

Build and run the Linux device target from Windows through WSL:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-dev-container.ps1 -MediaRoot C:\Users\VicLi\Music
```

Open an interactive shell instead:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-dev-container.ps1 -Mode shell -MediaRoot C:\Users\VicLi\Music
```

The device executable expects Linux device paths. Override them with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-dev-container.ps1 -MediaRoot C:\Users\VicLi\Music -- --fbdev /dev/fb0 --input-dev /dev/input/event0
```

Equivalent environment variables:

- `LOFIBOX_FBDEV`
- `LOFIBOX_INPUT_DEV`
- `LOFIBOX_MEDIA_ROOT`
- `LOFIBOX_RUNTIME_LOG_PATH`
- `XDG_STATE_HOME`
- `XDG_CACHE_HOME`

## Build Locally On Linux

```bash
cmake -S . -B build/device -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLOFIBOX_BUILD_DEVICE=ON \
  -DBUILD_TESTING=OFF

cmake --build build/device --target lofibox_zero_device
```

Run:

```bash
LOFIBOX_MEDIA_ROOT="$HOME/Music" \
./build/device/lofibox_zero_device --fbdev /dev/fb0 --input-dev /dev/input/event0
```

Build the direct Linux X11 desktop-widget target:

```bash
cmake --preset linux-x11-debug
cmake --build --preset linux-x11-debug-build
```

Run:

```bash
LOFIBOX_MEDIA_ROOT="$HOME/Music" \
./build/x11/lofibox
```

This is a real Linux presentation target using the same app, playback, library, remote-source, metadata, DSP, persistence, and credential semantics as the device target.

## Preview APT Repository

Before LoFiBox is available from the official Debian archive, preview packages
can be published as a signed GitHub Pages APT repository.

```bash
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

The preview repository uses a per-repository keyring and `Signed-By`; do not use
`apt-key`. The official Debian archive remains the long-term target.

## Optional Host Fingerprinting

The development container already installs `libchromaprint-tools`. For direct host runs, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ensure-host-fpcalc.ps1
```

To let the script install Chromaprint/fpcalc through a supported package manager:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ensure-host-fpcalc.ps1 -Install
```

The runtime also honors `FPCALC_PATH` when `fpcalc` is installed outside `PATH`.

## Tests

Shared tests do not require the Linux device adapter:

```powershell
cmake --preset windows-vs2022-test-debug
cmake --build --preset windows-vs2022-debug-build-tests
ctest --test-dir build\test -C Debug --output-on-failure
```

Linux validation:

```bash
bash scripts/wsl-validate.sh
```

## Runtime Boundary

The product boundary is:

```text
Source -> Decode/Playback Services -> DSP/Visualization -> App State -> Canvas -> Linux Presenter
```

The app layer must not know whether it is running in a container, on a Cardputer Zero, or on another Linux device. Those facts belong to platform adapters and startup configuration.

The Linux input truth is the kernel input device stream. Product behavior should be asserted against logical app input events, not against screenshots, product photos, or mock shell geometry.
