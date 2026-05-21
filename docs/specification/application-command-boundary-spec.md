<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Application Command Boundary Specification

## 1. Purpose

This document defines the product command/query boundary that preserves `LoFiBox Zero` as it gains more runtime shells, command consumers, and automation surfaces over time.

It exists to keep product commands from being trapped inside the current GUI runtime shape.
Without this boundary, every new consumer will be tempted to borrow UI flows, call controllers directly, or reach into runtime providers in its own way.
That would make the project harder to evolve, harder to test, and easier to drift.

CLI is one beneficiary of this refactor.
It is not the reason the boundary exists.
The same boundary must serve GUI routing, desktop integration, the current direct CLI, the current runtime command transport, future automation clients, and internal tests.

This document is about long-term command/query ownership.
It is not the CLI command tree.
It is also not a CLI-enablement document with a broader-sounding name: the boundary exists so LoFiBox can keep one explainable product command model as GUI pages, desktop integration, automation, tests, runtime IPC, and later terminal entry points evolve independently.

## 2. Authority

For enduring architecture and source ownership, use `project-architecture-spec.md`.
For integrated product facts, use `integrated-product-core-spec.md`.
For shared app-state contracts, use `lofibox-zero-app-state-spec.md`.
For persistence domains, use `lofibox-zero-persistence-spec.md`.
For desktop event handoff, use `linux-desktop-integration-spec.md`.
For live runtime commands and snapshots, use `runtime-command-session-architecture-spec.md`.
For installed terminal syntax, output modes, and CLI exit-code contracts, use `lofibox-cli-surface-spec.md`.
For testing gates, use `testing-ci-spec.md`.

This document defines the application command/query boundary that sits between runtime shells and product domains.
It does not define the live runtime command bus; live playback, active queue, active EQ, active remote session, and live settings are governed by `runtime-command-session-architecture-spec.md`.
It also does not define terminal command syntax; the CLI surface is governed by `lofibox-cli-surface-spec.md`.

## 3. Scope

This document covers:

- the distinction between UI/page commands and product commands
- the application command/query surface needed by GUI, desktop integration, CLI, automation clients, and tests
- direct commands versus commands that must address a running runtime
- service registry and service-family boundaries
- allowed relationship to controllers, runtime services, and platform adapters
- migration constraints for refactoring `AppRuntimeContext`, playback, library, source profile, remote browse, metadata, EQ, credentials, cache, and diagnostics behavior

This document does not define:

- final CLI syntax
- argument parsing rules
- terminal output formatting
- shell completion behavior
- IPC transport technology
- a new persistence engine
- a new UI page set

## 4. Distinction Contract

### 4.1 Current Confusions

- `CLI integration` is being confused with one downstream benefit of a broader evolution boundary.
- `AppRuntimeContext` is being treated as both a GUI runtime shell and a product command object.
- page confirmation methods such as `confirmMainMenu`, `handleSearchConfirm`, or remote-profile editor character handlers are being confused with product actions.
- `RuntimeServices` is being confused with an application service layer. It is a capability registry, not a command surface.
- `PlaybackController` and `LibraryController` are being pulled toward public command APIs even though they still carry orchestration and GUI helper responsibilities.
- `targets` command-line parsing is being confused with a real product CLI.

### 4.2 Valid Distinctions

- `ProductCommand` is a product action such as play, pause, scan, browse, search, update source profile, apply EQ, or set credentials.
- `ProductQuery` is a structured read such as playback status, library status, source profile list, search results, remote browse results, or diagnostics.
- `PageCommand` is a UI event interpretation such as confirm selected row, move selection, edit focused field, or open help.
- `ApplicationService` is a stable product-facing service that executes product commands and queries without depending on page selection state or runtime-shell details.
- `AppServiceRegistry` is the composition object that groups application services for command clients.
- `GuiCommandRouter` translates page/input events into product commands and product queries.
- `CliDispatcher` parses terminal arguments and calls application services. It does not own product behavior.
- `AutomationAdapter` or future MCP-facing code translates external automation requests into product commands and product queries.
- `RuntimeCommandClient` sends runtime commands to an already-running app process when a command must affect live playback or live runtime state.
- `RuntimeCommandServer` lives inside the running app process and invokes the same application services used by GUI code.
- `DirectCommand` is safe to execute in a short-lived process without owning the live playback runtime.
- `RuntimeCommand` must target the running LoFiBox instance because it concerns live playback, active queue, current output, or in-memory runtime truth.

### 4.3 Invalid Distinctions

- Do not define external product commands by reusing page names or selection flows.
- Do not make `AppRuntimeContext` the public command API for GUI routing, desktop integration, CLI, automation, or tests.
- Do not make `targets` own product command behavior.
- Do not let each command consumer directly pick providers out of `RuntimeServices`.
- Do not expose remote-profile character editing or page commit methods as product source-profile commands.
- Do not let desktop adapters, CLI code, or automation clients talk directly to playback backends, remote protocol providers, metadata providers, credential stores, or UI pages.
- Do not let direct commands mutate live playback or active queue state behind the running app's back.

## 5. Normative Architecture

The target structure is:

```text
GUI pages / desktop adapters / CLI / future automation / tests
        |
        v
Application Command And Query Boundary
        |
        v
Playback / Queue / Library / Remote / Metadata / EQ / Credentials / Cache / Diagnostics
        |
        v
RuntimeServices capability groups and platform adapters
```

Runtime shells adapt input/output and process boundaries.
They do not define product command semantics.

The GUI path should become:

```text
InputEvent -> GuiCommandRouter -> ApplicationService -> product/domain/runtime services
```

The current direct CLI path is:

```text
argv -> DirectCliDispatcher -> AppServiceHost/AppServiceRegistry -> ApplicationService
```

Runtime CLI commands use:

```text
argv -> CliDispatcher -> RuntimeCommandClient -> running RuntimeCommandServer -> RuntimeCommandBus -> RuntimeSessionFacade
```

The desktop integration path should become:

```text
D-Bus/MPRIS/media-key/MIME event -> DesktopCommandAdapter -> ApplicationService or RuntimeCommandClient
```

## 6. Service Families

The application command/query boundary should be organized into explicit service families.
The exact file names may evolve, but the responsibility boundaries must remain stable.

### 6.1 Playback Command Service

Owns product playback commands such as:

- play local track by stable media or track id
- play resolved URI or stream
- pause
- resume
- toggle play/pause
- stop when product behavior requires a stop command
- next and previous
- seek when seek capability exists
- set shuffle
- set repeat mode

It must return structured command results.
It must not expose page selection state or raw backend objects.

### 6.2 Queue Command Service

Owns product queue mutations such as:

- add media item
- remove media item
- move media item
- clear queue
- set active queue position
- rebuild queue from a product query result when explicitly requested

It must not make callers know about GUI list selection or page row indexes.

### 6.3 Playback Status Query Service

Owns structured reads such as:

- current item
- elapsed and duration
- playback status
- source label
- queue position
- shuffle and repeat mode
- stream diagnostics visible to non-debug command clients

It must read playback facts; it must not trigger enrichment or source browsing merely because a client asked for status.

### 6.4 Library Query Service

Owns product library reads such as:

- list tracks
- list albums
- list artists
- list genres
- list composers
- query by filters
- fetch track detail
- library readiness and scan status
- search local and reachable remote results through normalized `MediaItem` values

It must not own scanning, durable indexing, remote provider access, or UI row rendering.

### 6.5 Library Mutation Service

Owns product library mutations such as:

- scan
- refresh
- rebuild
- accept metadata corrections
- update play-count and recently-played facts when commanded by playback workflow

It must not own current GUI browse context.
It may refresh or rebuild from enabled local roots, but the durable local-root lifecycle belongs to `SourceProfileCommandService`.
CLI-supplied scan paths are temporary diagnostic roots unless a source-profile command persists them.

### 6.6 Library Open Action Service

Owns product-level open semantics for explicit object references such as:

- open track
- open album
- open artist
- open playlist
- open source profile root

GUI-only selected-row confirmation may use this service after resolving the selected row into a product reference.
CLI must call it with explicit ids or references, not row selection.

### 6.7 Source Profile Command Service

Owns source-profile lifecycle:

- create profile
- update profile
- delete profile
- validate profile
- persist profiles
- list profiles
- set active/default profile when product behavior allows it
- attach or rotate credential references
- probe profile availability
- add local media root
- remove local media root
- enable or disable local media root
- list local media roots
- project enabled local media roots for library refresh
- set default local media root if product behavior exposes it

It must not expose character-by-character field editing.
GUI remote setup pages are only one editor frontend over this service.
Local media root profiles are durable `SourceProfilesDomain` objects, not library-index internals.

### 6.8 Remote Browse Query Service

Owns remote catalog and stream queries:

- browse root
- browse child path
- list artists/albums/playlists/folders/genres/favorites/recent views where supported
- search within source
- resolve playable item
- expose provider capability and degraded-state facts

It must return normalized catalog nodes and playable references.
It must not start playback by itself unless invoked through a playback command.

### 6.9 Metadata Command Service

Owns metadata and enrichment commands:

- request enrichment
- accept or reject enrichment proposal
- cache accepted facts
- write back metadata only when capability and policy allow it
- explain metadata/artwork/lyrics provenance

It must not decide UI layout and must not bypass authority, confidence, or writeback policy.

### 6.10 EQ Command Service

Owns product DSP commands:

- apply current compact EQ state
- apply preset
- reset EQ
- toggle bypass when exposed
- persist DSP preset/profile choices when the relevant domain exists
- report active DSP profile summary

It must update playback through the DSP chain and must not restart playback merely to apply a parameter change.

### 6.11 Credential Command Service

Owns credential lifecycle commands:

- set secret
- update secret
- delete or revoke secret reference
- validate credential availability
- report safe credential status

It must never print, log, or return raw secret material in ordinary command results.

### 6.12 Cache Command Service

Owns cache/offline commands:

- report cache usage
- clear selected cache buckets
- schedule or cancel offline sync where supported
- run garbage collection

It must respect cache policy and must not delete arbitrary paths outside governed cache buckets.

### 6.13 Runtime Diagnostic Service

Owns diagnostic queries and safe repair actions:

- runtime service availability
- provider availability
- media backend availability
- desktop integration availability
- source profile probe summary
- redacted stream diagnostics
- packaging/runtime dependency checks

It must redact sensitive data and must not become a backdoor to provider internals.

## 7. Direct Commands And Runtime Commands

Every non-GUI command consumer must split command execution into two modes.
This distinction is about state ownership, not about terminal syntax.

### 7.1 Direct Commands

Direct commands are safe to execute in a short-lived process because they read or mutate durable configuration, cache, library, or diagnostic state without owning live playback.

Examples:

- library scan
- library status
- source local-root add/remove/enable/disable/list
- source profile add/update/delete/list
- credential set/delete/status
- cache status/clear
- diagnostics/doctor
- provider probe that writes no live playback state

Direct commands may create an `AppServiceRegistry` with host runtime capabilities and then exit.
They must follow XDG paths and locking rules for persistent domains.

### 7.2 Runtime Commands

Runtime commands must be sent to the running LoFiBox process because they touch live playback, current output, active queue, or in-memory app state.

Examples:

- play
- pause
- resume
- next
- previous
- seek
- queue add/remove/move when it affects the active live queue
- now/status for live playback
- apply EQ to currently playing audio
- request library refresh in the running instance when it reloads durable enabled roots and rebuilds only in-memory library state

Runtime commands must use the implemented `RuntimeCommandClient` and `RuntimeCommandServer` path.
They must not start a second independent app runtime to control playback.
They must not mutate persisted state behind the running app's back to simulate live state changes.
Runtime library refresh commands must not add, remove, enable, disable, or persist local roots.
Those durable mutations remain direct source-profile commands.
The transport-neutral runtime command/query/result/snapshot contract is the authority above the Unix socket transport and any future transport.

### 7.3 Ambiguous Commands

Some commands can have both direct and runtime forms.

Examples:

- `eq preset apply` may be a persisted default update when no runtime is targeted, or a live runtime update when a player is running.
- `source probe` can be a direct diagnostic query, while `source play` is a runtime playback command.
- `queue import` can prepare persisted playlist data directly, while `queue add` to the active queue is a runtime command.

Ambiguous commands must state their mode explicitly in the application service design before any external command surface is added.

## 8. Refactoring Rules

### 8.1 `AppRuntimeContext`

`AppRuntimeContext` is the GUI shell and runtime-client consumer.

It may own:

- render target composition
- input event routing hookup
- page navigation state
- page-local remote browse/search/edit buffers
- GUI-local EQ/settings selection state
- references to `AppServiceHost` and `RuntimeCommandClient`

It must not be the long-term owner of:

- `RuntimeServices`
- `AppControllerSet`
- `RuntimeHost`
- `RuntimeSessionFacade`
- `RuntimeCommandBus`
- `RuntimeCommandServer`
- `InProcessRuntimeCommandClient`
- external runtime transports
- live playback tick
- product playback command semantics
- source-profile product mutation semantics
- search execution truth
- remote browse command truth
- credential lifecycle commands
- EQ/DSP product command semantics
- command results intended for reuse by GUI, desktop integration, CLI, automation, or tests

`LoFiBoxApp` or another top-level app host owns `RuntimeServices`, `AppServiceHost`, `RuntimeHost`, and `AppRuntimeContext` as separate objects.
`RuntimeHost::tick` advances live playback/runtime state before the GUI shell updates its page lifecycle.
`AppRuntimeContext` may interpret GUI page selections into application-service queries and runtime command payloads, but all live runtime mutation must be submitted through `RuntimeCommandClient`.
It must not reach through `AppServiceHost::controllers()` or `AppServiceHost::services()` directly.

### 8.2 GUI Command Router

GUI command routing owns page interpretation only.

It may translate:

- selected row confirm
- focus field edit
- page back
- page help
- shortcut key

into application service calls.

It must not become the product command source of truth.

### 8.3 Controllers

Controllers may remain internal coordination objects while behavior is migrated.

They must not become public command boundaries.
Application services may compose controllers temporarily during migration, but callers must see product commands and structured results, not controller implementation details.

### 8.4 Runtime Services

`RuntimeServices` remains a capability registry grouped by domain.

Application services may consume grouped runtime capabilities.
Command clients must not directly fish providers out of `RuntimeServices`.

### 8.5 Targets

`src/targets` must remain thin.

Target entry points may:

- parse high-level startup mode
- construct host runtime/service registry
- dispatch to a CLI entry adapter
- launch a GUI/device runner

They must not contain product command implementations.

### 8.6 Source Placement

Application command/query boundary source placement is:

```text
src/application/
  app_service_host.*
  app_service_registry.*
  playback_command_service.*
  queue_command_service.*
  playback_status_query_service.*
  library_query_service.*
  library_mutation_service.*
  library_open_action_service.*
  source_profile_command_service.*
  remote_browse_query_service.*
  metadata_command_service.*
  eq_command_service.*
  credential_command_service.*
  cache_command_service.*
  runtime_diagnostic_service.*
```

`src/cli/` exists for terminal parsing, dispatch, and formatting only.
Direct CLI depends on `src/application/`.
Runtime CLI depends on the runtime command/client/result/snapshot contract and the external transport client.
`src/cli/` must not depend on `src/app/AppRuntimeContext`, UI pages, controllers, runtime domains, runtime bus, runtime server, or provider implementations.

No empty aspirational source directory should be created before it has real files and implementation placement checks.

## 9. Result Model

Application services must return structured results rather than print or render directly.

A command result should be able to express:

- success or failure
- stable machine-readable code
- human summary
- optional structured payload
- redacted diagnostics
- retryability where relevant

Page rendering, desktop notification text, terminal formatting, and automation payload conversion are separate projection responsibilities.

## 10. Persistence And Locking

Direct commands that modify persisted domains must follow the persistence spec and use safe domain-level locking when concurrent GUI/runtime access is possible.

Runtime commands that modify live state must go through the runtime command server.

Single-instance locking is not a substitute for command-mode design.
The lock prevents multiple live runtime owners; it does not authorize a second CLI process to mutate in-memory playback state directly.

## 11. Relationship To Current Code

The current code now contains the application command/query boundary and the live runtime command/session boundary:

- `LoFiBoxApp` owns `RuntimeServices`, `AppServiceHost`, `RuntimeHost`, and `AppRuntimeContext` as separate top-level objects.
- `RuntimeHost` owns the live runtime session, runtime bus, runtime server, local in-process client, external Unix socket server, runtime EQ/settings state, shutdown/reload flags, and live runtime tick.
- `AppRuntimeContext` is a GUI shell and runtime-client consumer, not a public command API and not the live runtime host.
- `app_command_executor` owns page command execution semantics, not general product command semantics.
- `AppServiceRegistry` groups current application services for GUI routing, direct CLI, runtime host composition, and tests.
- `AppServiceHost` composes the current app/controller/runtime objects needed by direct command consumers without making targets or `src/cli` construct product internals directly.
- `playback_command_service`, `queue_command_service`, `playback_status_query_service`, `library_query_service`, `library_mutation_service`, `library_open_action_service`, `source_profile_command_service`, `remote_browse_query_service`, `credential_command_service`, `cache_command_service`, and `runtime_diagnostic_service` are current service-family implementations.
- `RuntimeServices` groups capabilities but is not an application service registry.
- `src/cli/direct_cli.*` is the direct terminal adapter for durable source, credential, library, cache, and doctor commands.
- `src/cli/runtime_cli.*` is the runtime terminal adapter for live playback, active queue, active EQ, active remote session, live settings, and runtime shutdown/reload commands. It talks to the running app through external runtime transport and has no in-process fallback.
- `targets/cli_options.cpp` remains startup-option handling and positional-open support, not the reusable product command surface.

Future refactors must preserve working behavior while keeping durable product commands behind application services and live commands behind the runtime command/client/server path.

## 12. Required Machine Gates

The architecture and implementation-placement checks must grow as this boundary becomes code.

They must reject current and future drift such as:

- `src/cli` including UI pages or `AppRuntimeContext`
- `src/targets` owning command implementations
- desktop adapters directly calling playback backends or remote providers
- application services directly including concrete platform adapters
- command clients accessing flat provider internals rather than application services
- GUI remote-profile field editors being used as product source-profile mutation APIs
- direct external commands mutating live playback or active queue state without a runtime command client
- automation or desktop adapters mutating live playback or active queue state without a runtime command path
- `AppRuntimeContext` owning `RuntimeHost`, runtime bus/server/session/client internals, runtime transports, `RuntimeServices`, `AppControllerSet`, or live runtime tick
- `AppRuntimeContext` calling `AppServiceHost::controllers()` or `AppServiceHost::services()` directly
- runtime CLI constructing app services, controllers, runtime domains, runtime bus, runtime server, or `AppRuntimeContext`

Implementation steps after this specification must update checks only when corresponding source files exist.
Checks must not force empty aspirational directories.

## 13. AI Constraints For Future Work

- Do not implement new external command surfaces outside the application command/query boundary.
- Do not treat page names, row indexes, or field-edit states as external-facing product objects.
- Do not make `AppRuntimeContext` a convenient public API or host for product/runtime commands.
- Do not expose controllers as the stable external command boundary.
- Do not create a `src/cli` command tree that directly calls runtime providers.
- Do not add a runtime command without explicitly selecting whether it is a direct application command or a live runtime command.
- Do not add a new runtime transport that bypasses the transport-neutral runtime command/session contract in `runtime-command-session-architecture-spec.md`.
- If a future requirement changes what is direct versus runtime, update this specification before changing command code.

## 14. 2026-04-28 Remote / Credential / Direct CLI Boundary Regression

This update clarifies and records the next boundary step after the first application-service pass.

- `RemoteBrowseQueryService` is the product-facing owner for remote catalog queries, remote source search, playable-node normalization, remote stream resolution, remote directory cache use, recent-browse cache updates, local read-only remote track fact caching, provider capability facts, degraded-state facts, source diagnostics, and stream diagnostics.
- GUI `RemoteBrowse`, `ServerDiagnostics`, `StreamDetail`, and Search flows may keep page-local selected profile, selected parent, selected node, and selected stream state, but they must consume structured remote query results and project them into rows. They must not call remote catalog providers or stream resolvers directly.
- Server diagnostics and stream detail truth are structured application query results before they become UI rows or terminal lines. The reusable truth includes source identity, provider family, connection status, credential reference status, TLS policy, permission, token redaction status, resolved URL redaction, buffer state, recovery action, quality preference, bitrate, codec, sample rate, channel count, live/seekable flags, and provider capability facts.
- `CredentialCommandService` owns secret set/status/delete behavior. `SourceProfileCommandService` owns profile lifecycle, profile fields, credential-reference attachment, readiness, capability labels, permission labels, TLS toggles, persistence, and probe. Source-profile text editing must not write passwords or API tokens directly.
- `AppServiceHost` is the direct-command composition helper. It exists so direct CLI and tests can build application services without treating `AppRuntimeContext`, targets, controllers, or runtime provider groups as their public command boundary.
- Direct CLI parses and formats durable source, credential, library, cache, and doctor commands, creates the existing service registry, and calls application services. It does not control live playback, active queue, current output, GUI page state, UI pages, concrete platform adapters, or provider internals.
- Runtime CLI parses and formats live playback, active queue, active EQ, active remote session, live settings, shutdown, and reload commands. It must use `RuntimeCommandClient` over the external runtime transport and must not construct a second runtime instance or fall back to application services.
