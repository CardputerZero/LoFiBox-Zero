<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Page Spec: Main Menu

## 1. Purpose

- Act as the primary front door of the product.
- Surface the six first-class destinations of the current implemented product shell.

## 2. Related Specs

- Shared current UI rules: `docs/specification/lofibox-zero-page-spec.md`
- Layout template: `docs/specification/lofibox-zero-layout-spec.md`
- Visual styling: `docs/specification/lofibox-zero-visual-design-spec.md`

## 3. Entry Conditions

- `Main Menu` is entered after `Boot`.
- It may also be re-entered when navigation unwinds back to the root UI surface.

## 4. Data Dependencies

- fixed top-level destination list
- current selected destination index
- current playback summary text for the topbar
- destination-specific iconography for each visible card

## 5. State Enumeration

- `STATE_MAIN_MENU_READY`
  main menu is interactive with one active destination selection

The current main menu `MUST` contain exactly these six items, in this order:

1. `Music`
2. `Library`
3. `Playlists`
4. `Now Playing`
5. `Equalizer`
6. `Settings`

On first arrival after `Boot`, the selected destination `MUST` default to `Library`.

## 6. Required Elements

- visually distinct root-page presentation
- one mandatory center selected card
- visible left and right preview cards resolved by circular carousel order
- destination labels under each visible card
- pagination dots
- a topbar with `F1:HELP` on the left
- current playback basic information on the right side of the topbar
- visible selection state
- destination-specific iconography inside every visible card

Semantic meaning `MUST` remain:

- `Music` = direct access to the full song list
- `Library` = category-based browsing
- `Playlists` = smart-playlist entry
- `Now Playing` = playback focus page even when no active track exists
- `Equalizer` = dedicated EQ page
- `Settings` = settings list

Additional visual lock:

- Visible cards `MUST NOT` be rendered as blank or unadorned color blocks.
- Each visible card `MUST` contain destination-specific iconography rendered either from imported legacy icon art or an equivalent runtime-drawn implementation.
- The selected card `MUST` be visually dominant through size, contrast, and emphasis.
- The side previews `MUST` remain clearly subordinate but still recognizable.
- The side previews `MUST` always be populated; the first and last menu items `MUST` wrap to each other instead of producing an empty edge.
- The topbar `MUST` show `F1:HELP` at the left edge.
- The topbar `MUST NOT` show signal bars or battery indicators.
- The right-side playback information region `MUST` have a fixed width.
- When a track or stream is active, the right-side playback information region `MUST` show current playback information: playback state plus track title and, when available, artist.
- If the playback information fits the fixed width, it `MUST` be right-aligned inside the region.
- If playback information exceeds the fixed width, it `MUST` scroll from right to left instead of being hard-truncated.
- The playback information region `MUST` use soft horizontal edge fades: content disappears toward the left edge and appears from the right edge rather than clipping abruptly.
- If no track is active, the playback information region `MAY` show a muted empty playback summary such as `NO TRACK`.
- Pagination dots `MUST` be visible and indicate current menu position.
- The main menu `MUST NOT` communicate movement through disabled end-state arrows; it is an infinite carousel with no visual "start" or "end" condition.
- Main Menu `MUST NOT` wrap its carousel content in an extra inset stage frame or boxed content panel.
- The page should read as a full-screen menu surface with a top strip, floating cards, and bottom pagination dots.

## 7. Event Contract

- `EVT_PAGE_ENTER`
  - effect: enter `STATE_MAIN_MENU_READY` and ensure one destination is selected, defaulting to `Library` on first root arrival
- `EVT_NAV_LEFT`
  - effect: move selection to previous destination; from `Music`, wrap to `Settings`
- `EVT_NAV_RIGHT`
  - effect: move selection to next destination; from `Settings`, wrap to `Music`

- `EVT_CONFIRM`
  - effect: enter the selected destination
- `EVT_BACK`
  - effect: remain at root; no deeper in-app navigation is triggered
- `EVT_HELP`
  - effect: open or close the Main Menu shortcut-help modal
- `EVT_MENU_PLAY`
  - effect: resume current paused track, keep current playing track, or start the first song when no current track exists
- `EVT_MENU_PAUSE`
  - effect: pause current playback without leaving Main Menu
- `EVT_MENU_PREVIOUS`
  - effect: jump to previous queued track without leaving Main Menu
- `EVT_MENU_NEXT`
  - effect: jump to next queued track without leaving Main Menu
- `EVT_MENU_PLAY_MODE`
  - effect: cycle playback mode without leaving Main Menu

## 8. Transition Contract

- `STATE_MAIN_MENU_READY + EVT_NAV_LEFT` -> effect: move selection left with circular wrap -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_NAV_RIGHT` -> effect: move selection right with circular wrap -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Music]` -> effect: open all songs -> `Songs`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Library]` -> effect: open library hub -> `Music Index`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Playlists]` -> effect: open playlists -> `Playlists`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Now Playing]` -> effect: open playback focus page -> `Now Playing`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Equalizer]` -> effect: open equalizer -> `Equalizer`
- `STATE_MAIN_MENU_READY + EVT_CONFIRM [guard: selected item = Settings]` -> effect: open settings -> `Settings`
- `STATE_MAIN_MENU_READY + EVT_BACK` -> effect: stay at root page -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_HELP` -> effect: toggle page-local shortcut modal -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_MENU_PLAY` -> effect: play or resume music in place -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_MENU_PAUSE` -> effect: pause music in place -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_MENU_PREVIOUS` -> effect: previous track in place -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_MENU_NEXT` -> effect: next track in place -> `STATE_MAIN_MENU_READY`
- `STATE_MAIN_MENU_READY + EVT_MENU_PLAY_MODE` -> effect: cycle playback mode in place -> `STATE_MAIN_MENU_READY`

## 8.1 Shortcut Help Modal

- `F1` `MUST` open a Main Menu-specific shortcut-help modal.
- The Main Menu modal `MUST NOT` reuse the Songs/List shortcut text.
- The modal `MUST` include:
  - `F2`: play song
  - `F3`: pause
  - `F4`: previous track
  - `F5`: next track
  - `F6`: play mode
  - `F9`: search
  - `F10`: library
  - `F11`: queue
  - `F12`: settings
- These shortcuts exist so the user can control playback without entering `Now Playing`.
- `BACKSPACE`, `ENTER`, or `F1` `MUST` close the modal before any other Main Menu navigation behavior.

## 9. Empty State

- No dedicated empty state exists for `Main Menu`.
- Missing library content `MUST NOT` remove root destinations; downstream pages own their own empty states.

## 10. Error State

- No dedicated error page belongs inside `Main Menu` in the current implementation.
- The page `MUST NOT` fake unavailable destinations as working deeper surfaces.
- The page `MUST NOT` degrade into blank placeholder cards, missing pagination dots, or visible end-state gaps and still be considered valid.

## 11. Non-Goals

- Additional top-level pages beyond the six current implementation entries
- Turning the main menu into a generic settings dashboard
