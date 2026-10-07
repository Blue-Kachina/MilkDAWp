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
- **Visual** (Settings > Visual): how the picture looks, the Macros, and the gate.
  See [Visual controls](#visual-controls) below.
- **Output** (Settings > Output settings): the Output window, and layers in the plugin.
- **Diagnostics** (`D`): what the renderer is doing, frame times, the beat, and
  recent errors. **Copy diagnostics** puts it all on the clipboard for a bug report.
- **About** (Settings > About, or Help > About in the app): versions, credits,
  licences, and the update check.

## Visual controls

Every control here is a parameter, so a DAW can automate it. Double-click a
control to put it back to its starting value, or press **Reset visual** to
reset them all. At their starting values the picture is exactly what the preset
draws, at no extra cost.

- **Hue**, **Saturation**, **Brightness**: the colours.
- **Speed**: how fast the preset's own motion runs. Turning it never jumps the
  picture. It can't slow the preset's reaction to the music.
- **Zoom** and **Rotation**: zoom in or out, and spin (Rotation is a speed; back
  at 0 the picture settles upright).
- **Trails**, **Pixelate**, **Glow**, **Blur**, **Mirror**, **Kaleidoscope**,
  **RGB Split**: effects over the picture.
- **Media Mix**: how much of the media source shows over the visual. Choose an
  image or a camera in **Settings > Media source**; it fills the picture
  (cropped, not stretched), an image's transparent parts stay clear, and the
  effects above apply to it too. Cameras work on Windows, macOS and Linux (macOS
  asks for permission the first time; in a DAW it asks in the DAW's name). Videos
  loop; in a DAW they follow the playhead, so the same bar always shows the same
  frame and a rendered mix is repeatable. MilkDAWp plays video with the
  system's own decoders: on Windows and macOS, H.264 MP4 always works; on Linux,
  video needs GStreamer installed, and H.264 its `gst-libav` plugins.
  **Settings > Media source > Blend** picks how the media meets the picture:
  Normal, Add, Screen, Multiply, Luma key, or **Displace**, where the media isn't
  shown at all but bends the picture: its red pushes sideways, its green up and
  down, mid-grey leaves it alone, and Media Mix sets how far. **Burn in**
  paints the media into the preset itself, so the preset's own motion swirls and
  fades it like anything it drew; Media Mix sets how strongly.
- **Warp** and the **Macros** work with `.milkdawp` presets (see
  [Presets](presets.md)): each one names its Macros in this panel ("Swirl"), and
  any it doesn't use are dimmed. With a `.milk` they are dimmed, but you can still
  set and automate them. With a `.milkdawp` playing, **Zoom** and **Rotation**
  turn the preset's own zoom and rotation, so a tunnel really pulls in.
- **Wave Size** is dimmed: it isn't possible with projectM yet.
- **Lock Macros**: off, the Macros go back to the new preset's starting values
  whenever the preset changes; on, they keep their values.
- **Gate**: like a noise gate on a guitar. While this instance's input is below
  **Threshold**, the picture fades out over **Release**, and it is back on the
  next note. The meter shows the input level, the threshold as a line, and a light
  that is lit while the picture shows. The visual keeps running while hidden.

In the plugin, an instance whose Output window shows other instances as layers
applies its Visual controls to the whole mixed picture; an instance sending its
picture to another applies them to its own layer.

## Quality

**Settings > Quality**: *Auto* (the default) lowers the render resolution when
your graphics card can't keep up and raises it again when it can. *Low*,
*Medium* and *High* fix it.
