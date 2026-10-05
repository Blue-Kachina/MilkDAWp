# ADR-0011: Visual controls on any renderer (Phase 8, Stage A)

Status: Proposed (D17, Stage A part). Amends the parameter surface of §4.8 and
`docs/parameters.md`.

## Context

Phase 8 (`development_roadmap.md`, design in `custom_format_exploration.md`
§4.4 and §6) lets the DAW shape how a visual looks while it plays, not only
which preset plays. Stage A has to work on stock projectM 4.2, whose API can't
read or set a preset's variables. So everything in this stage acts around
projectM: on the picture it renders (post effects), on the clock it is given
(`projectm_set_frame_time`), and on how a layer is mixed (the gate).

Things a design has to respect:

- VST3, AU and CLAP expect a fixed parameter list. Renaming parameters per
  preset is patchy across hosts.
- REAPER stores automation envelopes by parameter index, so a parameter
  inserted in the middle of the list would move every saved envelope after it.
- Every parameter is an automation lane in every host, so the count needs
  review (exploration doc §8, "Parameter count explosion").
- Layers (`layers_like_shrek.md`): an instance may show other instances'
  pictures as layers, or send its own picture to another instance.

## Decision

**Parameters.** 28 parameters, appended after the 26 of 1.0 (54 in all). A
test pins the order of the first 26.

- 16 **Visual** globals: Hue, Saturation, Brightness, Speed, Zoom, Rotation,
  Warp, Trails, Wave Size, Pixelate, Glow, Blur, Mirror, Kaleidoscope,
  RGB Split, Media Mix (`visual*` ids). Each default is the control's neutral
  value.
- **Macro 1-8** (`macro1`..`macro8`, 0..1) and **Lock Macros** (`lockMacros`,
  default Off). The host shows "Macro N"; each preset gives the Macros their
  meaning (Stage B).
- **Gate**, **Gate Threshold (dB)** (-100..0, default -80) and
  **Gate Release (ms)** (0..2000, default 80, skewed towards the short end)
  (`layerGate*`).

**Groups.** `ParameterSpec::group` names a host parameter group ("Visual",
"Macros", "Layer Gate"). A group is a contiguous run of `allParameters()`, and
the plugin adds each run as one `AudioProcessorParameterGroup`, so the
flattened order, and with it every index, is exactly the model's. The 1.0
parameters stay ungrouped. Putting them in groups would not move them, but
nothing needs it.

**Neutral costs nothing.** With every post effect at neutral, a layer goes out
exactly as before: no extra pass and no extra target. `EffectsState` frees its
targets while neutral. Controls glide over 50 ms (block-rate automation steps)
and snap to their target once within a hair of it, so returning a control to
neutral reaches it.

**Where the Visual globals act.** On an instance with its own Output (alone,
or showing other instances as layers), they act on its whole picture: the
canvas after mixing. On an instance sending its picture to another, they act on
its own layer before it is mixed. A lone instance's layer is its canvas, so both
readings agree.

**Stage A behaviour of each control.**

- Hue, Saturation, Brightness, Zoom, Rotation, Pixelate, Glow, Blur, Mirror,
  Kaleidoscope, RGB Split and Trails are post effects (`engine::EffectsChain`).
  The order is fixed: colour and geometry in one pass, then blur, glow and
  Trails. Sizes scale with frame height, so a lowered render quality looks the
  same, only softer.
- **Rotation** is a speed. When it returns to 0, the frame settles upright over
  about a second, so the chain can go neutral again.
- **Speed** sets the rate of the layer's preset clock: `dt x speed` is
  integrated into `projectm_set_frame_time`, never `time x speed`, so turning it
  never jumps. It can't slow what a preset does once per frame (decay,
  feedback) or the reaction to audio. While a soft cut blends, the clock runs at
  least real time, so Speed 0 can't freeze a blend half done. Every layer now
  runs on this clock instead of projectM's system clock, so a paused engine no
  longer fast-forwards presets on resume.
- **Warp**, **Wave Size** and the **Macros** do nothing until a `.milkdawp`
  preset gives them a meaning (Stage B). They are not faked as post effects,
  which would make them mean something else later. The UI dims them and says
  why.
- **Media Mix** (8.6a) is the opacity of the instance's media source (Settings >
  Media source; an image for now), drawn over the layer's picture before the
  effects, so they apply to it too. It fills the frame, centred and cropped, and
  keeps its own transparency. Following the rule above, on an instance that
  composites other layers it goes over the mixed canvas. The source path is
  saved with the plugin state (`mediaSourcePath`) and the app's preferences.
  8.6b adds cameras through JUCE's `CameraDevice` (Windows and macOS;
  `JUCE_USE_CAMERA` is on there only), saved as `camera:<device name>`. Each
  camera is opened once per process and shared by every instance showing it.
  It is opened and closed on the message thread, and frames are converted on the
  camera's thread (scaled to 1280 px at most), so the render thread only uploads.
  8.6c adds video files through `VideoDecoder` (Media Foundation on Windows;
  AVFoundation and GStreamer are 8.6d), decoded on a thread per source. A video
  loops. In the plugin its position is the host's sample position over the
  sample rate, also while stopped, so scrubbing moves it and a render is
  repeatable. A jump back, or more than a second ahead, seeks. The app runs it
  freely.
  8.6e: the media's blend mode (Normal, Add, Screen, Multiply, Luma key,
  Displace) is a **setting**, saved like the source (`mediaBlend`), not a 17th
  Visual global: the 16 are settled, and blend modes automate poorly. Displace
  copies the canvas first, then samples it offset by the media's red and green
  (up to a tenth of the frame, scaled by Media Mix). It is not offered in
  `layerBlend`: adding a choice to that parameter would remap the normalised
  values of host automation already recorded on it.
  8.6f (the spike) became a seventh media blend, **Burn in**:
  `projectm_opengl_burn_texture` draws the media into the preset's main texture
  before it renders, so the preset's warp and decay carry it on. It burns every
  frame, from a frame-sized copy whose alpha is Media Mix (cropped to fit like
  the other modes); burning only every few frames at low Media Mix strobed.
  Displace reads its map from a mip level about 90 px tall, so a camera's noise
  doesn't make the picture jitter.
- **Lock Macros** off: when the preset changes, the shell moves each Macro to
  the new preset's default (0 for a `.milk`) as a normal parameter gesture on
  the message thread. A preset arriving from a restored session is not a
  change.
- **Gate:** the layer's draw opacity is multiplied by the gate envelope. A
  gated layer is still fed and rendered. A lone instance fades its output from
  black while the gate is not fully open.

**UI.** A shared `ui::VisualSettingsPanel` ("Visual...", in the settings menu
of both shells) is built from the parameter groups. It has a gate meter showing
level, threshold line and open light.

## Consequences

- 54 parameters in hosts. Groups keep the list navigable where hosts show them.
  Any later addition is appended and goes through the same review.
- "Zoom" means the canvas zoom now and will offset the preset's own `zoom` once
  a `.milkdawp` plays (Stage B). The drawer must say which applies.
- Lock Macros off moves Macros at each preset change. In touch, latch or write
  automation modes the host may record those moves. That still needs a check in
  REAPER and one other host before release (exploration doc §6.3).
- Effect shaders are GLSL 3.30 core like the compositor's. Android (Phase 7)
  will need ES variants.
- Ruled out: renaming parameters per preset, inserting parameters into the 1.0
  list, a user-orderable effects chain (the order is fixed in v1; per-preset
  order belongs to `.milkdawp`), and rendering Warp or Wave Size as post effects.
