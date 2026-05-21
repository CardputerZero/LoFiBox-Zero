<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Implementation Specification

## 1. Purpose

This document constrains the current UI implementation for `LoFiBox Zero`.

It exists because final product meaning must be kept separate from what is implemented right now.
This file is a current implementation contract, not the final product definition.

Use this document when implementing the current screens, navigation, UI state, and near-term UI behavior.

Use `lofibox-zero-final-product-spec.md` when deciding what the product ultimately is.
Use `lofibox-zero-app-state-spec.md` when playback truth, navigation truth, library context, queue truth, search truth, source truth, or shared settings truth are involved.
Use `legacy-lofibox-product-guidance.md` when deciding whether a feature belongs in the product at all.
Use `lofibox-zero-media-pipeline-spec.md` when decode, playback-pipeline, or output behavior is involved.
Use `lofibox-zero-audio-dsp-spec.md` when EQ or DSP behavior is involved.
Use `lofibox-zero-streaming-spec.md` when remote-source or server-backed behavior is involved.
Use `lofibox-zero-text-input-spec.md` when Search, remote-profile fields, playlist names, or any editable text behavior is involved.
Use `project-architecture-spec.md` when deciding where code belongs architecturally.
Use `lofibox-zero-layout-spec.md` for page geometry and template ownership.
Use `lofibox-zero-visual-design-spec.md` for styling, color, and typography.

## 2. Scope

This document covers:

- page purpose
- required UI elements
- page-level state
- entry and exit rules
- navigation outcomes
- empty-state behavior
- explicit non-goals

This document does not define:

- the product's final form
- final pixel-perfect visual polish
- internal code structure
- final device packaging or deployment
- the detailed page contracts for streaming, online, or cloud features

## 3. Specification Strength

The following words are normative in this document:

- `MUST`
  Required for the page to be considered aligned with the current implementation contract.
- `SHOULD`
  Strong default unless later product decisions change it.
- `MAY`
  Optional and non-blocking.
- `MUST NOT`
  Explicitly disallowed for the current implementation contract.

## 4. Global Page Contract

### 4.1 Shared Navigation Rules

- Every actionable page `MUST` be operable with keyboard-only input.
- `Back` `MUST` return to the previous page in the navigation stack.
- `Confirm` `MUST` activate the current selection or the page's primary action.
- Selection movement `MUST` be predictable and page-consistent.
- Non-main-menu pages `MUST` expose a clear back affordance through input behavior and page-local help, but the standard top bar left zone remains `F1:HELP` rather than a raw back glyph.

### 4.2 Selection And Focus Rules

- A newly entered list-like page `MUST` select the first actionable row by default unless a later specification explicitly overrides it.
- Implementations `MUST NOT` invent ad-hoc per-page remembered cursors unless that behavior is deliberately specified later.
- Non-actionable empty-state rows `MUST NOT` pretend to be interactive.

### 4.3 Truthful Status Rule

- Top-bar status indicators `MUST` represent real data when shown.
- UI `MUST NOT` show fake Wi-Fi, fake signal, fake Bluetooth, or made-up system state merely because a mockup had decorative icons.
- Battery status `SHOULD` be shown when the system can report it truthfully.

### 4.4 Density And Layout Rule

- All pages `MUST` remain readable on the shared `320x170` logical screen.
- A page `MUST NOT` inherit large-screen spacing assumptions from old `480x222` design exports.
- If a larger-screen legacy layout does not fit `320x170`, the product meaning `MUST` be preserved even when the exact composition changes.

### 4.5 Empty-State Rule

- Every data-driven page `MUST` define a stable empty state.
- Empty states `MUST` keep navigation working.
- Empty states `MUST NOT` crash, loop, or produce visually broken rows.

### 4.6 Small-Screen Text Rule

- Primary labels `MUST` be prioritized over secondary metadata when space is limited.
- Long text `SHOULD` be clipped, truncated, or selectively scrolled rather than overflowing unpredictably.
- Metadata `MUST` remain Unicode-safe and CJK-capable.

### 4.7 Page-Local F1 Help Rule

- Every page except `Boot` `MUST` accept `F1` as `EVT_HELP`.
- `F1:HELP` is a defensive programming contract, not a shared catch-all help page.
- The App shell may provide the common modal frame and close behavior, but the shortcut rows `MUST` be selected from the current page's own page specification.
- A page without explicitly defined shortcuts `MUST` still implement `EVT_HELP` and show an empty help modal such as `NO SHORTCUTS`.
- A page `MUST NOT` borrow another page's shortcut content merely because the page has no current shortcuts.
- When a help modal is open, `BACKSPACE`, `ENTER`, or `F1` `MUST` close the modal before normal page navigation or page actions run.

### 4.8 Device Keyboard Contract

LoFiBox Zero targets the Debian/Linux handheld keyboard layout as a product input surface, not as a generic desktop keyboard accident.

- `F1` is always page-local help except on `Boot`.
- `F2` resumes or starts playback; `F3` pauses playback; `F4` selects the previous track; `F5` selects the next track.
- `F6` cycles the user-facing playback mode in this order: order, shuffle, repeat-all, repeat-one, order. GUI/device shortcut help must describe this as a play-mode command rather than exposing separate shuffle/repeat booleans.
- `F7` and `F8` are not GUI/device playback-mode shortcuts. Repeat-all and repeat-one may still exist as explicit runtime or CLI commands, but the primary GUI/device input surface must present playback mode as one cycle.
- `F9` opens Search; `F10` opens Library; `F11` opens Up Next; `F12` opens Settings.
- `UP` and `DOWN` move one row in list-like pages. `PGUP` and `PGDN` move by one viewport in list-like pages.
- Framebuffer/evdev adapters must treat Linux `EV_KEY` codes as the input truth. `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, and `KEY_RIGHT` may become direction actions; ordinary letter events such as `KEY_X`, `KEY_F`, `KEY_S`, `KEY_Z`, and `KEY_C` must remain text/character events unless the kernel or driver reports a real direction key event.
- On the Main Menu, `LEFT` and `RIGHT` move one page preview; `PGUP` and `PGDN` jump by a larger page step; `HOME` resets to the first menu item.
- Outside the Main Menu, `HOME` returns to the Main Menu. `BACKSPACE` returns to the previous page unless the current page is a text editor that owns backspace for editing.
- Text-entry pages own committed UTF-8 text, `BACKSPACE`, and `OK` while editing according to `lofibox-zero-text-input-spec.md`. Global playback keys must not corrupt text buffers.
- Page-local shortcuts such as `L` for Lyrics, `Q` for Queue, `T` for sort, and `E`/`INS` for playlist editing are valid only on pages whose help modal advertises them.

## 5. Application-Level Navigation Map

The current implementation page graph is:

- `Boot` -> `Main Menu`
- `Main Menu` -> `Songs(All)` via `Music`
- `Main Menu` -> `Music Index` via `Library`
- `Main Menu` -> `Playlists`
- `Main Menu` -> `Now Playing`
- `Main Menu` -> `Equalizer`
- `Main Menu` -> `Settings`
- any non-`Boot` page -> `Search` via global `F9`
- `Settings` -> `Remote Setup`
- `Remote Setup` -> `Remote Profile Settings`
- `Remote Profile Settings` -> `Remote Field Editor`
- `Music Index` -> `Artists`
- `Music Index` -> `Albums(All)`
- `Music Index` -> `Songs(All)`
- `Music Index` -> `Genres`
- `Music Index` -> `Composers`
- `Music Index` -> `Compilations`
- `Artists` -> `Albums(Filtered By Artist)`
- `Albums` -> `Songs(Album Context)`
- `Genres` -> `Songs(Genre Context)`
- `Composers` -> `Songs(Composer Context)`
- `Compilations` -> `Songs(Compilation Context)`
- `Playlists` -> `Playlist Detail`
- `Playlist Detail` -> `Now Playing`
- `Settings` -> `About`

## 6. Per-Page Specification System

### 6.1 Standard Page File Template

Each page file under `docs/specification/current-ui-pages/` should follow this contract shape:

1. `Purpose`
2. `Related Specs`
3. `Entry Conditions`
4. `Data Dependencies`
5. `State Enumeration`
6. `Required Elements`
7. `Event Contract`
8. `Transition Contract`
9. `Empty State`
10. `Error State`
11. `Non-Goals`

If a section is intentionally minimal for a specific page, the page file should still say so explicitly rather than omitting the topic by silence.

### 6.2 State And Event Contract Conventions

Page files under `docs/specification/current-ui-pages/` should use these naming rules:

- page states use `STATE_<PAGE>_<MODE>`
- events use `EVT_<ACTION>`
- guards are written inline as `[guard: ...]`
- effects are written inline as `effect: ...`

Transition bullets should use this shape:

- `STATE_X + EVT_Y [guard: ...] -> effect: ... -> STATE_Z`
- `STATE_X + EVT_Y [guard: ...] -> effect: ... -> <Target Page>`

If an event changes selection or a local value but does not change the page mode, the transition should explicitly say that it stays in the same state.

### 6.3 Canonical Shared Events

Page files should prefer this shared event vocabulary whenever it fits:

- `EVT_PAGE_ENTER`
- `EVT_PAGE_EXIT`
- `EVT_NAV_UP`
- `EVT_NAV_DOWN`
- `EVT_NAV_LEFT`
- `EVT_NAV_RIGHT`
- `EVT_CONFIRM`
- `EVT_BACK`
- `EVT_HELP`

Pages may extend this vocabulary with domain-specific events when needed, for example:

- `EVT_SYSTEM_READY`
- `EVT_PROGRESS_UPDATED`
- `EVT_PLAYBACK_TRACK_CHANGED`
- `EVT_SHUFFLE_TOGGLED`
- `EVT_REPEAT_TOGGLED`

Custom events should remain action-oriented and should not encode backend implementation details into the event name.

### 6.4 Page Specification Index

The shared rules in Sections `4`, `5`, `7`, `8`, and `9` apply to every page spec below.

Each current implementation page has its own dedicated specification file:

- `Boot`: `docs/specification/current-ui-pages/boot.md`
- `Main Menu`: `docs/specification/current-ui-pages/main-menu.md`
- `Music Index`: `docs/specification/current-ui-pages/music-index.md`
- `Artists`: `docs/specification/current-ui-pages/artists.md`
- `Albums`: `docs/specification/current-ui-pages/albums.md`
- `Songs`: `docs/specification/current-ui-pages/songs.md`
- `Genres`: `docs/specification/current-ui-pages/genres.md`
- `Composers`: `docs/specification/current-ui-pages/composers.md`
- `Compilations`: `docs/specification/current-ui-pages/compilations.md`
- `Playlists`: `docs/specification/current-ui-pages/playlists.md`
- `Playlist Detail`: `docs/specification/current-ui-pages/playlist-detail.md`
- `Search`: `docs/specification/current-ui-pages/search.md`
- `Now Playing`: `docs/specification/current-ui-pages/now-playing.md`
- `Equalizer`: `docs/specification/current-ui-pages/equalizer.md`
- `Settings`: `docs/specification/current-ui-pages/settings.md`
- `Remote Setup`: `docs/specification/current-ui-pages/remote-setup.md`
- `Remote Profile Settings`: `docs/specification/current-ui-pages/remote-profile-settings.md`
- `Remote Field Editor`: `docs/specification/current-ui-pages/remote-field-editor.md`
- `About`: `docs/specification/current-ui-pages/about.md`

## 7. Shared App-State Index

Cross-page shared state is defined outside page files and must not be redefined page by page.

The current shared application-state contracts are:

- `NavigationStack`: `docs/specification/app-states/navigation-stack.md`
- `PlaybackSession`: `docs/specification/app-states/playback-session.md`
- `QueueState`: `docs/specification/app-states/queue-state.md`
- `SearchState`: `docs/specification/app-states/search-state.md`
- `SourceState`: `docs/specification/app-states/source-state.md`
- `LibraryContext`: `docs/specification/app-states/library-context.md`
- `SettingsState`: `docs/specification/app-states/settings-state.md`
- `NetworkState`: `docs/specification/app-states/network-state.md`
- `MetadataServiceState`: `docs/specification/app-states/metadata-service-state.md`
- `CredentialState`: `docs/specification/app-states/credential-state.md`
- `LibraryIndexState`: `docs/specification/app-states/library-index-state.md`
- `PersistenceState`: `docs/specification/app-states/persistence-state.md`

Page files may project these states and trigger their events, but page files must not become the primary owner of those truths.

## 8. Deferred Features That Must Not Leak Into Current UI Pages

The following are intentionally deferred from the current UI pages and `MUST NOT` be smuggled into page implementations without an explicit product decision and page-spec update:

- advanced search filters, search history, and search ranking controls beyond the current Search page contract
- custom playlist creation
- queue editor
- streaming or server-management UI that has not yet been page-specified
- online artwork lookup
- account sync
- advanced audio-device settings
- generic plugin/settings systems

## 9. Implementation Checklists

## 9.1 A Page Is Not Done Unless

- its purpose matches this document
- its visible elements match this document
- its actions and navigation outcomes match this document
- it defines truthful empty states
- it remains operable with keyboard-only input
- it does not introduce unapproved scope

## 9.2 A Page Is Wrong If

- it copies old widget structure without preserving the correct product meaning
- it promotes a design-only idea into the current UI without a product decision
- it invents fake system state
- it adds convenience actions that were never specified
- it blurs the distinction between library browsing, playback, settings, and device adapter behavior

## 20. Final Surface Baseline In Current Implementation

As of 2026-04-27, the current small-screen page model includes these product surfaces:

- Main Menu
- Library/List browsing
- Now Playing
- Lyrics
- EQ
- Settings
- Source Manager
- Remote Setup
- Search
- Queue / Up Next
- Remote Browse
- Server Diagnostics
- Stream Detail
- Playlist Editor

Pages that are not yet pixel-specialized must still have a page identity, title, list classification where applicable, and page-specific `F1:HELP` ownership. They may reuse the shared compact list renderer, but they must not be represented as anonymous rows inside another page.
