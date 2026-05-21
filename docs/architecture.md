<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Architecture

This document is the engineering entry point for LoFiBox architecture.
Normative details live under `docs/specification/`.

## Product Truth

LoFiBox is a pure C++ Linux desktop music player targeting Debian and Ubuntu official repositories.
Cardputer Zero, PocketFrame, framebuffer, VNC, and other device-shaped environments are adapters or validation profiles, not the product's highest identity.

Primary specs:

- `docs/specification/lofibox-zero-final-product-spec.md`
- `docs/specification/integrated-product-core-spec.md`
- `docs/specification/debian-official-archive-spec.md`
- `docs/specification/project-architecture-spec.md`

## Target Source Boundaries

The long-term source layout is semantic rather than incidental:

- `src/core`: domain model, state, commands, errors, configuration
- `src/playback`: playback lifecycle, queue, seek, gapless, crossfade
- `src/audio`: decoder, output, DSP, EQ, ReplayGain, resampling
- `src/cache`: cache policy, remote-directory/station/recent caches, offline audio, sync planning
- `src/metadata`: tags, artwork, lyrics linkage, metadata cache
- `src/library`: scan, index, database, migrations, queries
- `src/playlist`: playlist model and parsers
- `src/remote`: remote source and media server providers
- `src/plugins`: provider API, manifests, registration, lifecycle
- `src/desktop`: MPRIS, D-Bus, media keys, notifications
- `src/platform`: XDG, file watching, platform adapters
- `src/security`: credentials, TLS, auth, sensitive data
- `src/ui`: pages, controls, view models, themes

Current page renderers and shared UI primitives live under `src/ui`.
`src/ui` owns UI projection types only; it must not include `src/app` headers or reference `app::` internal state types.
`src/app` may orchestrate which page is rendered, but it must not own page, chrome, modal, font, or color drawing implementations, and it must translate app/runtime/playback state into UI projections before calling page renderers.
`src/app/app_input_router.*` owns page-level input routing; `LoFiBoxApp` delegates `InputEvent` handling to that router and exposes command execution through a narrow target interface.
`src/app/app_command_executor.*` owns page command execution semantics, including main-menu actions, list confirmation, settings row actions, playback commands, playback-mode cycling, and EQ band/gain limits.
`src/app/app_page_model.*` owns page titles, page rows, browse-list classification, Settings rows, and list viewport limits; `LoFiBoxApp` supplies state inputs but does not define page-model branches.
`src/app/app_renderer.*` owns page-level render routing and UI view assembly; `LoFiBoxApp` delegates drawing through a narrow render target interface instead of directly including concrete UI page renderers.
`src/app/app_lifecycle.*` owns application tick ordering; `LoFiBoxApp` delegates `update()` so runtime status refresh, library loading, boot transition, and playback tick remain one explicit lifecycle boundary.
`src/app/app_runtime_context.*` owns app runtime state, controllers, runtime services, and the target-interface implementations consumed by input, render, lifecycle, and command helpers; `LoFiBoxApp` is only the public facade.
`src/app/app_runtime_state.*` owns scalar/session UI-facing runtime state such as navigation, settings, network status, metadata service status, EQ state, help state, boot timestamps, and media roots; `src/app/app_controller_set.*` owns controller objects such as library and playback controllers.
`src/app/runtime_services.*` exposes `RuntimeServiceRegistry`, grouped by capability: `ConnectivityServices`, `MetadataServices`, `PlaybackServices`, `RemoteMediaServices`, and `CacheServices`; concrete host/device code fills providers, while shared app code consumes grouped capabilities only.
`src/playback/playback_controller.*` owns playback state-machine and queue semantics; `src/playback/playback_enrichment_coordinator.*` owns asynchronous metadata/artwork/lyrics enrichment worker lifecycle and pending-result merge semantics.
`src/playback/playback_session_clock.*`, `playback_backend_controller.*`, `playback_completion_policy.*`, `playback_visualization_source.*`, and `playback_runtime_coordinator.*` own playback time, backend commands, end-of-track policy, visualization-frame sourcing, and runtime transition orchestration.
`src/app/library_query_service.*` owns library fact queries; `src/app/library_navigation_service.*` owns list title/row projection; `src/app/library_open_action_resolver.*` owns list-open action semantics; `src/app/library_list_context.h` owns current browse/list context.
`src/app/app_projection_builder.*` owns app-state to UI view-model projection; `src/app/app_renderer.*` dispatches pages and boot/help chrome only.
`src/ui/widgets/lyrics_layout.*` owns lyric parsing, active-line selection, and scrolling-window layout; `src/ui/effects/lyrics_spectrum_effect.*` owns lyrics-page spectrum visual algorithms.
`src/platform/host/runtime_enrichment_clients.cpp` is shared enrichment helper/orchestration code; concrete host enrichment clients live in dedicated protocol files.
`src/platform/host/host_runtime_service_providers.*` owns host runtime service group construction; `src/platform/host/runtime_services_factory.*` is only the final composition boundary that returns a null-completed `RuntimeServices` registry.
`src/platform/host/runtime_metadata_enrichment_orchestrator.*` owns host metadata lookup ordering across identity and MusicBrainz clients; metadata providers consume its result and retain only local tag/cache/writeback application.
`src/app/library_repository.*` owns in-memory library fact state and scan replacement; `LibraryController` coordinates list context/navigation and no longer owns the raw `LibraryModel` storage directly.
`src/playback/*` owns playback lifecycle, queue, state, clocks, completion policy, enrichment coordination, and runtime handoff; `src/app` may keep compatibility forwarding headers but must not contain playback implementation `.cpp` files.
`src/audio/audio_pipeline_controller.*` owns the app-facing audio pipeline facade for backend command dispatch, playback state reads, and visualization tap reads. `src/audio/decoder/*` owns decoder contracts, `src/audio/output/*` owns output contracts and host output implementation, and `src/audio/dsp/*` owns DSP node state and sample-processing policy.
`src/cache/cache_manager.*` owns cache/offline buckets, capacity and age policies, garbage collection, remote directory caches, station-list caches, recent-browse caches, offline audio saves, and album/playlist offline sync planning.
`src/remote/common/remote_provider_contract.*` owns provider manifests/capabilities; `src/app/remote_profile_store.*` and `src/platform/host/xdg_remote_profile_store.*` own profile persistence with credential references only. `src/app/media_item.*`, `media_search_service.*`, and `src/playback/mixed_playback_queue.*` define the local/remote media-item bridge. `src/library/library_indexer.*` and `library_store.*` own indexing and durable-store boundaries. `src/metadata/enrichment_authority_policy.*` owns enrichment trust decisions.
`src/remote/jellyfin`, `src/remote/emby`, `src/remote/opensubsonic`, and `src/remote/navidrome` own first-batch provider protocol behavior. `scripts/remote_media_tool.py` is only a private helper entrypoint that dispatches into those provider modules; it must not contain provider endpoint logic.
`src/playlist/*` owns playlist format detection and parser contracts. `src/plugins/*` owns plugin/provider manifest registry contracts.
`src/platform/host/lyrics_pipeline_components.*` owns lyrics cache and lyrics writeback policy; `src/platform/host/lyrics_provider.cpp` composes those components instead of hiding the whole lyrics chain.
`src/platform/host/runtime_host_tools.*` names host helper sub-boundaries for text parsing, JSON helpers, cache-path derivation, and helper-script resolution.
The integrated product core distinguishes media identity, media source, media stream, playback facts, audio pipeline, enrichment pipeline, library facts, UI projection, and runtime shell responsibilities. Those distinctions are normative in `docs/specification/integrated-product-core-spec.md`.
`src/metadata/metadata_merge_policy.*` owns safe merge rules for enrichment results; `src/metadata/match_confidence_guard.*` owns confidence checks before authoritative writeback decisions.
`src/app/library_mutation_service.*` owns library fact mutations such as play count and last-played timestamp updates.
`src/remote/common/remote_source_registry.*` owns the first-batch remote source registry and treats Navidrome as an OpenSubsonic-compatible provider family.
`src/security/credential_policy.*` owns credential references, TLS defaults, and secret redaction helpers for remote/source work.
`src/app/settings_projection_builder.*` and `src/app/source_manager_projection.*` own Settings and Source Manager projection rows instead of letting page-model code hand-assemble source/server details.
`src/desktop/desktop_integration_boundary.*` owns the initial desktop command and now-playing projection boundary; desktop adapters must consume this boundary rather than reading UI pages or backend objects directly.

## Hard Boundary Rules

- UI must not directly call FFmpeg, SMB, Jellyfin, SQLite, D-Bus, or protocol clients.
- UI/page code must not include `src/app`, concrete platform adapters, or backend/protocol implementation layers.
- UI/page code must not reference `app::` internal types; page renderers consume UI projections and render-only view models.
- App composition code must use `src/ui` primitives for top bars, frames, modals, text, image blits, and theme colors instead of reimplementing low-level drawing.
- App composition code must not directly map raw input to page behavior; routing from input actions to app commands lives in the input router.
- App composition code must not directly implement menu index branches, Settings row branches, playback command behavior, library open-result behavior, or EQ clamp semantics; page command execution lives in the app command executor boundary.
- App composition code must not directly construct page titles, page rows, Settings rows, browse-list classification, or list viewport constants; page model generation lives in the app page-model boundary.
- App composition code must not directly include concrete `src/ui/pages` renderers; page render dispatch lives in the app renderer boundary.
- App composition code must not inline application tick ordering; runtime refresh, library loading, boot transition, and playback update order lives in the app lifecycle boundary.
- `LoFiBoxApp` must not own runtime state, controllers, routing helpers, page render helpers, lifecycle helpers, command helpers, or debug-snapshot assembly; those belong to `AppRuntimeContext`.
- `AppRuntimeContext` must not re-accumulate raw state/controller fields; runtime state belongs to `AppRuntimeState`, controller ownership belongs to `AppControllerSet`, and context remains the adapter across target interfaces.
- Runtime service access must go through capability groups (`connectivity`, `metadata`, `playback`, `remote`) rather than top-level provider fields, so adding lyrics, tag writeback, streaming, or desktop capabilities does not turn the registry into a global grab bag.
- `PlaybackController` must not own enrichment threads, pending enrichment maps, or network enrichment request merging; it delegates those to `PlaybackEnrichmentCoordinator`.
- Runtime enrichment protocol clients must remain single-responsibility host clients; AcoustID, MusicBrainz, Cover Art Archive, LRCLIB, lyrics.ovh, and ffprobe behavior must not collapse back into one monolithic client file.
- `LyricsProvider` may orchestrate lookup, but cache semantics, writeback policy, embedded reading, online resolution, and match guarding must remain distinct concepts.
- `LibraryController` may coordinate library page behavior, but library fact queries, navigation row/title projection, open-action resolution, and list context state must stay separable.
- `AppRenderer` must not translate app state into UI projection structs inline; projection building belongs to `AppProjectionBuilder`.
- Lyrics pages must not own lyric parsing, active-line location, scrolling-window algorithms, or spectrum rendering algorithms; those belong to widgets/effects.
- Host runtime helper utilities must be named by responsibility rather than hidden inside `runtime_host_internal`.
- Runtime shells such as PocketFrame, Cardputer Zero, container, framebuffer, X11, VNC, or desktop widget surfaces must not fork media identity, playback, audio, library, remote, enrichment, or projection semantics.
- Host runtime service group construction must stay outside app controllers and outside UI/page code; factory code may compose service groups but must not regain protocol, playback, or metadata implementation details.
- Core code must not include app, platform, UI, playback, audio, metadata, library, remote, desktop, or security layers.
- Host adapters implement runtime services and helper/resource resolution, but do not depend on concrete app/page classes.
- Targets compose app runners and platform adapters; they do not own page implementations or product behavior.
- Playback must not know protocol details.
- Remote providers produce media objects and streams; they do not own playback state machines.
- Metadata and library code do not depend on UI.
- Desktop integration translates external events into player commands.
- Protocol clients do not persist plaintext secrets.
- Runtime user data follows XDG paths.

The repository enforces include-direction rules with `scripts/check-architecture-boundaries.ps1`, wired into CI.
The repository enforces implementation placement rules with `scripts/check-implementation-placement.ps1`, wired into CI. This gate rejects empty `src` placeholder directories, provider logic hidden in helper entrypoints, playback implementation under `src/app`, and audio output implementation outside `src/audio/output`.

## Runtime Path Boundary

Runtime paths are resolved through the platform layer, not by feature code reading environment variables directly.
On Linux, LoFiBox uses:

- config: `$XDG_CONFIG_HOME/lofibox` or `~/.config/lofibox`
- data: `$XDG_DATA_HOME/lofibox` or `~/.local/share/lofibox`
- cache: `$XDG_CACHE_HOME/lofibox` or `~/.cache/lofibox`
- state: `$XDG_STATE_HOME/lofibox` or `~/.local/state/lofibox`
- runtime: `$XDG_RUNTIME_DIR/lofibox` or a temporary fallback for ephemeral locks

If `HOME` is unset on Linux, the platform boundary may resolve `~` from the effective user's account record before using any temporary or compatibility fallback.
The cache store, runtime logger, script bridge payloads, and single-instance lock must use this path boundary.
Installed paths such as `/usr/share/lofibox` and `/usr/lib/lofibox` are read-only runtime resource locations and must not receive user data, cache files, logs, or state.

## Runtime Helper Boundary

Host runtime helper scripts are packaged application resources, not source-tree assumptions.
Runtime resolution order is:

1. `LOFIBOX_HELPER_DIR` for explicit test/development override
2. installed private helper directory such as `/usr/lib/lofibox`
3. source-tree `scripts/` directory for developer builds

Feature code must call the runtime helper resolver instead of embedding `scripts/tag_writer.py` or `scripts/remote_media_tool.py` paths directly.

## Data Flow

The intended high-level flow is:

```text
SourceProvider -> CatalogProvider -> MediaItem
MediaItem -> StreamResolver -> TrackSource / MediaStream
TrackSource -> Decoder -> DSP Chain -> Volume -> AudioOutput
MetadataProvider -> Track / Album / Artist / Artwork / Lyrics projection
Desktop/Input/UI -> PlayerCommand -> PlaybackSession
```

## Command Flow

UI, media keys, D-Bus, and device input must converge into player commands.
Commands update core playback, queue, library, source, and settings state through defined boundaries.

## Error Model

Errors should be mapped at the boundary where they occur:

- protocol errors become source/provider errors
- decode errors become media-pipeline errors
- credential errors become security/credential errors
- desktop integration errors remain adapter errors

User-facing errors should be explanatory without leaking secrets.
