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
- AAX (Avid signing program), iOS/Android, or web builds. The video-first UI (§4.9) is
  deliberately touch-compatible so a mobile shell is not ruled out later, but none is built.
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
| Bundled preset pack | | ✔ | |
| Signed, notarized installers | | ✔ | |
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
  draws the engine's latest texture with JUCE's `copyTexture`. The PBO-readback fallback for
  drivers that refuse to share is still planned, not built; a surface detects and reports a
  failed share.
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
| D11 | Signing accounts | **Open** (needed before Phase 6) | Apple Developer ID (notarization) and a Windows code-signing certificate (EV or OV via Azure Trusted Signing) are prerequisites for 1.0 installers. Budget and account ownership to be confirmed. |
| D12 | Bundled presets | **Open** (needed before Phase 6) | Curate a licence-clean subset of the projectM community packs; confirm per-pack licences before bundling. |
| D13 | Window model | Decided | Video-first primary window with a hover/tap/pinned control drawer; a separately owned Output window for fullscreen on another display; detached-controls window as a secondary feature (§4.9). Replaces v1's control-strip-plus-pop-out-video layout. |
| D14 | Development environment | Decided | Single container image (devcontainer + CI + agent sessions) covering core, CLI, headless render, and lint; CI as the Windows/macOS build farm; idempotent native bootstrap with a doctor mode for those who want local builds (§4.10). No Nix. |
| D15 | projectM version | Recommended | Minimum projectM **4.2**, built from a pinned upstream `master` commit through a vcpkg overlay port until 4.2.0 is tagged. 4.1.7 draws its final image to framebuffer 0 whatever FBO is bound, which breaks §4.5. It also uses GLEW without initializing it, and times presets only by the wall clock. 4.2 adds `render_frame_fbo`, a GL-loader create call (no GLEW) and `set_frame_time`. Amends D4. See ADR-0008. |

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
      built (ADR-0009).
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
      resumes on the same context when visible again.- [x] 2.11 (M) `milkdawp_ui` drawer components: `ControlDrawer` (hidden / revealed / pinned
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
   saying the surface is "ok".
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

- [ ] 4.1 (M) `milkdawp_app` shell with `juce_add_gui_app`: video-first main window with the
      shared `ControlDrawer` (auto-hide default in fullscreen, pinned otherwise), menu bar with
      the shared `Shortcuts` table (§4.9) plus app-only `Space`, single-instance guard. Main window can fullscreen directly; ⛶ opens the `OutputWindow`
      for a second display; "float controls" for the projector-plus-laptop setup.
- [ ] 4.2 (M) Audio input: `AudioDeviceManager` device selector, input channel pair choice,
      level meter, "no signal" hint. Startup restores the last device; graceful fallback when
      it is missing.
- [ ] 4.3 (S) Preferences (`PropertiesFile`): device, preset library root, output display,
      quality, logging toggle, last window geometry.
- [ ] 4.4 (M) MIDI learn: map CC/notes to any parameter; persisted; UI affordance on each
      control.
- [ ] 4.5 (M) Preset library browser: tree of the library root, search, favourites, recently
      played, right-click add to blacklist.
- [ ] 4.6 (S) File associations and drag-and-drop for `.milk` files and folders.
- [ ] 4.7 (L) Windows WASAPI loopback capture module behind `SystemAudioCapture`, selectable as
      "System audio" in the device list.
- [ ] 4.8 (L) macOS system audio capture via Core Audio process taps (14.2+) with
      ScreenCaptureKit fallback (13+); permission flow and messaging.
- [ ] 4.9 (S) Linux: verify PipeWire/Pulse monitor sources appear; document.
- [ ] 4.10 (S) Crash reporting hooks (local minidump/log bundle) and "collect logs" menu item.
- [ ] 4.11 (S) Retire the JUCE `Standalone` wrapper format once 4.1–4.3 land (or keep it as a
      dev convenience behind a CMake option).

Hand test: fresh user account on each OS: install, launch, play music from a browser, confirm
visuals react without configuring anything (Windows/macOS via loopback, Linux via monitor
source).

### Phase 5 — Musicality, performance, and polish

**Goal:** the features that make v2 noticeably better than v1 in a live setting. **Exit:**
energy mode demonstrably cuts on drops in the fixture set; adaptive quality keeps ≥55 fps at
1080p on an integrated GPU with the bundled pack; 4-hour soak test passes with flat memory.

- [ ] 5.1 (M) Energy / section-change transition mode: rolling energy percentile, drop
      detector, build-up detection (optional), cooldown in bars.
- [ ] 5.2 (M) Weighted shuffle with ratings and tags; per-preset "never auto-select"; tag
      filters in the transition settings ("only *calm* during breakdowns" is post-1.0).
- [ ] 5.3 (M) Adaptive quality on the real FBO with GPU-time-driven hysteresis and a manual
      override; visible current-scale indicator.
- [ ] 5.4 (S) Preset load hitch mitigation: measure per-preset compile time, cache it, and
      prefer cheap presets when the scheduler needs a hard cut on the next beat. This is the
      **primary** hitch fix. projectM compiles preset shaders synchronously on the GL thread in
      every version (4.1.7 and 4.2 alike, no async load API), so 2.5's file prefetch can't hide
      that cost. If measurements show it's still bad, consider contributing async or
      parallel shader compilation upstream (§10).
- [ ] 5.5 (S) Beat sensitivity semantics: one knob that scales both our detector's threshold
      and `projectm_set_beat_sensitivity`, documented. The two are different things:
      projectM's value only rescales the bass/mid/treb levels presets animate from (clamped
      0–2). It detects no beats and triggers no transitions. Document the knob as "how hard
      visuals react + how easily we cut", or split it into two parameters if the coupling feels
      wrong in hand tests.
- [ ] 5.6 (M) Multi-instance behaviour in a DAW: shared library handle, per-instance engine,
      GPU budget awareness (lower FPS for instances without visible surfaces). With 4.2 (D15),
      every `projectm_handle` in the process shares one GL function resolver (the first
      create call's load proc wins), so all plugin instances must pass the same resolver.
- [ ] 5.7 (M) Soak and stress tests: 4-hour run script for the app; rapid parameter
      automation; preset folder of 2,000 files; hot-unplugging the audio device.
- [ ] 5.8 (S) Accessibility and UX pass: keyboard navigation in both shells, tooltips, high-DPI on
      all platforms, drawer scrim contrast over bright presets, touch-target sizes in the
      drawer (≥ 32 px) so a future touch shell needs no relayout.
- [ ] 5.9 (S) Diagnostics panel: GL vendor/renderer, projectM version, frame time, beat
      confidence, last errors, "copy diagnostics" button (replaces v1's Phase 9 benchmark
      idea with something cheaper and more useful).

Hand test: a DJ-style hour with mixed genres in the app with Energy mode; note every transition
that felt wrong and file it with the timestamp.

### Phase 6 — Content, packaging, release

**Goal:** 1.0. **Exit:** signed installers for all platforms published by the tag workflow;
docs live; v1 repo archived with a pointer.

- [ ] 6.1 (S) Curate and licence-check the bundled preset pack (D12); default preset chosen;
      first-run library root points at it.
- [ ] 6.2 (M) Windows installer (Inno Setup or WiX): VST3 to `Common Files\VST3`, app to
      Program Files, optional desktop shortcut, uninstaller; signed (D11). Installs the
      Microsoft Visual C++ 2015-2022 Redistributable (x64) when it's missing: the plugin,
      app and projectM all link the dynamic MSVC runtime (3.10, decided 2026-09-26).
- [ ] 6.3 (M) macOS: `.pkg` with VST3 + AU + app, Developer ID signed, notarized, stapled;
      universal binary.
- [ ] 6.4 (M) Linux: AppImage for the app, tarball for the VST3, `.deb` as stretch.
- [ ] 6.5 (S) Release workflow: tag → build matrix → sign → package → GitHub Release with
      generated notes and checksums. Pre-release channel on `-beta` tags.
- [ ] 6.6 (M) Documentation: user guide (plugin + app), capture how-tos per platform, OBS
      workflow, MIDI/automation guide, troubleshooting, FAQ on licences.
- [ ] 6.7 (S) In-app "About" with versions and licences; update check (opt-in, GitHub
      releases API).
- [ ] 6.8 (S) Beta programme: two weeks of `-beta` builds, issue template, triage.
- [ ] 6.9 (S) 1.0 release, archive the v1 repository with a README pointer to this one, transfer
      open v1 issues that still apply, announce.

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
- Searchable preset browser panel (filter-as-you-type, favourites) to replace the drawer's
  click-the-preset-name popup menu for large libraries.
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

Coverage expectation: core ≥ 80% line coverage reported in CI; engine and shells covered by
smoke tests and validators rather than a percentage.

---

## 9. Packaging, signing, distribution

- **Windows:** signed VST3 bundle + signed app installer. Runtime DLLs (projectM, GLEW,
  freetype, png, zlib, brotli) live inside the `.vst3` bundle's binary folder and next to the
  app exe. Signing via Azure Trusted Signing or an OV/EV certificate (D11).
- **macOS:** universal `.pkg` installing `MilkDAWp.vst3`, `MilkDAWp.component`, and
  `MilkDAWp.app`; each bundle carries `Frameworks/` with fixed-up dylibs; hardened runtime,
  Developer ID signature, notarization, stapling. Loopback capture entitlements/permissions
  documented.
- **Linux:** AppImage for the app (bundles the shared libraries), `.tar.gz` for the VST3 with
  `Contents/Resources/lib` rpath layout from v1; `.deb` stretch.
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
| projectM preset compile hitches on the render thread | visible stutter on transitions | measure and cache per-preset cost (5.4), prefetch, prefer cheap presets for hard cuts, consider upstream async load contribution |
| Hosts that dislike OpenGL (some macOS hosts, sandboxed AUv3 not in scope) | plugin unusable in that host | pluginval + DAW matrix early (Phase 3); engine can run with zero surfaces; out-of-process renderer is the long-term escape hatch |
| macOS system audio capture APIs require newer OS and permissions | standalone loopback on older macOS | feature-gate at runtime; document BlackHole fallback; MVP ships without native loopback |
| Beat tracking on non-electronic or rubato material | wrong-feeling transitions | confidence-gated fallback to Timed mode; host transport wins in the DAW; fixtures include hard cases so the gate is honest |
| Signing/notarization accounts and costs | blocks 1.0 installers | decide D11 early (Phase 4 at the latest); unsigned dev builds continue via CI artifacts |
| projectM pinned to an untagged upstream `master` commit (D15) has a regression 4.1.7 did not | broken or different preset rendering, found late | pin a hash, never float; 2.7 headless render over real presets on every push; DAW checklist before each beta; bump on a branch; move to the 4.2.0 tag as soon as it exists |
| vcpkg baseline drift breaking projectM builds | CI red for reasons unrelated to our code | pinned baseline; bump on a branch with the full matrix; binary cache |
| JUCE 9 is two months old; 9.0.x point releases may change behaviour we depend on (EGL, Direct2D compositing, CoreAudio rewrite) | surprise breakage on upgrade, or a platform bug we cannot fix | JUCE pinned by tag + hash; upgrades on a branch with the full matrix and the DAW checklist; keep `BREAKING_CHANGES.md` review as a step in the upgrade PR template; report upstream with a minimal repro |
| Duplicate zlib/libpng between JUCE 9's C-mode bundled copies and vcpkg's | ODR violations, odd crashes on one platform only | single-copy rule decided in 0.1 and checked at link time in CI |
| Scope creep from post-1.0 ideas (Spout, scenes, OSC) | 1.0 slips | tiers in §3 are the contract; new ideas go to the backlog section, not into phases |
| Preset pack licensing | cannot bundle content | D12 resolved before Phase 6; ship with a downloader as fallback |

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
