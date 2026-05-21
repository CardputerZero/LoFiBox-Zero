<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Library Scan And Indexing Specification

## 1. Purpose

This document defines how `LoFiBox Zero` discovers media roots, scans indexable content, builds library indexes, refreshes them over time, and repairs or rebuilds them when needed.

It exists to make library readiness a first-class domain rather than a side effect of boot, metadata parsing, or browse-page assumptions.

## 2. Authority

For final product meaning, use `lofibox-zero-final-product-spec.md`.
For architecture ownership, use `project-architecture-spec.md`.
For persistence rules, use `lofibox-zero-persistence-spec.md`.
For shared runtime projection, use `lofibox-zero-app-state-spec.md`.

This document defines the scan, build, refresh, and rebuild contract for the local library index.

## 3. Scope

This document covers:

- which source roots are indexable by the local library scanner
- recursive discovery rules
- candidate media eligibility rules
- index build outputs and derived library views
- scan modes such as initial build, incremental refresh, and full rebuild
- startup readiness relationship to indexing
- repair and rebuild rules for damaged or stale index data

This document does not define:

- remote media-server catalog browsing that is natively exposed by the server
- audio decode behavior
- queue behavior
- page-local selection state

## 4. Indexing Source Scope

### 4.1 Indexable Root Families

The local library index may be built from indexable roots such as:

- the default local media root, `~/Music`
- user-configured local filesystem roots
- LAN or share-backed roots when they are configured as browsable file roots and are available for directory traversal

`/music` is not the product default. It may remain only as an explicit configured root or as a compatibility fallback when no user home can be resolved.
The scanner must not promote `/music` back into the product meaning of the default library.

### 4.1.1 Default Local Media Root

- The default local media root is `~/Music`.
- The default root is materialized from the current user-home context by the source-profile domain, not by page code or target entry points.
- On Linux, if `HOME` is unset, the current user-home context may be resolved from the effective user's account record before any compatibility fallback is considered.
- The default root is used only when `SourceProfilesDomain` has no enabled local media root.
- If the default cannot be resolved from a user home, an implementation may fall back to a compatibility root, but that fallback must be treated as compatibility behavior rather than the product default.

### 4.1.2 Enabled Local Roots

- Enabled local media roots are durable source profiles owned by `SourceProfilesDomain`.
- The scanner must read enabled local media roots from `SourceProfilesDomain` through the application command/query boundary.
- CLI-supplied scan paths are diagnostic or temporary roots. They must not become durable local media roots unless routed through source-profile mutation commands.
- Target entry points such as X11 and device launchers must not define product root defaults directly.

### 4.2 Non-Indexable By Default

The following do not automatically belong to the local library scanner:

- direct remote playback URLs
- radio or station streams
- remote media-server catalogs that already expose their own browse/search APIs

Those sources may still participate in the final product through other capability layers, but they are not silently treated as local scan roots.

## 5. Discovery Rules

### 5.1 Recursive Traversal

- Library discovery is recursive and directory-based.
- Child directories under an enabled media root must be traversed unless explicit exclusion rules are later introduced.

### 5.2 Candidate Eligibility

A filesystem entry is indexable only when:

- it resolves to a supported media file candidate
- it is reachable under an enabled indexable root
- it is not rejected by scan exclusions or corruption handling

### 5.3 Root Normalization

The scanner must normalize configured roots before indexing them so that logically identical roots do not create duplicate index partitions.

## 6. Index Build Outputs

The indexing domain must be able to produce at least:

- track records
- album grouping records
- artist grouping records
- genre grouping records
- composer grouping records
- compilation grouping records
- metadata lookup projections used by browse pages

It must also preserve the product data needed by smart playlists and history-driven behavior:

- `added_time`
- `play_count`
- `last_played`

## 7. Metadata And Fallback Integration

### 7.1 Metadata First

The indexer should prefer extracted metadata when it is available and valid.

### 7.2 Fallback Rules

When metadata is incomplete:

- missing title falls back to filename without extension
- missing album may fall back to the immediate parent directory
- missing artist may fall back to the parent-of-parent directory
- missing genre or composer fall back to explicit `Unknown` values

### 7.3 Unicode Safety

Indexing must preserve Unicode-safe metadata handling, including CJK-capable behavior.

## 8. Scan Modes

### 8.1 Initial Build

The first successful local-library readiness path should support:

- root discovery
- recursive scan
- metadata collection
- index build before ordinary browsing depends on it

### 8.2 Incremental Refresh

The indexing domain should support incremental refresh when:

- configured roots change
- known media files change
- files are added or removed
- the user or system requests refresh

### 8.3 Full Rebuild

A full rebuild should be supported when:

- persisted library metadata is corrupt
- schema or derivation behavior changes incompatibly
- the user explicitly requests a rebuild

## 9. Startup Relationship

- Startup should communicate that the library is loading when indexing work is required.
- The product must not fake scan completion when the index is not actually ready.
- Linux startup may move away from a single blocking boot model later, but current readiness semantics must still truthfully reflect indexing progress.

## 10. Refresh And Rebuild Triggers

The indexing domain should react to triggers such as:

- enabled media roots changed
- source root added, removed, enabled, or disabled
- explicit full rebuild request
- explicit incremental refresh request
- invalid or stale persisted metadata index detected

Adding, removing, enabling, or disabling a local media root marks the library index stale.
A refresh or rebuild command must rescan the current enabled roots from `SourceProfilesDomain`.
Startup must scan enabled roots consistently across the X11 and device targets.

## 11. Repair And Fallback Rules

### 11.1 Invalid Root

- Invalid or unreachable roots must degrade to non-indexable roots rather than corrupting the whole index.

### 11.2 Corrupt Persisted Index

- Corrupt library metadata caches may be discarded and rebuilt from source media.

### 11.3 Partial Index Health

- If only some roots or files fail, the index should preserve the valid portion and mark degraded library readiness rather than pretending to be fully healthy.

## 12. Relationship To Persistence

The indexing domain depends on persisted domains such as:

- `SourceProfilesDomain` for indexable roots
- `LibraryMetadataDomain` for persisted metadata index content
- `ListeningHistoryDomain` for smart-playlist inputs

The indexer may rebuild persisted index content from source media when required.

## 13. Relationship To App-State Contracts

- `LibraryIndexState` projects scan/build/refresh readiness and index health
- `LibraryContext` projects browse filters over a built index
- `PersistenceState` projects whether persisted library domains loaded cleanly

## 14. Non-Goals

- Full-text search ranking
- Remote media-server library synchronization internals
- Audio decode validation during scan

## 14. Current Implementation Convergence

As of 2026-04-27, library readiness is not only scan completion. The implemented governance baseline also includes:

- incremental change classification for added, modified, removed, and unchanged media files
- migration planning with explicit `from_version`, `to_version`, destructive/non-destructive status, and operation descriptions
- smart playlist rules for recently added, recently played, most played, favorites, and never played views
- local and remote unified search grouping with source labels and result explanations
- duplicate and same-recording decisions based on MusicBrainz recording id, fingerprint, and conservative title/artist/duration fallback

The scanner, store, browse pages, and playback history must use this governance layer for indexing decisions instead of deriving library truth from a visible list page.
