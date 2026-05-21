<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Implementation Status

## 1. Purpose

This document describes current progress only.

It is intentionally not a product-definition document.
Use `lofibox-zero-final-product-spec.md` for final product truth.
Use `project-architecture-spec.md` for enduring architecture rules.

## 2. Current User-Visible Scope

- shared `320x170` player UI with menu, library browsing, now-playing, lyrics, EQ, and settings surfaces
- local media-root scan and playable queue behavior
- metadata, artwork, lyrics, and tag writeback runtime services
- track identity runtime service with MusicBrainz fallback and optional Chromaprint/AcoustID fingerprint path
- remote media runtime provider path for Jellyfin, OpenSubsonic/Navidrome-compatible servers, and Emby, including probe, catalog/search/recent lookup, and stream resolution behind app-facing remote service interfaces
- expanded streaming source model for direct URLs, internet radio, playlist manifests, HLS/DASH, SMB, NFS, WebDAV, FTP, SFTP, and DLNA/UPnP provider families, with manifest capabilities and protocol classification
- remote catalog hierarchy model for artists, albums, tracks, genres, playlists, folders, favorites, recently added, recently played, and stations
- streaming playback policy for buffer thresholds, reconnect/retry recovery, next-item and album/playlist prefetch, quality/transcode preference, and stream diagnostics
- final page-model surfaces for Search, Queue/Up Next, Remote Browse, Server Diagnostics, Stream Detail, and Playlist Editor
- implementation placement gate for source ownership: first-batch remote provider behavior lives under `src/remote/*`, playback implementation lives under `src/playback`, audio output/decoder contracts live under `src/audio/*`, and helper entrypoints cannot hide protocol implementation details
- unified `CacheManager` for playback, artwork, metadata, lyrics, remote-directory, station-list, recent-browse, and offline-audio cache buckets, including capacity/age policy, garbage collection, and album/playlist offline sync planning
- final-form DSP domain objects for built-in/user/device/content-specific EQ profiles, preset CRUD/import/export, device/content bindings, loudness, balance, limiter, ReplayGain mode, parametric bands, high-pass/low-pass filters, and smoothing helpers
- media-library governance service for incremental change detection, database migration planning, smart playlists, source-grouped search result explanations, and duplicate/same-recording identity decisions
- metadata governance service for local fingerprint index entries, enrichment conflict reporting, artwork provenance priority, and writeback format coverage for ID3, FLAC/Vorbis, Ogg/Vorbis, and MP4-family tags
- playback stability policy for gapless/crossfade preparation, transition lead time, and start/end jitter suppression
- desktop runtime integration state for MPRIS/D-Bus/media-key/notification availability and desktop open-file/open-URL requests
- credential lifecycle and permission model for system-keyring-facing storage, token validity/revocation, read/write source capabilities, and safe status reporting
- host single-instance startup guard for the Linux device executable
- direct Linux X11 desktop-widget target, with `linux-x11-debug` and `linux-x11-debug-build` CMake presets
- host-machine Chromaprint/fpcalc provisioning through `scripts/ensure-host-fpcalc.ps1`; the current Windows host has `fpcalc` 1.6.0 installed through winget

## 3. Current Device-Side Scope

- Linux framebuffer device target
- Linux `evdev` keyboard input path
- direct Linux framebuffer output adapter
- host runtime capability adapters injected behind app-facing interfaces

## 4. Still Not Locally Completable

- LAN share and DLNA/UPnP provider families now resolve configured stream URLs through the unified remote stream path; richer native directory discovery still depends on optional backend/platform adapters.
- pointer-facing or large-screen final UI surfaces for advanced/professional DSP editing require an accepted non-small-screen device profile. The domain model and runtime chain state exist; the current required small-screen profile exposes the compact 10-band surface.
- Real Linux-session MPRIS service, D-Bus service registration, global media-key capture, and notification delivery remain platform-adapter work beyond the current app-side desktop projection/state boundary.
- Debian archive upload readiness still requires external maintainer identity, release tag, ITP/changelog closure, and sponsor-style clean chroot proof as tracked in `docs/engineering-check-report.md`.

## 5. Explicitly Excluded

- SDL or mock desktop simulator support is not a missing implementation item. `project-architecture-spec.md` removes that product/runtime distinction; direct host visual validation belongs to the real Linux X11 desktop-widget target.

## 6. Rule

Nothing in this file may be treated as the final product definition merely because it reflects what is implemented today.


## 2026-04-27 Functional Convergence Update

This update records implementation, not just domain modeling:

- Source Manager selections now enter real remote behavior: configured profiles probe, open Remote Browse, enumerate provider catalog nodes, resolve playable nodes, show Stream Detail, and start remote playback through the shared audio backend URI path.
- Direct URL, internet radio, playlist manifest, HLS/DASH, SMB, NFS, WebDAV, FTP, SFTP, and DLNA/UPnP source kinds round-trip through persisted profile kind strings and can be resolved as configured stream URIs.
- Jellyfin, Emby, OpenSubsonic, and Navidrome provider tooling exposes browse actions for root categories, artists, albums, playlists, folders where applicable, favorites, recent views, and playable tracks.
- Search is now an interactive page: typed characters build the query, local and active remote results are grouped by source, and confirming a result starts local or remote playback.
- Stream Detail is now actionable: it shows resolved diagnostics and `ENTER` starts the resolved stream.
- Queue / Up Next is reachable from Now Playing with `Q` and displays current local or remote playback state.
- Playlist Editor is reachable from playlist pages with `E` or `INS` so `F4` remains the global previous-track transport key, and the editor has its own page identity/help ownership.
- Desktop-open positional file/URL arguments are accepted by CLI targets and routed into local library startup playback or direct remote stream playback.
- Library rescan now records incremental change facts and migration planning in `LibraryRepository` rather than leaving governance only as a standalone model.
- Metadata governance now supports a persistent fingerprint index path in addition to in-memory lookup.
- Playback runtime now consumes the stability policy for transition preparation and start/end jitter suppression.
- `lofibox_remote_browse_playback_flow_smoke` covers Source Manager -> Remote Browse -> Stream Detail -> Now Playing remote URI playback.
- Remote browse/search playback now carries remote title, artist, album, and duration into Now Playing/Lyrics projections, and read-only remote items can reuse accepted local metadata cache records keyed by stable source identity plus item id.
- Host fingerprint provisioning is now executable outside the container: `scripts/ensure-host-fpcalc.ps1` detects `FPCALC_PATH` or `fpcalc`, can install Chromaprint through supported package managers, and was verified on this Windows host through winget.
- Direct Linux desktop-widget builds are no longer implicit knowledge: `linux-x11-debug` configures the real X11 presentation target.

## 2026-04-28 Search And Text Input Specification Update

This update records specification and implementation convergence:

- Search is a current first-class small-screen page, not a deferred feature.
- Editable text is now governed by `lofibox-zero-text-input-spec.md`.
- Search query truth is committed UTF-8 app state; input-method preedit remains transient projection and must not mutate query truth.
- Debian/Linux CJK input belongs to the user's system/session input-method stack and enters LoFiBox through committed UTF-8 text events.
- The X11 desktop-widget target must integrate with system input methods; framebuffer/evdev remains a direct Linux-key input adapter unless a separate device input-method design is specified.
- Current implementation now routes committed UTF-8 text events through app input, applies Unicode-safe append/backspace behavior, and keeps ASCII shortcuts from consuming non-ASCII Search text.
- Search now snapshots local plus configured ready remote results, preserves exact non-ASCII query matching, groups results by source, and starts local or remote playback from the selected result.

## 2026-04-28 EQ/Search Runtime Convergence Update

This update records implementation convergence after the EQ/Search runtime pass:

- EQ page state now updates the active playback `DspChainProfile`; changes are hot-applied to the running playback backend instead of requiring a track restart.
- The Debian host audio path now decodes local and remote media to realtime PCM, processes it through the active DSP chain, and hands the processed frames to the Linux output sink.
- On Debian targets with `aplay`, the realtime PCM sink uses ALSA with a small explicit buffer before falling back to other helpers, reducing the old `ffplay`-buffered UI/audio skew.
- Playback progress and spectrum advance only after the realtime output path confirms that processed PCM has entered the platform playback sink.
- The real-audio smoke path on `vicliu@192.168.50.92` was verified with `/usr/bin/aplay`, and installed-app remote playback from Library -> Songs was observed using `realtime PCM DSP -> /usr/bin/aplay`.

## 2026-04-28 Application Command Boundary Specification Update

This update records specification and implementation convergence:

- `application-command-boundary-spec.md` now defines an application command/query boundary for product evolvability, not for CLI alone.
- The boundary separates product commands and queries from GUI page commands, selected-row confirmation, focused-field editing, and runtime-shell details.
- GUI routing, desktop integration, direct CLI, runtime CLI, automation clients, and tests are all command consumers that must converge through application services instead of each inventing a route into controllers, `AppRuntimeContext`, UI pages, or runtime providers.
- `AppRuntimeContext` remains the current GUI runtime composition shell under migration pressure; it is not the long-term public product command API.
- `RuntimeServices` remains a grouped capability registry, not the product command/query service layer.
- Direct versus runtime command ownership is now specified as a state-ownership distinction: durable configuration/library/cache/diagnostic work may be direct, while live playback, active queue, current output, and in-memory runtime truth must target the running runtime once external runtime commands exist.
- `src/application` now contains real application services and registry code for playback commands, queue commands, playback status queries, library query/mutation/open actions, and source profile commands.
- GUI command routing now dispatches through `AppServiceRegistry` instead of requiring command targets to expose `LibraryController` or `PlaybackController`.
- `AppRuntimeContext` delegates source-profile create/update/persist/probe/readiness/credential-label behavior to `SourceProfileCommandService`, and delegates playback/library product commands through application services.
- `PlaybackController` no longer depends on application services; it accepts a playback-started recorder callback and remains an internal playback/queue coordinator behind `PlaybackCommandService`.
- `LibraryController` keeps GUI browse context and repository access while reusable query, mutation, and open-action services live under `src/application`.
- Architecture and implementation-placement gates now enforce the new application boundary placement and reject controller exposure from GUI command routing.
- No CLI feature is implemented by this status entry.

## 2026-04-28 Remote Browse / Credential / Direct CLI Boundary Update

This update records specification and implementation convergence for the next boundary pass:

- `RemoteBrowseQueryService` now lives under `src/application` and owns remote root/child browsing, source-scoped search, playable-node normalization, remote stream resolution, remote directory cache reads/writes, recent-browse cache writes, local read-only remote track fact caching, provider capability facts, degraded-state facts, source diagnostics, and stream diagnostics.
- `AppRuntimeContext` still owns GUI page state such as selected remote profile, parent, node, and stream, but Remote Browse, Search remote results, Server Diagnostics, and Stream Detail now consume structured application-service query results before projecting rows.
- Server diagnostics and stream details now have reusable structured truth before UI projection, including source label, provider kind/family, connection, credential reference status, TLS status, permission, token redaction status, stream URL redaction, buffer state, recovery action, quality preference, bitrate, codec, sample rate, channel count, live/seekable state, and provider availability.
- `CredentialCommandService` now owns credential status, secret set, and secret delete. `SourceProfileCommandService` no longer persists password or API token values through text-field updates; it keeps profile lifecycle, profile field mutation, readiness, credential-reference attachment, TLS toggles, permission labels, persistence, and probe.
- `CacheCommandService` and `RuntimeDiagnosticService` now provide application-level cache and doctor facts for direct command consumers.
- `AppServiceHost` now composes direct-command application services over the current app/controller/runtime objects so `src/cli` and tests do not treat `AppRuntimeContext`, targets, controllers, or runtime provider groups as their public command boundary.
- `src/cli` contains a direct command dispatcher for durable commands: source list/add/update/probe, credentials set/status/delete, library scan/status/query, cache status/clear, and doctor.
- Live playback, active queue, now-playing, seek, previous/next, pause/resume, active EQ, active remote session, live settings, shutdown, and reload belong to runtime command work. They now use the runtime command client/server contract and external runtime transport; direct CLI must not implement them by constructing a second runtime instance.
- Architecture and implementation-placement gates now cover `src/application/remote_browse_query_service.*`, `credential_command_service.*`, `cache_command_service.*`, `runtime_diagnostic_service.*`, and `src/cli/direct_cli.*`.
- The current arm64 Debian package was built and installed over the existing same-version package on `vicliu@192.168.50.92`; package-time CTest passed 60/60, `dpkg -i` replaced `lofibox (0.1.0-1) over (0.1.0-1)`, the installed `/usr/bin/lofibox` hash matched the package build artifact, GUI startup stayed alive until the timeout killed it, and installed direct CLI checks passed for source list, cache status, doctor, local library status/query, isolated source/credential/cache writes, and probe of every configured remote source.

## 2026-04-29 Runtime Command And Session Architecture Update

This update records specification and implementation convergence for the first live-runtime boundary pass:

- `runtime-command-session-architecture-spec.md` now defines runtime CLI as one future entry point into a transport-neutral runtime command/query bus, not as a CLI module that may borrow GUI runtime methods.
- The architecture now distinguishes durable application services from live runtime session truth. Live playback, active queue, active EQ, active remote session, and immediately effective runtime settings belong behind the runtime command/session boundary.
- GUI page interpretation remains GUI-owned, but GUI actions that mutate live playback, queue, or EQ must submit runtime commands. Page-local state such as selected rows and the selected EQ band remains projection/input state rather than runtime truth.
- `src/runtime` now contains the first in-process runtime command bus, command dispatcher, query dispatcher, session facade, result contract, and structured playback/queue/EQ/remote-session snapshots.
- GUI playback transport, queue stepping, playback mode toggles, local/remote library track start, stream-detail start, desktop-open URL start, and active EQ mutations now route through runtime commands inside the running instance.
- `lofibox_runtime_command_bus_smoke` covers start-track, queue-step, seek, queue-clear, EQ mutation, EQ preset cycling, structured remote-session snapshot projection, and runtime versioning. `lofibox_desktop_open_runtime_command_smoke` covers desktop-open URL playback through runtime command handling.
- Architecture and implementation-placement gates now require `src/runtime` ownership and prevent runtime code from depending on `AppRuntimeContext`, UI pages, targets, platform transports, or CLI adapters.
- This historical status entry has been superseded by the runtime-host ownership closure below: runtime IPC and external runtime CLI now exist through the Unix socket runtime transport.

## 2026-04-29 Runtime Command Architecture 收口 Update

This update records the runtime-command closure pass:

- `RuntimeSessionFacade` is now a composition facade over explicit runtime domains instead of the place where live business behavior keeps accumulating.
- `PlaybackRuntime`, `QueueRuntime`, `EqRuntime`, `RemoteSessionRuntime`, `SettingsRuntime`, and `RuntimeSnapshotAssembler` now live under `src/runtime` as the first domain split beneath the facade.
- `RuntimeCommandPayload` now uses tagged payload variants for command-specific data instead of one shared field bag.
- `RuntimeCommandResult` now preserves command origin, correlation id, `version_before_apply`, and `version_after_apply`.
- `RuntimeCommandClient`, `RuntimeCommandServer`, `RuntimeTransport`, `InProcessRuntimeCommandClient`, length-prefixed runtime envelopes, Unix socket runtime transport, and runtime CLI command parsing now define the runtime entry contract. Runtime CLI uses external transport only.
- `RuntimeHost` now owns the in-process runtime session, bus, server, local client, external transport, and live tick for the current running instance. `AppRuntimeContext` receives a runtime client and submits live commands through that client instead of composing or owning runtime internals.
- `AppCommandExecutor` remains a GUI page interpreter. It emits runtime commands through `AppCommandTarget` and no longer constructs `RuntimeCommandBus` or `RuntimeSessionFacade`.
- Runtime domain and client/server smoke tests now guard the domain split, snapshot assembly, client/server envelope behavior, command origin preservation, version-before/version-after facts, and unsupported-command rejection.
- Architecture and implementation-placement gates now require the runtime domain/client/server files and reject GUI command-routing paths that directly construct the runtime bus/session facade.

## 2026-04-29 Runtime Host Ownership And External Runtime CLI Update

This update records the runtime-host ownership closure:

- `LoFiBoxApp` now owns `RuntimeServices`, `AppServiceHost`, `RuntimeHost`, and `AppRuntimeContext` as separate top-level objects. `AppRuntimeContext` is no longer the app's runtime host.
- `RuntimeHost` owns `RuntimeSessionFacade`, `RuntimeCommandBus`, `RuntimeCommandServer`, `InProcessRuntimeCommandClient`, runtime EQ/settings state, external Unix socket transport, runtime shutdown/reload flags, and live playback tick.
- `LoFiBoxApp::update()` advances live playback through `RuntimeHost::tick(delta_seconds)` before updating the GUI shell. `AppLifecycleTarget` no longer exposes playback update.
- `AppRuntimeContext` now receives `AppServiceHost&` and `RuntimeCommandClient&`, owns only GUI/page state, and consumes app behavior through `AppServiceRegistry`; it does not directly call `AppServiceHost::controllers()` or `AppServiceHost::services()`.
- Runtime-to-GUI remote playback callbacks have been removed. Desktop-open URL, Stream Detail, Search remote result, and unified remote-library playback paths resolve streams through application query services and submit explicit resolved runtime command payloads.
- EQ runtime truth is split into `runtime::EqRuntimeState`; GUI selected-band state is split into `app::EqUiState`. Live settings truth is split into `runtime::SettingsRuntimeState`; GUI settings row/index state is split into `app::SettingsUiState`.
- Every published runtime command kind now has dispatcher behavior. Playback stop/seek, queue jump/clear, remote reconnect, settings apply, runtime shutdown, and runtime reload are no longer formal contract placeholders.
- Runtime CLI now connects over the Unix socket runtime transport. It has no in-process fallback and does not construct app services, controllers, runtime domains, runtime bus, runtime server, or `AppRuntimeContext`.
- New smoke coverage includes runtime host ownership, Unix socket transport, runtime CLI, seek/stop/queue-clear, settings apply, shutdown/reload, and desktop-open runtime-command routing.

## 2026-04-29 CLI Surface Contract And Machine Output Update

This update records the CLI contract pass:

- `lofibox-cli-surface-spec.md` now owns installed terminal syntax, the direct/runtime command split, product-object command families, `--json` / `--porcelain` / `--fields` / `--quiet`, and stable exit-code meanings.
- The CLI vocabulary is now explicitly product-object based: `source` configures durable profiles, `remote` browses/searches/resolves connected catalogs, `search` is the unified local/remote entry, `play` and `queue` target live runtime state, and CLI commands must not simulate pages or selected-row UI flows.
- Direct CLI now supports machine output and field filtering for implemented durable commands: source list/show/add/update/remove/probe/auth-status/capabilities, credentials list/show-ref/status/validate/set/delete with stdin secret options, library scan/status/list/query, unified search, remote browse/recent/search/resolve/stream-info, cache status/purge/gc, and doctor.
- Runtime CLI now supports `now`, `queue show`, `eq show`, JSON command results, field-filtered snapshots, `play --id`, `play --pause/resume/toggle/next/prev/stop/seek/shuffle/repeat`, `queue next`, and `queue set`, while continuing to use the external runtime transport only.
- `lofibox version --json` and `--porcelain` are available through the common target option path.
- The direct/runtime CLI smoke tests now cover machine output, field filtering, credential-reference status, source capability projection, local search, remote browse root projection, cache purge, runtime `now`, and runtime EQ snapshot projection.
- Verified on `vicliu@192.168.50.48`: no-X11/no-device Ninja test build passed 71/71 CTest tests.
- Verified on `vicliu@192.168.50.92`: temporary-prefix device build installed and passed CLI smoke; Debian `dpkg-buildpackage -b -us -uc` passed package-time 71/71 tests; `sudo dpkg -i lofibox_0.1.0-1_arm64.deb` replaced the installed package; installed CLI smoke and `sh debian/tests/smoke` passed.

## 2026-04-29 Terminal UI Presentation Target Update

This update records the first terminal-native presentation target pass:

- `lofibox-terminal-ui-spec.md` now defines LoFiBox TUI as a formal presentation target, not a CLI print mode or GUI fallback.
- The TUI boundary is explicit: it projects `RuntimeSnapshot`, emits `RuntimeCommand`, owns only terminal-local UI state, and must not access audio backends, playback backend controllers, lyrics providers, library scanners, remote providers, credential stores, GUI pages, or `AppRuntimeContext`.
- `RuntimeSnapshot` now includes TUI-ready projection domains for visualization, lyrics, library, source summary, diagnostics, creator placeholders, richer playback metadata, and visible queue items.
- Runtime snapshot assembly now projects existing code facts already present in the running instance: playback session lyrics and visualization frame, library counts/readiness, queue item labels through library queries, and diagnostic capability status.
- `lofibox_zero_tui` and standalone `lofibox-tui` now build a zero-dependency ANSI TUI surface with adaptive layouts, Unicode/ASCII/minimal character modes, dashboard/now/lyrics/spectrum/queue/library/sources/EQ/DSP/diagnostics/creator/help/command-palette views, runtime event ingestion, raw terminal input, alternate screen, and diff rendering.
- Device and X11 targets dispatch `lofibox tui ...` to the TUI entry point when TUI support is built.
- New smoke tests cover progress bars, spectrum rendering, lyrics fallback/current-line display, layout classification, input routing, dashboard rendering, runtime projection, and terminal capability/diff rendering behavior.
- Verified on `vicliu@192.168.50.48`: no-X11/no-device Ninja build with `LOFIBOX_BUILD_TUI=ON` built `lofibox_zero_tui_bin`, passed all 8 TUI smoke tests, and passed full CTest 79/79.
- Verified on `vicliu@192.168.50.92`: Debian `dpkg-buildpackage -b -us -uc` passed package-time CTest 79/79, `sudo dpkg -i /home/vicliu/lofibox_0.1.0-1_arm64.deb` installed the package, `/usr/bin/lofibox-tui` existed, `lofibox-tui --help`, `lofibox tui --help`, `lofibox-tui --once --no-color --charset ascii`, and `sh debian/tests/smoke` all passed. The one-shot TUI correctly rendered the runtime-disconnected page when no runtime socket was active.

## 2026-04-29 Terminal UI Event Stream / Creator / Input Completion Update

This update records the completion pass for the three TUI gaps left by the first presentation-target implementation:

- Runtime event streaming now exists as a real Unix socket stream contract, not a TUI-private polling loop. Clients send an `event_stream` envelope, receive `runtime.connected` and `runtime.snapshot`, then receive snapshot-derived change events and heartbeat snapshots.
- Runtime event types now cover playback change/progress/error, queue, EQ, remote, settings, lyrics, lyrics-line, visualization frame, library scan state, diagnostics, creator, and runtime snapshot/connection events.
- The Unix socket runtime server now handles event streams on client worker threads so long-lived stream clients do not block command or query clients.
- TUI interactive mode now uses a dedicated runtime event ingest thread, drains `RuntimeEvent` objects into `TuiModel`, and keeps rendering bounded by the configured refresh cadence. `--once` remains a one-shot snapshot render.
- Creator mode is no longer only a placeholder page. `CreatorAnalysisRuntime` derives a live creator projection from the running playback visualization frames: estimated BPM, spectral key estimate, LUFS estimate, dynamic range estimate, waveform points, beat grid, loop markers, section markers, stem status, analysis source, and confidence.
- Terminal input decoding now preserves committed UTF-8 text, recognizes Shift+Tab, Shift+Arrow, Ctrl+D, Ctrl+U, Backspace, arrows, Enter, Escape, Tab, and printable characters, and guards bracketed paste by stripping terminal controls and filling command/search text without executing pasted command-palette content.
- Smoke coverage now includes runtime event stream connection/command concurrency/event delivery, creator projection through runtime snapshots, UTF-8 terminal input decoding, bracketed paste guard behavior, and command-palette paste confirmation semantics.
- Verified on `vicliu@192.168.50.48`: no-X11/no-device Ninja build with `LOFIBOX_BUILD_TUI=ON` passed full CTest 80/80, including the new runtime event stream, creator projection, and terminal input guard coverage.
- Verified on `vicliu@192.168.50.92`: Debian package-time CTest passed 80/80, the regenerated `/home/vicliu/lofibox_0.1.0-1_arm64.deb` installed over the already-installed `lofibox (0.1.0-1)` with `Unpacking lofibox (0.1.0-1) over (0.1.0-1)`, and installed smoke passed for `/usr/bin/lofibox-tui`, `lofibox tui --help`, `lofibox-tui --once --no-color --charset ascii`, and `sh debian/tests/smoke`.

## 2026-05-21 Local Media Root Persistence Convergence Update

This update records implementation convergence for durable local media roots:

- Enabled local media roots now persist as `SourceProfilesDomain` local-root profiles, including stable id, display name, local filesystem path, enabled/default-eligible flags, and no credential reference.
- `~/Music` is now the projected default local media root when no enabled local-root profile exists; with `HOME=/root`, the default resolves to `/root/Music`.
- GUI/device startup uses `LibraryMutationService::beginAsyncRefreshConfiguredLibrary()` when no diagnostic roots are supplied, so x11/device app startup reads enabled roots through application services instead of target-local defaults.
- `LibraryMutationService` now exposes configured-root refresh methods, and local-root add/remove/enable/disable marks the in-memory library index stale before the next refresh/rebuild.
- Direct CLI supports `lofibox source local-root list/add/remove/enable/disable` and `lofibox library root list/add/remove/enable/disable` as aliases over the same source-profile local-root lifecycle.
- `lofibox library scan [path...]` remains a temporary diagnostic scan; commands without explicit roots refresh from durable enabled local roots.
- Runtime `LibraryRefresh` is a live in-memory refresh request only: it reloads enabled durable roots and starts async library refresh, but it does not mutate `SourceProfilesDomain`.
- Local-root profiles are filtered out of remote browse/search behavior while still round-tripping through profile persistence.
- Windows/MSVC builds now compile sources as UTF-8 with `/utf-8`, which is required by existing TUI Unicode source files.
- Smoke coverage now includes default `~/Music` projection, configured-root library refresh, local-root direct CLI add/list/enable/disable/remove, XDG local-root persistence without credentials, runtime library refresh, and TUI/runtime projection checks.

## 2026-05-21 Local Media Root Startup Environment Fix (0.2.3)

This update records the post-0.2.2 fix for device/APPLaunch starts where the process environment may not include a login-shell `HOME`:

- Linux runtime paths now resolve `~` from the effective user's account record when `HOME` is unset, so root-owned APPLaunch/device starts still read `/root/.local/share/lofibox` instead of a temporary profile store.
- `SourceProfileCommandService` uses the same effective-user home fallback for the default local media root, keeping the product default as `~/Music` before any `/music` compatibility fallback.
- Direct CLI local-root add/remove/enable/disable commands now best-effort request runtime `LibraryRefresh`, letting a running GUI reload durable enabled roots immediately after the durable source-profile mutation.
- Smoke coverage now includes effective-user home fallback when `HOME` is unset and GUI startup from persisted local-root profiles with no explicit diagnostic roots.

## 2026-05-22 Early PCM Child Exit Completion Fix (0.2.6)

This update records the post-0.2.5 Linux PCM child-exit and early backend-position finish fix for the 0.2.6 patch line:

- Linux PCM worker exit is now observed while pipe reads are pending, so a decoder or sink process that exits early cannot leave runtime stranded in `PLAYING`.
- When a backend finish reports a reliable position still materially before the known duration, runtime pauses the current track rather than treating that adapter event as normal queue completion.

## 2026-05-21 Playback Completion And Switch Latency Fix (0.2.5)

This update records the post-0.2.4 playback completion and local Linux switch-latency fix for the 0.2.5 patch line:

- Automatic queue advance no longer trusts `AudioPlaybackState::Finished` alone when a known duration exists. Early backend Finished states keep the current item active until the playback clock is within the accepted remaining-duration threshold.
- Linux local PCM decoding is realtime-throttled to reduce sink-buffer drain races.
- Local Linux track switching no longer waits synchronously for the old realtime PCM worker to join before starting the next track, and local PCM startup no longer blocks the playback command on a two-second output-confirmation window plus retry.
- Local track playback starts the backend before refreshing local metadata/artwork so slow metadata or embedded-artwork reads do not delay audio process startup.
- Linux builds compile the WebUI target by default, keeping CI's default Linux configure path on WebUI smoke coverage.
- Settings projects the configured WebUI address only when network status is online, and the value scrolls inside the normal bounded value column instead of widening the row.

## 2026-05-21 Playback And Input Semantics Fix (0.2.4)

This update records the post-0.2.3 playback/input convergence for the 0.2.4 patch line:

- Main Menu `F6` and Now Playing Up/Down now share the same playback-mode cycle: order -> shuffle -> repeat all -> repeat one -> order. `F7` and `F8` are no longer primary GUI/device playback-mode shortcuts.
- Linux evdev keyboard handling now treats standard `EV_KEY` codes as the input truth: only `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, and `KEY_RIGHT` become direction commands, while ordinary letter keys remain character input.
- Smoke coverage now includes unified playback-mode shortcut routing and Linux evdev direction/letter-key separation.
