<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Page Spec: Now Playing

## 1. Purpose

- Provide the focused playback screen for the current track.

## 2. Related Specs

- Shared current UI rules: `docs/specification/lofibox-zero-page-spec.md`
- Layout template: `docs/specification/lofibox-zero-layout-spec.md`
- Visual styling: `docs/specification/lofibox-zero-visual-design-spec.md`
- Media metadata rules: `docs/specification/legacy-lofibox-product-guidance.md`

## 3. Entry Conditions

- `Now Playing` is entered either directly from `Main Menu` or after starting playback from a track list or playlist.

## 4. Data Dependencies

- current playback session
- current track title, artist, and album when available
- cover-art metadata when available
- elapsed and total duration when available
- current playback mode flags for shuffle and repeat
- current paused versus playing state

## 5. State Enumeration

- `STATE_NOW_PLAYING_ACTIVE`
  current track metadata and playback timing are available
- `STATE_NOW_PLAYING_EMPTY`
  no current track is active, but the page remains valid

## 6. Required Elements

- Track title
- Artist
- Album
- Cover art region
- Elapsed time
- Total duration
- Progress bar
- Previous control
- Play/Pause control
- Next control
- Playback mode control

## 7. Event Contract

- `EVT_PAGE_ENTER`
  - effect: enter `STATE_NOW_PLAYING_ACTIVE` when a track exists, otherwise `STATE_NOW_PLAYING_EMPTY`
- `EVT_NAV_LEFT`
  - effect: request previous track
- `EVT_NAV_RIGHT`
  - effect: request next track
- `EVT_CONFIRM`
  - effect: toggle play or pause
- `EVT_PLAY_MODE_CYCLED`
  - effect: cycle playback mode through order, shuffle, repeat-all, repeat-one
- `EVT_BACK`
  - effect: return to the previous page in the navigation stack
- `EVT_PLAYBACK_TRACK_CHANGED`
  - effect: refresh page state from current playback session

## 8. Transition Contract

- `STATE_NOW_PLAYING_ACTIVE + EVT_NAV_LEFT` -> effect: request previous track and refresh state -> `STATE_NOW_PLAYING_ACTIVE`
- `STATE_NOW_PLAYING_ACTIVE + EVT_NAV_RIGHT` -> effect: request next track and refresh state -> `STATE_NOW_PLAYING_ACTIVE`
- `STATE_NOW_PLAYING_ACTIVE + EVT_CONFIRM` -> effect: toggle play/pause -> `STATE_NOW_PLAYING_ACTIVE`
- `STATE_NOW_PLAYING_ACTIVE + EVT_PLAY_MODE_CYCLED` -> effect: update playback mode -> `STATE_NOW_PLAYING_ACTIVE`
- `STATE_NOW_PLAYING_ACTIVE + EVT_BACK` -> effect: return to previous page -> previous page
- `STATE_NOW_PLAYING_ACTIVE + EVT_PLAYBACK_TRACK_CHANGED [guard: no active track remains]` -> effect: clear playback metadata -> `STATE_NOW_PLAYING_EMPTY`
- `STATE_NOW_PLAYING_EMPTY + EVT_PLAYBACK_TRACK_CHANGED [guard: active track now exists]` -> effect: load current track metadata -> `STATE_NOW_PLAYING_ACTIVE`
- `STATE_NOW_PLAYING_EMPTY + EVT_BACK` -> effect: return to previous page -> previous page

## 8.1 Page-Local F1 Help

- `F1` `MUST` open the `Now Playing` page's own help modal.
- The current `Now Playing` page has no explicit shortcut rows; therefore the modal `MUST` use the empty implementation.
- The modal `MUST NOT` reuse Main Menu's playback shortcuts until this page explicitly specifies its own shortcut table.

## 9. Empty State

- `Now Playing` `MUST` remain a valid page even if no track is active.
- In no-track state, the page `MUST` show a stable fallback such as `No Track`.
- No-track state `MUST` clear or hide cover art gracefully.
- No-track state `MUST` avoid invented metadata.

## 10. Error State

- No dedicated error page belongs here in the current implementation.
- Missing cover art `MUST` degrade gracefully to an empty cover region.
- Missing track metadata `MUST` degrade gracefully without duplicated titles or invented values.
- The top bar `MUST NOT` duplicate the main title in both the top bar and main content header.

## 11. Non-Goals

- Lyrics
- Pointer-drag scrubbing
- Queue management UI on this page
- Technical debug stats such as codec or sample-rate readouts in the current playback UI
