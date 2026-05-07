<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Plugin System Architecture Gap Analysis

> Evaluates current codebase readiness for the plugin system defined in
> `lofibox-zero-plugin-system-spec.md`. Each issue is classified by severity
> and the implementation phase it blocks.

## Summary

The codebase has strong runtime architecture (command bus, snapshots, events)
that aligns well with the plugin vision. However, **five hard architectural
issues** must be resolved before any plugin sub-module can be wired in. The
remaining issues are scoped to specific phases and can be addressed
incrementally.

---

## HARD ISSUE 1 — Theme is Fully Hardcoded at Compile Time

**Severity:** Blocks Phase 1 (Skin Plugin)
**Files:** `src/ui/ui_theme.h`, all `src/ui/pages/*.cpp`, `src/core/canvas.h`,
`src/core/bitmap_font.h`, `src/webui/webui_theme_tokens.h`

### Current State

`ui_theme.h` defines the entire visual design as `inline constexpr` values:

```cpp
inline constexpr auto kBgRoot = core::rgba(5, 6, 8);
inline constexpr auto kBgPanel0 = core::rgba(10, 12, 15);
// ... 20+ more color constants
inline constexpr int kTopBarHeight = 20;
inline constexpr int kListTop = 28;
// ... spacing constants
```

Every page (`now_playing_page.cpp`, `equalizer_page.cpp`, `lyrics_page.cpp`,
`list_page.cpp`, `main_menu_page.cpp`, `about_page.cpp`) directly references
these `constexpr` values. Colors, spacing, and font metrics are baked into
rendering logic at compile time.

### Required Change

1. Create a runtime `UiTheme` struct (replacing `ui_theme.h` constexpr) with
   palette, spacing, and layout fields.
2. Thread `const UiTheme&` through all page rendering functions.
3. `AppRenderTarget` must expose the active `UiTheme`.
4. `SkinPluginAdapter` loads `skin.json` → populates `UiTheme`.
5. Font selection must become runtime: `BitmapFont` needs to load from a font
   file path rather than using a single compiled-in font.
6. WebUI has a parallel problem: `webui_theme_tokens.h` must also become
   runtime-driven from the same skin.

### Impact Radius

Every page file. This is the single largest refactor required for the plugin
system. But the change is mechanical — replace `ui::kColorName` with
`theme.palette.color_name` — not architectural.

---

## HARD ISSUE 2 — No Layout Template System

**Severity:** Blocks Phase 1 (Skin Plugin)
**Files:** `src/ui/pages/now_playing_page.cpp`, `src/ui/pages/equalizer_page.cpp`,
`src/ui/pages/lyrics_page.cpp`, `src/ui/pages/list_page.cpp`,
`src/app/app_projection_builder.h`, `src/app/app_renderer.h`

### Current State

Page rendering is monolithic: each page's layout logic is directly embedded in
its rendering function. There is no separation between "what data to show"
(model/projection) and "how to lay it out" (template/layout).

For example, `NowPlayingPage` rendering decides where the cover art goes, how
many text lines to show, where the spectrum is positioned — all as hardcoded
coordinates in the render function.

### Required Change

1. Define layout template structs for each page type (`NowPlayingLayout`,
   `EqLayout`, `LyricsLayout`, `ListLayout`).
2. Extract layout decisions (positions, sizes, visibility) from page renderers
   into these templates.
3. Page renderers take `ViewData + LayoutTemplate + UiTheme` and produce pixels
   without making their own layout decisions.
4. Skin JSON populates layout templates via `SkinPluginAdapter`.

### Impact Radius

All 6 page renderers. However, the data flow (RuntimeSnapshot → Projection →
Render) stays intact. Only the "where to put things" logic moves from page
code into template structs.

---

## HARD ISSUE 3 — Remote Providers are Hardcoded in a Dispatcher

**Severity:** Blocks Phase 2 (Remote Source Plugin)
**Files:** `src/remote/tooling/remote_media_dispatcher.py`,
`src/remote/common/remote_source_registry.h`,
`src/remote/common/remote_provider_contract.h`,
`src/app/remote_media_services.h`

### Current State

The Python-side dispatcher (`remote_media_dispatcher.py`) maps provider kinds
to modules via a hardcoded `_provider()` function with an if-else chain:

```python
def _provider(kind: str):
    if kind == "jellyfin":       return jellyfin_provider
    if kind == "emby":           return emby_provider
    if kind == "navidrome":      return navidrome_provider
    # ... etc
```

The C++ side (`RemoteSourceRegistry`) maps a fixed `RemoteServerKind` enum to
manifests. Adding a new remote source requires:
1. A new `RemoteServerKind` enum value
2. A new Python module
3. A new if-else branch in the dispatcher
4. A new entry in `RemoteSourceRegistry`

There is no `profile_schema` concept — the UI must know about each provider
type explicitly.

### Required Change

1. Replace `_provider()` dispatcher with a plugin-based discovery: each
   provider plugin directory contains `plugin.json` + `provider.py`.
2. Implement `lofibox-jsonrpc-1` protocol: persistent stdio JSON-RPC instead
   of the current temp-file-passing approach (`captureProcessOutput`).
3. Add `profile_schema` method to provider protocol — UI renders config forms
   from schema, not from provider-specific knowledge.
4. `RemoteSourceRegistry` must accept dynamically registered providers from
   plugins, not just from the fixed `RemoteServerKind` enum.
5. `PluginRuntime` must manage external helper process lifecycle.

### Impact Radius

The Python remote provider framework and the C++ remote registry. Existing
providers (Jellyfin, Emby, Navidrome, OpenSubsonic, DLNA, Shares, Playlist)
can be migrated to the plugin layout incrementally — they continue to work
through the existing dispatcher until migrated.

---

## HARD ISSUE 4 — No External Process Lifecycle Management

**Severity:** Blocks Phase 2 and Phase 3 (Remote + Metadata Plugins)
**Files:** `src/platform/host/runtime_host_internal.h` (has process primitives
but no lifecycle management), `src/platform/host/remote_media_runtime.cpp`

### Current State

The codebase has low-level process utilities (`spawnPipeProcess`,
`stopPipeProcess`, `RunningPipeProcess`, `captureProcessOutput`) but no
lifecycle management layer:
- No persistent process (each call spawns a new process)
- No timeout enforcement
- No crash detection or restart policy
- No stderr capture to log
- No stdin/stdout protocol multiplexing
- No health check / heartbeat

The current remote media tool is invoked via `callRemoteMediaTool()` which
writes JSON to a temp file, spawns `python3 remote_media_tool.py <tmpfile>`,
and reads stdout. This is one-shot, not a persistent helper.

### Required Change

1. Build `PluginRuntime` — a persistent process manager that:
   - Spawns an external helper on first use
   - Keeps stdin/stdout pipes open
   - Multiplexes requests over the pipe (one JSON object per line)
   - Enforces per-request timeout (default 30s)
   - Detects process death, restarts (max 3 in 60s window)
   - Captures stderr to plugin-specific log
   - Disables plugin after repeated failures
2. JSON-RPC encode/decode on the C++ side for `lofibox-jsonrpc-1`.

### Impact Radius

New module. Adapts existing low-level process utilities. Existing one-shot
`callRemoteMediaTool()` can be retired after migration.

---

## HARD ISSUE 5 — Plugin Manifest is Too Minimal

**Severity:** Blocks Phase 0 (Foundation)
**Files:** `src/plugins/plugin_manifest.h`, `src/plugins/plugin_manifest.cpp`

### Current State

```cpp
enum class PluginKind { InternalProvider, BinaryProvider };

struct PluginManifest {
    std::string id{};
    std::string name{};
    PluginKind kind{PluginKind::InternalProvider};
    std::vector<std::string> capabilities{};
    std::vector<std::string> runtime_dependencies{};
};
```

Missing:
- `version` (semver)
- `api_version`
- `schema_version` (manifest format version)
- `entry` (command, args, protocol for external helpers)
- `permissions` (capability declarations for packaging review)
- `resources` (paths to plugin-relative resource dirs)
- `AssetPack` and `ExternalHelper` in `PluginKind` enum
- Effect configuration for DSP plugins

### Required Change

Extend `PluginManifest` to match §5 of the plugin spec. Add `PluginKind::AssetPack`
and `PluginKind::ExternalHelper`. This is the smallest change by line count
but it's the foundation everything else builds on.

### Impact Radius

The single `plugin_manifest.h` header and its single consumer
(`plugin_manifest.cpp`). Currently nothing else references these types, so
this is a clean extension with no downstream breakage.

---

## SOFT ISSUE 6 — Settings State Has No Plugin Dimensions

**Severity:** Needed for Phase 0 (Foundation)
**Files:** `src/runtime/settings_runtime_state.h`, `src/runtime/settings_runtime.h`

### Current State

`SettingsRuntimeState` only tracks:
```cpp
struct SettingsRuntimeState {
    std::string output_mode{"DEFAULT"};
    std::string network_policy{"DEFAULT"};
    std::string sleep_timer{"OFF"};
    bool shutdown_requested{false};
    bool reload_requested{false};
};
```

No plugin state: enabled/disabled list, selected skin, metadata provider order,
nothing.

### Required Change

Add plugin-related fields or a separate `PluginSettingsStore` (as defined in
the spec) that holds enabled/disabled lists and provider priority order.
`PluginSettingsStore` persists to `~/.config/lofibox/plugins.json`.

### Impact Radius

`SettingsRuntime`, `SettingsRuntimeSnapshot`, `RuntimeSnapshotAssembler`,
WebUI projection. Low complexity — additive change.

---

## SOFT ISSUE 7 — No DSP Effect Provider Abstraction

**Severity:** Blocks Phase 4 (DSP Plugin)
**Files:** `src/audio/dsp/dsp_chain.h`, `src/audio/audio_pipeline_controller.h`

### Current State

`DspChain` is monolithic — it directly implements EQ + ReplayGain + Limiter
as a single class. There is no `AudioEffectProvider` interface, no effect
registry, no way to chain effects from plugins. Effects are not first-class
entities.

The `EqManager` manages EQ profiles through `PresetRepository`, but this is
EQ-specific, not a general effect management system.

### Required Change

1. Define `AudioEffectProvider` C++ interface (as in spec §6.3).
2. Refactor `DspChain` to compose effects from registered providers rather
   than hardcoding EQ + ReplayGain + Limiter.
3. `DspPluginAdapter` loads effect configurations from plugin manifests.

### Impact Radius

DSP chain, audio pipeline controller, EQ manager. This is Phase 4 work and
can be deferred.

---

## SOFT ISSUE 8 — RuntimeSnapshot Has No Plugin Section

**Severity:** Needed for Phase 0 diagnostics
**Files:** `src/runtime/runtime_snapshot.h`,
`src/runtime/runtime_snapshot_assembler.h`

### Current State

`RuntimeSnapshot` has sections for playback, queue, EQ, remote, settings,
visualization, lyrics, library, sources, diagnostics, and creator — but
nothing for plugins. The WebUI diagnostics page can't show loaded plugins.

### Required Change

Add `PluginRuntimeSnapshot` struct containing loaded plugin count, enabled
plugins, and any plugin-specific status messages. Thread through assembler
and WebUI projection.

### Impact Radius

Snapshot assembler, WebUI projection, diagnostics page. Additive change, low
complexity.

---

## SOFT ISSUE 9 — AppRenderTarget Interface Assumes Fixed Page Set

**Severity:** Complicates Phase 1 (Skin Plugin)
**Files:** `src/app/app_renderer.h`, `src/app/app_projection_builder.h`

### Current State

`AppRenderTarget` exposes a fixed set of state queries (playback, EQ, library,
etc.) and `buildMainMenuProjection()`, `buildNowPlayingProjection()`, etc. are
free functions that each return a specific view type.

While this doesn't block skin plugins (the skin only changes colors/layouts
within existing pages), it does mean new page types can't be added by plugins.

### Assessment

This is **by design** — the plugin spec explicitly forbids plugins from adding
new page types or changing routing. The fixed page set is a feature, not a bug.
No change needed for Phase 1.

However, if future phases want plugin-contributed UI (e.g., a DSP plugin
providing a custom control panel), `AppRenderTarget` would need to support
dynamic page resolution. Defer until needed.

---

## SOFT ISSUE 10 — WebUI Theme Tokens are Duplicated from C++ Theme

**Severity:** Complicates Phase 1 (Skin Plugin)
**Files:** `src/webui/webui_theme_tokens.h`

### Current State

WebUI has its own hardcoded theme tokens that mirror `ui_theme.h`. When a skin
changes, both the C++ theme and the WebUI theme must be updated.

### Required Change

WebUI theme tokens should be served from the same `UiTheme` runtime struct
(serialized to JSON and served via the WebUI HTTP API). The WebUI reads its
CSS custom properties from the server at load time.

### Impact Radius

WebUI `index.html`, `webui_theme_tokens.h`, WebUI HTTP router. Low complexity
— additive endpoint for theme JSON.

---

## Issue-to-Phase Mapping

| Issue | Severity | Blocks Phase | Effort Estimate |
|-------|----------|--------------|-----------------|
| 1. Hardcoded theme | **Hard** | Phase 1 | Large (all pages) |
| 2. No layout templates | **Hard** | Phase 1 | Medium (page refactor) |
| 3. Hardcoded dispatcher | **Hard** | Phase 2 | Medium (Python + C++) |
| 4. No process lifecycle | **Hard** | Phase 2, 3 | Large (new module) |
| 5. Minimal manifest | **Hard** | Phase 0 | Small (one header) |
| 6. Settings state | Soft | Phase 0 | Small |
| 7. No DSP provider abstraction | Soft | Phase 4 | Medium |
| 8. No plugin snapshot | Soft | Phase 0 | Small |
| 9. Fixed page set | By design | — | None |
| 10. Duplicated WebUI theme | Soft | Phase 1 | Small |

## Recommended Resolution Order

```
Phase 0: Fix #5 (manifest) + #6 (settings) + #8 (snapshot)
         → Foundation ready, nothing else changes.

Phase 1: Fix #1 (theme) + #2 (layouts) + #10 (WebUI theme)
         → Skin plugins work. Largest refactor but mechanical.

Phase 2: Fix #3 (dispatcher) + #4 (process lifecycle)
         → Remote plugins work. New PluginRuntime module.

Phase 3: Extend #4 patterns to metadata pipeline.
         → Metadata/lyrics plugins work.

Phase 4: Fix #7 (DSP provider abstraction).
         → Audio effect plugins work.
```
