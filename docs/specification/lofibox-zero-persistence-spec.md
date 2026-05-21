<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Persistence Specification

## 1. Purpose

This document defines what `LoFiBox Zero` persists, what remains runtime-only, how persisted domains are loaded, and how corrupted or outdated state is repaired.

It exists to stop persistence behavior from being implied by individual features.
Save, load, default, repair, and secret-handling rules must be explicit product and engineering decisions.

## 2. Authority

For final product meaning, use `lofibox-zero-final-product-spec.md`.
For shared runtime ownership, use `lofibox-zero-app-state-spec.md`.
For track identity, audio fingerprinting, and enrichment authority, use `lofibox-zero-track-identity-spec.md`.
For credential references, secure secret material, and session-lifecycle rules, use `lofibox-zero-credential-spec.md`.
For library scan, build, refresh, rebuild, and readiness rules, use `lofibox-zero-library-indexing-spec.md`.
This document defines persistence-domain truth and load/save/repair behavior for those shared states and related configuration objects.

## 3. Scope

This document covers:

- persisted versus runtime-only domain boundaries
- source and streaming-server profile persistence
- settings persistence
- listening-history and library metadata persistence
- DSP preset persistence
- startup load order
- repair and fallback rules
- credential-handling rules

This document does not define:

- physical storage engine choice
- low-level database or file schema implementation
- media cache internals already owned by capability specs
- page-local widget memory

## 4. Persistence Domain Index

The current persistence domains are:

- `SettingsDomain`
- `CredentialReferenceDomain`
- `SourceProfilesDomain`
- `LibraryMetadataDomain`
- `ListeningHistoryDomain`
- `DspPresetDomain`

The following remain runtime-only by default unless a later spec changes them explicitly:

- `NavigationStack`
- `SearchState` query/result snapshots
- transient playback buffers
- transient source-availability probes
- transient queue ordering and active position

## 5. Global Persistence Principles

### 5.1 Truth Separation

- Persisted configuration must be kept separate from runtime projections.
- User-configured truth must be persisted.
- Ephemeral runtime availability, buffer health, and in-flight transport details must not be treated as durable configuration.

### 5.2 Load Then Repair

- Persisted domains must load before the app presents them as durable truth.
- If a persisted domain is invalid, the system must either repair it deterministically or fall back to safe defaults.
- Failed persistence must not be silently presented as successful persistence.

### 5.3 Reference Over Raw Secrets

- Persistent records must store credential references rather than raw passwords, tokens, API keys, cookies, or session blobs in ordinary configuration payloads.
- Secure credential material belongs in a dedicated secret store or credential container outside ordinary product records, as defined in `lofibox-zero-credential-spec.md`.

## 6. Persisted Domains

### 6.1 `SettingsDomain`

This domain persists user-facing settings truth such as:

- sleep timer policy
- backlight policy
- language selection

This domain must not duplicate playback-session truths such as shuffle or repeat.

### 6.2 `SourceProfilesDomain`

This domain persists user-configured source definitions and source selection truth.

Persisted source profile families may include:

- local media roots
- LAN share profiles such as `SMB`, `NFS`, `WebDAV`, `FTP`, or `SFTP`
- direct remote URL or station entries
- media-server profiles for:
  - `Jellyfin`
  - `Navidrome`
  - `Emby`
  - `Subsonic` and `OpenSubsonic`
  - `DLNA` and `UPnP` media servers

Each persisted source profile should be able to carry at least:

- `id`
- `name`
- `kind`
- `service_type` or source-family discriminator
- `base_url`, service root, local root, or share target as applicable
- `auth_mode`
- `credential_ref`
- `default_eligible`
- `enabled`
- user-configured TLS or certificate policy
- user-configured read-only versus writable intent when relevant

#### 6.2.1 Local Media Root Source Profiles

Local media roots are part of `SourceProfilesDomain`.
They must not be persisted in a parallel `LibraryRootsDomain` unless this specification is changed first.

A local media root source profile should carry:

- `kind`: `local-root` or `local_media_root`
- `id`: a stable source id
- `name`: a display label
- `local_root`: the filesystem path to scan
- `default_eligible`: whether it may act as the default local source
- `enabled`: whether the scanner should include it
- read/write intent when product behavior exposes writeback for that root
- no `credential_ref` by default

The current implementation may temporarily store `local_root` in an existing transport field such as `base_url` while preserving the product meaning as a local filesystem path.
That implementation detail must not turn `base_url` into the long-term semantic name for local roots.

The domain should also persist:

- active source identifier when the product chooses to restore it
- default source identifier

This domain must not persist transient reachability as if it were durable truth.

### 6.3 `CredentialReferenceDomain`

This domain persists:

- `credential_ref`
- `auth_mode`
- source-to-credential linkage metadata

This domain must not persist raw secret material inside ordinary product records.

### 6.4 `LibraryMetadataDomain`

This domain persists library-facing descriptive data such as:

- scanned metadata index
- normalized title, artist, album, genre, and composer results
- accepted track identity, including MusicBrainz recording, release, and release-group identifiers
- identity source, confidence, and fingerprint-verification state
- cover-art metadata references

This domain may be rebuilt from source media when needed.

Audio fingerprints themselves are runtime derivations unless a later local fingerprint-index feature explicitly promotes them into durable library data.
External identity-service API keys are credential material and must not be persisted in ordinary library metadata.

### 6.5 `ListeningHistoryDomain`

This domain persists product-history data such as:

- `added_time`
- `play_count`
- `last_played`

These values must be treated as product data, because they drive smart playlists and history-driven behavior.

### 6.6 `DspPresetDomain`

This domain persists EQ and DSP configuration truth such as:

- system preset definitions
- user preset definitions
- per-device DSP profile bindings when supported
- last active DSP selection when restore behavior is enabled

## 7. Streaming Server Persistence Rules

### 7.1 Mainstream Server Coverage

The persisted server-profile model must explicitly support at least these mainstream server families:

- `Jellyfin`
- `Navidrome`
- `Emby`
- `Subsonic` and `OpenSubsonic`
- `DLNA` and `UPnP`

### 7.2 Persisted Server Connection Fields

A persisted media-server profile should be able to carry:

- server `id`
- display `name`
- server `kind`
- `base_url`
- `auth_mode`
- `credential_ref`
- user-configured TLS policy
- default-server eligibility
- persisted user-facing capability preferences such as original-versus-transcoded preference when supported

### 7.3 Connection Workflow Contract

The generic server connection workflow should be:

1. create or edit a source profile
2. validate target address or service root
3. authenticate using the selected auth mode
4. persist durable profile data and secure credential reference
5. restore saved profile later without rebuilding it manually
6. re-project runtime availability and session status on load

### 7.4 Session Material Rule

- Durable source identity belongs in persisted source profiles.
- Secure credentials belong in a credential store referenced by `credential_ref`, as defined by `lofibox-zero-credential-spec.md`.
- Ephemeral session cookies or temporary session tokens are runtime state unless a later security policy explicitly allows durable session restoration.

## 8. Startup Load Order

The product should load persisted domains in this broad order:

1. settings defaults and persisted settings
2. source profiles and default/active source truth
3. library metadata and listening history
4. DSP presets and DSP restore targets
5. runtime projections built from those durable domains

This order keeps the app from presenting UI that depends on source, settings, or history truth before those truths exist.

## 9. Repair And Fallback Rules

### 9.1 Invalid Settings

- Invalid persisted settings must degrade to safe defaults.

### 9.2 Invalid Source References

- Dangling active or default source identifiers must be cleared or repaired to an existing saved source.

### 9.3 Invalid Library Metadata

- Corrupt library metadata caches may be discarded and rebuilt from source media.

### 9.4 Invalid DSP Presets

- Corrupt DSP preset records must be repaired or discarded rather than applied as partially valid audio truth.

## 10. Relationship To App-State Contracts

- `SettingsState` projects `SettingsDomain`
- `SourceState` projects `SourceProfilesDomain`
- `CredentialState` projects secure credential and session readiness over `CredentialReferenceDomain`
- `LibraryIndexState` projects runtime readiness over `LibraryMetadataDomain`
- `LibraryContext` depends on loaded library-facing domains
- `PlaybackSession` may project from loaded queue and playable-item sources, but playback itself is not a persistence domain here
- `QueueState` and `SearchState` remain runtime-only by default unless a later spec says otherwise

## 11. Non-Goals

- Storage-engine selection
- Full offline-media binary cache design
- Persisting every transient UI state merely because it exists
