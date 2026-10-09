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
| 8.11 pack | "Cream of the CrAWp", made by the `milkdawp_preset_pack` target (`tools/mdw-convert/CMakeLists.txt`) from the fetched pack in `_deps/cream-of-the-crop`; `cmake/milkdawp-pack-exclusions.txt` (the 67 projectM can't load); `BundledContent` (`kPackFolderName`, `fromOldPack` for 1.0 sessions); installers remove the old folder; file associations; `package.sh` check | `BundledContentTests` |

Verified for 8.7–8.10: Windows Debug (455 tests), Linux GCC (`ci-linux`) and Clang ASan
(`ci-linux-asan`) in the container. For 8.11: see the roadmap entry. Not yet: macOS, TSan/RTSan,
and any hand test. The build now converts the bundled pack, so any bundled preset is a
`.milkdawp` to hand-test with: automate its Macros in REAPER. The shipped pack is "Cream of the
CrAWp" (converted only, no `.milk`, 2026-10-07).

Still to do in Stage B:
- A hand test of the proposed Macros (names, ranges) and of ADR-0013's Zoom/Rotation/Trails
  constants, which are guesses until then.
- After a pack bump, regenerate the exclusions (ADR-0014): build Release `mdw-convert`, then
  `mdw-convert --verify --dry-run --journal j.txt --write-exclusions cmake/milkdawp-pack-exclusions.txt
  build-win/_deps/cream-of-the-crop` (about 3 hours; rerun with the same journal
  to carry on after a crash).

## 8.12 (2026-10-08): media and music for presets, no second patch

Done, not committed; ADR-0015. The spike settled it: at our pin projectM does *not* own a texture
the load callback hands over (its header is stale), and samples it every frame, so one texture per
layer that the engine refreshes is a live feed.

| Item | Code | Tests |
|---|---|---|
| 8.12 textures | `ProjectMInstance::setTextureLoadCallback`; `onTextureLoad` and `Layer::presetMedia` in `RenderEngine.cpp` (both names, full strength, cropped, flipped) | `HeadlessRenderTests` `[externaltex]` (the spike), `RenderEngineTests` `[externaltex]` |
| 8.12 music | `core/PresetInputs` (`BeatSnapshot`, `presetAudioInputs`), `BeatClockState::beatsPerBar`/`beatInBar`, the director's broadband `onsets`, `LayerChannel::setBeat`; the render thread sets `mdw_beat_phase`/`bar_phase`/`bpm`/`onset` | `PresetInputsTests`, `DirectorTests`, `RenderEngineTests` `[presetvars]`, `HostTransportTests`, `BeatClockTests` |
| 8.12 presets | `resources/presets/MilkDAWp Originals/` (13: Camera Tunnel, Bar Spinner, Onset Edges, then ten camera presets, ADR-0015), copied into the pack by `milkdawp_original_presets`; `package.sh` check | `HeadlessRenderTests` `[originals]` |

Verified: Windows Debug (476 tests), Linux GCC (`ci-linux`) and Clang ASan in the container; checked by eye in
`mdw-view` with an image. To try by hand: each Originals preset with music playing (REAPER, host
tempo, and the app's detector), a camera and a video file; Lock Macros off so the Macros start on
the preset's defaults (`mdw-view` doesn't do that: pass `--set macro1=...`).

## Next: the 8.D decision gate

Own renderer go / no-go, as an ADR from Stage B. Neither 8.12 half needed a projectM change, so
the only patch carried is still ADR-0012's.

Other notes:
- The effects shaders are GLSL 3.30 core; Android (Phase 7) will need ES variants.
- Android video (MediaCodec) waits for Phase 7.
