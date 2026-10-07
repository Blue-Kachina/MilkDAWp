# MilkDAWp 2 — Our own renderer and the `.milkdawp` format (post-1.0 brainstorm)

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done, `[-]` dropped.

Living doc: vision, feasibility findings, format sketch, architecture, roadmap and open questions for
Phase 8 (development_roadmap.md, Phase 8; §3 tier table; D17). Like `layers_like_shrek.md`, this is both
the brainstorm and the guide: add dated notes as decisions are made, `Note (YYYY-MM-DD): ...`, and keep
the roadmap's Phase 8 checklist in step with §7 here.

---

## 1. Pitch

projectM gives us MilkDrop, and MilkDrop presets are a closed box. A preset's per-frame code decides its
zoom, rotation, colours and wave sizes every frame, and nothing outside the preset can touch them. The DAW
can automate *which* preset plays and *when* it changes, but not *how it looks* while it plays.

The goal: every visual becomes something you can play. Draw an automation lane and the tunnel turns faster
through the chorus, the colours shift to red on the drop, the picture pixelates on the breakdown and glows
on the build. The same controls can come from a MIDI knob, an OSC rig, or a phone in the audience's hands.
A camera or a video file can be painted into the feedback loop so the visuals grow out of a live picture.

The `.milkdawp` format keeps the spirit of `.milk`: plain text, equations, readable by a human, and every
value the original preset set is still its default. What it adds is a declared set of **controls** (what
the host can automate and how each one acts on the preset), an **effects chain**, and **inputs** (camera,
video, images).

## 2. Feasibility in one page

| Piece | Feasibility | Cost | Needs our own renderer? |
|---|---|---|---|
| Effects chain over the visual (hue, saturation, tint, pixelate, glow, blur, mirror/kaleidoscope, RGB split, extra feedback) | High | M | No. A GL pass chain after projectM, beside `LayerCompositor`. |
| Speed control | High (partial) | S | No. We already drive `projectm_set_frame_time` (D15); integrate `dt × speed` into it. Per-frame decay and audio response still run per frame, so "speed" scales time-driven motion only. |
| OSC in/out | High | S–M | No. JUCE ships `juce_osc`. |
| Phone / web remote (roadmap Phase 9) | High | M–L | No. Needs a small embedded HTTP + WebSocket server (JUCE has none). |
| Camera, video, image as compositor layers | High | M | No. They are just more inputs to `LayerCompositor` (L2). |
| Camera painted *into* projectM's feedback loop | Medium (spike) | S–M | No, if `projectm_opengl_burn_texture` behaves well enough (§8 Q4 in the Layers doc: it is a full-strength one-shot draw). |
| True per-preset overrides (zoom, rot, warp, wave colour, q-vars) on **every** existing preset, full fidelity | High, *with a small projectM patch* | M | **No.** See §4.2: one setter for named preset variables, carried in our overlay port (no upstream PR, decided 2026-10-05). |
| `.milkdawp` v1 files (a `.milk` plus controls, effects, inputs) | High | M | No. v1 renders on patched projectM. |
| Camera/video as a texture *inside* preset shaders (`sampler_camera`) | Medium | M | No with a second projectM patch (register an external GL texture by name); yes otherwise. |
| New preset variables (`beat_phase`, `bar_phase`, `bpm`, `onset`, macros) | High | S | No. Same setter as the overrides. |
| Our own MilkDrop-compatible renderer, close to projectM on Cream of the Crop (not pixel-perfect, §5.1) | Medium | **XL** | It *is* the renderer. Biggest risks: HLSL shader translation, MilkDrop's undocumented quirks, golden-image testing of presets that call `rand()`. |
| Particles, 3D, compute effects (fosfora-style) | Medium | L | Yes, or a separate non-MilkDrop layer type. GL 4.3 compute is not on macOS (GL 4.1 max), so particles would be transform feedback or fragment-shader state. |

**Headline finding:** projectM 4.2's public API has **no way to read or set a preset's variables** (checked
`parameters.h`, `core.h`, `user_sprites.h` on upstream master; only *sprite* variables have get/set).
But the variable store lives in projectm-eval, which projectM already links (MIT, standalone, register a
variable and write through its pointer). So a few dozen lines in projectM give us the core of what the user
asked for without rewriting MilkDrop. That changes the order of work:

1. **Stage A: controls that work on any renderer** (effects chain, speed, OSC, media layers; the phone remote follows in Phase 9). Ships
   value on the current engine and is reused unchanged later.
2. **Stage B: `.milkdawp` v1 on patched projectM.** The format, the override layer, the converter and the
   curated pack. Every existing preset renders exactly as before when controls are at their defaults.
3. **Stage C: our own renderer**, only when Stage B has shown what projectM can't do (camera in shaders if
   the texture patch is refused, our own transitions, particles, removing the LGPL/GL-version coupling,
   Android/mobile performance). Decision gate 8.D, not a commitment.

Doing C first would put the most expensive and riskiest item in front of every user-visible benefit.

## 3. What fosfora offers (inspiration)

[kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) is a Rust + wgpu (Vulkan/Metal) VJ engine,
MIT/Apache-2.0. Different language and graphics API, so we borrow ideas, not code. Read on 2026-10-05
(README and `docs/TECHNICAL.md`).

| fosfora idea | What it does | Take for MilkDAWp |
|---|---|---|
| **Declared inputs** in the preset (`.pfx` `"inputs"`: type Float/Color/Bool/Point2D, name, default, min, max; up to 16 floats, read in WGSL as `param(i)`) | The preset says which knobs it has | Our `[controls]` section (§5). A fixed maximum per preset, which also suits DAW parameter lists (§6.1). |
| **`"rates"`**: a param marked as a speed is integrated; its running sum goes to a spare uniform | Turning a speed knob never makes the picture jump | Essential. A "speed" control must change d(phase)/dt, never multiply `time`. Applies to Speed, rotation speed, any LFO rate. |
| Authoring rule "never `time × audio` for position" | Avoids jitter | Same rule for override modes: audio and automation scale size/colour, integrate into position. |
| **Binding matrix**: any source (MIDI, OSC, audio feature, hand tracking) to any target | One patch bay for all control | Later: a modulation matrix (audio features, LFOs → controls). In a DAW, host automation stays the primary source. |
| Input drain order MIDI → OSC → Web, last write wins per frame | Simple, predictable conflicts | Adopt. Host automation sits above all of them in the plugin (it owns the parameter). |
| **Web remote**: HTTP and WebSocket on one port, JSON messages, full state snapshot on connect, audio features broadcast at 10 Hz, QR-code pairing, access keys, several users at once, no app install | Phone control | Our Phase 9 almost exactly. QR + access key is the right security floor on a LAN. |
| **OSC** in on 9000, out on 9001, `/fosfora/param/<name> f <value>` | Lighting desks, TouchDesigner | `/milkdawp/<instance>/<paramId> f`. Signal broadcast (beats, bars, sections, drop prediction) out over OSC is cheap for us: the `BeatClock` and Energy mode already know these. |
| **Media**: webcam, still images, GIF, video files (H.264/HEVC/AV1 via FFmpeg), depth-map silhouettes; images and 3D models as particle emitters | Live picture in the visuals | Camera, video and image layers (8.6), then camera as a preset texture (8.12). Emitters wait for particles (Stage C). |
| **8-layer stack, 13 blend modes, 3 displacement modes** (foreground luminance offsets the sampling UV of what is below) | Rich compositing | We have 8 layers and 5 blends (Layers L2). Add displacement: cheap, striking with a camera layer. |
| **Post chain**: HDR `Rgba16Float` ping-pong targets, quarter-resolution bloom, separable blur, ACES tonemap | Glow without blowing out | Our effects chain (8.2). HDR targets matter once glow and extra feedback stack. |
| Feedback via `"feedback": true` per pass, `feedback(uv)` helper | Multi-pass effects | Our renderer's pass graph (Stage C) and the "extra feedback" effect. |
| **83 audio features** (key, percussive/harmonic split, build/drop prediction, MFCC, chroma) with asymmetric EMA smoothing | Richer reactions | Expose what we already compute (onset, beat phase, bar phase, tempo, energy) to presets and to the modulation matrix. More features is a separate core-analysis project. |
| **Scenes / cue lists** advancing on timer, beat or key | Show control | Already in our post-1.0 backlog (scenes, setlists). Controls make snapshots meaningful: a scene is a set of control values. |
| Spout / Syphon / NDI / v4l2loopback, hardware-encoded recording | Output to OBS/Resolume | Already in our backlog. |
| Built-in WGSL editor with live compile | Authoring | Much later: a `.milkdawp` control editor first (map knobs to variables without editing code). |

What we have that fosfora doesn't: a DAW plugin with sample-accurate host automation, ~10k MilkDrop presets,
and musical transition scheduling. The plan keeps those and borrows fosfora's control surface.

## 4. Architecture

### 4.1 Where things sit

```
 host automation · MIDI · OSC · web remote · modulation matrix
                        │ (all end as ParameterModel values)
                        ▼
 ┌─ milkdawp_core ──────────────────────────────────────────────┐
 │ ParameterModel (+ Visual globals, Macros) · ControlMap        │
 │ MilkdawpPreset (parse/validate/serialize, no GL)              │
 └───────────────────────────┬───────────────────────────────────┘
 ┌─ milkdawp_engine ─────────┴───────────────────────────────────┐
 │ Visualizer backend:  ProjectMBackend (patched)  │ OwnBackend  │ ← Stage C
 │   └ override layer: control values → preset variables          │
 │ MediaSources (camera, video, image → GL textures)              │
 │ EffectsChain (post passes per layer and on the canvas)         │
 │ LayerCompositor (+ displacement modes)                         │
 └────────────────────────────────────────────────────────────────┘
 RemoteServer (HTTP + WebSocket, OSC) in the shells, per process
```

Rules that carry over from the roadmap: `core/` stays JUCE- and GL-free (the format parser and control
mapping live there and are unit-tested in the devcontainer); only `engine/` touches GL or projectM; both
shells get every feature.

### 4.2 The projectM patch (Stage B's foundation)

Two functions, carried as a patch in `vcpkg-overlays/projectm` (`vcpkg_from_github(... PATCHES ...)`). We do
**not** submit them upstream (decided 2026-10-05): upstream review is slow, and nothing should wait on it.

- `projectm_set_preset_variable(instance, name, value)`: writes a named variable into the active preset's
  per-frame **and** per-vertex contexts *before* per-frame code runs each frame (persisting until changed).
  Both, because MilkDrop copies only built-ins and `q1`..`q32` from per-frame to per-vertex code, so a
  custom variable would never get there on its own (found in 8.7). Covers macros (`mdw_m1`..`mdw_m8`), new
  audio variables (`mdw_beat_phase`, `mdw_bar_phase`, `mdw_bpm`, `mdw_onset`), and the override inputs.
  During a soft-cut both presets receive it. Built in 8.7; ADR-0012.
- Later, `projectm_set_external_texture(instance, name, gl_texture_id, w, h)`: registers a GL texture the
  texture manager hands out as `sampler_<name>` in warp/comp shaders. Covers `sampler_camera`,
  `sampler_video`.

The override itself needs no patch: when we load a `.milkdawp`, we **append** generated per-frame and
per-pixel lines after the preset's own code (MilkDrop runs `per_frame_N` lines in order), e.g. for a
preset whose own code ends at `per_frame_7`:

```
per_frame_8=rot = rot + mdw_dt * ((0) + (0.4) * (mdw_m1 - (0.5)));
per_frame_9=zoom = zoom * pow(1.04, mdw_zoom);
per_frame_10=warp = warp * mdw_warp;
```

The numbers carry on from the preset's own: projectM and MilkDrop both stop reading code at the first
missing number, so an earlier sketch's `per_frame_901` would never have run (found in 8.8). The exact
rules, and why a Macro on its default changes nothing bit for bit, are in ADR-0013.

so the preset computes its value, then our line reshapes it, then the warp mesh and shaders see the
result. Because overrides act on the preset's own variables, they compound through the feedback loop
exactly as the preset's own motion does: a real zoom, not a zoomed picture.

Risk: projectM might cache, rename or fold variables in ways that make the setter awkward (per-vertex
context is a separate evaluation context; q-vars are copied between them). Spike 8.7 answers this first.

### 4.3 Override modes

| Mode | Formula after the preset's code | Use |
|---|---|---|
| Replace | `v = v + (c - v) × amount` | Pin a colour, force zoom to 1 |
| Offset | `v = v + c` | Rotation bias, hue offset |
| Scale | `v = v × c` | Bigger waves, stronger warp |
| Rate | `phase += dt × c` (fosfora's "rates"), then the preset reads `phase` | Speeds: never jumps |
| Expression | arbitrary code reading `mdw_mN` | Curated presets that expose something clever |

`amount` = 0 (or c at its neutral value) must give the original preset bit-for-bit; that is a test.

### 4.4 Effects chain

Post passes in the engine's one GL context, after each layer and/or on the final canvas. First set (all
cheap, all a single fullscreen pass): hue rotate, saturation, brightness/contrast, tint, invert, pixelate,
posterize, blur, glow (quarter-res bloom), RGB split, mirror (X/Y/quad), kaleidoscope (N segments),
vignette, and an **extra feedback** pass of our own (previous canvas × decay, with zoom/rotate) that turns
any preset or a camera into a trail machine. Every amount is an automatable parameter. Order: fixed for v1
(colour → geometry → glow → feedback) to keep the parameter list static; per-preset ordering lives in the
`.milkdawp` file.

HDR: move layer/frame targets to `RGBA16F` when glow and feedback are on (fosfora's lesson), otherwise
8-bit stays.

#### Gate (proposed 2026-10-05)

Works like a noise gate on a guitar: when a layer's own audio input falls below a threshold, the layer
disappears, and it comes back the moment the input returns. Clean stops in technical passages show up as
clean stops in the picture. Renderer-agnostic (Stage A), no projectM change.

- **Parameters** (in the host's "Layer" group beside Opacity / Blend / Mute, not among the 16 Visual
  globals, because it acts on the layer's visibility, not on the picture):
  - `layerGateEnabled`: Bool, default Off. The toggle.
  - `layerGateThreshold`: Float, -100..0 dBFS, default **-80 dB** (decided 2026-10-05: sensible for a DI
    guitar; the range goes below the default so the knob isn't parked at its end stop). The threshold knob.
  - `layerGateRelease`: Float, 0..2000 ms (skewed so the short end has most of the travel), default
    80 ms. How long the layer takes to fade out once the gate closes: 0 is a hard cut, long values are a
    slow fade. Included from the start (decided 2026-10-05).
- **Detector:** the layer's *own* input (the instance's track; in Layers, each sender's own ring, so a
  gated guitar layer vanishes while the drum layer keeps going). Peak envelope in dBFS with a fast attack,
  read by the render thread from the audio ring each frame: at most one frame of latency, sample-accurate
  detection isn't needed for a picture. Uses what MilkDAWp receives, so its place in the FX chain matters
  (after the user's own gate it simply follows that gate).
- **Behaviour:** opens above the threshold, closes 3 dB below it (fixed hysteresis so a decaying note
  doesn't flicker), holds 50 ms, then fades out over the Release time. Opens with no fade, so the hit
  lands on time. Hold is fixed for v1.
- **"Not visible" means** the layer's draw opacity is multiplied by the gate envelope (0..1). Below it,
  other layers show through; on a lone instance the canvas fades to black.
- **The visual keeps running while gated.** Unlike Opacity 0 or Mute (which today stop feeding and
  drawing the layer, `RenderEngine::run()`), a closed gate still feeds audio and renders, so on reopen the
  preset is where it would have been, not frozen at the moment it closed. Costs GPU during silence, which is
  accepted for short gaps. Implementation: a separate gate atomic on `LayerChannel`, not a write to
  `opacity_`.
- **Lone instance:** today one layer draws straight into the output without the compositor, so the gate
  needs that path to go through `LayerCompositor` (or a one-pass fade) while the gate is enabled.
- **UI:** a level meter with the threshold line and an open/closed light in the Layer section of the
  drawer, as on any hardware gate, so the threshold can be set by eye. Shown on single instances too.
- **Later:** expose Hold; a gate *mode* (hide / freeze the last frame / fade to black only);
  side-chain key (gate one layer from another input); gate a media layer by an audio input.

### 4.5 Media sources

`MediaSource` → a GL texture updated on the render thread when a new frame exists (uploaded only on change).

- **Camera:** JUCE's `CameraDevice` covers Windows, macOS and Android; **not Linux**, which needs V4L2
  directly. Permission prompts on macOS and Android.
- **Video files: platform decoders** (decided 2026-10-05; no FFmpeg).
  Media Foundation on Windows, AVFoundation on macOS, MediaCodec on Android, each behind one small
  `VideoDecoder` interface that hands back CPU frames (BGRA/NV12) for upload. Linux: the system's
  GStreamer, loaded at runtime if installed (we don't ship it, so nothing to bookkeep); without it,
  video is unavailable on Linux with a plain message. Why not FFmpeg:
  - *Codec patents.* Shipping our own H.264/HEVC decoders puts patent licensing on us; the OS decoders
    are already licensed by the OS vendor. This is the deciding reason.
  - *LGPL bookkeeping.* An exact-build source offer, a configure that provably excludes GPL and nonfree
    parts, relinkable dynamic libraries on every platform, notices in every installer. We already carry
    this for projectM (D10); a second, much larger LGPL dependency doubles it.
  - *Size.* Even a decoder-only FFmpeg build is tens of MB per platform.
  - Costs of the platform route: three (four with Linux) small code paths, and each platform decodes a
    different set of formats (HEVC on Windows needs a Store extension). Document "H.264 MP4 works
    everywhere" and treat the rest as a bonus. JUCE's `VideoComponent` plays video but doesn't hand us
    frames for GL, so we can't just use it.
- **Images / GIF:** JUCE image loading, trivial.
- **Sync in a DAW:** a video file's position follows the host transport (ppq → seconds) when the host is
  playing, so a rendered DAW session is repeatable. Free-running in the app.

Uses: (1) a compositor layer with blend/opacity/displacement (Layers L2); (2) burned into projectM's
feedback buffer (spike); (3) a preset texture `sampler_camera` (patch 2 or Stage C); (4) a luminance mask
for effects (pixelate only where the dancer is).

### 4.6 Remote control

The web remote moved to its own phase (roadmap Phase 9, decided 2026-10-05). Its design notes stay here
until that phase gets a doc of its own. OSC stays in Phase 8 (8.4).

- **OSC** (`juce_osc`): receive on a configurable port (default 9000), send on 9001. Addresses
  `/milkdawp/<instance>/<paramId> f`, `/milkdawp/<instance>/next`, plus outbound beat/bar/drop signals.
- **Web remote:** one HTTP + WebSocket port; the page is bundled in `BinaryData`; JSON messages; full state
  snapshot on connect; ~10 Hz status (preset name, beat phase, levels). Pairing by QR code containing
  `http://<lan-ip>:<port>/?key=<random>`; keys revocable; LAN only by default. Library: JUCE has no HTTP
  server, so pick one in Phase 9 (candidates: IXWebSocket, BSD-3; civetweb, MIT; cpp-httplib + a WS add-on, MIT).
- **In the plugin**, one server per process (like `LayerRegistry`), listing every instance by its label.
  A remote move goes through the real parameter (`beginChangeGesture` / `setValueNotifyingHost` /
  `endChangeGesture`, on the message thread), so **the DAW records phone moves as automation**. Host
  automation playing back wins over the phone, as it would over a mouse.
- Windows Defender Firewall prompts the first time a plugin opens a listening socket, *in the host's name*.
  Off by default in the plugin; on-demand "Enable remote" with an explanation.

### 4.7 Our own renderer: EyesCream (Stage C)

Name decided 2026-10-05: **EyesCream**. Only built if the gate in 8.D says so. Shape:

- MilkDrop 2 pipeline: per-frame equations (projectm-eval) → per-vertex warp mesh (CPU, projectm-eval,
  ~48×36 grid, fine on CPU) → warp shader into the feedback texture → blur1/2/3 → custom waves and shapes
  → main waveform → composite shader → gamma, echo, borders. Noise textures and the texture pack as
  MilkDrop names them.
- **Shaders converted offline**: the `.milk` → `.milkdawp` converter translates HLSL to GLSL once (reusing
  the HLSL translator projectM vendors, licence to check), so the runtime never parses HLSL and the
  translated GLSL can be fixed by hand per preset.
- OpenGL 3.3 core / ES 3.0, same as the engine, Android (Phase 7) and JUCE surfaces. Not wgpu.
- Async preset compile on a shared context (fixes the compile-hitch risk in roadmap §10 for good).
- Our own transitions: MilkDrop 2's blend patterns, plus beat-driven masks (Layers §8 Q3) as first-class.
- Licence: projectM is LGPL-2.1; porting from it into our AGPL-3.0 code is licence-compatible but needs
  attribution, so stop and ask before copying any of it (roadmap §11). Prefer the MIT pieces
  (projectm-eval) and a clean implementation from the MilkDrop preset authoring guide. MilkDrop 2's own
  source was released by Nullsoft under a BSD licence (to verify before relying on it).
- `Visualizer` becomes an interface with two backends; the playlist picks per preset (`.milk` → projectM,
  `.milkdawp` with `mdw_renderer=eyescream` → ours), so both live side by side for as long as needed.

## 5. The `.milkdawp` format (sketch, v1)

**Decided (2026-10-05): a superset of `.milk`.** Same INI-style `key=value` lines, all MilkDrop keys untouched,
new keys prefixed `mdw_`, written in the same indexed style MilkDrop already uses (`wavecode_0_r=`). If a
spike (8.8) shows projectM ignores unknown keys, then a `.milkdawp` loads on stock projectM as its original
preset, MilkDrop-era editors can still open it, diffs stay readable, and the converter is lossless.
*8.8 (2026-10-06):* it does, and so does MilkDrop 2, once the file is renamed to `.milk` (ADR-0013).

```ini
[preset00]
; ---- MilkDAWp extension, format version ----
mdw_format=1
mdw_title=Flexi - mindblob [mash-up]
mdw_source=Cream of the Crop/Fractal/Flexi - mindblob.milk
mdw_source_sha256=4f1c…
mdw_tags=fractal,warm,slow
mdw_renderer=projectm          ; projectm | eyescream (Stage C)

; ---- controls: what the host can automate (fixed slots, see §6.1) ----
mdw_ctl_1_name=Swirl
mdw_ctl_1_slot=macro1
mdw_ctl_1_target=rot
mdw_ctl_1_mode=rate
mdw_ctl_1_min=-0.2
mdw_ctl_1_max=0.2
mdw_ctl_1_default=0
mdw_ctl_2_name=Blob colour
mdw_ctl_2_slot=macro2
mdw_ctl_2_mode=expr
mdw_ctl_2_code=ob_r = ob_r * (1 - mdw_m2) + mdw_m2; ob_b = ob_b * (1 - mdw_m2);

; ---- effects chain defaults for this preset (globals still override) ----
mdw_fx_1=glow,amount:0.35
mdw_fx_2=mirror,mode:x

; ---- inputs ----
mdw_input_camera=feedback,amount:0.15  ; layer | feedback | texture

; ---- original MilkDrop preset follows unchanged ----
fRating=4.000000
fGammaAdj=1.980000
zoom=0.99951
...
per_frame_1=wave_r = 0.5 + 0.5*sin(time*1.13);
...
```

The `mdw_ctl_*_target`/`mode` pairs are compiled at load into the appended `per_frame_N` lines of §4.2;
`expr` controls append their `code` as written.

### 5.0 Superset vs the alternatives (superset chosen 2026-10-05)

"Superset" means: **a `.milkdawp` file is a complete, valid `.milk` file with extra lines in it.** Strip
every `mdw_` line and what's left is the original preset, byte for byte. The file is self-contained (one
file to share, rate, tag, put in a playlist), and the original `.milk` still sits beside it untouched
(§5.1), so the duplication is deliberate: the `.milk` is the archive, the `.milkdawp` is the working copy.

How it loads on patched projectM (Stage B): our loader reads the file, takes out the `mdw_` lines, turns
the controls into the appended `per_frame_N` lines, and hands projectM ordinary `.milk` text through
`projectm_load_preset_data`. So projectM never needs to tolerate our keys; spike 8.8 is only about whether
*stock* tools (projectM elsewhere, MilkDrop itself) can open a renamed `.milkdawp` as a plain preset.

|                                        | **Superset** (chosen)                                                      | **Sidecar**                                                     | **Container** (TOML/JSON)                                      |
|----------------------------------------|----------------------------------------------------------------------------|-----------------------------------------------------------------|----------------------------------------------------------------|
| What it is                             | `.milk` text + `mdw_` lines, one file                                      | `foo.milkdawp` holds only the `mdw_` lines and names `foo.milk` | New structured file with the `.milk` text embedded as a string |
| Files per preset                       | 1 (+ the archived `.milk`)                                                 | 2, which must travel together                                   | 1 (+ the archived `.milk`)                                     |
| Opens in MilkDrop/projectM tools       | Yes, if renamed                                                            | The `.milk` does; the controls are lost                         | No                                                             |
| Breaks if the `.milk` is moved/renamed | No                                                                         | **Yes**                                                         | No                                                             |
| Human-readable, diff-friendly          | Yes, same style as `.milk`                                                 | Yes                                                             | Yes, but code in escaped strings                               |
| Multi-line code (Stage C GLSL)         | Awkward: one `key=value` per line, as `.milk` does (`warp_1=`, `warp_2=`…) | Same                                                            | Natural (multi-line strings)                                   |
| New parser dependency                  | None (we parse `.milk` already)                                            | None                                                            | toml++ or a JSON lib                                           |

Caveats of the superset: a MilkDrop-era editor that re-saves the file will drop our `mdw_` lines (it only
writes keys it knows), and the flat `key=value` style gets verbose for large blocks of code. Neither matters
much while mappings are static and made by the converter (§9 Q8). If Stage C ever needs big hand-edited
GLSL, a superset can still carry it the way `.milk` carries HLSL today (numbered lines).

### 5.1 Migration rules (decided 2026-10-05)

- **The `.milk` originals are kept, for posterity.** Migration only ever *adds* `.milkdawp` files;
  it never deletes, moves, renames or rewrites a `.milk`. The bundled Cream of the Crop pack ships
  unchanged beside the `.milkdawp` pack, and a user's own `.milk` folders are read-only to the
  converter. `mdw_source` and `mdw_source_sha256` point each `.milkdawp` back to its original.
- **Close is good enough.** The converter does not have to be perfect. Its report flags presets
  that look noticeably different so they can be fixed by hand or left on projectM, but a
  difference is not a failure. (Stage B renders through projectM itself, so neutral controls
  match the original by construction; this rule matters most for Stage C.)

Versioning: `mdw_format` is an integer; the parser in `core/` migrates old versions forward, same discipline
as `StateSchema`. Unknown `mdw_` keys from a newer version are kept on save, not dropped.

## 6. Controls in a DAW

### 6.1 Fixed slots, per-preset meaning

VST3, AU and CLAP expect the parameter list to be fixed when the plugin loads. Renaming per preset is
patchy across hosts (JUCE can signal a name change; many hosts ignore it). So:

- **16 Visual globals** (§6.2): always mean the same thing, on every preset and every renderer.
- **Macros 1–8**: each preset decides what they do (`mdw_ctl_N_slot=macroN`). The drawer shows the current
  preset's names ("Macro 1: Swirl"); the host shows "Macro 1". Like Ableton's rack macros.
- **Lock Macros** (§6.3): an automatable on/off that decides whether Macros survive a preset change.

- **Gate** (§4.4): Gate on/off, Gate Threshold, Gate Release, in the Layer group.

New parameters: 16 + 8 + 1 + 3 = 28, on top of today's 26. Grouped in the host (JUCE parameter groups
"Visual", "Macros") so the list stays navigable.

### 6.2 The 16 Visual globals (proposed 2026-10-05)

Every global has a **neutral** value that leaves the picture untouched, and at neutral it costs nothing.
"Stage A" is how it works on stock projectM through the effects chain; "Stage B" is what changes once a
`.milkdawp` preset is playing on patched projectM.

| #  | Control          | Type, range, neutral                          | Stage A (any preset)                                                 | Stage B (`.milkdawp`)                                                             |
|----|------------------|-----------------------------------------------|----------------------------------------------------------------------|-----------------------------------------------------------------------------------|
| 1  | **Hue**          | Float, -180..180°, 0                          | Hue rotate of the frame                                              | same                                                                              |
| 2  | **Saturation**   | Float, 0..2, 1                                | 0 = greyscale, 2 = vivid                                             | same                                                                              |
| 3  | **Brightness**   | Float, 0..2, 1                                | Exposure of the frame                                                | same                                                                              |
| 4  | **Speed**        | Float, 0..4, 1                                | Integrated into the preset clock (§6.4); time-driven motion only     | also scales `rate`-mode controls                                                  |
| 5  | **Zoom**         | Float, -1..1, 0                               | Zooms the frame in/out                                               | Offsets the preset's own `zoom`: the tunnel really pulls in, through the feedback |
| 6  | **Rotation**     | Float, -1..1, 0 (a speed, integrated)         | Spins the frame                                                      | Offsets the preset's `rot`: real swirl                                            |
| 7  | **Warp**         | Float, 0..3, 1                                | No effect (drawer says "needs a .milkdawp preset")                   | Scales the preset's `warp`                                                        |
| 8  | **Trails**       | Float, 0..1, 0                                | Our extra feedback pass (previous frame mixed back in)               | Pushes the preset's `decay` towards 1 as well                                     |
| 9  | **Wave Size**    | Float, 0..3, 1                                | No effect                                                            | Scales waveform and custom wave/shape sizes                                       |
| 10 | **Pixelate**     | Float, 0..1, 0                                | Block size, smooth steps                                             | same                                                                              |
| 11 | **Glow**         | Float, 0..1, 0                                | Quarter-res bloom                                                    | same                                                                              |
| 12 | **Blur**         | Float, 0..1, 0                                | Separable blur ("smoothing")                                         | same                                                                              |
| 13 | **Mirror**       | Choice: Off / Left-Right / Top-Bottom / Quad  | Fold the frame                                                       | same                                                                              |
| 14 | **Kaleidoscope** | Choice: Off / 3 / 4 / 5 / 6 / 8 / 12 segments | Radial fold                                                          | same                                                                              |
| 15 | **RGB Split**    | Float, 0..1, 0                                | Chromatic offset of the channels                                     | same                                                                              |
| 16 | **Media Mix**    | Float, 0..1, 0                                | Opacity of the chosen camera/video/image (source picked in settings) | also feeds `sampler_camera` presets                                               |

Why these: 1–3 cover colour, 4–9 are the "MilkDrop-native" motions you asked for (speed, size, rotation),
10–15 are the effects (pixelate, glow, smoothing, feedback via Trails), and 16 is the door for camera and
video. Two (Warp, Wave Size) do nothing in Stage A, on purpose: faking them as post effects would make them
mean something different later. Each control a renderer can't serve is greyed out with a reason.

**For later consideration** (not in the first 16; each one is a parameter in every host, so add sparingly):

- *Tint* (colour + amount: two parameters, colour pickers automate badly), *Contrast*, *Invert*,
  *Posterize*, *Vignette*, *Gamma*.
- *Beat Pulse*: a zoom/brightness kick on each beat from `BeatClock`. The most "MilkDAWp" of the lot, first
  in line if a slot frees up.
- *Strobe / Beat Flash*: needs a photosensitivity warning and a hard rate cap.
- *Edge / Outline*, *Film grain*, *Scanlines / CRT*, *Palette remap* (map luminance onto a gradient).
- *Displacement* amount (media layer warps what's below) once displacement blends exist.
- *Shape Size* separate from Wave Size; *Echo* (MilkDrop's video echo) separate from Trails.
- *Motion vectors* visibility; *Darken centre*.

### 6.3 Lock Macros (decided 2026-10-05)

An automatable Bool beside the Macros, like **Lock Current Preset**:

- **On:** a preset change keeps every Macro at its current value (the lane carries on; the new preset
  reads it with its own meaning).
- **Off** (the default, decided 2026-10-05): on a preset change the plugin moves each Macro to the new preset's
  `mdw_ctl_N_default`, so every preset first appears as its author intended.

Host details to respect:

- The jump happens at the transition moment (the cut, or the start of a soft-cut), on the message thread
  as a normal gesture, so the host sees it like a mouse move.
- A Macro lane the host is *playing back* overrides the jump on the next block anyway. In practice "Off"
  matters for un-automated Macros and live play; automated ones behave as if locked.
- In touch/latch/write automation modes the jump would be recorded. That's arguably right (it's what
  happened), but it needs a check in REAPER and one other host before shipping.
- Unused Macros (the preset declares fewer than 8) reset to 0 when Off, and keep their value when On.

### 6.4 Smoothing and musical feel

Every control has a short smoothing time (block-rate automation can step), and "rate" controls integrate.
Optional beat-synced modulation (LFO locked to `BeatClock` phase) is the natural next step and costs little
once controls exist.

## 7. Roadmap (proposed)

Mirrors development_roadmap.md Phase 8. Sizes as in the roadmap: S fits a session, M a few, L needs
splitting before starting.

**Stage A: controls on any renderer**

Done 2026-10-05: all of Stage A. Details, the 8.6 split, and what still needs a hand test are
in the roadmap's Phase 8 and ADR-0011.

- [x] 8.1 (M) ADR + `ParameterModel`: the 16 Visual globals (§6.2), Macro 1–8 and Lock Macros (§6.3),
      parameter groups, state schema additive keys, `docs/parameters.md` regenerated, drawer section
      "Visual" (both shells). Globals do nothing yet except the ones 8.2/8.3 wire.
- [x] 8.2 (M) `EffectsChain` in the engine: hue/sat/brightness/tint, pixelate, blur, glow, RGB split,
      mirror, kaleidoscope, extra feedback; per-layer and canvas placement; GPU-tested against
      hand-computed values like `LayerCompositorTests`; skipped entirely when all amounts are neutral.
- [x] 8.2b (M) Gate (§4.4): `layerGateEnabled` / `layerGateThreshold` (default -80 dB) /
      `layerGateRelease` (default 80 ms), peak envelope from the layer's own ring with 3 dB hysteresis,
      50 ms hold, fade-out over Release, instant open; gated layers keep feeding
      and rendering; lone-instance path through the compositor while enabled; drawer meter with threshold
      line and open light. Tests: a fixture with clean stops (`breakdown_drop` or a new staccato one) opens
      and closes on the right frames; no flicker on a decaying tone at the threshold.
- [x] 8.3 (S) Speed: integrate `dt × speed` into the time passed to `projectm_set_frame_time`; document
      what it does not slow (per-frame decay, audio response).
- [x] 8.4 (M) OSC in/out (`juce_osc`), per-process in the plugin, outbound beat/bar/drop signals.
- [-] 8.5 Web remote: moved to its own phase, roadmap Phase 9 (2026-10-05).
- [x] 8.6 (L) Media sources: camera (JUCE `CameraDevice`; V4L2 on Linux), image, video (platform
      decoders, §4.5; GStreamer at runtime on Linux), as compositor layers with transport-synced video in the plugin.
      Displacement blend modes in `LayerCompositor`. Spike: camera via `burn_texture` into projectM's
      feedback.

**Stage B: `.milkdawp` v1 on patched projectM**

- [x] 8.7 (M) **Spike:** the `projectm_set_preset_variable` patch in the overlay port. Prove: a value set
      from outside is read by per-frame code, survives frames, reaches per-vertex code via the usual
      copy, applies to both presets during a soft-cut. Write the result into an ADR. No upstream PR
      (decided 2026-10-05): upstream moves slowly, so we carry the patch ourselves (§8 Risks).
      *Done (2026-10-05), ADR-0012:* every claim holds, proven by pixel-readback headless tests
      (`[presetvars]`), plus init code and presets loaded later. One correction: "the usual copy" only
      carries `q1`..`q32`, so the patch writes per-vertex code's context directly (a control test shows
      a plain variable doesn't get there). Not covered: custom shape/wave code (route through `q` vars).
      Windows only so far; Linux CI and macOS still to run.
      *Upstream check (2026-10-05, master `dd89dfb`; latest tag still v4.1.7):* still no setter for preset
      variables. Only user sprites have get/set (`projectm_sprite_get_var`/`_set_var`, 2026-08-28). Draft
      PR #971 (expression variable *watch* API, read-only, stalled since 2026-02) is the closest; no PR
      or issue proposes a setter. If #971 lands, recheck the patch at the next bump: it touches the same code.
- [x] 8.8 (S) **Compatibility check:** do stock projectM and MilkDrop open a renamed `.milkdawp` (extra
      `mdw_*` keys) as the plain preset? The superset format is already chosen and our loader strips the
      keys anyway (§5.0), so the result only goes into the user docs ("rename to `.milk` to open
      elsewhere", or not). *Done 2026-10-06: yes, renamed; ADR-0013.*
- [x] 8.9 (M) `core/MilkdawpPreset`: parse, validate, serialize, version migration, round-trip tests;
      control → generated code compiler (§4.3) with "neutral = original" tests. *Done 2026-10-06.*
- [x] 8.10 (M) Engine: load `.milkdawp`, push control values each frame, globals through the override
      layer where the preset supports them and through the effects chain otherwise. `PresetLibrary` and
      the browser handle both file types. *Done 2026-10-06; Wave Size turned out impossible on
      projectM (ADR-0013).*
- [x] 8.11 (L) `tools/mdw-convert`: `.milk` → `.milkdawp` in bulk. Heuristics propose controls (variables
      the preset actually writes: zoom, rot, warp, wave/shape colours, q-vars), a headless before/after
      render check that defaults are identical, and a report. Writes `.milkdawp` files *beside* the
      originals and never touches a `.milk` (§5.1); a test checks the source tree is byte-identical after a
      run. Curate a first pack from Cream of the Crop by rating.
      *Done 2026-10-06, ADR-0014:* every preset in the pack is rated 5, so instead the whole
      pack is converted at build time, beside the originals, minus what `--verify` turns down.
- [ ] 8.12 (M) Patch 2: external textures (`sampler_camera`, `sampler_video`) and the `mdw_` audio
      variables (`beat_phase`, `bar_phase`, `bpm`, `onset`); a handful of hand-made presets that use them.
      *Upstream check (2026-10-05, master `dd89dfb`):* 4.2's `projectm_set_texture_load_event_callback`
      can hand projectM an existing GL `texture_id` for a named texture (docs list "video frames"), a
      possible substitute for the external-texture half of this patch. Untested; projectM takes ownership
      and deletes the texture, and the callback fires on load, not per frame. Draft PR #970 (stalled since
      2026-02) would add a "use but don't own" mode.

**Decision gate**

- [ ] 8.D (S) Own renderer go / no-go, written as an ADR from Stage B experience: what projectM blocked,
      performance on Android (Phase 7), maintenance cost of carrying
      patches.

**Stage C: our own renderer (only on "go")**

- [ ] 8.13 (L) Skeleton: per-frame + per-vertex (projectm-eval), warp mesh, feedback, composite; renders
      the three fixture presets. Golden-image harness: same audio, same frame times, projectM vs ours,
      SSIM per frame, fixed seeds where possible.
- [ ] 8.14 (L) Waves, shapes, borders, echo, gamma, motion vectors; blur1-3, noise textures, texture pack.
- [ ] 8.15 (L) Shader presets: offline HLSL → GLSL in `mdw-convert`; closeness report (not a pass/fail gate, §5.1) across Cream of the
      Crop; per-preset `mdw_renderer` chosen by the score.
- [ ] 8.16 (M) Async compile, our own transitions (MilkDrop 2 blend patterns + beat masks).
- [ ] 8.17 (L) Beyond MilkDrop: particles / extra pass graphs (fosfora-style), only after the above.

## 8. Risks

- **Carrying our own patch:** we don't submit it upstream (2026-10-05), so we carry it in the overlay port
  forever and re-apply it on every projectM bump. Keep it tiny; the pin and bump procedure already exist (ADR-0008).
- **Parameter count explosion:** every global and effect is an automation lane in every host. Group them
  in the host (JUCE parameter groups), keep the count reviewed in the ADR (8.1).
- **"Zoom" means two different things** in Stage A (canvas zoom) and Stage B (preset zoom) for the same
  global. The drawer must say which applies to the current preset, or the user will think it is broken.
- **Remote server in a plugin** (Phase 9): firewall prompts in the host's name, several instances and
  processes, security on shared networks. Off by default; key-based pairing; LAN only.
- **Camera on Linux** (JUCE `CameraDevice` doesn't support it) and **video formats differing per
  platform** (platform decoders, §4.5): promise H.264 MP4 everywhere, the rest is best effort.
- **Lock Macros Off + host automation write modes** may record the per-preset jumps (§6.3). Check in
  two hosts before shipping.
- **Own renderer fidelity:** HLSL translation, MilkDrop quirks, `rand()` in presets breaks exact golden
  comparisons. "Close" is the bar, not "exact" (§5.1), so this is about the badly-off outliers. Mitigated by
  the gate, by converting offline, and by letting projectM keep any preset ours is visibly far off on.
- **Scope:** this is the largest body of work proposed for the project. It stays post-1.0 (roadmap §10,
  "Scope creep") and each stage ships on its own.

## 9. Open questions

1. ~~Superset vs sidecar vs container~~: decided (2026-10-05), **superset** (§5.0).
2. ~~Which Visual globals?~~ Decided (2026-10-05): **16** globals; the proposed list is §6.2, awaiting
   Matthew's review of the list itself. Macros stay at 8 unless he wants 16.
3. ~~Do Macros keep their value on a preset change?~~ Decided (2026-10-05): a **Lock Macros** parameter
   (§6.3), default **Off**.
4. ~~Video decoding~~: decided (2026-10-05), **platform decoders**, no FFmpeg (§4.5).
5. ~~Web remote~~: deferred to its own phase (roadmap Phase 9). Server library and plugin-vs-app-only are
   that phase's questions.
6. ~~Should the curated `.milkdawp` pack replace Cream of the Crop?~~ Decided (2026-10-05): beside it;
   the `.milk` originals are always kept (§5.1). Which one the playlist uses (2026-10-06): the
   `.milkdawp`, which sits beside its `.milk` and hides it from the list (ADR-0014).
7. ~~Name~~: decided (2026-10-05), **EyesCream** for the renderer. Still open: whether the curated pack
   shares the name ("the EyesCream pack").
8. ~~Control editor?~~ Decided (2026-10-05): **no, mappings stay static for now.** `.milkdawp` files come
   from `mdw-convert` and hand editing; the app only plays them. The 1.0 non-goal "we do not author
   presets" stands. *Note (2026-10-07):* Matthew added "Macro slots as insert slots" to the roadmap's
   post-1.0 backlog: add or replace a slot's control on the playing preset, in memory only, never
   written to the file. Close to an editor without saving; the backlog entry lists what's open.
9. ~~Gate release and default threshold~~: decided (2026-10-05), Release is a parameter from the start
   (default 80 ms); default threshold -80 dB (§4.4).

## 10. Decisions log

- (2026-10-05) Created. Recommended order: controls on any renderer (A) → `.milkdawp` v1 on patched
  projectM (B) → own renderer behind a decision gate (C).
- (2026-10-05) Matthew: the plan matches what he had in mind. Migration keeps every `.milk` file for
  posterity (only adds `.milkdawp` files beside them). Conversion need not be perfect; close is the
  goal (§5.1).
- (2026-10-05) 16 Visual globals (list proposed in §6.2). New automatable **Lock Macros** parameter
  (§6.3). Web remote moved to its own phase (roadmap Phase 9). Renderer name: **EyesCream**. Control
  mappings stay static: no in-app control editor. Video: platform decoders recommended over FFmpeg
  (patents, LGPL bookkeeping, size), pending confirmation.
- (2026-10-05) Matthew confirmed: **superset** format (§5.0), **platform decoders** for video (§4.5), and
  **Lock Macros defaults to Off** (§6.3). Spike 8.8 is now a compatibility note, not a format decision.
- (2026-10-05) Matthew: add a **Gate** effect: a layer disappears while its input is below a threshold,
  toggleable, like the gate on his guitar for clean stops. Design in §4.4, item 8.2b.
- (2026-10-05) Gate: Release is its own parameter from the start; default threshold -80 dB (DI guitar).
- (2026-10-05) Stage A built (ADR-0011). Decided along the way:
  - The media blend mode (Normal, Add, Screen, Multiply, Luma key, Displace, Burn in) is a
    setting, not a 17th Visual global.
  - Media Mix is the media's opacity over the layer, before the effects.
  - The `burn_texture` spike became the Burn in blend.
- (2026-10-05) Finding for 8.12: projectM 4.2's `projectm_set_texture_load_event_callback`
  takes an app-supplied GL texture for any sampler a preset names. That may give
  `sampler_camera`/`sampler_video` without patch 2, but projectM owns and deletes that
  texture, so a live feed needs a spike first.
- (2026-10-06) 8.11 (ADR-0014): every Cream of the Crop preset is rated 5, so "curate by rating"
  can't work. Matthew: convert **all** of it, leaving out only what fails the before/after check,
  with each `.milkdawp` **beside its `.milk`** in the bundled content, made at build time.
