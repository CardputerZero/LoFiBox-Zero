<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Cardputer Zero Adaptation Specification

## 1. Purpose

This document defines the `Cardputer Zero` adaptation profile for `LoFiBox Zero`.

`Cardputer Zero` is not the product's whole identity.
It is, however, a current first-class target profile that the project must adapt to deliberately.

This distinction prevents two opposite failures:

- shrinking LoFiBox into a board-specific demo
- ignoring a real target's special screen and interaction constraints because LoFiBox is also a Linux desktop application

The base LoFiBox product is already a chromeless desktop-widget music terminal.
The Cardputer Zero profile adapts that product shell to a stricter small-screen and hardware-keyboard environment; it does not invent the no-menu-bar rule by itself.

## 2. Authority

For final product meaning, use `lofibox-zero-final-product-spec.md`.
For architecture boundaries, use `project-architecture-spec.md`.
For committed text, preedit, Unicode editing, and input-method boundaries, use `lofibox-zero-text-input-spec.md`.
For page, layout, and visual rules, use:

- `lofibox-zero-page-spec.md`
- `lofibox-zero-layout-spec.md`
- `lofibox-zero-visual-design-spec.md`

This document controls Cardputer Zero profile-specific presentation and adapter constraints.

## 3. Profile Definition

The `Cardputer Zero` profile is a Linux target profile with:

- small-screen presentation
- hardware-keyboard-first input
- the shared chromeless application surface
- no conventional window menu bar
- no desktop chrome treated as product UI
- page-level discoverability through profile-appropriate help, such as `F1:HELP`

The profile may use framebuffer, VNC, PocketFrame, or another Linux presentation adapter during development or validation.
Those adapters are not product truth.

## 4. Screen Contract

Where the active target uses the Cardputer/PocketFrame small-screen profile, the UI must treat `320x170` as the logical design surface unless a later profile spec explicitly changes it.

The product surface must:

- use all available logical screen area intentionally
- avoid conventional desktop menu bars
- avoid OS window chrome as part of the design
- keep top-bar content inside the product's own visual system
- keep page affordances readable at the small-screen scale

## 5. Input Contract

The profile is hardware-keyboard-first.

Linux input truth still comes from kernel input events such as `EV_KEY` and `KEY_*`.
Printed key legends, product images, PocketFrame button labels, or validation-harness naming must not redefine key semantics.

Profile input adapters may translate physical events into logical app commands, but that translation must stay inside the platform/device boundary.

For editable text, the framebuffer/evdev profile may emit directly translatable committed text.
It must not claim Debian desktop CJK IME support merely because the X11 desktop target supports system input methods.
If this profile later needs CJK composition, that must be specified as a device input method, input proxy, or alternate shell before implementation.

## 6. Product Model Non-Fork Rule

The `Cardputer Zero` profile must reuse the same product semantics as other Linux targets:

- `Track`
- `Album`
- `Artist`
- `Playlist`
- `Station`
- `Queue`
- `PlaybackSession`
- `EqProfile`
- source profiles
- metadata
- lyrics
- artwork
- DSP chain

The profile may change presentation.
It must not fork playback, library, streaming, metadata, credentials, persistence, or DSP behavior.

## 7. APPLaunch Integration Contract

When LoFiBox is launched from the Cardputer Zero `APPLaunch` environment, `APPLaunch` is treated as a runtime shell adapter.
It may discover Linux device paths and pass them to LoFiBox through process environment inheritance.
It must not redefine LoFiBox product semantics.

The APPLaunch desktop entry must start the LoFiBox APPLaunch wrapper, not the generic desktop binary and not the device binary directly:

- desktop entry path: `/usr/share/APPLaunch/applications/lofibox.desktop`
- `Exec`: `/usr/lib/lofibox/lofibox-applaunch`
- `Icon`: `share/images/lofibox.png`

The wrapper must then `exec` the best installed runtime for the active display session:

- wrapper path: `/usr/lib/lofibox/lofibox-applaunch`
- device target path: `/usr/lib/lofibox/lofibox_zero_device`
- native Wayland target: `/usr/bin/lofibox-wayland`
- X11 target: `/usr/bin/lofibox-x11`

In `auto` mode, the wrapper must prefer the native Wayland target when `WAYLAND_DISPLAY` is present, then the X11 target when `DISPLAY` is present, and only then fall back to the framebuffer device target. Explicit `LOFIBOX_DISPLAY_BACKEND` values must override that auto-detection.

Cardputer Zero APPLaunch starts LoFiBox as an appliance app. In that launch profile, the wrapper must enable the LoFiBox WebUI by default while preserving explicit user overrides:

1. Existing `LOFIBOX_WEBUI`
2. Existing `LOFIBOX_WEBUI_BIND`
3. Existing `LOFIBOX_WEBUI_PORT`
4. Default `LOFIBOX_WEBUI=1`
5. Default `LOFIBOX_WEBUI_BIND=0.0.0.0`
6. Default `LOFIBOX_WEBUI_PORT=8765`

This is a Cardputer APPLaunch adaptation rule, not a generic Linux desktop rule. The ordinary `lofibox` command must not silently start an HTTP service merely because it was launched from a normal desktop environment.

The wrapper must preserve explicit user overrides.
It must use these precedence rules for framebuffer selection:

1. Existing `LOFIBOX_FBDEV`
2. `APPLAUNCH_LINUX_FBDEV_DEVICE` inherited from APPLaunch
3. A local `/proc/fb` scan for an entry containing `fb_st7789v`
4. `/dev/fb1` as the last Cardputer Zero fallback

This rule exists because the ST7789V framebuffer number is an observed Linux device fact, not a stable product constant.
The implementation must not assume that the small screen is always `/dev/fb0` or always `/dev/fb1` when a stronger runtime signal is available.

The wrapper must use these precedence rules for keyboard input selection:

1. Existing `LOFIBOX_INPUT_DEV`
2. `APPLAUNCH_LINUX_KEYBOARD_DEVICE` inherited from APPLaunch
3. `/dev/input/by-path/platform-3f804000.i2c-event` as the Cardputer Zero fallback

`APPLAUNCH_LINUX_KEYBOARD_MAP` must not be treated as a LoFiBox input contract unless LoFiBox gains an explicit adapter for APPLaunch/LVGL keymap files.
The current LoFiBox device input path consumes Linux evdev key events and its own xkb/custom-key translation, not APPLaunch's LVGL keymap file.

Mouse or pointer environment variables such as `LV_LINUX_MOUSE_DEVICE` are not part of the current LoFiBox Cardputer Zero contract.
They must not be surfaced as supported LoFiBox behavior until a LoFiBox pointer input adapter exists.

## 8. Packaging And Install Contract

The Cardputer Zero APPLaunch integration must be installed only when the Linux framebuffer device target exists.
Installing an APPLaunch entry that points to a missing runtime is invalid.

When `lofibox_zero_device` is built, the install layout must include:

- `/usr/lib/lofibox/lofibox_zero_device`
- `/usr/lib/lofibox/lofibox-applaunch`
- `/usr/share/APPLaunch/applications/lofibox.desktop`
- `/usr/share/APPLaunch/share/images/lofibox.png`

The device target path `/usr/lib/lofibox/lofibox_zero_device` must remain stable regardless of whether the X11 desktop target is also built.
If a build without the X11 desktop target needs a generic command, it may additionally install `/usr/bin/lofibox`, but it must not replace the private device target path required by APPLaunch.

The APPLaunch icon must be a Cardputer-sized asset owned by LoFiBox.
It must not be a symlink to the standard Linux hicolor `180x180` desktop icon.
The standard Linux desktop icon and the APPLaunch icon are separate presentation assets:

- standard Linux desktop icon: hicolor icon theme
- APPLaunch icon: `/usr/share/APPLaunch/share/images/lofibox.png`

The standard Linux desktop entry must remain independent from the APPLaunch entry.
Changes to the APPLaunch integration must not change the normal Linux desktop launch path unless a separate Linux desktop specification requires it.

## 9. Validation Rule

Profile-specific changes should be validated against a real profile runtime or a faithful Linux validation harness.

The validation harness must remain an adapter.
If a behavior works only in the harness and not in the shared Linux product code path, the implementation is structurally suspect.

The Cardputer Zero APPLaunch integration must have a regression check that covers at least:

- the APPLaunch desktop `Exec` path
- the APPLaunch icon path and small-screen icon dimensions
- the native Wayland display declaration
- the Cardputer APPLaunch WebUI defaults
- the wrapper's framebuffer precedence
- the wrapper's keyboard-device precedence
- the wrapper's final fallback `exec` of `lofibox_zero_device`

Build or packaging verification should confirm that `/usr/lib/lofibox/lofibox_zero_device` and `/usr/lib/lofibox/lofibox-applaunch` are installed with executable permissions.

## 10. AI Constraints

- Do not remove Cardputer Zero constraints merely because LoFiBox is a Linux desktop player.
- Do not reclassify Cardputer Zero constraints as product-wide desktop constraints.
- Do not add window menu bars or generic desktop chrome to the Cardputer Zero product surface.
- Do not describe the no-menu-bar rule as only a Cardputer Zero workaround; it is part of the primary LoFiBox shell.
- Do not let framebuffer, VNC, PocketFrame, or container details leak into shared app semantics.
- Do not hard-code a framebuffer device path when APPLaunch or `/proc/fb` can provide a stronger runtime signal.
- Do not treat APPLaunch's LVGL keyboard map or mouse device variables as LoFiBox contracts before LoFiBox has adapters that consume them.
