<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero — WebUI Design Specification

## Status: Implemented

## 1. Positioning

WebUI is a **remote GUI expression layer** — a runtime client that consumes `RuntimeSnapshot` / `RuntimeEvent` and submits `RuntimeCommand` through the existing `RuntimeCommandClient` interface. It is NOT a second player, admin backend, or new feature layer.

| Is | Is Not |
|----|--------|
| Runtime command client | GUI runtime host |
| Runtime snapshot / event consumer | App controller |
| Remote projection layer | Playback controller |
| Read-only + command surface | Admin / management backend |
| Single-file SPA (no build step) | New service layer |

## 2. Dependency Boundary

```
Browser WebUI (SPA)
    |
HTTP (GET / POST) + WebSocket (RFC 6455 text frames)
    |
WebUiServer (POSIX socket accept loop, background thread)
    |
WebUiHttpRouter (route dispatch + command JSON parsing)
WebUiWsRuntimeStream (WebSocket upgrade + event poll loop)
    |
WebUiRuntimeAdapter (sole RuntimeCommandClient contact point)
    |
RuntimeCommandClient (InProcessRuntimeCommandClient)
    |
RuntimeCommandServer / RuntimeCommandBus
    |
RuntimeSessionFacade → Playback / Queue / EQ / Remote / Settings Runtime
```

### MUST depend on:
- `runtime::RuntimeCommandClient` (virtual interface)
- `runtime::RuntimeSnapshot` (flat struct, 11 sub-sections)
- `runtime::RuntimeEvent` (enum kind + version + snapshot + metadata)
- `runtime::RuntimeCommand` (enum kind + variant payload + origin + correlation_id)
- `runtime::RuntimeCommandResult` (accepted / applied / code / message / version tracking)
- `runtime::runtimeEventsBetween()` (diff two snapshots → event vector)
- `runtime::runtimeEventKindName()` (enum → string for JSON)

### MUST NOT depend on:
- `app::AppRuntimeContext`
- `app::LoFiBoxApp` (except for composition at the target main level)
- `ui::pages/*`
- `ui::ui_primitives`
- `playback::PlaybackController`
- `runtime::RuntimeSessionFacade`
- `runtime::RuntimeCommandBus`
- `platform::host::*` concrete backends
- `remote::*` concrete providers

The dependency direction is strictly one-way: `WebUI → RuntimeClient → RuntimeCommandBus → Runtime`. GUI and WebUI synchronize through shared `RuntimeSnapshot`, not direct callbacks.

## 3. Pages

The frontend is a single-file SPA (`assets/webui/index.html`) with embedded CSS and JavaScript. Five tabs, navigated via a fixed bottom tab bar with inline SVG icons:

| Tab | DOM ID | Content |
|-----|--------|---------|
| Now Playing | `page-now-playing` | Track title, artist, album, source label, 6px progress bar (tap-to-seek), large 56px play/pause circle, 44px prev/next/shuffle/repeat buttons, spectrum visualization canvas (80px), horizontal swipe for prev/next |
| Queue | `page-queue` | Scrollable queue list with item count heading, full-width rows (min 48px) with active accent border, tap to jump (`QueueJump`), duration right-aligned |
| Library | `page-library` | Search input bar (UI placeholder), stats pills (status, track/album/artist/genre counts), degraded-state warning banner |
| EQ | `page-eq` | Custom vertical sliders (28px thumb, 3px rail, `touch-action: none`), preset cycling with prev/next buttons, enable toggle pill, Reset button, 50ms throttle on commands, responsive slider height 140-180px |
| More | `page-more` | Merged view combining Sources, Settings, and Diagnostics. Settings rows (output mode, network policy, sleep timer), Sources info rows (active source, connection, stream resolved, profile, buffer, codec), Diagnostics pills (Runtime/Audio/Library/Remote/Cache OK/ERR), backend/output/track count info, warnings and errors lists |

The Sources tab and standalone Settings/Diagnostics tabs have been merged into a single "More" page with visual section headings. This reduces bottom-nav clutter and fits comfortably on mobile screens (5 tabs, no overflow).

Pages NOT included: Creator, Lyrics (full page), DSP profiles, Help, Command Palette. Lyrics and visualization data are embedded in the Now Playing JSON response for potential future use.

## 4. API Design

### 4.1 HTTP Endpoints

All responses include CORS headers (`Access-Control-Allow-Origin: *`).

| Method | Path | Request | Response |
|--------|------|---------|----------|
| `GET` | `/` | — | `text/html` — embedded `index.html` SPA |
| `GET` | `/index.html` | — | Same as `/` |
| `GET` | `/api/runtime/snapshot` | — | `application/json` — full `buildFullSnapshotJson()` output |
| `POST` | `/api/runtime/commands` | `{"kind":"webui.<Action>","origin":"webui","correlation_id":"web-...", ...params}` | `application/json` — `{"accepted":bool,"applied":bool,"code":"...","message":"...","version_before":N,"version_after":N}` |
| `OPTIONS` | `*` | — | `204 No Content` — CORS preflight with allowed methods/headers |

There are no per-section snapshot endpoints. The client fetches `/api/runtime/snapshot` on initial load and every 5 seconds as a polling fallback. Real-time updates come via WebSocket.

HTML, CSS, and JavaScript are all embedded in a single `index.html` served from a compile-time C++ raw string literal (`webUiIndexHtml()`). There is no `assets/` sub-path routing and no filesystem-served static directory.

### 4.2 WebSocket

```
ws://<host>:<port>/api/runtime/events
```

The server detects `Upgrade: websocket` in the request headers and performs an RFC 6455 handshake using SHA-1 + Base64 for the `Sec-WebSocket-Accept` key.

After upgrade, the server runs an event poll loop (~250ms interval):
1. Call `adapter_.pollEvents()` — diffs previous vs current snapshot via `runtimeEventsBetween()`
2. Each resulting `RuntimeEvent` is serialized with `buildEventJson()` and sent as a WebSocket text frame (FIN + opcode 0x1, unmasked server→client)
3. On socket error or `stop()`, a close frame (opcode 0x8) is sent and the socket is closed

**Event JSON format** (single frame) — CRITICAL: `buildEventJson()` writes
playback fields directly inside `"snapshot"` (no `"playback"` wrapper key).
The frontend MUST read `msg.snapshot.status`, `msg.snapshot.current_track_id`,
etc. — NOT `msg.snapshot.playback.*`.  Any mismatch here silently breaks all
event-driven state updates.

```json
{
  "event": "playback.changed",
  "snapshot": {
    "status": "playing",
    "audio_active": true,
    "current_track_id": 42,
    "title": "Bohemian Rhapsody",
    "artist": "Queen",
    "album": "A Night at the Opera",
    "album_artist": "Queen",
    "source_label": "Local",
    "source_type": "LOCAL",
    "elapsed_seconds": 42.5,
    "elapsed_ms": 42500,
    "duration_seconds": 355,
    "duration_ms": 355000,
    "seekable": true,
    "live": false,
    "volume_percent": 75,
    "muted": false,
    "codec": "FLAC",
    "bitrate_kbps": 900,
    "sample_rate_hz": 44100,
    "bit_depth": 16,
    "shuffle_enabled": false,
    "repeat_all": false,
    "repeat_one": false,
    "error_code": "",
    "error_message": ""
  },
  "stream_version": 48,
  "elapsed_seconds": 42.5,
  "duration_seconds": 355,
  "current_index": 2,
  "text": "",
  "code": "",
  "message": "",
  "timestamp_ms": 1714938000000
}
```

> **Test Enforcement**: The smoke test `tests/webui_smoke.cpp`
> `test_projection_event()` asserts the serialized JSON structure.
> If this format changes, both the C++ projection AND the frontend event
> handler must be updated together — or the WebSocket stream becomes dead air.

Event kinds (serialized names): `playback.changed`, `playback.progress`, `playback.error`, `queue.changed`, `eq.changed`, `remote.changed`, `settings.changed`, `lyrics.changed`, `lyrics.line_changed`, `visualization.frame`, `library.scan_started`, `library.scan_progress`, `library.scan_completed`, `diagnostics.changed`, `creator.changed`, `runtime.snapshot`, `runtime.connected`, `runtime.disconnected`.

### 4.3 Snapshot JSON Format

The full snapshot (`GET /api/runtime/snapshot`) returns all sections in a flat top-level object:

```json
{
  "playback": {
    "status": "playing", "audio_active": true,
    "title": "...", "artist": "...", "album": "...", "album_artist": "...",
    "source_label": "...", "source_type": "...",
    "elapsed_seconds": 42.5, "elapsed_ms": 42500,
    "duration_seconds": 355, "duration_ms": 355000,
    "seekable": true, "live": false,
    "volume_percent": 75, "muted": false,
    "codec": "FLAC", "bitrate_kbps": 900, "sample_rate_hz": 44100, "bit_depth": 16,
    "shuffle_enabled": false, "repeat_all": false, "repeat_one": false,
    "error_code": "", "error_message": ""
  },
  "queue": {
    "count": 12, "active_index": 2, "selected_index": 2,
    "shuffle_enabled": false, "repeat_all": false, "repeat_one": false,
    "items": [
      {"track_id": 1042, "queue_index": 0, "title": "...", "artist": "...", "album": "...", "source_label": "...", "duration_seconds": 240, "active": false, "playable": true}
    ]
  },
  "eq": {
    "enabled": true, "preset_name": "Rock",
    "bands": [2, 1, 0, -1, 0, 2, 3, 1, 0, -2]
  },
  "lyrics": { "available": false, "synced": false, "lines": [], ... },
  "visualization": { "available": false, "bands": [], "peaks": [], ... },
  "remote": { "profile_id": "", "connection_status": "UNKNOWN", ... },
  "library": { "ready": true, "degraded": false, "track_count": 1500, "album_count": 120, "artist_count": 80, "genre_count": 42, "status": "READY" },
  "sources": { "configured_count": 2, "active_source_label": "Local", ... },
  "settings": { "output_mode": "DEFAULT", "network_policy": "DEFAULT", "sleep_timer": "OFF" },
  "diagnostics": {
    "runtime_ok": true, "audio_ok": true, "library_ok": true, "remote_ok": true, "cache_ok": true,
    "audio_backend": "ALSA", "output_device": "default",
    "library_track_count": 1500, "failed_scan_count": 0,
    "warnings": [], "errors": []
  },
  "version": 42
}
```

### 4.4 Library Content API

When `WebUiServer::setLibraryQueryProvider()` and/or `setLibraryEnrichProvider()` have been called during startup, additional endpoints become available. Both provider interfaces are abstract and live in `lofibox_zero_core`; concrete adapters are injected by `device_main.cpp` / `x11_main.cpp` via the `AppStarter` callback. No `#if defined` guards exist in core — the dependency direction is `lofibox_webui → lofibox_zero_core`.

**Architecture:**
```
WebUiHttpRouter
  ├── LibraryQueryProvider*   (abstract, in core)
  │     └── LibraryQueryProviderAdapter (in application/, header-only)
  │           ├── LibraryQueryService (index + find)
  │           └── ArtworkProvider (ffmpeg extraction + remote download)
  └── LibraryEnrichProvider*   (abstract, in core)
        └── LibraryEnrichProviderAdapter (in application/, header-only)
              ├── CacheManager (Metadata bucket, 30-day TTL)
              └── curl → Wikipedia API
```

| Method | Path | Response |
|--------|------|----------|
| `GET` | `/api/library/tracks` | `application/json` — array of `{id, title, artist, album, genre, duration_seconds, play_count}` |
| `GET` | `/api/library/albums` | `application/json` — array of `{name, artist, track_count}` |
| `GET` | `/api/library/artist/{name}` | `application/json` — `{artist, summary, source}` or 404 |
| `GET` | `/api/library/album/{artist}/{album}` | `application/json` — `{album, artist, summary, source, genres, year}` or 404 |
| `GET` | `/api/artwork/{track_id}` | `image/png` binary + `Cache-Control: public, max-age=86400` or 404 |

Artist and album names in paths are URL-encoded; the router percent-decodes before querying.

#### 4.4.1 Artwork Data Flow

**Local tracks:**
```
TrackRecord.path (real filesystem path)
       ↓
FfmpegArtworkProvider::read(path)
       ↓ embedded/directory/online extraction
appCacheDir()/artwork/<cacheKeyForPath(path)>.png
       ↓
Adapter readBinaryFile() → image/png bytes
```

**Remote tracks (Emby / Jellyfin / OpenSubsonic):**
```
Python remote provider
       ↓ extracts tokenless artwork URL
RemoteTrack.artwork_url = "https://server/Items/X/Images/Primary?maxWidth=320"
       ↓ stored via LibraryController
TrackRecord.artwork_url
       ↓
Adapter getArtworkPng():
  1. cache key = "remote-artwork-" + profile_id + "-" + track_id
  2. Check appCacheDir()/artwork/<key>.png → hit: return bytes
  3. Miss → artwork_->readRemoteIdentity(key, path, AllowOnline, artwork_url)
         → downloadUrlToPngFile() downloads via curl → writes cache
  4. readBinaryFile(cache_path) → return bytes or 404
```

The stable cache key is `"remote-artwork-{profile_id}-{track_id}"`. This matches the `stable_cache_key` parameter passed to `ArtworkProvider::readRemoteIdentity()`, ensuring the adapter can find files written by the provider. The Python sync step is NOT responsible for downloading artwork — the adapter triggers download on first WebUI request, and subsequent requests hit the cache.

#### 4.4.2 Enrichment Data Flow

```
Wikipedia API (explaintext extracts)
       ↓ curl, rate-limited (200ms min interval)
CacheManager::putText(Metadata, key, extract, 30-day TTL)
       ↓
Adapter getArtistInfo / getAlbumInfo
       ↓ cache hit → instant; miss → network → cache → return
WebUiHttpRouter → JSON response
```

Enrichment providers are optional — when absent, enrich endpoints return 404.

## 5. Command Mapping

The WebUI frontend sends POST bodies with `kind: "webui.<Action>"` plus payload parameters. The `WebUiHttpRouter::parseWebUiCommand()` method strips the `webui.` prefix, maps the action to a `RuntimeCommandKind`, and builds the corresponding `RuntimeCommandPayload`.

| WebUI Action | RuntimeCommandKind | Payload | Frontend Trigger |
|---|---|---|---|
| `Toggle` | `PlaybackToggle` | `empty()` | Space key, Play/Pause button |
| `Previous` | `QueueStep` | `queueStep(-1)` | ArrowLeft, Prev button |
| `Next` | `QueueStep` | `queueStep(1)` | ArrowRight, Next button |
| `ToggleShuffle` | `PlaybackToggleShuffle` | `empty()` | Shuffle button |
| `CycleRepeat` | `PlaybackCycleRepeat` | `empty()` | Repeat button |
| `QueueJump` | `QueueJump` | `queueIndex(queue_index)` | Click queue item |
| `EqSetBand` | `EqSetBand` | `eqSetBand(eq_band_index, eq_gain_db)` | EQ slider change |
| `EqAdjustBand` | `EqAdjustBand` | `eqAdjustBand(eq_band_index, eq_gain_delta)` | (available, not wired in UI) |
| `EqCyclePreset` | `EqCyclePreset` | `eqCyclePreset(delta)` | Next Preset button |
| `EqReset` | `EqReset` | `empty()` | Reset button |
| `Seek` | `PlaybackSeek` | `seek(seconds)` | (available, not wired in UI) |

Commands use `CommandOrigin::Automation`. Each command carries a `correlation_id` of the form `web-<unix_timestamp_ms>`.

Commands must NOT simulate GUI page operations — no `POST /api/gui/click-*`, no `POST /api/page/*`.

## 6. GUI ↔ WebUI Synchronization

```
WebUI clicks Pause → HTTP POST /api/runtime/commands {"kind":"webui.Toggle",...}
    |
WebUiHttpRouter parses command → RuntimeCommand{PlaybackToggle}
    |
WebUiRuntimeAdapter::submitCommand() → InProcessRuntimeCommandClient::dispatch()
    |
RuntimeCommandServer → RuntimeCommandBus serial execution
    |
PlaybackRuntime state change → RuntimeSnapshot version increments
    |
[HTTP response returns immediately with accepted/applied]
[WebSocket poll loop detects snapshot delta → runtimeEventsBetween()]
    |
WebSocket pushes RuntimeEvent JSON frames to all connected clients
    |
GUI Now Playing refreshes (reads same RuntimeSnapshot)
WebUI Now Playing refreshes (receives WebSocket event or polling fallback)
CLI/TUI/desktop sync-refresh
```

GUI Now Playing MUST only display from `RuntimeSnapshot` fields — never from GUI key-press-derived cached state, state pushed from WebUI, page-local `currentTrack`, or reverse-engineered queue row text.

## 7. Source Layout

```
src/webui/
  webui_json.h                 — JSON building helpers (appendString, appendBool, separator, etc.)
  webui_json.cpp               — Implementation + HTTP response builders (200/400/404/405/500)
  webui_config.h               — WebUiConfig struct + CLI/env parsing declarations
  webui_config.cpp             — --webui, --webui-bind, --webui-port + LOFIBOX_WEBUI* env vars
  webui_theme_tokens.h         — CSS custom properties (:root block), header-only
  webui_runtime_adapter.h      — WebUiRuntimeAdapter: sole RuntimeCommandClient contact point
  webui_runtime_adapter.cpp    — querySnapshot(), submitCommand(), pollEvents()
  webui_projection.h           — 9 JSON DTO builder declarations
  webui_projection.cpp         — buildNowPlayingJson, buildQueueJson, ..., buildEventJson
  webui_static_assets.h        — webUiIndexHtml(), mimeTypeForPath(), assetContentForPath()
  webui_static_assets.cpp      — Embedded index.html as raw C++ string literal
  webui_http_router.h          — WebUiHttpRouter: route dispatch + command parsing
  webui_http_router.cpp        — handleRequest(), parseWebUiCommand(), action→command mapping
  webui_ws_runtime_stream.h    — WebUiWsRuntimeStream: WebSocket upgrade + event push loop
  webui_ws_runtime_stream.cpp  — RFC 6455 handshake, SHA-1, Base64, frame send/close
  webui_server.h               — WebUiServer: POSIX socket accept loop, background thread
  webui_server.cpp             — socket/bind/listen/accept, connection dispatch

assets/webui/
  index.html                   — Single-file SPA (~400 lines, embedded CSS + JS)

tests/
  webui_smoke.cpp              — Config, JSON helpers, projection, command parsing, adapter, router tests
```

### Responsibility Boundaries

- **webui_json**: Zero-dependency JSON building. Functions do NOT prepend commas — callers use `separator()` between fields to produce valid JSON for browser `JSON.parse()`.
- **webui_http_router**: Route dispatch only — `GET /` → HTML, `GET /api/runtime/snapshot` → JSON, `POST /api/runtime/commands` → parse + submit + result. Does not touch playback/queue/EQ directly. Also handles CORS OPTIONS preflight.
- **webui_runtime_adapter**: ONLY place allowed to call `RuntimeCommandClient`. Offers `querySnapshot()`, `submitCommand()`, `pollEvents()`. Maintains a previous/current snapshot pair for event diffing. Tracks connection health and last error.
- **webui_projection**: Maps `RuntimeSnapshot` sections → structured JSON DTOs. Sibling to GUI page projection, not GUI row reuse. Builds self-contained JSON strings for each page + a full-snapshot aggregator + event serialization.
- **webui_ws_runtime_stream**: Manages one WebSocket connection. Performs RFC 6455 upgrade handshake (extracts `Sec-WebSocket-Key`, computes SHA-1 + Base64 accept), then loops on `adapter_.pollEvents()` sending JSON text frames. Handles close frame on shutdown. Minimal SHA-1 and Base64 implemented inline (zero external crypto deps).
- **webui_static_assets**: Serves `index.html` from a compile-time raw string literal. CSS and JS are embedded in the HTML (no separate files). Returns MIME types by extension for future asset expansion.
- **webui_config**: Reads `--webui` / `--webui-bind` / `--webui-port` from CLI args, and `LOFIBOX_WEBUI` / `LOFIBOX_WEBUI_BIND` / `LOFIBOX_WEBUI_PORT` from environment. CLI parsing consumes 1-2 args per flag and returns count so callers can advance the arg index.
- **webui_server**: Lifecycle only — `socket()` → `bind()` → `listen()` → background thread `accept()` loop. For each connection: reads HTTP request, checks for WebSocket upgrade, dispatches to router or WS stream. Uses `SO_REUSEADDR`. Knows nothing about playback/queue/EQ.

## 8. Frontend SPA Architecture

### Technology
- Single `index.html` with embedded `<style>` and `<script>` — no build step, no external dependencies
- WebSocket (`ws://` or `wss://`) for real-time event push (runs on detached thread, does not block HTTP accept loop)
- HTTP `fetch()` for snapshot polling (5s interval) and command dispatch
- HTML5 Canvas for spectrum visualization (80px height)
- Inline SVG icons for all tab and control buttons (no icon font or image dependencies)
- Custom EQ sliders using DOM elements + touch/mouse events (no native `<input type="range">`)

### Typography
Body text uses system sans-serif stack for readability on small mobile screens. Monospace is reserved for data values (time codes, EQ gain values, diagnostics counts).

```css
font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
```

Font size scale: 16px page headings, 14px body/labels, 13px secondary info, 11px captions/time, 10px monospace EQ values.

### Touch Interaction
Minimum touch target size: 44×44px (per Apple HIG). The large play/pause button is 56×56px. All interactive elements use `:active { transform: scale(0.94) }` for touch feedback.

**Swipe gesture** (Now Playing page):
- `touchstart` records start coordinates
- `touchmove` with `|deltaX| > |deltaY|` threshold prevents vertical scroll
- Artwork translates horizontally with touch (max 80px) and fades for visual feedback
- `touchend` with swipe >60px → `Previous` (right swipe) or `Next` (left swipe)
- Only active on Now Playing page; does not interfere with other pages

**Tap-to-seek** (progress bar):
- `click` on the 20px-tall bar wrapper computes `ratio = offsetX / width`
- Sends `Seek` with `seconds = ratio × duration_seconds`

**EQ slider touch handling**:
- Custom DOM-based vertical sliders: 28px circular thumb, 3px rail, `touch-action: none`
- `touchstart` / `touchmove` / `touchend` per band, maps clientY to gain value (-12..+12)
- Commands throttled at 50ms intervals to avoid flooding the server
- Mouse fallback via `mousedown` / `mousemove` / `mouseup` with `dragging` flag
- `e.preventDefault()` on touchmove prevents page scroll while sliding

### Responsive Design

| Breakpoint | Artwork | EQ Slider Height | Layout |
|-----------|---------|-----------------|--------|
| <480px (phone portrait) | 140×140 | 140px | Column, centered, single-column |
| 480-767px (phone landscape) | 160×160 | 160px | Column, centered |
| 768px+ (tablet/desktop) | 200×200 | 180px | NP side-by-side (artwork left, info right), content max-width 640px centered |
| 1024px+ | 200×200 | 180px | max-width 680px |

Viewport meta: `width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no` — prevents accidental pinch-zoom during EQ slider interaction.

### State Model
```javascript
state = {
  playback:  { status, title, artist, album, source_label, elapsed_seconds, duration_seconds,
               shuffle_enabled, repeat_all, repeat_one, ... },
  queue:     { items: [{ queue_index, title, artist, source_label, duration_seconds, active }], ... },
  eq:        { bands: [10], preset_name, enabled },
  lyrics:    { lines: [] },
  visualization: { bands: [10], available },
  library:   { status, track_count, album_count, artist_count, genre_count, ready, degraded },
  sources:   { active_source_label, configured_count, connection_status, stream_resolved },
  settings:  { output_mode, network_policy, sleep_timer },
  diagnostics: { runtime_ok, audio_ok, library_ok, remote_ok, cache_ok, ... },
  remote:    { profile_id, connection_status, buffer_state, codec, bitrate_kbps },
  version:   0
};
```

State is updated from two sources:
1. **WebSocket events**: `onmessage` parses JSON, copies `snapshot.playback` fields and `elapsed_seconds` into `state.playback`
2. **HTTP polling**: `fetch('/api/runtime/snapshot')` every 5 seconds — `Object.assign(state, data)` replaces all top-level keys

### Rendering
- `renderAll()` called on tab switch, WebSocket message, and after command dispatch
- Each tab has a dedicated `render*()` function that rebuilds innerHTML
- Spectrum canvas redrawn at 80px height with gradient bars
- Keyboard shortcuts: Space (Toggle), ArrowLeft (Previous), ArrowRight (Next)

### Connection Management
- Auto-reconnect WebSocket on close (2s delay)
- Connection status displayed in header: green/red dot + text (`PLAYING`, `PAUSED`, `——`, `offline`)
- Red "DISCONNECTED" pill overlay when WebSocket is down
- HTTP polling continues regardless of WebSocket state

### Visual Theme
Touch-friendly dark panel with warm amber accent on deep blue-black.

```css
:root {
  --lb-bg:          #07090d;   /* deep black-blue */
  --lb-panel:       #111722;   /* dark semi-translucent */
  --lb-panel-soft:  #151d2a;
  --lb-line:        #293241;   /* fine low-contrast borders */
  --lb-text:        #f2efe6;   /* warm white */
  --lb-muted:       #8b94a5;   /* grey-blue */
  --lb-accent:      #e6a33a;   /* warm amber/orange */
  --lb-accent-soft: #7a5420;
  --lb-danger:      #c65a4a;
  --lb-ok:          #7fb069;
}
```

Layout: fixed header (brand + connection dot), scrollable main area, fixed 56px bottom tab bar with inline SVG icons and labels, safe-area-inset-bottom for notched devices. Responsive at 480px, 768px, and 1024px breakpoints.

### SVG Icon Inventory
All icons are inline SVG elements (22-24px viewBox, `fill="none" stroke="currentColor" stroke-width="2"` unless filled). No external icon fonts or images.

| Icon | Usage | Style |
|------|-------|-------|
| play-circle | Now Playing tab | stroke |
| play / pause triangle/rects | Play/Pause toggle | fill |
| skip-back / skip-forward | Prev/Next buttons | stroke |
| shuffle arrows | Shuffle toggle | stroke |
| repeat / repeat-1 | Repeat toggle | stroke |
| list (three lines + bullets) | Queue tab | stroke |
| music-note | Library tab | stroke |
| sliders / bars | EQ tab | stroke |
| three-dots / more | More tab | stroke |
| search magnifier | Library search bar | stroke |

## 9. Deployment

| Property | Default |
|----------|---------|
| Enabled | OFF |
| Bind address | `0.0.0.0` (all interfaces) |
| Port | `8765` |
| Authentication | None |
| TLS | Not supported |

### CLI

```bash
lofibox --webui
lofibox --webui --webui-bind 127.0.0.1 --webui-port 9999
```

### Environment

```bash
LOFIBOX_WEBUI=1 LOFIBOX_WEBUI_BIND=127.0.0.1 LOFIBOX_WEBUI_PORT=8765 lofibox
```

The WebUI server is constructed and started inside `LoFiBoxApp` constructor when `webui_config.enabled` is true. It runs on a dedicated background thread for the lifetime of the app. The destructor calls `stop()` which shuts down the listen socket and joins the thread.

Static assets are embedded at compile time — there is no filesystem asset directory to install.

## 10. WebSocket Protocol Details

### Handshake

Server parses the HTTP upgrade request to extract `Sec-WebSocket-Key`. The accept key is computed as:

```
accept = base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
```

SHA-1 is implemented inline (FIPS 180-4, ~80 lines) and Base64 is implemented inline (~30 lines). No external crypto libraries required.

Server responds:
```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <base64-sha1>
```

### Framing (Server → Client)

- FIN bit always set (no fragmentation)
- Opcode: 0x1 (text frame)
- Mask bit: 0 (server→client frames are never masked per RFC 6455 §5.3)
- Payload length: 7-bit for ≤125 bytes, 16-bit for ≤65535, 64-bit above

### Event Poll Loop

1. Prime adapter with `querySnapshot()` to establish baseline
2. Loop every ~250ms:
   - `adapter_.pollEvents()` — compares previous snapshot to current, generates `RuntimeEvent` vector
   - Serialize each event via `buildEventJson()`
   - Send as WebSocket text frame
   - If send fails (client disconnected), exit loop
3. On `stop()` or disconnect: send close frame (0x88, no payload)

## 11. CMake

```cmake
set(LOFIBOX_BUILD_WEBUI_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LOFIBOX_BUILD_WEBUI_DEFAULT ON)
endif()
option(LOFIBOX_BUILD_WEBUI "Build the WebUI remote-control HTTP/WebSocket server." ${LOFIBOX_BUILD_WEBUI_DEFAULT})
```

Linux builds compile WebUI by default so default Linux CI configurations build `lofibox_webui` and run `lofibox_webui_smoke`. Non-Linux builds keep the default off because the current server implementation uses POSIX sockets.

Library `lofibox_webui` (STATIC):
- Source files: `webui_json.cpp`, `webui_config.cpp`, `webui_runtime_adapter.cpp`, `webui_projection.cpp`, `webui_static_assets.cpp`, `webui_http_router.cpp`, `webui_ws_runtime_stream.cpp`, `webui_server.cpp`
- Include: `${PROJECT_SOURCE_DIR}/src` (same as core)
- C++ Standard: 20
- Links: `lofibox_zero_core` (for `RuntimeCommandClient`, `RuntimeSnapshot`, `RuntimeEvent`, etc.)
- Must NOT link: `lofibox_zero_host_runtime`, `lofibox_zero_tui`, platform backends

### Compile-Time Guard

`LOFIBOX_HAVE_WEBUI=1` is defined **PRIVATE** on the two executable targets that construct the WebUI callback:

```cmake
# lofibox_zero_x11 (X11 desktop target)
if(TARGET lofibox_webui)
    target_link_libraries(lofibox_zero_x11 PRIVATE lofibox_zero_core lofibox_webui)
    target_compile_definitions(lofibox_zero_x11 PRIVATE LOFIBOX_HAVE_WEBUI=1)
endif()

# lofibox_zero_device (framebuffer target)
if(TARGET lofibox_webui)
    target_link_libraries(lofibox_zero_device PRIVATE lofibox_zero_core lofibox_webui)
    target_compile_definitions(lofibox_zero_device PRIVATE LOFIBOX_HAVE_WEBUI=1)
endif()
```

The define is NOT set on `lofibox_zero_core`. This is intentional — `lofibox_zero_core` has zero compile-time knowledge of WebUI types.

### Callback Injection (AppStarter)

WebUI construction is injected at runtime via a `std::function` callback, avoiding circular dependencies and ODR violations:

1. `lofibox_zero_core` exposes `AppStarter = std::function<void(RuntimeCommandClient&, const AppServiceRegistry&)>`
2. `LoFiBoxApp` constructor accepts `AppStarter on_start` — if non-null, calls it with the runtime client and app service registry
3. `device_main.cpp` / `x11_main.cpp` construct the `AppStarter` lambda (guarded by `#if defined(LOFIBOX_HAVE_WEBUI)`), which creates `WebUiRuntimeAdapter` + `WebUiServer`, wires optional library providers from the app service registry, and calls `start()`
4. The lambda captures WebUI objects in a `shared_ptr` to keep them alive for the app lifetime

This eliminates the previous problem where `LOFIBOX_HAVE_WEBUI` was needed inside `lofibox_app.cpp` (part of `lofibox_zero_core`) but `lofibox_zero_core` could not link against `lofibox_webui` without creating a cycle.

The same target-level composition writes the display endpoint into `RuntimeServices::ui.webui_url`. The GUI Settings projection may display this address when network status is online, but core application code must not depend on WebUI server types.

### Link Order

`lofibox_zero_core` is listed before `lofibox_webui` in the link line. This ensures that when the GNU linker processes the static archives left-to-right, `lofibox_webui` symbols are available to resolve references from `device_main.cpp` and `x11_main.cpp`.

Test:
```cmake
add_executable(lofibox_webui_smoke tests/webui_smoke.cpp)
target_link_libraries(lofibox_webui_smoke PRIVATE lofibox_webui)
add_test(NAME lofibox_webui_smoke COMMAND lofibox_webui_smoke)
```

The test links only `lofibox_webui` — it does not need `lofibox_zero_core` or `LOFIBOX_HAVE_WEBUI`.

## 12. Tests

`tests/webui_smoke.cpp` covers:

| Test Group | What It Verifies |
|---|---|
| Config | Default values, `--webui`/`--webui-bind`/`--webui-port` CLI parsing, env parsing (no env set), invalid port rejection |
| JSON Helpers | `escape()` for special chars, `appendString`/`appendBool`/`appendInt`/`appendDouble` output, `openObject`/`closeObject` nesting, `appendStringArray`, HTTP response builders (200/400/404/405/500) |
| Projection | Empty snapshot produces valid JSON with all section keys, Now Playing contains track metadata, Diagnostics contains ok/err flags and warnings/errors arrays, EQ contains bands array and preset, Event contains kind/version/elapsed |
| Command Parsing | Toggle → `PlaybackToggle`, Previous → `QueueStep(-1)`, Next → `QueueStep(1)`, QueueJump → `QueueJump(index)`, EqSetBand → `EqSetBand(band, gain)`, EqCyclePreset → `EqCyclePreset(delta)`, unknown action rejected, missing params rejected |
| Static Assets | `webUiIndexHtml()` returns non-null HTML with `<!DOCTYPE html>` and title, MIME type lookup by extension, asset path routing |
| Runtime Adapter | Fake `RuntimeCommandClient` integration: `querySnapshot()` returns expected data, `submitCommand()` returns accepted result, `pollEvents()` with unchanged snapshot returns empty vector, `isConnected()` transitions after first query |
| HTTP Router | `GET /` → 200 + HTML, `GET /api/runtime/snapshot` → 200 + JSON with track data, `POST /api/runtime/commands` (valid) → 200 + accepted, `POST /api/runtime/commands` (invalid) → 400, `OPTIONS` → 204 + CORS headers, unknown path → 404 |

The test uses a `FakeRuntimeCommandClient` that implements the virtual `RuntimeCommandClient` interface with canned responses, avoiding any dependency on the real runtime.

## 13. Architecture Enforcement (CI)

Forbidden includes from `src/webui/`:
- `app/app_runtime_context.h`
- `app/lofibox_app.h` (except `LOFIBOX_HAVE_WEBUI` guards in app composition files)
- `ui/pages/*`
- `ui/ui_primitives.h`
- `playback/playback_controller.h`
- `playback/playback_backend_controller.h`
- `platform/host/*`
- `remote/*` (concrete providers)
- `runtime/runtime_session_facade.h`
- `runtime/runtime_command_bus.h`
- `runtime/runtime_command_server.h`

Allowed includes:
- `runtime/runtime_command_client.h`
- `runtime/runtime_command.h`
- `runtime/runtime_event.h`
- `runtime/runtime_result.h`
- `runtime/runtime_snapshot.h`
- Standard library + POSIX networking headers

## 14. Version Control Design

This project targets Debian official repository inclusion. Version numbers must be strictly controlled, with a single source of truth and deterministic propagation to all consumers.

### 14.1 Single Source of Truth

The project version is defined in exactly one place:

```cmake
# CMakeLists.txt line 5
project(LoFiBoxZero VERSION X.Y.Z LANGUAGES C CXX)
```

`PROJECT_VERSION` is the authoritative version string. No other file may define the version independently. The `PROJECT_VERSION_MAJOR`, `PROJECT_VERSION_MINOR`, and `PROJECT_VERSION_PATCH` variables are available for decomposed use if needed, though current consumers use the full string.

### 14.2 Propagation: CMake → C++ Preprocessor

CMake injects the version into C++ via `target_compile_definitions`:

```
target_compile_definitions(lofibox_zero_core PUBLIC
    LOFIBOX_VERSION="${PROJECT_VERSION}"
)
```

This definition is `PUBLIC` on `lofibox_zero_core` — every library and executable that links it (transitively or directly) inherits the macro. This includes:

| Target | Link | Receives LOFIBOX_VERSION |
|--------|------|--------------------------|
| `lofibox_zero_core` | — | Yes (defines it PUBLIC) |
| `lofibox_zero_target_cli` | links core | Yes (inherits) |
| `lofibox_zero_host_runtime` | links core | Yes (inherits) |
| `lofibox_webui` | links core | Yes (inherits) |
| `lofibox_zero_x11` | links core | Yes (inherits) |
| `lofibox_zero_device` | links core | Yes (inherits) |
| `lofibox_zero_tui` | links core | Yes (inherits) |

Every C++ consumer uses the same pattern — preprocessor conditional with a fallback:

```cpp
#if defined(LOFIBOX_VERSION)
constexpr std::string_view kVersion{LOFIBOX_VERSION};
#else
constexpr std::string_view kVersion{"unknown"};
#endif
```

This ensures that even if the macro is somehow absent (e.g., a build system misconfiguration), the binary degrades gracefully to `"unknown"` rather than displaying stale hardcoded text or failing to compile.

### 14.3 Propagation: CMake → Python

Python helpers (currently `jellyfin_provider.py`) cannot consume C preprocessor macros. They receive the version through CMake's `configure_file()` mechanism:

**Template** (`cmake/version.py.in`):
```python
LOFIBOX_VERSION = "@PROJECT_VERSION@"
```

**CMake generation**:
```cmake
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.py.in"
    "${CMAKE_BINARY_DIR}/generated/version.py"
    @ONLY
)
install(FILES "${CMAKE_BINARY_DIR}/generated/version.py"
    DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}"
)
```

**Python consumption** (`src/remote/jellyfin/jellyfin_provider.py`):
```python
try:
    from version import LOFIBOX_VERSION
except ImportError:
    LOFIBOX_VERSION = "0.0.0-dev"
```

The generated `version.py` is installed to `${LOFIBOX_PRIVATE_LIBDIR}` (`/usr/lib/lofibox/` on Debian), which is on `sys.path` because `remote_media_tool.py` (also installed there) adds its parent directory to the import path. The `try/except ImportError` fallback to `"0.0.0-dev"` covers development builds where the generated file does not exist at the expected path.

### 14.4 Consumers and Their Purposes

| Consumer | File | Purpose | Format |
|----------|------|---------|--------|
| CLI `--version` | `src/targets/cli_options.cpp` | Human and machine-readable version output | `lofibox X.Y.Z` / `{"version":"X.Y.Z"}` |
| CLI `--help` | `src/targets/cli_options.cpp` | Banner in help text | `LoFiBox X.Y.Z` |
| GUI About Page | `src/app/app_projection_builder.cpp` | On-device about screen | Rendered in 320×170 canvas |
| Jellyfin Auth | `src/remote/jellyfin/jellyfin_provider.py` | HTTP User-Agent / auth header | `Version="X.Y.Z"` |
| Debian Package | `debian/changelog` | Package version for dpkg/APT | `lofibox (X.Y.Z-1)` |
| AppStream Metadata | `data/...metainfo.xml` | Software center (GNOME/KDE) display | `<release version="X.Y.Z" .../>` |

### 14.5 App Version vs. Snapshot Version

Two distinct version concepts exist in the codebase and must not be confused:

| Concept | Type | Source | Purpose |
|---------|------|--------|---------|
| **App version** | String (`"X.Y.Z"`) | CMake `PROJECT_VERSION` | Software release identity; displayed in CLI, GUI About, Jellyfin UA, Debian package |
| **Snapshot version** | Integer (`42`, `43`, …) | `RuntimeSnapshot::version` | Monotonic counter incremented on each state mutation; used for cache invalidation and event diffing |

The snapshot `version` field in WebUI JSON responses (e.g., `"version": 42` at the top level of `/api/runtime/snapshot`) is the runtime state counter, **not** the app release version. The WebUI frontend stores this as `state.version` and uses it internally for event stream tracking. It must never be displayed to the user as if it were the software version.

### 14.6 Version Bump Procedure

When releasing a new version, the following files must be updated:

1. **CMakeLists.txt** — `project(LoFiBoxZero VERSION X.Y.Z …)` — this is the single source change; all automated propagation flows from here
2. **CHANGELOG.md** — add `## [X.Y.Z]` section following Keep a Changelog format
3. **debian/changelog** — add `lofibox (X.Y.Z-1) UNRELEASED; urgency=medium` entry following Debian policy format
4. **data/io.github.vicliu624.lofibox.metainfo.xml** — add `<release version="X.Y.Z" date="YYYY-MM-DD" />` entry (newest first)

All other version-bearing locations are generated or preprocessor-derived and require no manual edit:

| Location | Automatic? | Mechanism |
|----------|-----------|-----------|
| `cli_options.cpp` | Yes | `LOFIBOX_VERSION` preprocessor macro |
| `app_projection_builder.cpp` | Yes | `LOFIBOX_VERSION` preprocessor macro |
| `jellyfin_provider.py` | Yes | `configure_file()` from `version.py.in` |
| `debian/watch` | Yes | GitHub tag pattern match — no version literal |

### 14.7 Debian Compliance Notes

- **Upstream source**: The version in `CMakeLists.txt` is the upstream version. Debian packaging adds the Debian revision (e.g., `X.Y.Z-1`) in `debian/changelog` only. The upstream source must report the unadorned version string.
- **`debian/watch`**: Points to GitHub release tags. The version extraction pattern `.*/v?(\d\S*)\.tar\.gz` matches tags like `vX.Y.Z`. The tag name must match the `CMakeLists.txt` version.
- **`--version` output**: `lofibox X.Y.Z` is the upstream version. The Debian package may optionally append the Debian revision, but the default behavior (reporting the raw upstream version) is correct for a pristine upstream source.
- **Reproducible builds**: The `configure_file()` approach for Python version injection is deterministic — given the same source tree, the generated `version.py` is byte-identical. The preprocessor macro approach is also deterministic. Both satisfy Debian's reproducible builds requirement.
- **No hardcoded versions**: The architecture enforcement rules in Section 13 apply to version strings as well. No source file (C++, Python, or otherwise) may contain a hardcoded version literal. The grep pattern `"0\.[0-9]+\.[0-9]+"` should return zero results in `src/` at all times.
