# ADR-0015: Media and music inputs for presets, without a second patch

Status: Proposed (Phase 8.12, 2026-10-08). Builds on ADR-0012 (the
preset-variable patch) and ADR-0013 (`.milkdawp` v1); answers the open
question ADR-0012 left about "8.12's external textures".

## Context

Roadmap 8.12 (exploration doc §4.2, §4.5): presets that use what only MilkDAWp
has. Two halves:

1. **External textures**: `sampler_camera` and `sampler_video` in warp and
   composite shaders, showing the layer's media source (8.6).
2. **Music variables**: `mdw_beat_phase`, `mdw_bar_phase`, `mdw_bpm` and
   `mdw_onset`, readable in per-frame and per-vertex code.

The plan was a second projectM patch (`projectm_set_external_texture`) for the
first half. Since 8.6f, projectM 4.2's `projectm_set_texture_load_event_callback`
looked like a way round it, with two doubts: its header says projectM takes
ownership of a texture handed over and deletes it, and the callback fires when
a preset loads, not every frame. The second half was always meant to use
ADR-0012's setter.

## The spike

Read at our pinned commit (`TextureManager::TryLoadingTexture`) and proven
headless (`engine/tests/HeadlessRenderTests.cpp`, `[externaltex]`, Windows
WGL):

| Claim | Result |
|---|---|
| projectM owns and deletes a texture handed over by id | **No.** The code wraps it with `owned = false`; the header comment is stale. Still alive after purges and after the instance is destroyed. |
| A live feed needs the callback every frame | **No.** projectM keeps the texture under its name and samples it every frame, so new *contents* in the same texture show on the next frame with no reload and no new request. |
| projectM forgets it | Only when its cache purges it (one texture per preset load, the oldest-and-biggest of those two or more loads unused). The next preset naming it asks again; we hand over the same texture. Proven. (A side finding: with exactly one candidate, projectM's purge weighting is 0/0 and evicts nothing.) |
| Orientation | projectM reads a texture's first row as its **top** (stb_image's order, like its file textures), not GL's bottom. GL-order pictures show upside down unless flipped. |
| Names | The callback gets the name without `sampler_` and without a wrap/filter prefix (`sampler_fc_video` → `video`), in the preset's case. `texsize_video` works too. |

## Decision

1. **No second patch.** External textures go through the upstream callback.
   ADR-0012's patch stays the only one we carry.
2. **One texture per layer** (`Layer::presetMedia` in `RenderEngine.cpp`),
   handed over for both `camera` and `video` (any case), made the first time a
   preset of that layer names either. Frame-sized, and keeps its GL name across
   resizes (`GlFrameTarget`); `texsize_*` is fixed at the size it had when the
   preset loaded. Declared before the projectM instance, so it is destroyed
   after it.
3. **What it shows**: the layer's media source (camera, video or image,
   whatever it is: the two names are aliases), at full strength, cropped to
   fill and flipped, stamped every frame once it exists. **Media Mix and the
   media blend don't change it**: they are how the media goes over the
   picture, and a preset built on the camera shouldn't go black when the
   overlay is off. Black while there is no media (cleared once).
4. **Music variables**, set in every preset every frame with the other `mdw_*`
   values:
   - The director publishes a `core::BeatSnapshot` per run of hops to the
     layer's channel (`LayerChannel::setBeat`): tempo, confidence, the next
     beat's sample, beats per bar, the beat's place in the bar, and the latest
     broadband onset. From the host transport while it plays (its time
     signature gives the bar), otherwise from the beat detector (4/4, its
     downbeat guess). `BeatClockState` gained `beatsPerBar` and `beatInBar`
     for this.
   - The render thread turns it into values at the ring's newest sample (what
     the layer was just fed): `core::presetAudioInputs`. The phases run on at
     the snapshot's tempo past the predicted beat, across bars, so they move
     smoothly between the director's updates.
   - `mdw_beat_phase`, `mdw_bar_phase`: 0 on the beat (bar), rising to just
     below 1. `mdw_bpm`: 0 while there is no beat (no tempo, or confidence 0,
     e.g. the host stopped); the phases are 0 then too. `mdw_onset`: 1 at an
     onset, decaying with a 0.1 s time constant; it needs no tempo. A new
     broadband `OnsetDetector` in the director's pipeline feeds it (the bass
     one keeps steering the beat clock).
5. **Presets**: three hand-made `.milkdawp` files in
   `resources/presets/MilkDAWp Originals/` (Camera Tunnel, Bar Spinner, Onset
   Edges), AGPL-3.0-or-later. The build copies them into the bundled pack as a
   `MilkDAWp Originals` folder (target `milkdawp_original_presets`), after the
   pack is made; `package.sh` refuses a content folder without them.

## Consequences

- Any preset can use the camera or a video, and lock to the beat and the
  bar, on stock projectM 4.2 plus ADR-0012's patch. Elsewhere (renamed to
  `.milk`) such presets run with a black texture and zeros.
- A layer that ever loaded a media preset keeps stamping its texture every
  frame while it has media (one frame-sized draw), even after moving to
  presets that don't use it. Cheap; tracking which presets name it isn't
  worth it yet.
- If upstream ever makes callback textures owned (as its header claims), we
  would hand projectM a texture it then deletes. The `[externaltex]` spike
  test checks for exactly that (`glIsTexture` after purges and teardown), so
  a bump that changes it fails there. Draft PR #970 ("use but don't own")
  is moot for us at this pin.
- `texsize_camera`/`texsize_video` go stale when the frame size changes,
  until the next preset load. Sampling stays right; only texel-sized offsets
  are off.
- The engine's recent errors still log projectM at Error level only, and a
  composite shader that fails to compile is only a warning there (projectM
  falls back to its default). The hand-made presets' test listens at Warn.
- Found on the way: `mdw-view --set macroN=` never reached the engine; fixed.
  `mdw-view` still doesn't move Macros to a preset's defaults, so pass them.
- The own-renderer gate (8.D) loses one argument for going ahead: projectM
  didn't block this.
