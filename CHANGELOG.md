<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Changelog

All notable changes to LoFiBox Zero will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] — 2026-05-05

### Added
- **Realtime audio remix effects (Radio, Tape, Vinyl)** — creative sound-color processors in the DSP chain.
  - **Effect registry** (`src/audio/dsp/audio_effect_registry.h/.cpp`) — plugin-driven descriptor model with `plugin_id`, `effect_id`, `name`, `description`, `default_intensity`. Registry functions: `builtinAudioEffects()`, `audioEffectById()`, `audioEffectsForPlugin()`, `cycleAudioEffectId()`, `audioEffectName()`.
  - **Built-in plugin** (`data/plugins/builtin-remix/plugin.json`) — `io.github.vicliu624.lofibox.effect.remix` with capabilities `audio.effect` / `audio.effect.realtime`, three nodes: `remix.radio`, `remix.tape`, `remix.vinyl`.
  - **Radio effect** — narrow-band AM broadcast simulation: 285Hz–3.6kHz band-pass, 1.15kHz/+4.6dB and 2.45kHz/+2.0dB midrange peaks, 5.7Hz sinusoidal AM flutter (±3.5%) with random ionospheric jitter, ±0.0038 receiver noise, stereo-to-mono narrowing (34% stereo / 66% mono), tanh saturation (drive 1.75), 0.85 wet mix.
  - **Tape effect** — worn cassette simulation: 38Hz–9.8kHz band-pass, 180Hz/+2.4dB low-end warmth bump, 4.3kHz/-2.0dB head-gap roll-off, 0.36Hz wow (±1.1%) + 6.4Hz flutter (±0.4%), ±0.0014 tape hiss, tanh saturation (drive 1.42), 0.96 wet mix.
  - **Vinyl effect** — turntable simulation: 46Hz–12.8kHz band-pass, 120Hz/+1.4dB low-end compensation, 5.2kHz/+0.9dB cartridge resonance, 0.36Hz wow (±0.25%) only, ±0.0022 surface noise, dust ticks (~14/sec, exponential decay×0.88), scratches (~2/sec, decay×0.985), tanh saturation (drive 1.22), 0.92 wet mix.
  - **Shared DSP infrastructure** — 4-stage biquad cascade per channel, xorshift32 PRNG, three phase accumulators, `tanh` soft-saturation with gain normalization, per-channel biquad isolation, stereo noise spread (±6% L/R bias).
  - **Effect switching** — hot-update model (no track restart), full state reset on switch (biquad memories, phase accumulators, PRNG, dust/scratch accumulators).
  - **Default intensity per effect** — Radio 0.85, Tape 1.0, Vinyl 1.0. Applied automatically from descriptor on selection.
- **`AudioEffectProfile` on `DspChainProfile`** (`src/audio/dsp/dsp_chain.h`) — new `effect` slot with `plugin_id`, `effect_id`, `name`, `intensity`.
- **`AudioEffectCycle` runtime command** — cycles OFF → Radio → Tape → Vinyl → OFF through the full runtime command bus, serialized in `runtime_envelope_serializer.cpp`, tracked in `RuntimeSnapshot`/`RuntimeEvent`.
- **Effect state** — `EqRuntimeState` gains `effect_plugin_id`, `effect_id`, `effect_intensity`. `EqRuntimeSnapshot` gains `effect_name`, `effect_enabled`.
- **UI surfaces for remix**: GUI (`R` key + Equalizer/Now Playing display), TUI (`R` key, `r` stays reconnect), WebUI (REMIX badge + Settings CYCLE button + `R`/`r` key), CLI (`lofibox remix`).
- **Remix specification** (`docs/specification/lofibox-zero-audio-dsp-spec.md` Section 11) — full spec covering registry model, three effect parameters, shared infrastructure, switching behavior, UI contract, data model, and AI constraints.
- **WebUI remote-control surface** — HTTP/WebSocket server providing browser-based remote control.
  - Single-file SPA frontend (`assets/webui/index.html`) with embedded CSS and JavaScript — no build step, no external dependencies.
  - Seven tabs: Now Playing, Queue, Library, Sources, EQ, Settings, Diagnostics.
  - Real-time updates via WebSocket (`/api/runtime/events`) with HTTP polling fallback (`/api/runtime/snapshot` every 5s).
  - Command dispatch via `POST /api/runtime/commands`.
  - Warm amber-on-black visual theme matching the LoFiBox identity.
  - Keyboard shortcuts: Space (toggle play/pause), ArrowLeft (previous), ArrowRight (next).
  - Auto-reconnect on WebSocket disconnect with connection status overlay.
  - Spectrum visualization via HTML5 Canvas with amber gradient.
  - 10-band EQ with vertical range sliders (31Hz–16kHz).
  - Responsive layout with mobile breakpoint at 500px.
- **Zero-dependency JSON layer** (`src/webui/webui_json.h/.cpp`) — `ostringstream`-based JSON building and HTTP response formatting.
- **Runtime adapter** (`src/webui/webui_runtime_adapter.h/.cpp`) — sole contact point with `RuntimeCommandClient`, handles snapshot query, command dispatch, and event diffing via `runtimeEventsBetween()`.
- **Snapshot projection** (`src/webui/webui_projection.h/.cpp`) — 9 JSON DTO builders mapping `RuntimeSnapshot` sections and `RuntimeEvent` to structured JSON.
- **POSIX socket HTTP/WebSocket server** (`src/webui/webui_server.h/.cpp`) — background-thread accept loop with per-connection routing (HTTP dispatch or WebSocket upgrade).
- **RFC 6455 WebSocket implementation** (`src/webui/webui_ws_runtime_stream.h/.cpp`) — upgrade handshake with inline SHA-1 + Base64, text frame sending, close frame shutdown. Zero external crypto dependencies.
- **CLI and environment configuration** (`src/webui/webui_config.h/.cpp`) — `--webui`, `--webui-bind <addr>`, `--webui-port <port>` flags and `LOFIBOX_WEBUI`, `LOFIBOX_WEBUI_BIND`, `LOFIBOX_WEBUI_PORT` environment variables.
- **`LOFIBOX_BUILD_WEBUI` CMake option** (default OFF) — conditionally builds `lofibox_webui` library and links into X11 and Device targets.
- **Smoke test suite** (`tests/webui_smoke.cpp`) — 8 test groups covering config parsing, JSON helpers, snapshot/event projection, command parsing (10 action mappings), static asset serving, runtime adapter integration, and HTTP router responses. Uses a `FakeRuntimeCommandClient` for zero-dependency testing.
- **Design specification** (`docs/webui-design-spec.md`) — 13-section comprehensive spec covering positioning, dependency boundary, pages, API design, frontend architecture, WebSocket protocol details, command mapping, source layout, deployment, CMake integration, tests, and CI architecture enforcement rules.

### Changed
- **DSP limiter disabled by default** — `EqProfile::limiter_enabled` and `LimiterProfile::enabled` now default to `false`. Previous default of `enabled=true` with `ceiling_db=-1.0` caused hard clipping at 0.891 linear for all playback.
- **Hard-clamp limiter removed** — only final ±1.0 safety clamp remains. `ClipStats` diagnostics added (`over_ceiling_count`, `over_fullscale_count`, `peak_before`, `peak_after`) with ~5s periodic logging.
- **Library scanning made asynchronous** — background scan thread with progress callbacks; boot page now shows per-phase progress (SCANNING FILES / READING METADATA / BUILDING INDEXES) with file counts, current path, and a progress bar.
- **`-re` flag removed for local playback** — ffmpeg decoder no longer constrains decode speed for local files. Retained for network streams only. Improves buffer fill and reduces underrun risk.
- **GUI artwork fixed for remote tracks** — `PlaybackController::refreshArtwork()` now routes remote tracks through `readRemoteIdentity()` with enrichment cache keys and artwork URL fallback, matching WebUI behavior.
- **9 smoke tests updated** for async scanning model (loop-until-ready). `app_lifecycle_smoke.cpp` rewritten with 4 test scenarios including multi-tick Loading polling.
- `LoFiBoxApp` constructor accepts optional `WebUiConfig` parameter (gated by `LOFIBOX_HAVE_WEBUI`).
- `runLoFiBoxApp()` signature extended with optional `WebUiConfig` parameter.
- X11 and Device target mains parse `--webui*` CLI flags and pass config to the app runner.
- **Version management overhaul** — single source of truth enforced across the entire project:
  - `LOFIBOX_VERSION` compile definition moved from `lofibox_zero_target_cli` only to `lofibox_zero_core` (PUBLIC), ensuring all libraries and executables inherit the version macro.
  - `app_projection_builder.cpp`: replaced hardcoded `"0.1.0"` with `LOFIBOX_VERSION` preprocessor macro (fallback `"unknown"`), aligning GUI About page with CMake version.
  - `jellyfin_provider.py`: replaced hardcoded `"0.1.0"` User-Agent with import from CMake-generated `version.py` module (fallback `"0.0.0-dev"` for development builds).
  - `cmake/version.py.in` template added — `configure_file()` injects `PROJECT_VERSION` at build time; installed to `${LOFIBOX_PRIVATE_LIBDIR}` for Python helpers.
  - `debian/changelog` bumped to `0.2.0-1`.
  - `data/io.github.vicliu624.lofibox.metainfo.xml` added `0.2.0` release entry for AppStream metadata.
- `docs/webui-design-spec.md` added Section 14: comprehensive version control design documentation covering single source of truth, CMake→C++ and CMake→Python propagation, all consumer mappings, app version vs snapshot version distinction, version bump procedure, and Debian compliance requirements.
- `docs/specification/lofibox-zero-version-control-spec.md` created — standalone version control specification covering all 10 sections (purpose, single source of truth, CMake→C++ propagation, CMake→Python propagation, consumer registry, app vs snapshot version distinction, bump procedure, Debian compliance, forbidden patterns, AI constraints). Cross-referenced from `project-architecture-spec.md` and `debian-official-archive-spec.md`.

## [0.1.0] — Initial Release

### Added
- Core runtime architecture: `RuntimeCommandClient` → `RuntimeCommandServer` → `RuntimeCommandBus` → `RuntimeSessionFacade`.
- Playback, Queue, EQ, Remote Session, Settings, Library, Sources, Diagnostics, Creator, Lyrics, Visualization runtime domains.
- `RuntimeSnapshot` — flat snapshot struct aggregating all 11 runtime sub-sections with version tracking.
- `RuntimeEvent` — 16 event kinds with `runtimeEventsBetween()` diff-based generation.
- `RuntimeCommand` — 29 command kinds with type-safe variant payload (15 payload types).
- `InProcessRuntimeCommandClient` — same-process runtime client.
- `UnixSocketRuntimeTransport` — external runtime command/query/event transport over Unix domain sockets.
- Inline JSON serializer/parser (`runtime_envelope_serializer.cpp`) — zero external JSON dependencies.
- Three build targets: Linux framebuffer device (`lofibox_zero_device`), X11 VNC/PocketFrame (`lofibox_zero_x11`), ANSI terminal UI (`lofibox_zero_tui`).
- TUI: 13 pages (Dashboard, Now Playing, Lyrics, Spectrum, Queue, Library, Sources, EQ, DSP, Diagnostics, Creator, Help, Command Palette), widget system, layout engine, input router.
- GUI: 320×170 fixed-pixel canvas, bitmap font rendering, 23 `AppPage` states, multi-page UI with list navigation.
- Audio pipeline: decoder contract, DSP chain with real-time engine, host audio playback backend.
- Library: scanner, indexer, store, governance, metadata enrichment, search.
- Remote media: provider contract, source registry, catalog model, streaming playback, Emby/Jellyfin integration.
- Cache manager, credentials policy, single-instance lock, XDG path support.
- Plugin manifest system, playlist parser.
- Desktop integration boundary.
- 60+ smoke tests covering runtime, app, TUI, library, playback, DSP, metadata, remote media, and platform layers.
- GPL-3.0-or-later licensing.
