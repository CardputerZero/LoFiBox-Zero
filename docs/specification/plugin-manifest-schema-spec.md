<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Plugin Manifest Schema Specification

> **Audience:** Third-party plugin developers, packagers, and LoFiBox core maintainers.
> **Normative reference:** This document defines the canonical `plugin.json` format.
> Conformance to this spec is required for plugin acceptance into the official
> repository.

## 1. Purpose

This document defines the `plugin.json` manifest format — the single entry point
by which LoFiBox discovers, validates, and activates every plugin. No plugin may
load without a valid manifest.

## 2. Manifest Lifecycle

```
Plugin directory scanned
  → plugin.json found
  → schema_version check (reject if unsupported)
  → field validation (reject if required fields missing)
  → capability resolution (match to extension points)
  → startup ordering (topological sort by dependencies)
  → PluginRegistry registration
  → adapter activation
```

## 3. Schema Version

The `schema_version` field is an integer that identifies the manifest format.
LoFiBox reads this field first and rejects manifests with unsupported versions.

| `schema_version` | Status |
|------------------|--------|
| `1` | Current. All LoFiBox Zero releases ≥ 0.2.0. |

Manifests with unknown `schema_version` are **skipped with a warning** — not
an error — to allow forward-compatible plugin directories that contain manifests
for future LoFiBox versions alongside current ones.

## 4. Field Reference

### 4.1 Required Fields (all kinds)

| Field | Type | Description |
|-------|------|-------------|
| `schema_version` | integer | Must be `1` |
| `id` | string | Reverse-domain unique identifier (see §4.5) |
| `name` | string | Human-readable display name, max 128 chars |
| `version` | string | Semantic version (`MAJOR.MINOR.PATCH`) |
| `api_version` | string | LoFiBox API version this plugin targets (currently `"1"`) |
| `kind` | string | `asset_pack`, `external_helper`, or `internal_provider` |
| `capabilities` | string[] | Non-empty array of capability namespace values (see §6) |

### 4.2 Kind-Specific Required Fields

| Kind | Required Field | Description |
|------|---------------|-------------|
| `external_helper` | `entry` | Process launch configuration (see §4.3) |
| `asset_pack` | `resources` | Must contain at least one resource path |
| `internal_provider` | *(none extra)* | Registered at compile time via C++ API |

### 4.3 Entry Specification (`external_helper` only)

```json
"entry": {
  "command": "python3",
  "args": ["provider.py"],
  "protocol": "lofibox-jsonrpc-1"
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `command` | yes | Executable name or absolute path. Must be resolvable via `$PATH` or absolute. |
| `args` | no | Arguments passed to `command`. Default: `[]`. |
| `protocol` | yes | Must be `"lofibox-jsonrpc-1"` (currently the only recognized protocol). |
| `cwd` | no | Working directory override. Default: the plugin directory itself. |
| `env` | no | Object of extra environment variables (`{"KEY": "value"}`). Merged with parent process env. |

### 4.3.1 Protocol: `lofibox-jsonrpc-1` — Pipe Layout

The host spawns the helper process with **three separate pipes** — stdin, stdout,
and stderr are never multiplexed on the same file descriptor.

```
Parent (LoFiBox)              Child (helper process)
┌──────────────┐              ┌───────────────────────┐
│ stdin_fd  ───┼── pipe A ───▶│ stdin  (JSON-RPC req)  │
│ stdout_fd ◀──┼── pipe B ───│ stdout (JSON-RPC resp) │
│ stderr_fd ◀──┼── pipe C ───│ stderr (log output)    │
└──────────────┘              └───────────────────────┘
```

**Rules:**

- **pipe A (stdin):** LoFiBox writes one JSON-RPC request per line. The helper
  reads lines, dispatches, writes responses to stdout.
- **pipe B (stdout):** The helper writes one JSON-RPC response per line. LoFiBox
  reads and parses each line. **Only JSON-RPC messages may appear on this pipe.**
- **pipe C (stderr):** The helper may write arbitrary log/diagnostic output.
  LoFiBox reads this pipe asynchronously on a dedicated thread and appends all
  output to `<plugin_dir>/stderr.log`. **Nothing on this pipe is parsed as
  JSON-RPC.**

**Why three pipes:**

Mixing stderr and stdout on one fd would mean a `print()` debug statement or a
library warning could interleave with a JSON-RPC response, producing a
non-parseable line. Separate pipes guarantee protocol integrity regardless of
what the helper logs.

**Thread safety:**

The stderr reader thread and the request/response thread operate on different
file descriptors (`stderr_fd_` vs `stdout_fd_`) and require no synchronization.
Both are joined/closed cleanly on process shutdown.

### 4.4 Optional Fields (all kinds)

| Field | Type | Description |
|-------|------|-------------|
| `description` | string | Short description, max 256 chars |
| `author` | string | Author name and/or email |
| `homepage` | string | Plugin homepage URL |
| `license` | string | SPDX license identifier (e.g. `"MIT"`, `"GPL-3.0-or-later"`) |
| `runtime_dependencies` | string[] | System packages or runtimes needed (informational) |
| `permissions` | string[] | Runtime capability declarations (see §7) |
| `resources` | object | Plugin-relative paths to resource files/directories |
| `hidden` | boolean | If `true`, plugin is not shown in user-facing plugin lists. Default: `false`. |
| `builtin` | boolean | If `true`, plugin cannot be disabled or removed by the user. Default: `false`. |
| `config` | object | Plugin-type-specific configuration (see §8) |

### 4.5 Identifier Format

Plugin IDs use reverse-domain notation:

```
[tld].[domain].[project].[category].[name]
```

**Rules:**
- Must match regex: `^[a-z][a-z0-9._-]{2,127}$`
- Must start with a lowercase letter
- Segments separated by `.`
- Recommended prefix: `io.github.vicliu624.lofibox` for official plugins
- Community plugins: `community.<category>.<name>` or a domain the author controls
- The final segment is the plugin short-name

**Examples:**
```
io.github.vicliu624.lofibox.remote.jellyfin      ✓ official
io.github.vicliu624.lofibox.theme.classic-dark    ✓ official
community.remote.my-provider                      ✓ community
community.lyrics.genius                           ✓ community
MyPlugin                                         ✗ no dots
com.example.LoFiBox.plugin                       ✗ uppercase
```

### 4.6 Version Format

Plugin `version` must be valid [Semantic Versioning 2.0.0](https://semver.org):

```
MAJOR.MINOR.PATCH
```

- `MAJOR`: breaking changes to the plugin's own behavior or configuration
- `MINOR`: new capabilities or features, backward-compatible
- `PATCH`: bug fixes, backward-compatible

Pre-release suffixes (`-alpha.1`, `-beta.2`) are accepted but discouraged for
published plugins.

## 5. Validation Rules

### 5.1 Hard Rejections

The manifest is **rejected** (plugin not loaded) if:

- `schema_version` is absent or not an integer
- `schema_version` is not `1` (until future versions are defined)
- `id` is absent, empty, or does not match the regex in §4.5
- `name` is absent or empty
- `version` is absent or not valid semver
- `kind` is absent or not one of the three recognized values
- `capabilities` is absent, empty, or contains no recognized values
- `external_helper` is missing `entry` or `entry.command` or `entry.protocol`
- `external_helper` has an unrecognized `entry.protocol`

### 5.2 Soft Warnings

The manifest is **accepted** but a warning is logged if:

- `capabilities` contains unrecognized values (accepted for forward compatibility,
  but the unknown capabilities are ignored)
- `runtime_dependencies` lists packages that are not installed
- `resources` paths do not exist on disk
- `permissions` contain unrecognized values
- Fields not defined in this spec are present (forward compatibility)

## 6. Capability Namespace (Normative)

### 6.1 UI / Skin

```
ui.theme                 — color palette, font selection, spacing
ui.icons                 — icon set overrides
ui.layout.now_playing    — Now Playing page layout template
ui.layout.library        — Library browse page layout template
ui.layout.eq             — Equalizer page layout template
ui.layout.lyrics         — Lyrics page layout template
```

### 6.2 Audio / DSP

```
audio.effect             — generic audio effect
audio.effect.realtime    — real-time (low-latency) audio effect
audio.effect.offline     — offline/batch audio processing
audio.effect.visualizer  — audio visualization generator
```

### 6.3 Remote Sources

```
remote.source            — remote media repository provider
remote.browse            — catalog browsing (artists, albums, folders)
remote.search            — text search across remote catalog
remote.stream            — track-to-stream resolution
remote.auth.profile      — profile-based authentication
```

### 6.4 Metadata

```
metadata.reader          — embedded tag / file metadata extraction
metadata.enricher        — online metadata enrichment (MusicBrainz, AcoustID, ...)
metadata.lyrics          — lyrics lookup (plain, synced LRC)
metadata.artwork         — cover art / fanart lookup
metadata.fingerprint     — acoustic fingerprint / track identity
metadata.tag_writer      — metadata writeback to media files
```

## 7. Permission Namespace

Permissions are **runtime capability declarations** for packaging review and
user visibility. They are NOT a user/role access control system.

```
network.client           — outbound HTTP/HTTPS connections
network.server           — listen on network ports
credential.read.profile  — read stored remote source credentials
credential.write         — write/modify stored credentials
storage.read.library     — read local library/media files
storage.write.cache      — write to plugin cache directory
storage.write.tag        — write metadata tags to media files
audio.capture            — access audio input device
process.subprocess       — spawn child processes
```

## 8. Config Section (Per-Kind)

### 8.1 Asset Pack (`skin.json` inline or reference)

```json
"config": {
  "skin": "skin.json"
}
```

If `config.skin` is a string, it names a file within the plugin directory
(default: `skin.json`). If it is an object, it is treated as inline skin
configuration (same schema as `skin.json`).

### 8.2 DSP Effect (effect chain inline)

```json
"config": {
  "effect": {
    "type": "dsp_chain",
    "nodes": [
      { "type": "lowpass", "cutoff_hz": 7200 },
      { "type": "soft_clip", "drive": 1.4 }
    ]
  }
}
```

### 8.3 External Helper (provider-specific)

```json
"config": {
  "connect_timeout_ms": 10000,
  "max_retries": 3
}
```

Provider-defined configuration passed to the helper process on startup.

## 9. Startup Ordering and Dependencies

### 9.1 Problem Statement

Plugins may depend on each other:

- A **cache plugin** must initialize before a **remote source plugin** that
  uses it.
- An **auth/credential plugin** must be ready before a **stream plugin** that
  reads credentials.
- A **metadata enricher** must be available before a **lyrics provider** that
  depends on enriched track identity.

Currently all plugins start independently with no ordering guarantees. This
section defines the contract for future startup ordering.

### 9.2 Dependency Declaration

Plugins declare dependencies in the `depends_on` field:

```json
"depends_on": [
  {
    "plugin_id": "io.github.vicliu624.lofibox.cache.sqlite",
    "required": true
  },
  {
    "capability": "remote.auth.profile",
    "required": false
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `plugin_id` | string | Exact plugin ID to depend on (use this for direct dependencies) |
| `capability` | string | Any plugin providing this capability (use this for interface dependencies) |
| `required` | boolean | If `true`, failure to load the dependency blocks this plugin. Default: `true`. |

**Rules:**
- A plugin may declare both `plugin_id` and `capability` dependencies.
- `required: false` means "load after if present, but proceed without it."
- `required: true` with an unsatisfied dependency → plugin is disabled, warning logged.
- Circular dependencies → both plugins are disabled, error logged.

### 9.3 Readiness Signal

For `external_helper` plugins, the helper process signals readiness by writing
a notification to stdout before entering the main request loop:

```json
{"jsonrpc":"2.0","method":"ready","params":{"status":"ok"}}
```

**Rules:**
- The host waits for the `ready` notification (with a configurable timeout,
  default 10s) before routing requests to the plugin.
- If the plugin does not send `ready` within the timeout, it is treated as
  failed — restarted or disabled per PluginRuntime retry policy.
- `internal_provider` plugins are considered ready immediately after
  construction.
- `asset_pack` plugins are always ready immediately (no process to start).

### 9.4 Startup Sequence

```
1. Discover all manifests → build dependency graph
2. Topological sort (respecting depends_on edges)
3. For each plugin in order:
   a. asset_pack:     load resources immediately (no process)
   b. internal_provider: construct immediately (no process)
   c. external_helper: spawn process, wait for ready signal (timeout T)
      - If ready within T: activate
      - If timeout: retry per PluginRuntime policy, then disable
      - If required dependency not ready: skip, warn
4. PluginRegistry emits "startup_complete" event
```

### 9.5 Startup Order Example

Given these plugins:

```json
// Plugin A: cache provider
{"id": "io.github.vicliu624.lofibox.cache.sqlite", "kind": "internal_provider", ...}

// Plugin B: remote source
{"id": "io.github.vicliu624.lofibox.remote.jellyfin", "kind": "external_helper",
 "depends_on": [{"capability": "remote.auth.profile", "required": true}], ...}

// Plugin C: auth provider
{"id": "io.github.vicliu624.lofibox.auth.oauth", "kind": "external_helper",
 "capabilities": ["remote.auth.profile"], ...}
```

Topological sort produces: **A → C → B** (C must be ready before B starts).

### 9.6 Future: Startup Phases

For more granular control (deferred to a future `schema_version`):

```json
"startup_phase": "early"   // "early" | "normal" | "late"
```

- `early`: infrastructure plugins (cache, logging, credential store)
- `normal`: default, most plugins
- `late`: UI skins, post-processing, analytics

Within each phase, `depends_on` ordering applies.

## 10. Complete Examples

### 10.1 UI Skin Plugin (`asset_pack`)

```json
{
  "schema_version": 1,
  "id": "io.github.vicliu624.lofibox.theme.classic-dark",
  "name": "Classic Dark",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "asset_pack",
  "description": "Default dark theme for LoFiBox Zero",
  "author": "vicliu624",
  "license": "GPL-3.0-or-later",
  "capabilities": [
    "ui.theme",
    "ui.icons",
    "ui.layout.now_playing",
    "ui.layout.eq",
    "ui.layout.lyrics"
  ],
  "resources": {
    "skin": "skin.json",
    "icons": "icons/",
    "fonts": "fonts/"
  },
  "config": {
    "skin": "skin.json"
  },
  "hidden": false,
  "builtin": true
}
```

### 10.2 Remote Source Plugin (`external_helper`)

```json
{
  "schema_version": 1,
  "id": "io.github.vicliu624.lofibox.remote.jellyfin",
  "name": "Jellyfin Provider",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "external_helper",
  "description": "Browse and stream from Jellyfin media servers",
  "author": "vicliu624",
  "license": "GPL-3.0-or-later",
  "entry": {
    "command": "python3",
    "args": ["provider.py"],
    "protocol": "lofibox-jsonrpc-1",
    "cwd": "."
  },
  "capabilities": [
    "remote.source",
    "remote.browse",
    "remote.search",
    "remote.stream",
    "remote.auth.profile"
  ],
  "runtime_dependencies": ["python3", "python3-requests"],
  "permissions": ["network.client", "credential.read.profile"],
  "resources": {
    "schema": "profile.schema.json"
  },
  "depends_on": [
    {
      "capability": "remote.auth.profile",
      "required": false
    }
  ]
}
```

### 10.3 DSP Effect Plugin (`internal_provider`)

```json
{
  "schema_version": 1,
  "id": "io.github.vicliu624.lofibox.effect.lofi",
  "name": "LoFi Effect",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "internal_provider",
  "description": "Lo-Fi audio effect: lowpass, bitcrush, wow/flutter, soft clip",
  "license": "GPL-3.0-or-later",
  "capabilities": [
    "audio.effect",
    "audio.effect.realtime"
  ],
  "config": {
    "effect": {
      "type": "dsp_chain",
      "nodes": [
        { "type": "lowpass", "cutoff_hz": 7200 },
        { "type": "bitcrush", "bits": 12, "mix": 0.35 },
        { "type": "wow_flutter", "depth": 0.12 },
        { "type": "soft_clip", "drive": 1.4 }
      ]
    }
  },
  "builtin": true
}
```

### 10.4 Metadata Lyrics Plugin (`external_helper`)

```json
{
  "schema_version": 1,
  "id": "community.lyrics.lrclib",
  "name": "LRCLib Lyrics Provider",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "external_helper",
  "description": "Fetch synced lyrics from LRCLib",
  "author": "community",
  "license": "MIT",
  "entry": {
    "command": "python3",
    "args": ["provider.py"],
    "protocol": "lofibox-jsonrpc-1"
  },
  "capabilities": [
    "metadata.lyrics"
  ],
  "runtime_dependencies": ["python3", "python3-requests"],
  "permissions": ["network.client"],
  "depends_on": [
    {
      "capability": "metadata.fingerprint",
      "required": false
    }
  ]
}
```

## 11. Error Catalog

| Code | Condition | Behavior |
|------|-----------|----------|
| `MANIFEST_MISSING` | No `plugin.json` in directory | Directory skipped |
| `MANIFEST_PARSE_ERROR` | Invalid JSON | Plugin disabled, error logged |
| `SCHEMA_VERSION_UNSUPPORTED` | `schema_version` > 1 | Plugin skipped, warning logged |
| `FIELD_MISSING` | Required field absent | Plugin disabled, error logged |
| `FIELD_INVALID` | Field value invalid (bad ID, bad version) | Plugin disabled, error logged |
| `CAPABILITY_UNKNOWN` | Capability not in namespace | Accepted, unknown capabilities ignored, warning logged |
| `DEPENDENCY_UNSATISFIED` | `depends_on` with `required: true` not met | Plugin disabled, error logged |
| `DEPENDENCY_CIRCULAR` | Circular `depends_on` chain detected | All plugins in cycle disabled, error logged |
| `READY_TIMEOUT` | Helper did not send `ready` within timeout | Plugin restarted or disabled per retry policy |
| `PROCESS_CRASHED` | Helper process exited unexpectedly | Plugin restarted or disabled per retry policy |

## 12. References

- [LoFiBox Zero Plugin System Specification](lofibox-zero-plugin-system-spec.md)
- [LoFiBox Plugin And Provider System Specification](plugin-provider-system-spec.md)
- [Semantic Versioning 2.0.0](https://semver.org)
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
- [SPDX License List](https://spdx.org/licenses/)
