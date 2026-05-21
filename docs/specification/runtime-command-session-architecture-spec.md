<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Runtime Command And Session Architecture

## 1. Purpose

This document defines the live runtime command/query boundary for `LoFiBox Zero`.

The boundary exists because live playback, active queue, active EQ, active remote stream state, and immediately effective runtime settings are not durable configuration and are not GUI page state.
They belong to the running LoFiBox instance.

Runtime CLI is one external entry point into this boundary.
The same boundary must serve GUI input, desktop integration, runtime CLI, future automation clients, MCP-style runtime tools, and tests.
The terminal UI is another runtime client: it observes runtime snapshots and emits runtime commands, but it does not own live playback, queue, EQ, remote, lyrics, visualization, library, or diagnostic truth.

## 2. Authority

For enduring source ownership, use `project-architecture-spec.md`.
For durable product commands, use `application-command-boundary-spec.md`.
For shared app-state contracts, use `lofibox-zero-app-state-spec.md`.
For playback behavior, use `lofibox-zero-media-pipeline-spec.md` and `lofibox-zero-final-product-spec.md`.
For EQ/DSP behavior, use `lofibox-zero-audio-dsp-spec.md`.
For streaming behavior, use `lofibox-zero-streaming-spec.md`.
For testing gates, use `testing-ci-spec.md`.

This document defines the runtime command/session layer that sits between interface adapters and the running live domains.

## 3. Distinction Contract

### 3.1 Current Confusions

- `runtime CLI` is being confused with a CLI module. It is an external entry into the runtime command bus.
- `AppRuntimeContext` is being confused with the owner of live playback, queue, EQ, and remote session truth.
- GUI page commands such as selected-row confirm, EQ selected-band movement, and page-local stream detail confirmation are being confused with live runtime commands.
- UI rows such as queue rows, stream detail rows, and diagnostics rows are being confused with structured runtime snapshots.
- IPC transport is being confused with the runtime command protocol.
- direct CLI commands are being confused with commands that mutate the running runtime instance.

### 3.2 Valid Distinctions

- `InterfaceAdapter` receives input from GUI, CLI, desktop, MCP, automation, or tests and submits runtime commands or queries.
- `RuntimeCommandBus` is the transport-neutral in-process boundary that accepts live runtime commands and runtime queries.
- `RuntimeCommandDispatcher` serializes and applies live runtime commands to the running instance.
- `RuntimeQueryDispatcher` returns structured live runtime snapshots.
- `RuntimeCommand` is a mutation request against live runtime truth.
- `RuntimeQuery` is a read request for live runtime truth.
- `RuntimeCommandResult` is the structured outcome of a runtime command.
- `RuntimeSnapshot` is structured live truth before GUI rows, terminal text, desktop notifications, or automation payloads.
- `RuntimeSnapshot` must also be rich enough for terminal-native projection. TUI widgets, runtime CLI JSON, and future MCP-style state queries should project from the same snapshot semantics instead of each deriving state from lower layers.
- `RuntimeCommandKind::LibraryRefresh` is a narrow runtime request to refresh the running instance's in-memory library index from the current durable enabled local roots.
- `RuntimeHost` is the sole in-process owner of live runtime lifetime, tick, runtime domains, runtime bus, server, local client, and optional external transport.
- `RuntimeSessionFacade` is the process-local composition facade over playback runtime, queue runtime, EQ runtime, remote session runtime, live settings runtime, and snapshot assembly. It is not a second controller and must not keep accumulating business logic.
- `PlaybackRuntime`, `QueueRuntime`, `EqRuntime`, `RemoteSessionRuntime`, and `SettingsRuntime` are the live runtime domains that own current session truth.
- `RuntimeSnapshotAssembler` composes domain snapshots into one full runtime snapshot. GUI rows, terminal text, and automation payloads project from that snapshot.
- `RuntimeCommandClient` and `RuntimeCommandServer` are transport-neutral wrappers around the runtime command/query contract.
- `RuntimeTransport` is the transport envelope boundary. It may move commands and queries, but it must not understand playback, queue, EQ, remote, settings, or GUI semantics.
- `AppRuntimeContext` is a GUI shell and runtime client consumer. It may interpret page state into commands and project snapshots into rows, but it does not own runtime services, controllers, runtime session, runtime bus, runtime server, runtime client, transport, callback bridges, or runtime tick.

### 3.3 Invalid Distinctions

- Do not make runtime CLI call `AppRuntimeContext`, page confirm methods, or page field editors.
- Do not make GUI-only selected row or selected EQ band part of runtime truth.
- Do not make UI row text the command/query schema for runtime CLI, desktop integration, or automation.
- Do not let a second process mutate playback, active queue, active EQ, or current remote stream by writing durable state.
- Do not introduce IPC before the transport-neutral command/query/result/snapshot/client/server/transport contract exists.
- Do not let transport details such as Unix sockets, D-Bus, JSON-RPC, stdio, or named pipes define product command semantics.

## 4. Normative Architecture

The runtime command architecture is:

```text
GUI / runtime CLI / desktop / MCP / automation / tests
        |
        v
Runtime Client / Runtime Server / Transport Envelope
        |
        v
Runtime Command And Query Bus
        |
        v
Runtime Session Facade
        |
        v
Playback Runtime / Queue Runtime / EQ Runtime / Remote Session Runtime / Settings Runtime / Runtime Projections
        |
        v
Application Services / Controllers / RuntimeServices
```

The direct CLI remains outside this live runtime layer:

```text
argv -> DirectCliDispatcher -> AppServiceHost/AppServiceRegistry -> durable application services
```

The runtime CLI path is:

```text
argv -> RuntimeCommandClient -> RuntimeCommandServer -> RuntimeCommandBus -> RuntimeSessionFacade
```

The GUI path for live runtime mutation must also use a runtime client:

```text
GUI page interpreter -> in-process RuntimeCommandClient -> RuntimeCommandServer -> RuntimeCommandBus
```

Top-level app ownership is:

```text
LoFiBoxApp
  |-- RuntimeServices
  |-- AppServiceHost
  |-- RuntimeHost
  `-- AppRuntimeContext
```

`RuntimeServices` and `AppControllerSet` are owned by the non-GUI app core host path.
`RuntimeHost` owns live runtime objects and advances them through `RuntimeHost::tick`.
That tick is a runtime mutation and must be serialized through the same
`RuntimeCommandBus` mutex used by external command dispatch and runtime queries.
The host must not call `RuntimeSessionFacade::tick` directly while command
clients can concurrently mutate playback, queue, EQ, remote, settings, or
creator state.
`AppRuntimeContext` receives `AppServiceHost&` and `RuntimeCommandClient&`; it must not construct or retain the runtime host internals.

The external runtime CLI path is:

```text
argv -> UnixSocketRuntimeCommandClient -> UnixSocketRuntimeTransport
     -> UnixSocketRuntimeCommandServer -> RuntimeCommandServer -> RuntimeCommandBus
```

There is no in-process fallback for runtime CLI commands.

## 5. Runtime Domains

### 5.1 Playback Runtime

Playback runtime owns:

- current playback status
- current local track id or current stream identity
- current source label
- elapsed and duration facts
- audio-active and failure/recovery facts that are live-session facts
- playback transport mutations: play, pause, resume, toggle, stop, seek, next, previous

Playback runtime does not own:

- page selection
- Now Playing page UI state
- library browse context
- durable library indexing

### 5.2 Queue Runtime

Queue runtime owns:

- active queue ids
- active queue index
- queue mode facts such as shuffle, repeat-all, and repeat-one when they affect the active queue
- queue mutation version

Queue runtime does not own:

- songs page context
- library list projection
- playlist editor page selection

### 5.3 EQ Runtime

EQ runtime owns:

- currently effective DSP/EQ profile
- enabled or disabled state
- current 10-band gains
- active preset name or custom marker
- runtime-applied DSP update version

EQ runtime does not own:

- Equalizer page selected band
- Equalizer page cursor/focus/scroll state
- small-screen layout or help text

### 5.4 Remote Session Runtime

Remote session runtime owns:

- current active remote profile identity when a remote stream is selected or playing
- current remote source session facts
- current resolved stream diagnostics
- buffer, degraded, recovery, quality, codec, bitrate, live, and seekable facts for the active stream

Remote session runtime does not own:

- durable source profile fields
- credential secret lifecycle
- remote browse page selection
- remote catalog cache persistence policy

Source configuration remains owned by `SourceProfileCommandService`.
Remote browse/search/resolve query behavior remains owned by `RemoteBrowseQueryService`.
Remote session runtime owns only the active live-session truth.
Remote playback must enter runtime through explicit resolved runtime payloads such as resolved stream payloads or resolved library-track payloads.
Runtime code must not call back into `AppRuntimeContext` to resolve or start a GUI-selected remote stream.

### 5.5 Live Settings Runtime

Live settings runtime owns only settings that immediately affect a running session, such as active output mode, active network policy, active sleep timer, or active projection mode once those settings exist.

Durable settings persistence remains an application/persistence concern.

### 5.6 Library Refresh Runtime Request

The runtime layer may expose a narrow library refresh command for a running
instance.

This command:

- reloads enabled local media roots from durable `SourceProfilesDomain`
- starts or performs a library refresh/rebuild for the running instance
- updates in-memory library readiness and scan progress through existing application services

This command must not:

- add, remove, enable, disable, or persist local roots
- write `SourceProfilesDomain`
- redefine default media-root semantics
- treat runtime state as durable source-profile truth

## 6. Command And Query Contract

Runtime commands must be represented by an explicit command object with:

- `RuntimeCommandKind`
- `RuntimeCommandPayload`
- `CommandOrigin`
- `CorrelationId`

Runtime queries must be represented by an explicit query object with:

- `RuntimeQueryKind`
- `CommandOrigin`
- `CorrelationId`

Runtime command results must include:

- `accepted`
- `applied`
- stable `code`
- human `message`
- `origin`
- `correlation_id`
- `version_before_apply`
- `version_after_apply`

`RuntimeCommandPayload` must be a tagged payload contract. It must not be a loose struct where every command shares unrelated optional fields such as track id, seek seconds, EQ band index, and preset name.

Each command kind must validate the payload tag it needs. A command with the wrong payload type must not be silently interpreted through unrelated fields.

Runtime snapshots must be structured objects, not pre-rendered text.
GUI rows, terminal output, desktop notifications, and automation payloads are projections over runtime snapshots.
The full runtime snapshot may include projection-only domains such as visualization, lyrics, library readiness, source summary, diagnostics, and creator-analysis placeholders when those facts are derived from the running instance. These projections do not make the TUI or CLI owners of the underlying services.

Every public `RuntimeCommandKind` must either be implemented or removed from the contract.
Formal command kinds must not remain as permanently unsupported placeholders.

## 6.1 Runtime Truth And Projection Guardrails

Runtime command/query code is the boundary between product truth and interface
projection. It must not turn interface fallbacks into playback facts.

Required guardrails:

- A command must return `applied=false` when the requested runtime mutation could
  not actually be applied. It must not report success and leave the session in a
  half-updated state.
- Queue stepping or queue jumping to a remote item must either use an explicit
  resolved remote-library-track payload/starter or fail with `applied=false`.
  It must not fall through to local path playback with an empty or synthetic
  path.
- Remote search confirmation and remote browse playback must enter playback
  through a governed remote media projection. When a local library projection is
  created for a remote item, that projection must carry the stable remote
  identity needed by runtime snapshots, metadata governance, lyrics lookup, and
  queue display.
- Playback snapshots must describe the active session facts. They must not
  derive title, artist, lyrics, stream URL, or queue truth from rendered GUI/TUI
  rows.
- A playback session may retain the selected/current item after backend failure,
  but `PlaybackStatus::Playing` is only valid when the backend start/resume has
  been accepted and the session is audio-active. Backend refusal, backend
  failure, or inactive resume must remain `Paused` with `audio_active=false` and
  must return `applied=false` for the runtime command that failed to create live
  audio.
- A local audio backend must not treat process creation alone as playback
  acceptance. For local file playback, the backend must observe a bounded
  output-start confirmation, or return failure to the playback runtime. A
  one-time retry is allowed for transient sink teardown/startup races, but a
  second unconfirmed start must remain a failed command rather than a fake
  `PLAYING` projection.
- Starting the currently active track id is idempotent. If the current session
  already has the same track id, `PlaybackStatus::Playing`, and
  `audio_active=true`, `PlaybackStartTrack` must return applied without
  destroying and recreating the audio backend. Presentation targets may use this
  to navigate from a list row to Now Playing without interrupting sound.
- `Paused + current_track_id + audio_active=false` is not automatically a
  resumable audio session. If the retained item is at or beyond its known
  duration, `play`, `resume`, and GUI/TUI play-toggle commands must restart the
  current media item from the beginning through the same runtime start path
  used by `PlaybackStartTrack`. They must not seek to the exact end of the file
  and report a successful play-toggle while the backend immediately exits.
- Seek is a playback restart at a requested offset. If backend start refuses
  that offset, including an offset at or beyond EOF, the session must become
  `Paused` with `audio_active=false`, and the runtime command result must be
  `applied=false`. The previous `PLAYING` projection must not survive a failed
  seek.
- Presentation targets may display honest fallback states, but those fallback
  states must not feed back into runtime truth.

Commands whose names describe setting a state must be idempotent. `shuffle on`
and `shuffle off` are state-setting runtime commands, not toggles. Toggle
semantics must remain explicit through `PlaybackToggleShuffle` or a command
whose public spelling says `toggle`.

## 7. Serialization And Instance Boundary

All live runtime mutations must pass through one serialized runtime command dispatcher in the running instance.
Runtime ticking is also a live mutation. It advances playback time, backend
state, visualization, lyrics-line projection, queue completion, and creator
analysis. Therefore ticking must be serialized by the runtime bus, not by a
parallel host-side shortcut around the bus.

This applies equally to:

- GUI input
- desktop media keys
- runtime CLI
- future automation
- future MCP runtime tools

The single-instance lock prevents multiple live runtime owners.
It does not authorize another process to mutate live runtime state directly.

## 8. Source Placement

Runtime command/session code belongs under `src/runtime/`.

Required first-stage files are:

```text
src/runtime/
  runtime_command.*
  runtime_result.*
  runtime_snapshot.*
  runtime_session_facade.*
  runtime_host.*
  playback_runtime.*
  queue_runtime.*
  eq_runtime.*
  eq_runtime_state.*
  remote_session_runtime.*
  settings_runtime.*
  settings_runtime_state.*
  runtime_snapshot_assembler.*
  runtime_command_dispatcher.*
  runtime_query_dispatcher.*
  runtime_command_bus.*
  runtime_command_client.*
  runtime_command_server.*
  runtime_transport.*
  in_process_runtime_client.*
  runtime_envelope_serializer.*
  unix_socket_runtime_transport.*
```

The transport-neutral client/server/transport contracts live in `src/runtime`.
The current Linux-first external transport is a Unix domain socket with length-prefixed runtime envelopes.
It consumes the runtime contract instead of redefining command semantics.

## 9. Relationship To GUI

`AppRuntimeContext` remains the GUI runtime shell.

It may own:

- GUI input routing
- page navigation
- page-local selection/focus/help state
- page projection assembly
- temporary GUI browse state while a dedicated runtime domain is not yet owning it

It must not own:

- `RuntimeServices`
- `AppControllerSet`
- `RuntimeHost`
- `RuntimeSessionFacade`
- `RuntimeCommandBus`
- `RuntimeCommandServer`
- `InProcessRuntimeCommandClient`
- external runtime transport
- runtime tick/update
- runtime-to-GUI callback bridges for remote playback

It must expose GUI live mutation through an in-process `RuntimeCommandClient`.
Page projections that need live playback, queue, EQ, or active remote-session facts must query runtime snapshots through the client.

Whenever GUI input changes live playback, active queue, active EQ, active remote session, or live settings, the page interpreter must submit a runtime command.
GUI-only motion such as moving a list selection or moving the selected EQ band may remain page state.
EQ runtime truth belongs in `EqRuntimeState`; GUI selected-band state belongs in `EqUiState`.
Live settings truth belongs in `SettingsRuntimeState`; GUI settings row/index state belongs in `SettingsUiState`.

The GUI lifecycle helper may refresh status, load the library, and advance boot-page navigation.
It must not advance playback; live playback advancement belongs to `RuntimeHost::tick`.

## 10. First Implementation Scope

The current implementation scope has established:

- playback
- queue
- EQ
- active remote session
- live settings
- runtime host ownership
- transport-neutral client/server interfaces
- in-process GUI client path
- Unix socket runtime transport
- runtime CLI commands and queries over the external transport

Runtime domain tests must validate the domain objects directly so behavior does not slide back into `RuntimeSessionFacade`.
Client/server and transport tests must validate result envelopes, correlation ids, command origins, version-before/version-after facts, query consistency, socket transport behavior, runtime CLI behavior, and host shutdown/reload request propagation.

## 11. AI Constraints

- Do not implement runtime CLI commands until this runtime bus exists and tests prove GUI can use it.
- Do not add transport before the transport-neutral command/query/result/snapshot contract is in place.
- Do not let `src/cli` include runtime domains, runtime bus, runtime server, `AppRuntimeContext`, controllers, or UI pages for live-state control; runtime CLI must use a runtime command client and external transport.
- Do not add live playback, queue, EQ, or remote-session mutation methods to `AppRuntimeContext`.
- Do not allow UI rows to become runtime command/query payloads.
- Do not add new concrete playback, queue, EQ, remote, or settings business logic to `RuntimeSessionFacade`; add it to the matching runtime domain.
- Do not make `AppCommandExecutor` construct `RuntimeCommandBus` or `RuntimeSessionFacade`. It may interpret GUI input and submit runtime commands through its target.
- Do not make transport include runtime domain headers. Transport sees only command/query/result/snapshot envelopes.
- Do not reintroduce runtime-to-GUI callbacks for remote playback. GUI may resolve a stream through application query services and submit an explicit runtime command payload; runtime host owns the subsequent playback and active remote-session mutation.

## 12. Runtime Event Stream

The runtime transport also owns a streaming observation mode.

This mode is distinct from command dispatch and one-shot query:

- `RuntimeCommand` mutates live runtime truth.
- `RuntimeQuery` returns one structured snapshot.
- `RuntimeEvent` publishes ordered changes and periodic snapshot heartbeats to
  external observers such as TUI, automation, tests, and future MCP clients.

The event stream must not introduce a second runtime truth source. Every event
is generated from the same `RuntimeSnapshot` projection used by runtime CLI and
TUI, and every event carries the full snapshot plus event-specific payload
fields.

The current Unix socket transport supports an `event_stream` envelope. The
server keeps the stream connection open, emits `runtime.connected` and
`runtime.snapshot` on connect, and continues accepting other command/query
clients concurrently. Stream event versions are monotonic per stream so
progress, visualization, lyrics-line, and creator-analysis updates can be
observed even when command-result version truth has not changed.

The canonical event families are:

```text
runtime.connected
runtime.disconnected
runtime.snapshot
playback.changed
playback.progress
playback.error
queue.changed
eq.changed
remote.changed
settings.changed
lyrics.changed
lyrics.line_changed
visualization.frame
library.scan_started
library.scan_progress
library.scan_completed
diagnostics.changed
creator.changed
```
