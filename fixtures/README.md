# Fixtures

Audio fixtures used by `milkdawp_core` tests, the `mdw-analyze --suite`
metric gate (Phase 1.10), and engine headless render tests (Phase 2.7).

## Policy

- **Short.** Each clip is ≤10 seconds. Onset/tempo detection and transition
  scheduling only need a few bars to exercise; long clips just slow down CI.
- **Licence-clean.** Every clip is either synthesized (a script, committed
  alongside the fixture) or sourced under a licence permissive enough to
  redistribute in this repository (e.g. CC0, or original recordings the
  contributor owns outright). Record the source and licence in this file's
  fixture table when adding one.
- **Annotated.** Every music fixture ships with a matching `beats.txt`.
- **Covers the hard cases, not just the easy ones.** Phase 1.9's set spans
  four-on-the-floor, syncopated, a tempo change, a breakdown/drop, sparse
  acoustic material, silence, and noise -- because a beat tracker that only
  sees clean electronic material will look better in CI than it behaves for
  Matthew at a gig.

## Layout

```
fixtures/
├── README.md            # this file
├── thresholds.json       # pass/fail thresholds for mdw-analyze --suite (Phase 1.10)
└── <clip-name>/
    ├── audio.wav          # mono or stereo, any sample rate (mdw-analyze resamples)
    ├── beats.txt           # one beat time in seconds per line, ascending
    ├── drops.txt           # optional: the drops Energy mode must cut on (5.1)
    └── SOURCE.md            # where this clip came from and its licence
```

## `beats.txt` format

Plain text, one beat time in seconds per line, ascending, `\n`-terminated:

```
0.482
0.960
1.438
1.917
```

Downbeats are not marked separately in 1.0 -- see §4.3's downbeat heuristic
in `development_roadmap.md`; a `downbeats.txt` in the same format may be
added later without changing this schema.

## `drops.txt` format

The same format as `beats.txt`: the times where the bass comes back after a
breakdown or build-up, where Energy mode should hard-cut (5.1,
`core::SectionDetector`). `mdw-analyze --suite` requires each one to be found
within `dropToleranceSeconds` and nothing else to be called a drop. A fixture
without the file has no drops, so the steady fixtures also check that steady
music never cuts.

## Regenerating

`tools/generate-fixtures <fixturesDir>` writes the synthesized fixtures. The
random parts (kick transients, noise) come from `std::uniform_real_distribution`,
whose output differs between standard libraries, so a run with another compiler
gives slightly different `audio.wav` bytes (the beats are the same). Write to a
scratch folder and copy in only the fixtures you mean to change.

## Adding a fixture

1. Add `fixtures/<name>/audio.wav` (synthesize it, or bring a licence-clean
   recording) and `fixtures/<name>/SOURCE.md` describing where it came from.
2. Annotate beats by ear or with an existing reference tracker into
   `fixtures/<name>/beats.txt`.
3. Add an entry to `fixtures/thresholds.json` once that file exists
   (Phase 1.10) so the fixture is actually checked in CI, not just present.
