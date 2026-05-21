<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Page Spec: Songs

## 1. Purpose

- Present a directly playable list of tracks for the current browse context.

## 2. Related Specs

- Shared current UI rules: `docs/specification/lofibox-zero-page-spec.md`
- Layout template: `docs/specification/lofibox-zero-layout-spec.md`
- Metadata rules: `docs/specification/legacy-lofibox-product-guidance.md`

## 3. Entry Conditions

- `Songs` is entered from `Main Menu` via `Music`, or from filtered library pages such as `Albums`, `Genres`, `Composers`, and `Compilations`.

## 4. Data Dependencies

- track list for the active browse context
- normalized track title
- current selected row
- active context label or selected object name
- entry origin

## 5. State Enumeration

- `STATE_SONGS_READY`
  active song list is non-empty and interactive
- `STATE_SONGS_EMPTY`
  current context contains no tracks

Supported contexts:

- `All Songs`
- `Artist`
- `Album`
- `Genre`
- `Composer`
- `Compilation Album`

## 6. Required Elements

- page title reflecting current context
- song rows using track title as primary text
- stable empty-state row when the current context contains no tracks
- top-left `F1:HELP` hint instead of a back arrow

## 7. Event Contract

- `EVT_PAGE_ENTER`
  - effect: enter `STATE_SONGS_READY` when tracks exist, otherwise `STATE_SONGS_EMPTY`
- `EVT_NAV_UP`
  - effect: move selection up when song list is non-empty and selection is not first row
- `EVT_NAV_DOWN`
  - effect: move selection down when song list is non-empty and selection is not last row
- `EVT_CONFIRM`
  - effect: start playback of the selected track when song list is non-empty
- `EVT_BACK`
  - effect: return to the page that established the current context
- `EVT_HELP`
  - effect: open a modal shortcut-help window
- `EVT_SORT`
  - effect: cycle the active song sort mode

## 8. Transition Contract

- `STATE_SONGS_READY + EVT_NAV_UP [guard: selection is not first row]` -> effect: move selection up -> `STATE_SONGS_READY`
- `STATE_SONGS_READY + EVT_NAV_DOWN [guard: selection is not last row]` -> effect: move selection down -> `STATE_SONGS_READY`
- `STATE_SONGS_READY + EVT_CONFIRM` -> effect: start playback of selected track -> `Now Playing`
- `STATE_SONGS_READY + EVT_HELP` -> effect: show shortcut modal -> `STATE_SONGS_READY`
- `STATE_SONGS_READY + EVT_SORT` -> effect: cycle title ascending, title descending, artist ascending, artist descending, added ascending, added descending -> `STATE_SONGS_READY`
- `STATE_SONGS_READY + EVT_BACK [guard: origin = Main Menu Music]` -> effect: return to root page -> `Main Menu`
- `STATE_SONGS_READY + EVT_BACK [guard: origin = Albums]` -> effect: return to albums -> `Albums`
- `STATE_SONGS_READY + EVT_BACK [guard: origin = Genres]` -> effect: return to genres -> `Genres`
- `STATE_SONGS_READY + EVT_BACK [guard: origin = Composers]` -> effect: return to composers -> `Composers`
- `STATE_SONGS_READY + EVT_BACK [guard: origin = Compilations]` -> effect: return to compilations -> `Compilations`
- `STATE_SONGS_EMPTY + EVT_BACK [guard: origin = Main Menu Music]` -> effect: return to root page -> `Main Menu`
- `STATE_SONGS_EMPTY + EVT_BACK [guard: origin = Albums]` -> effect: return to albums -> `Albums`
- `STATE_SONGS_EMPTY + EVT_BACK [guard: origin = Genres]` -> effect: return to genres -> `Genres`
- `STATE_SONGS_EMPTY + EVT_BACK [guard: origin = Composers]` -> effect: return to composers -> `Composers`
- `STATE_SONGS_EMPTY + EVT_BACK [guard: origin = Compilations]` -> effect: return to compilations -> `Compilations`

## 9. Empty State

- If the current context has no tracks, the page `MUST` show `No Songs` or equivalent.

## 9.1 Shortcut Help Modal

- `F1` `MUST` open a modal shortcut-help window on top of the list.
- The modal `MUST` include:
  - `OK`: play
  - `UP` / `DOWN`: move one row
  - `PGUP` / `PGDN`: move by one page
  - `BACKSPACE`: return to parent page
  - `T`: sort
  - `F2` through `F6`: global playback controls
  - `F9`: search
- The modal `MUST NOT` show the active sort mode as extra explanatory text; users should perceive sorting from the list order itself.
- `BACKSPACE` `MUST` close the modal before navigating away.
- `T` sort order `MUST` cycle through:
  - song title ascending
  - song title descending
  - artist name ascending
  - artist name descending
  - added time ascending
  - added time descending

## 10. Error State

- No dedicated error page belongs here in the current implementation.
- Missing metadata `MUST` degrade to normalized track-title fallback rather than broken or blank rows.
- The page `MUST NOT` invent queue-editing, long-press, or context-menu behavior to compensate for missing data.

## 11. Non-Goals

- In-page queue editor
- Context menu system
- Long-press system
