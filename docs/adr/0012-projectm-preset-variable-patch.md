# ADR-0012: Carry our own projectM patch for external preset variables

Status: Proposed (Phase 8.7 spike done, 2026-10-05). Amends ADR-0008 (the
overlay port now also patches projectM) and the licensing notes of ADR-0006.

## Context

Phase 8 Stage B (`development_roadmap.md` Phase 8, design in
`custom_format_exploration.md` §4.2) needs the DAW to reach inside a running
preset: macros (`mdw_m1`..`mdw_m8`), new audio inputs (`mdw_beat_phase`,
`mdw_bpm`, ...) and the override inputs that generated `.milkdawp` code reads.
projectM's public API has no way to set a preset's variables. Checked on
2026-10-05 against upstream master `dd89dfb` (latest tag still v4.1.7): only
user sprites have `projectm_sprite_get_var`/`_set_var`. Draft PR #971 (a
read-only expression variable *watch* API, stalled since 2026-02) is the
closest; no PR or issue proposes a setter.

The variable store lives in projectm-eval, which projectM already links:
`projectm_eval_context_register_variable(ctx, name)` returns the one stable,
case-insensitive storage that code compiled before *or* after the call uses.

One detail of MilkDrop shaped the design. A preset's per-frame and
per-vertex code run in **separate** eval contexts, and the only values
MilkDrop copies from one to the other each frame are the built-ins and
`q1`..`q32`. A custom variable set in per-frame code reads 0 in per-vertex
code. The exploration doc's plan to reach per-vertex code "via the usual
copy" would not have worked for `mdw_*` names; the spike's control test
confirms it.

## Decision

1. **Patch projectM in our overlay port and never submit it upstream.**
   Upstream review is slow, and nothing in Stage B should wait on it. The
   patch is `vcpkg-overlays/projectm/0001-set-preset-variable.patch`, applied
   by `vcpkg_from_github(... PATCHES ...)`; the port's `port-version` is 1.
2. **API:** one function,
   `projectm_set_preset_variable(instance, name, value)` (in `parameters.h`).
   - `ProjectM` keeps every name and value set so far. Each call forwards to
     the active preset and, during a soft cut, to the incoming one too.
   - Every new preset receives all stored values *before* `Initialize()`, so
     `per_frame_init` code already sees them.
   - `MilkdropPreset` registers the name in **both** its per-frame and
     per-vertex contexts and writes the value into both every frame, right
     after the built-in state is loaded and before per-frame code runs. So
     the value holds across frames even if the preset's own code assigns it.
   - Names are case-insensitive (as in the expression language). NULL or
     empty names are ignored. Render thread only, like the rest of the API.
3. **Our side:** `ProjectMFunctions::setPresetVariable` is a *required*
   symbol, like every other entry: a stock projectM fails `load()` with a
   message naming the patch. That's fine, because every build (Windows,
   macOS, Linux CI, the devcontainer) builds projectM from the overlay port,
   and its binary caches are keyed on `vcpkg-overlays/**`.
   `ProjectMInstance::setPresetVariable(name, value)` wraps it.
4. **Licensing:** we now ship a modified LGPL library. Each patched file has
   a dated change notice at its top (LGPL-2.1 §2a), the patch itself is
   LGPL-2.1-only, and `THIRD_PARTY_NOTICES.md` says the source offer covers
   the upstream commit plus our patches. Dynamic linking is unchanged.

## What the spike proved (8.7)

Headless tests in `engine/tests/HeadlessRenderTests.cpp` (`[presetvars]`).
Each preset turns a variable into its outer border colour, drawn opaque
every frame through a plain composite shader, and the test reads that pixel
back. All pass on Windows (WGL), Debug, against the patched library:

| Claim | Test | Result |
|---|---|---|
| Per-frame code reads a value set from outside | border red = `mdw_m1` = 0.75 | pass (within ±4/255) |
| Per-frame **init** code reads a value set before load | `init_seen = mdw_m1` shown in green | pass |
| It survives frames, even if the preset overwrites it | preset sets `mdw_m1 = 0` every frame; still 0.75 after 60 frames | pass |
| A new value applies on the next frame | 0.75 → 0.25, init value unchanged | pass |
| Per-vertex code reads it | per-vertex `reg01 = mdw_m2`, per-frame shows `reg01` | pass (0.6) |
| ...and wouldn't without the patch writing both contexts | per-vertex `reg02 = plain_var` reads 0 | pass (control) |
| Both presets receive it during a soft cut | A shows it in red, B in green, both loaded with 0; set once at 25% of a 2 s blend; A checked at 25%, B at 75% | pass, 40/40 runs (projectM picks a random transition); each channel 64–91 of 255 above blue, which neither preset draws |
| *(how the soft-cut test was tuned)* | A first version checked both presets at 50% | flaked 1 in ~6: some transitions show one preset far more than the other mid-blend (red only +8.7). Not a patch issue: A always got the value |
| A value set earlier reaches a preset loaded later | set while preset 1 is active, read by preset 2 | pass |

The full suite stays green (`ctest`, 437 tests, Windows Debug). Not yet run:
Linux CI (Mesa llvmpipe) and macOS.

## Consequences

- Stage B can go ahead on projectM: 8.9–8.11 can rely on `mdw_*` variables
  in per-frame, per-frame init and per-vertex code.
- **Not covered:** custom shape and custom waveform code (each has its own
  eval contexts). Generated `.milkdawp` code for those would have to copy a
  value into a `q` variable in per-frame code (MilkDrop passes `q1`..`q32` to
  shapes and waves). Extend the patch only if 8.11's converter shows a real
  need.
- Setting a built-in name (e.g. `zoom`) replaces the value per-frame code
  starts from. That's allowed but isn't what the API is for; the override
  layer appends generated per-frame lines instead (exploration doc §4.2).
- Values are never removed: a name set once keeps being written into every
  later preset. That's harmless for presets that don't use it (an unused
  variable); the engine sets every control's current value anyway.
- Every projectM bump must re-apply the patch (`portfile.cmake` says how). If
  upstream PR #971 lands, recheck the patch: it touches the same code.
- The own-renderer decision (8.D) no longer weighs "upstream's response",
  only the cost of carrying this patch and any later one (8.12's external
  textures, unless 4.2's texture-load callback is enough).
