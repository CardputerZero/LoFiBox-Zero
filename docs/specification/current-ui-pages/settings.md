<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Current UI Page Spec: Settings

## 1. Purpose

- Provide a small, product-relevant set of device and player preferences.

## 2. Related Specs

- Shared current UI rules: `docs/specification/lofibox-zero-page-spec.md`
- Layout template: `docs/specification/lofibox-zero-layout-spec.md`

## 3. Entry Conditions

- `Settings` is entered from `Main Menu`.

## 4. Data Dependencies

- current setting rows and their truthful values
- device capability data needed to show those values honestly
- current selected row

## 5. State Enumeration

- `STATE_SETTINGS_READY`
  settings list is interactive with one selected row

The current `Settings` page `MUST` contain these base rows, in this order:

1. `Network`
2. `Metadata Service`
3. `Sleep Timer`
4. `Backlight`
5. `Language`
6. `Remote Setup`
7. `About`

When WebUI is enabled and network status is online, `Settings` `MUST` insert a read-only `WebUI` row between `Language` and `Remote Setup`. The row value is the user-facing WebUI address. Long addresses `MUST` remain in the normal bounded value column and scroll inside that range rather than widening the row layout.

## 6. Required Elements

- page title
- seven base settings rows, plus the conditional WebUI address row when WebUI is enabled and network status is online
- truthful current values when values are shown
- clear chevron or equivalent affordance on rows that navigate deeper

## 7. Event Contract

- `EVT_PAGE_ENTER`
  - effect: enter `STATE_SETTINGS_READY` and select the first row
- `EVT_NAV_UP`
  - effect: move selection up when selection is not first row
- `EVT_NAV_DOWN`
  - effect: move selection down when selection is not last row
- `EVT_CONFIRM`
  - effect: perform only the truthful current implementation behavior of the selected row
- `EVT_BACK`
  - effect: return to `Main Menu`

## 8. Transition Contract

- `STATE_SETTINGS_READY + EVT_NAV_UP [guard: selection is not first row]` -> effect: move selection up -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_NAV_DOWN [guard: selection is not last row]` -> effect: move selection down -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row = Network]` -> effect: no-op because connectivity is system-managed and read-only in the current implementation -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row = Metadata Service]` -> effect: no-op because service is read-only in the current implementation -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row = WebUI]` -> effect: no-op because the WebUI address is read-only in the current implementation -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row = Remote Setup]` -> effect: open the Remote Setup page that lists supported remote source kinds -> `Remote Setup`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row = About]` -> effect: open about page -> `About`
- `STATE_SETTINGS_READY + EVT_CONFIRM [guard: selected row is not About]` -> effect: perform row's truthful current behavior or no-op -> `STATE_SETTINGS_READY`
- `STATE_SETTINGS_READY + EVT_BACK` -> effect: return to main menu -> `Main Menu`

## 8.1 Page-Local F1 Help

- `F1` `MUST` open the `Settings` page's own help modal.
- The current `Settings` page has no explicit shortcut rows; therefore the modal `MUST` use the empty implementation.
- The modal `MUST NOT` reuse list, about, or main-menu shortcut content.

## 9. Empty State

- No dedicated empty state exists for `Settings`.

## 10. Error State

- No dedicated error page belongs here in the current implementation.
- Capability-dependent values `MUST` degrade gracefully rather than showing invented hardware or system facts.
- `Repeat` `MUST NOT` be added to this page merely as a convenience toggle.
- Network connectivity `MUST NOT` be treated as an app-managed Wi-Fi configuration workflow; it must project shared network state.
- Metadata-service truth `MUST NOT` be implemented as a fake editable setting; it must project shared metadata-service state, including whether online completion is currently usable.

## 11. Non-Goals

- Advanced system administration
- Developer/debug settings
- Account or cloud settings
- Top-level server login, credential, TLS, or permission rows before a remote source kind is selected
- App-managed Wi-Fi configuration
