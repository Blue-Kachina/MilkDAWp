# The beta programme (6.8)

**Skipped for 1.0** (decided 2026-10-05): MilkDAWp isn't aimed at a large
audience, and 1.0 is tagged directly. This page stays as the plan for a beta,
should a later release want one; the issue forms and labels it describes are
in use either way.

A beta would be two weeks of `-beta` builds before a release: real people,
real DAWs, real hardware, on everything the release workflow can't check by
itself.

## Before the first beta

1. Labels: `bash scripts/release/create-labels.sh` (once; needs `gh`).
2. Run `docs/daw-checklist.md` in your own DAWs.
3. Tag `v1.0.0-beta.1` ([releasing.md](releasing.md)). The workflow publishes it
   as a pre-release with installers for all three platforms.

## What testers should know

Put this, or a link to it, in the announcement:

- **Report problems** with the [bug form](https://github.com/Blue-Kachina/MilkDAWp/issues/new?template=bug_report.yml).
  The diagnostics panel's **Copy diagnostics** (press `D`) is the most useful
  thing to paste.
- **Turn on update checks** (Help > About > Check for updates once a day) to
  hear about the next beta; testers on a beta are told about newer betas.

What to try, roughly in order of value:

- the plugin in your DAW (which DAW, which version, which OS): loading,
  saving and reopening a project, automation of *Next Preset*, two instances
  layered into one Output window;
- an hour of music in the app with **Energy** mode, noting every change that
  felt wrong and when;
- the preset browser with the full pack: search, hearts, stars;
- the Output window on a second screen and in OBS;
- macOS: the `.pkg`'s first-run prompt, the AU in Logic or GarageBand, Intel
  Macs; Linux: the AppImage on your distribution.

## Triage

Every new issue gets `triage` from its form. Within a few days:

- reproduce or ask (`needs-info`); add `platform:*` / `host-specific` / `beta`;
- `blocker` if 1.0 can't ship with it: crashes, data loss, a common host that
  can't load the plugin, a broken installer;
- everything else is scheduled or closed with a reason; remove `triage`.

`preset-removal` requests are honoured, not triaged: add the preset's path to
`MILKDAWP_PRESET_REMOVALS` in `cmake/BundledContent.cmake` (deleted after the
pack is extracted, so the next release no longer has it) and tell the projectM
project, which maintains the pack.

## Ending the beta

1.0 ships when two weeks have passed since the last `blocker` was fixed and no
`blocker` is open. Then follow 6.9 ([promotion.md](promotion.md)).
