# MilkDAWp 2 — Layers (post-1.0 brainstorm)

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done, `[-]` dropped.

Living doc: UX vision, architecture notes, roadmap, and open questions for the Layers backlog item
(development_roadmap.md, Post-1.0 backlog; §3 tier table; 5.6 multi-instance; D15; ADR-0008, ADR-0009).
Add dated notes as decisions are made: `Note (YYYY-MM-DD): ...`.

---

## 1. Pitch

One visual canvas, many musical sources. Drop a MilkDAWp on the kick track, another on the vocal, another on
the synth bus. Each drives its own visual (its own preset, playlist and transitions), and they appear together
in one Output window. Each layer reacts to *its* track, not the master mix.

## 2. The user's experience

**Principle: nothing changes until you ask.** One instance behaves exactly as today. A second instance never
hijacks the first.

The shared canvas is **an Output window that accepts more than one source**. There is no separate "canvas"
object, no group, and no host election. An instance that other instances point at *is* the canvas.

### 2.1 Settings → Output (new modal, same style as Transitions)

| Setting | Behaviour |
|---|---|
| **Default to fullscreen** | When on, this instance's Output window opens fullscreen. The pop-out button on an instance configured this way opens the window fullscreen. |
| **Target** | `New Window` (default; today's behaviour) or `Other Instance`. |
| **Instance picker** | Only when Target = `Other Instance`. Lists *instances* in this project (not open windows, which are closed by default after load). Each entry shows the user-editable instance label plus the host track name/colour when the host provides it. Instances that themselves target another instance are greyed out (no chains, no cycles). |
| **Target screen** | Only when Target = `New Window` **and** more than one monitor exists. Opens a monitor picker; hovering a monitor draws a highlight frame around that screen. When Target = `Other Instance`, show read-only "Shown on: *screen* via *target label*". |
| **Sources** | Only on an instance that has at least one sender. One row per sender: thumbnail, label, order, blend, opacity, mute/solo. See 2.3. |
| **Show my own visual** | Only on an instance that has senders. On by default (the target's own visual is the bottom layer). Off makes the instance a **pure mixer**. Never forced. |

### 2.2 Flow

1. **Insert instance #1.** Solo, as today.
2. **Insert instance #2.** Both show a quiet, dismissible hint: *"2 MilkDAWp instances in this project. Send
   this one to the other's Output? [Open Output settings]"*. Never auto-connected.
3. **Instance #2 → Output → Target = Other Instance → pick #1.** #2 stops opening its own window; its visual
   now appears as a layer in #1's Output window. #2's editor keeps showing its own layer as a preview.
4. **Pop-out on #2** opens (or focuses) **#1's** Output window, since that is where #2's output lives.
5. **#1's Output modal** now shows a Sources list. Fade, blend, reorder, or turn off "Show my own visual" to
   make #1 a pure mixer.
6. **Two independent canvases** need nothing special: send some instances to #1 and others to #3.

### 2.3 Composition parameters

Opacity, blend mode, mute and solo are **parameters on the sender's own instance**, so they appear as
automation lanes on that track (fade the vocal visual in at the chorus with an ordinary envelope). Layer order
is a stored value on the sender. The Sources list on the target is just a convenient editor for those values.
Blend modes: Normal / Add / Screen / Multiply / Luma key / Alpha key / Mask.

### 2.4 Lifecycle rules

- A target is stored by a stable **per-instance GUID** in plugin state, not a window handle.
- Instance load order is not guaranteed. A sender resolves its target **lazily** and reconnects when the
  target appears in the registry.
- Fullscreen / screen choices belong to the instance that owns the window.
- Hosts that sandbox plugins in separate processes: the instance picker is empty with a plain explanation.
  Link mode (L7) is the long-term answer.
- **Target deleted or missing: deferred** (see §8, Q1).

**Standalone app:** same Output modal; inputs are devices / channel pairs (loopback, mic, file) instead of
tracks, and layers are added with a "+ Layer" button.

## 3. Architecture (recommended)

**Model A, "hub renders, layers send".** The *targeted* instance's `RenderEngine` holds N layer slots (each a
`ProjectMInstance` plus its own FBO) in its single GL context. Sender instances do no GL while attached.

- Each sender keeps its own `AudioRing` and its own `Director`, so per-track beat analysis, playlists,
  transition modes and all existing parameters keep working and state stays per-instance.
- On attach, the sender's Director output (`PresetHandoff` / `TransitionExecutor` target) is re-pointed from
  its own engine to a slot in the target's engine. The target's render thread pulls PCM from each sender's ring
  (`PcmFeeder` per slot).
- Compositor pass in the target: sort by order, blend each layer FBO into the canvas FBO with a small shader
  set. Output goes to the existing surfaces / readback / Output window unchanged.
- **Registry:** process-wide static in the plugin DLL (v1 and dev-alt-identity builds are separate DLLs, so
  they never see each other). Keyed by **target GUID**. Holds `shared_ptr` handles (ring, layer-param
  snapshot, preview texture slot). Lifetime and detach-on-destroy are the hard part (§7).
- **No chains:** an instance with senders cannot itself target another instance (and vice versa). Enforced in
  the picker and re-checked on attach.
- The target's own visual is just slot 0. "Show my own visual" off = slot 0 skipped (pure mixer).

**Rejected, Model B ("every instance renders, share textures"):** more robust standalone, but N GL contexts
and render threads, `wglShareLists` across N contexts (ADR-0009 shows the NVIDIA pain), and cross-feed becomes
impractical. Revisit only if the registry proves too fragile.

**Rejected, groups A–H + elected canvas host:** replaced by the target model (2026-10-01). Same engine, but
the target model reuses the Output window concept, makes independent canvases free, and removes host election.

**Engine refactor first:** `RenderEngine` becomes "N slots, N=1 for Solo". The Solo path is unchanged for
users. This is the 1.0 guardrail already noted in roadmap 2.13.

Facts this rests on (as of 2026-10-01):
- Today each plugin instance owns a `Visualizer` (ring + Director + `RenderEngine` with its own offscreen
  context and render thread). There is no cross-instance state.
- `OutputWindow` is owned by the processor and survives editor close; fullscreen toggling exists.
- `ProjectMInstance` is already a separate object (ADR-0008); `RenderEngine` owns exactly one.
- projectM 4.2 pin provides `render_frame_fbo`, `set_frame_time` and `burn_texture`. `burn_texture` is not yet
  wrapped in `ProjectMLibrary`. All handles share one GL resolver.
- Preset compile is synchronous on the render thread, so N layers stack compile hitches.

## 4. Musical glue

- **Group transition sync:** all layers cut on the same bar (shared `BeatClock` / host transport), or staggered
  offsets (kick layer cuts on 1, vocal layer on 3). Configured on the target, applied to its senders.
- **Cross-feed:** wrap `projectm_opengl_burn_texture`; per-layer "feed from layer X" amount so visuals bleed
  into each other.
- **Animated masks:** split, radial, luma/alpha key, MilkDrop3-style blend patterns driven by `BeatClock`.
- **Inputs:** track audio (default), sidechain bus on one instance, later per-band splits so one track can feed
  a "bass layer" and a "hats layer".

## 5. Performance

N projectM at full resolution is expensive.
- Per-layer render scale; skip layers with opacity 0 or muted.
- Reuse the existing pause-when-no-surface logic.
- GPU budget governor: lower FPS/scale of lower layers first.
- Stagger preset compiles so at most one layer compiles per frame (reuse `PresetCompileTimeCache`).
  Done (2026-10-01): with several layers at most one loads a preset per frame, the layer served first
  rotates, and the others' due transitions wait a frame. A lone layer is never deferred. Test: five layers
  all due at once all get their preset.
- Show per-layer cost in the Sources list. Done (2026-10-01), see L5.

## 6. Roadmap (proposed)

- [~] M0 (M) **Ships early, independent of Layers.** Monitor picker with hover highlight frame (click-through
      overlay per display, DPI, unplugged-monitor fallback), plus the Output settings modal with "Default to
      fullscreen" and "Target screen". Note (2026-10-01): plugin done and tried by the user ("worked well").
      `core/DisplayLayout`, `WindowLayout` keys `outputDefaultFullscreen` / `outputTargetDisplay`,
      `ui/OutputSettings`, processor + editor wiring. Display identity is the display's own bounds, so a saved
      screen that is unplugged falls back to automatic and returns when reconnected. Note (2026-10-01,
      later): the standalone `app/` shell has it too (View > "Output settings..."; `AppState` keys
      `outputDefaultFullscreen` / `outputTargetDisplay`; the drawer's Output button honours the fullscreen
      default; `OutputSettingsPanel::setLayersAvailable(false)` hides the Layers rows there, since an app has
      no other instances to link to). Built and unit-tested, not yet seen on screen.
- [~] L0 (M) Multi-slot `RenderEngine`; Solo = one slot; no behaviour change; tests. Note (2026-10-01): first
      half done. New `engine/LayerChannel.h` (audio ring ref + `TransitionExecutor` + `PresetHandoff`: what a
      Director talks to). `RenderEngine` owns one as `primary_`; `pushTransition()` / `presetHandoff()` forward
      to it, so `Director` and every shell are unchanged. The render loop in `RenderEngine::run()` now iterates
      a `std::vector<std::unique_ptr<Layer>>` (instance + PCM feeder + load-failure flag) for beat sensitivity,
      output size, transitions and audio feed, with exactly one layer today. All suites pass (engine 54 cases,
      plugin, app, core, ui), incl. a new test that hands a preset to the primary layer and sees the render
      thread load it. Not covered: the rejected-preset path (projectM 4.2 accepts arbitrary text in
      `loadPresetData`). Still to do: `addLayer` / `removeLayer` (render-thread handshake) and drawing more
      than the primary, which needs the compositor (L2). `RenderStats` still describe the primary layer only.
      Note (2026-10-01, later): second half done. `RenderEngine::addLayer(LayerChannel&)` /
      `removeLayer(...)` block until the render thread (which owns the context) has created/destroyed the
      instance; requests still queued when the thread ends are answered `false`, so a caller can never hang.
      `kMaxLayers = 8` (primary included), `RenderStats::layers`. With more than one layer each draws into its
      own `GlFrameTarget` and `LayerCompositor` mixes them into the output frame; one layer still draws
      straight into the output (unchanged path). Hidden or opacity-0 layers are neither fed nor drawn.
      Tests: 11 `[layers]` cases incl. add/compose/hide/remove/re-add/duplicate/cap-at-8/not-running.
- [~] L1 (M) Two layers in `tools/mdw-view` from two WAVs, hard-coded blend. Proves N projectM in one context
      and the 4.2 FBO path. Note (2026-10-01): the core risk is retired. `engine/tests/HeadlessRenderTests.cpp`
      ("two projectM instances share one context...", tag `[layers]`) renders two `ProjectMInstance`s with
      different presets into separate FBOs in one context, interleaved, on this box (Windows, real GPU):
      both draw, they differ, and instance A's time-driven border pixel matches its solo run to within 2/255
      at frames 10/40/70. Caveat: the border check proves A's output is undisturbed, not that B never writes
      into A's FBO; the "frames differ" check only partly covers that. The planned mdw-view demo was dropped
      in favour of automated coverage on real engines (`RenderEngineTests.cpp`, `plugin/tests/LayersTests.cpp`),
      which exercise the same path with less ceremony. Still unseen by a human: a real composite on screen.
- [~] L2 (M) Compositor shaders; blend/opacity/mask/mute/order params in `ParameterModel` (regenerate
      `docs/parameters.md`). Note (2026-10-01): compositor done. `engine/LayerCompositor` (GLSL 3.30, one
      fullscreen draw per layer, blend via GL blend state, canvas alpha stays 1, GL state saved/restored for
      projectM). Blend modes: Normal / Add / Screen / Multiply, verified on the GPU against hand-computed values
      (`LayerCompositorTests.cpp`). `LayerChannel` carries opacity / blend / visible / order atomics. The
      parameters are in: `layerOpacity`, `layerBlend`, `layerMute`, `layerOrder` (`ParameterModel`,
      `ControlMapping`, `docs/parameters.md` regenerated), automatable on each instance's own track. Blend
      modes are now Normal / Add / Screen / Multiply / **Luma key** (the layer's black becomes transparent:
      alpha = 4 x luma, clamped; no extra parameter). Still to do: alpha key (projectM's output has no alpha,
      so it needs a different source) and animated masks (BeatClock-driven patterns).
- [~] L3 (L) Registry keyed by target GUID, attach/detach lifecycle, Director re-pointing, no-chain rule;
      two plugin instances in REAPER. Note (2026-10-01): built and tested in-process; **not yet tried in a
      DAW**. How it works: `plugin/src/LayerRegistry` (process-wide, per plugin binary). A sender calls
      `RenderEngine::yieldPrimaryLayer(true)` (its engine stops touching its own primary layer), then the hub
      does `addLayer(sender.primaryLayer())`: the sender's `LayerChannel` (its ring, its own Director's
      presets and transitions) is drained by the hub's render thread, so per-track analysis and every existing
      parameter keep working. Detach reverses it. Lifetime rules (documented in `LayerRegistry.h`): all attach
      bookkeeping and every engine call is under the hub entry's mutex; a hub detaches its senders before
      clearing its engine pointer, a sender detaches before its own engine dies. State: `instanceId`,
      `instanceLabel`, `outputTargetInstance` (additive keys); a duplicated state gets a fresh id. The link is
      lazy: it is made when the target appears (registry notifies, `reconcileLayers()` runs on the message
      thread). Chains are refused both ways. Tests (`plugin/tests/LayersTests.cpp`, 11 cases on real engines,
      none skipped): identity, naming, parameters to engine, state round trip, attach/detach, no chain, lazy
      link, hub destroyed first, sender destroyed first, mixed teardown of 1 hub + 3 senders. pluginval
      (strictness 5, no GUI) passes on the VST3.
- [~] L4 (M) Extend the M0 Output modal: Target, instance picker, instance labels; pop-out opens target's
      window. Note (2026-10-01): done in code, **not yet seen in a DAW**. The Output panel now has Name (the
      user's label; falls back to the host's track name via `updateTrackProperties`), "Show on" (New window or
      another instance; senders greyed out, an unloaded saved target shown as "Not connected (waiting)"), and
      hides the screen picker while sending. Pop-out and F11 on a sender forward to the hub's window. A
      sender's own editor shows a "Shown in <hub>'s Output window" label instead of a frozen picture.
- [~] L5 (M) Sources list in the target's Output modal (thumbnails, order, blend, opacity, mute/solo, "Show my
      own visual" / pure mixer). Note (2026-10-01): the *controls* exist, but per instance: the Output panel
      of any instance that is in a canvas shows Opacity, Blend, Order and Mute (worded "Hide my visual" on the
      instance that owns the canvas = the pure mixer, never forced), bound to the real parameters.
      Note (2026-10-01, later): the hub-side **Sources list** is in. On the instance that owns the canvas, the
      Output panel shows one row per sender, bottom to top: name, opacity, blend, mute, edited through the
      sender's real parameters (the hub reaches them via `LayerRegistry::Entry` hooks, so host undo and
      automation see them as ordinary edits). Per-layer GPU cost is measured (separate `GL_TIME_ELAPSED`
      queries per layer plus the mix, read after `glFinish`; `LayerChannel::gpuMs()`), shown as the name's
      tooltip, and the name turns amber from 4 ms and red from 8 ms. Not built: thumbnails; scrolling for
      many sources (7 senders make the panel tall, and at the 480x270 editor minimum it is clipped).
- [~] L7 (M) Transition sync; cross-feed (`burn_texture` in `ProjectMLibrary`). Note (2026-10-01): **transition
      sync is in, cross-feed is not.** Finding first: beat-quantized cuts were *not* naturally in step across
      instances, because `TransitionScheduler` counted `bars` x 4 beats from whenever each instance's
      scheduler happened to start. New: `TransitionSchedulerConfig::gridAnchored` + `gridOffsetBeats`: cut on the
      beats where `beatIndex % (bars x 4) == offset` (offset wraps). Host beat indices are absolute
      (`floor(ppq)`), so with "BPM From DAW" every instance using the same bars and offset cuts on the same
      beat, and different offsets stagger them (kick on 1, vocal on 3). With the *detected* tempo, indices are
      private to each instance, so the grid only means something per instance (the tooltip says so).
      Parameters `transitionGridSync` / `transitionGridOffset` (0-15 beats), through `ControlMapping` ->
      `EngineControls` -> `Director`, and a "Bar grid" toggle + offset slider in the Transition panel (shares the
      fifth row of the left column, so the panel keeps its height; plugin attachments and app bindings).
      Tests: grid fires only on its offset; two instances started at different times cut on the same beats
      (and, un-anchored, demonstrably do not); offsets stagger and wrap; control mapping; panel relevance. Not
      done: Hybrid and Timed do not use the grid (Hybrid already snaps to bar lines, `beat % 4 == 0`);
      cross-feed.
- [ ] L8 (L) Standalone multi-input; sidechain input; per-band splits.
- [ ] L9 (L) Link mode / out-of-process for sandboxing hosts (shares work with texture-sharing output).

## 7. Risks

- Registry lifetime and thread safety (attach/detach while audio runs).
- Host instance load/destroy order; lazy target resolution.
- Hosts that sandbox plugins.
- GPU cost scaling; compile hitches stacking on one render thread.
- projectM's process-global GL resolver (all instances must pass the same one).
- State migration for target GUID / order / blend (StateSchema version bump, additive keys); undo/redo.
- Monitor picker: DPI, hot-plug, overlay windows stealing focus.
- Instance naming: host track names are not always available, hence the user-editable label.

## 8. Open questions

1. **Deferred:** what happens when a sender's target is deleted or missing at load? Candidates: fall back to
   own New Window, stay quiet until the target returns, or prompt. Remember the GUID either way so it can
   reconnect.
2. How should the GPU budget governor degrade layers (scale first, then FPS, then drop bottom layer)? Per-layer
   GPU cost is now measured (L5), so this can be decided on real numbers; nothing acts on them yet.
3. **Animated masks** (L2 leftover): which patterns (split, radial, wipe, ripple?), per layer or per canvas,
   and driven by what phase (host bar position, so they stay in step across instances, vs. each instance's own
   `BeatClock`)? Needs a decision before building: it adds a mask choice, a speed, and a phase input to the
   compositor.
4. **Cross-feed** (L7 leftover): `projectm_opengl_burn_texture` is a one-shot draw into a layer's feedback
   buffer at full strength, so "layer A bleeds into layer B by X%" is not a single call. Options: burn a
   pre-faded copy each frame, or only offer pulse-style cross-feed on beats. Not wrapped in `ProjectMLibrary` yet.
5. Sources list at many senders: scroll, or a separate window? (7 senders make the Output panel taller than the
   480x270 minimum editor.)
6. The Output panel's height at small editor sizes in general: scroll the panel, or move Layers into its own
   tab?

## 9. Decisions log

- (2026-10-01) Connection model is "Target = Other Instance" via an Output settings modal, replacing groups
  A–H and elected canvas host.
- (2026-10-01) A targeted instance may be a pure mixer, but it is optional and never forced ("Show my own
  visual", default on).
- (2026-10-01) Pop-out on a sender opens the target's Output window.
- (2026-10-01) Missing/deleted target behaviour deferred (§8 Q1).
- (2026-10-01) Max layers per target: 8, counting the target's own visual. 4 is the tested/supported
  configuration for the first release; 5–8 allowed but unvalidated. Cap is a single constant. (`kMaxSurfaces`
  is output surfaces, unrelated.)
- (2026-10-01) Plugin multi-instance has priority over standalone multi-input.
- (2026-10-01) Monitor picker ships early, ahead of Layers, as a standalone Output improvement (v1 had one).
