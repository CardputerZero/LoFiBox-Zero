<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox CLI Surface Specification

## 1. Purpose

This document defines the terminal command surface for the installed executable:

```text
lofibox
```

The CLI is an interface adapter over product objects. It is not a page macro
language and it must not simulate GUI navigation.

The same executable serves two audiences:

- user CLI: default, human-oriented output
- machine CLI: stable `--json` and `--porcelain` output for scripts, MCP,
  systemd, package tests, and automation

The CLI surface is downstream from the application command/query boundary and
the runtime command/session boundary. It does not own playback, queue, source,
credential, cache, library, EQ, metadata, lyrics, artwork, fingerprint, desktop,
or persistence semantics.

## 2. Authority

Use `application-command-boundary-spec.md` for durable product command
ownership.

Use `runtime-command-session-architecture-spec.md` for live playback, active
queue, active EQ, active remote session, and live settings.

Use the domain specifications for product truth:

- `lofibox-zero-app-state-spec.md`
- `lofibox-zero-persistence-spec.md`
- `lofibox-zero-credential-spec.md`
- `lofibox-zero-library-indexing-spec.md`
- `lofibox-zero-streaming-spec.md`
- `lofibox-zero-audio-dsp-spec.md`
- `lofibox-zero-track-identity-spec.md`
- `linux-desktop-integration-spec.md`

## 3. Distinction Contract

### 3.1 Current Confusions

- `CLI command` can be confused with `PageCommand`.
- `source` can be confused with `remote`.
- `search`, `library query`, `remote search`, and `open` can be collapsed into
  one overloaded verb.
- durable settings, live settings, and product settings can be treated as one
  storage bucket.
- credential refs can be treated as raw secrets.
- runtime status text can be treated as the runtime truth schema.

### 3.2 Valid Distinctions

- `source` owns durable remote/source profile configuration, authentication
  attachment status, probe, and capability reporting.
- `remote` owns connected remote catalog browsing, search, resolution, and
  stream diagnostics.
- `library` owns local index mutation and local library reads.
- `search` is the unified local/remote product search entry and returns grouped
  media-item semantics.
- `open` turns an explicit path, URL, stream, or media reference into a playback
  session request; it is not source configuration or catalog browsing.
- `play` owns live playback transport and explicit playback-session starts.
- `queue` owns the live active queue.
- `settings` owns product-level preferences such as sleep timer, backlight, and
  language.
- `config` owns ordinary non-secret configuration values.
- `credentials` owns secret references and safe credential status. It never
  prints raw secret material.
- `cache` owns governed cache buckets and offline plans.
- `metadata`, `lyrics`, `artwork`, and `fingerprint` are separate enrichment,
  acceptance, cache, identity, and writeback command families.

### 3.3 Invalid Distinctions

- Do not expose `ui press left`, selected-row confirmation, field focus, or page
  cursor state as formal CLI commands.
- Do not make source-profile text editors the credential command surface.
- Do not make remote browse commands mutate playback unless they explicitly
  hand a resolved playable object to the playback runtime.
- Do not allow a direct short-lived CLI process to mutate live playback, active
  queue, active EQ, or current remote stream state.
- Do not print raw passwords, tokens, resolved secret-bearing URLs, or provider
  headers in user or machine output.

## 4. Global Output Contract

All state-oriented CLI commands must support stable machine output as the
implementation for that command lands.

Global output options:

```text
--json
--porcelain
--fields <comma-separated-field-list>
--quiet
```

Rules:

- default output is human readable
- `--json` emits structured JSON objects or arrays
- `--porcelain` emits stable tab-separated records
- `--fields` filters object fields when the command returns named fields
- `--quiet` suppresses stdout while preserving the exit code
- command result text is a projection; it is not the product truth schema

## 4.1 Agent Discovery And Error Contract

The CLI is also an automation and agent interface. A capable agent must be able
to discover commands, risks, fields, required runtime state, mutation behavior,
and exit-code meaning without reading source code or scraping prose help.

The installed executable must therefore expose a stable discovery surface:

```text
lofibox commands --json
lofibox help --json
lofibox help <family> [verb] --json
lofibox <family> [verb] --schema
```

Discovery output must include at least:

- command path
- summary
- whether the command mutates product or runtime state
- whether the command requires a running runtime instance
- accepted options and positional arguments
- field names accepted by `--fields`
- JSON output shape at the command-family level
- stable exit-code meanings
- security notes where secrets, remote URLs, writeback, or deletion are involved

Human help and machine help must follow the same command routing rules.

Required help behavior:

```text
lofibox help                 == lofibox --help
lofibox <family> --help      shows family help
lofibox <family> <verb> --help shows command help where implemented
lofibox tui --help           shows TUI help, not global help
```

`lofibox help` is reserved for help. It must never be interpreted as a path,
URL, open request, or GUI/X11 launch request.

When `--json` is present and a CLI command fails before producing a domain
object, stderr must contain a structured error object:

```json
{
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "play --id requires a numeric track id.",
    "argument": "--id",
    "expected": "integer track id",
    "exit_code": 2,
    "usage": "lofibox play --id <track-id>"
  }
}
```

Structured error output must not be followed by a second generic error for the
same parse failure. For example, a specific invalid argument error must not also
print `Unknown runtime command.`

`--fields` is a contract, not a best-effort typo sink. Runtime query commands
and schema-published strict query commands must reject unknown fields with exit
code `2` and list allowed fields in text or structured JSON. Direct command
families that still share the legacy projection helper must at minimum emit a
machine-readable JSON warning with `_unknown_fields` and `_allowed_fields`
instead of silently returning `{}`. Full runtime snapshots must support nested
fields such as:

```text
--fields playback.status,playback.title,queue.index
```

For backward compatibility, bare playback fields on full-snapshot commands may
be interpreted as `playback.<field>`, but they must not cause unrelated domains
such as a large queue to be emitted.

State-setting commands must be idempotent when their vocabulary says they are
sets. In particular:

```text
lofibox shuffle on
lofibox shuffle off
lofibox play --shuffle on
lofibox play --shuffle off
```

must set shuffle to the requested state. They must not toggle.

Secret-bearing help must prefer stdin forms:

```text
--password-stdin
--token-stdin
```

Human help must not present `--password <secret>` or `--token <secret>` as the
recommended path, because argv secrets leak through shell history, process
inspection, logs, and agent transcripts.

## 5. Exit Code Contract

Stable exit codes:

```text
0  success
2  user input or usage error
3  resource not found
4  credential missing or authentication failed
5  network, remote, runtime transport, or provider connection failure
6  playback backend or live playback failure
7  persistence failure
```

Additional non-public errors may use `1` while a domain is still being migrated,
but published CLI behavior should converge to the stable codes above.

## 6. Command Families

The intended top-level command families are:

```text
lofibox version
lofibox doctor
lofibox now
lofibox runtime ...
lofibox play ...
lofibox queue ...
lofibox library ...
lofibox source ...
lofibox remote ...
lofibox metadata ...
lofibox lyrics ...
lofibox artwork ...
lofibox fingerprint ...
lofibox eq ...
lofibox settings ...
lofibox config ...
lofibox credentials ...
lofibox persistence ...
lofibox cache ...
lofibox search ...
lofibox open ...
lofibox desktop ...
lofibox tui ...
```

This list is a product vocabulary target. A command may appear in this
specification before every subcommand is implemented, but code must not pretend
that an unsupported subcommand succeeded.

## 7. Direct Command Families

Direct commands may run in a short-lived process because they operate on durable
configuration, governed cache, local library/index state, safe diagnostics, or
remote catalog queries.

Current direct implementation scope:

```text
lofibox version
lofibox doctor
lofibox source list
lofibox source show <profile-id>
lofibox source add <kind> --id <id> --name <name> --base-url <url>
lofibox source update <profile-id>
lofibox source remove <profile-id>
lofibox source probe <profile-id>
lofibox source auth-status <profile-id>
lofibox source capabilities <profile-id>
lofibox source local-root list
lofibox source local-root add <path> [--name <label>]
lofibox source local-root remove <id-or-path>
lofibox source local-root enable <id-or-path>
lofibox source local-root disable <id-or-path>
lofibox credentials list
lofibox credentials show-ref <profile-id-or-ref>
lofibox credentials status <profile-id-or-ref>
lofibox credentials set <profile-id-or-ref>
lofibox credentials delete <profile-id-or-ref>
lofibox credentials validate <profile-id-or-ref>
lofibox library scan [path...]
lofibox library root list
lofibox library root add <path> [--name <label>]
lofibox library root remove <id-or-path>
lofibox library root enable <id-or-path>
lofibox library root disable <id-or-path>
lofibox library status
lofibox library list tracks|albums|artists|genres|composers|compilations
lofibox library query tracks|albums ...
lofibox search <query>
lofibox remote browse <profile-id> [path]
lofibox remote recent <profile-id>
lofibox remote search <profile-id> <query>
lofibox remote resolve <profile-id> <item-id>
lofibox remote stream-info <profile-id> <item-id>
lofibox cache status
lofibox cache purge
lofibox cache gc
```

Direct commands that write profiles, credentials, or cache state must return
`7` on persistence failure.

`lofibox source local-root ...` is the durable command surface for local media
root profiles in `SourceProfilesDomain`.
`lofibox library root ...` commands are library-facing aliases over those same
source-profile local-root profiles; they must not write a separate library-root
store.
`lofibox library scan [path...]` remains a temporary diagnostic scan. Paths
provided to `library scan` or `--root` do not become durable roots.
Library commands with no temporary root must read the current enabled local
roots from `SourceProfilesDomain`.

Credential commands must accept stdin forms for secrets:

```text
--password-stdin
--token-stdin
```

## 8. Runtime Command Families

Runtime commands must target the running LoFiBox instance through the runtime
transport. They must not instantiate a second application runtime to mutate live
truth.

`lofibox tui` is not a one-shot runtime CLI command. It is a terminal-native
presentation target defined by `lofibox-terminal-ui-spec.md`. The CLI dispatcher
may route that subcommand to the TUI entry point, but TUI rendering, input
routing, and terminal lifecycle must remain outside `src/cli`.

Current runtime implementation scope:

```text
lofibox now
lofibox runtime status
lofibox runtime playback
lofibox runtime queue
lofibox runtime eq
lofibox runtime remote
lofibox runtime settings
lofibox runtime reload
lofibox runtime shutdown
lofibox runtime library-refresh
lofibox play
lofibox play --id <track-id>
lofibox play --pause
lofibox play --resume
lofibox play --toggle
lofibox play --next
lofibox play --prev
lofibox play --stop
lofibox play --seek <seconds>
lofibox play --shuffle on|off
lofibox play --repeat off|one|all
lofibox pause
lofibox resume
lofibox toggle
lofibox stop
lofibox seek <seconds>
lofibox next
lofibox prev
lofibox queue show
lofibox queue clear
lofibox queue next
lofibox queue set <index>
lofibox eq show
lofibox eq enable
lofibox eq disable
lofibox eq reset
lofibox eq band <index> <gain-db>
lofibox eq preset <name>
lofibox remote reconnect
```

Future queue mutation commands such as `queue add`, `queue remove`, `queue move`,
`queue save`, and `queue load` require matching runtime command payloads before
they may be advertised as implemented.

## 9. Deferred Families

These command families are part of the target CLI vocabulary, but they require
domain command services or runtime payloads before implementation:

- `config`
- `settings` durable product preference commands
- `persistence`
- `metadata`
- `lyrics`
- `artwork`
- `fingerprint`
- `open`
- `desktop`
- advanced `eq preset` CRUD/import/export and binding commands
- offline audio cache commands

Until those services exist, CLI code must fail unsupported subcommands with a
usage error instead of inventing local behavior.

## 10. AI Constraints

- Keep CLI code under `src/cli` limited to parsing, dispatch, projection, and
  exit-code mapping.
- Keep direct CLI commands behind application services.
- Keep runtime CLI commands behind the external runtime transport.
- Add a command family to help/man pages only when its implemented subset is
  real and tested.
- Update this document before changing whether a command is direct or runtime.
