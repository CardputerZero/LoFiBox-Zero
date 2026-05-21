<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Page Spec: Playlist Detail

## 1. Purpose

- Show the playable contents of the currently selected playlist.

## 2. Related Specs

- Shared current UI rules: `docs/specification/lofibox-zero-page-spec.md`
- Layout template: `docs/specification/lofibox-zero-layout-spec.md`

## 3. Entry Conditions

- `Playlist Detail` is entered from `Playlists` by confirming a selected playlist row.

## 4. Data Dependencies

- active playlist identifier or name
- ordered track list for that playlist
- track title and artist metadata
- current selected row

## 5. State Enumeration

- `STATE_PLAYLIST_DETAIL_READY`
  playlist contains one or more tracks and is interactive
- `STATE_PLAYLIST_DETAIL_EMPTY`
  selected playlist contains no tracks

Supported playlist semantics:

- `On-The-Go` = current session on-the-go list
- `Recently Added` = descending `added_time`
- `Most Played` = descending `play_count`
- `Recently Played` = descending `last_played`

## 6. Required Elements

- page title showing active playlist name
- track rows with title as primary text and artist as secondary text
- stable empty-state row when the active playlist has no entries

## 7. Event Contract

- `EVT_PAGE_ENTER`
  - effect: enter `STATE_PLAYLIST_DETAIL_READY` when tracks exist, otherwise `STATE_PLAYLIST_DETAIL_EMPTY`
- `EVT_NAV_UP`
  - effect: move selection up when playlist is non-empty and selection is not first row
- `EVT_NAV_DOWN`
  - effect: move selection down when playlist is non-empty and selection is not last row
- `EVT_CONFIRM`
  - effect: start playback when playlist is non-empty
- `EVT_BACK`
  - effect: return to `Playlists`

## 8. Transition Contract

- `STATE_PLAYLIST_DETAIL_READY + EVT_NAV_UP [guard: selection is not first row]` -> effect: move selection up -> `STATE_PLAYLIST_DETAIL_READY`
- `STATE_PLAYLIST_DETAIL_READY + EVT_NAV_DOWN [guard: selection is not last row]` -> effect: move selection down -> `STATE_PLAYLIST_DETAIL_READY`
- `STATE_PLAYLIST_DETAIL_READY + EVT_CONFIRM` -> effect: start playback of selected track -> `Now Playing`
- `STATE_PLAYLIST_DETAIL_READY + EVT_BACK` -> effect: return to playlists -> `Playlists`
- `STATE_PLAYLIST_DETAIL_EMPTY + EVT_BACK` -> effect: return to playlists -> `Playlists`

## 8.1 Page-Local F1 Help

- `F1` `MUST` open the `Playlist Detail` page's own shortcut-help modal.
- The current shortcut rows are:
  - `OK`: play
  - `UP` / `DOWN`: move one row
  - `PGUP` / `PGDN`: move by one page
  - `BACKSPACE`: back
  - `T`: sort
  - `E` / `INS`: edit playlist
  - `F2` through `F6`: global playback controls
  - `F9`: search
- The modal `MUST NOT` reuse Main Menu shortcut content.

## 9. Empty State

- Empty playlists `MUST` show a stable `Empty` or equivalent non-interactive state.

## 10. Error State

- No dedicated error page belongs here in the current implementation.
- Invalid playlist inputs `MUST` degrade to the same stable empty-state behavior rather than corrupted rows.

## 11. Non-Goals

- In-page reorder
- Remove-from-playlist editor
- Multi-select bulk actions
