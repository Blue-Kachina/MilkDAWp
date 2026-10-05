# The picture and the controls

MilkDAWp is "video first": the picture fills the window, and the controls sit
in a **drawer** along the bottom. Move the mouse over the bottom of the window
(or tap it) to reveal the drawer; it hides again when you move away. The pin
button keeps it open; the plugin starts pinned.

From left to right the drawer has:

- **Previous / Next** preset;
- the **preset name**: click it (or press `B`) to open the [preset browser](presets.md).
  The line under it shows the folder and the preset's position;
- **Lock** (stay on this preset) and **Shuffle**;
- the **transition mode** (Manual, Timed, Beat quantized, Hybrid, Energy; see
  [Transitions](transitions.md)) and, beside it, the tempo and how sure the beat
  detector is;
- **Output** (open or close the Output window), **Settings** (a menu with
  everything else), and **Pin**.

## Keyboard shortcuts

The same keys work in the plugin, the app, the Output window and the floating
controls. In the plugin, keys MilkDAWp doesn't use go on to your DAW, so its
own shortcuts keep working.

| Key | Does |
|---|---|
| `←` / `→` | Previous / next preset |
| `L` | Lock the current preset |
| `Space` | Lock (app only; in a DAW, Space stays the transport) |
| `S` | Shuffle on or off |
| `B` | Open the preset browser |
| `M` | Open the settings menu |
| `D` | Show or hide the diagnostics panel |
| `H` | Hide or show the drawer |
| `P` | Pin the drawer open |
| `F11` | Fullscreen (the app's window, or the Output window from the plugin) |
| `Esc` | Close the open panel; leave fullscreen; show the drawer |

Panels (Transitions, Output, Presets, About) take the keyboard while open:
`Tab` moves between their controls, and `Esc` closes them.

## The Output window

**Output** in the drawer opens a second window that shows only the picture,
for a second screen, a projector, or [OBS](capture-and-obs.md). `F11` makes it
fullscreen; **Settings > Output settings** chooses which screen it goes
fullscreen on and whether it opens fullscreen. In the plugin it keeps running
when you close the plugin window, and your project remembers whether it was
open and where.

## Floating controls

**Settings > Float controls in a window** moves the drawer into a small window
of its own, so it never covers the picture. **Dock controls** (or closing that
window) puts it back.

## Panels

- **Transitions** (Settings > Transition settings): how and when presets change.
- **Output** (Settings > Output settings): the Output window, and layers in the plugin.
- **Diagnostics** (`D`): what the renderer is doing, frame times, the beat, and
  recent errors. **Copy diagnostics** puts it all on the clipboard for a bug report.
- **About** (Settings > About, or Help > About in the app): versions, credits,
  licences, and the update check.

## Quality

**Settings > Quality**: *Auto* (the default) lowers the render resolution when
your graphics card can't keep up and raises it again when it can. *Low*,
*Medium* and *High* fix it.
