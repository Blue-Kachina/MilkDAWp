# MilkDAWp 2 — Development Roadmap

MilkDAWp 2 is a ground-up rebuild of [MilkDAWp](https://github.com/Blue-Kachina/MilkDAWp): a
JUCE + projectM music visualizer that ships as a DAW plugin (VST3, AU) **and** as a standalone
application for Windows, macOS, and Linux. This document scopes the project, records what v1
taught us, fixes the architecture, and breaks the work into phases small enough for a single
focused development session each.

Status legend used throughout: `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` dropped.

---

## Table of contents

1. [Vision, goals, non-goals](#1-vision-goals-non-goals)
2. [What v1 taught us (code audit)](#2-what-v1-taught-us-code-audit)
3. [Product scope by tier](#3-product-scope-by-tier)
4. [Architecture](#4-architecture)
5. [Key decisions](#5-key-decisions)
6. [Repository layout](#6-repository-layout)
7. [Phased plan](#7-phased-plan)
8. [Testing and CI strategy](#8-testing-and-ci-strategy)
9. [Packaging, signing, distribution](#9-packaging-signing-distribution)
10. [Risks and mitigations](#10-risks-and-mitigations)
11. [Working agreements for AI-assisted development](#11-working-agreements-for-ai-assisted-development)
12. [References](#12-references)

---

## 1. Vision, goals, non-goals

### Vision

Drop MilkDAWp on a channel in your DAW, or open it as an app and point it at any audio source,
and get MilkDrop-style visuals that move **with the music**: preset changes land on bars and
drops instead of on a wall-clock timer, and the whole thing is stable enough to run a four-hour
live set or a streaming session without babysitting.

### Goals

- **One engine, two shells.** A single visualization engine drives both the plugin and the
  standalone app. Features land in both at once because the shells are thin.
- **Musical transitions.** Real onset detection and tempo tracking in our own code, with host
  transport (tempo, bars, beats) used whenever the DAW provides it. Transitions are
  beat-quantized, energy-aware, and scheduled with sample accuracy.
- **Zero audio impact.** The plugin remains a bit-exact passthrough with no added latency, no
  allocations, no locks, and no message-thread calls on the audio thread.
- **Robust rendering.** projectM lives in an engine that outlives the editor window. Opening,
  closing, popping out, or going fullscreen never resets the visual.
- **Installable by normal people.** Signed installers on all three platforms; presets bundled;
  first-run works with no configuration.
- **Testable.** The core is a plain C++ library with an offline CLI, so beat detection and
  transition logic can be iterated on against audio fixtures in CI, without a DAW or GPU.

### Non-goals (for 1.0)

- Authoring or editing `.milk` presets. We consume presets; we do not write them.
- Video/media playback, camera input, or non-projectM render backends.
- AAX (Avid signing program), iOS, or web builds.
- Android in 1.0. **Android is a planned post-1.0 target** for the standalone app (Phase 7,
  ADR-0010, D16). Until then, 1.0 work keeps it cheap: the video-first UI (§4.9) stays
  touch-compatible, surfaces work without a shared GL context (2.15), and Phase 4 follows
  ADR-0010's shell boundary rules.
- A general-purpose VJ mixer. Scenes, setlists, OSC, and texture sharing (Spout/Syphon/NDI)
  are post-1.0 (see §3).

---

## 2. What v1 taught us (code audit)

v1 (`Blue-Kachina/MilkDAWp`, latest tag `v0.7.5`) works and ships, and got there fast. The
following observations come from reading its source and are the concrete reasons v2 is a rebuild
rather than a refactor. Line references are to `src/PluginProcessor.cpp` at `v0.7.5` unless
noted.

### 2.1 Beat detection is not actually ours

- `produceAnalysisSnapshot()` (lines 969–1004) runs an FFT and discards the result
  ("reserved for future phases"), computes short-time energy, and maintains a moving average
  with a comment: *"for future beat detection (no output yet)"*. Nothing downstream consumes it
  except a CPU fallback gradient.
- Beat-driven transitions rely entirely on projectM's internal hard-cut detector
  (`projectm_set_hard_cut_*`, lines 3350–3383), which is a volume-delta threshold with no tempo
  model. Because projectM's own cooldown timer does not reset when we load presets externally,
  v1 adds an application-level cooldown inside the callback and sets
  `projectm_set_preset_duration(86400)` to disable projectM's timer (line 3359). These are
  workarounds stacked on a detector we do not control.
- **v2:** own the analysis (§4.3). Feed PCM to projectM for its visuals, but drive transitions
  from our onset detector, tempo tracker, and, in the plugin, the host's transport.
- The `set_preset_duration(86400)` / hard-cut-disable workaround does **not** carry forward:
  projectM 4's core never switches presets by itself. Its timer and hard-cut detector only
  fire `projectm_preset_switch_requested_event`, and v2 never registers that callback (audit
  of the 4.1.7 and 4.2 sources, 2026-09-26; see ADR-0008).

### 2.2 Transitions have three competing clocks

Timed auto-advance is driven by a `juce::Timer` on the message thread **and** by DAW playhead
time computed on the audio thread (lines 444–496) **and** by projectM's hard-cut callback on the
GL thread. Each path calls `nextPresetInPlaylist()` via `MessageManager::callAsync`. Which one
fires first depends on scheduling, and none of them can land a transition on a beat.

**v2:** one `TransitionScheduler` in the core with a single simulated-time input, emitting
sample-stamped transition requests (§4.4).

### 2.3 Real-time safety violations on the audio thread

- `processBlock` allocates a `std::vector<float>` every block (line 420).
- `processBlock` calls `juce::MessageManager::callAsync` (lines 479, 491), which allocates and
  takes a lock.
- `parameterChanged` (called from the host's automation thread or the audio thread) mutates
  playlist `juce::Array`s and calls `goToPlaylistRelative()` directly (lines 523–560) while the
  message thread reads the same arrays for the UI. This is a data race.

**v2:** the audio callback only writes to lock-free rings. Everything else happens on the
engine thread. Enforced with a real-time sanitizer build in CI (§8).

### 2.4 Two render paths, one of them fake

- `VisualizationThread.h` contains a stub `ProjectMContext` (line 27) that renders animated
  gradients into a `juce::Image`. `SharedAssetCache` caches metadata for that stub, and
  `AdaptiveQualityController` scales the stub's back buffer. Six of seven unit tests exercise
  this path.
- The real projectM instance lives in `VizOpenGLCanvas`, a component inside the **editor**
  (line 3021). It is created lazily on the GL thread and destroyed in
  `openGLContextClosing()` (line 3563).

Consequences: closing the editor kills the visualization; the pop-out and dock operations
reparent the canvas component (lines 2768–2830), which forces JUCE to recreate the GL context,
which destroys and recreates projectM, so every pop-out resets the visual; adaptive quality has
no effect on what the user actually sees.

**v2:** projectM is owned by an engine bound to the processor or app lifetime and renders into
an FBO. Windows are just presentation surfaces (§4.5).

### 2.5 Preset loading blocks the render thread

`projectm_load_preset_file` is called on the GL thread between frames (line 3408). Preset
compilation can take tens to hundreds of milliseconds, so every transition is a visible hitch.
**v2:** measure it, prefetch file contents, pre-validate presets off-thread, and time the load
so the blend midpoint lands on the beat (§4.4).

### 2.6 Three mechanisms for loading one library

projectM symbols are resolved at runtime via `GetProcAddress` on Windows and `dlsym` on POSIX
(two near-identical blocks, lines 3178–3290), *and* the Windows build uses `/DELAYLOAD`
(`CMakeLists.txt` lines 385–397), *and* macOS/Linux set rpaths. The runtime resolution exists so
hosts can scan the plugin even when the DLL is missing, which is a real requirement.
**v2:** one `ProjectMLibrary` wrapper with one loading strategy per platform, and a
"visualization unavailable" state that the UI explains instead of silently falling back.

### 2.7 Structure

`PluginProcessor.cpp` is 3,696 lines and contains the processor, the editor, the look-and-feel,
the external window, the settings panel, and the GL canvas. `PluginEditor.cpp` is a 36-line
stub. Parameter access on the GL thread goes through string-keyed lookups every frame. Several
`static bool` once-flags live inside member functions.

### 2.8 CI covers the stubs

Linux and macOS CI build tests only (plugin off); only Windows builds the plugin. No test
touches the real render path, transitions, or state migration against real presets. No plugin
validator runs.

### 2.9 Worth keeping

- vcpkg manifest mode with pinned baseline, custom dynamic triplets, and the LGPL shared-linkage
  check in CMake.
- `CMakePresets.json` layout and the tag-triggered release workflow, including the macOS
  `install_name_tool` fix-up loop.
- The parameter set itself (beat sensitivity, transition duration + jitter, shuffle, lock,
  preset index, trigger next/prev, hard-cut controls, blend time, quality override) is a good
  1.0 surface and is the basis for state migration.
- Editor-size persistence and the Cubase-specific ordering lesson (host may create the editor
  before `setStateInformation`).
- The DAW-playhead idea: pausing and resetting the transition clock when transport stops or
  loops is correct behaviour and carries forward.
- Embedding SVG icons and the logo as binary data (the loader itself needs porting: JUCE 9
  removed `Drawable::createFromSVG(const XmlElement&)`, see §4.11).
- The OBS-friendly external window: fixed title, transparency, hover overlay, borderless
  fullscreen on a chosen display.
- Keyboard shortcuts in the plugin: F11 in the editor and F11/Esc in the external window
  worked in the hosts tested, with `EDITOR_WANTS_KEYBOARD_FOCUS FALSE`. v2 keeps and extends
  them (§4.9).

---

## 3. Product scope by tier

| Capability | MVP (0.x) | 1.0 | Post-1.0 |
|---|---|---|---|
| VST3 plugin (Win/macOS/Linux) | ✔ | ✔ | |
| AU plugin (macOS) | | ✔ | |
| CLAP / LV2 | | | ✔ |
| Standalone app (Win/macOS/Linux) | ✔ (JUCE Standalone wrapper) | ✔ (full app shell) | |
| Standalone app (Android, Phase 7) | | | ✔ |
| Audio input: device / interface | ✔ | ✔ | |
| Audio input: system loopback | documented virtual-cable workaround | Windows + macOS native | Linux native |
| Own onset + tempo tracking | ✔ | ✔ | |
| Host transport sync (tempo, bars) | ✔ | ✔ | |
| Transition modes: timed, beat-quantized, manual | ✔ | ✔ | |
| Transition mode: energy / section-change | | ✔ | |
| Playlist: folder scan, shuffle-no-repeat, lock, prev/next, index automation | ✔ | ✔ | |
| Preset library: tags, ratings, favourites, weighted shuffle | | ✔ | |
| Video-first window with control drawer (§4.9) | ✔ | ✔ | |
| Output window: fullscreen on any display, primary window keeps live mirror | ✔ | ✔ | |
| Detached controls window | | ✔ | |
| Engine survives editor close / output window open-close | ✔ | ✔ | |
| Adaptive quality (FBO resolution scaling that affects real output) | | ✔ | |
| Host automation of all parameters | ✔ | ✔ | |
| MIDI learn (standalone) | | ✔ | |
| Bundled preset pack (Cream of the Crop + textures, D12) | | ✔ | |
| Searchable preset browser (needed at ~10k bundled presets) | | ✔ | |
| Installers for all platforms, signed where free (D11) | | ✔ | |
| Texture sharing output (Spout / Syphon / NDI) | | | ✔ |
| Scenes and snapshot morphing | | | ✔ |
| Setlists and cues | | | ✔ |
| OSC / web remote | | | ✔ |
| Out-of-process renderer ("Link mode") | | | ✔ |
| Layers: multiple inputs → multiple projectM instances composited on one canvas | | | ✔ |

---

## 4. Architecture

### 4.1 Layers

```
┌──────────────────────────────┐   ┌──────────────────────────────┐
│  milkdawp_plugin             │   │  milkdawp_app                │
│  JUCE AudioProcessor/Editor  │   │  JUCE GUI app shell          │
│  VST3 · AU · (Standalone)    │   │  device/loopback input,      │
│  host transport → BeatClock  │   │  MIDI learn, preferences     │
└──────────────┬───────────────┘   └──────────────┬───────────────┘
               │            milkdawp_ui (shared JUCE widgets,       │
               │            look-and-feel, output windows)          │
┌──────────────┴──────────────────────────────────┴───────────────┐
│  milkdawp_engine  (JUCE-dependent)                              │
│  RenderEngine · ProjectMLibrary · OutputSurface · PresetLoader  │
└──────────────────────────────┬──────────────────────────────────┘
┌──────────────────────────────┴──────────────────────────────────┐
│  milkdawp_core  (no JUCE, no GL; std C++20 only)                │
│  AudioRing · Analyzer (FFT, bands, onsets) · TempoTracker ·     │
│  BeatClock · TransitionScheduler · Playlist · PresetLibrary ·   │
│  ParameterModel · StateSchema/Migration · Messages (POD)        │
└─────────────────────────────────────────────────────────────────┘
```

Rules:

- `milkdawp_core` has **no** JUCE or OpenGL dependency. It compiles on Linux CI without a
  display and is exercised by `mdw-analyze`, an offline CLI (§7, Phase 1). This is what makes
  beat detection and transitions testable by an agent in a loop.
- `milkdawp_engine` is the only place that touches projectM and GL.
- Shells never talk to projectM. They post messages to the engine and read status snapshots.

### 4.2 Threading model

| Thread | Owner | Does | Never does |
|---|---|---|---|
| Audio (host callback / device callback) | shell | copies PCM into `AudioRing`, writes transport info into an atomic snapshot | allocate, lock, log, call message thread |
| Analysis | core, driven by engine | pulls from `AudioRing` in hops (512 samples), runs FFT/onsets/tempo, updates `BeatClock`, ticks `TransitionScheduler` | touch GL or UI |
| Render (GL) | engine | feeds PCM to projectM, executes due `TransitionRequest`s, renders to FBO, presents to surfaces | parse presets, block on I/O |
| Preset I/O | engine | scans folders, reads and pre-validates preset files, warms the next preset | touch GL |
| Message (UI) | shell | widgets, parameter attachments, status polling via a lock-free snapshot | block waiting on any other thread |

Communication is via SPSC/MPSC lock-free queues carrying plain-old-data messages
(`core/Messages.h`). Strings cross threads only as interned preset IDs, never as `juce::String`.

### 4.3 Audio analysis (core)

Inputs: mono mix of the ring at the source sample rate, resampled to a fixed internal rate
(44.1/48 kHz passthrough; others resampled) so tuning constants are stable.

Pipeline per 512-sample hop (≈10.7 ms at 48 kHz):

1. **STFT** with a 2048-point Hann window (4× overlap).
2. **Band energies**: bass / low-mid / mid / high with attack/release smoothing, plus a
   broadband RMS. These feed UI meters and the energy-based transition mode.
3. **Onset detection function (ODF)**: log-compressed magnitude spectral flux, half-wave
   rectified, optionally per band (bass-only ODF is what we want for kick-aligned cuts).
4. **Peak picking**: adaptive threshold (`mean + k·std` over a sliding ±0.5 s window), minimum
   inter-onset interval 60 ms, emits `Onset{samplePos, strength, band}`.
5. **Tempo estimation**: autocorrelation (or comb-filter bank) of the ODF over a rolling 6–8 s
   window restricted to 60–200 BPM, with octave-error weighting toward 90–150. Emits a tempo
   hypothesis with confidence.
6. **Beat phase tracking**: a lightweight predictive tracker (comb/PLL or a small
   dynamic-programming pass over the ODF window) that predicts the next beat time and corrects
   on strong onsets. Emits `BeatClock{bpm, nextBeatSample, beatIndex, barIndex, confidence}`.
   Downbeat (bar) estimation is best-effort: assume 4/4, pick the phase that maximizes bass
   onset energy.

**Host transport override (plugin only):** when `AudioPlayHead` reports `isPlaying`, a valid
`bpm`, and `ppqPosition`, the `BeatClock` is derived from the host with confidence 1.0 and the
detector runs only for meters and energy. Time signature comes from the host when present.

Acceptance metrics (Phase 1) use an annotated fixture set: beat F-measure ≥ 0.85 within ±70 ms
on steady electronic material, tempo within 2% or an exact octave, and no false onsets during
30 s of silence or pink noise.

### 4.4 Transition scheduling (core)

`TransitionScheduler` is a pure state machine ticked by the analysis thread with the current
`BeatClock`, band energies, transport state, playlist, and parameters. It outputs
`TransitionRequest{presetId, cutStyle, blendSeconds, dueAtSample}`.

Modes (a parameter, automatable):

| Mode | Behaviour |
|---|---|
| Manual / Locked | never auto-advances |
| Timed | v1 behaviour: duration with optional jitter min/max; clock pauses when transport stops and resets on loop/relocate |
| Beat-quantized | advance every N bars (1, 2, 4, 8, 16), due at the predicted downbeat; falls back to Timed when beat confidence is below threshold for more than 4 s |
| Hybrid | Timed target, snapped forward to the next bar boundary |
| Energy | hard cut on a strong bass onset when broadband energy exceeds a rolling percentile (a "drop"), with a cooldown expressed in bars; otherwise behaves like Beat-quantized |

Cut style is Hard or Soft with a blend time in seconds or beats. Preset selection is a pluggable
policy: Sequential, Shuffle with a no-repeat history window, or Weighted (ratings/tags, 1.0).

Landing on the beat: the render thread knows the sample position of the frame it is about to
present (from the ring's write cursor and the audio clock). A request is executed on the first
frame whose span includes `dueAtSample`. For Soft cuts the request is issued `blend/2` early so
the perceptual midpoint sits on the beat. Preset file contents are prefetched by the Preset I/O
thread as soon as the *next* preset is chosen, which happens one transition ahead.

### 4.5 Rendering engine

- `ProjectMLibrary`: RAII wrapper around the projectM 4 C API. Loads the shared library at
  runtime on all platforms with one code path, exposes typed functions, and reports a clear
  `Unavailable{reason}` state. Uses `projectm_opengl_render_frame_fbo` so we control the target.
  That function exists only from projectM **4.2**. In 4.1.7, `projectm_opengl_render_frame`
  always draws its final composite to framebuffer 0, whatever FBO is bound. projectM is
  therefore pinned to an upstream 4.2 commit (ADR-0008, D15), and 4.2 is the minimum
  supported version.
- projectM's other responsibilities, so we don't rebuild them: soft-cut blends (built-in
  transition shaders, length set by `projectm_set_soft_cut_duration`), loading a preset from
  memory (`projectm_load_preset_data`), preset-time control from our clock
  (`projectm_set_frame_time`, 4.2), and the bass/mid/treb levels presets read (scaled by
  `projectm_set_beat_sensitivity`). We own *when* to cut; projectM owns *how* it looks.
- `RenderEngine`: owns exactly one GL context and one projectM instance for the lifetime of
  the processor or app. Renders into an FBO at the *output* resolution (the largest attached
  surface, times the adaptive-quality scale), then presents to each `OutputSurface`. The
  context is an `OffscreenGLContext` that belongs to no window, on the engine's own render
  thread (ADR-0009, the outcome of the 2.3 spike).
- `OutputSurface`: the embedded primary-window surface (editor or app main window) or an
  owned `OutputWindow` that can go fullscreen on a chosen display (§4.9). Surfaces attach and
  detach without affecting the engine. Each surface is its own JUCE GL context, linked to
  the engine's at creation by a handshake the surface runs itself (`wglShareLists` with the
  render thread parked). JUCE's `setNativeSharedContext` fails on NVIDIA, see ADR-0009. It
  draws the engine's latest texture with JUCE's `copyTexture`. A surface that can't share
  (a driver that refuses, Linux until EGL sharing exists, Android under stock JUCE) detects
  it and falls back to CPU copies of each frame, read back through two pixel-pack buffers
  and uploaded into a texture of its own, one frame behind (2.15, ADR-0009).
- Context ownership when no surface is visible (plugin editor closed, no Output window): the
  engine keeps its context, its projectM instance and all logical state (current preset,
  playlist position, scheduler), and does no GPU work. Rendering resumes on the next visible
  surface with the visual exactly where it was, which covers the "keep visual trails alive"
  1.0 option without a visible context-owner window.
- Adaptive quality scales the FBO, not a CPU image, and is driven by measured GPU frame time
  with hysteresis. The user sees the effect.

### 4.6 Out-of-process rendering (design constraint, not MVP work)

v1 is described as running "in a separate process/thread". It is a thread. A separate renderer
**process** would isolate hosts from GL driver issues and preset crashes, and the standalone app
could *be* that process ("Link mode"). It also makes embedding a preview inside the plugin
editor hard on macOS (cross-process view embedding is not supported).

Decision for 1.0: in-process engine thread. But the engine boundary is designed for it: all
shell↔engine traffic is POD messages plus an audio ring, so a shared-memory + pipe transport can
replace the in-process queues later without touching the shells. Post-1.0 item.

### 4.7 Standalone audio input

- MVP: `juce::AudioDeviceManager` input device selection (interfaces, mics, virtual cables
  like VB-Cable/BlackHole). Ships with a "how to capture system audio" doc per platform.
- 1.0: native system loopback capture modules behind a `SystemAudioCapture` interface:
  Windows via WASAPI loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`), macOS via Core Audio process
  taps (macOS 14.2+) with ScreenCaptureKit audio as fallback (13+), each requiring a permission
  prompt handled in the app. Linux stays on PulseAudio/PipeWire monitor sources exposed through
  the device list.
- `SystemAudioCapture` is an interface owned by the app shell, with one implementation file
  per platform chosen at runtime, so Android's `AudioPlaybackCapture` module (Phase 7,
  ADR-0010) plugs in without touching the others. Microphone input through
  `AudioDeviceManager` is always available, and is Android's first tier.

### 4.8 State and compatibility

- State is a versioned schema (`StateSchema v2`) serialized as a `ValueTree`. Round-trip is
  unit-tested. There is no v1 migration (dropped 2026-09-26, see 1.14): a v1 session opens
  with v2 defaults, because the loader skips lines it cannot parse.
- Preset references are stored both as absolute paths and as `{libraryRoot, relativePath,
  contentHash}` so a moved preset folder can be relinked.

### 4.9 Window model: video first, controls in a drawer

v1's editor is a control strip with the visual underneath, and the *video* is what pops out.
v2 inverts this. In both shells the primary window **is** the visualization, and the controls
live in a bottom drawer that appears over it, the way modern video players work.

```
┌──────────────────────────────────────────────┐
│                                              │
│              visualization                   │
│                                              │
│                                              │
├──────────────────────────────────────────────┤  ← drawer (hover / tap / pinned)
│ [preset ▾] ◀ ▶  🔒 🔀  [mode ▾]  ♩128  ⛶ ⚙ 📌│     over a translucent scrim
└──────────────────────────────────────────────┘
```

Concepts:

- **Primary window.** The plugin editor, or the app's main window. Always shows the visual.
- **Output window.** A window we own, opened from the drawer's ⛶ button, that can go
  borderless-fullscreen on a chosen display. It is just another `OutputSurface` (§4.5): the
  engine renders once to the FBO and presents to both. The primary window keeps showing the
  live mirror and stays the control surface. This is the OBS workflow: output on the capture
  display, controls in the DAW on the other display, with no "the video left" moment.
  Reason it is a separate window: a plugin editor is framed by the host and can never
  fullscreen itself. In the app the main window can go fullscreen directly, and the Output
  window is for the second display.
- **Detached controls (secondary).** The drawer component hosted in its own floating window,
  for the projector-plus-laptop setup. Same component, so it is cheap; not a design driver.

Drawer behaviour:

| Rule | Why |
|---|---|
| Three states: hidden, revealed, **pinned** | Automating a knob while the drawer vanishes under the cursor is miserable. Pinned is the default in the plugin editor; auto-hide is the default on the Output window and in app fullscreen. |
| Reveal on hover **or** tap; auto-hide after ~3 s of no pointer activity when not pinned | Touch has no hover, and some hosts swallow mouse-move events. |
| Translucent dark scrim behind the controls, optional blur | Knobs over a moving psychedelic field are unreadable. |
| First-run reveal: drawer starts open until the first interaction | Otherwise new users think the plugin has no controls. |
| Drawer row holds only essentials: preset combo, prev/next, lock, shuffle, transition mode, BPM/sync badge, output, settings, pin | Everything else (transition tuning, quality, playlist tools, diagnostics) lives in popovers or the settings panel. |
| Keyboard shortcuts work in every window we show, and every action is also pointer-reachable | v1's F11 and Esc worked in the plugin editor and in the external window across the hosts tested, so shortcuts are a first-class feature in both shells. Windows we own (Output, detached controls) receive keys unconditionally. The host-framed editor receives them in most hosts, but a few intercept some keys, so the pointer path is the guarantee and the DAW checklist records per-host behaviour. |
| Minimum primary-window size is small (e.g. 480×270); the drawer collapses to icons | Video-first layouts shrink gracefully; control-first ones do not. |

Keyboard shortcuts, one table for both shells (`milkdawp_ui/Shortcuts`), attached to the
primary window, the Output window, and the detached-controls window alike:

| Key | Action | Note |
|---|---|---|
| `F11` | Toggle fullscreen: Output window if open or focused, else the app main window; in the plugin editor it opens the Output window fullscreen | carried over from v1 |
| `Esc` | Exit fullscreen; if not fullscreen, reveal the drawer | carried over from v1 |
| `←` / `→` | Previous / next preset | |
| `L` | Toggle lock | |
| `S` | Toggle shuffle | |
| `H` | Toggle drawer (reveal or hide, respects pin) | |
| `P` | Pin / unpin drawer | |
| `B` | Browse presets (the preset picker; arrows + Return choose) | 5.8 |
| `M` | Open the settings menu (arrows + Return choose) | 5.8 |
| `D` | Show / hide the diagnostics panel | 5.8, 5.9 |
| `Tab` / `Shift+Tab` | Inside an open popover (Transitions, Output, Diagnostics): move between its controls; `1`-`9` / `A` in Output pick a screen / Automatic | 5.8 |
| `Space` | Toggle lock | **app only**: every DAW binds Space to transport, so the plugin never claims it |

Rules: unmodified letters only when the visualization or drawer has focus, never while a text
field is active; the plugin never consumes keys it does not handle (`keyPressed` returns false)
so the host still sees them; shortcuts are shown in tooltips. Plugin build keeps v1's
`EDITOR_WANTS_KEYBOARD_FOCUS FALSE`, which received keys fine in practice; flipping it to `TRUE`
is a per-host experiment in Phase 3 only if a target host drops keys.

Shared implementation: `milkdawp_ui` provides `ControlDrawer`, `DrawerScrim`, `OutputWindow`,
and `Shortcuts`, and both shells compose them identically.

### 4.10 Development environment

Cross-platform plugin development cannot be fully containerized: macOS binaries need Apple's
toolchain on macOS, DAW-grade Windows binaries need MSVC, and containers have no GPU or audio
devices. What *can* be containerized is everything that does not need those: the JUCE-free
core, `mdw-analyze`, headless render tests under Mesa, lint, and docs. That is where most
iteration happens, so it gets first-class treatment. Three supported ways to work:

1. **Container (default for Linux work and for agents).** One image, defined in
   `.devcontainer/Dockerfile` (Ubuntu 24.04, GCC + Clang, CMake, Ninja, vcpkg with projectM
   and the other native dependencies **pre-built**, the pinned JUCE 9 source pre-fetched, EGL +
   Mesa llvmpipe + Xvfb, pluginval), published to GitHub Container Registry by CI. The same image is used by CI jobs, by VS Code / CLion devcontainers, and by
   Claude Code web sessions via a session-start hook. Identical environment everywhere, and the
   10–30 minute vcpkg build happens once when the image is published rather than per checkout.
   On a Linux host, `/dev/dri` and the display socket can be passed through so `mdw-view` runs
   with a real GPU inside the container.
2. **CI as the Windows and macOS build farm.** Every push produces VST3/AU/app artifacts and
   pluginval reports for all three platforms. "Push, wait fifteen minutes, download the
   bundle" is a legitimate day-to-day loop for a project of this size.
3. **Native, when you want it.** Idempotent bootstrap scripts install the minimum
   (`winget` for VS Build Tools + CMake + Ninja on Windows; Xcode command line tools + Homebrew
   on macOS), vcpkg stays repo-local as in v1, and `cmake --preset dev-<os>` does the rest. A
   `bootstrap --doctor` mode reports what is missing. Toolchain minimums are pinned in one
   file (`toolchain.json`) read by the scripts and CI.

Nix could give pinned toolchains on macOS and Linux without containers, but it does not cover
Windows and has a steep learning curve; not adopted.

### 4.11 JUCE 9

JUCE 9.0.0 shipped on 21 July 2026, with 9.0.2 current as of this writing (7 September 2026).
v2 targets **JUCE 9.x** from the start rather than porting later. What it changes for us:

| JUCE 9 change | Effect on MilkDAWp 2 |
|---|---|
| Linux OpenGL contexts now use **EGL** instead of GLX; `libegl-dev` and `libxi-dev` are new build dependencies | Aligns with the headless render plan (2.7): EGL surfaceless contexts on CI and in the devcontainer. Both packages go in the Dockerfile and bootstrap. |
| "Improved the CMake build system for headless environments" | Fewer workarounds for building `milkdawp_engine` tests without a display. |
| New SVG parser (lunasvg): radial gradients, clip paths, dashed strokes, referenced elements | Better icon rendering. `Drawable::createFromSVG(const XmlElement&)` is **removed**; v1's icon loader used it, so the port uses `createFromSVGString` / `createFromSVGFile`. |
| `Drawable` no longer inherits from `Component`; new `DrawableComponent` wrapper | Any drawable placed in a layout goes through `DrawableComponent`. `DrawableButton` still takes drawables. |
| Native Direct2D GPU UI rendering on Windows | Faster drawer and popovers. **Spike item:** confirm the JUCE-painted drawer composites correctly over the GL surface on Windows (child of the GL component, painted via JUCE's GL renderer) rather than as an overlapping sibling peer. Folded into 2.3. |
| Redesigned macOS CoreAudio implementation using aggregate devices, lower latency, better drift compensation | Directly improves the standalone app's audio input path (4.2). |
| Multi-touch improved on Linux; **off by default on Windows** | Drawer tap-reveal needs `usesWindowsMultiTouch()` returning true in the plugin editor and `setUsingWindowsMultiTouch(true)` in the app. |
| Variable fonts | Optional; one weight axis for the drawer typography if it earns its place. |
| Bundled zlib/libpng/libflac now compiled as C, not wrapped in C++ namespaces | ODR/link-conflict risk **if** vcpkg's zlib/libpng were also linked into the same binary. **Resolved 2026-09-26, the other way than Phase 0 originally picked:** the vcpkg-copies path (`JUCE_INCLUDE_ZLIB_CODE=0`/`JUCE_INCLUDE_PNGLIB_CODE=0` + external `ZLIB::ZLIB`/`PNG::PNG`) was live for weeks and passed pluginval and the Standalone build every single time, but silently broke loading in real, independent VST3 hosts (REAPER, Cubase both rejected it with no exception/crash/hang -- see the Phase 2.1 note below for the full investigation). Root cause: the "ODR risk" this row describes never actually existed in practice -- projectM is loaded entirely at runtime (`ProjectMLibrary`, 2.1) and was never linked at build time despite `engine/CMakeLists.txt` doing so anyway (dead code, now removed), and Windows uses DirectWrite rather than FreeType, so nothing ever pulled a second zlib/libpng copy in. Reverted to JUCE's own bundled copies (the CI link-step check this row already called for -- `cmake/scripts/check_single_zlib_libpng.cmake` -- stays in place and now reports `zlib=0, libpng=0`, i.e. no separate copy at all, on every build). `MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG` remains available as a manual opt-in if a future dependency genuinely needs to link its own zlib/libpng at build time. |
| `OpenGLContext::setImageCacheSize` now takes bytes | Irrelevant unless we set it; noted so nobody copies a JUCE 8 value. |
| Minimums: C++17, CMake 3.22, VS 2019, Xcode 12.4, GCC 7 / Clang 6; deploy to macOS 10.11+, Windows 1607+ | All well below our D5/D9 floors. |
| Plugin formats: `Standalone Unity VST3 AU AUv3 AAX VST LV2` | LV2 stays available for post-1.0; CLAP still needs `clap-juce-extensions`. |

How we get it: **the vcpkg `juce` port is still at 8.0.7**, so JUCE 9 cannot come from vcpkg
without maintaining an overlay port. JUCE upstream is designed for `add_subdirectory`, so v2
vendors JUCE via CMake `FetchContent` pinned to a release tag **and** commit hash, with the
source cached in the devcontainer image and in CI. vcpkg keeps supplying projectM and every
other native dependency. projectM comes from a repo-local **overlay port** pinned to an
upstream 4.2 commit hash, because the registry port (4.1.7) cannot render to an FBO
(ADR-0008). Upgrades of JUCE happen by bumping the tag
on a branch and running the full matrix, same as a vcpkg baseline bump.

Licensing: JUCE 9 remains dual-licensed, AGPLv3 or the commercial JUCE 9 EULA. MilkDAWp is
AGPL-3.0-or-later (D10), so the AGPL path applies. JUCE's README asks that AI tools generating
JUCE code tell their users a commercial licence may be required; it would be if the project ever
moved off AGPL.

---

## 5. Key decisions

Decisions marked **Recommended** are the plan of record unless overruled; **Open** items need a
call from Matthew before the phase that depends on them.

| # | Decision | Status | Recommendation and rationale |
|---|---|---|---|
| D1 | Plugin identity | Decided | v2 **is** the next MilkDAWp. Keep v1's manufacturer code `OMda`, plugin code `Mlkw`, bundle ID `com.otitismedia.MilkDAWp`, and product name `MilkDAWp`; the first release ships as MilkDAWp 1.0 and existing sessions keep loading with state migrated (§4.8). The v1 repository is archived with a pointer once 1.0 ships (6.9). Consequences: during development, never install v1 and v2 into the same plugin folder at once (same IDs, hosts will pick one arbitrarily); dev and beta builds carry a visible pre-release version string and the state schema is versioned from the first commit so beta sessions migrate forward. |
| D2 | Plugin formats | Recommended | VST3 + AU + Standalone wrapper for 1.0. CLAP via `clap-juce-extensions` and LV2 post-1.0. No AAX. |
| D3 | Renderer location | Recommended | In-process engine thread for 1.0, IPC-ready boundary (§4.6). |
| D4 | Dependency management | Decided | **JUCE 9.x** via CMake `FetchContent` pinned to a release tag and commit hash (the vcpkg port lags at 8.0.7). Everything else, projectM 4.x included, via vcpkg manifest mode with pinned baseline and custom dynamic triplets (LGPL). See §4.11. |
| D5 | Language and toolchain | Recommended | C++20. MSVC 2022, Apple Clang 15+, GCC 12+ / Clang 16+. Warnings as errors on our targets. `clang-format` + `clang-tidy` config committed. |
| D6 | Core test framework | Recommended | Catch2 v3 for `milkdawp_core` and engine tests (JUCE-free core cannot use `juce::UnitTest`). `pluginval` for plugin binaries. |
| D7 | Playlist implementation | Recommended | Own `Playlist` in core rather than `libprojectM-4-playlist`, because we need ratings, tags, history windows, and sample-accurate scheduling that the projectM playlist does not offer. |
| D8 | Standalone shell | Recommended | Phase 3 uses JUCE's `Standalone` plugin format to get an app early. Phase 4 replaces it with a real `juce_add_gui_app` shell sharing `milkdawp_ui`. |
| D9 | Minimum OS | Recommended | Windows 10 21H2+, macOS 12+ (universal x86_64 + arm64), Ubuntu 22.04+ / glibc 2.35+. Loopback on macOS needs 13+/14.2+ and is feature-gated at runtime. |
| D10 | Licensing | Recommended | Project stays AGPL-3.0-or-later (JUCE 9 AGPLv3 path), projectM LGPL-2.1 dynamically linked, notices shipped in installers. Moving off AGPL would require the commercial JUCE 9 licence. |
| D11 | Signing | Decided (2026-10-04) | **Free options only**: this is a hobby project with no budget for certificates or developer accounts. **Windows:** apply to the [SignPath Foundation](https://signpath.org/) free open-source programme once a release exists (their terms require an already-published release and a fully automated build from this repo; SignPath Foundation is then named as publisher on the certificate). Before applying, ask whether JUCE's AGPL/commercial dual licence conflicts with their "no commercial dual-licensing" rule (we use JUCE only under the AGPL). Until accepted, or if refused, Windows ships unsigned. **macOS:** ad-hoc signed in CI (`codesign -s -`, which Apple Silicon requires before it will load the code), not notarized; the docs give first-run steps (System Settings → Privacy & Security → "Open Anyway", or `xattr -dr com.apple.quarantine`). **Linux:** nothing needed. **All platforms:** SHA-256 checksums in every release, plus GitHub artifact attestations (`actions/attest-build-provenance`, free) so a download can be checked against the CI run that built it. Revisit if a free notarization route ever appears. |
| D12 | Bundled presets | Decided (2026-10-04) | Bundle projectM's **Cream of the Crop** pack (~9,800 presets, curated by Jason Fletcher / ISOSCELES; [presets-cream-of-the-crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop)) together with the projectM **texture pack** its presets reference, the same content projectM's own Steam release ships. Licence position (from the pack's LICENSE.md): MilkDrop presets carry no formal licence; authors keep copyright, but freely released presets are treated as public domain after decades of redistribution, and authors may ask for removal. We ship the pack's LICENSE.md and attribution unchanged, credit the curator in About and the docs, and honour removal requests (follow upstream removals; a takedown note in the docs). Consequences: installer size is measured before choosing full pack vs a subset with an in-app "download the full pack"; the first-run scan, Weighted shuffle and preset metadata must stay fast at ~10k entries; the click-the-name popup menu can't handle ~10k presets, so the searchable preset browser moves from the post-1.0 backlog into Phase 6 (6.1b). |
| D13 | Window model | Decided | Video-first primary window with a hover/tap/pinned control drawer; a separately owned Output window for fullscreen on another display; detached-controls window as a secondary feature (§4.9). Replaces v1's control-strip-plus-pop-out-video layout. |
| D14 | Development environment | Decided | Single container image (devcontainer + CI + agent sessions) covering core, CLI, headless render, and lint; CI as the Windows/macOS build farm; idempotent native bootstrap with a doctor mode for those who want local builds (§4.10). No Nix. |
| D15 | projectM version | Recommended | Minimum projectM **4.2**, built from a pinned upstream `master` commit through a vcpkg overlay port until 4.2.0 is tagged. 4.1.7 draws its final image to framebuffer 0 whatever FBO is bound, which breaks §4.5. It also uses GLEW without initializing it, and times presets only by the wall clock. 4.2 adds `render_frame_fbo`, a GL-loader create call (no GLEW) and `set_frame_time`. Amends D4. See ADR-0008. |
| D16 | Android | Recommended | Android standalone app is a planned **post-1.0** target (Phase 7): Android 10+ (API 29), OpenGL ES 3.0, arm64-v8a. A Gradle project builds our CMake tree through the NDK (JUCE's CMake API has no Android support). Surfaces use CPU readback (JUCE 9 cannot share an Android GL context). Audio is microphone first, `AudioPlaybackCapture` second. Presets are imported into app storage. Phase 4 follows ADR-0010's shell boundary rules so none of this needs rework. See ADR-0010. |

---

## 6. Repository layout

```
MilkDAWp2/
├── CMakeLists.txt                 # top-level: options, vcpkg, subdirectories
├── CMakePresets.json              # dev-{win,mac,linux}, ci-*, release-*
├── vcpkg.json / vcpkg-configuration.json   # projectM and other native deps (not JUCE)
├── toolchain.json                 # pinned minimum tool versions, read by bootstrap + CI
├── triplets/                      # x64-windows-dynamic, arm64-osx-dynamic, ...
├── .devcontainer/                 # Dockerfile + devcontainer.json (the one image, §4.10)
├── .claude/                       # session-start hook for Claude Code web sessions
├── cmake/                         # JUCE 9 FetchContent pin, deps copy, sanitizers, warnings
├── core/                          # milkdawp_core  (no JUCE)
│   ├── include/milkdawp/core/...
│   ├── src/...
│   └── tests/                     # Catch2, fixture-driven
├── engine/                        # milkdawp_engine (JUCE + GL + projectM)
│   ├── include/milkdawp/engine/...
│   ├── src/...
│   └── tests/                     # headless GL smoke tests (Mesa on CI)
├── ui/                            # milkdawp_ui: ControlDrawer, scrim, OutputWindow, LAF
├── plugin/                        # milkdawp_plugin: processor, editor, state migration
├── app/                           # milkdawp_app: standalone shell, capture modules
├── android/                       # Phase 7: Gradle project wrapping the CMake tree (ADR-0010)
├── tools/
│   └── mdw-analyze/               # offline CLI: WAV in → onsets/beats/transitions out
├── resources/                     # icons, logo, bundled presets (git-lfs or fetched)
├── fixtures/                      # short audio clips + annotations for core tests
├── packaging/                     # Inno Setup / WiX, pkgbuild, AppImage recipes
├── docs/                          # ADRs, user guide, capture how-tos
├── scripts/                       # bootstrap.sh / bootstrap.ps1 (--doctor), CI helpers
└── .github/workflows/             # devcontainer-image.yml, ci.yml, pluginval.yml, release.yml
```

---

## 7. Phased plan

Sizing: **S** ≈ one focused session, **M** ≈ 2–4, **L** ≈ 5–10, **XL** must be split before
starting. Sizes are estimates for planning credits, not commitments. Each phase ends with a
short "what to test by hand" list for Matthew; agents should append to it as they go.

### Phase 0 — Foundation and guardrails

**Goal:** a repo that builds on all three platforms in CI with the target structure, before any
feature code, and a one-command developer environment. **Exit:** `cmake --preset ci-linux &&
ctest` green on Linux, macOS, Windows; sanitizer job green; empty `milkdawp_core` and
`milkdawp_engine` targets link; opening the repo in a devcontainer gives a working build in
under two minutes; a fresh Claude Code web session can build and run the core tests.

- [x] 0.1 (S) Top-level CMake with options, warnings-as-errors module, C++20, presets for
      dev/ci/release on each platform. JUCE 9.x via `FetchContent` pinned to tag + hash
      (§4.11). Port vcpkg manifest, baseline, and triplets from v1 for projectM and the rest;
      bump the baseline to the latest projectM 4.x; drop the `juce` entry from `vcpkg.json`.
      Decide and enforce the zlib/libpng single-copy rule from §4.11 with a link-time check.
      Note: JUCE 9.0.2 (commit `7278278`) and projectM 4.1.7 pinned; macOS floor raised to
      12.0 to match D9 (v1 used 11.0). The single-copy check
      (`milkdawp_check_single_zlib_libpng` in `cmake/SingleZlibLibpngCheck.cmake`) is
      implemented but unwired — there's no linked binary to check until 0.2+ adds real
      targets. Verified by configuring with the VS 2022 generator (JUCE fetch + `juceaide`
      build succeed); the vcpkg/projectM path is untested here since this machine has no
      `VCPKG_ROOT` — needs a real check on CI or a dev box with vcpkg installed.
      Update (2026-09-27): the vcpkg/projectM path is now verified in CI on all three
      platforms (run `36320629909`, commit `3d5dcc9`): projectM 4.2.0 from the overlay port
      (2.12) as `x64-windows-dynamic`, `arm64-osx-dynamic`, and `x64-linux-dynamic` (the last
      pre-built in the devcontainer image). The single-copy check is wired and passes on every
      final binary on all three. It had a Linux false positive, fixed on the way: `ldd` prints
      `libz.so.1 => /lib/.../libz.so.1`, and the regex counted the name and the path as two
      copies; it now counts only resolved paths.
- [x] 0.2 (S) Skeleton targets: `milkdawp_core` (static lib), `milkdawp_engine`,
      `milkdawp_ui`, `milkdawp_plugin`, `milkdawp_app`, `mdw-analyze`, Catch2 test runner.
      Note: building all six targets plus Catch2 tests verified locally (MSVC/Ninja, VS 2022
      generator, `MILKDAWP_WITH_PROJECTM=OFF`) — VST3 bundle, app exe, and CLI all produced,
      2/2 core tests pass. Surfaced and fixed a real bug in 0.1's zlib/libpng decision: it was
      unconditionally disabling JUCE's bundled zlib/libpng, which fails any build without
      vcpkg's copies actually linked in (e.g. this skeleton, with no `VCPKG_ROOT` available).
      Now gated on `MILKDAWP_WITH_PROJECTM` and wired to link vcpkg's `ZLIB`/`PNG` targets when
      on; still unverified with projectM actually present (needs vcpkg on a real box or CI).
      Update (2026-09-27): verified with projectM present on Windows, macOS and Linux in CI
      (see 0.1 and 0.3).
      `milkdawp_app` stays off by default (`MILKDAWP_BUILD_APP=OFF`) per D8/Phase 4.
- [~] 0.3 (S) CI matrix (Linux, macOS, Windows): configure, build all targets, run core tests.
      vcpkg binary caching via GitHub cache to keep runs under ~15 minutes after warm-up.
      Note: `.github/workflows/ci.yml` written (macOS/Windows native + Linux-in-container jobs,
      x-gha vcpkg binary caching). YAML syntax checked with `js-yaml`; **not** run — needs an
      actual push/PR to verify (a shared-state action I didn't take without asking).
      Update (2026-09-27): **green on all three platforms** (run `36320629909`, commit
      `3d5dcc9`): `ctest` 177/177 on `windows-latest` (MSVC), `macos-latest` (Apple Clang,
      arm64) and Linux (GCC 13, in the devcontainer image), plus the `mdw-analyze` metric gate
      (1.10) and pluginval (3.9). The repo is public, so Actions minutes (macOS included) are
      free. Every push before this had failed in 0 s without running a job: `container:
      ${{ env.* }}` isn't allowed (the `env` context isn't available there), so the whole file
      failed to parse; and GHCR rejects the uppercase `Blue-Kachina` owner name. The image name
      is now a lowercase literal, with GHCR credentials on the container jobs. Getting to green
      also surfaced the first non-MSVC compile of the codebase: Clang's
      `-Wunused-private-field` (5 fields) and `-Wunused-function`/`-Wunused-const-variable`
      (platform-only GL helpers, `generate-fixtures`' `kPi`), GCC's `-Wclass-memaccess`
      (`SeqlockSnapshot`), nested `Config` structs with default member initializers used as
      `= {}` default arguments inside their own class (CWG 1397; MSVC accepts it, GCC/Clang
      don't; now `RenderEngineConfig`/`DrawerStateMachineConfig` at namespace scope with
      `Config` aliases), `JUCE_WEB_BROWSER=0`/`JUCE_USE_CURL=0` missing on `milkdawp_engine`
      and `milkdawp_ui_tests` (GTK/libcurl on Linux), a GCC `-Wmaybe-uninitialized` false
      positive in JUCE's bundled HarfBuzz (demoted to a warning, GCC only), and
      `[[clang::nonblocking]]` placement (see 3.1). **Stays `[~]`:** caching doesn't work.
      vcpkg has removed the `x-gha` backend (the log says so), so both native jobs rebuild
      projectM and its dependencies from source on every run, and a run takes about 22-25
      minutes, not ~15. Needs a different provider (`files` + `actions/cache`, or NuGet on
      GitHub Packages).
      Update (2026-09-27, run `36323254149`, commit `c4e70a2`): vcpkg caching fixed. The
      native jobs use the `files` provider, restored and saved with `actions/cache` (key: vcpkg
      commit + manifest + triplets + overlay ports; saved right after Configure, even if a
      later step fails). The first run missed as expected, built projectM (1.1 min macOS,
      1.4 min Windows) and saved. It also turned out the Linux jobs had been rebuilding
      everything too: the workflow-level `VCPKG_BINARY_SOURCES: clear;...` wiped the
      image's pre-built cache. It's now scoped to the native job, and all four Linux jobs log
      `Restored 10 package(s) from /opt/vcpkg-binary-cache` in about 140 ms. **Still over
      ~15 minutes**, though, because vcpkg was never the main cost: the Build step is 18 min
      on Windows, 15-16 min on Linux/ASan, 11-12 min on TSan/macOS. JUCE's module sources
      compile once per target that links JUCE (7 times on Linux). Next step, in progress:
      compiler caching in CI (`hendrikmuhs/ccache-action`: ccache on Linux/macOS, sccache on
      Windows with `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`, since `/Zi`'s shared
      `.pdb` isn't cacheable; launchers passed on the CI configure line only, so local
      builds are unchanged). If warm runs are still slow, the bigger fix is compiling JUCE's
      modules once into a shared static library instead of into every target.
      Update (2026-09-27, run `36326021776`, commit `a69496d`): first warm compiler-cache
      run, **8.5 min wall clock** (was 18 and 23.5). Build step: macOS 19 s, Windows 1.3 min,
      TSan 1.2 min, RTSan 1.5 min, ASan 3.5 min (98% ccache hits), Linux 6.7 min (20% hits).
      The Linux job's ccache key `ci-linux` is a prefix of `ci-linux-asan` etc., and the
      action restores by prefix, so it had loaded the ASan cache; renamed to `ci-linux-gcc`.
      Windows also rebuilt projectM once (1.9 min in Configure): the new `.gitattributes`
      checks the triplet and overlay files out with LF instead of the runner's CRLF, which
      changes both the vcpkg ABI hashes and the cache key, so the fallback cache didn't
      match. The new key was saved, so later runs restore it. Stays `[~]` until a run with
      the renamed Linux key confirms all jobs are warm.
- [x] 0.4 (S) Sanitizer job on Linux: ASan + UBSan for core/engine tests, TSan for queue and
      ring tests. Clang RealtimeSanitizer (`-fsanitize=realtime`) job for functions marked
      `[[clang::nonblocking]]` (the audio callback path).
      Note: `cmake/Sanitizers.cmake` + `ci-linux-{asan,tsan,rtsan}` presets added; `sanitize` job
      in ci.yml. No TSan-worthy code exists yet (AudioRing/Messages land in Phase 1.1/1.2); no
      `[[clang::nonblocking]]` function exists yet either (Phase 3.1). Unverified — no Clang in
      this sandbox and nothing to sanitize yet regardless.
      Update (2026-09-27): all three jobs run and are green in CI (run `36320629909`), 169/169
      tests each (core, engine, ui, plugin-free build). The image needed two fixes first:
      `-fsanitize=realtime` needs Clang 20, but Ubuntu 24.04 stops at 18, so LLVM now comes
      from apt.llvm.org (`LLVM_VERSION=20`); and the sanitizer runtimes
      (`libclang_rt.asan*.a`, `.tsan*.a`) were never installed, so ASan/TSan could not have
      linked on the old image either (`libclang-rt-20-dev` now). `llvm-symbolizer` is on
      PATH too, so reports name functions instead of printing `<null>` frames. TSan found a
      real race on its first run (see 1.1). **Gap:** the RTSan preset builds with
      `MILKDAWP_BUILD_PLUGIN=OFF`, and nothing else is marked `[[clang::nonblocking]]`, so
      the job currently checks nothing; see 3.1. Running sanitizer binaries locally in Docker
      needs `--security-opt seccomp=unconfined` (TSan re-execs itself with ASLR off via
      `personality()`, which the default seccomp profile blocks).
- [x] 0.5 (S) `clang-format`, `clang-tidy`, `.editorconfig`, pre-commit hook script,
      `CONTRIBUTING.md` with the threading rules from §4.2.
- [x] 0.6 (S) ADR directory with ADR-0001..0006 recording D1–D12 as decided so far.
      Note: grouped as 6 ADRs covering D1-D10 + D13-D14 (12 decided/recommended items); D11/D12
      excluded since they're still Open, not decided.
- [x] 0.7 (S) `LICENSES/`, `THIRD_PARTY_NOTICES.md`, SPDX headers template.
      Note: `LICENSES/AGPL-3.0-or-later.txt` (+ top-level `LICENSE`) fetched from SPDX's
      license-list-data; retrofitted the SPDX header onto every source file written so far.
- [x] 0.8 (S) Fixture policy: short (≤10 s) audio clips with permissive licences or synthesized,
      plus `fixtures/README.md` on annotation format (`beats.txt`: one beat time per line).
      Note: policy doc only — no actual clips yet (that's Phase 1.9).
- [x] 0.9 (M) Devcontainer image: `.devcontainer/Dockerfile` with GCC + Clang, CMake, Ninja,
      vcpkg and the manifest dependencies pre-built for `x64-linux-dynamic`, the pinned JUCE 9
      source pre-fetched, JUCE 9's Linux dependencies (`libegl-dev`, `libxi-dev`, plus the
      usual X11/freetype/ALSA set), Mesa llvmpipe with EGL, Xvfb, pluginval, clang-format/tidy. `devcontainer.json` with recommended extensions and
      the CMake preset pre-selected. Optional GPU/display passthrough documented.
      Note: verified for real by Matthew on 2026-09-19 — configured and built inside the
      devcontainer, `ctest` reports 90/90 real tests passing (0 failures), and
      `mdw-analyze --suite fixtures/ --thresholds fixtures/thresholds.json` passes all 6
      fixtures from that build. This was previously blocked in-sandbox (no reachable Docker
      daemon); now confirmed working end to end on a real machine.
      Update 2026-09-19: added the Claude Code CLI (Node 22 + `npm install -g @anthropic-ai/
      claude-code`) to the Dockerfile, and a named volume mount (`/root/.claude`) in
      `devcontainer.json` so its login survives container rebuilds. This is distinct from the
      `.claude/hooks/session-start.sh` hook (0.11) -- that hook runs *from inside* an
      already-running Claude Code session and warms up the C++ build; this is what makes `claude`
      exist as a command at all when connecting to the devcontainer directly (VS Code Dev
      Containers / CLion Gateway) to run or resume an interactive session. Not yet rebuilt/tested
      against a real container on this pass (needs a real Docker daemon, same limitation 0.9
      itself had before Matthew's machine).
- [x] 0.10 (S) `devcontainer-image.yml`: builds and publishes the image to GHCR on changes to
      the Dockerfile, `vcpkg.json`, `vcpkg-configuration.json`, or the JUCE pin in `cmake/`; CI jobs from 0.3 run inside
      it (`container:`) so CI and local containers are identical.
      Note: workflow written; ci.yml's Linux jobs run `container: ghcr.io/.../milkdawp2-devcontainer:latest`.
      **Bootstrapping gotcha:** on a brand-new repo this image doesn't exist yet, so ci.yml's
      Linux/sanitize jobs will fail until someone runs this workflow once via `workflow_dispatch`
      (or pushes a Dockerfile change to `main`). Not run — needs a push/dispatch to verify.
      Update (2026-09-27): runs and publishes `ghcr.io/blue-kachina/milkdawp2-devcontainer`
      (`:latest` and `:<sha>`) in 6-9 minutes (so far) with the GHA layer cache; all four Linux CI
      jobs run inside it. The package is public, so `docker pull` works without a login.
      Its first runs failed because GHCR names must be lowercase (now a literal). **Known
      race:** a push that changes the Dockerfile starts this workflow and CI together, and
      CI pulls the *previous* `:latest`, so toolchain changes only reach CI on the following
      run (re-run the failed jobs once the image finishes). The image now has LLVM 20 (see
      0.4).
- [ ] 0.11 (S) Claude Code web session-start hook (`.claude/`): pulls or reuses the image
      contents, configures the Linux preset, warms the build so agents can run tests
      immediately. Verified by opening a fresh session and running `ctest`.
      Note: `.claude/settings.json` + `.claude/hooks/session-start.sh` written (shell syntax
      checked); it no-ops outside the devcontainer image. Not verified end-to-end — that needs
      an actual fresh Claude Code web session on top of the (also unverified) published image.
- [x] 0.12 (M) Native bootstrap: `scripts/bootstrap.ps1` (winget: VS Build Tools, CMake,
      Ninja) and `scripts/bootstrap.sh` (Xcode CLT check, Homebrew: cmake, ninja), both
      idempotent, both reading `toolchain.json`, both with `--doctor` that prints found vs
      required versions and exits non-zero on gaps.
      Note: `-Doctor` mode run for real on this Windows box — correctly found cmake 4.0.3 and
      VS 2022 BuildTools, correctly flagged ninja missing (present under VS but not on PATH),
      exit code 1. `bootstrap.sh` only shell-syntax-checked (`sh -n`); its macOS-specific parts
      (`xcode-select`, `pkgutil`) are unverified on this box.
- [x] 0.13 (S) `CONTRIBUTING.md` "three ways to develop" section (§4.10) with the exact
      commands for each, and a note on which phases need native builds.

Hand test: open the repo in VS Code with the Dev Containers extension and confirm the build and
tests run without installing anything else on the host.

### Phase 1 — Core analysis and scheduling (JUCE-free)

**Goal:** beat detection and transition logic that can be iterated on offline. **Exit:**
`mdw-analyze` reports beat F-measure and tempo error against every fixture; scheduler
simulations are deterministic and pass; CI runs the metric suite and fails on regression.

- [x] 1.1 (S) `AudioRing`: lock-free SPSC ring for interleaved float PCM with sample-position
      cursors; `copyLatest(n)` and `consumeHop(n)` APIs. TSan-tested.
      Note: 8 tests incl. a real concurrent writer/reader thread test; caught and fixed a livelock
      in the test itself (not the ring) where the reader could spin forever after the ring
      dropped frames it fell behind on. TSan not run for real (no Clang in this sandbox) — the
      `ci-linux-tsan` CI job (0.4) is unverified end-to-end.
      **Bug found and fixed (2026-09-27):** the first real TSan run reported a data race in
      the concurrent writer/reader test. By design, `write()` overwrites the oldest frames
      when a reader falls behind, but `copyFrames()` checked a frame was available and then
      `memcpy`'d it, so `write()` could wrap round and overwrite the slot during the copy:
      undefined behaviour, and a frame of newer audio returned under an older position. The
      header's "already overwritten frames come back as zero" was never enforced. Fixed with
      the same seqlock idea as `SeqlockSnapshot` (3.1): the buffer is relaxed
      `std::atomic<float>` (plain loads/stores on x86/ARM); `write()` publishes
      `writeReserve_` (how far it may overwrite) behind a release fence before touching the
      buffer; `copyFrames()` re-reads it after an acquire fence and zeroes any frame that could
      have been overwritten mid-copy. `write()` stays wait-free. This covers `consumeHop()`,
      `copyLatest()` and `copyRange()` (the render thread's `PcmFeeder`). Verified in the CI
      image with TSan: the original code fails the `[AudioRing]` tests 50/50 runs, the fix
      0/50, full core suite 100/100; CI's `ci-linux-tsan` job is green since.
- [x] 1.2 (S) `Messages.h`: POD message types (parameter change, transition request, preset
      load result, status snapshot) and the SPSC/MPSC queue templates. Static-asserted trivially
      copyable.
      Note: SPSC only (see the header comment) — nothing in the current design needs true MPSC.
- [x] 1.3 (M) `Analyzer`: resampler to internal rate, STFT (own radix-2 FFT or a header-only
      dependency such as pffft via vcpkg), band energies with smoothing, spectral-flux ODF
      (broadband + bass).
      Note: own radix-2 FFT (`core/Fft.h`), verified against a DC signal, a pure sine, and
      Parseval's theorem. Linear-interpolation resampler, not windowed-sinc — a documented
      simplification, swappable later without touching Analyzer's interface.
- [x] 1.4 (M) `OnsetDetector`: adaptive threshold, peak picking, min inter-onset interval.
      Unit tests on synthetic clicks, sweeps, silence, noise.
      Note: causal (trailing-window + 1-hop-latency peak confirmation), not the centered ±0.5s
      window read literally — a live analysis thread can't get lookahead latency back; documented
      in the header as a deliberate interpretation.
- [x] 1.5 (M) `TempoTracker`: autocorrelation/comb tempo estimate with octave weighting and
      confidence; hysteresis so BPM does not flicker.
      Note: locks onto synthetic 100/120/128 BPM click trains within a few BPM and stays stable
      hop-to-hop (tested). Real-world accuracy against actual music is unverified without more
      varied fixtures.
- [x] 1.6 (M) `BeatClock` phase tracker: predicts next beat and bar, corrects on onsets,
      exposes confidence and `samplesUntilNextBeat()`. Downbeat heuristic for 4/4.
      Note: found and fixed a real bug via the fixture suite (see 1.9/1.10) — the phase-correction
      window was a hard gate that, combined with an un-wrapped onset/prediction delta, could
      permanently lock onto the wrong phase (0.0 F-measure on the easy four-on-the-floor case).
      Fixed by wrapping the delta to the nearest equivalent phase and widening the default
      correction window to match; F-measure went 0.0 -> 0.69 on that fixture as a result.
- [x] 1.7 (S) `HostTransport` adapter: given `{bpm, ppq, timeSig, isPlaying, samplePos}`
      produce a `BeatClock` with confidence 1.0; handles stop, loop, and relocate.
      Note: stateless by design (recomputes from the host's ppq every call), so loop/relocate
      need no special-casing — there's no stale prediction to correct in the first place.
- [x] 1.8 (M) `mdw-analyze` CLI: WAV in, JSON/CSV out (onsets, beats, tempo curve, band
      energies), `--reference beats.txt` scoring (F-measure at ±70 ms, tempo error), and
      `--plot` to emit an SVG for eyeballing.
      Note: own minimal WAV reader/writer in core (PCM16/8, float32; no third-party dependency).
      All four output modes (report, `--json`, `--csv`, `--plot`) smoke-tested on a real fixture.
- [x] 1.9 (S) Fixture set: 8–12 clips covering four-on-the-floor, syncopated, tempo change,
      breakdown/drop, sparse acoustic, silence, noise. Annotate beats; commit.
      Note: 6 of the ~8-12 clips, all synthesized deterministically by `tools/generate-fixtures`
      (fixed RNG seed) rather than hand-authored: four_on_the_floor, syncopated, tempo_change,
      sparse_acoustic, silence, noise. No dedicated breakdown/drop clip yet (that's Phase 5.1's
      energy-mode territory anyway) and no real-music fixtures — those need Matthew's own
      licence-clean material per the fixtures policy, not something to fabricate.
- [x] 1.10 (S) Metric gate in CI: `mdw-analyze --suite fixtures/` must meet §4.3 thresholds;
      thresholds live in `fixtures/thresholds.json` so tuning is explicit.
      Note: `--suite` implemented and passing 6/6 locally (not yet run in actual CI). Thresholds
      are an honest Phase 1 *baseline*, not yet §4.3's 0.85 F-measure target — see
      `fixtures/thresholds.json`'s `$status` field for exact current numbers per fixture and why
      closing that gap is unfinished tuning work, not something quietly lowered to look done.
      Update 2026-09-19: re-verified by Matthew inside the devcontainer build (still 6/6), on
      top of the full `ctest` suite (90/90) — see 0.9. Still not wired as an actual CI job.
      Update (2026-09-27): runs in CI now, as a step of the Linux job
      (`mdw-analyze --suite fixtures --thresholds fixtures/thresholds.json`), 6/6 passing
      (run `36320629909`).
- [x] 1.11 (M) `Playlist`: folder scan (recursive, `.milk`), sequential / shuffle-no-repeat
      (history window) / weighted policies, lock, index mapping, stable ordering across
      rescans. Pure functions, unit-tested.
- [x] 1.12 (L) `TransitionScheduler`: modes from §4.4, cut style, blend timing, transport
      pause/reset, confidence fallback, cooldown in bars. Driven by a simulated clock in tests:
      given a synthetic `BeatClock` stream, assert requests are due exactly on downbeats.
      Note: the roadmap sizes this item at (L) — 5-10 sessions — on its own; what's landed here
      covers all 5 modes with tests (incl. BeatQuantized's confidence fallback, Hybrid's bar-snap,
      Energy's drop-detect+cooldown) but is a first pass, not the full session-count of polish an
      "L" implies. Likely rough edges: Energy's "rolling percentile" is approximated as
      mean+k*stddev over a window rather than a true percentile; multi-beat host-relocate jumps
      within a single hop use a coarser position estimate.
- [x] 1.13 (S) `ParameterModel`: the canonical list of parameters (ID, range, default,
      automatable, v1 alias) shared by plugin, app, and migration. Generated docs table.
      Note: v1's 15 parameters carried forward with identical ids/ranges/defaults (trivial 1:1
      migration) plus 3 new v2-only parameters for §4.4's transition modes
      (transitionMode/transitionBars/presetSelectionPolicy). Docs table generated by
      `tools/generate-param-docs` into `docs/parameters.md` — regenerate after any change here,
      don't hand-edit that file.
- [x] 1.14 (M) `StateSchema` v2 + `MigrateFromV1`: read v1 `MilkDAWpState`, map params,
      paths, editor size. Fixtures captured from real v1 sessions (Matthew to provide 2–3
      `.vstpreset` or host project state blobs).
      Note: `V1StateRecord`/`StateSchemaV2` are plain-data structs — the actual
      `juce::ValueTree::readFromData` parsing of v1's binary blob is JUCE-specific and belongs in
      the plugin layer (Phase 3.2), which populates `V1StateRecord` and hands it to
      `migrateFromV1()`; core stays JUCE-free per §4.1. Tested against v1 0.7.5's real parameter
      defaults (read from its actual source), not real captured session fixtures — **still need
      Matthew's 2-3 real `.vstpreset`/project blobs** to validate against an actual v1 binary
      state blob, per this item's own text. This is the one Phase 1 item I could not fully close
      without that input.
      Update (2026-09-26): v1 migration is dropped; Matthew decided v2 doesn't need to load v1
      sessions. The `StateSchemaV2` half stands. `migrateFromV1`/`V1StateRecord` were never
      wired into the plugin and are now dead code.

Hand test: run `mdw-analyze` on a couple of your own tracks and check the SVG: do the beat
markers sit on the kicks? Note any track where it drifts and add it as a fixture.

### Phase 2 — Rendering engine

**Goal:** projectM rendering owned by an engine, not a window. **Exit:** a headless test
renders three real presets to an FBO on Linux CI (Mesa llvmpipe) and checks the output is
non-black and changes between frames; a dev-only viewer app shows live rendering from a WAV
file with beat-aligned transitions.

- [x] 2.1 (M) `ProjectMLibrary`: single runtime-loading path (LoadLibrary/dlopen), typed
      function table, version check, `Unavailable{reason}`. Remove `/DELAYLOAD` reliance; keep
      rpath + bundle-relative search. Windows dependency probing (GLEW etc.) folded in.
      Note: implemented on `juce::DynamicLibrary` (engine/ is already JUCE-dependent, so this
      gets us the one-loading-strategy-per-platform requirement without hand-rolling
      LoadLibrary/dlopen ourselves) — search order is bundle-directory hint, then the current
      module's own directory, then the bare library name so the OS's normal rpath/PATH search
      gets the last word. Deliberately does **not** use the vcpkg-linked `MILKDAWP_PROJECTM_TARGET`
      from `cmake/ProjectMDependency.cmake`/`EngineInfo::hasProjectM()` — those still answer "was
      the SDK present at configure time?" (kept as-is, unchanged) — this class hand-declares its
      own function-pointer typedefs and resolves everything by name at runtime, so a build
      compiles and a plugin loads/scans cleanly with `Unavailable{reason}` even with no projectM
      SDK present at all, on either end. Covers 14 functions (create/destroy, load preset,
      window/mesh size, fps, preset duration, beat sensitivity get/set, PCM feed, FBO render,
      preset-switch-failed callback, version string) — enough for 2.2's `RenderEngine`, not the
      full projectM API. Windows dependency probing (GLEW etc.) from v1's loader is **not**
      folded in yet since nothing here touches GL resources directly; revisit if `RenderEngine`
      (2.2) needs it. 3 new Catch2 tests added (`engine/tests/`, new `milkdawp_engine_tests`
      target, wired into CTest same as core) — locally verified real (93/93, up from the
      previous 90/90) on this Windows box with `MILKDAWP_WITH_PROJECTM=OFF` (no vcpkg here), VS
      2022 generator, `Unavailable` branch exercised since no projectM DLL exists on this
      machine. The exact projectM 4 symbol names/signatures are from memory of the public C
      API, not checked against the real vcpkg-installed header (no `VCPKG_ROOT` on this box) —
      needs a real run against the devcontainer's actual projectM 4.1.7 install to confirm the
      "available" branch (version check, all 14 symbols resolving) actually works, not just the
      "missing" branch this box could exercise.
      Update: `VCPKG_ROOT` is now set on this box (vcpkg installed to `C:\vcpkg`) and
      `MILKDAWP_WITH_PROJECTM=ON` builds for real -- and doing so surfaced two real bugs the
      `Unavailable`-only testing above couldn't catch. First, `kLibraryFileName` was a single
      hardcoded `"projectM-4.dll"`, but this vcpkg port applies a `d` debug postfix
      (`projectM-4d.dll`), so a Debug MilkDAWp build could never find a Debug vcpkg install by
      that name -- fixed by trying both names (`kLibraryFileNames[]`), same reasoning extended
      un-verified to the mac/Linux names. Second, and more fundamental: because this class
      deliberately never links `MILKDAWP_PROJECTM_TARGET` at build time (loads it by name at
      runtime instead, per this class's own design), vcpkg's automatic runtime-DLL deployment
      (`VCPKG_APPLOCAL_DEPS`) never copies projectM's shared library next to the plugin/app
      binary the way it copies zlib/libpng -- so even with the right name, the file was never
      there to find. Fixed with a new `milkdawp_deploy_projectm_runtime(<target>)` CMake
      function (`cmake/ProjectMDependency.cmake`) that POST_BUILD-copies
      `$<TARGET_FILE:${MILKDAWP_PROJECTM_TARGET}>` next to `milkdawp_plugin_VST3`/
      `milkdawp_plugin_Standalone` (no `bundleDirectoryHint` needed --
      `currentModuleDirectory()` alone finds it once the file is actually there).
      That got the DLL loading, but surfaced the real prize: every one of the 14 hand-declared
      symbol names/signatures in `ProjectMFunctions` — "from memory of the public C API, not
      checked against the real vcpkg-installed header," per this item's original note — could
      finally be checked against that header for the first time. 13/14 were exactly right.
      One wasn't: `projectm_opengl_render_frame_fbo` does not exist in the real API
      (`render_opengl.h` has `projectm_opengl_render_frame(instance)`, no FBO parameter --
      it renders into whatever framebuffer is currently bound), so `require()` failed it and
      *the entire load failed* (all-or-nothing symbol resolution), reporting `unavailable` with
      that symbol named as missing. Renamed the function pointer to `openglRenderFrame` to match
      the real API's name/signature; `RenderEngine::renderFrame()` (2.2 below) now wraps the call
      in a `glBindFramebuffer`/restore scope guard instead of passing the FBO as an argument.
      That got the library reporting `available` -- and immediately crashed the Standalone build
      within a couple seconds of launch (see 2.2's update below for why and the fix). The engine/
      plugin test binaries still don't get the DLL deployed (only plugin/Standalone do), so the
      test suite still only exercises the `Unavailable` branch.
      Update (2026-09-26): once 2.2's crash was fixed, a *new*, much longer investigation started
      -- the plugin loaded and ran perfectly in the Standalone build and in `pluginval
      --strictness-level 5` (scan, editor, audio processing at every sample rate/block size,
      state save/restore, automation -- full `SUCCESS`, repeatedly, across a week of rebuilds --
      but **silently failed to load in REAPER and Cubase**, independently, with no exception, no
      crash, no hang, and the module cleanly unloading afterward. Root-caused by building a
      vanilla JUCE 9 example plugin from the same JUCE checkout and adding this project's real
      features to it one at a time (GL context owned by the processor not the editor, APVTS,
      resizable+constrainer, keyboard focus, a full FlexBox drawer with real
      `ButtonAttachment`/`ComboBoxAttachment`s, the `VST3_CATEGORIES "Analyzer"` tag) -- every one
      of them loaded fine in REAPER. The one change that reproduced the failure: disabling JUCE's
      bundled zlib/libpng and linking vcpkg's copies instead (`JUCE_INCLUDE_ZLIB_CODE=0`/
      `JUCE_INCLUDE_PNGLIB_CODE=0`, §4.11's original Phase-0 decision). That, in turn, traced back
      to `engine/CMakeLists.txt` statically linking `MILKDAWP_PROJECTM_TARGET` into
      `milkdawp_engine` -- dead code contradicting this very class's "loads at runtime only"
      design (nothing in `engine/` includes a real projectM header or calls a linked symbol) --
      which pulled vcpkg's zlib/libpng in transitively and, with them, real `z.dll`/`libpng16.dll`
      files sitting next to the plugin binary that some *other* already-loaded plugin in REAPER's
      session most likely collides with by bare DLL name. Fixed by deleting that stale
      `target_link_libraries` call and reverting `MILKDAWP_JUCE_ZLIB_LIBPNG_FROM_VCPKG`'s default
      to OFF (see §4.11's own updated row for the full before/after). Confirmed fixed for real:
      the actual `MilkDAWp2 Dev.vst3` now loads and runs in REAPER. `ProjectMLibrary` itself still
      reports `unavailable` there specifically (`could not locate or load 'projectM-4.dll'`) even
      though the DLL is confirmed present next to the binary -- a separate, much lower-severity
      loose end, not yet root-caused, tracked as a follow-up rather than blocking further work
      (real projectM rendering isn't wired into `OutputSurface` yet regardless, per 2.4/2.6, so
      nothing currently active depends on this).
- [x] 2.2 (M) `RenderEngine` skeleton: owns GL context + projectM instance; FBO render via
      `projectm_opengl_render_frame_fbo`; per-frame PCM feed from `AudioRing`; parameter
      application from queue (no string lookups on the render thread).
      Note: the projectM-instance half is done -- `RenderEngine::create()` loads a
      `ProjectMLibrary` (2.1), creates/configures/destroys one instance, exposes
      `pushParameterUpdate()` (SPSC queue of a closed `ParameterTarget` enum + float, drained on
      the render thread with no string lookup -- the actual §2.7 anti-pattern this item targets),
      `loadPreset()` (immediate/unscheduled -- 2.6 adds `dueAtSample` timing on top), a preset-
      switch-failed callback trampoline, and `renderFrame(AudioRing, fbo)` which feeds PCM via
      `AudioRing::copyLatest()` (deliberately not `consumeHop()` -- that cursor belongs to the
      analysis thread per its own doc comment; render is a second, independent reader, exactly
      what `copyLatest()` exists for) and calls `openglRenderFrameFbo`. Never returns null: an
      unavailable projectM is a valid, permanently-inert `RenderEngine` whose calls are all
      no-ops, not a construction failure. **Deliberately does not own a GL context yet** -- that
      half of this item's title is Phase 2.3's spike (shared contexts across platforms/surfaces
      is genuinely a separate, harder problem, sized L on its own right below this one); this
      class's contract is the same one a `juce::OpenGLContext` renderer callback already gives
      you (context is current when you're called), so 2.3 slots in underneath it without a
      redesign. 4 new tests (7/7 in `milkdawp_engine_tests` now), same honest caveat as 2.1: run
      on this Windows box with no projectM installed, so only the `Unavailable` branch is
      exercised here (97/97 total, up from 93/93) -- the "instance actually created and rendered
      a frame" branch still needs a devcontainer run with real projectM present.
      Update (2026-09-26, projectM API audit, ADR-0008): two defects found by reading the real
      4.1.7 source, fixed in 2.13 and 2.14. (1) `ScopedFramebufferBinding` has no effect.
      4.1.7's `RenderFrame()` calls `glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)` before its final
      composite, so the frame always lands on framebuffer 0 whatever `targetFbo` is. It only
      appears to work because JUCE's embedded-surface target is framebuffer 0. (2) The PCM feed
      pushes the latest fixed `pcmFrameCount` (512) frames every render call. At 48 kHz / 60 fps
      a frame spans 800 samples, so about a third of the audio never reaches projectM. Above
      ~94 fps the same samples are fed twice. projectM keeps its own sample buffer, so the
      correct feed is "every frame written since the last render call", tracked with a
      render-side cursor and capped at `projectm_pcm_get_max_samples()`.
- [~] 2.3 (L) **Spike:** presentation to multiple surfaces per platform. Try shared contexts
      (`setNativeSharedContext`) on Win/macOS/Linux; measure; fall back to PBO readback for the
      preview. Also verify the JUCE-painted `ControlDrawer` composites over the GL surface on
      each platform under JUCE 9 (Direct2D on Windows, EGL on Linux), as a child of the GL
      component. Write an ADR with the result and the per-platform strategy (ADR-0007 went to an
      unrelated dev-tooling decision that came up mid-spike, and ADR-0008 to the projectM 4.2
      pin this spike depends on; this one will be ADR-0009). **Depends on 2.12:** the
      two-surface half needs real render-to-FBO, which projectM 4.1.7 cannot do.
      Note: two real findings from Matthew's REAPER/Windows testing (2026-09-19), not guesses:
      (1) **A JUCE-painted component must be a child of the GL-attached component, not a
      sibling, to render on top of it.** First cut had the diagnostics label as an editor-level
      sibling of `OutputSurface`; it was immediately painted over by the GL surface every frame.
      Confirmed exactly the risk §4.11 flagged for the drawer ("as a child of the GL component...
      rather than as an overlapping sibling peer") -- fixed by reparenting the label as a child
      of `OutputSurface` itself, and now it stays visible. This answers that part of the item for
      Windows: plain JUCE child-compositing over a GL surface works, no Direct2D-specific
      workaround needed (at least for simple 2D overlays; `ControlDrawer` itself still doesn't
      exist to test the real case, 2.11).
      (2) **Plain `detach()` + `attachTo()` does *not* preserve the native GL context.** The
      `glContextCreationCount()` diagnostic climbs by one on every editor close/reopen, not just
      once at startup -- confirmed by repeated open/close cycles in REAPER. So RenderEngine
      holding one long-lived `juce::OpenGLContext` *object* does not, by itself, give a
      long-lived native context to render into; JUCE tears down and recreates the underlying
      context on every (re)attach regardless. This has a real consequence not yet acted on:
      `RenderEngine::create()` currently creates the projectm instance once, in its own
      constructor, before any GL context has ever existed -- if the real API allocates GL
      resources inside `projectm_create()` (very likely for a GL-based renderer, but unconfirmed
      -- still no projectM build anywhere to test against), that instance would end up bound to
      a context Windows just proved gets discarded. The honest fix (moving instance
      creation/recreation to fire from `newOpenGLContextCreated()` instead of the constructor)
      is **not implemented yet**: it's a design change to code already covered by passing tests,
      and doing it blind, without the real library to verify the assumption against, risks
      "fixing" a problem that may not exist in projectM's actual API. Flagged here rather than
      guessed at. Still need: the `setNativeSharedContext` two-surface test (this only covers
      one surface so far), and macOS/Linux entirely (no hardware available for either).
      Update (2026-09-20): the assumption above is now confirmed, the hard way -- with a real
      projectM install actually loadable (2.1/2.2's DLL-deployment fixes), the plugin crashed
      instantly on insertion in Reaper (red/failed slot, editor never shown), because
      `RenderEngine`'s constructor called `projectm_create()` at processor-construction time,
      before any GL context existed at all. Fixed exactly as flagged: `RenderEngine::
      ensureInstanceCreated()`/`releaseInstance()` now do the create/destroy, called from
      `OutputSurface::newOpenGLContextCreated()`/`openGLContextClosing()` respectively (the
      latter guarantees a context still current for the destroy, same as the former does for the
      create). `isAvailable()` already meant `instance_ != nullptr` rather than "library loaded",
      so no caller-visible contract changed -- callers just now correctly see `isAvailable() ==
      false` for the brief, normal window before any surface has attached, instead of a
      false-positive followed by a crash. (Standalone staying up right after this specific fix
      wasn't actually a confirmation of it -- at that point `ProjectMLibrary::load()` was still
      failing on the `projectm_opengl_render_frame_fbo` symbol-name bug 2.1 describes, so
      `ensureInstanceCreated()`'s `!library_` guard meant `projectm_create()` never actually ran
      yet either. Once 2.1's symbol fix landed and the library genuinely loaded, `projectm_create()`
      ran for the first time and crashed Standalone within a couple seconds -- see below.)
      Update 2: that crash's real cause -- confirmed via `dumpbin /dependents` on the real
      `projectM-4d.dll` and a check of projectM's own vcpkg buildtrees source -- is GLEW.
      projectM links GLEW internally (dumpbin shows a static import on `glew32d.dll`) but never
      calls `glewInit()` itself (grepped projectM's source: `glewInit` appears only in its
      unrelated SDL example app, never in the library). JUCE loads its own OpenGL extension
      bindings independently and does nothing for GLEW's separate global function-pointer table,
      so any extension call inside `projectm_create()` jumped through a null pointer. Fixed with
      a small `ensureGlewInitialized()` helper in `RenderEngine.cpp` (Windows-only for now, same
      no-hardware caveat as everywhere else in this item): opens `glew32(d).dll` by name --
      already resident in the process since projectM's own DLL statically imports it, so this
      just gets a handle to the same loaded module and its global state -- and calls `glewInit()`
      once before the first `projectm_create()`. Confirmed for real this time: Standalone stayed
      up and responsive for 6+ seconds (past the crash window) with `projectM: available`.
      Reaper re-verification is still Matthew's next hand-test.
      Update 3 (2026-09-26): **Windows answered; ADR-0009 written.** The design changed from
      "the engine owns a `juce::OpenGLContext` that surfaces attach" (which cannot keep a native
      context alive, finding (2) above) to **an `OffscreenGLContext` owned by the engine's own
      render thread** (hidden WGL window, never shown). projectM renders into triple-buffered
      textures there, and each `OutputSurface` is its own JUCE context that shares with it.
      Two more real findings on the way: JUCE's `setNativeSharedContext` path silently fails
      on NVIDIA (its `wglShareLists` runs on JUCE's thread while the engine's context is
      current), so surfaces now share themselves in `newOpenGLContextCreated()` through a
      handshake that parks the render thread (`RenderEngine::shareIntoCurrentContext`); and
      `glBlitFramebuffer` into a JUCE window's default framebuffer fails with
      `GL_INVALID_OPERATION`, so surfaces draw with JUCE's `copyTexture` instead. A
      `glIsTexture` probe per surface detects a failed share and shows it in the diagnostics.
      Verified on this box (RTX 4070 Ti SUPER, JUCE 9.0.2): the Standalone editor shows
      projectM at 60 fps with the drawer composited over it (answering the drawer half of this
      item for Windows), and `mdw-view --output` shows the main window and an Output window
      rendering the same frames at once (the two-surface test). The GLEW crash and the "context
      recreated on every reopen" problem are gone by construction: projectM's context never
      belongs to a window. **Stays `[~]`:** macOS (no offscreen context yet) and Linux (EGL
      offscreen context exists for headless use, but no sharing with JUCE's windows) need
      hardware; the PBO-readback fallback for drivers that refuse to share is designed, not
      built (ADR-0009). Update (2026-09-27): the fallback is built (2.15), and Linux windows
      now use it instead of showing black. macOS still needs an offscreen context.
- [x] 2.4 (M) `OutputSurface` implementations: embedded component (primary window) and
      `OutputWindow` (owned top-level window, borderless fullscreen on a chosen display,
      remembers its display). Attach/detach without engine restart; both surfaces show the
      same frame. Port v1's OBS niceties (fixed window title, transparency option).
      Note: the embedded primary-window half exists (`engine::OutputSurface`, above) and is
      wired into the real plugin editor. Renders a time-based colour cycle, not a real projectM
      frame, since `renderFrame()`/`AudioRing` aren't wired in yet (no projectM build exists
      anywhere this has run to render for real) -- pure placeholder content standing in so the
      context-lifecycle question is separately testable from "does projectM rendering work".
      `OutputWindow` (the owned top-level, borderless-fullscreen half) is not started; it is
      exactly what needs 2.3's shared-context answer first, since it's the actual two-surface
      case that spike is about (and, through 2.3, needs 2.12's render-to-FBO).
      Update (2026-09-26): done on Windows. `OutputSurface` is now a presenter (own JUCE GL
      context, shares with the engine, draws the latest frame scaled to cover and cropped,
      reports its size and visibility to the engine), used by the plugin editor, the Output
      window and `mdw-view`. `engine::OutputWindow` is a top-level window with a fixed title
      ("MilkDAWp Output", for OBS window capture) that switches between a titled resizable
      window and borderless fullscreen covering the display it is on (F11 or double-click;
      Esc leaves). It never uses JUCE kiosk mode, which is process-wide and would fight a DAW.
      The switch recreates the native window, so the surface re-shares every time, which
      exercises the ADR-0009 path constantly. Verified with two surfaces showing the same
      frames (`mdw-view --output`). Remaining v1 OBS nicety, **not done**: the
      window-transparency option (deferred to 3.12, where the plugin's Output window gets its
      settings). "Remembers its display" is in-session only: the windowed bounds live on the
      processor and fullscreen uses the display the window is on; persisting them in the
      plugin state is 3.12's.
- [x] 2.5 (M) `PresetLoader` on the Preset I/O thread: read file, cheap syntax pre-validation,
      blacklist on failure (`projectm_set_preset_switch_failed_event_callback`), prefetch of the
      next preset, load-time measurement and logging.
      Note: `validate()` is a cheap non-parsing sniff (exists, non-empty, first 512 bytes have no
      null byte and at least one `=`) -- catches "this obviously is not a preset", never claims
      to validate projectM-compatible syntax. `prefetch()` runs validate() first, then reads the
      whole file and times it (`std::chrono::steady_clock`), returning file bytes +
      `loadTimeMicros`; failures on either path are recorded in an internal blacklist
      (path -> reason), queryable/clearable. RenderEngine's preset-switch-failed callback (2.2)
      is the *other* writer this blacklist is designed for (an actual projectM load failure this
      class's cheap check couldn't catch) but isn't wired to it yet in this pass -- that's a
      one-line follow-up once something owns both a PresetLoader and a RenderEngine together
      (Phase 3's processor, most likely). Does **not** yet produce `core::Messages`'
      `PresetLoadResultMessage` (interned `presetId`): that requires a `PresetLibrary`
      ID-interning table that §4.1's architecture diagram names but nothing in the repo builds
      yet (only `Playlist`, 1.11, exists) -- deferred honestly rather than faking an ID. 8 new
      tests (14/14 in `milkdawp_engine_tests`, all pass regardless of projectM presence since
      this is pure file I/O). 104/104 total, up from 97/97.
      Scope note (2026-09-26, ADR-0008 audit): the prefetched bytes should be loaded with
      `projectm_load_preset_data` (in the API since 4.0, not yet in `ProjectMLibrary`) so the
      render thread never touches the filesystem. Prefetch hides only the file read, though.
      The visible hitch is preset **shader compilation**, which projectM does synchronously on
      the GL thread inside the load call, in 4.1.7 and 4.2 alike. Measuring and working around
      that cost is 5.4's job, and 5.4 is the primary hitch mitigation, not this item.
- [x] 2.6 (S) Execute `TransitionRequest`s on the render thread at `dueAtSample`; early-issue
      for soft cuts. Log actual vs intended landing error in samples.
      Note: `TransitionExecutor` (`engine/include/milkdawp/engine/TransitionExecutor.h`) does the
      sample-position state machine -- SPSC-queued `TransitionRequestMessage`s, Soft cuts issued
      `blendSeconds/2` early so the blend's perceptual midpoint lands on `dueAtSample` (§4.4),
      Hard cuts issued exactly at it, `onTick(currentSample, callback)` reports each due
      transition's actual issue sample and landing-error-in-samples to the caller. Deliberately a
      template callback, not `std::function`, so nothing on the render thread's hot path
      allocates (§4.2). It is pure sample-position math -- no GL, no projectM, no JUCE -- so,
      like `core::TransitionScheduler` which produces the messages it consumes, it's fully
      deterministic under a simulated clock: 7 new tests (21/21 total in `milkdawp_engine_tests`
      now), no environment caveat needed this time. **Not wired into `RenderEngine::renderFrame()`
      yet** (hence `[~]`, not `[x]`): the callback `onDue` would need to resolve
      `TransitionRequestMessage::presetId` to a file path before calling `loadPreset()`, and
      nothing does that yet -- same `PresetLibrary` gap noted in 2.2 and 2.5. Wiring these three
      pieces together (TransitionExecutor + PresetLoader + RenderEngine) is a natural single
      follow-up task once something owns all three (Phase 3's processor). 120/120 total, up from
      113/113 (7 new tests from this item).
      Update (2026-09-26): wired end to end, by the engine rather than the processor, so the
      plugin, `mdw-view` and the future app share it. New pieces:
      - `core::PresetLibrary`: interns preset paths as stable ids (never reused, 0 = none).
      - `engine::Director`: the analysis thread. It consumes the ring in 512-frame hops and
        runs Analyzer → bass OnsetDetector → TempoTracker → BeatClock, or `HostTransport`
        while the plugin's host plays, into `TransitionScheduler`. It owns the Playlist,
        PresetLibrary and PresetLoader, handles next/previous/index jumps and lock, and
        detects host stop, loop and relocate.
      - `engine::PresetHandoff`: moves the preset text the director read to the render thread
        through fixed slots and SPSC queues, so nothing allocates on the render thread.
      - The render loop: `TransitionExecutor` plus `projectm_load_preset_data`, setting the
        soft-cut duration first when the cut is soft.
      - projectM load failures go back to the director and into the blacklist.
      - `engine::Visualizer`: the facade shells own (ring, render engine, director, in the
        right construction and destruction order).
      The processor publishes an `EngineControls` snapshot from its parameters every block,
      and the preset folder and current preset are now saved in and restored from
      `StateSchemaV2`. The editor's "Set" menu picks the folder; the drawer shows
      "n/N name" and the BPM with its source.

      One deliberate deviation, recorded in `Director`'s header: preset files are read on the
      director thread when a transition fires, not prefetched a transition ahead. The
      scheduler decides on the hop a beat is *crossed*, so there is no earlier moment, and
      .milk files are a few KB. The same fact means `dueAtSample` is usually already past when
      a request arrives, so soft cuts start on the beat instead of `blend/2` before it.
      Scheduling a beat ahead is a scheduler improvement for Phase 5.

      Tests: `DirectorTests` runs the real director and render threads against a folder of
      the fixture presets plus one broken file. It covers the initial load; next, previous
      (via history) and index jumps; skipping the broken file; lock suppressing automatic
      transitions; and Timed mode advancing on its own. 155/155 total on Windows.
- [~] 2.7 (M) Headless render test harness: offscreen GL context on Linux (EGL surfaceless
      first, since JUCE 9 uses EGL natively; Xvfb + Mesa as fallback), render N frames of
      fixture presets, assert non-black + frame-to-frame delta, run under ASan. Needs 2.12:
      renders to an FBO via `projectm_opengl_render_frame_fbo` and drives preset time with
      `projectm_set_frame_time` from the fixture's sample position, so frame N is reproducible.
      Note (2026-09-26): the harness exists and passes on Windows. `HeadlessRenderTests.cpp`
      uses `OffscreenGLContext` (the same class the render thread uses; ADR-0009) with
      `ProjectMInstance` and `GlFrameTarget`, and renders the three new fixture presets
      (`fixtures/presets/`, written for this repo, AGPL). It feeds a sine, pins time with
      `set_frame_time` at 60 fps, and reads back pixels. Checks: every preset is non-black and
      changes between frames; time-driven content is reproducible from the frame time alone
      (the border colour at frames 10/40/70 matches across two fresh instances within 2 levels
      per channel, and does change between those frames); and a preset projectM rejects fires
      the failure callback. Whole frames are *not* reproducible across instances: projectM
      seeds per-load randomness, and MilkDrop 1 presets always get a random hue shading in the
      composite (projectM's `VideoEcho`, which ignores `fShader`). So `mdw-border.milk` is a
      MilkDrop 2 preset with a plain composite shader. This matters for any future
      "compare against a golden image" test and for offline render-to-video (post-1.0). projectM is now deployed next to the engine test
      binary, so these and the render-thread tests exercise the real library on a dev box;
      without it they report why and pass. **Stays `[~]`:** the Linux EGL-surfaceless path
      is written but has never compiled or run (no Linux box or CI run yet), and no ASan run
      has happened anywhere.
      Update (2026-09-27): the Linux EGL path now compiles (GCC and Clang, including ASan) and
      the harness runs in every CI job, but **nothing renders in CI yet**: all three
      platforms take the "unavailable, report why and pass" path. On Linux,
      `eglGetDisplay`/`eglInitialize` fails in the devcontainer image (confirmed locally with
      `-s`: "headless render unavailable here: eglGetDisplay/eglInitialize failed"). Mesa's
      EGL vendor file is present, so the likely cause is asking for the default display with
      no X/Wayland display, instead of `eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
      ...)`. macOS still has no offscreen context (the `#else` stub). The Windows runner only
      has the GL 1.1 software renderer. So the ASan run exists, but it doesn't reach the
      render path yet. Next: fix the Linux display selection, then consider making CI fail
      rather than skip when the render path is expected to be available.
      Update (2026-09-27, run `36323254149`): **the Linux render path now runs in CI for
      real.** Cause confirmed with a small EGL probe in the image: `eglGetDisplay(
      EGL_DEFAULT_DISPLAY)` fails with `EGL_NOT_INITIALIZED` when there's no X11/Wayland
      display, while `eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, ...)` gives
      Mesa llvmpipe with GL 4.5 core. `OffscreenGLContext` now tries the default display
      first (on a desktop that's the display JUCE's windows use, which 2.3's sharing will
      need) and falls back to the surfaceless platform. Local run in the image: the three
      tests make 19 assertions (up from 3 when skipping), and all 36 engine test cases pass.
      Regression guard: with `MILKDAWP_REQUIRE_HEADLESS_RENDER` set, "unavailable" is a
      failure instead of a pass, and the Linux CI job sets it. In CI the three tests pass
      in 0.11-0.72 s each (0.02 s when they were skipping). **Stays `[~]`** for the ASan
      part: the `ci-linux-asan` preset builds with `MILKDAWP_WITH_PROJECTM=OFF`, so the
      harness still skips under ASan. Turning projectM on there is the remaining step (expect
      LeakSanitizer reports from Mesa/projectM that may need a suppressions file). macOS
      still has no offscreen context, and the Windows runner only has GL 1.1, so neither
      renders in CI.
- [x] 2.8 (S) Frame timing and GPU time metrics (`GL_TIMESTAMP` queries where available);
      status snapshot for the UI.
      Note (2026-09-26): `RenderStats`, published per frame through a `SeqlockSnapshot`:
      fps measured over one second; CPU frame time (render call + `glFinish`); GPU time
      (`GL_TIME_ELAPSED` query around the render call, read after the finish, so no stall);
      FBO size; surfaces attached; paused flag; presets loaded and failed; last preset load
      time (parse + shader compile on the render thread, the number 5.4 needs); last landing
      error in samples. The director publishes `DirectorStatus` (BPM, beat confidence, beat
      source host/detected/none, playlist size and position, transitions issued, presets
      skipped). The plugin editor's diagnostics line and `mdw-view`'s info line show both
      (Set > Show diagnostics toggles it in the plugin).
- [x] 2.9 (M) `mdw-view` dev tool: WAV → engine → primary window with the `ControlDrawer`
      from 2.11. First place beat-aligned transitions are visible to a human. Runs inside the
      devcontainer with GPU passthrough on Linux hosts.
      Note (2026-09-26): `tools/mdw-view` (JUCE GUI app, `MILKDAWP_BUILD_MDW_VIEW`, on by
      default). Usage: `mdw-view [--output] [audio-file] [preset-folder]`. It plays the file
      (looping) through the default output device while handing the same blocks to an
      `engine::Visualizer`, exactly as a plugin block would arrive, and shows an
      `OutputSurface` with the shared `ControlDrawer`. prev/next, lock, shuffle and the
      transition-mode combo drive the engine directly (no APVTS), a prototype of Phase 4's
      non-plugin wiring. "Set" opens files and folders, plays/pauses, and sets cut style and
      bars. Out opens the Output window; `--output` opens it at startup. Space plays/pauses;
      the §4.9 keys work. The default preset folder is `fixtures/presets`. Verified on
      Windows: launches, loads the fixture folder, renders at 60 fps, and shows main and
      Output windows in sync. The Linux devcontainer and GPU-passthrough half is untested (no
      Linux display here, and 2.3 has no Linux sharing path yet).
- [x] 2.10 (S) Engine behaviour with zero surfaces: pause GPU work, keep logical state, resume.
      Test: attach, detach, attach again; preset and playlist position unchanged.
      Note (2026-09-26): with no visible surface the render thread makes no GL calls; it
      keeps its context and projectM instance, pumps its window messages and services share
      requests. So preset, playlist position *and* the visual itself survive; the director
      keeps analysing and scheduling throughout. `RenderEngineTests` covers it: frames stop
      advancing while hidden, `paused` is set, the engine stays available, and rendering
      resumes on the same context when visible again.
- [x] 2.11 (M) `milkdawp_ui` drawer components: `ControlDrawer` (hidden / revealed / pinned
      states, hover and tap reveal, auto-hide timer, first-run reveal), `DrawerScrim`
      (translucent band, optional blur), slot layout that collapses to icons at small widths.
      Unit-testable state machine for the reveal/hide logic.
      Note: the state machine half is done -- `milkdawp::ui::DrawerStateMachine`
      (`ui/include/milkdawp/ui/DrawerState.h`) is deliberately JUCE-free (no `Component`, no
      `Timer`; every method takes "now" as a parameter instead of reading a clock itself), so
      it's a fully deterministic unit under test per this item's own text. Encodes all of §4.9's
      rules: three states, hover/tap reveal, auto-hide after `autoHideSeconds` while unpinned,
      pin suppresses auto-hide and makes 'H' a no-op (unpin first, then hide), first-run reveal
      stays open regardless of elapsed time until the *first* interaction of any kind. 9 new
      tests in a new `milkdawp_ui_tests` target (wired into CTest like core/engine) -- these are
      fully environment-independent (no JUCE types involved), unlike the engine tests' honest
      "only the Unavailable branch is exercised here" caveat. 113/113 total, up from 104/104.
      The JUCE half now exists too (Phase 3.3): `ui::DrawerScrim` (flat translucent fill --
      optional blur explicitly not implemented, a separate per-platform investigation) and
      `ui::ControlDrawer` (`ui/include/milkdawp/ui/ControlDrawer.h`), a `Component` composing
      the row from §4.9's mockup (prev/next, lock, shuffle, transition-mode combo, BPM label,
      output/settings/pin) over the scrim, ticking `DrawerStateMachine` from its own `Timer` and
      staying at a fixed bottom footprint with `alpha 0` while hidden so the hover-to-reveal
      gesture keeps working even when visually gone. Deliberately owns no
      `AudioProcessorValueTreeState` (keeps `milkdawp_ui` decoupled from the plugin layer, §4.1)
      -- Phase 3.3's `PluginEditor` attaches the public widgets to real parameters. Icon-
      collapsing at small widths is **not implemented**: every control that isn't a combo box is
      already icon-only (matches the mockup), so the one remaining case (the two combo boxes
      shrinking their text at narrow widths) was judged not worth a bespoke breakpoint mechanism
      yet. Checkbox now `[x]`: the class exists and is composed for real in Phase 3.3, which was
      this item's own stated blocker.
- [x] 2.12 (S) projectM 4.2 overlay port (ADR-0008, D15): `vcpkg-overlays/projectm/` building
      upstream `master` at a pinned commit hash (start from `1e7ef78`, 2026-09-10, re-check
      for newer commits first) with a `SHA512`; register it under `overlay-ports` in
      `vcpkg-configuration.json`; move `vcpkg.json`'s `projectm` override to the overlay version.
      Confirm the installed library is still `projectM-4(d).dll` / `libprojectM-4.so` /
      `.dylib` (SO version 4 on master), so `ProjectMLibrary`'s names and
      `milkdawp_deploy_projectm_runtime` keep working, and that `glew32*.dll` no longer
      appears among its dependencies (`dumpbin /dependents`). Update the devcontainer image
      and `THIRD_PARTY_NOTICES.md` (the source offer names the commit, not a version).
      **Blocks 2.3, 2.4, 2.7, 2.13.**
      Note (2026-09-26): done and built on Windows. `vcpkg-overlays/projectm/` pins
      `1e7ef7803b69024d1e0656705670adda2ffac817` (still master's head and no 4.2.0 tag upstream
      on this date), adapted from the registry's 4.1.7 port. GLEW dependency dropped, and
      `macos-pkgconfig.patch` dropped because master no longer emits the `opengl` pkg-config
      requirement it removed. The SHA512 matched on first install. The bundled
      `vendor/projectm-eval` submodule is at projectm-eval v1.0.7, the same version as the
      baseline's port, so no other bump was needed. Installed result verified: `projectM-4.dll`
      and `projectM-4d.dll` (names unchanged); `version.h` reports 4.2.0;
      `render_frame_fbo`, `create_with_opengl_load_proc` and `set_frame_time` are exported;
      `dumpbin /dependents` shows only system/CRT DLLs (plus `OPENGL32.dll` in Debug), no
      GLEW. Because 4.2 has no GLEW at all, `RenderEngine`'s `ensureGlewInitialized()` would
      have reported projectM unavailable, so it was **deleted here** rather than in 2.13.
      `projectm_opengl_render_frame` in 4.2 still targets framebuffer 0, so rendering
      behaviour is unchanged until 2.13 switches to `render_frame_fbo`. Stale
      `glew32(d).dll`/`.pdb` copies from 4.1.7 builds were deleted from
      `build-win`'s plugin artefact folders (`milkdawp_deploy_projectm_runtime` only ever
      adds files). `dev-win` configure + Debug build clean; `ctest` 135/136. The one
      failure was the pre-existing `DoubleBufferedSnapshot` race noted under 3.1, not this
      change (fixed right after as `SeqlockSnapshot`; 138/138 since). Hand-tested
      2026-09-26: the Debug VST3 loads in REAPER without crashing and reports projectM
      available. Dockerfile copies `vcpkg-overlays/` before `vcpkg install`, and the
      devcontainer-image workflow triggers on it. **Stays `[~]`:** not yet run in the
      devcontainer/Linux or macOS. (The live-instance hand test is done: REAPER, above, and
      the Standalone and `mdw-view` now render real presets with it, see 2.3.)
      Update (2026-09-27): builds on Linux (`x64-linux-dynamic`, in the devcontainer image,
      `libprojectM-4.so.4.2.0`) and macOS (`arm64-osx-dynamic`, in CI) as well as Windows. The
      deploy step places it next to the Linux and macOS binaries and the runtime layout
      check (3.10) passes there. Loading it at runtime on Linux/macOS is still unverified,
      because the headless tests skip before `ProjectMLibrary` renders anything (2.7); that
      is tracked under 2.3/2.7, not here.
- [x] 2.13 (M) Adopt the 4.2 API in the engine. `ProjectMLibrary`: require
      `projectm_opengl_render_frame_fbo`, `projectm_create_with_opengl_load_proc` and
      `projectm_set_frame_time`. A 4.1.x library then reports `Unavailable` and names the
      missing symbol; no 4.1 fallback path. Also add the setters the scheduler needs and
      nothing currently wires: `projectm_set_soft_cut_duration`, `projectm_set_hard_cut_enabled`,
      `projectm_set_preset_locked`, `projectm_load_preset_data`, plus
      `projectm_set_log_callback` for 5.9. `RenderEngine`: delete `ScopedFramebufferBinding` (`ensureGlewInitialized()` was
      already removed in 2.12), render with `render_frame_fbo`, and create instances through
      the load-proc variant. Try `juce::OpenGLHelpers::getExtensionFunction` versus `nullptr`
      (projectM's own resolver) per platform and record which one works. Keep per-instance
      state (handle, PCM cursor, FBO) in a struct rather than loose `RenderEngine` members.
      That costs nothing now and keeps post-1.0 layers open (ADR-0008, Consequences). Tests:
      the `Unavailable` branch names the missing 4.2 symbol; the available branch on a box with
      the overlay installed.
      Note (2026-09-26): done. `ProjectMLibrary` resolves 21 symbols, including all three 4.2
      ones; a 4.1 library fails with them named and a pointer to ADR-0008. A new version check
      accepts 4.2 or later 4.x only (`parseVersion`/`isSupportedVersion`, unit-tested).
      `ProjectMInstance` is the per-instance object; `RenderEngine` holds one rather than loose
      members. `ScopedFramebufferBinding` is gone: frames render with `render_frame_fbo` into
      `GlFrameTarget`s. Instances are created with the load-proc variant and a *null* proc
      (projectM's own glad resolver), which works on Windows with our WGL context; JUCE's
      `getExtensionFunction` was not needed, so it wasn't tried. `setLogCallback`/`setLogLevel`
      are resolved but not registered yet (5.9 will). The bigger structural change this
      enabled, an engine-owned offscreen context, is 2.3's update and ADR-0009.
- [x] 2.14 (S) Correct PCM feed to projectM: replace the fixed `copyLatest(pcmFrameCount)`
      per render call with a render-side read cursor that feeds exactly the frames written
      since the last call, capped at `projectm_pcm_get_max_samples()` (keep the newest if
      over). No drops at low frame rates, no duplicates at high ones (see 2.2's update).
      Deterministic unit test with a fake ring and several simulated frame rates.
      Note (2026-09-26): `engine::PcmFeeder` with a new `AudioRing::copyRange()`. It starts
      from "now" rather than replaying the ring, and preallocates, so the render thread never
      allocates. `PcmFeederTests`: 48 kHz audio at 30 fps (every frame fed exactly once), at
      144 fps with 512-frame blocks (no repeats, total fed = total written), a long stall
      (newest `max` frames only), and reset. Related fix: the processor's ring is now
      created once (stereo, 2^16 frames) instead of being replaced in every `prepareToPlay`,
      which would have raced the threads now reading it.
- [x] 2.15 (M) Readback fallback for surfaces that can't share the engine's context
      (ADR-0009's plan, pulled forward by ADR-0010: it is also how Linux windows and Android
      get a picture). Note (2026-09-27): `OutputSurface` registers as a readback client
      when its `glIsTexture` probe fails. While any client exists, the render thread reads
      each frame into one of two pixel-pack buffers and maps the other, filled a frame
      earlier, into `FrameReadbackExchange`. That is a triple-buffered CPU copy where readers
      pin the buffer they upload, and the writer drops a frame rather than wait. The surface
      uploads the newest copy into its own texture and draws it like the shared path. No
      clients, no cost; the buffers are freed when the last one goes. Tests:
      `FrameReadbackExchangeTests` (publish, pinning, reuse, clear, and a 3-reader stress test
      for torn frames) and an engine test that runs the real render thread with a readback
      client (right size, keeps up, trails the GPU frame by one, withdrawn when the client
      leaves). `MILKDAWP_FORCE_FRAME_READBACK=1` forces the path: verified on Windows with
      `mdw-view` (60 fps at 1280×720, same image as the shared path). Diagnostics now say
      "shared" or "readback (no shared context)".

Hand test: `mdw-view` with a folder of presets and a track with a clear drop. Transitions
should land on downbeats in Beat-quantized mode; no hitch longer than one frame on most presets.

Hand-test list for the 2026-09-26 Phase 2 pass (Windows only; macOS/Linux can't be built
here):
1. **REAPER, VST3 (Debug, dev identity):** insert, open the editor, Set > Choose preset
   folder (a real pack). Presets should render with the drawer over them. Next/prev (buttons
   and arrows), Lock, Shuffle and the mode combo should all act. Play a project: the BPM
   badge should show the host tempo with "host".
2. **Close and reopen the editor** several times while it plays: the visual must *not*
   reset (same preset, trails intact). Diagnostics (Set > Show diagnostics) should keep
   saying the surface is "shared" (it said "ok" before 2.15).
3. **Output window:** Out opens it; F11 (in it, or in the editor) toggles borderless
   fullscreen; Esc leaves it. Drag it to the second display, F11, and check it fills that
   display. Close the editor: the Output window keeps running. Remove the plugin: it closes.
4. **Save the project, reopen it:** same folder, same preset.
5. **Transport:** stop, loop and relocate in REAPER; Timed mode should pause when stopped,
   and BeatQuantized should cut on bar lines while playing.
6. **mdw-view** with your own track: `mdw-view path\to\track.wav path\to\presets`.
   Transitions in BeatQuantized should land near the downbeats (they fire on the beat, not
   ahead of it: see 2.6's note).
7. **Two plugin instances** in one REAPER project, both editors open: both render, with
   independent presets.

### Phase 3 — Plugin shell (VST3 / AU / Standalone wrapper)

**Goal:** a plugin at least as capable as v1 0.7.5, on the new engine. **Exit:** `pluginval`
strictness 5+ passes on all platforms in CI; manual checks in
Reaper, Ableton Live, FL Studio, Cubase, Logic (AU) pass the checklist below.

- [x] 3.1 (M) `MilkDAWpProcessor`: stereo/mono passthrough, RT-safe ring writes, transport
      snapshot, `ParameterModel`-driven APVTS layout, engine lifetime bound to the processor.
      `[[clang::nonblocking]]` on `processBlock`; RTSan job covers it.
      Note: `createParameterLayout()` builds the APVTS straight from `core::allParameters()`
      (one `AudioParameterFloat`/`Bool`/`Int`/`Choice` per `ParameterSpec`), so the two can never
      drift. `processBlock` interleaves into a `prepareToPlay`-sized scratch buffer (no
      per-block allocation) and writes it into a new `core::AudioRing` member; a new
      `core::DoubleBufferedSnapshot<T>` (JUCE-free, `core/`) publishes the host transport
      (extracted from `AudioPlayHead::getPosition()`) without the torn-read risk a bare
      `std::atomic<TransportInfo>` would have (that struct is well over the platform's
      lock-free CAS width). `renderEngine_` is a `unique_ptr<RenderEngine>` created in the
      constructor and destroyed with the processor -- literally "bound to the processor's
      lifetime" per §4.5. Buffer is only ever read, never written (bit-exact passthrough, §4.1).
      Verified for real: built both the shared-code lib *and* the actual `MilkDAWp.vst3` binary
      on this Windows box (MSVC/VS2022, `MILKDAWP_WITH_PROJECTM=OFF`), single-zlib/libpng check
      passed. New `milkdawp_plugin_tests` target (first plugin-layer tests in the repo) exercises
      the parameter layout against every `ParameterSpec`, default values, bit-exact passthrough,
      and transport-snapshot capture via a fake `AudioPlayHead` -- all passing, which is real
      confirmation the JUCE 9 API usage here (`ParameterID`, `Optional<T>::orFallback`,
      `PositionInfo` setters, `MemoryBlock::copyFrom`) is correct, not just compiling. `[[clang::
      nonblocking]]` is applied via a `__has_cpp_attribute`-guarded macro (no-op on MSVC/GCC);
      the RTSan job itself (0.4) still hasn't run for real anywhere (no Clang in any sandbox
      used so far). 129/129 total (6 new plugin tests + 3 for the new `DoubleBufferedSnapshot`).
      **Bug found and fixed (2026-09-26, during 2.12):** the `DoubleBufferedSnapshot` concurrent
      "never torn" test failed about half the time (16 of 30 reruns, Windows Debug build). The
      design had a real race, not a flaky test. If the writer published twice while a reader
      was still copying buffer *k*, the second publish overwrote *k* mid-copy, and one index
      over two buffers cannot exclude that. Replaced by `core::SeqlockSnapshot<T>`
      (`core/include/milkdawp/core/SeqlockSnapshot.h`), same `publish()`/`read()` interface.
      A sequence counter goes odd during a write and even after it, and a reader retries if a
      publish overlapped its copy. The writer (audio thread) stays wait-free; only UI/render
      readers can retry. The payload is held as relaxed `std::atomic<uint64_t>` words, so
      concurrent access is not a C++ data race (TSan-clean by construction). The constructor
      publishes `T{}`, so non-zero member defaults (`TransportInfo::timeSigNumerator = 4`)
      survive before the first publish. Tests renamed to `SeqlockSnapshotTests.cpp`, with two
      new cases: an eight-word payload tearing test, and non-zero defaults before the first
      publish. Result: 138/138 in `ctest` (Windows Debug), and the concurrency tests passed
      50/50 reruns. The `ci-linux-tsan` job still hasn't run for real anywhere (0.4).
      Update (2026-09-27): `ci-linux-tsan` now runs in CI and `SeqlockSnapshot`'s tests are
      TSan-clean there. Apple Clang rejected `MILKDAWP_NONBLOCKING void processBlock(...)`:
      `[[clang::nonblocking]]` is a type attribute and must follow the parameter list, and
      Clang also requires `noexcept` with it. Now `processBlock(...) noexcept
      MILKDAWP_NONBLOCKING override` (checked against Clang 20 with the project's warning
      flags). `processBlock` is therefore `noexcept` on every compiler: an exception in it
      now terminates rather than propagating into the host. **Correction:** "RTSan job
      covers it" above is not true yet. The `ci-linux-rtsan` preset builds with
      `MILKDAWP_BUILD_PLUGIN=OFF`, so `processBlock` is never compiled or run under RTSan.
      Covering it needs the plugin (or a plugin-test target) built in that job, with a test
      that drives `processBlock`.
- [x] 3.2 (S) State save/restore with schema v2 and v1 migration; editor size persistence with
      the Cubase ordering fix.
      Note: the v2-native half is done and tested -- `getStateInformation`/`setStateInformation`
      round-trip through `core::StateSchemaV2` + `serializeStateSchemaV2`/`deserializeStateSchemaV2`
      (Phase 1.14's JUCE-free format), covering every APVTS parameter plus editor size. Editor
      size lives as a plain member on the processor (`editorWidth()`/`Height()`/`setEditorSize()`),
      read by the editor's constructor and written from its `resized()` -- this *is* the Cubase
      ordering fix: the value is available whenever the editor happens to be constructed, relative
      to `setStateInformation`, because nothing has to be pushed into an editor that might not
      exist yet (§2.9). Round-trip tested (change a parameter + editor size, serialize, restore
      into a fresh processor instance, verify both). **v1 migration is explicitly not
      implemented** -- `core::migrateFromV1` (1.14) needs a `V1StateRecord` built from a real v1
      `juce::ValueTree::readFromData` blob's actual bytes, and 1.14's own note already flags that
      no real v1 session blob has ever been fed to this codebase. Guessing at v1's exact tree
      shape without one would be unverifiable, so `setStateInformation` only handles v2-native
      state for now; wiring v1 detection + migration in is a clearly-scoped follow-up for whenever
      those fixtures arrive.
      Update (2026-09-26): v1 migration dropped (see 1.14), so this item is complete as it is.
- [x] 3.3 (M) Video-first editor (§4.9): the whole editor is an embedded `OutputSurface`
      with the `ControlDrawer` over it, pinned by default. Drawer row: preset combo, picker,
      prev/next, lock, shuffle, transition mode, BPM/sync badge, output, settings, pin. Status
      from engine snapshots, not timers polling the processor. Resizable down to 480×270.
      Note: `PluginEditor` now composes `ui::ControlDrawer` as a child of `OutputSurface`
      (`Config{.startPinned = true}`, §4.9's plugin-editor default) and attaches its widgets to
      real `apvts` parameters: `lockCurrentPreset`/`shuffle` via `ButtonAttachment`,
      `transitionMode` via `ComboBoxAttachment` (combo items sourced from
      `core::ParameterModel`'s own choices list, so they can't drift), prev/next buttons pulse
      `triggerPrev`/`triggerNext` 0->1->0 in one host gesture (momentary commands, not
      persistent toggles -- state save/restore never captures a "stuck on" trigger). The BPM
      label and the preset-index label read `processorRef.currentBeatClock()` and the raw
      `presetIndex` parameter from the existing 10Hz UI timer -- "engine snapshots, not
      processor polling" per this item's own text, the same pattern the diagnostics label
      already used. `setResizeLimits`-equivalent minimum is 480x270 via the editor's
      constrainer. **Left honestly incomplete, hence `[~]` not `[x]`:** the "picker" (a real
      preset browser with names) needs the `PresetLibrary` gap 2.2/2.5/2.6 already flag and
      doesn't exist; `presetLabel` shows a bare index for now. The output (⛶) and settings (⚙)
      buttons are present in the row per the mockup but disabled with a tooltip, since Phase
      2.4/3.12 (Output window) and Phase 3.4 (settings popover) don't exist yet -- a disabled
      button with an honest tooltip beats one that silently does nothing. None of
      `presetIndex`/`triggerPrev`/`triggerNext`/`lockCurrentPreset`/`shuffle`/`transitionMode`
      are consumed by the engine yet to actually change what's rendering -- they are real,
      automatable, host-visible parameters now, but wiring their effect is the same
      `PresetLibrary`-shaped follow-up 2.6 already names. Visually confirmed for real in the
      Standalone build on this Windows box (`MILKDAWP_WITH_PROJECTM=ON` for the first time --
      see 2.1/2.2's notes on that changing), not just unit-tested: Matthew ran it and caught a
      real bug the unit tests couldn't -- the drawer's `FlexBox` row (`AlignItems::center`, no
      explicit `.withHeight()` on any item) computed every button/combo/label at zero height,
      since JUCE's `computePreferredSize` falls back to `minHeight` (0.0f) for an unset cross-
      axis size under anything but `AlignItems::stretch`; only the scrim (positioned directly,
      not through the FlexBox) rendered. Fixed by giving every item an explicit height equal to
      the row's own height. Now run in Reaper for real too, which caught a second bug the
      Standalone testing hadn't: `lockButton`/`shuffleButton`/`outputButton`/`settingsButton`/
      `pinButton` originally used emoji glyphs (padlock/shuffle/pin are supplementary-plane
      codepoints, U+1F000+), and JUCE's font fallback did not reliably resolve them on this
      Windows box -- they rendered as nothing, easily mistaken for a missing button, while the
      BMP symbols already in the row (the output surface's ⛶ candidate, ♩ on the BPM badge)
      were fine. Replaced all five with plain text (`Lock`/`Shuf`/`Out`/`Set`/`Pin`) rather than
      mix reliable and unreliable glyphs. A real icon set (drawn `Path`s or an SVG font, not
      relying on the system font's emoji coverage) is the correct long-term fix and folds into
      2.11's already-deferred "collapse to icons at small widths" work. Still not run in any
      other host (Live/FL/Cubase/Logic) -- that is Phase 3.11's DAW checklist, hand-testing work
      Matthew still needs to do per host.
      Update (2026-09-26, Phase 2 pass): the preset label now shows "n/N name" from the
      engine director (the picker itself is still open), the BPM badge shows the engine's beat
      (host or detected, marked "host"), "Out" opens/closes the Output window (3.12) and "Set"
      opens a menu (preset folder, rescan, Output fullscreen, diagnostics). The parameters
      listed above now drive the engine (2.6).
      Update (2026-09-26, "UI Love"): the picker exists. Clicking the preset title opens a
      popup menu (`ui::buildPresetTree`/`addPresetTree`) with the library as a folder tree and
      the current preset ticked, plus choose-folder and rescan at the top. Matthew judged the
      menu enough for the plugin, so this item is done. A browser with search, favourites and
      recents is Phase 4.5.
- [x] 3.4 (S) Transition settings popover: mode selector, bars (N), blend, energy threshold,
      jitter, with sensible defaults (Beat-quantized, 4 bars, soft 2 beats).
      Note (2026-09-26): `ui::TransitionSettingsPanel` (`ui/include/milkdawp/ui/TransitionSettings.h`)
      holds the widgets (mode, bars, timed interval, jitter + min/max, energy threshold, hard
      cuts, blend). Like `ControlDrawer`, it has no APVTS, so the app (Phase 4) can reuse it. The
      editor attaches it to the parameters and opens it from Set > "Transition settings...";
      Close or Esc hides it. It is a child of `OutputSurface` above the drawer, not a
      `CallOutBox`, for two reasons: a desktop window's attachments could outlive the processor
      if a host removes the plugin while the popover is open, and a child composites over the GL
      content the same way the drawer does. Settings that do nothing in the current mode are
      dimmed, not disabled, via the pure `transitionSettingsRelevance()` (unit-tested). New
      parameter `energyThreshold` (0.5-4.0 sd, default 2.0), wired through
      `EngineControls::energyThreshold` to `TransitionSchedulerConfig::energyThresholdMultiplier`,
      which had no control before; `docs/parameters.md` regenerated. The blend default stays
      `softCutDuration`'s v1 3 s rather than "2 beats", since changing a carried-forward v1
      parameter's meaning would break migration. Checked for real in the Standalone build: the
      popover opens from the menu, dimming follows mode and jitter changes, it stays in sync
      with the drawer's mode combo, and Close and Esc both hide it. Not yet run in a host.
- [x] 3.5 (S) Beat/tempo badge in the drawer (BPM, confidence, host-sync indicator), useful
      for trust and for debugging in the field.
      Note (2026-09-26): `ui::describeBeat()` formats the badge: "♩128 host" (green) while the
      host drives, "♩120 80%" for a detected tempo (amber, with an explanatory tooltip, below
      BeatQuantized's 0.3 fallback threshold), and "♩--" (grey) with no beat. There was no
      `TooltipWindow` anywhere, so no drawer tooltip had ever shown. The editor now holds a
      `SharedResourcePointer<TooltipWindow>`, which gives one per process rather than one per
      instance, so editors from several plugin instances don't each draw every tooltip. The
      host and detected states are unit-tested only: the Standalone check had no audio input.
- [~] 3.12 (S) Output window from the plugin: ⛶ opens `OutputWindow` (2.4) on the remembered
      display; editor keeps the live mirror and pinned drawer; closing the editor leaves the
      output window running; removing the plugin closes it. Also v1's remaining OBS nicety,
      the window-transparency option (moved here from 2.4).
      Note (2026-09-26, done alongside Phase 2): the processor owns the `OutputWindow`, so
      closing the editor leaves it running, and destroying the processor (removing the
      plugin) closes it. The drawer's "Out" toggles it windowed; F11 in the editor opens it
      fullscreen or toggles fullscreen (§4.9: a host-framed editor never fullscreens itself);
      the window's own close button closes it through the processor. Built and run in the
      Standalone build only so far. **Not done:** the transparency option; persisting the
      window bounds and fullscreen display in the plugin state (they are remembered only
      while the processor lives); checking it in REAPER (Matthew's hand test).
      Update (2026-09-26): Matthew checked it in REAPER with two instances; both Output windows
      behave. The plugin state now saves the window layout (`core::WindowLayout` in
      `StateSchemaV2`): whether the Output window is open, whether it is fullscreen, and its
      windowed bounds, which also fix the display it goes fullscreen on. These are additive
      keys, so older v2 states load with the window closed. Restoring state reopens, closes,
      or re-fullscreens the window on the message thread through an `AsyncUpdater`, whatever
      thread the host restores on. The processor keeps the layout current under a mutex (never
      locked on the audio thread) through `OutputWindow::onLayoutChanged`, because
      `getStateInformation` may run off the message thread. Saved bounds that land on no
      connected display (a monitor unplugged since) fall back to a default centred on the main
      display. Checked in the Standalone: opened the Output window, quit, relaunched, and it came
      back at the same position. While there, `deserializeStateSchemaV2` stopped throwing on
      malformed input: `std::stoi`/`std::stof` on a corrupt or fuzzed blob (pluginval sends
      those) would have crashed the host, so a bad line is now skipped. Parsing uses the classic
      locale, since a host's decimal-comma locale would misread "3.5". Both have tests.
      **Still not done:** the transparency option. v1 has no window-transparency code (its
      `VisualizationWindow` only had the fixed title and borderless fullscreen), so the
      requirement needs defining before it is built. A layered (per-window alpha) GL window is
      also unreliable on Windows, and OBS window capture ignores it anyway.
- [x] 3.13 (S) Detached controls: "float controls" action hosts the drawer in a small owned
      window; docking returns it. Same component, no duplicated wiring.
      Note (2026-09-26): `ui::DetachedControlsWindow` (`ui/include/milkdawp/ui/`) borrows the
      editor's own `ControlDrawer` as non-owned content and hands it back on dock, so no
      attachment or callback is duplicated. Set > "Float controls in a window" / "Dock
      controls" switches between them, and the window's close button docks. The window is
      only resizable in width; floating, the drawer is always shown, the pin button is hidden,
      and the preset name takes the spare width. The editor owns the window, but whether the
      controls float, and where, is the processor's saved `WindowLayout`, which the editor
      follows. So closing and reopening the editor, or reloading the project, floats them
      again in the same place. Keys in the window go to the editor's shortcut handler (§4.9:
      windows we own receive keys).
      Checked in the Standalone: float, dock, restore across a relaunch, and L/S from the
      floating window. That check found two older drawer bugs. Toggled buttons (Lock/Shuf/Pin)
      looked identical on and off, so they now have a blue "on" colour. Clicking a drawer
      button also moved keyboard focus to it, so Space/Return would press that button instead
      of reaching a DAW's transport; drawer controls no longer take focus. It also found that
      `mapKeyPress` only matched upper-case letter codes. JUCE on Windows gets those from the
      scan code, and injected keys without one (remote desktop, on-screen keyboards) arrive
      lower-case, so letters now match either case (tested). Not yet run in a host.
- [~] 3.14 (S) Shortcuts in the plugin: attach the shared `Shortcuts` table (§4.9) to the
      editor, Output window, and detached controls; unhandled keys fall through to the host;
      verify F11, Esc, arrows, L, S, H, P per host and record results in the DAW checklist.
      If a target host drops keys, try `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` in that host and
      record the trade-off.
      Note: the shared table itself now exists --
      `milkdawp::ui::mapKeyPress(juce::KeyPress, isAppShell)` (`ui/include/milkdawp/ui/Shortcuts.h`)
      -- a pure `KeyPress -> ShortcutAction` function with no window/Component dependency, so it's
      the same kind of deterministic unit `DrawerStateMachine` (2.11) is. Deliberately stops at
      the logical action: what `ToggleFullscreen` or `ExitFullscreenOrRevealDrawer` *does* differs
      per window (plugin editor opens an Output window fullscreen instead of fullscreening
      itself, since a host-framed editor can't), so that routing stays each window's own job.
      Encodes every rule in §4.9's table, including Space being app-shell-only and the
      unmodified-letters-only rule for L/S/H/P. 7 new tests, all environment-independent (no
      window, no host, just `KeyPress` values in and `ShortcutAction` values out). Now attached
      to the editor too (Phase 3.3): `PluginEditor::keyPressed` calls `mapKeyPress(key,
      isAppShell=false)` and handles `ExitFullscreenOrRevealDrawer`/arrows/`L`/`S`/`H`/`P`;
      `ToggleFullscreen` is deliberately left unhandled (returns `false`, so the host still sees
      it) since it needs the Output window (2.4/3.12) this editor can't open yet -- never
      consuming a key it can't act on, per §4.9's own rule.
      Update (2026-09-26): first real per-host verification, in Reaper, once a real VST3 build
      actually loaded there (§4.11's zlib/libpng fix). Initial result: L/S/P worked but H and the
      arrows didn't -- `setWantsKeyboardFocus(true)` alone only makes the editor *eligible* for
      focus, it doesn't *claim* it, so Reaper's own host-provided preset-selector combo (part of
      its FX chain UI, not ours) kept initial focus and our `keyPressed()` never fired for
      anything routed through it. Fixed by actually calling `grabKeyboardFocus()` (once at
      construction, and again from a new `visibilityChanged()` override for when the editor is
      hidden/reshown without being reconstructed, e.g. switching FX chain tabs and back) --
      exactly the "if a target host drops keys" contingency this item's own text names, just a
      focus-claiming bug rather than needing the `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` escalation.
      After that fix: **F11, Esc(reveal), L, S, H, P all confirmed working in Reaper** (H
      specifically confirmed correct once the drawer was actually unpinned first -- it's
      documented as a no-op while pinned, §4.9/2.11, and briefly looked broken before that was
      accounted for). Arrows correctly do nothing yet, as expected -- they pulse
      `triggerPrev`/`triggerNext`, which nothing on the engine side consumes until `PresetLibrary`
      exists (2.6). F11 correctly falls through to Reaper's own fullscreen (never consumed, since
      there's no Output window yet). **Stays `[~]`:** only Reaper tested so far (Live/FL/Cubase
      still needed for the DAW checklist), and the Output window/detached-controls windows still
      don't exist (3.12/3.13) for F11 to have anything real to do.
      Update (2026-09-26): F11 in the editor now opens the Output window fullscreen (or
      toggles it) and is consumed, so it no longer falls through to the host; the arrows now
      change presets (the engine consumes triggerPrev/triggerNext through a parameter
      listener). Needs re-checking in REAPER.
- [~] 3.6 (S) Host transport integration: `AudioPlayHead` → `HostTransport`; verify stop,
      loop, relocate behaviour in two DAWs.
      Note: the wiring is done and unit-tested -- `processBlock` extracts a `core::TransportInfo`
      from `AudioPlayHead::getPosition()` (landed as part of 3.1) and feeds it through
      `core::HostTransport` (Phase 1.7) every block, publishing the resulting `BeatClockState` via
      a second `DoubleBufferedSnapshot` (now `SeqlockSnapshot`, see 3.1), exposed as `currentBeatClock()`. `HostTransport` is
      stateless by design (1.7's own note: "recomputes fresh from the host's ppq every call"), so
      stop/loop/relocate need no special-casing in this class -- that claim is unit-tested in
      core (`HostTransportTests.cpp`, 1.7) and now exercised end-to-end through the processor via
      a fake `AudioPlayHead` in `milkdawp_plugin_tests`. **Stays `[~]`, not `[x]`:** this item's
      own text explicitly asks to "verify ... in two DAWs", and nothing here has run inside an
      actual DAW -- only a fake playhead in a unit test. That verification needs a real plugin
      build loaded into real hosts, which is Phase 3.11's DAW checklist territory.
- [x] 3.7 (S) Enable the JUCE `Standalone` format to get an early app for testing (D8).
      Note: added `Standalone` to `plugin/CMakeLists.txt`'s `FORMATS` list alongside `VST3`, plus
      the same single-zlib/libpng link check the VST3 target already had. Built `MilkDAWp.exe`
      (the Standalone target) for real on this Windows box -- links clean, single-zlib/libpng
      check passes. Did not launch it interactively (a Standalone build opens a real, visible
      window, which isn't something to pop up unannounced), so "first place beat-aligned
      transitions are visible to a human" is still pending real rendering (Phase 2's blocked
      items) -- this item is specifically about the build target existing, which it now does.
- [ ] 3.8 (M) AU target on macOS; `auval` in CI on macOS runner.
- [~] 3.9 (M) `pluginval` job in CI for VST3 (all platforms) and AU (macOS), strictness 5,
      with the runtime dependency layout check from v1 (`check_runtime_win.ps1`) ported.
      Note (2026-09-26): `scripts/pluginval.ps1` downloads pluginval 1.0.4 (version and
      SHA-256 pinned in `toolchain.json`) into `build-tools/` and validates the newest VST3
      build, or `-Plugin <path>`. Run locally on Windows against the dev-identity VST3: it
      passes strictness 10 with the GUI tests on (editor opened while processing, background
      thread state, parameter thread safety, parameter fuzzing), five randomised repeats each,
      in Debug and Release, with no failures. The Windows CI job now runs it at strictness 10,
      3 randomised repeats, without GUI tests (no GPU on the runner), and uploads the log on
      failure. That CI step hasn't run yet, because the CI workflow itself hasn't run (0.3).
      Update (2026-09-27): the Windows CI step now runs and passes (strictness 10, 3 repeats,
      no GUI; `SUCCESS` in run `36320629909`).
      **Not done:** macOS/Linux (the script is Windows-only; macOS goes with the AU in 3.8),
      Steinberg's `validator` (pluginval skips it without `--vst3validator`). The runtime
      layout check landed with 3.10, as a build step rather than a separate script.
      Update (2026-09-27): Linux CI now runs pluginval on the VST3 too (the image's pinned
      pluginval under `xvfb-run`, strictness 10, 3 randomised repeats, no GUI; log uploaded
      on failure). Passed in run `36326021776`: `SUCCESS` on Linux and Windows, both logging
      `MilkDAWp: projectM 4.2.0 loaded`. A plugin that can't find projectM stays inert and still
      passes pluginval, so both pluginval steps now set `MILKDAWP_REQUIRE_PROJECTM`: with it
      set, the processor constructor aborts with the load failure reason when projectM didn't
      load, and otherwise prints `MilkDAWp: projectM <version> loaded`. Checked locally on
      Windows: Release VST3 passes strictness 10 and logs `projectM 4.2.0 loaded`; with
      `projectM-4.dll` renamed away it aborts with the reason and pluginval fails.
      **Still not done:** macOS (with the AU, 3.8), Steinberg's `validator`.
- [~] 3.10 (S) Runtime dependency bundling per platform, ported from v1 (DLL copy, dylib
      fix-up, rpath), now for VST3, AU, and Standalone.
      Note (2026-09-26): `milkdawp_deploy_projectm_runtime()` now copies exactly one file,
      projectM's library, resolved per config from `$<TARGET_FILE:libprojectM::projectM>`
      (vcpkg maps RelWithDebInfo to its Release build) and named the way `ProjectMLibrary`
      opens it (the versioned `.so`/`.dylib` becomes the unversioned name). It used to copy
      vcpkg's whole `bin` directory, which shipped `projectM-4-playlist.dll` (unused) and .pdb
      files next to the plugin, and never removed anything. The Release Standalone folder
      still held `z.dll`/`libpng16.dll`, the exact collision from §4.11. The deploy step now
      deletes any file from a vcpkg `bin` directory it finds next to the binary.
      `milkdawp_check_runtime_layout()` (`cmake/scripts/check_runtime_layout.cmake`, the
      port of v1's `check_runtime_win.ps1`) runs after it on the VST3 and Standalone and fails
      the build unless the folder holds only the binary, its own byproducts (.pdb/.ilk) and
      projectM's library. Because it's a build step, it runs in every local and CI build on
      every platform. Verified on Windows: Debug and Release rebuilds removed the stale files
      and passed the check; a planted `glew32.dll` and a missing `projectM-4.dll` each fail it;
      177/177 tests pass, with the headless render tests actually loading projectM; the
      Release Standalone renders with projectM 4.2.0; pluginval passes on the new VST3.
      `dumpbin /dependents`: projectM and the plugin need only system DLLs and the MSVC runtime
      (`MSVCP140`, `VCRUNTIME140(_1)`), with no third-party DLLs.
      **Not done:** macOS and Linux are written for but unbuilt (dylib install names, rpath,
      and where a signed bundle wants the dylib, `Contents/Frameworks` vs next to the binary,
      go with 3.8 and 6.3); the AU doesn't exist yet (3.8). The MSVC runtime is deliberately
      not bundled: Matthew decided (2026-09-26) that the Windows installer installs the VC++
      Redistributable (6.2), rather than building with the static CRT, which would also
      need a matching projectM triplet. Until the installer exists, a machine without the
      Redistributable can't load the plugin.
      Update (2026-09-27): macOS and Linux now build in CI. The runtime layout check passes on
      their VST3 and Standalone, and the single-zlib/libpng check on those plus `mdw-view`
      and `mdw-analyze`. Built is not loaded, though: whether the dylib install name/rpath and the
      Linux `$ORIGIN` rpath actually let `ProjectMLibrary` find projectM at runtime is still
      unverified, since nothing in CI loads the plugin on those platforms yet (no pluginval
      there, and 2.7 skips). Stays `[~]` for that and the AU (3.8).
      Update (2026-09-27): the Linux half now has a check. The Linux CI job loads the VST3 in
      pluginval with `MILKDAWP_REQUIRE_PROJECTM` set (3.9), so a runtime layout in which the
      plugin can't find `libprojectM-4.so` fails the job instead of passing inert. First run
      (`36326021776`) passed: the bundle holds `MilkDAWp.so` and `libprojectM-4.so` side by
      side, and the plugin loaded projectM 4.2.0 from there. Linux is verified. macOS stays unverified until it gets a pluginval step (3.8).
- [x] 3.11 (S) DAW compatibility checklist doc (`docs/daw-checklist.md`): scan, insert,
      automate every parameter, save/reload, drawer reveal/pin in each host, keyboard
      shortcuts in editor and Output window, output window on second display, close and
      reopen editor with output open, remove plugin.
      Note: written -- a host table (Reaper/Live/FL/Cubase/Logic) plus a 26-item per-host
      checklist covering every bullet this item names, a separate v1-session-loading section
      (blocked on the same real v1 blobs 3.2's note flags), and pointers back from 3.6/3.14's
      own roadmap notes to here for the DAW verification neither of those items could do in this
      sandbox. This is pure documentation -- nothing in it is agent-verifiable without a real
      DAW and a real display, which is the whole reason it exists as a durable, fillable
      checklist rather than something re-derived each time. All rows are currently unchecked;
      filling them in is real hand-testing work for whoever has hosts to test in.

Hand test: the DAW checklist in at least Reaper + one other host on each OS you have. Reproduce
your OBS setup: output
window fullscreen on the capture display, editor with pinned drawer on the other.

### Phase 4 — Standalone application

**Goal:** a real app people would open without a DAW. **Exit:** app launches to a working
visual within 10 seconds on a clean machine with a bundled preset and default input; all
features of the plugin editor are available; preferences persist.

**Shell boundary rules (ADR-0010).** These keep Phase 7 (Android) a port of the shell, not a
rework:
- `milkdawp_ui` holds only what every shell shares.
- The menu bar, single-instance guard, window geometry, file associations, and the
  second-display Output window live in the desktop app shell (`app/`), not in shared code.
- Keyboard shortcuts stay shared, but no action is keyboard-only.
- The preset library root is always a real directory the app can scan; how it gets filled
  (pick a folder on desktop, import or extract into app storage on Android) belongs to the
  shell.
- `SystemAudioCapture` is an interface with one implementation per platform (§4.7).

- [x] 4.1 (M) `milkdawp_app` shell with `juce_add_gui_app`: video-first main window with the
      shared `ControlDrawer` (auto-hide default in fullscreen, pinned otherwise), menu bar with
      the shared `Shortcuts` table (§4.9) plus app-only `Space`, single-instance guard. Main window can fullscreen directly; ⛶ opens the `OutputWindow`
      for a second display; "float controls" for the projector-plus-laptop setup. Per the
      boundary rules above, the menu bar and single-instance guard are desktop-shell code.
      Note (2026-09-27):
      - Structure: `app/` is now `milkdawp_app_lib` (everything testable) plus `Main.cpp`.
        `MainComponent` is the shell-neutral content: `OutputSurface` with the drawer,
        transition popover, diagnostics and input hint as children. Every action is a
        public method, shared by the drawer, the shortcuts, the settings popup and the menu
        bar. `MainWindow` is the desktop part: menu bar (File / Playback / View, with shortcut
        hints; macOS uses the system menu bar), kiosk-mode fullscreen with the menu bar
        hidden, and geometry.
      - The plugin's parameter→engine mapping moved to `engine::toEngineControls` over
        `engine::ParameterValues`, so both shells share one mapping (`ControlMappingTests`
        pin its defaults to `ParameterModel`).
      - The app binds the shared widgets through `ParameterBinding`, its stand-in for APVTS
        attachments, with ranges from `ParameterModel`.
      - Built by default now (`MILKDAWP_BUILD_APP=ON`, as ADR-0003 planned). Identity
        follows D1; dev-identity builds are "MilkDAWp2 Dev" with their own settings folder.
      - F11 fullscreens the Output window if it is open, else the main window; Esc leaves
        fullscreen. The drawer unpins in fullscreen and returns to the user's pin choice
        after.
      - Verified on Windows: menus, F11/Esc round trip at 1920×1080 60 fps, and a second
        launch bringing the running window forward.
- [x] 4.2 (M) Audio input: `AudioDeviceManager` device selector, input channel pair choice,
      level meter, "no signal" hint. Startup restores the last device; graceful fallback when
      it is missing.
      Note (2026-09-27):
      - `AudioInput` opens up to one stereo input pair and no outputs, so nothing can feed
        back. Its callback hands blocks to `Visualizer::processAudio`, as a plugin would.
        A saved device that is gone falls back to the default input (JUCE's
        `selectDefaultDeviceOnFailure`).
      - File > Audio input... is JUCE's `AudioDeviceSelectorComponent` (inputs as stereo
        pairs) with a live meter and the current input described.
      - `SignalMonitor` (unit-tested) drives the hint over the video: "No audio input" at
        once, or "No signal from <device>" after 3 s under -60 dBFS; either one opens the
        dialog. Each reader of the input peak has its own slot, so the meter and the hint
        don't steal each other's peaks.
      - Verified with a USB microphone on Windows.
- [x] 4.3 (S) Preferences (`PropertiesFile`): device, preset library root, output display,
      quality, logging toggle, last window geometry. (`PropertiesFile` works on Android too;
      output display and window geometry are desktop-only keys.)
      Note (2026-09-27):
      - `AppState` ↔ `PropertySet` in `AppPreferences` (unit-tested round trip; saved
        parameters are clamped to the model's ranges on load).
      - Stored in `<app data>/MilkDAWp/app.settings`, saved a second after each change and
        on quit.
      - Covers every engine parameter (quality included), preset folder and current preset,
        device XML, main window bounds and fullscreen, Output window open/bounds/fullscreen
        (the bounds pick the display), floating controls and their bounds, drawer pin,
        diagnostics, and logging (File > Write a log file, via `juce::FileLogger`; File >
        Show log file).
      - Verified by relaunching: preset, mode, folder, Output window and fullscreen all come
        back as left.
- [x] 4.4 (M) MIDI learn: map CC/notes to any parameter; persisted; UI affordance on each
      control.
      Note (2026-09-27):
      - `MidiLearn` (new, `app/src/MidiLearn.{h,cpp}`) listens on every enabled MIDI input
        (no per-device picker; out of scope here) and maps CC/note-on messages to a
        parameter id, persisted in `AppState.midiMappings` (one line per mapping) through
        `stateString()`/`restoreFromState()`.
      - The UI affordance lives at the one chokepoint every bindable widget already goes
        through: `ParameterBinding::bind()`. `ParameterBinding` is now also a
        `juce::MouseListener` on each widget it binds; right-click shows "MIDI Learn..." /
        "Clear MIDI mapping", and every bound widget's tooltip shows its current mapping (or
        "Right-click to map a MIDI control", or "Listening..." while armed) via
        `refreshMidiLearnTooltips()`.
      - **Real-hardware finding, not just a design choice:** enabling a MIDI input
        (`AudioDeviceManager::setMidiInputDeviceEnabled`) blocked forever against a real USB
        MIDI interface on this machine (a MOTU box) -- confirmed with timing diagnostics
        before removing them. `enableAllInputs()` therefore runs on its own **detached**
        background thread, checking an `alive` flag between devices; the destructor does not
        join it, since joining could hang app shutdown on the same stuck driver call.
        This also fixed a second problem: JUCE's Windows MIDI enumeration call needs the
        message loop pumped to complete, which isn't running yet inside
        `JUCEApplication::initialise()` (where `MidiLearn` is constructed) and never runs at
        all in a console unit test -- moving the call off that thread entirely sidesteps both.
      - Verified against real hardware: app stays responsive at launch (was hanging
        indefinitely before the fix); `app_tests` runs in seconds instead of hanging forever.
        `MidiLearnTests.cpp` covers the pure state machine (arm/cancel, mapping table
        round-trip through `stateString`/`restoreFromState`, malformed-line handling); nothing
        exercises `handleIncomingMidiMessage` itself, since CI has no MIDI hardware.
      Update (2026-10-03): that background enable thread was a race. It called
      `juce::MidiInput::getAvailableDevices()` and `AudioDeviceManager::setMidiInputDeviceEnabled`
      / `add|removeMidiInputDeviceCallback`, all message-thread JUCE APIs (an unlocked shared
      device table and device vector, and a `sendChangeMessage` that re-entered `MidiLearn`
      and started another thread), while the message thread used the same ones. It showed as
      the "flaky" `[MidiLearn]` tests: SIGSEGV or a debug-heap `_CrtIsValidHeapPointer`
      assertion in 7-9 of 80 runs, one of which left a modal CRT dialog waiting for a click.
      Under cdb, JUCE's `JUCE_ASSERT_MESSAGE_THREAD` fired on the first run. Fix: `MidiLearn` no
      longer goes through `AudioDeviceManager` (constructor is now `MidiLearn(ParameterBinding&)`).
      It owns its `juce::MidiInput`s. The message thread lists the devices (which builds JUCE's
      table) and adopts and starts each input. Only `MidiInput::openDevice`, the call a driver
      can block, runs in the background. Hot-plug comes from `juce::MidiDeviceListConnection`,
      and unplugged inputs are closed and can be reopened. The destructor waits up to 2 s for
      in-flight opens, so none outlives the object or a test's JUCE shutdown, without letting a
      hung driver block quitting. Result: 0/100 MidiLearn-only and 0/100 full app-suite runs
      failed, against 7-9/80 before; full `ctest` 331/331. **Known deviation:** JUCE also marks
      `openDevice` message-thread-only, so a Debug build under a debugger breaks once per MIDI
      device at launch. Its lookups only read the table just built on the message thread;
      JUCE rewrites it only on a plug/unplug, so a hot-plug in the same millisecond as an open is
      the remaining window. The MOTU interface that hung is no longer attached (only the "USB
      Uno MIDI Interface": open 0.5 ms, start 0.3 ms), so whether `openDevice` or `start()`
      was the blocking call is unconfirmed. If it was `start()`, opening could move back to the
      message thread and the deviation would go away. Test executables now link
      `cmake/TestNoCrashDialogs.cpp` (`milkdawp_test_no_crash_dialogs`), so on Windows a
      CRT assert, `abort()` or crash prints to stderr and fails the test instead of opening a
      modal dialog.
- [x] 4.5 (M) Preset library browser: tree of the library root, search, favourites, recently
      played, right-click add to blacklist.
      Note (2026-09-27):
      - `PresetBrowserPanel` (new, `app/src/PresetBrowserPanel.{h,cpp}`, opened via File >
        "Browse presets..."): a search box, All/Favourites/Recent tabs, and a `juce::TreeView`
        built from `ui::buildPresetTree`, which gained a `(name, index)`-pairs overload so a
        filtered view (Favourites/Recent, both flat and most-relevant-first rather than
        grouped by folder) keeps the *real* playlist index rather than its position in the
        filtered list. Pull-based like `AudioSettingsPanel`: polls on a 2 Hz timer, only
        rebuilding the tree when the underlying data actually changed, so an open folder or
        scroll position survives ticks where nothing did.
      - Favourites and recently-played are app-level state (`AppState.favouritePresets` /
        `.recentlyPlayedPresets`, both keyed by absolute path, the latter bounded to
        `kMaxRecentlyPlayed` = 20 and recorded in `MainComponent::timerCallback` whenever the
        director's current preset path changes).
      - The blacklist reuses `PresetLoader`'s existing (already-implemented, §2.5) blacklist
        rather than inventing a browser-only flag, so right-click "blacklist" actually keeps
        playback off a preset, the same as a preset projectM itself rejects. Reaching it from
        the UI thread needed new plumbing: `Director::blacklistPreset`/`unblacklistPreset`
        queue the request (`pendingBlacklistOps_`, drained once per director-thread loop
        tick) and `blacklistedPaths()` publishes a snapshot; `Director::presetPath(s)` mirrors
        `presetName(s)` so the browser can key by absolute path, which a playlist index
        doesn't survive a rescan.
      - Tests: `ui/tests/PresetMenuTests.cpp` (the pairs overload),
        `engine/tests/PresetLoaderTests.cpp` (`blacklistedPaths()`),
        `engine/tests/DirectorTests.cpp` (paths mirror names; blacklist round-trips) -- the
        last needs no projectM/GL, since the folder scan and the blacklist queue both run on
        the director thread regardless of whether a surface can render.
- [x] 4.6 (S) File associations and drag-and-drop for `.milk` files and folders
      (desktop-shell code, per the boundary rules).
      Note (2026-09-27):
      - `MainComponent::openPath()` (new) opens a `.milk` file (its folder, with that file
        preferred) or a folder, shared by three entry points: `MainComponent` now implements
        `juce::FileDragAndDropTarget` (drop a file or folder onto the main window); `Main.cpp`
        passes `initialise()`'s command line through on first launch and
        `anotherInstanceStarted()`'s through `MainWindow::content()` (new accessor) on a
        second one (double-clicking a second `.milk` file brings the single-instance window
        forward and opens it, rather than starting a second engine).
      - `juce_add_gui_app(... DOCUMENT_EXTENSIONS "milk")` adds the macOS Info.plist
        document-type metadata for free. Windows registry / Linux desktop-file association
        registration is installer work (§9, not started) -- Main.cpp already handles whatever
        path that hands it either way, so nothing here blocks on it.
- [x] 4.7 (L) Windows WASAPI loopback capture module behind `SystemAudioCapture`, selectable as
      "System audio" in the device list. Defines the interface: start/stop, a permission
      state the UI can explain ("needs permission", "denied", "unsupported on this OS"), and
      audio delivered into the same `AudioRing` path as a device input. Phase 7's Android
      module implements it too, so nothing Windows-specific goes in the interface.
      Note (2026-09-27):
      - `SystemAudioCapture` (`app/src/SystemAudioCapture.h`) is a small interface
        (open/close/isOpen/permissionState/describe/takePeak), with one implementation file per
        platform picked by `app/CMakeLists.txt` at build time and a `createSystemAudioCapture()`
        factory that is never null: `SystemAudioCaptureWindows.cpp` (WASAPI loopback, real COM
        against the default render endpoint, its own MTA capture thread) on Windows, and
        `SystemAudioCaptureUnsupported.cpp` (always `PermissionState::Unsupported`) everywhere
        else until 4.8/4.9. Windows needs no consent for loopback, so its `permissionState()` is
        always `Granted`; failures surface as text through `describe()`/`open()`'s return value.
      - `AudioSourceRouter` (new, `app/src/AudioSourceRouter.h`) picks between `AudioInput` and
        `SystemAudioCapture` so exactly one feeds the `Visualizer` at a time -- both write into
        the same ring, so running both would mix two unrelated streams into it. `MainComponent`
        and `AudioSettingsPanel` now hold the router instead of `AudioInput` directly.
      - `AudioSettingsPanel` gained a "Capture system audio (loopback) instead of a device"
        toggle above the existing device selector; picking it closes the device and opens
        system-audio capture (and vice versa), and it is greyed out with a tooltip when
        `permissionState() == Unsupported`. `AppState.useSystemAudio` (persisted) remembers the
        choice across launches.
      - `PeakReader` (meter/monitor) moved out of `AudioInput` into a shared
        `app/src/AudioPeakReader.h`, since `SystemAudioCapture` needed the same two-reader peak
        tracking (`SystemAudioPeakTracker`, in `SystemAudioCapture.h`) over its own capture
        thread.
      - Verified on Windows (dev-identity build): app launches on the device input as before;
        opening File > Audio input..., checking the new toggle closes the device
        (`AudioDeviceSelectorComponent` shows `<< none >>`) and starts real WASAPI loopback --
        the dialog's status line and the app's diagnostics overlay both read "System audio
        (loopback)". Unchecking it reopens the last device. App quits cleanly from either mode
        (the capture thread is joined in `close()`). `functiondiscoverykeys_devpkey.h`
        (`PKEY_Device_FriendlyName`, for a nicer status string) triggers `DEFINE_PROPERTYKEY`
        redefinition errors under `INITGUID` with this SDK (10.0.26100.0); dropped rather than
        chased -- the status line just says "System audio (loopback)" without the device name.
      - Not done here: macOS process taps (4.8) and the Linux verification pass (4.9) still
        return the Unsupported stub; Android's implementation is Phase 7 per ADR-0010.
- [ ] 4.8 (L) macOS system audio capture via Core Audio process taps (14.2+) with
      ScreenCaptureKit fallback (13+); permission flow and messaging.
- [ ] 4.9 (S) Linux: verify PipeWire/Pulse monitor sources appear; document.
- [x] 4.10 (S) Crash reporting hooks (local minidump/log bundle) and "collect logs" menu item.
      Note (2026-10-03):
      - `RecentLog` (`app/src/RecentLog.h`) is now the app's logger from `initialise()` to
        `shutdown()`. It timestamps every line, keeps the last 500 in memory, and writes to the
        log file only while File > Write a log file is on. A crash report or log bundle always
        has the recent lines, even with file logging off. `tryGetLines()` doesn't wait for the
        lock, so a crash on a thread that was logging can't deadlock the report.
      - `CrashReporter` (`app/src/CrashReporter.h`) installs JUCE's crash handler (unhandled SEH
        exceptions on Windows, fatal signals elsewhere) plus `std::terminate`. A crash writes
        `crash-<date>_<time>.txt` (reason, OS/CPU/memory, stack trace, recent log) to a
        `Crashes` folder beside `app.settings`. On Windows it also writes a `.dmp` minidump
        (`MiniDumpWriteDump`, thread info, unloaded modules; links `dbghelp`), and it writes the
        minidump first because it needs the least from a damaged process. On macOS/Linux the
        report is written from a signal handler, so it's best effort. A `session.running` marker
        exists while the app runs. The next launch finding it means an unclean exit, and a report
        newer than it means a crash; only the crash case prompts "closed unexpectedly", with
        [Collect logs...] [Not now]. Keeps the newest 10 reports.
      - File > Collect logs... (`LogBundle`, `app/src/LogBundle.h`) writes one zip wherever the
        user picks (default: Desktop, "MilkDAWp logs <date> <time>.zip"). It holds the log
        file (if one exists), `app.settings`, the newest 5 crash reports with minidumps, and
        `diagnostics.txt` (version, system, the diagnostics overlay's text, now
        `MainComponent::diagnosticsText()`, and the recent log). It writes through a
        `TemporaryFile`, so a failure leaves any existing zip untouched. Nothing is uploaded.
        File > Show log file no longer depends on the current logger being a `FileLogger`.
      - Debug builds take `--simulate-crash` (a null write 2 s after the window is up) to check
        the whole path on each OS.
      - Tests: `RecentLogTests` (capacity, timestamps, file only while set),
        `CrashReporterTests` (report contents incl. a real minidump on Windows, clean vs
        crashed vs killed previous session, pruning), `LogBundleTests` (zip contents, missing
        files skipped, duplicates stored once).
      - Verified on Windows (dev identity, Debug): `--simulate-crash` exits with 0xC0000005 and
        leaves a 2.4 KB report plus a 340 KB minidump. The report's stack shows the faulting
        lambda under the timer callback, and its recent log has the startup lines. Relaunching
        shows the prompt. Collect logs saves a zip with `app.settings`, both crash files and
        diagnostics (projectM 4.2.0, fps, GL vendor/renderer). A clean quit removes the marker.
        Full `ctest`: 285/285.
      - Not done: macOS/Linux runs of `--simulate-crash` (no Mac here; Linux needs a desktop).
        The plugin installs no crash handler on purpose: the process belongs to the host.
- [x] 4.11 (S) Retire the JUCE `Standalone` wrapper format once 4.1–4.3 land (or keep it as a
      dev convenience behind a CMake option).
      Note (2026-10-03): kept as a dev convenience. The plugin's Standalone is still the
      quickest way to run the plugin editor (drawer, Output window, layers) without a DAW, and
      most of Phase 3's checks used it. New option `MILKDAWP_PLUGIN_STANDALONE` (default ON)
      adds `Standalone` to the plugin's `FORMATS` and runs the zlib/libpng, projectM-deploy and
      runtime-layout checks for that target. `release-win`/`release-mac`/`release-linux` set
      it OFF, so packaging builds ship the VST3 (and AU later) plus `milkdawp_app` only. Dev
      and CI presets keep it ON so it keeps compiling. Checked by configuring both ways: no
      `milkdawp_plugin_Standalone` target when OFF, and a full Debug build when ON.

Hand test: fresh user account on each OS: install, launch, play music from a browser, confirm
visuals react without configuring anything (Windows/macOS via loopback, Linux via monitor
source).

### Phase 5 — Musicality, performance, and polish

**Goal:** the features that make v2 noticeably better than v1 in a live setting. **Exit:**
energy mode demonstrably cuts on drops in the fixture set; adaptive quality keeps ≥55 fps at
1080p on an integrated GPU with the bundled pack; 4-hour soak test passes with flat memory.

- [x] 5.1 (M) Energy / section-change transition mode: rolling energy percentile, drop
      detector, build-up detection (optional), cooldown in bars.
      Note (2026-10-03):
      - The 1.12 rule was wrong in steady music. It called a hop a drop when broadband RMS
        was 2 standard deviations over its 8 s mean and a bass onset landed. Every kick in a
        steady section clears that, so the mode hard-cut every `energyCooldownBars` whatever
        the music did. Measured on the fixtures before replacing it: 16 "drops" in
        `four_on_the_floor` (all 16 kicks), 12 in `syncopated`, 17 in `tempo_change`, 8 in
        `sparse_acoustic`.
      - New `core::SectionDetector` (`core/include/milkdawp/core/SectionDetector.h`): pure, one
        call per hop, buffers sized up front. It works on the **bass** band
        (`AnalysisFrame::bassEnergy`), since that is what a breakdown or build-up removes and a
        drop brings back. Broadband energy also rises through a riser. The level is the bass
        averaged over 0.5 s. The **rolling percentile** is the loud reference: the 80th
        percentile of that level over 60 s, sampled every 0.25 s. A **drop** is a bass onset
        where the bass jumps `jumpDb` above the *loudest* level of the 2.5 s before (the last
        0.5 s left out, so a fill, kick roll or riser's last beat doesn't hide it) and comes
        back within 6 dB of the loud reference. Comparing with the loudest moment rather than
        the mean is what makes steady kicks, sparse hits and tempo changes never qualify.
        There is one drop per breakdown: the kicks after a drop still see the breakdown behind
        them. Music starting after silence counts as a drop; anything under an energy floor
        doesn't. `SectionFrame::breakdown` reports "a drop can happen now".
      - `TransitionScheduler` Energy mode: a hard cut on each drop outside the cooldown (bars).
        The rest of the time it behaves like BeatQuantized, now including its low-confidence
        fallback to Timed, which Energy mode used to skip. The beat-quantized count restarts
        at the drop, so the next regular cut is exactly `bars` after it, and a grid-anchored
        cut inside the cooldown is dropped. The detector runs in every mode, so switching to
        Energy starts with history. `tick()` now takes the bass energy (Director passes
        `frame.bassEnergy` instead of `broadbandRms`). `lastSection()` exposes the frame.
      - The `energyThreshold` parameter keeps its id, 0.5-4 range and default 2, so saved
        projects and automation still load. Its meaning changes to `jumpDb = 4 + 3 x value`
        (5.5-16 dB, default 10). The bottom stays above a steady kick's ~4.4 dB swing over
        its own average. Its label loses "(sd)" (`docs/parameters.md` regenerated), and the
        tooltip is rewritten.
      - Exit criterion ("energy mode demonstrably cuts on drops in the fixture set"): new
        fixture `fixtures/breakdown_drop` (128 BPM: 2 full bars of kick, bassline and hats, a
        2-bar breakdown with a pad and a noise riser, then the drop on beat 17 at 7.969 s), with
        a new optional `drops.txt` annotation. `mdw-analyze --suite` now requires every
        fixture to find exactly its annotated drops (none without the file) within
        `dropToleranceSeconds` (0.15). Result: drop found at 7.979 s (10 ms late), 0 drops in
        every other fixture, all 7 pass (also in CI's Linux suite job).
      - The fixture generator gained the new fixture at the end of its run, so it doesn't
        shift the RNG for the others. Regenerating on MSVC changes the other WAVs' bytes
        anyway (`std::uniform_real_distribution` differs between standard libraries; beats
        are identical), so only `breakdown_drop` was copied in. `fixtures/README.md` documents
        `drops.txt` and this.
      - The app's diagnostics overlay shows the bass level, the loud reference, "breakdown"
        and a drop count (`DirectorStatus`), for the Phase 5 hand test.
      - Tests: `SectionDetectorTests` (10 cases: steady kicks, breakdown then drop, the
        breakdown flag, sparse hits, a soft hit in a breakdown, onset required, one drop per
        breakdown, silence then music, near-silence, threshold). Energy cases in
        `TransitionSchedulerTests` rewritten around realistic envelopes: no hard cuts on 70 s
        of steady kicks, one hard cut on the drop with the next regular cut exactly 4 bars
        later, and the cooldown suppressing a second drop 6 s later (both get through with a
        1-bar cooldown). Full `ctest`: 297/297.
      - **Not done:** build-up detection (optional in this item). A build-up is visible as
        `breakdown` with broadband energy rising, but nothing uses it yet. "Only *calm* during
        breakdowns" is 5.2/post-1.0 anyway. Known limit: a build-up whose kick roll runs
        right up to the drop for longer than the 0.5 s guard hides that drop (the quiet
        stretch isn't quiet). Real-music checking is the Phase 5 hand test (Matthew), not
        done.
      Correction (2026-10-03, during 5.2): the app's diagnostics showed a "drop" with nothing
      playing. Room noise on the mic (bass about -70 to -120 dB) jumped over the first floor
      (1e-7), and in a silent room the loud reference is silent too. Measured on the fixtures,
      a kick near full scale is about 1.5e5 in `bassEnergy` units (+52 dB), so the floor is
      now 1.0 (0 dB, about bass at -52 dBFS). That is far above room noise and below any real
      track. A new test covers room-noise blips. The detector tests now build their tracks at
      kick-like levels, and the fixture suite is unchanged (7/7, same drop time). The overlay
      also printed "-71.6933 dB" because `juce::String(float, 0)` means default precision;
      it now rounds.
- [x] 5.2 (M) Weighted shuffle with ratings and tags; per-preset "never auto-select"; tag
      filters in the transition settings ("only *calm* during breakdowns" is post-1.0).
      Note (2026-10-03):
      - `core::PresetMetadata` (`core/include/milkdawp/core/PresetMetadata.h`): rating (0
        unrated, 1-5), "never auto-select" and tags (lower case, trimmed, unique) per preset.
        It is keyed by **file name**, case-insensitive, not by path. Moving or re-rooting a
        preset folder keeps the ratings, and the same preset in two packs (common with MilkDrop
        packs) shares one. Text format, one preset per line (`rating\tflags\ttags\tfile
        name`); malformed lines are skipped. Weight: unrated 1, then each star doubles it
        (1 star 0.25, 3 stars 1, 5 stars 4).
      - `engine::PresetMetadataStore`: the file `<user data>/MilkDAWp/preset-metadata.txt`
        (`MilkDAWp2 Dev` in dev builds), one store per process (`shared()`), shared by the
        app and every plugin instance, since ratings describe the user's library, not a
        project. It rereads the file when another process changes it (mtime, checked at most
        once a second) and before each write, so the app and a DAW don't erase each other's
        edits (last writer wins per preset). Writes go through a `TemporaryFile`.
      - `Playlist`: entries carry `autoSelect`. Sequential, Shuffle and Weighted skip ineligible
        entries on advanceNext/advancePrevious; `setCurrentIndex` (a pick by hand) still plays
        them. If nothing is eligible, everything is, so a filter that matches nothing never
        stops playback. Weighted now gets its weights from ratings.
      - `Director::setPresetMetadata` / `setTagFilter`. The director reapplies weights and
        eligibility whenever the store's generation, the filter or the playlist changes.
        `DirectorStatus` gains `autoSelectable` and `tagFilterMatchesNothing`. "Never
        auto-select" applies in every policy and to next/previous; ratings only matter under
        Weighted.
      - UI (`ui/PresetInfoMenu.h`, shared): clicking the preset title opens the picker, which
        now has a section for the current preset: Rating submenu (stars plus "4 stars", since
        the glyphs are small in the drawer font), "Never auto-select", "Tags..." (a small
        dialog listing tags in use). The app's library browser right-click menu has the same
        items. Transition settings gains an "Only tags" row (comma-separated, applied as
        typed) with a status ("12 of 340 presets", or an orange "no preset has these tags").
        The filter is per instance: saved in the app's preferences (`tagFilter`) and in the
        plugin state (`StateSchemaV2::tagFilter`, an additive key; older states have none).
      - Tests: `PresetMetadataTests` (key by file name, normalising, weights, filter,
        round-trip incl. CRLF, malformed lines), new `PlaylistTests` (sequential skips,
        shuffle/weighted never pick excluded presets even at weight 100, picks by hand, the
        nothing-eligible fallback, previous, single eligible), `PresetMetadataStoreTests`
        (missing file, write/read across stores, unchanged write, two writers keep both
        edits, an external change is picked up, `shared()`), a Director end-to-end test (real
        GL and projectM: next skips the excluded preset, a pick by hand plays it, a tag filter
        narrows to one, a no-match filter is ignored and reported, a rating change elsewhere
        reaches the playlist), `PresetInfoMenuTests`, `describeAutoSelection`, app preference
        and state-schema round-trips. Full `ctest`: 322/322.
      - Checked in the app (Windows, dev identity): the picker shows the section, choosing
        "4 stars" writes `4\t\t\tmdw-wave.milk` to the file, and the "Only tags" row shows
        "all 3 presets", then "no preset has these tags" for a tag nobody has. Test data
        removed afterwards. The plugin editor uses the same code (picker section, tag row,
        state key) and builds, but wasn't checked in a host.
      - Not done: showing ratings in the browser tree, and tag *weights* (tags only filter).
        "Only *calm* during breakdowns" stays post-1.0 as written; `SectionFrame::breakdown`
        (5.1) is the hook it would use.
- [x] 5.3 (M) Adaptive quality on the real FBO with GPU-time-driven hysteresis and a manual
      override; visible current-scale indicator.
      Note (2026-10-03):
      - `core::AdaptiveQuality` (`core/include/milkdawp/core/AdaptiveQuality.h`): pure, one call
        per rendered frame. Discrete scales (100/85/70/55/40 %), because each change
        reallocates the frame buffers and shows as a jump, so fewer and larger changes look
        better. The cost is the GPU time (`GL_TIME_ELAPSED`, summed over layers), or the CPU
        frame time (which includes `glFinish`) when the query reports nothing, smoothed with a
        0.25 s time constant. **Hysteresis:** step down when the smoothed cost stays above 85% of
        the frame budget for 0.5 s; far over budget, it jumps straight to the first step
        predicted to fit within 70%. Step up only when the next step up is predicted under
        55% for 3 s. The prediction scales with pixel count, which overestimates since
        projectM's per-vertex work doesn't scale, so steps up stay cautious. After any change
        it ignores frames for 1 s, because the first frames at a new size cost more. A
        one-frame spike (a preset load) changes nothing.
      - `RenderEngine`: `setQualityScale(<= 0)` now means Auto. The render thread runs the
        controller against `config.fps` and applies its scale the next frame; choosing Auto
        again starts from full scale. `qualityScaleFor` maps the `qualityOverride` choices
        to Auto 0 / Low 0.5 / Medium 0.75 / High 1.0. Before this, Auto meant a fixed 1.0.
        `RenderStats` gains `qualityScale` and `qualityAuto`.
      - **Manual override:** a Quality submenu (Auto / Low / Medium / High) in the app's View
        menu and the plugin's settings menu. Until now `qualityOverride` had no UI at all,
        only host automation in the plugin. **Indicator:** the drawer's preset detail line adds
        "render 70%" (or "render 50% (fixed)") whenever the scale is below 100%, using
        `ui::describeRenderQuality`, without a new drawer widget. Both diagnostics overlays
        show "quality N% (auto|fixed)".
      - Tests: `AdaptiveQualityTests` (7 cases against a fixed-plus-per-pixel GPU model: a light
        load stays at 100%; an overloaded GPU drops in at most two changes to a scale that fits
        and no lower than needed; recovery steps up one at a time at least 3 s apart; a
        one-frame spike; the CPU fallback; a 30 fps target; reset). Also a `RenderEngine` test
        with real GL (Auto reports itself at 100% on a light frame; a fixed 0.75 resizes the
        FBO to 240 wide and reports "fixed"), `describeRenderQuality`, and the ControlMapping
        Auto value. Full `ctest`: 331/331.
      - Checked in the app (Windows, RTX 4070 Ti): the overlay reads "quality 100% (auto)".
        View > Quality > Low gives a 640x348 frame (visibly coarser), "quality 50% (fixed)" and
        "3 / 3 · render 50% (fixed)" in the drawer. Set back to Auto afterwards.
      - **Not verified:** that Auto actually steps down in real use. This GPU renders the test
        presets in about 0.5 ms (budget 16.7), so it never needs to. The phase exit criterion
        (>= 55 fps at 1080p on an integrated GPU with the bundled pack) needs an iGPU run and
        the bundled pack (6.1); that is Matthew's hand test. Forcing this box's Intel iGPU
        would mean changing Windows' per-app GPU preference, so I left it alone.
- [x] 5.4 (S) Preset load hitch mitigation: measure per-preset compile time, cache it, and
      prefer cheap presets when the scheduler needs a hard cut on the next beat. This is the
      **primary** hitch fix. projectM compiles preset shaders synchronously on the GL thread in
      every version (4.1.7 and 4.2 alike, no async load API), so 2.5's file prefetch can't hide
      that cost. If measurements show it's still bad, consider contributing async or
      parallel shader compilation upstream (§10).
      Note (2026-09-27):
      - `PresetCompileTimeCache` (new, `engine/include/milkdawp/engine/PresetCompileTimeCache.h`,
        header-only): last-measured load time per absolute path. `Director::run()` already had
        the measurement -- `RenderStats::lastPresetLoadMs` (projectM parse + shader compile, on
        the render thread) -- just nowhere to keep it; the director thread now polls
        `render_.stats()` once per loop and records `(path, lastPresetLoadMs)` whenever
        `currentPresetId` changes, keyed by path via the existing `PresetLibrary::pathFor`.
      - `step()`'s attempt loop uses the cache only for a hard cut (`CutStyle::Hard`) under a
        *randomized* policy (ShuffleNoRepeat/Weighted): scans the same candidates it already
        would, and switches to one confirmed at or under 20 ms instead of the first valid one,
        falling back to that first valid pick when nothing scanned is confirmed cheap. Sequential
        policy is deliberately excluded -- it's a user-visible order, and a first attempt that
        used raw cost comparison ended up jumping back to the already-measured *current* preset
        instead of advancing, since an unmeasured next entry has no cost to compare against yet;
        `DirectorTests.cpp`'s existing Sequential-mode test caught this immediately. The fix:
        only ever prefer a *confirmed*-cheap candidate over the default first-valid pick, never
        an unmeasured one over a measured one in either direction, and never touch Sequential's
        order at all.
      - `PresetCompileTimeCacheTests.cpp` (new) covers the cache in isolation. No new
        Director-level test asserts the preference itself (real GL load timings are too
        variable to assert an exact pick deterministically); the existing Director tests (which
        exercise Hard cutStyle end to end) pass unchanged and are the regression guard.
      - Verified on Windows: `milkdawp_engine_tests` (52 cases/633 assertions) and the full
        project's `ctest` (212 tests) pass; VST3, Standalone, and app targets still build clean.
      Follow-up note (2026-09-27, same day): the freeze was still visibly happening after the
      above landed -- reported as smooth animation up to a preset switch, then all animation
      stopping for roughly half a second to a second, on transitions the fix above never
      touched. Root cause of the gap: `RenderEngine::run()` calls `loadPresetData()` inline on
      the render thread with no rendered frames produced until it returns, on *every* cut style
      -- the "soft/timed cuts don't bother, since the blend hides a hitch either way" reasoning
      above was wrong. projectM's blend only blends *rendered frames* after loading finishes;
      no frames render at all during the synchronous compile, so a soft cut stalls exactly like
      a hard cut. (A real fix -- a second live `ProjectMInstance` loading off the render thread
      entirely, crossfaded in once ready -- was scoped and then set aside: making it correct
      requires a symmetric dual-pipeline where either thread's context can become the one
      publishing frames, since a `ProjectMInstance`'s internal VAOs/FBOs are not shareable
      across GL contexts and so can never be handed from the thread that created it to another
      one; that's a bigger change than this pass, left for a future session.) The narrower fix
      landed instead: `step()`'s cost-aware candidate scan no longer checks `cutStyle` at all,
      only `playlist->policy()` -- every cut style now benefits from steering toward a
      confirmed-cheap candidate under a randomized policy, not just hard cuts. Sequential is
      still excluded, unchanged, for the reason above (order is user-visible). This narrows how
      often the freeze is hit but does not eliminate it: it still happens on the first-ever load
      of any preset in a session, and whenever no cheap alternative has been measured yet.
- [x] 5.5 (S) Beat sensitivity semantics: one knob that scales both our detector's threshold
      and `projectm_set_beat_sensitivity`, documented. The two are different things:
      projectM's value only rescales the bass/mid/treb levels presets animate from (clamped
      0–2). It detects no beats and triggers no transitions. Document the knob as "how hard
      visuals react + how easily we cut", or split it into two parameters if the coupling feels
      wrong in hand tests.
      Note (2026-10-03): **split, on measurements rather than hand tests.** The coupling was
      tried in the fixture suite: the bass onset detector's threshold multiplier was swept over
      the range a 0-2 knob would cover (5.0 / 3.5 / 2.5 / 1.8 / 1.25). Only the current 2.5
      passes everything. At 1.8 and 1.25 `breakdown_drop`'s beat tracking falls to F=0, and at
      5.0 `four_on_the_floor` and `breakdown_drop` fall to F=0 and `tempo_change` fails. That
      threshold sets beat-phase correction and Energy drops, so a user knob on it would mostly
      break tracking. Outcome:
      - `beatSensitivity` stays projectM-only, unchanged (id, range, default, v1 alias), and is
        documented as "how hard visuals react".
      - "How easily we cut" is the transition mode's own settings: Bars, the timed interval,
        and Energy mode's Energy Threshold (5.1). No new parameter was needed.
      - The parameter had no UI at all (host automation only). Both shells now show it as
        **Reactivity** in the shared Transitions panel, with a tooltip saying it doesn't change
        when transitions happen. The panel is one row taller.
      - `docs/parameters.md` gains a generated Notes section (from
        `tools/generate-param-docs`) on Beat Sensitivity versus Energy Threshold, and
        `ParameterModel.cpp` has the same explanation at the parameter.
      - Full `ctest` 331/331, fixture suite 7/7. Checked in the app: the panel shows the row.
        The plugin binds the same slider by APVTS attachment (built, not run in a host).
      Found while running the tests (2026-10-03): the app's `[MidiLearn]` tests, long written off
      as an environmental ~5-8% flake, were a real thread race (see 4.4's update). Fixed;
      `cmake/TestNoCrashDialogs.cpp` also stops a crashing test from opening a modal CRT
      dialog that stalls an unattended `ctest`.
- [x] 5.6 (M) Multi-instance behaviour in a DAW: shared library handle, per-instance engine,
      GPU budget awareness (lower FPS for instances without visible surfaces). With 4.2 (D15),
      every `projectm_handle` in the process shares one GL function resolver (the first
      create call's load proc wins), so all plugin instances must pass the same resolver.
      Note (2026-10-04):
      - **Shared library handle:** `ProjectMLibrary::acquireShared()` (new) gives every
        `RenderEngine` in the process the same loaded copy and function table, held by
        `shared_ptr` and kept while any engine lives (a process-wide `weak_ptr`; the first
        successful load wins, later hints are ignored, and a failed load isn't remembered).
        Before this each engine did its own `ProjectMLibrary::load()`: the OS refcounted it
        to one module anyway, so this makes the sharing explicit rather than fixing a bug.
        `load()` stays for tests and tools.
      - **Same resolver:** already true, now documented at `acquireShared`: `ProjectMInstance`
        always passes a null load proc (projectM's own resolver) for every instance and layer.
      - **Per-instance engine:** already true (one `RenderEngine`, context and render thread per
        processor). Unchanged.
      - **Instances without visible surfaces:** already render nothing (2.10: a closed or
        minimized editor pauses its engine, 0 fps, no GPU work, state kept). Rather than a
        "lower FPS" tier for them, the new work is for instances that *are* visible:
        **GPU budget awareness.** Each render thread publishes its smoothed frame cost to a
        process-wide table (`GpuShare` in `RenderEngine.cpp`, 32 slots, one per running
        engine, 0 while paused). An engine in Auto quality (5.3) budgets against
        `core::AdaptiveQuality::sharedBudgetMs`: what the other rendering engines leave of
        the frame, but never less than an equal share. So a heavy instance steps its
        resolution down first, and a light one is never squeezed below its fair part by a
        heavy neighbour. Fixed-quality instances still publish their cost. `RenderStats`
        gains `gpuSharers`, and both diagnostics overlays add "GPU shared by N instances"
        when N > 1.
      - Tests: `AdaptiveQualityTests` (the budget formula; two simulated instances that
        together overrun the frame: the heavy one drops, the light one stays at 100%, and
        together they then fit), `ProjectMLibraryTests` (same library for every caller while
        held, a fresh load after release), and a `RenderEngine` test with two real engines in
        one process (both report 2 sharers while both render; hiding one pauses it, freezes
        its frame count, and the other carries on reporting 1). Full `ctest`: 336/336.
      - **Not verified:** several instances in a real DAW (hand test: three or four
        instances in REAPER, two editors open, overlay shows "GPU shared by 2 instances";
        close one and it goes away). And that Auto actually steps down when the shared load
        overruns: like 5.3, this GPU never gets near the budget.
- [ ] 5.7 (M) Soak and stress tests: 4-hour run script for the app; rapid parameter
      automation; preset folder of 2,000 files; hot-unplugging the audio device.
- [x] 5.8 (S) Accessibility and UX pass: keyboard navigation in both shells, tooltips, high-DPI on
      all platforms, drawer scrim contrast over bright presets, touch-target sizes in the
      drawer (≥ 32 px) so a future touch shell needs no relayout.
      Note (2026-10-04):
      - **Keyboard navigation.** The drawer's controls still never take focus (a focused button
        would swallow Space, the DAW's transport key), so the keyboard reaches them through
        shortcuts. New in the shared table (§4.9): `B` opens the preset picker, `M` the
        settings menu, `D` the diagnostics panel. JUCE's popup menus already take arrows and
        Return, so everything in the settings menu is now keyboard-reachable. The popovers
        (Transitions, Output, Diagnostics) are different: the user opened them to work in
        them. `ui::KeyboardNavigation` makes each a keyboard focus container whose sliders,
        combo boxes, buttons and text fields take focus. Opening one focuses its first
        control, `Tab`/`Shift+Tab` move, a `KeyboardFocusRing` draws an accent outline
        around the focused control, and `Esc` (which no control uses, so it reaches the
        shell) closes it and gives focus back to the window. The Output panel's screen map
        was mouse-only: with focus in the panel, `1`-`9` pick that screen and `A` picks
        Automatic.
      - **Tooltips.** Audited: every drawer control has one. The Settings button, the preset
        title and the "Show diagnostics" menu items now name their new keys. A test checks
        that every drawer control has a tooltip and that the keyboard ones name their key.
      - **Screen readers.** The preset title was a plain component, invisible to
        accessibility tools. It now reports itself as a button titled "Preset: <name>", with
        the detail line as its description and a press action that opens the picker. The
        icon buttons were already announced by their names.
      - **Scrim contrast.** Measured instead of eyeballed: `ui::contrastRatio` (WCAG 2) over a
        pure-white preset. The old straight ramp (clear at the top to 88% black at 70% of
        the height) left the detail line at about 3.4:1 at the top of the button row,
        below AA's 4.5:1. The gradient now gets dark faster: 50% behind the progress track,
        80% at the top of the button row, 90% at the bottom. The top edge is still clear.
        Every row of the button area now passes 4.5:1 for both text colours over white.
      - **Touch targets.** Already met: buttons are 44 px, the mode chip and badge 36 px
        tall. A test now holds every visible drawer control to >= 32x32 px, docked and
        floating, from 480 px to 1920 px wide.
      - **High-DPI.** Checked in code, not changed. `OutputSurface` reports its size times
        `getApproximateScaleFactorForComponent` (display scale and a host's editor scale
        alike), so the engine renders at physical pixels. Icons are paths and text is
        vector, so nothing is a bitmap. **Not verified:** on a real HiDPI display, and not
        at all on macOS or Linux (no hardware here).
      - Tests: `AccessibilityTests` (contrast formula, scrim contrast, touch targets,
        tooltips, focus container) and new `ShortcutsTests` cases. Full `ctest`: 348/348.
      - **Not verified by hand:** the keyboard flow in a real window (I didn't screen-capture
        the app this time; see 5.9). Hand test: in the app and in REAPER, press `B`, `M`, `D`;
        open Transitions from `M`, `Tab` through it and watch the ring, `Esc` out, then
        check `←`/`→` change presets again. Also look at the drawer over a bright preset.
- [x] 5.9 (S) Diagnostics panel: GL vendor/renderer, projectM version, frame time, beat
      confidence, last errors, "copy diagnostics" button (replaces v1's Phase 9 benchmark
      idea with something cheaper and more useful).
      Note (2026-10-04):
      - `ui::DiagnosticsPanel` (new, shared by both shells) replaces the two shells' own
        overlay labels and their duplicated text-building code. It shows: projectM version and
        GL vendor/renderer/version; fps (or "paused"), frame size, CPU and GPU frame time,
        quality, layers, GPU sharers (5.6); beat source, BPM, confidence, bass level, drops;
        preset counts, last load time and the current preset; the shell's input (app) and
        surface mode; and the newest 5 recent errors. **Copy diagnostics** puts a fuller
        report on the clipboard: the shell (e.g. "MilkDAWp2 2.0.0 (VST3 in REAPER)"), the
        time, the OS/CPU/RAM, everything above, and every recent error.
      - The data is a plain `core::DiagnosticsInfo` struct, so `ui` still doesn't depend on
        `engine` (§4.1). `engine::Visualizer::diagnostics()` fills the engine and director
        parts and the shell adds its own. The app's log bundle (4.10) writes the same text.
      - **Last errors:** `engine::RecentErrors` (new, one per `RenderEngine`) keeps the last
        20 and folds back-to-back repeats into one entry with a count. It is fed by the
        director (a preset file that couldn't be read, or that projectM rejected, by file
        name and reason), by projectM's preset-switch-failed message, and by projectM's own
        error log. The log callback is registered **per render thread**
        (`currentThreadOnly`), so each plugin instance hears only its own projectM
        instances.
      - Opened from Settings / View > Show diagnostics, or `D` (5.8). It sits top left over
        the picture, and `Esc` or Close hides it. In the plugin it is now hidden until asked
        for; the old label showed by default. The app still remembers it (`showDiagnostics`).
      - Tests: `DiagnosticsPanelTests` (lines for a running, paused and unavailable engine;
        only the newest errors shown but all copied; the panel grows with its lines),
        `RecentErrorsTests` (order, folding, capacity, cutting long messages), and the
        `Director` end-to-end test now checks that the broken fixture file shows up in
        `Visualizer::diagnostics()` as "zz-broken.milk skipped: ...". Full `ctest`: 348/348.
      - **Not verified by hand:** what it looks like. I launched the app to capture its window,
        but another full-screen application was in front, so the capture showed that instead
        (deleted). Hand test: open it in the app and in REAPER, check it reads well over the
        picture, press Copy diagnostics and paste.

Hand test: a DJ-style hour with mixed genres in the app with Energy mode; note every transition
that felt wrong and file it with the timestamp.

### Phase 6 — Content, packaging, release

**Goal:** 1.0. **Exit:** installers for all platforms published by the tag workflow, signed
where it's free (D11: Windows via SignPath if accepted, macOS ad-hoc with documented first-run
steps), with checksums and build attestations; docs live; this repository renamed to MilkDAWp
and the v1 repo archived with a pointer.

**Order of work** (numbers are labels, not sequence): 6.5 first, unsigned, so every later item
is exercised by the real pipeline → 6.1 + 6.1b → 6.2–6.4 → first `-beta` release (6.8) →
6.10 (SignPath needs a published release) → 6.6/6.7 alongside → 6.9.

- [~] 6.1 (S) Bundle Cream of the Crop + the projectM texture pack (D12): measure installer
      size (full pack vs subset plus an in-app download), ship the pack's LICENSE.md and
      curator credit, default preset chosen (pleasant and cheap), first-run library root
      points at it. Check that the first-run scan, Weighted shuffle and preset metadata stay
      fast at ~10k presets.
      Update (2026-10-04): done except the hand test in a DAW and About's credit (6.7).
      - **Size: ship the whole pack.** At the pinned commits, 9,795 presets + 67 textures
        are 115 MB on disk (139 MB allocated, ~9,800 small files), 34 MB as zip, 13 MB as
        tar.gz and ~3 MB with xz/LZMA (presets are small text with heavy duplication across
        edits). No subset and no in-app download needed.
      - **Fetch:** `cmake/BundledContent.cmake` (`MILKDAWP_BUNDLE_CONTENT`, default ON)
        downloads both repos as GitHub archives of a pinned commit, checks a SHA-256 and lays
        them out as `<build>/content/{Presets/Cream of the Crop, Textures}`. Plain
        `file(DOWNLOAD)`/`file(ARCHIVE_EXTRACT)`, not FetchContent: FetchContent extracts
        under a long temporary name, which pushes the deepest presets past Windows' MAX_PATH.
      - **Windows paths:** the pack's longest relative path is 174 characters. Installed under
        `C:\ProgramData\MilkDAWp\Presets\Cream of the Crop\` (50) nothing is over 260; in
        Explorer's default extraction of a zip (`Downloads\<zip name>\<top folder>\...`) 905
        files would be. So the Windows release ships the content as its own
        `-windows-presets.zip` to extract into `C:\ProgramData\MilkDAWp`; macOS and Linux
        carry `Content/` inside the app. Build trees must stay within ~85 characters of the
        drive root. (Superseded by 6.2-6.4: every installer now puts the content in the
        shared location itself.)
      - **Finding it:** `engine::BundledContent` searches `$MILKDAWP_CONTENT_DIR`, then
        `Content` beside the binary (or `../Resources/Content` in a bundle), then the shared
        location (`%ProgramData%\MilkDAWp`; `/Library/Application Support/MilkDAWp` and the
        per-user one; `$XDG_DATA_HOME/milkdawp`, `/usr/local/share/milkdawp`,
        `/usr/share/milkdawp`), then, in dev builds only, the build tree
        (`MILKDAWP_CONTENT_FROM_BUILD_TREE`, off in the release presets; checked that no build
        path is in the release binaries).
      - **First run:** the app starts in the bundled pack when no folder is saved (or the
        saved one is gone) and saves it like a chosen folder. The plugin does the same in
        `prepareToPlay` when no session state set a folder: hosts restore state before
        preparing, so a saved session's folder wins and the pack is never scanned only to be
        replaced.
      - **Default preset:** `Geometric/Wire Circles/Geiss - Many Colors 1.milk`. Eight cheap
        candidates (no warp/comp shader, at most one custom wave and shape, ~2 ms to load)
        were rendered with no audio; five drew nothing, since most of the pack is audio-driven
        and a first launch often has no input yet. Many Colors 1, Many Colors 2 and 3D - Luz
        looked good in silence. One constant (`BundledContent::kDefaultPresetRelativePath`).
      - **Textures:** the engine never set projectM's texture search paths, so presets that
        sample the pack's textures (521 use `sampler_worms` alone) drew projectM's black
        stand-in. `projectm_set_texture_search_paths` is now bound and called once per
        instance from `RenderEngineConfig::textureSearchPaths`, set by the app, the plugin
        and mdw-view. A headless render test shows `worms.jpg` (mean brightness 307 vs 0
        without the path). User libraries that keep textures beside their presets aren't
        searched yet: presets are loaded from memory, so projectM doesn't know their folder.
      - **Scale (Windows, Release / Debug):** scan of 9,795 presets 0.49 / 0.86 s; director
        library ready (scan, playlist, start on the default preset) 0.53 / 1.2 s; selection
        pass with ratings 3 / 21 ms; metadata round trip (1,959 entries) 1 / 19 ms; one
        Weighted pick 127 µs / 3 ms. The Weighted pick was 9.6 ms in Debug: it asked
        `isInNoRepeatWindow()` (a walk of the history) for each of ~10k entries; it now builds
        the window as a mask once per pick. `BundledContentTests` checks these with
        generous bounds wherever the pack was fetched (CI too).
      - **Licences:** the pack's `LICENSE.md`/`README.md` and the texture pack's README ship
        unchanged; `THIRD_PARTY_NOTICES.md` and each release README credit the curator and
        say how to ask for a removal. The texture pack states no licence at all (its README
        only describes the contents); shipped anyway per D12, noted in the notices.
      - Tests: `BundledContentTests` (search order, content root, default preset and
        textures, the fetched pack, scale) and the headless texture test. Full `ctest`:
        354/354 on Windows.
      - **Not verified by hand:** a first launch of the app and a new plugin instance in a DAW
        actually opening on Many Colors 1, and how it looks with music.
- [~] 6.1b (M) Searchable preset browser (moved up from the post-1.0 backlog by D12):
      filter-as-you-type over ~10k presets, favourites and ratings (5.2), keyboard
      navigable (5.8). Replaces the click-the-preset-name popup menu for large libraries;
      Phase 7.6 builds on it.
      Update (2026-10-04): done in both shells; not yet checked by hand in a DAW.
      - `ui::PresetBrowser` (new, shared): a popover over the picture, like Transitions and
        Output, opened by the preset title, `B`, or the app's File > Browse presets /
        Playback > Choose preset. It replaces the popup menu of nested folders in both shells
        and the app's separate library window (`PresetBrowserPanel`, 4.5, removed). Header:
        folder path, result count ("4 of 9,795 presets"), Folder... and Rescan. A search box,
        All / Favourites / Rated (best first) / Recent (app only: the plugin keeps no
        history) tabs, and a `juce::ListBox` that paints only visible rows. Rows are two lines
        (name; folder, "blacklisted" / "never auto-selected", #tags), 40 px tall for touch
        (7.6), with a heart and five stars that are clickable. Right-click adds Play,
        favourite, blacklist and the 5.2 rating / never auto-select / tags items. One popover
        at a time in both shells.
      - **Search** (`ui::PresetSearchIndex`): every word of the query must appear in the
        name, the folder or the preset's tags, in any order ("geiss wave"). Names are
        lower-cased once per playlist change. 10k presets: 2.4 ms per keystroke and a 39 ms
        rebuild in Debug.
      - **Keyboard** (5.8): opening it selects and scrolls to the playing preset and focuses
        the search box (typing replaces the last search). Up/Down/Page Up/Page Down move
        without playing, Return plays the selection (after typing, the best match is
        selected), Esc closes. With the list focused (Tab): Home/End, F toggles the heart,
        0-5 set the rating, other typing returns to the search box. The list gets
        `KeyboardFocusRing`'s outline; rows have accessible names ("Geiss - Many Colors 1,
        favourite, 4 stars, playing").
      - **Favourites are shared now.** They were app-only (`AppState.favouritePresets`, by
        path), so the plugin had none. `core::PresetInfo` gains `favourite`, stored as an
        `f` in the metadata file's flags column next to `n` (older readers ignore unknown
        letters; older files have no favourites). They follow the 5.2 rules: keyed by file
        name, shared by the app and every plugin instance, picked up from other processes.
        The app moves its saved favourites into the metadata once at startup and clears the
        old list.
      - Pull-based: while visible it polls 4 times a second, comparing the director's
        `playlistGeneration` and the store's `generation()` instead of copying 10k names
        each tick. A rating or heart is shown at once and confirmed by the store's next
        generation.
      - Tests: `PresetBrowserTests` (query words; any-order and tag matching; 10k search
        timing; filter, count and selection; open on the playing preset; arrows don't play,
        Return does, Esc closes; F/0-5 write the shared metadata; Favourites and Rated tabs;
        changes made elsewhere appear on the next poll; Recent only with a history; row hit
        areas) and a `PresetMetadataTests` case for the flag. Full `ctest`: 366/366. pluginval
        (strictness 5, editor tests on) passes with the browser in the editor.
      - Checked in the app (Windows, dev identity, the 9,795-preset pack; the dev settings
        were backed up and restored): `B` opens it on the playing preset; "geiss many" narrows
        to 4 of 9,795 as typed; Tab, `4`, `F` rate and heart a preset and the row updates. The
        background is now 96% opaque, after the diagnostics panel's text showed through at 88%.
      - Not done: mdw-view (a dev tool, mostly run on the 3 fixture presets) keeps the popup
        menu. No folder tree: the flat list shows each preset's folder, and typing a folder
        name filters to it. **Not checked by hand:** the plugin in REAPER (open with the
        title and `B`; check that the search box gets keys there rather than the host).
- [~] 6.2 (M) Windows installer (Inno Setup or WiX): VST3 to `Common Files\VST3`, app to
      Program Files, optional desktop shortcut, uninstaller. Unsigned until 6.10 succeeds,
      then signed through SignPath in the release workflow (D11). Installs the
      Microsoft Visual C++ 2015-2022 Redistributable (x64) when it's missing: the plugin,
      app and projectM all link the dynamic MSVC runtime (3.10, decided 2026-09-26).
      Update (2026-10-05): written and compiled locally; installing is only tested on
      GitHub's runner (the smoke test below), not yet run there.
      - **Inno Setup** (6.7.3, pinned in `toolchain.json`): `packaging/windows/MilkDAWp.iss`,
        built by `package.sh` into `MilkDAWp-<v>-windows-x64-setup.exe` (33 MB, of which
        ~25 MB is Microsoft's `vc_redist.x64.exe`; the 9,795 presets compress to ~3 MB).
        Per-machine (admin): VST3 to `{commoncf64}\VST3`, app and licences to
        `{autopf}\MilkDAWp`, presets and textures to `{commonappdata}\MilkDAWp`
        (`C:\ProgramData`, which `engine::BundledContent` searches, so this also removes
        6.1's manual "extract the presets zip" step). Components: app, VST3, presets (a
        "compact" type skips the presets). Tasks: desktop shortcut (off), `.milk`
        association (on; §4.6's Windows half). Start menu entry, uninstaller (leaves
        `%APPDATA%\MilkDAWp`), Windows 10 21H2+ (D9), fixed AppId for upgrades.
      - **VC++ runtime:** bundled and run (`/install /quiet /norestart`) only when the
        registry says the x64 runtime is missing or older than the toolset that built us
        (`14.<VCToolsVersion minor>`, passed in by `package.sh`).
      - The app icon (`ICON_BIG`) is new: `resources/icon.png`, v1's wordmark on a dark tile
        (`resources/make-icon.ps1`; the skull "D" alone couldn't be cut cleanly from the
        135 px source). JUCE makes the `.ico`, which the installer also uses.
      - **Smoke test** (`scripts/release/smoke-test-windows.ps1`, release workflow only):
        silent install with the association, every file and registry key checked (and
        nothing but the plugin and projectM in the VST3's binary folder), pluginval at
        strictness 5 on the *installed* VST3 with projectM required, the installed app
        started, silent uninstall, nothing left.
      - The zips and `-windows-presets.zip` from 6.5/6.1 are gone; the symbols zip stays.
- [~] 6.3 (M) macOS: `.pkg` with VST3 + AU + app, universal binary, ad-hoc signed
      (`codesign -s -`), not notarized (D11: no paid Apple Developer account). The docs and
      the release notes give the first-run steps for Gatekeeper.
      Update (2026-10-05): written; **never run** (no Mac here). The first workflow_dispatch
      dry run is its first test.
      - **AU** (D2): `FORMATS` gains AU on Apple (type aufx, subtype `Mlkw`, manufacturer
        `OMda`, the v1 identity), with projectM deployed into the component and the runtime
        layout check, like the VST3.
      - `packaging/macos/build-pkg.sh` + `distribution.xml`: one component package each for
        the app (`/Applications`), VST3 (`/Library/Audio/Plug-Ins/VST3`), AU
        (`.../Components`) and presets (`/Library/Application Support/MilkDAWp`), relocation
        off, combined by `productbuild` with a choice per component, macOS 12+, both
        architectures. `package.sh` merges the x86_64 projectM into all three bundles and
        re-signs them ad hoc first. The package itself is unsigned: Gatekeeper asks once
        (Control-click > Open, in the README and the release notes); files a package installs
        carry no quarantine flag.
      - **Smoke test** (`scripts/release/smoke-test-macos.sh`): `installer -pkg`, every binary
        universal, every signature valid, `auval -v aufx Mlkw OMda` on the installed AU, the
        app started, everything removed.
- [~] 6.4 (M) Linux: AppImage for the app, tarball for the VST3, `.deb` as stretch.
      Update (2026-10-05): done, including the `.deb`; verified locally in Docker, not yet on
      GitHub.
      - **Builds on Ubuntu 22.04 now** (D9's floor). The devcontainer's 24.04 build needed
        glibc 2.38 and GCC 13's libstdc++ (`GLIBCXX_3.4.32`), so it wouldn't run on 22.04.
        The release job runs in a plain `ubuntu:22.04` container;
        `scripts/release/linux-setup.sh` installs GCC 12, CMake and Ninja from pip (22.04's
        are too old), JUCE's dependencies and vcpkg. The same script reproduces it locally
        in Docker. GCC 12 needed `-Wno-error=use-after-free` (a false positive inside
        libstdc++'s `std::string` at -O3, fixed in GCC 13; `cmake/Warnings.cmake`, GCC < 13
        only).
      - The binaries link only OpenGL/EGL, ALSA, fontconfig, freetype and the C/C++ runtime
        (readelf), and JUCE loads X11 at runtime, so nothing is bundled.
      - **AppImage** (`packaging/linux/build-appimage.sh`, appimagetool 1.9.1 and the static
        type-2 runtime pinned by SHA-256, so no libfuse2 needed): `usr/bin/` holds the app and
        projectM, `usr/share/milkdawp/` the presets (`BundledContent` gained
        `<binary dir>/../share/milkdawp`), plus a desktop entry, icon and `.milk` MIME type.
        20 MB.
      - **VST3 tarball**: the bundle, the presets and `install-vst3.sh` (to `~/.vst3` and
        `~/.local/share/milkdawp`). 26 MB.
      - **.deb** (`packaging/linux/build-deb.sh`): `/usr/lib/milkdawp` (app and projectM),
        `/usr/bin/milkdawp`, `/usr/lib/vst3/MilkDAWp.vst3`, `/usr/share/milkdawp`, desktop
        entry, icon, MIME type, copyright. Version `2.0.0~beta.1`, so it sorts before 2.0.0;
        the file name keeps the label. Depends from the binaries' NEEDED plus JUCE's X11
        libraries. 16 MB.
      - **Smoke test** (`scripts/release/smoke-test-linux.sh`), run locally in a fresh 22.04
        container, all passing: no binary needs glibc newer than 2.35; the AppImage holds the
        app, projectM and 9,795 presets, and the app starts from it (Xvfb, Mesa software
        GL); pluginval at strictness 5 on the tarball's VST3 with projectM required; the
        `.deb` installs with apt, the installed app starts, and it removes cleanly. Also
        checked by hand: the same AppImage starts on Ubuntu 24.04.
- [~] 6.5 (S) Release workflow: tag → build matrix (Release config, real identity) → sign
      where available → package → GitHub Release with generated notes, SHA-256 checksums and
      artifact attestations (`actions/attest-build-provenance`). `-beta` tags publish as
      pre-releases. Starts unsigned and before the installers exist (zipped bundles), so
      the pipeline is proven early.
      Update (2026-10-04): written, not yet run on GitHub. How to cut a release:
      `docs/releasing.md`.
      - `.github/workflows/release.yml`: a `v*` tag runs `version` (the tag's numbers
        must equal `project(VERSION)`, so a wrong tag fails in seconds), then
        `build-native` (Windows, macOS) and `build-linux` (devcontainer image) with the
        `release-*` presets and `MILKDAWP_DEV_ALT_IDENTITY=OFF` passed explicitly, then
        `publish` (`SHA256SUMS.txt`, attestations, `gh release create --generate-notes`,
        `--prerelease` for a suffixed tag). Running it by hand (workflow_dispatch) builds
        and packages without publishing, and keeps the archives as a workflow artifact.
        No compiler cache: release objects build from clean. The vcpkg cache is still
        restored.
      - **Version label:** `MILKDAWP_VERSION_LABEL` (new cache variable, defaults to
        `PROJECT_VERSION`, checked to be `PROJECT_VERSION[-suffix]`). `core::versionString()`
        and the app's version now come from CMake instead of a hard-coded "2.0.0", so a
        beta shows "2.0.0-beta.1" in the app, the plugin diagnostics and log bundles.
        Hosts and the VST3 moduleinfo still see the plain number.
      - **Release gate:** pluginval at strictness 5 (§8) on the Release VST3 that ships,
        with `MILKDAWP_REQUIRE_PROJECTM`, on Windows and Linux. CI already ran the tests on
        the commit; the release presets build none.
      - `scripts/release/package.sh` stages the VST3 and the app (projectM included,
        `.pdb/.ilk/.exp/.lib` dropped), `LICENSE`, `LICENSES/` plus projectM's vcpkg
        copyright file, `THIRD_PARTY_NOTICES.md` and a per-platform `README.txt` (install
        paths, the VC++ redistributable link, SmartScreen and Gatekeeper first-run steps).
        Windows also gets `-symbols.zip` (the app's and the VST3's `.pdb`, for the
        crash reporter's minidumps): `release-win` now compiles `/Zi` and links `/DEBUG`
        with `/OPT:REF /OPT:ICF`.
      - **macOS universal:** vcpkg builds projectM for arm64 only (the `arm64-osx-dynamic`
        triplet) while `release-mac` builds universal. The link works because projectM is
        loaded at runtime, but Intel Macs would run inert. The job installs the manifest for
        `x64-osx-dynamic` as well, and `package.sh` merges the two dylibs with `lipo` in
        each bundle, checks both slices are there, then re-signs ad-hoc (`codesign
        --force --deep -s -`) (D11).
      - `THIRD_PARTY_NOTICES.md` (now shipped) no longer says zlib/libpng come from vcpkg:
        JUCE's bundled copies are used since §4.11's 2026-09-26 fix.
      - Verified locally: `release-win` built with the real identity; packaged; the
        extracted zip passed pluginval at strictness 5 with projectM loading from inside
        the bundle; both binaries carry the label. Core tests pass (153 cases). Linux the
        same way in the devcontainer image: `release-linux` built and packaged, and the
        extracted tarball passed pluginval at strictness 5.
      - **Not verified:** the macOS job (no Mac here: the x64 vcpkg install, `lipo` and
        `codesign` steps first run on GitHub), the `publish` job, and attestations. Next
        step: a workflow_dispatch dry run, then the first `-beta` tag.
      - ~~Known gap: Linux builds in the Ubuntu 24.04 image (glibc 2.39), above D9's
        22.04 / glibc 2.35 floor.~~ Fixed in 6.4: the release builds in `ubuntu:22.04`.
- [ ] 6.6 (M) Documentation: user guide (plugin + app), capture how-tos per platform, OBS
      workflow, MIDI/automation guide, troubleshooting, FAQ on licences.
- [ ] 6.7 (S) In-app "About" with versions and licences; update check (opt-in, GitHub
      releases API).
- [ ] 6.8 (S) Beta programme: two weeks of `-beta` builds, issue template, triage.
- [ ] 6.9 (S) 1.0 release and repository promotion, in this order:
      1. Back up v1: `git clone --mirror` plus a `git bundle create … --all`; export issues
         (`gh issue list --state all --json …`), the wiki and release assets, which a mirror
         doesn't include; keep a built v1 `.vst3` so old sessions can still be opened in v1.
      2. Test v1 → v2 state migration (§4.8) on copies of real v1 sessions. A release build
         uses v1's identity (D1), so installing it replaces v1 in every host, and a session
         saved by v2 won't reopen in v1.
      3. Rename the v1 repo (e.g. `MilkDAWp-v1`), commit a README pointer to this one, then
         archive it (archived repos are read-only).
      4. Rename this repo `MilkDAWp2` → `MilkDAWp` (GitHub redirects the old URLs; old links to
         `Blue-Kachina/MilkDAWp` now land here, and links to v1 release downloads break) and
         `git remote set-url origin` in every clone.
      5. Sweep leftover `MilkDAWp2` names: the GHCR image `milkdawp2-devcontainer`
         (`ci.yml`, `devcontainer-image.yml`, `.devcontainer/devcontainer.json`; a package
         doesn't follow a repo rename, so keep the name or rename it and rebuild), and the
         text in `app/`, `engine/`, `core/` and the docs. The dev identity's
         `MilkDAWp2 Dev` names (ADR-0007) stay as they are.
      6. Transfer open v1 issues that still apply; announce.
- [ ] 6.10 (S) Windows signing (D11): after the first published `-beta` release, apply to the
      SignPath Foundation open-source programme (asking first about JUCE's dual licence);
      on acceptance, add the SignPath step to the release workflow. If refused, record it in
      D11 and keep shipping unsigned with checksums and attestations.

### Phase 7 — Android standalone app (post-1.0)

**Goal:** the standalone app on Android phones and tablets, built from the same core, engine
and UI (ADR-0010, D16). **Exit:** a signed build installs on Android 10+ and shows a working
visual from a bundled preset within 10 seconds of first launch, reacting to the microphone
after one permission prompt. The visual survives backgrounding and rotation without
resetting. The CI matrix builds the APK on every push.

Starts after 1.0, but 7.1 can run any time as a time-boxed spike: it is the biggest unknown
and decides how the rest is built.

- [ ] 7.1 (L) **Spike:** Gradle project under `android/` building our CMake tree through the
      NDK (`externalNativeBuild`), vcpkg chainloaded for `arm64-android` (plus
      `x64-android` for the emulator). Do the projectM overlay port, projectm-eval and glm
      build? Does `libprojectM-4.so` load from the APK by bare name? Does JUCE's Android
      activity glue work without `juce_add_gui_app` (JUCE's CMake API has no Android
      support)? Done when the headless render test (2.7) passes on a device or emulator.
      Write the result into ADR-0010; if Gradle can't work, decide on an Android-only
      Projucer project there.
- [ ] 7.2 (S) `OffscreenGLContext` Android branch, checked before `__linux__` (Android
      defines it): `EGL_OPENGL_ES_API`, ES 3.x, surfaceless where
      `EGL_KHR_surfaceless_context` exists, else a 1×1 pbuffer. The `GL_TIME_ELAPSED` query
      is used only with `EXT_disjoint_timer_query`; otherwise `gpuFrameMs` stays -1.
- [ ] 7.3 (M) Android app shell: one fullscreen activity whose main view is an
      `OutputSurface` on the readback path (2.15; JUCE 9 cannot share an Android GL
      context). Drawer auto-hides, tap reveals, touch targets sized for fingers, and no
      desktop-shell features (Phase 4's boundary rules). Lifecycle: on suspend the surface
      goes invisible and the engine pauses (2.10); it resumes without a reset. Rotation and
      split-screen resize the surface only.
- [ ] 7.4 (S) Microphone input through `AudioDeviceManager` (Oboe), with the `RECORD_AUDIO`
      runtime permission, a rationale screen, and a "denied" state that explains how to fix
      it.
- [ ] 7.5 (L) Android `SystemAudioCapture` (§4.7) over `AudioPlaybackCapture`: Kotlin +
      JNI, MediaProjection consent, foreground service with its notification. Tell the user
      that some apps block capture. Measure our onset/tempo quality on this path against
      the fixtures before calling it done.
- [ ] 7.6 (M) Presets: extract the bundled pack (6.1) from APK assets into app storage on
      first run and after an app update; "Import presets" copies a Storage Access Framework
      folder (textures included) into app storage, and the engine scans that as it does any
      folder. Includes the searchable, touch-friendly preset browser from the post-1.0
      backlog: the drawer's popup menu does not work on a phone.
- [ ] 7.7 (M) Mobile performance: tune adaptive quality (5.3) and the preset-cost cache
      (5.4) on at least one low-end and one high-end phone. Pick a phone-safe default subset
      of the bundled pack. Watch thermal throttling over a 30-minute run.
- [ ] 7.8 (M) Optional: a JUCE patch that passes the share context through on Android
      (`juce_OpenGL_android.h` ignores it, `juce_OpenGLContext.cpp` hard-codes
      `EGL_NO_CONTEXT`), offered upstream first. Removes the per-frame copy; readback stays
      as the fallback.
- [ ] 7.9 (M) CI and release: an Android build job, plus an emulator smoke run of the
      engine tests where practical. Signed AAB/APK from the tag workflow; Play listing with
      the AGPL source offer and projectM's LGPL notice. The Play upload key is a new
      decision next to D11.

Hand test: on a mid-range phone, install, grant the microphone, play music from a speaker,
confirm the visuals react; switch to another app and back (visual intact); rotate; import a
preset folder; then try playback capture with a music app.

### Post-1.0 backlog (unscheduled)

- Texture sharing output: Spout (Windows), Syphon (macOS), NDI (all) so OBS/Resolume can
  take the frame without screen capture.
- Scenes: snapshot all parameters, morph between snapshots over time.
- Setlists and cues with DAW markers / MIDI program changes.
- OSC server and a small web remote for phone control.
- Out-of-process renderer / "Link mode" between plugin and app (§4.6).
- CLAP and LV2 formats.
- Linux native loopback capture module.
- Offline high-resolution render to video (`projectm_set_frame_time` from the file's sample
  clock makes this a straight loop, D15).
- Cast the Output window to a network display (Chromecast / AirPlay / DLNA). The drawer's
  Output button is a pop-out today; a cast target would sit beside it.
- Drawer countdown: `DirectorStatus` publishes the next scheduled transition time and the
  beat phase, so the drawer's (currently empty) progress track fills in Timed/Hybrid modes
  and shows beat pips in BeatQuantized, with "next in 0:11" in the preset detail line.
- ~~Searchable preset browser panel~~: moved into Phase 6 as 6.1b by D12 (the bundled pack
  has ~10k presets). Phase 7.6 builds on it.
- **Layers: several inputs, several visuals, one canvas.** N projectM instances in the
  engine's one GL context, each fed its own audio input and rendering to its own FBO
  (`render_frame_fbo`, D15), mixed onto the output by our own compositor pass. Mix options:
  split, alpha/luma key, add/screen, animated masks (MilkDrop3-style blend patterns, but
  driven by `BeatClock`). Optional cross-feed via `projectm_opengl_burn_texture`: one layer's
  output drawn into another's feedback buffer so the visuals bleed into each other. Input
  routing: sidechain buses on one plugin instance; "send" instances feeding a "hub" instance
  through an in-process registry (breaks in hosts that sandbox plugins into separate
  processes); multiple input channel pairs or devices in the app. Background: MilkDrop3's
  `.milk2` "double preset" is *not* multi-projectM. It is MilkDrop 2's own old/new preset
  transition held at a fixed blend progress: one engine, one audio input, two preset states
  under a per-vertex blend mask. Layers generalizes that to independent inputs. The 1.0
  guardrail lives in 2.13 (per-instance state in a struct).

---

## 8. Testing and CI strategy

| Layer | Tooling | Runs where |
|---|---|---|
| Environment | devcontainer image built and smoke-tested (configure + core tests inside it) | on changes to Dockerfile or vcpkg manifests |
| `milkdawp_core` unit tests | Catch2 v3; deterministic, fixture-driven | every push, all platforms; ASan/UBSan/TSan job on Linux, inside the devcontainer image |
| Analysis quality gate | `mdw-analyze --suite fixtures/` vs `thresholds.json` | every push, Linux |
| Scheduler simulations | Catch2 with simulated `BeatClock` streams | every push |
| Engine headless render | offscreen GL via Mesa llvmpipe, fixture presets | every push, Linux; nightly on macOS/Windows runners |
| RT safety | Clang RealtimeSanitizer on `processBlock` and ring code | every push, Linux |
| Plugin validation | `pluginval` strictness 5 (VST3 all platforms, AU macOS), `auval` | every push |
| Soak / stress | scripted 4-hour app run, memory sampling | nightly / pre-release |
| Manual DAW matrix | `docs/daw-checklist.md` | before each beta and release |
| Android build (Phase 7) | Gradle + NDK APK build; engine tests on an emulator where practical | every push, once 7.9 lands |

Coverage expectation: core ≥ 80% line coverage reported in CI; engine and shells covered by
smoke tests and validators rather than a percentage.

---

## 9. Packaging, signing, distribution

- **Windows:** VST3 bundle + app installer. Runtime DLLs (projectM, GLEW,
  freetype, png, zlib, brotli) live inside the `.vst3` bundle's binary folder and next to the
  app exe. Signed through the free SignPath Foundation programme if accepted, else unsigned
  (D11, 6.10).
- **macOS:** universal `.pkg` installing `MilkDAWp.vst3`, `MilkDAWp.component`, and
  `MilkDAWp.app`; each bundle carries `Frameworks/` with fixed-up dylibs; ad-hoc signed, not
  notarized (D11: no paid Apple Developer account), with Gatekeeper first-run steps in the
  docs. Loopback capture entitlements/permissions documented.
- **Every release:** SHA-256 checksums and GitHub artifact attestations (D11).
- **Linux:** AppImage for the app (bundles the shared libraries), `.tar.gz` for the VST3 with
  `Contents/Resources/lib` rpath layout from v1; `.deb` stretch.
- **Android (Phase 7):** signed AAB for Play and APK for sideloading, arm64-v8a;
  `libprojectM-4.so` as its own library in the APK (LGPL, dynamic); bundled presets in APK
  assets, extracted to app storage on first run. Play upload key alongside D11.
- **Presets:** bundled pack installed to a shared location per platform; user library root
  defaults there but is changeable.
- **Licences:** AGPL-3.0 for MilkDAWp, LGPL-2.1 for projectM (dynamically linked, notices and
  source offer in installers), JUCE AGPL, third-party notices generated at build time.

---

## 10. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Shared GL contexts across windows behave differently per platform/host | primary window mirror + Output window design | Phase 2.3 spike before committing; PBO readback fallback for the primary-window mirror is always available |
| Hosts swallow hover or mouse-move events so the drawer never reveals | controls unreachable in that host | tap/click reveal as well as hover; pinned is the plugin default; DAW checklist (3.11) tests drawer reveal per host |
| A host intercepts some keys before the editor sees them | shortcuts silently dead in that host | every action is also pointer-reachable; Output and detached-controls windows are ours and always get keys; per-host results in the DAW checklist; `EDITOR_WANTS_KEYBOARD_FOCUS` experiment (3.14) |
| Devcontainer image drifts from what CI runs, or grows stale against the vcpkg baseline | "works in the container, fails in CI" | CI runs *inside* the published image; image rebuild is triggered by manifest changes; image tag recorded in CI logs |
| projectM preset compile hitches on the render thread | visible stutter on transitions | measure and cache per-preset cost (5.4), prefetch, prefer cheap presets on any cut style under a randomized policy, consider upstream async load contribution or a dual-instance/crossfade redesign |
| Hosts that dislike OpenGL (some macOS hosts, sandboxed AUv3 not in scope) | plugin unusable in that host | pluginval + DAW matrix early (Phase 3); engine can run with zero surfaces; out-of-process renderer is the long-term escape hatch |
| macOS system audio capture APIs require newer OS and permissions | standalone loopback on older macOS | feature-gate at runtime; document BlackHole fallback; MVP ships without native loopback |
| Beat tracking on non-electronic or rubato material | wrong-feeling transitions | confidence-gated fallback to Timed mode; host transport wins in the DAW; fixtures include hard cases so the gate is honest |
| No budget for signing or notarization (D11) | SmartScreen warnings on Windows; Gatekeeper blocks first launch on macOS | Windows: SignPath Foundation's free OSS programme (6.10); macOS: ad-hoc signing plus documented first-run steps; checksums and build attestations on every release so downloads can still be verified |
| projectM pinned to an untagged upstream `master` commit (D15) has a regression 4.1.7 did not | broken or different preset rendering, found late | pin a hash, never float; 2.7 headless render over real presets on every push; DAW checklist before each beta; bump on a branch; move to the 4.2.0 tag as soon as it exists |
| vcpkg baseline drift breaking projectM builds | CI red for reasons unrelated to our code | pinned baseline; bump on a branch with the full matrix; binary cache |
| JUCE 9 is two months old; 9.0.x point releases may change behaviour we depend on (EGL, Direct2D compositing, CoreAudio rewrite) | surprise breakage on upgrade, or a platform bug we cannot fix | JUCE pinned by tag + hash; upgrades on a branch with the full matrix and the DAW checklist; keep `BREAKING_CHANGES.md` review as a step in the upgrade PR template; report upstream with a minimal repro |
| Duplicate zlib/libpng between JUCE 9's C-mode bundled copies and vcpkg's | ODR violations, odd crashes on one platform only | single-copy rule decided in 0.1 and checked at link time in CI |
| Scope creep from post-1.0 ideas (Spout, scenes, OSC) | 1.0 slips | tiers in §3 are the contract; new ideas go to the backlog section, not into phases |
| Preset pack licensing (D12: MilkDrop presets carry no formal licence) | an author asks for removal | ship the pack's LICENSE.md and attribution; honour removal requests and follow upstream removals; the in-app downloader (6.1) remains the fallback if bundling ever has to stop |
| ~10k bundled presets (D12) | slow first-run scan, unusable popup menu | searchable browser (6.1b); measure scan and shuffle at full pack size in 6.1 |
| Android build path (JUCE's CMake API has no Android support; our projectM overlay has never built for an Android triplet) | Phase 7 costs much more than estimated, or needs a second build system | 7.1 spike first, time-boxed, before anything else is scheduled; Phase 4 follows ADR-0010's boundary rules so the shell isn't the problem too |
| Readback surfaces (Linux, Android, refusing drivers) cost a full-frame GPU→CPU→GPU copy per surface | lower frame rate on weak GPUs, especially phones | only while a readback surface exists; adaptive quality (5.3) shrinks the frame; JUCE sharing patch (7.8) removes it on Android |

---

## 11. Working agreements for AI-assisted development

These apply to any agent session working in this repo (and to humans, but agents forget).

**Session sizing.** Pick one checkbox. If it does not fit in a session, split it in this file
first (add sub-items), commit the split, then start. Never leave a phase's exit criteria vaguer
than you found them.

**Definition of done for a task.**
1. Code compiles warning-free on the platform you are on; CI green on all three.
2. Tests exist for the behaviour (core: unit; engine: headless smoke; shells: pluginval or the
   DAW checklist updated).
3. Threading rules in §4.2 respected; anything touching the audio callback carries
   `[[clang::nonblocking]]` and passes the RTSan job.
4. The checkbox in this file is ticked in the same commit, with a one-line note if the
   approach changed.
5. If a decision in §5 was touched, an ADR was added or amended.

**Boundaries.**
- Do not add JUCE or GL includes to `core/`. If you think you need to, write down why in the PR
  and stop.
- Do not add a third way to do something that already has two. Delete one first.
- Do not disable, skip, or loosen a test or a threshold in `fixtures/thresholds.json` to get
  green. Lowering a threshold is a decision for Matthew with the metric report attached.
- Do not change plugin identity codes, bundle IDs, or the state schema version without an ADR.
- Do not add UI that only exists in one shell. Drawer, output window, and settings are
  `milkdawp_ui` components composed by both shells.
- Work in the devcontainer unless the task needs a native toolchain (AU, MSVC-specific,
  installers, device capture). Say so in the PR when it does, so the reviewer knows to pull a
  CI artifact rather than build locally.

**When to stop and ask.**
- Any §5 item marked **Open** that the task depends on.
- A platform behaviour that contradicts an ADR (write up what you saw first).
- Anything involving accounts, certificates, or licences.
- End of each phase: post the hand-test list and what you would like checked.

**Commit hygiene.** Small commits, imperative subject, body says *why*. Reference the roadmap
item (`[1.6]`) in the subject.

---

## 12. References

- v1 source: https://github.com/Blue-Kachina/MilkDAWp (tag `v0.7.5`)
- projectM 4 C API: https://github.com/projectM-visualizer/projectm (`src/api/include/projectM-4/`);
  4.2 additions are on `master`, marked `@since 4.2.0` (ADR-0008)
- MilkDrop3 (MilkDrop 2 fork, `.milk2` double presets): https://github.com/milkdrop2077/MilkDrop3
- JUCE 9 releases: https://github.com/juce-framework/JUCE/releases (9.0.0 on 2026-07-21,
  9.0.2 on 2026-09-07)
- JUCE breaking changes: https://github.com/juce-framework/JUCE/blob/master/BREAKING_CHANGES.md
- JUCE CMake API: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- JUCE licensing: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md
- pluginval: https://github.com/Tracktion/pluginval
- Onset detection and beat tracking background: Bello et al., "A Tutorial on Onset Detection
  in Music Signals" (2005); Ellis, "Beat Tracking by Dynamic Programming" (2007); Böck et al.,
  "Evaluating the Online Capabilities of Onset Detection Methods" (2012).
- Clang RealtimeSanitizer: https://clang.llvm.org/docs/RealtimeSanitizer.html
- vcpkg manifest mode and versioning: https://learn.microsoft.com/vcpkg/
