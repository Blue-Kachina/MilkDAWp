# Test presets

Three minimal `.milk` presets written for this repository (AGPL-3.0-or-later,
same as the code). They exist for the headless render test (Phase 2.7) and as
a tiny, licence-clean preset folder for `mdw-view` and manual testing:

| File | What it draws | Visible with silent audio? |
|---|---|---|
| `mdw-border.milk` | thick outer border, colour cycling with time; MilkDrop 2 format with a plain composite shader so its colour depends on time alone (no random hue shading) | yes |
| `mdw-wave.milk` | circular waveform driven by the audio | no (needs signal) |
| `mdw-feedback.milk` | zooming/rotating feedback trail, waveform and thin border | yes (border) |

They are deliberately simple: they test that projectM loads presets from
memory, renders into our FBO and animates, not how presets look.
