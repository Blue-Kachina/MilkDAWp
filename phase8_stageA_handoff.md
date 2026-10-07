# Phase 8 Stage A: handoff (updated 2026-10-05, end of Stage A)

Stage A of `development_roadmap.md` Phase 8 is **done, committed and green in CI** on every job:
Windows, macOS, Linux GCC, ASan, TSan and RTSan. The roadmap's Phase 8 entries (8.1-8.6, with the 8.6a-f
split) are ticked, and each carries a short "Update (2026-10-05)" summary. The exploration doc's §7
mirrors them. The design record is `docs/adr/0011-visual-controls.md`. This file is the working
note for whoever continues: where things live, what still needs a human, and what's next.

Matthew commits and pushes himself (memory `milkdawp2_user_owns_git.md`).

## What's left in Stage A: hand tests only

Matthew has tested: the Visual panel; Media Mix with an image and a camera on Windows; Displace and
Burn in (both flickered at first; fixed and confirmed).

Still to try by hand (the agent may not click or type on the desktop):
1. **OSC:** enable it in Settings > OSC remote control (a firewall prompt), then send from
   TouchOSC or `oscsend`. Include a REAPER recording pass that captures OSC moves as automation.
2. **Lock Macros** on and off across preset changes in REAPER. Check whether REAPER's write and latch
   modes record the Macro jumps (ADR-0011, exploration doc §6.3).
3. **Gate** on a guitar track with hard stops: clean stops, no flicker on sustained notes.
4. **Video in REAPER:** Media source > Choose image or video, then play and scrub; the frame should
   follow the playhead.
5. **Automation:** Hue, Speed and Macro 1 lanes in REAPER (the Phase 8 exit test, together with
   items 1 and 4).
6. **Linux camera** (V4L2) and **macOS video** (AVFoundation) on real machines: both are built and
   pass CI, but neither has run against real hardware.

## Where things live

| Item | Code | Tests |
|---|---|---|
| 8.1 parameters | `core/ParameterModel` (+`group`, `skewCentre`), `core/MacroLock`, `plugin/src/PluginProcessor` (groups, generic `readControls`, Lock Macros timer), `app/src/MainComponent` (`applyMacroLock`) | `ParameterModelTests`, `PluginProcessorTests` |
| 8.1 UI | `ui/VisualSettings` (three columns; scrolls when small), menus in `PluginEditor`/`MainComponent` | `VisualSettingsTests` (layout overflow checks) |
| 8.2 effects | `engine/EffectsChain`, `engine/src/GlUtil` (shared with `LayerCompositor`) | `EffectsChainTests` (GPU) |
| 8.2b gate | `core/LayerGate`; per-layer level and envelope in `engine/src/RenderEngine.cpp` | `LayerGateTests` |
| 8.3 speed | `core/PresetClock`, `RenderEngine::run()` `renderLayer` | `VisualControlsTests` |
| 8.4 OSC | `core/OscAddress`, `engine/OscRemote`, `ui/OscSettingsDialog` | `OscAddressTests`, `OscRemoteTests` |
| 8.6 media | `engine/MediaSource`, `engine/src/VideoMediaSource.cpp`, `engine/VideoDecoder` + `src/VideoDecoder{Windows,Linux,Unsupported}.cpp` / `VideoDecoderMac.mm`, `src/Cameras.h` + `CameraJuce.cpp` / `CameraLinux.cpp`, `MediaTexture` in `RenderEngine.cpp`, `LayerCompositor::{overlay,stamp}`, `ui/MediaMenu` | `MediaSourceTests`, `VideoTests` (each platform encodes its own clip), `LayerCompositorTests` |

The values flow from the shells to the render thread like this:
- `EngineControls::visual`/`gate` → `Visualizer::setControls` → `LayerChannel`
  (`setVisual`, `setGate`).
- Media: `LayerChannel::setMediaSource`, `setMediaBlend`, and `setMediaTimeline`, which
  `Visualizer::processAudio` publishes from the host transport.
- The primary layer's Visual controls, media and effects act on the whole canvas when the engine
  mixes several layers.

Dev tool: `mdw-view [--set <paramId>=<value>]... [--media <file>] [--media-blend 0-6]` shows any of
this on a real preset without the UI.

## Lessons from this stage (also in memory)

- **MSVC hides GCC/Clang errors.** Adding a field to a struct that is brace-initialised by position
  broke every non-Windows CI job. Give such structs constructors. Build `ci-linux` in Docker before
  pushing (memory `milkdawp2_local_linux_ci.md` has the commands).
- `juce_video.h` `#undef`s `JUCE_USE_CAMERA` on Linux, so test it with `defined(...)`.
- The RTSan build exports every symbol, so JUCE's bundled libjpeg captures GStreamer's JPEG plugin.
  That's why the Linux test clip is Theora, not MJPEG.
- Plugin tests need JUCE running (`plugin/tests/JuceEnvironment.cpp`). Without it, every timer,
  async update and message listener asserts, and singletons leak. That was the old leak report.
- Measure visual bugs before fixing them. The Burn in flicker was found by diffing ~20 quick
  `mdw-view` screenshots per blend mode.

## Stage B so far (2026-10-06)

8.7–8.11 are done but not committed: the preset-variable patch (ADR-0012), the `.milkdawp`
format and loader (ADR-0013), and the converter with the bundled pack (ADR-0014).

| Item | Code | Tests |
|---|---|---|
| 8.7 patch | `vcpkg-overlays/projectm/0001-set-preset-variable.patch`, `ProjectMInstance::setPresetVariable` | `HeadlessRenderTests` `[presetvars]` |
| 8.9 format | `core/MilkdawpPreset` (parse, serialize, `compileForProjectM`, Macro defaults/names) | `MilkdawpPresetTests` |
| 8.10 engine | `Director` (`issue()` compiles; `currentPreset()`), `PresetHandoff` `.milkdawp` flag, `RenderEngine` (sets `mdw_*` every frame; no post Zoom/Rotation on a `.milkdawp` layer), `EngineControls::macros` → `LayerChannel::setMacros` | `HeadlessRenderTests` `[milkdawp]`, `DirectorTests`, `ControlMappingTests` |
| 8.10 shells | Lock Macros defaults in `PluginProcessor::timerCallback` / `MainComponent::applyMacroLock`; `VisualSettingsPanel::setPresetControls`; app opens `.milkdawp` | `VisualSettingsTests` |
| 8.10 library | `Playlist::scanFolder` (hides a `.milk` beside its `.milkdawp`), `PresetMetadata::keyFor` | `PlaylistTests`, `PresetMetadataTests` |
| 8.11 converter | `core/MilkConvert` (`analyzeMilk`, `convertMilk`, `convertFolder`, `withoutRandomness`), `core/Sha256`, `tools/mdw-convert` (`Verifier` for `--verify`) | `MilkConvertTests`, `Sha256Tests` |
| 8.11 pack | `milkdawp_preset_pack` target (`tools/mdw-convert/CMakeLists.txt`), `cmake/milkdawp-pack-exclusions.txt`, `BundledContent::defaultPreset` prefers the `.milkdawp`, file associations, `package.sh` check | `BundledContentTests` |

Verified for 8.7–8.10: Windows Debug (455 tests), Linux GCC (`ci-linux`) and Clang ASan
(`ci-linux-asan`) in the container. For 8.11: see the roadmap entry. Not yet: macOS, TSan/RTSan,
and any hand test. The build now converts the bundled pack, so any bundled preset is a
`.milkdawp` to hand-test with: automate its Macros in REAPER.

Still to do in Stage B:
- A hand test of the proposed Macros (names, ranges) and of ADR-0013's Zoom/Rotation/Trails
  constants, which are guesses until then.
- After a pack bump, regenerate the exclusions (ADR-0014): build Release `mdw-convert`, then
  `mdw-convert --verify --dry-run --journal j.txt --write-exclusions cmake/milkdawp-pack-exclusions.txt
  "build-win/content/Presets/Cream of the Crop"` (a couple of hours; rerun with the same journal
  to carry on after a crash).

## Next: the 8.12 spike

Before planning 8.12, spike the finding from 8.6f. projectM 4.2's
`projectm_set_texture_load_event_callback` takes an app-supplied GL texture for any sampler a preset
names, which may give `sampler_camera`/`sampler_video` without patch 2. The catch: projectM owns the
texture, deletes it when it is done, and loads it once per preset.

Other notes:
- The effects shaders are GLSL 3.30 core; Android (Phase 7) will need ES variants.
- Android video (MediaCodec) waits for Phase 7.
