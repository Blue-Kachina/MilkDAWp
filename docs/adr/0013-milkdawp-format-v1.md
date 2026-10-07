# ADR-0013: The `.milkdawp` format, v1, and how it loads on projectM

Status: Proposed (Phase 8.8–8.10, 2026-10-06). Builds on ADR-0011 (the Visual
globals and Macros) and ADR-0012 (the `projectm_set_preset_variable` patch).

## Context

Phase 8 Stage B gives presets controls the DAW can automate
(`development_roadmap.md` 8.8–8.10, design in `custom_format_exploration.md`
§4.2, §4.3 and §5). The format was already chosen on 2026-10-05: a
**superset** of `.milk`, where a `.milkdawp` is a complete `.milk` plus lines
whose keys start with `mdw_`. This ADR records what implementing it settled:
how the file is read, how controls become code, and what the engine and shells
do with it.

Two readers set the rules, because a `.milkdawp` has to behave like a `.milk`
in both:

- **projectM** (`PresetFileParser`, at our pinned commit): a key is the text
  before the first space or `=`, lower-cased; the first occurrence of a key
  wins; lines with no delimiter, or one in the first column, are skipped.
  Code is read as `per_frame_1`, `per_frame_2`, ... and **stops at the first
  missing number**.
- **MilkDrop 2** (`state.cpp`, `_GetLineByName` and `ReadCode`, read from the
  open-source release on 2026-10-06): the same key rule (case-sensitive), the
  first occurrence wins, unknown keys are never looked up, and code stops at
  the first missing number in the same way. MilkDrop 1 reads the `[preset00]`
  INI section.

## Decision

### 8.8: stock tools open a renamed `.milkdawp` as the plain preset

Both readers only look up keys they know, and no MilkDrop key starts with
`mdw_`, so a `.milkdawp` renamed to `.milk` plays as its original preset in
projectM and in MilkDrop 2. Proven headless for projectM (the parser our patch
leaves alone): the raw file, with `mdw_` lines before and after `[preset00]`,
in mixed case and with a space instead of `=`, renders the same as the plain
preset. MilkDrop is from reading its source, not run. Two caveats for the user
docs: both programs list only `*.milk`, so the file must be renamed; and
MilkDrop's preset editor drops the `mdw_` lines when it saves.

### 8.9: `core/MilkdawpPreset`

- **Reading** (`parseMilkdawp`) never fails. A line is ours when its key (the
  projectM rule) starts with `mdw_`, in any case. Every other line, with its
  own line break, goes to `milk`, so taking our lines out gives the original
  **byte for byte**. Duplicate keys: the first wins, as in both readers.
- **Keys, v1**: `mdw_format`, `mdw_title`, `mdw_source`, `mdw_source_sha256`,
  `mdw_tags` (comma list), `mdw_renderer`, and per control
  `mdw_ctl_N_{name,slot,mode,target,stage,min,max,default,value,code}`, N 1–99.
  `slot` is `macro1`..`macro8`; `mode` is `replace`, `offset`, `scale`, `rate`
  or `expr`; `stage` is `frame` (default) or `pixel`. Scalar values may carry a
  trailing `; comment`; names and code may not.
- **Problems** are sentences, reported once per file per session in the
  diagnostics' recent errors. A broken control is left out (bad slot or mode,
  missing target, `code` or `value`, min equal to max); a bad number falls back
  and a default outside min..max is clamped. The preset itself always plays.
- **Unknown keys** (a typo, a newer format's, or ones sketched for later such
  as `mdw_fx_*`) are kept in order and written back on save.
- **Versions**: a missing `mdw_format` reads as 1, with a note. A newer
  format loads what this build knows, with one note, and keeps the rest.
  `migrate()` is where version steps will go; there is only v1.
- **Writing** (`serializeMilkdawp`) puts our block right after `[preset00]`
  (MilkDrop 1 reads that section), or at the top if there is none, in the
  file's own line endings. Reading back gives the same preset.
- **Macros**: each Macro's default and name come from the first control on
  it. Several controls may share a Macro (one knob, several targets).

### Compiling controls (`compileForProjectM`)

The loader hands projectM plain `.milk` text: `milk` plus generated lines.
Our `mdw_` lines never reach it.

- **Numbering**: generated lines take the numbers right after the preset's
  own, because projectM stops at the first gap. The exploration doc's
  `per_frame_9NN` would never have been read. Lines a preset has *after* a
  gap (never read) are renamed `mdw_unread_...` in the compiled text, so
  filling the gap can't bring them in.
- **Order**: controls, in number order, then the Visual globals; so the
  user's knob acts on top of the preset's own controls.
- **A preset whose code ends without `;`** (legal at the very end) gets a
  leading `; ` on our first line. projectm-eval rejects a leading `;` with
  nothing before it, so it is added only when needed (comments ignored).
- **Value of a control**: `c = default + (max - min) × (mdw_mK − macroDefault)`,
  not `min + span × mdw_mK`. Every literal is written with 17 significant
  digits of the float the engine passes. With the Macro on its default the
  bracket is exactly 0 and `c` is exactly the default, so neutral defaults
  give `x + 0`, `x × 1` or `x + (v − x) × 0`, and the preset runs exactly as
  before. (The Macro values snap to their target in the engine's smoothing, so
  "on its default" is exact too.)
- **Modes** (§4.3): replace `t = t + (value − t) × c`; offset `t = t + c`;
  scale `t = t × c`; rate `t = t + mdw_dt × c`, where `mdw_dt` is this frame's
  step of the preset clock (Speed already in it); expr sets `mdw_cN = c` and
  appends `code`. Per-pixel controls go after the per-pixel code; a rate is
  always per frame. Unset ranges: replace 0..1, scale 0..2, otherwise −1..1;
  unset default: the neutral value.
- **The Visual globals**, appended to every `.milkdawp` (exploration doc §6.2,
  Stage B column), each exact at neutral:

  | Global | Generated line | At full |
  |---|---|---|
  | Zoom (−1..1) | `zoom = zoom × pow(1.04, mdw_zoom)` | ±4% per frame, through the feedback |
  | Rotation (−1..1) | `rot = rot + 0.04 × mdw_rot` | |
  | Warp (0..3) | `warp = warp × mdw_warp` | |
  | Trails (0..1) | `decay = decay + (1 − decay) × 0.5 × mdw_trails` | half way to no decay |

  The constants are first guesses, to tune by hand.
- **Wave Size can't be served**: the waveform's scale (`fWaveScale`) is read
  from the file once and is not a per-frame variable in projectM, so no
  appended line can change it. It stays dimmed with that reason. Custom shapes
  and waves aren't reachable either (ADR-0012).

### 8.10: engine and shells

- **Director** reads a `.milkdawp`, compiles it, and offers the text through
  `PresetHandoff` flagged as `.milkdawp`. `Director::currentPreset()` returns
  the path with its Macro defaults and names under one lock.
- **Render thread**, every frame, for every layer: sets `mdw_m1`..`mdw_m8`
  (from `EngineControls::macros`, smoothed with the globals' 50 ms and snapped
  to the target), `mdw_zoom`, `mdw_rot`, `mdw_warp`, `mdw_trails` and
  `mdw_dt`, in every preset (a plain `.milk` never names them; the next
  preset has them before its init code runs). For a layer showing a
  `.milkdawp`, the post effects leave out Zoom and Rotation, which the preset
  then does itself. Trails stays a post effect as well, as §6.2 says. The flag
  changes when a load succeeds, so during a soft cut from a `.milk` the
  outgoing preset loses its post Zoom and Rotation; accepted.
- **Lock Macros off** moves the Macros to `currentPreset().macroDefaults`
  (unused Macros to 0), in both shells.
- **Visual panel**: Warp is dimmed only for a `.milk`; each Macro shows the
  preset's name for it, or is dimmed when the preset doesn't use it.
- **Playlist**: the scan takes `.milk` and `.milkdawp`, and leaves out a
  `.milk` with a `.milkdawp` of the same name beside it (the archived
  original). Opening that `.milk` directly starts on the `.milkdawp`.
- **Ratings, favourites, tags** key a `.milkdawp` as its `.milk`
  (`PresetMetadata::keyFor`), so converting keeps them.

## Consequences

- `tools/mdw-convert` (8.11) has a format, a writer and a compiler to target,
  and `compileForProjectM` is its before/after identity check: with neutral
  controls the compiled preset is the original plus lines that leave every
  variable exactly as the preset set it.
- Tests: `core/tests/MilkdawpPresetTests.cpp` (parse, save, problems, versions,
  numbering, exact neutral literals); headless `[milkdawp]` tests render the
  raw file, the compiled file and the plain preset the same, and show Macros
  and Zoom/Rotation/Warp reaching the preset's own variables;
  `DirectorTests` plays a `.milkdawp` beside its `.milk` end to end.
- Ruled out: numbering generated lines from 901; always prefixing `;`;
  computing a control's value as `min + span × macro` (not exact at the
  default); faking Wave Size as a post effect; listing both a `.milk` and the
  `.milkdawp` made from it.
- The global constants and the half-way Trails push are guesses until the
  Phase 8 hand test.
- Opening a `.milkdawp` in MilkDrop and re-saving it loses the controls; the
  `.milk` beside it is unaffected.
