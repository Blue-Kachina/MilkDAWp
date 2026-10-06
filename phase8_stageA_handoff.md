# Phase 8 Stage A: handoff (updated 2026-10-05, second session)

Work on `development_roadmap.md` Phase 8, Stage A (8.1-8.6). Design source:
`custom_format_exploration.md` §4.4 (effects, gate), §4.5 (media), §4.6 (OSC), §6 (controls). Nothing
is committed: Matthew commits himself. His own uncommitted edits to `custom_format_exploration.md` and
`development_roadmap.md` are in the working tree. Roadmap boxes are **not ticked**; tick them once he
has checked the hand tests below.

## State

All of Stage A (8.1, 8.2, 8.2b, 8.3, 8.4, 8.6a-f) is implemented and wired into both shells. Cameras
were hand-tested on Windows by Matthew. The Linux camera and macOS video are built but not tried on
real hardware. Everything builds (Debug), and all five test suites
pass: core 174 cases, engine 120, plugin 24, app 29, ui 82. GPU effects tests run on this machine's real GL context. pluginval passes at strictness 10. Not
yet done: the hand tests.

Build recipe (Windows): memory `milkdawp2_windows_build_workflow.md`. In one PowerShell call: put ninja
and the VS Installer folder on PATH, import `VsDevCmd.bat` into the process env, then set
`$env:VCPKG_ROOT="C:\vcpkg"` again; then `cmake --build --preset dev-win-Debug`. Use the Edit tool or
`[IO.File]::WriteAllText` for edits; in this PowerShell, `R` is an alias (Invoke-History) and
`Set-Content -NoNewline` failed.

## What exists

**8.1 parameters and UI**
- `core/ParameterModel`: 28 parameters **appended** after the 1.0 ones (REAPER automates by index; a
  test pins the order). `ParameterSpec` gains `group` and `skewCentre`. New: `parameterGroups()`,
  `kMacroCount`, `macroParameterId()`.
- The plugin builds host parameter groups from contiguous runs (`createParameterLayout`), with skew.
  A test checks the host sees model order and the "Visual" group. `readControls` copies every
  engine-read parameter generically (`engine::parameterMember`).
- `core/MacroLock` plus wiring. Plugin: a processor timer (10 Hz) watches the current preset and moves
  un-locked Macros as gestures. It ignores the first preset and a restored session's preset arriving.
  App: `MainComponent::applyMacroLock` in the timer, skipping the first preset of the session.
- `ui/VisualSettings` (`VisualSettingsPanel` and `GateMeter`) is built from the parameter groups. It
  dims Warp, Wave Size, Media Mix and the Macros with a reason, and has a Reset visual button and a
  scrolling viewport. It is in the settings menu of both shells ("Visual..."). The gate meter is fed
  from the timers with `primaryLayer().gateLevelDb()/gateOpen()`.
- `docs/adr/0011-visual-controls.md` (D17 Stage A). `docs/parameters.md` regenerated (Group column,
  notes); generator notes are in `tools/generate-param-docs`. User guide:
  `docs/user-guide/controls.md` gains "Visual controls".

**8.2 effects:** `engine/EffectsChain` and the internal `engine/src/GlUtil` (the compositor uses it
too). Wired into `RenderEngine::run()`:
- Lone layer: straight to the output when neutral. Otherwise it goes through `primary.target`, then
  the effects, then a fade from black by the gate (`LayerCompositor`).
- Several layers: senders' effects run per layer (`fxTarget`). The primary layer's effects run over
  the mixed canvas (`canvasTarget`).

**8.2b gate:** `core/LayerGate`. Per layer in `RenderEngine`: the peak of the fed samples; a frame
with no new audio holds the last level for 150 ms (big host buffers). The envelope multiplies draw
opacity, and gated layers keep feeding and rendering. `LayerChannel` carries the gate settings and the
meter outputs.

**8.3 speed:** `core/PresetClock`. Every layer renders at
`setFrameTime(clock.advance(frameDt, visual.speed))`, and soft cuts call `onSoftCut`. Note: presets
now always run on this clock instead of projectM's system clock.

**8.4 OSC:**
- `core/OscAddress` (address parsing, `oscName`, `plainFromNormalized` matching JUCE skew,
  `OscSettings`).
- `engine/OscRemote`, one per process via `juce::SharedResourcePointer`, linking `juce::juce_osc`.
  - In: `/milkdawp/[<instance>|*/]<paramId>[/norm] f`, `/milkdawp/[<instance>/]next|prev`, on the
    message thread.
  - Out at 60 Hz: `/milkdawp/<name>/beat i`, `/bar i`, `/drop`, `/preset s`.
  - Settings live in `osc.json` in the user data folder, off by default.
- `DirectorStatus` gains `beatIndex` and `barIndex`.
- Plugin: an endpoint per instance (by display name or id), with moves as gestures. App: one
  endpoint called "app".
- `ui/OscSettingsDialog` (async AlertWindow) under "OSC remote control..." in both settings menus.

**Dev tool:** `mdw-view --set <paramId>=<value>` (repeatable) sets Visual and gate parameters. It was
used to check the effects on a real preset; screenshots looked right.

## Open items
1. **pluginval**: strictness 10 on the Debug VST3 with `MILKDAWP_REQUIRE_PROJECTM` set: **passed**
   (log `build-tools/pluginval-logs/pluginval-20261005-174849.log`). Rerun with
   `pwsh scripts/pluginval.ps1 -Plugin "build-win/plugin/milkdawp_plugin_artefacts/Debug/VST3/MilkDAWp2 Dev.vst3"`.
2. ~~The one-block leak report in `milkdawp_plugin_tests`~~ **Fixed (2026-10-05).** The tests built
   processors with no JUCE message manager, so every timer, async update and message listener
   tripped a JUCE assertion, and on-demand singletons were never torn down.
   `plugin/tests/JuceEnvironment.cpp` (a Catch2 listener) now starts and stops JUCE for the run,
   as a host does: no leak, and no assertions under cdb.
3. Hand tests nobody has done yet (no desktop keystrokes or clicks allowed from the agent):
   - open Settings > Visual in the app and the plugin;
   - automate Hue, Speed and Macro 1 in REAPER;
   - Lock Macros on and off across preset changes (check whether REAPER's write and latch modes
     record the Macro moves, ADR-0011);
   - set the gate on a guitar track with hard stops;
   - OSC from TouchOSC or `oscsend` (enabling it opens a firewall prompt), including a REAPER
     recording pass capturing OSC moves.
4. **8.6 media sources**: the split below was confirmed by Matthew (2026-10-05). **8.6a is done:**
   - `engine/MediaSource` (interface, `ImageMediaSource`, `openMediaSource`, `coverUvScale`).
   - `LayerChannel::setMediaSource`.
   - `MediaTexture` in `RenderEngine.cpp` uploads a frame only when the source hands over a new one.
   - `LayerCompositor::overlay` and `LayerDraw::textureAlpha`/`uvScale*` draw the media over the
     layer's picture before the effects (over the canvas for a window owner) at Media Mix opacity.
   - Settings > Media source (Media Mix) in both shells.
   - Saved as `mediaSourcePath` in the plugin state and `mediaSource` in the app prefs.
   - `mdw-view --media <image>`.
   - Tests: `MediaSourceTests`, compositor overlay test, state round trips.

   The split:
   - 8.6a: `MediaSource` interface plus a still-image source (JUCE image loading), a compositor
     layer, and Media Mix driving its opacity. **Done.**
   - 8.6b: camera through JUCE `CameraDevice` (Windows/macOS), frames to a GL texture. **Built,
     not yet run against a real camera** (Matthew has no webcam set up yet; he'll test it).
     - `CameraMediaSource` and the shared-per-device registry are in `engine/src/MediaSource.cpp`.
     - Paths are `camera:<name>` (`cameraMediaPath`, `cameraDeviceName`).
     - `juce_video` is linked, with `JUCE_USE_CAMERA=1` on Windows/macOS only.
     - `CAMERA_PERMISSION_*` is set in the app and plugin CMake files.
     - The camera submenu is `ui/MediaMenu`.
     - The device is opened with `openDevice(index, 128, 64, 1920, 1080, false)` on the message
       thread, and closed there too (deferred if the last reference goes elsewhere).
     - Hand test: pick the camera in Settings > Media source > Camera, then raise Media Mix.
       Check that two instances on one camera both show it, and that the light goes off after
       None.
   - 8.6c: Windows video through Media Foundation behind `VideoDecoder`, transport-synced in the
     plugin. **Done.**
     - `engine/VideoDecoder.h` with `src/VideoDecoderWindows.cpp` (SourceReader, RGB32) and
       `VideoDecoderUnsupported.cpp` elsewhere.
     - `VideoMediaSource` (`src/VideoMediaSource.cpp`): a decode thread, looping, seeking on
       jumps.
     - `MediaTimeline` is published by `Visualizer::processAudio` (host samples / rate) through
       `LayerChannel::setMediaTimeline`, and the render thread calls `source->setTimeline()`.
     - `VideoTests.cpp` encodes its own H.264 clip with an MF SinkWriter, so there is no binary
       fixture.
     - Not yet seen in the real UI with a real video (hand test: Settings > Media source > Choose
       image or video, then scrub REAPER's playhead).
   - 8.6d: macOS AVFoundation, Linux GStreamer at runtime and V4L2 camera. **Done (2026-10-05).**
     - Linux video: `engine/src/VideoDecoderLinux.cpp`, GStreamer via dlopen, with a
       `decodebin ! videoconvert ! appsink` pipeline. Tested for real in the CI container
       (base and good plugins installed).
     - Linux camera: `engine/src/CameraLinux.cpp`, V4L2 mmap streaming, YUYV
       (`frameFromYuyv`, tested) or MJPEG. Not tested against real hardware: no camera in
       Docker.
     - macOS video: `engine/src/VideoDecoderMac.mm` (AVAssetReader; `enable_language(OBJC
       OBJCXX)` in the top-level CMakeLists). Verified only by macOS CI.
     - Cameras are split behind `engine/src/Cameras.h`, with `CameraJuce.cpp` for Windows/macOS
       and `CameraLinux.cpp`.
     - `VideoDecoder::writeTestClip` makes each platform's test clip: an MF H.264 MP4 on
       Windows, an AVAssetWriter H.264 MP4 on macOS, a GStreamer MJPEG AVI on Linux. Video tests
       stand down under TSan.
   - 8.6e: displacement blend modes. **Done.**
     - `LayerBlend::Displace` (not in the `layerBlend` parameter; `kMediaBlendCount`/`kMediaBlendNames`).
     - `LayerChannel::setMediaBlend`; Settings > Media source > Blend in both shells.
     - Saved as `mediaBlend` in the plugin state and the app prefs.
     - `mdw-view --media-blend 5`.
     - The compositor copies the canvas to `below_` and samples it pushed by the media's RG.
       Tested and seen on a preset.
   - 8.6f: spike, camera into projectM's feedback via `burn_texture`. **Done, and shipped as a
     blend mode.**
     - projectM 4.2 exports `projectm_opengl_burn_texture` (now a required symbol in
       `ProjectMFunctions::openglBurnTexture`, wrapped as `ProjectMInstance::burnTexture`).
     - Media blend **Burn in** (`LayerBlend::BurnIn`) burns into the layer's preset every
       frame. The media is first stamped into `burnTarget` (`LayerCompositor::stamp`: cropped,
       alpha = Media Mix), so projectM's blend applies the strength.
     - The first version burned every Nth frame at low Media Mix, which strobed: Matthew saw
       flicker, and frame diffs measured 10-30 (fixed 2026-10-05).
     - Displace also reads a ~90 px mip of the media (MediaTexture is mipmapped now), so camera
       noise can't jitter it.
     - Seen working in `mdw-view --media-blend 6`: the right way up, transparency respected.
   - **Finding for Stage B (8.12):** projectM 4.2 also has
     `projectm_set_texture_load_event_callback` (callbacks.h). An app can hand projectM a GL
     texture id for any sampler a preset names, which is how `sampler_camera`/`sampler_video` could
     work **without patch 2**. Catch: projectM takes ownership and `glDeleteTextures` it when done,
     and it loads once per preset. So a live feed must stay writable until projectM lets go, and
     we are not told when that happens. Needs a spike before 8.12 is planned (perhaps hand over a
     texture we only update while that preset is active, or re-hand on each load).
5. Effects shaders are GLSL 3.30 core; Android (Phase 7) needs ES variants.
