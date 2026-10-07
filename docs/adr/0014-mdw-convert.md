# ADR-0014: `mdw-convert` and the bundled `.milkdawp` pack

Status: Proposed (Phase 8.11, 2026-10-06). Builds on ADR-0013 (the format and
`compileForProjectM`) and ADR-0012 (the preset-variable patch).

## Context

Roadmap 8.11 (exploration doc §7): convert `.milk` presets to `.milkdawp` in
bulk, propose controls from what each preset does, check headless that the
result looks the same with controls neutral, report, and curate a first pack
from Cream of the Crop "by rating". The `.milk` files are kept and never
touched (§5.1).

Two findings changed the plan:

- **Every preset in Cream of the Crop is rated 5** (`fRating=5.000000` in all
  9,795), so rating can't pick a pack. Matthew (2026-10-06): convert **all of
  it**, leaving out only what fails the check, and put each `.milkdawp`
  **beside its `.milk`** in the bundled content, made at build time. Then
  (2026-10-07) ship only the converted presets, as "Cream of the CrAWp" (below).
- **projectM is not deterministic from one load to the next**, so "renders the
  same" needed real work (below).

## Decision

### Proposing Macros (`core/MilkConvert`, `convertMilk`)

Up to 8 Macros, each one control (Squash is two), all neutral on a centred
Macro (0.5): `scale` 0..2 around 1, or `offset` ±reach around 0. All are
per frame and act on variables projectM resets every frame (built-ins, and
q-variables, which reset to their post-init values), so nothing accumulates.
In this order:

1. **q-variables fed from the music**: set in per-frame code, read later
   (per-pixel code, a shader, a custom wave or shape), with `bass`/`mid`/
   `treb`/`vol` (or their `_att`) on the right-hand side. At most 2 here.
2. **What the preset's code animates**: Wave (`wave_a`), Outer and Inner
   Border (`ob_a`, `ib_a`), Motion Vectors (`mv_a`), Echo (`echo_alpha`),
   Drift X/Y (`dx`, `dy` ±0.02), Centre X/Y (`cx`, `cy` ±0.25), Squash
   (`sx` +, `sy` −, ±0.03). A family counts as animated when the code writes
   any of its variables (e.g. `wave_r` for Wave).
3. **Other q-variables**, to at most 3 in all.
4. **What the preset shows** by its header (wave alpha > 0.02, a border with
   size and alpha, motion vectors, echo alpha), using projectM's defaults
   for missing keys.
5. **Motion any preset has**: Drift, Centre, Squash.

Left out: q-variables that are a phase (`time`, `frame`, `progress` on the
right) or a selector (`int`, `rand`, `floor`, comparisons, `if`, `%`, ...);
scaling those jumps or breaks the picture. Zoom, Rotation, Warp and Trails
aren't proposed: the Visual globals already reach them (ADR-0013).

Also written: `mdw_title` (file name), `mdw_source` (path in the pack),
`mdw_source_sha256` (`core/Sha256`, our own FIPS 180-4 implementation, no
dependency), `mdw_tags` (the folders, which in Cream of the Crop are category
and subcategory, plus `shader` for presets with warp/comp shaders).

### Writing safely (`convertFolder`)

- Only `.milkdawp` files are created, replaced or (with `--prune-excluded`)
  deleted. A `.milk` is opened read-only. A test checks every file in the tree
  is byte-identical afterwards and the only new files are `.milkdawp`.
- Written to a temporary name then renamed, so an interrupted run leaves no
  half file. The temporary name (`.mdw~`) is no longer than the `.milk`'s, for
  MAX_PATH; the `.milkdawp` itself is 4 characters longer than the `.milk`.
- An existing `.milkdawp` is kept unless `--overwrite`; one that would be
  written byte for byte the same is left alone (no new timestamp).
- Every file must read back as exactly the preset written, with no
  problems, or it isn't written.
- A `.milk` that already has `mdw_` lines, is empty or binary, is skipped.

### The check (`mdw-convert --verify`)

Each preset: the compiled text projectM will really get must load; then the
original and the compiled conversion are rendered headless (256×144, 60
frames of a pulsing 55 Hz + 1760 Hz signal, frame time pinned, every Macro
on its default, globals neutral), comparing the last 30 frames. A fresh
projectM instance per render, so audio smoothing, `time` and `frame` start
the same.

What makes two renders of the same preset differ, and what we do:

| Source | Handling |
|---|---|
| `rand()` in preset code (projectm-eval's process-wide Mersenne Twister, never reseeded) | Replaced by `0.5*(...)` in both texts for the comparison only (`withoutRandomness`) |
| `rand_preset`, `rand_frame` (C `rand()`), `hue_shader` (`random_device`) | Constants in both shader texts |
| Random textures (`sampler_rand00`, `sampler_pw_rand01_smalltiled`, ...) | A fixed, distinct texture from the pack each |
| 2D noise textures (`noise_lq`..`noise_hq`, filled from the clock) | A fixed texture each |
| 3D noise (`noisevol_lq/hq`, clock-seeded) | Can't be swapped for an image: handled statistically |
| The **first render in a process** comes out differently from every later one (seen as a ~10/255 difference; cause not traced) | One warm-up render when the verifier starts |

Verdict: identical frames pass ("exact"). Otherwise the original is rendered
again, and the conversion passes when
its nearest render is within `1.5 × spread + 0.05` of some original render
(mean absolute difference per channel, 0..255; spread = the largest
difference among the original's renders); when it isn't, up to two rounds of
one more original and one more conversion render each. Injected faults
(`wave_a × 1.02` with `zoom × 1.002`) on four presets measured 0.17–4.8
against self-differences of 0.00001–0.28, all above the line. An earlier
rule, "if the original renders identically twice (then three times), any
difference fails", was dropped: in the full pack, presets it called
deterministic varied by ~2.4 when rendered more, so matching renders were
chance.

**Result on the pack** (2026-10-07, Release build, about 3 hours on this
machine): 9,719 of 9,795 pass, 2,512 of them exactly; 7.5 Macros per preset.
76 are turned down: 67 that projectM can't load at all (the original fails
to compile too, so they never played), and 9 that rendered differently. For
those 9, bisecting the appended lines one at a time found no line to blame:
every variant, a do-nothing line included, and the original against itself in
a fresh process, landed in the same 0–4.5 band. projectM carries some state
from one render to the next that we haven't traced (the first-render effect
above is part of it). Those 9 stay plain `.milk`.

`--journal` records each result as it goes (with "start" before a render),
so a run projectM crashes in can be restarted and that preset fails.

### The bundled pack: "Cream of the CrAWp"

First built (2026-10-06) as a `.milkdawp` beside every `.milk` in the
shipped "Cream of the Crop" folder. Changed the next day, Matthew: **ship
only the converted presets**, as their own pack, "Cream of the CrAWp", with
no `.milk` files, and leave out the presets projectM can't load. The 9 that
"rendered differently" ship too, converted, since that was projectM's noise.

- The fetched pack now sits in `_deps/cream-of-the-crop`
  (`MILKDAWP_SOURCE_PRESETS_DIR`), only read. The build (target
  `milkdawp_preset_pack`, in `ALL`) deletes and remakes
  `content/Presets/Cream of the CrAWp` with `mdw-convert --out`, leaving out
  `cmake/milkdawp-pack-exclusions.txt` (the 67 that projectM can't load), and
  copies the pack's `LICENSE.md` and `README.md` beside them (D12: the
  curator's notice ships unchanged). 9,728 presets.
- The exclusions list is committed and comes from a full
  `--verify --dry-run --write-exclusions` run, which writes the presets whose
  original won't load as entries and any other rejection as a comment to look
  at; regenerate it whenever the pack pin moves.
- About a minute, and only when the pack, the exclusions, the removals or the
  conversion's own sources (`MilkConvert.cpp`, `MilkdawpPreset.cpp`) change:
  not whenever `mdw-convert` relinks, which is after any change to core.
  When cross-compiling no presets are bundled (a warning). `package.sh`
  refuses a content folder without the made pack, or with a pre-8.11
  "Cream of the Crop" still in it (configure removes that from a build tree).
- Presets removed at their author's request are taken out of the fetched pack
  before conversion, so they never reach it.
- `BundledContent` names the pack's folder (`kPackFolderName`) and its default
  preset as a `.milkdawp`. `BundledContent::fromOldPack` moves a 1.0 session's
  "…/Presets/Cream of the Crop/…/x.milk" to the same place in the new pack,
  when it exists there; the plugin applies it to the restored folder and
  preset, the app to its saved ones. Ratings and tags are keyed by file name,
  so they carry over anyway.
- The installers take the 1.0 folder away on upgrade (Windows
  `[InstallDelete]`, a macOS `preinstall` script, the Linux tarball's install
  script; the `.deb` does it itself).
- File associations cover `.milkdawp` (Windows installer, macOS app,
  Linux MIME type `application/x-milkdawp-preset`).
- About, the FAQ, `THIRD_PARTY_NOTICES.md` and the preset-removal issue form
  no longer say the presets ship "unchanged": each is the author's text byte
  for byte plus our lines.

## Consequences

- The pack works with Macros from the first launch, and nothing generated is
  committed. The original `.milk` files are no longer shipped (a change to
  exploration doc §5.1, which shipped them for posterity): anyone can get
  them from the pack's repository, and taking a `.milkdawp`'s `mdw_` lines
  out gives each one back byte for byte.
- `presetIndex` automation in a 1.0 session counts through a different list
  (67 fewer presets), so it may land on other presets.
- `mdw-convert` also works on anyone's own folders (beside their `.milk`
  files, keeping existing `.milkdawp` files unless told otherwise). It isn't
  shipped to users; it's a dev tool, as `mdw-view` is.
- The heuristics are a first pass; their names and ranges (and ADR-0013's
  global constants) want a hand test in a DAW.
- The verify thresholds are empirical, and only "can't load" now keeps a preset
  out of the pack; a "renders differently" is listed for a look. One wrongly
  passed still runs the
  same code as the original plus lines that are exact at neutral by
  construction (ADR-0013), so the check is a safety net, not the guarantee.
- Ruled out: curating by rating (all equal); a separate pack folder beside
  the original (both would be listed); committing generated files; a determinism patch
  to projectM (it would touch five places and projectm-eval's static state).
- Dev aids: `--print-compiled <preset>` shows what projectM gets;
  `--compare <a> <b>` renders two texts the verify way.
