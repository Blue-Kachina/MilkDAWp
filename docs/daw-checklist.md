# DAW compatibility checklist

Manual verification for the plugin (§7 Phase 3 exit criteria: "manual checks in Reaper, Ableton
Live, FL Studio, Cubase, Logic (AU) pass the checklist below"). Nothing here can be verified by
an agent without a real DAW and a real GPU/display, so this file is filled in by hand, one row
per host, as each phase's items land. `[ ]` not tested · `[x]` passes · `[!]` fails (note the
failure and, where relevant, the workaround) · `[-]` not applicable to this host/platform.

Run through the whole list in at least one host before checking a Phase 3 item's own `[x]` box in
`development_roadmap.md` if that item says "verify in a DAW" or similar (see 3.6, 3.14's notes,
which currently point here).

## Hosts

| Host | Platform | Format | Status |
|---|---|---|---|
| Reaper | Win/macOS/Linux | VST3 | partially tested (Win, shortcuts only, 2026-09-26 -- see below) |
| Ableton Live | Win/macOS | VST3 | not yet tested |
| FL Studio | Win/macOS | VST3 | not yet tested |
| Cubase | Win/macOS | VST3 | not yet tested |
| Logic Pro | macOS | AU | not yet tested (AU itself isn't built yet, Phase 3.8) |

## Per-host checklist

Copy this table once per host/platform/format combination above.

| # | Check | Result | Notes |
|---|---|---|---|
| 1 | Plugin scans and loads without the projectM shared library present (§2.6: should report "visualization unavailable", never crash the scan) | `[ ]` | |
| 2 | Plugin scans and loads with projectM present | `[ ]` | |
| 3 | Insert on an audio track; audio passes through bit-exact (no added latency, no gain/phase change) | `[ ]` | |
| 4 | Automate every parameter in `docs/parameters.md` from the host's automation lane; value reflected in the plugin | `[ ]` | one row per parameter if any fail, otherwise "all pass" |
| 5 | Save project, close, reopen: every parameter value survives | `[ ]` | |
| 6 | Save project, close DAW entirely, reopen: every parameter value survives | `[ ]` | |
| 7 | Editor size persists across close/reopen of the editor within a session | `[ ]` | |
| 8 | Editor size persists across project save/reload (Cubase specifically: host may create the editor *before* `setStateInformation` -- confirm size still ends up correct, §2.9) | `[ ]` | |
| 9 | Keyboard: `F11` toggles fullscreen | `[ ]` | see 3.14: plugin editor should open the Output window fullscreen, not fullscreen itself |
| 10 | Keyboard: `Esc` exits fullscreen, or reveals the drawer if not fullscreen | `[ ]` | |
| 11 | Keyboard: `Left`/`Right` change preset | `[ ]` | |
| 12 | Keyboard: `L` toggles lock | `[ ]` | |
| 13 | Keyboard: `S` toggles shuffle | `[ ]` | |
| 14 | Keyboard: `H` toggles the drawer | `[ ]` | |
| 15 | Keyboard: `P` pins/unpins the drawer | `[ ]` | |
| 16 | Keyboard: unhandled keys fall through to the host (host's own shortcuts still work with the editor focused) | `[ ]` | |
| 17 | If any key above is swallowed by the host before the editor sees it | `[ ]` | note which key; try `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` for this host only and record the trade-off (3.14) |
| 18 | Drawer: hover/tap reveals it; auto-hides after ~3s of no pointer activity when unpinned | `[ ]` | |
| 19 | Drawer: pinned by default in the plugin editor; never auto-hides while pinned | `[ ]` | |
| 20 | Output window: opens on the remembered display, borderless fullscreen | `[ ]` | |
| 21 | Output window: editor keeps showing the live mirror while the Output window is open | `[ ]` | |
| 22 | Close the editor with the Output window open: Output window keeps running | `[ ]` | |
| 23 | Reopen the editor: still shows the live mirror, engine state (preset, playlist position) unchanged | `[ ]` | |
| 24 | Pop the editor out / dock it back (host-specific, e.g. Reaper's floating window): visual does not reset (§2.4, the core v1 bug this rebuild fixes) | `[ ]` | |
| 25 | Remove the plugin from the track: no crash, no leaked window | `[ ]` | |
| 26 | Multiple instances in one project: each renders independently, no shared-state bleed | `[ ]` | |

### Reaper -- Win -- VST3 (2026-09-26)

Only the shortcut rows (Phase 3.14) have been run so far, as part of fixing the VST3
rejection bug (§4.11) that had blocked any real-host testing until today. Everything else
below is still `[ ]`, carried over unchanged from the template above.

| # | Check | Result | Notes |
|---|---|---|---|
| 1 | Plugin scans and loads without the projectM shared library present | `[ ]` | |
| 2 | Plugin scans and loads with projectM present | `[ ]` | loads, but `ProjectMLibrary` reports unavailable in-host (`could not locate or load projectM-4.dll`) even though the file is present next to the plugin -- not root-caused yet, deferred since real projectM rendering isn't wired into `OutputSurface` yet either (2.4/2.6 gap) |
| 3 | Insert on an audio track; audio passes through bit-exact | `[ ]` | |
| 4 | Automate every parameter from the host's automation lane | `[ ]` | |
| 5 | Save project, close, reopen: every parameter value survives | `[ ]` | |
| 6 | Save project, close DAW entirely, reopen: every parameter value survives | `[ ]` | |
| 7 | Editor size persists across close/reopen of the editor within a session | `[ ]` | |
| 8 | Editor size persists across project save/reload | `[ ]` | |
| 9 | Keyboard: `F11` toggles fullscreen | `[-]` | correctly falls through to Reaper's own fullscreen (never consumed) since the Output window (3.12) it should open doesn't exist yet -- expected per 3.14's design, not a bug |
| 10 | Keyboard: `Esc` exits fullscreen, or reveals the drawer if not fullscreen | `[x]` | reveals drawer |
| 11 | Keyboard: `Left`/`Right` change preset | `[-]` | correctly inert -- pulses `triggerPrev`/`triggerNext`, which nothing consumes until `PresetLibrary` (2.6) exists |
| 12 | Keyboard: `L` toggles lock | `[x]` | |
| 13 | Keyboard: `S` toggles shuffle | `[x]` | |
| 14 | Keyboard: `H` toggles the drawer | `[x]` | initially looked broken; root cause was a missing `grabKeyboardFocus()` call (Reaper's own preset-selector combo held focus) -- fixed in `PluginEditor`, see §4.9/3.14. Confirmed working once tested with the drawer actually unpinned (no-op while pinned is correct, documented behavior) |
| 15 | Keyboard: `P` pins/unpins the drawer | `[x]` | |
| 16 | Keyboard: unhandled keys fall through to the host | `[x]` | confirmed via F11 (row 9) and the untouched arrows (row 11) |
| 17 | If any key above is swallowed by the host before the editor sees it | `[-]` | none observed once the focus fix (row 14) landed |
| 18 | Drawer: hover/tap reveals it; auto-hides after ~3s of no pointer activity when unpinned | `[ ]` | |
| 19 | Drawer: pinned by default in the plugin editor; never auto-hides while pinned | `[ ]` | |
| 20 | Output window: opens on the remembered display, borderless fullscreen | `[ ]` | |
| 21 | Output window: editor keeps showing the live mirror while the Output window is open | `[ ]` | |
| 22 | Close the editor with the Output window open: Output window keeps running | `[ ]` | |
| 23 | Reopen the editor: still shows the live mirror, engine state unchanged | `[ ]` | |
| 24 | Pop the editor out / dock it back: visual does not reset | `[ ]` | |
| 25 | Remove the plugin from the track: no crash, no leaked window | `[ ]` | |
| 26 | Multiple instances in one project: each renders independently, no shared-state bleed | `[ ]` | |

## Loading a v1 session

v1 state is not migrated (dropped 2026-09-26). The one check left is that an old session
doesn't break anything.

| # | Check | Result | Notes |
|---|---|---|---|
| 1 | Open a v1 MilkDAWp session: the plugin loads with v2 defaults, no crash | `[ ]` | |
