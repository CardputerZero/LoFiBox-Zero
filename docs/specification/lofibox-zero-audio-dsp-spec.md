<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Zero Audio DSP Specification

## 1. Purpose

This document defines the audio DSP and equalizer capability domain for `LoFiBox Zero`.

It exists to stop future work from treating `EQ` as only a row of sliders on one page.
In this project, a mature EQ capability is not just visible band controls.
It is the audio-processing domain that shapes, protects, normalizes, and adapts playback after decode and before output.

Use this document when deciding how `EQ`, `preamp`, `bypass`, `loudness`, `ReplayGain`, `limiter`, and future audio processors belong in the product and where their responsibilities stop.

For final product meaning, use `lofibox-zero-final-product-spec.md`.
For architecture placement, use `project-architecture-spec.md`.
For decode, metadata, and output-pipeline behavior, use `lofibox-zero-media-pipeline-spec.md`.
For remote-source and server behavior, use `lofibox-zero-streaming-spec.md`.
For page-level UI constraints, use `lofibox-zero-page-spec.md`.

## 2. Scope

This document covers:

- equalizer capability beyond the current UI implementation page
- DSP-chain role and placement in the playback path
- preset systems and runtime state
- safety and consistency controls such as `preamp`, `ReplayGain`, and `limiter`
- advanced filtering and profile binding
- audio effects and creative sound-color processors (Remix: Radio, Tape, Vinyl)
- runtime update behavior, persistence, and system experience constraints

This document does not define:

- the exact DSP math or filter implementation
- a mandatory audio backend or library
- the final page map for advanced or professional EQ modes
- Linux audio-device administration UX

## 3. Distinction Contract

### 3.1 Current Confusions

- `EQ page` and `audio DSP domain` are not the same thing.
- `graphic EQ`, `loudness`, `ReplayGain`, and `limiter` are related, but they are not one control.
- `preset persistence`, `current runtime state`, and `per-device binding` are different responsibilities.
- `realtime adjustment` must not be confused with `restart the playback chain`.

### 3.2 Valid Distinctions

- `DspChain` is the ordered processing path between decode and output.
- `EqProfile` is a persisted configuration object, not the processing engine itself.
- `EqSessionState` is the runtime truth about what is currently active.
- `EqEngine` applies EQ-related processing to decoded PCM.
- `EqManager` owns active profile selection, runtime toggles, and binding decisions.
- `PresetRepository` stores built-in and user-authored DSP presets.
- `OutputDeviceBinding` maps a physical or logical output target to a DSP profile choice.
- `AudioEffectProfile` is the active-effect slot on `DspChainProfile` — a creative sound-color processor, not a corrective EQ tool.
- `AudioEffectDescriptor` is a registry entry describing one effect variant (e.g. Radio, Tape, Vinyl); the registry owns discovery, selection, and display-name resolution.
- Effects and EQ presets are separate domains: effects use `AudioEffectCycle` and the effect registry; EQ presets use `EqCyclePreset` and the `PresetRepository`. They must not share cycling mechanisms or UI controls.

### 3.3 Invalid Distinctions

- Do not hardwire EQ as a special branch inside the player UI.
- Do not rebuild or restart track playback merely to apply slider changes.
- Do not assume one global EQ profile fits every output device or content type.
- Do not treat protective processing such as `preamp` or `limiter` as optional decoration when the product allows aggressive gain boosts.
- Do not model creative audio effects (Radio, Tape, Vinyl) as EQ presets or store them in the `PresetRepository`. Effects have their own registry, selection model, and DSP implementation.
- Do not hardcode effect display names ("RADIO", "TAPE", "VINYL") into UI rendering; use `audioEffectName()` from the registry.

## 4. Normative Product Definition

`LoFiBox Zero` EQ is a first-class DSP capability, not a settings afterthought.

A mature EQ and DSP capability should cover five layers:

1. basic equalization
2. preset management
3. advanced audio controls
4. advanced EQ and filter capability
5. system-level runtime experience

This capability belongs inside the audio-processing path.
It must not be modeled as a separate player branch or a page-only effect.

## 5. Capability Layers

### 5.1 Basic Equalization

A mature graphic EQ capability should grow beyond the current six-band implementation page.

The recommended target is:

- `10-band` graphic EQ
- or `15-band` graphic EQ

Common band sets may include frequencies such as:

- `31 Hz`
- `62 Hz`
- `125 Hz`
- `250 Hz`
- `500 Hz`
- `1 kHz`
- `2 kHz`
- `4 kHz`
- `8 kHz`
- `16 kHz`

Each band should support:

- positive and negative `dB` gain adjustment
- reset to `0 dB`
- clear enabled or neutral state semantics

The mature domain should also include:

- `preamp`
- global `EQ` enable
- `bypass`
- `A/B` comparison between processed and unprocessed sound

The current UI implementation remains governed by `lofibox-zero-page-spec.md`.
This document defines the broader capability boundary beyond today's implemented page surface.

### 5.2 Preset System

An EQ capability is incomplete if only expert users can benefit from it.

The preset system should include built-in presets such as:

- `Flat`
- `Bass Boost`
- `Treble Boost`
- `Vocal`
- `Rock`
- `Pop`
- `Jazz`
- `Classical`
- `Electronic`
- `Podcast` or `Speech`

User preset behavior should support:

- create preset
- copy preset
- rename preset
- delete preset
- overwrite or save current settings

Preset switching during playback should support:

- immediate effect
- no playback interruption
- no fake `Apply` step that rebuilds the whole playback session

### 5.3 Advanced Audio Controls

A more complete DSP capability should include controls adjacent to EQ in the same processing chain, especially:

- left or right `balance`
- `loudness` with adjustable strength
- `ReplayGain` with track mode, album mode, and target loudness behavior
- `limiter` or peak protection

These are not identical to graphic EQ, but they belong to the same runtime DSP domain.

### 5.4 Advanced EQ And Filter Capability

For advanced users, the DSP domain may grow to include:

- `parametric EQ`
- configurable center frequency
- configurable gain
- configurable `Q` or bandwidth
- `high-pass` filters
- `low-pass` filters
- per-output-device DSP profiles
- per-content-type DSP bindings such as music, speech, radio, movie, or accompaniment

These are valid directions for `LoFiBox Zero`, but they do not automatically rewrite the current UI implementation contract.

### 5.5 System-Level Runtime Experience

The product should treat DSP as a realtime experience, not an offline settings editor.

The system-level experience should include:

- realtime audible updates while parameters change
- minimized click, pop, or abrupt jumps during preset or state changes
- frequency-response or curve visualization when advanced UI exists
- processed versus bypass comparison feedback
- peak-level or clipping feedback when protective UX is available
- persistence and auto-restore for active state, presets, and device bindings

## 6. User-Facing Modes

The product may present DSP in multiple layers of complexity.

### 6.1 Simple Mode

The approachable default may include:

- `EQ` enable
- preset selector
- `preamp`
- graphic EQ sliders
- reset action

### 6.2 Advanced Mode

A more detailed mode may include:

- frequency-response curve
- `loudness`
- `balance`
- `limiter`
- `ReplayGain`
- save-as-preset behavior

### 6.3 Professional Mode

A high-control mode may include:

- `parametric EQ`
- per-band `frequency`, `Q`, and `gain`
- `high-pass` and `low-pass`
- output-device binding
- configuration import and export

Before any of these expanded modes become real screens, the page and layout specs must be updated explicitly.

## 7. DSP Chain Model

### 7.1 Core Processing Path

The playback path should not stop at:

`Source -> Decode -> Output`

The mature processing path should be understood as:

`Source -> Decode -> DspChain -> Volume -> Output`

`TrackSource`, `Decoder`, and `AudioSink` belong to `lofibox-zero-media-pipeline-spec.md`.
This document defines what belongs inside `DspChain`.

### 7.2 `DspChain`

`DspChain` is the host for ordered processors such as:

- `EQ`
- `loudness`
- `ReplayGain`
- `limiter`
- future corrective or analytical filters

`EQ` is one important node in this chain, not the whole chain itself.

### 7.3 Runtime Flow

A correct runtime flow looks like this:

1. decoder outputs PCM
2. playback pipeline checks active DSP state
3. PCM passes through the active `DspChain`
4. processed PCM proceeds through volume and output
5. parameter changes update DSP state without restarting playback
6. later audio frames are processed with the updated state immediately

This is a hot-update model, not a replay model.

### 7.4 Runtime Safety And Smoothness

The DSP chain should account for:

- clipping risk when multiple EQ bands are boosted
- `preamp` or equivalent gain headroom control
- `limiter` or explicit warning paths
- parameter smoothing or interpolation
- minimization of pop or abrupt artifacts
- CPU cost on smaller Linux devices
- long-running playback stability

## 8. Data And Responsibility Model

### 8.1 `EqBand`

`EqBand` represents one controllable band and may include fields such as:

- `frequency`
- `gain_db`
- `q`
- `enabled`

### 8.2 `EqProfile`

`EqProfile` represents a complete stored DSP or EQ configuration and may include fields such as:

- `id`
- `name`
- `type`
- `preamp_db`
- `bands[]`
- `balance`
- `loudness_enabled`
- `limiter_enabled`
- `replaygain_mode`
- `is_default`

### 8.3 `OutputDeviceBinding`

`OutputDeviceBinding` represents a mapping such as:

- `output_device_id`
- `eq_profile_id`

### 8.4 `EqSessionState`

`EqSessionState` represents runtime truth and may include:

- `current_profile_id`
- `enabled`
- `bypass`
- `last_modified_time`

### 8.5 `EqEngine`

`EqEngine` is the processing executor.

Its job is to:

- initialize filters
- update filter parameters
- process PCM frames
- return processed PCM

### 8.6 `EqManager`

`EqManager` owns configuration and active-state coordination.

Its job is to:

- select the active profile
- bind profiles to outputs or contexts
- save and load state
- notify the playback path about hot updates

### 8.7 `PresetRepository`

`PresetRepository` owns persistence for:

- system presets
- user presets
- device-specific presets

The implemented DSP domain must expose these final-form controls through shared audio/DSP objects rather than through page-local state:

- built-in presets including `Flat`, `Bass Boost`, `Treble Boost`, `Vocal`, `Rock`, `Pop`, `Jazz`, `Classical`, `Electronic`, and `Podcast / Speech`
- user preset create, copy, rename, delete, overwrite, import, and export semantics
- output-device and content-type bindings
- 10-band graphic EQ plus parametric EQ bands
- high-pass and low-pass filter settings
- balance, loudness, limiter, ReplayGain mode, bypass, and preamp state
- smoothing helpers for non-abrupt parameter transitions

## 9. Relationship To Other Specifications

- `lofibox-zero-media-pipeline-spec.md` defines `TrackSource`, decode, metadata, `DspChain` placement, and output-sink boundaries.
- this document defines what belongs inside the DSP domain and how EQ-related runtime behavior should work.
- `lofibox-zero-streaming-spec.md` defines how remote sources enter the system; streamed content should use the same DSP entry path when its media behavior allows it.
- `lofibox-zero-page-spec.md` still defines the current implemented EQ page and does not automatically inherit the full mature DSP surface from this document.

## 10. AI Constraints For Future Work

- Do not collapse the DSP domain back into a page-local slider implementation.
- Do not model EQ changes as track restarts.
- Do not separate streamed and local DSP behavior unless a real media-semantic difference requires it.
- Do not let a single early six-band screen become the long-term architecture boundary for DSP.
- If later work changes the meaning of `DspChain`, `EqProfile`, `EqEngine`, `EqManager`, or `PresetRepository`, update this specification before changing code structure.

## 11. Audio Effects

### 11.1 Purpose

Audio effects are creative sound-color processors that live in the DSP chain as a distinct domain from corrective EQ, loudness, limiter, or ReplayGain. While EQ targets frequency correction and playback safety, effects target aesthetic transformation — simulating the sound of specific playback media, environments, or signal chains.

Effects are not presets of the graphic EQ. They use their own filter networks, modulation sources, noise generators, and saturation stages that cannot be expressed as a static set of band gains.

### 11.2 Effect Registry Model

The effect system uses a registry-driven model so effects can be discovered, cycled, and selected by identifier without hardcoding effect names into UI or playback logic.

**`AudioEffectDescriptor`** is the registry entry. Each descriptor carries:

- `plugin_id` — owning plugin (e.g. `io.github.vicliu624.lofibox.effect.remix`)
- `effect_id` — unique within the plugin (e.g. `remix.radio`)
- `name` — human-readable label (e.g. `Radio`)
- `description` — one-line summary of what the effect sounds like
- `default_intensity` — the effect author's intended intensity (0.0–1.25), applied automatically when the effect is selected
- `builtin` — whether this effect ships with the product

**`AudioEffectProfile`** is the active-effect slot on `DspChainProfile`:

- `plugin_id` — which plugin owns the active effect; empty means OFF
- `effect_id` — which effect within that plugin is active; empty means OFF
- `name` — cached display name derived from the registry
- `intensity` — runtime intensity, initialized from the descriptor's `default_intensity` on selection

**Built-in effects** are registered at startup via `builtinAudioEffects()`. External plugin effects may be registered later through the plugin runtime, with the same descriptor shape.

**Selection model**: `cycleAudioEffectId(plugin_id, current_effect_id, delta)` cycles through the ordered list of effects for a plugin, wrapping from the last effect back to OFF. OFF is always reachable as position zero. Direct selection by effect_id is also supported via the registry lookup `audioEffectById()`.

### 11.3 Built-in Remix Effects

The product ships one built-in effect plugin (`io.github.vicliu624.lofibox.effect.remix`) with three nodes: Radio, Tape, and Vinyl. Each simulates the sound of a different analog playback medium through a combination of EQ filtering, pitch modulation, noise generation, saturation, and wet/dry mixing.

#### 11.3.1 Radio

**Concept**: Narrow-band AM broadcast receiver, mid-20th-century portable radio.

**Frequency response (4-stage biquad cascade)**:

| Stage | Type | Frequency | Gain | Q |
|-------|------|-----------|------|---|
| High-pass | Butterworth | 285 Hz | — | — |
| Low-pass | Butterworth | 3.6 kHz | — | — |
| Tone A | Peaking | 1.15 kHz | +4.6 dB | 0.8 |
| Tone B | Peaking | 2.45 kHz | +2.0 dB | 1.1 |

The combined response cuts below 285 Hz and above 3.6 kHz, producing the narrow "telephone" bandwidth of AM radio. The two midrange peaks simulate the resonant character of a small radio speaker enclosure.

**Modulation**: `0.955 + sin(5.7 Hz) × 0.035 + random × 0.006`

A 5.7 Hz sinusoidal component (±3.5%) simulates ionospheric amplitude flutter on distant AM signals. A broadband random jitter (±0.6%) adds short-term instability. The baseline is pulled slightly below unity (0.955) to compress the signal slightly, as real radio AGC circuits do.

**Noise**: Gaussian white noise at ±0.0038 amplitude, the strongest of the three effects. Simulates the background static hiss of a radio receiver.

**Mono narrowing**: Before the filter chain, stereo channels are mixed to (0.34 × original + 0.66 × mono sum), reflecting that AM radio is a mono medium. Tape and Vinyl do not apply this step.

**Saturation**: `tanh` soft-clipping at drive level 1.75 (effective ~1.64 at default intensity 0.85), giving the strongest saturation of the three effects. Simulates the aggressive limiting and distortion of a small radio amplifier.

**Dry/wet mix**: 0.85 wet / 0.15 dry at default intensity, retaining a trace of the original for intelligibility.

**Output gain**: 0.90, compensating for the midrange boost to keep perceived loudness in check.

#### 11.3.2 Tape

**Concept**: Worn compact cassette played on a consumer deck, with softened highs, saturation warmth, and transport flutter.

**Frequency response**:

| Stage | Type | Frequency | Gain | Q |
|-------|------|-----------|------|---|
| High-pass | Butterworth | 38 Hz | — | — |
| Low-pass | Butterworth | 9.8 kHz | — | — |
| Tone A | Peaking | 180 Hz | +2.4 dB | 0.7 |
| Tone B | Peaking | 4.3 kHz | -2.0 dB | 0.9 |

The high-pass removes subsonic rumble from the tape transport. The low-pass at 9.8 kHz (rather than a sharper cut) produces the gradual high-frequency roll-off of tape, not the hard cut of radio. The 180 Hz bump is the classic "tape warmth" — low-end head-bump from the reproduce equalization curve. The 4.3 kHz dip simulates gap-loss in the playback head, softening transient detail.

**Modulation**: `1.0 + sin(0.36 Hz) × 0.011 + sin(6.4 Hz) × 0.004`

Two-component transport instability: A slow 0.36 Hz component (±1.1%) simulates wow from reel eccentricity as the tape spool rotates. A faster 6.4 Hz component (±0.4%) simulates flutter from the capstan/pinch-roller mechanism. Both are sinusoidal with no random component, modeling deterministic mechanical sources.

**Noise**: ±0.0014, the quietest of the three effects. Simulates the low tape hiss floor of a well-biased cassette.

**Saturation**: `tanh` at drive 1.42. Moderate saturation representative of tape compression at normal recording levels — warm but not distorted.

**Dry/wet mix**: 0.96 wet / 0.04 dry.

**Output gain**: 0.98.

#### 11.3.3 Vinyl

**Concept**: Turntable playback with subtle wow, surface noise, dust ticks, and occasional scratch impulses.

**Frequency response**:

| Stage | Type | Frequency | Gain | Q |
|-------|------|-----------|------|---|
| High-pass | Butterworth | 46 Hz | — | — |
| Low-pass | Butterworth | 12.8 kHz | — | — |
| Tone A | Peaking | 120 Hz | +1.4 dB | 0.8 |
| Tone B | Peaking | 5.2 kHz | +0.9 dB | 1.2 |

The 46 Hz high-pass filters subsonic turntable rumble while preserving musical bass. The 12.8 kHz low-pass retains most of the audible treble — vinyl can carry information well above 10 kHz, unlike tape. The 120 Hz gentle lift compensates for the perceptual bass loss after RIAA de-emphasis in playback systems. The 5.2 kHz subtle lift mimics the mild high-frequency resonance of a moving-magnet cartridge.

**Modulation**: `1.0 + sin(0.36 Hz) × 0.0025`

A single slow wow component at 0.36 Hz (±0.25%), driven only by turntable platter eccentricity. No flutter component — belt-drive and direct-drive turntables have far better speed stability than cassette transports. The modulation depth is an order of magnitude smaller than Tape's.

**Noise**: ±0.0022 continuous surface noise, plus two impulse noise sources:

- **Dust ticks**: triggered randomly (~1 per 3333 samples, ~14/sec at 48 kHz). Adds ±0.09 impulse with exponential decay (×0.88 per sample, half-life ~5 samples).
- **Scratches**: triggered rarely (~1 per 25000 samples, ~2/sec at 48 kHz). Adds ±0.20 impulse with slower decay (×0.985 per sample, half-life ~46 samples).

Both impulses accumulate independently and are combined as `crackle`, then scaled by intensity before being added to the signal. This two-layer model separates the frequent low-level clicks of dust from the rarer but louder pops of scratches.

**Saturation**: `tanh` at drive 1.22. Light saturation suggesting the gentle tube or solid-state coloration of a phono preamp — not distortion, just warmth.

**Dry/wet mix**: 0.92 wet / 0.08 dry.

**Output gain**: 0.98.

#### 11.3.4 Shared Infrastructure

All three effects share a common processing pipeline:

```
dry signal preserved
  → [Radio only: stereo→mono narrowing]
  → high-pass → low-pass → tone A → tone B
  → × modulation
  → + noise (+ crackle for Vinyl)
  → soft-saturate (tanh)
  → × output_gain
  → wet/dry crossfade
```

**Modulation sources**: Three independent phase accumulators (wow at 0.36 Hz, flutter at 6.4 Hz, radio at 5.7 Hz) advance per sample frame at 48 kHz and wrap at 2π. Each effect activates only the sources relevant to its medium.

**Noise generation**: A single xorshift32 PRNG (seed `0x4d595df4`) shared across all effects. Reseeded on effect switch via `resetRemixState()`. Generates uniform [-1,1] values mapped to the per-effect noise amplitude. Stereo channels receive noise scaled by ×0.94 (left) and ×1.06 (right) to create a slight stereo spread so the noise does not collapse to center-mono.

**Saturation**: `tanh(sample × drive) / tanh(drive)` — a gain-normalized soft-clipper. The normalization by `tanh(drive)` ensures unity gain for small signals regardless of drive level; only signals approaching the saturation threshold are compressed. Drive is clamped to [1.0, 4.0].

**Biquad state isolation**: Each stereo channel maintains independent biquad state (x1/x2/y1/y2) for all four filters. On effect switch, all remix biquad states are zeroed (`resetRemixState()`) to prevent filter ringing from one effect's EQ curve from bleeding into the next effect's first few samples.

**Intensity scaling**: `remix_wet = clamp(coefficient_wet × intensity, 0.0, 1.0)` and `remix_drive = 1.0 + (coefficient_drive - 1.0) × intensity`. Intensity does not affect the EQ curve, modulation depth, or noise amplitude. It is a macro control over wetness and saturation only. The default intensity per effect is set by the effect author in the descriptor and applied automatically on effect selection.

### 11.4 Effect Switching Behavior

**Hot-switch, no track restart**: Changing the active effect updates the `DspChainProfile` and is applied to the running `RealtimeDspEngine` on the next processed frame. No track restart, no gap, no fade. This is the same hot-update model as EQ slider changes.

**State reset on switch**: When the effect changes (different `plugin_id` or `effect_id`), remix-specific state is fully reset: all four biquad filter memories for all channels, all three modulation phase accumulators, the noise PRNG, and the vinyl dust/scratch accumulators. This prevents the new effect from inheriting the previous effect's filter state.

**OFF is always reachable**: Cycling past the last effect returns to OFF (empty `effect_id`). The user can always disable all effects with one more cycle press.

### 11.5 Runtime And UI Surface Contract

**Runtime state** (`EqRuntimeState`): `effect_plugin_id`, `effect_id`, `effect_intensity`. These are part of the EQ runtime state because effects live alongside EQ in the DSP chain. The state initializes to OFF on startup and is not persisted across restarts — the product always starts clean.

**Runtime command**: `AudioEffectCycle(plugin_id, delta)` cycles forward (or backward) through a plugin's effects. The command is dispatched through the runtime command bus and applies immediately.

**Runtime snapshot** (`EqRuntimeSnapshot`): carries `effect_plugin_id`, `effect_id`, `effect_name`, `effect_intensity`, and `effect_enabled` for serialization to WebSocket events, CLI responses, and the WebUI projection.

**UI surfaces**:

| Surface | Trigger | Behavior |
|---------|---------|----------|
| GUI (framebuffer) | `R` key | Cycles OFF → Radio → Tape → Vinyl → OFF |
| GUI Equalizer page | — | Displays current effect name ("REMIX: RADIO") |
| GUI Now Playing | — | Displays effect name top-right when active |
| TUI | `R` (uppercase) | Same cycle; `r` (lowercase) is reconnect |
| WebUI | REMIX button or `R` key | Sends `AudioEffectCycle` command; button label updates to show current effect |
| CLI | `lofibox remix` | Sends `AudioEffectCycle` via runtime socket |

**Search page exclusion**: On the Search page, the `R` key is treated as text input, not as the effect shortcut. The input router checks `page != AppPage::Search` before routing to `cycleAudioEffect()`.

### 11.6 Data Model

**`AudioEffectDescriptor`** (registry):
```
plugin_id, effect_id, name, description, default_intensity, builtin
```

**`AudioEffectProfile`** (on `DspChainProfile`):
```
plugin_id, effect_id, name, intensity
```

**`EqRuntimeState` effect fields**:
```
effect_plugin_id, effect_id, effect_intensity
```

**`EqRuntimeSnapshot` effect fields**:
```
effect_plugin_id, effect_id, effect_name, effect_intensity, effect_enabled
```

### 11.7 Relationship To Other DSP Domains

- Effects are **independent** of EQ: the effect processor runs whether EQ is enabled or bypassed. The user can apply Radio coloration to a flat signal or combine it with a custom EQ curve.
- Effects run **after** the EQ chain and gain stage, **before** the final output clamp. The processing order is: `EQ → gain/balance → effect → final clamp`.
- Effects do **not** replace or interact with the limiter, loudness, or ReplayGain systems. Those remain corrective/protective; effects are creative.
- The three Remix effects are **not** EQ presets. They cannot be expressed as static band gains and must not be stored, selected, or cycled through the `PresetRepository`.

### 11.8 AI Constraints

- Do not hardcode effect names ("Radio", "Tape", "Vinyl") into UI rendering, input routing, or playback logic. Use the registry (`audioEffectName()`, `audioEffectById()`) for all display and selection.
- Do not confuse effect selection with preset selection. Effects and presets are separate domains with separate cycling mechanisms (`AudioEffectCycle` vs `EqCyclePreset`).
- Do not add effect-specific processing branches to the main DSP path. New effects should be added through the registry and the `RemixProcessor` enum (or a future generalized effect processor interface), not through inline conditionals.
- If a future effect requires parameters beyond the current `RemixCoefficients` model (e.g. tempo-synced modulation, stereo width, feedback), extend the effect descriptor and processing model before adding the effect.

## 15. Current Implementation Convergence

As of 2026-04-27, the DSP baseline is a chain/profile domain rather than an EQ-page implementation detail:

- `DspChainProfile` carries graphic EQ, parametric EQ, high-pass/low-pass filters, loudness, balance, limiter, ReplayGain mode, preamp, bypass, smoothing, clipping-protection intent, and an `AudioEffectProfile` slot for creative sound-color effects.
- Preset governance includes built-in presets, user presets, import/export, duplication, rename, delete, and overwrite semantics.
- Device/content bindings map output devices and content categories to DSP profile ids without making the UI page own that decision.
- Playback stability policy owns gapless/crossfade preparation, transition lead time, and start/end jitter suppression as playback-chain behavior rather than visual behavior.
- The active compact EQ page state is converted into the playback `DspChainProfile`; slider and preset changes hot-update the running PCM DSP engine and must not restart the track.
- For the Debian host target, local and remote playback converge through a realtime PCM path: media is decoded to PCM, processed by the active `DspChain`, and then handed to the Linux output sink.
- Built-in audio effects (Radio, Tape, Vinyl) are registered via `AudioEffectDescriptor` entries in `audio_effect_registry.cpp`, selected through the `AudioEffectProfile` slot on `DspChainProfile`, and rendered by `RealtimeDspEngine` using dedicated biquad chains, modulation oscillators, a shared xorshift32 noise source, and `tanh` soft-saturation. Effect switching hot-updates the DSP engine without track restart, resets all per-effect filter and modulation state, and is exposed through GUI (`R` key), TUI (`R` key), WebUI (REMIX button), and CLI (`lofibox remix`).

Future rendering pages may expose simple, advanced, or professional controls, but they must not redefine the DSP domain.
