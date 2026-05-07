<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Plugin System Specification

## 1. Purpose

This document defines the LoFiBox Zero plugin system — a four-family extension
model that allows users and community developers to extend the product without
breaking its architectural boundaries.

The plugin system is **not** a general-purpose extension framework. Plugins
operate within fixed extension points and cannot cross the product's core
boundaries.

## 2. Design Principles

1. **Plugin provides "how to draw / how to fetch / how to process"; core
   decides "what to draw / what to fetch / what to process."**
2. Plugins must not access AppState, Canvas, Presenter, playback queue, or
   credential store directly.
3. The plugin manager knows about plugin lifecycle but NOT about remote, DSP,
   metadata, or UI business logic.
4. C++ ABI is never a public plugin contract. External plugins communicate via
   well-defined protocols (manifest files for assets, stdio JSON-RPC for
   helpers, internal C++ interfaces for built-in providers).
5. Plugin crashes must not crash the main process.

## 3. Plugin Taxonomy

### 3.1 Four Capability Families

```
ui.theme            — colors, fonts, icons, backgrounds, spacing
ui.icons            — icon set overrides
ui.layout.*         — page layout templates
    ui.layout.now_playing
    ui.layout.library
    ui.layout.eq
    ui.layout.lyrics

audio.effect        — built-in DSP effect registration
audio.effect.realtime
audio.effect.offline
audio.effect.visualizer

remote.source       — remote media repository provider
remote.browse
remote.search
remote.stream
remote.auth.profile

metadata.reader     — tag / embedded metadata extraction
metadata.enricher   — online metadata enrichment (MusicBrainz, AcoustID, ...)
metadata.lyrics     — lyrics lookup
metadata.artwork    — cover art / fanart lookup
metadata.fingerprint — acoustic fingerprint / track identity
metadata.tag_writer — metadata writeback (future)
```

### 3.2 Four Plugin Kinds

| Kind              | Description                              | Runtime Level |
|-------------------|------------------------------------------|---------------|
| `asset_pack`      | Skin/theme/font/icon/layout resources     | Level 0       |
| `external_helper` | Python/shell process via stdio JSON-RPC   | Level 1       |
| `internal_provider` | Compiled-in C++ effect/source provider  | Level 2       |
| *(none yet)*      | Arbitrary .so loaded into main process    | Level 3 (NOT planned) |

## 4. Runtime Levels

### Level 0 — Manifest / Asset Plugin

- For UI skins, icons, fonts, layouts, DSP presets.
- No executable code. Just JSON manifests + resource files.
- Safe, portable, cannot crash main process.
- Loaded by reading `plugin.json` from plugin directory.

### Level 1 — External Helper Plugin

- For remote sources, lyrics providers, metadata enrichers.
- Independent process (Python, shell, any language).
- Communicates via `stdio` JSON-RPC (protocol: `lofibox-jsonrpc-1`).
- Main process manages lifecycle: spawn, timeout (default 30s per request),
  restart on crash (max 3 in 60s), disable on repeated failure.
- stderr captured to plugin-specific log file.

### Level 2 — Native Internal Provider

- For high-performance DSP effects and core built-in providers.
- Compiled into LoFiBox binary via C++ provider interface.
- Only for official/built-in modules. NOT exposed as a user plugin mechanism.
- Registered at startup via `PluginRegistry::registerPlugin()`.

### Level 3 — NOT Planned

- No arbitrary `.so` loading into main process.
- No plugin access to AppState, Canvas, Presenter, playback queue, or
  credential store.
- No plugin control over UI routing or playback state machines.

## 5. Plugin Manifest Schema

### 5.1 Schema (schema_version 1)

```json
{
  "schema_version": 1,
  "id": "community.remote.navidrome",
  "name": "Navidrome Provider",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "external_helper",
  "entry": {
    "command": "python3",
    "args": ["provider.py"],
    "protocol": "lofibox-jsonrpc-1"
  },
  "capabilities": [
    "remote.source",
    "remote.browse",
    "remote.search",
    "remote.stream",
    "remote.auth.profile"
  ],
  "runtime_dependencies": [
    "python3",
    "python3-requests"
  ],
  "permissions": [
    "network.client",
    "credential.read.profile"
  ],
  "resources": {
    "icons": "icons/",
    "schema": "profile.schema.json"
  }
}
```

### 5.2 Field Definitions

| Field | Required | Description |
|-------|----------|-------------|
| `schema_version` | yes | Manifest format version (integer, currently `1`) |
| `id` | yes | Reverse-domain unique plugin identifier |
| `name` | yes | Human-readable display name |
| `version` | yes | Plugin version (semver) |
| `api_version` | yes | Plugin API version this plugin targets |
| `kind` | yes | `asset_pack`, `external_helper`, or `internal_provider` |
| `entry` | for `external_helper` | Command, args, and protocol for launching helper |
| `capabilities` | yes | Array of capability namespace strings |
| `runtime_dependencies` | no | System packages or runtimes needed |
| `permissions` | no | Runtime capability declarations for packaging review |
| `resources` | no | Paths to plugin-relative resource directories |

### 5.3 Permission Namespace

Permissions are **runtime capability declarations** for packaging review and
user visibility — NOT a user/role access control system.

```
network.client          — outbound HTTP/HTTPS
network.server          — listen on ports
credential.read.profile — read stored remote source credentials
credential.write        — write credentials
storage.read.library    — read local library files
storage.write.cache     — write to plugin cache directory
storage.write.tag       — write metadata tags to media files
audio.capture           — access audio input
process.subprocess      — spawn child processes
```

### 5.4 Capability Namespace (Normative)

```
# UI
ui.theme
ui.icons
ui.layout.now_playing
ui.layout.library
ui.layout.eq
ui.layout.lyrics

# Audio / DSP
audio.effect
audio.effect.realtime
audio.effect.offline
audio.effect.visualizer

# Remote Sources
remote.source
remote.browse
remote.search
remote.stream
remote.auth.profile

# Metadata
metadata.reader
metadata.enricher
metadata.lyrics
metadata.artwork
metadata.fingerprint
metadata.tag_writer
```

## 6. Extension Point Architecture

### 6.1 Overview

```
                       ┌─────────────────────────┐
                       │       plugin.json        │
                       └───────────┬─────────────┘
                                   │
                       ┌───────────▼─────────────┐
                       │   Plugin Discovery       │
                       └───────────┬─────────────┘
                                   │
                       ┌───────────▼─────────────┐
                       │   Plugin Registry        │
                       └───────────┬─────────────┘
                                   │
      ┌────────────────────────────┼────────────────────────────┐
      │                            │                            │
┌─────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│Skin Adapter  │          │Remote Adapter    │        │Metadata Adapter  │
└─────┬────────┘          └────────┬─────────┘        └─────────┬────────┘
      │                            │                            │
┌─────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│App Projection│          │Remote Registry   │        │Metadata Pipeline │
└─────┬────────┘          └────────┬─────────┘        └─────────┬────────┘
      │                            │                            │
┌─────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│Canvas Render │          │Playback Source   │        │Merge Governance  │
└──────────────┘          └──────────────────┘        └──────────────────┘

┌──────────────┐
│DSP Adapter   │
└─────┬────────┘
      │
┌─────▼────────┐
│DSP Chain     │
└─────┬────────┘
      │
┌─────▼────────┐
│Audio Output  │
└──────────────┘
```

### 6.2 UI Skin Plugin (Level 0)

**What a skin can replace:**
- Color palette (background, panel, primary, secondary, accent, semantic)
- Font faces and sizes
- Icon set
- Page layout templates (Now Playing card style, EQ slider style, lyrics
  display mode, list row style)
- Component spacing and sizing

**What a skin CANNOT replace:**
- Playback state model
- Page routing logic
- Input behavior
- Playback commands
- Remote source semantics
- Queue semantics

**Skin manifest** (`skin.json` within the plugin directory):

```json
{
  "screen": {
    "target_width": 320,
    "target_height": 170
  },
  "palette": {
    "background": "#08090D",
    "panel": "#11131A",
    "primary": "#F3C56B",
    "secondary": "#9B8F73",
    "accent": "#DFA24A",
    "danger": "#D66A4A"
  },
  "pages": {
    "now_playing": {
      "layout": "compact-cover-left",
      "show_album_art": true,
      "show_spectrum": true,
      "show_source_badge": true,
      "title_max_lines": 1,
      "artist_max_lines": 1
    },
    "eq": {
      "layout": "ten-band-thin-slider",
      "band_label_mode": "minimal"
    },
    "lyrics": {
      "layout": "center-active-line",
      "inactive_lines": 2
    }
  }
}
```

**Hookup point:** Between AppProjection and Canvas Renderer. The skin
determines colors, fonts, and layout templates used by the projection builder.
Data still comes from the unified RuntimeSnapshot.

### 6.3 Audio Effect Plugin

**Two tiers:**

**Tier 1 — Built-in DSP Plugin (Level 2):**
- Compiled into LoFiBox via `AudioEffectProvider` C++ interface.
- Registers effect descriptor + factory.
- DSP chain queries available effects by capability.
- Effects are parametric (config in manifest JSON), no foreign code in
  process.

**Tier 2 — External DSP Plugin (Level 1, future):**
- LADSPA / LV2 bridge via helper process.
- PCM pipe to external process.
- Strict latency and stability bounds enforced by host.

**Effect manifest** (in `plugin.json`):

```json
{
  "id": "io.github.vicliu624.lofibox.effect.lofi",
  "name": "LoFi",
  "kind": "internal_provider",
  "capabilities": ["audio.effect", "audio.effect.realtime"],
  "effect": {
    "type": "dsp_chain",
    "nodes": [
      { "type": "lowpass", "cutoff_hz": 7200 },
      { "type": "bitcrush", "bits": 12, "mix": 0.35 },
      { "type": "wow_flutter", "depth": 0.12 },
      { "type": "soft_clip", "drive": 1.4 }
    ]
  }
}
```

**Boundary rules:**
- Must not block the audio callback.
- Must not allocate memory, perform I/O, or make network requests in the
  real-time thread.
- Must not control playback queue.
- Must not access remote credentials.
- Must not write metadata.

### 6.4 Remote Source Plugin (Level 1)

**Hookup point:** Host runtime → Remote Provider Adapter → Plugin helper
process → JSON response.

**Required operations:**
- `profile_schema` — declare required configuration fields
- `test_connection` / `probe` — verify connectivity with given profile
- `browse` — enumerate catalog by parent node
- `search` — text search for tracks
- `resolve_stream` — resolve a track to a playable stream
- `resolve_artwork` — resolve artwork URL for a track/album

**JSON-RPC protocol** (`lofibox-jsonrpc-1`):

Request (over stdin, one JSON object per line):
```json
{
  "method": "browse",
  "params": {
    "profile_id": "home-navidrome",
    "path": "/albums"
  }
}
```

Response (over stdout, one JSON object per line):
```json
{
  "items": [
    {
      "type": "album",
      "id": "album:123",
      "title": "Kind of Blue",
      "subtitle": "Miles Davis",
      "artwork_url": "plugin://navidrome/artwork/album/123"
    }
  ]
}
```

**`profile_schema` response:**
```json
{
  "method": "profile_schema",
  "result": {
    "fields": [
      { "name": "base_url", "type": "url", "required": true },
      { "name": "username", "type": "string", "required": true },
      { "name": "password", "type": "secret", "required": true }
    ]
  }
}
```

This allows WebUI to render configuration forms for unknown providers without
knowing provider-specific logic.

**Boundary rules:**
- Plugins receive only the profile + session they need for a request.
- Plugins never receive the full credential store.
- Plugins map external data into shared `RemoteCatalogNode` / `RemoteTrack` /
  `ResolvedRemoteStream` models.

### 6.5 Metadata Plugin (Level 1)

**Pipeline:**
```
Local File
  → embedded tag reader
  → filename/path heuristic
  → fingerprint / identity
  → metadata enrichers (plugin-provided)
  → artwork providers (plugin-provided)
  → lyrics providers (plugin-provided)
  → merge policy
  → governed metadata result
```

**Required operations:**
- `lookup_metadata` — given track identity, return enrichment candidates
- `lookup_artwork` — given track identity, return artwork candidates
- `lookup_lyrics` — given track identity, return lyrics candidates
- `lookup_fingerprint` — given audio fingerprint, return identity match

**Metadata candidate format:**
```json
{
  "track_id": "local:/Music/a.flac",
  "candidates": [
    {
      "title": "Blue in Green",
      "artist": "Miles Davis",
      "album": "Kind of Blue",
      "confidence": 0.93,
      "source": "musicbrainz"
    }
  ]
}
```

**Boundary rules:**
- Plugins provide **candidates**, not final values.
- `MetadataMergePolicy` and `MetadataGovernanceService` determine what is
  adopted.
- Existing rules take priority: embedded tags > high-confidence online >
  filename heuristics > placeholder.
- User manual edits always have highest priority.
- Tag writeback is deferred (no plugin writes to media files in Phase 1).

## 7. Plugin Manager Architecture

### 7.1 Module Structure

```
src/plugins/
    plugin_manifest.h           — PluginManifest struct + PluginRegistry
    plugin_discovery.h          — Filesystem scanner for plugin directories
    plugin_loader.h             — Manifest parser (JSON → PluginManifest)
    plugin_capability_index.h   — Capability → plugin lookup
    plugin_runtime.h            — External helper lifecycle management
    plugin_settings_store.h     — Enabled/disabled state, priority, selection
```

### 7.2 Responsibilities

| Module | Responsibility |
|--------|---------------|
| `PluginDiscovery` | Scan system and user plugin directories for `plugin.json` |
| `PluginLoader` | Parse `plugin.json`, validate schema, return `PluginManifest` |
| `PluginRegistry` | Store and deduplicate manifests, lookup by ID |
| `PluginCapabilityIndex` | Index plugins by capability, query by capability set |
| `PluginRuntime` | Spawn/monitor/kill external helpers, timeout, crash recovery, stderr logging |
| `PluginSettingsStore` | Persist enabled/disabled, selected skin, provider order to `~/.config/lofibox/plugins.json` |

### 7.3 Business Adapters

The plugin manager delegates domain-specific integration to adapters:

- `SkinPluginAdapter` — loads skin.json, resolves palette + layout into `UiTheme`
- `RemotePluginAdapter` — bridges `PluginRuntime` to `RemoteSourceRegistry`
- `MetadataPluginAdapter` — bridges `PluginRuntime` to metadata pipeline
- `DspPluginAdapter` — bridges `PluginRegistry` to DSP chain preset loading

## 8. Directory Layout

### 8.1 System-level plugins

```
/usr/share/lofibox/plugins/
    builtin-classic-dark/
        plugin.json
        skin.json
        icons/
        fonts/
    builtin-navidrome/
        plugin.json
        provider.py
    builtin-jellyfin/
        plugin.json
        provider.py
```

### 8.2 User-level plugins

```
~/.local/share/lofibox/plugins/
    my-theme/
        plugin.json
        skin.json
    my-lyrics-provider/
        plugin.json
        provider.py
    my-webdav-provider/
        plugin.json
        provider.py
```

### 8.3 Plugin cache

```
~/.cache/lofibox/plugins/
    <plugin-id>/
        artwork/
        metadata/
        lyrics/
```

### 8.4 Plugin configuration

```
~/.config/lofibox/plugins.json
```

```json
{
  "enabled": [
    "io.github.vicliu624.lofibox.theme.classic-dark",
    "community.lyrics.my-provider"
  ],
  "disabled": [
    "community.remote.experimental-sftp"
  ],
  "selected_skin": "io.github.vicliu624.lofibox.theme.classic-dark",
  "metadata_order": [
    "embedded",
    "musicbrainz",
    "acoustid",
    "community.lyrics.my-provider"
  ]
}
```

## 9. GUI / WebUI / CLI Integration

Plugins do NOT directly solve GUI/WebUI synchronization. All state
synchronization goes through the runtime event and snapshot system:

```
WebUI command
  → RuntimeCommandBus
  → PlaybackRuntime / SettingsRuntime / QueueRuntime
  → RuntimeEvent
  → RuntimeSnapshot
  → GUI projection
  → Now Playing redraw
```

Plugin-triggered changes flow through the same path:

```
Skin change    → settings runtime event → GUI + WebUI use new skin
DSP preset     → DSP runtime event      → Now Playing shows current effect
Remote source  → source profile event   → GUI + WebUI source list sync
Metadata update → library/runtime event → Now Playing re-displays title/lyrics/artwork
```

Plugins do not need to know about GUI or WebUI.

## 10. Implementation Phases

### Phase 0 — Plugin Manifest + Discovery + Registry (Foundation)

**Goal:** Scan plugin directories, parse plugin.json, index by capability,
enable/disable, display in diagnostics.

**Deliverables:**
- `PluginManifest` struct upgrade (all fields from §5)
- `PluginDiscovery` — scan system + user directories
- `PluginLoader` — parse + validate plugin.json
- `PluginCapabilityIndex` — capability lookup
- `PluginSettingsStore` — persistence of enabled/disabled state
- Diagnostics page shows loaded plugins

### Phase 1 — UI Skin Plugin

**Goal:** Replace colors, icons, fonts, and page layouts via skin plugins.

**Deliverables:**
- Runtime `UiTheme` struct (colors, fonts, spacing) replacing `ui_theme.h` constexpr
- `SkinPluginAdapter` — loads skin.json into `UiTheme`
- Layout template system for Now Playing, EQ, Lyrics pages
- Font loader (multiple bitmap fonts from skin)
- Icon loader (PNG from skin directory)

### Phase 2 — Remote Source Plugin

**Goal:** External remote source providers via stdio JSON-RPC helpers.

**Deliverables:**
- `PluginRuntime` — external process lifecycle management
- `RemotePluginAdapter` — bridges helper processes to `RemoteSourceRegistry`
- JSON-RPC protocol implementation (encode/decode)
- `profile_schema` support in WebUI remote setup
- Migrate existing Python providers to plugin layout

### Phase 3 — Metadata / Lyrics Plugin

**Goal:** Pluggable metadata enrichment, lyrics, and artwork providers.

**Deliverables:**
- `MetadataPluginAdapter` — bridges helpers to metadata pipeline
- Candidate-based metadata contribution model
- Integration with existing `MetadataMergePolicy` and `MetadataGovernanceService`
- Lyrics provider plugin support

### Phase 4 — DSP Plugin

**Goal:** Pluggable DSP effects (presets first, external later).

**Deliverables:**
- `AudioEffectProvider` C++ interface
- `DspPluginAdapter` — effect registration and discovery
- DSP preset from plugin manifest
- LADSPA/LV2 bridge (deferred to Phase 4b)

## 11. Backwards Compatibility

- Existing Python remote providers (`src/remote/`) continue to work during
  migration. They will be adapted to the plugin layout incrementally.
- Existing hardcoded theme (`ui_theme.h`) remains the default fallback when
  no skin plugin is selected.
- Existing DSP chain and EQ presets remain available as built-in providers.
- The `PluginManifest` struct is extended, not replaced — existing fields
  remain.

## 12. Normative References

- [Plugin And Provider System Specification](plugin-provider-system-spec.md)
- [Runtime Command And Session Architecture](runtime-command-session-architecture-spec.md)
- [LoFiBox Zero Audio DSP Specification](lofibox-zero-audio-dsp-spec.md)
- [LoFiBox Zero Visual Design Specification](lofibox-zero-visual-design-spec.md)
- [LoFiBox Zero Streaming Specification](lofibox-zero-streaming-spec.md)
- [LoFiBox Zero Track Identity Specification](lofibox-zero-track-identity-spec.md)
