# Capturing the picture and OBS

The cleanest picture to capture is the **Output window**: it shows only the
visual, with no drawer or panels, at whatever size you give it. Open it with
**Output** in the drawer.

## OBS Studio

1. Open the Output window (and size it to your canvas, e.g. 1920x1080, or put
   it fullscreen on a spare screen).
2. In OBS add a source:
   - **Windows:** *Window Capture*, window `[MilkDAWp.exe]` or your DAW's
     process for the plugin, title *MilkDAWp Output*. Set *Capture Method* to
     **Windows 10 (1903 and up)**; the older BitBlt method shows black for
     OpenGL windows.
   - **macOS:** *macOS Screen Capture*, method *Window Capture*, the MilkDAWp
     Output window. OBS needs *Screen Recording* permission (System Settings >
     Privacy & Security).
   - **Linux:** *Window Capture (Xcomposite)* on X11, or *Screen Capture
     (PipeWire)* and pick the window on Wayland.
3. Untick *Capture Cursor*, and crop if your window has borders.

The visual reacts to what MilkDAWp hears, not what OBS hears. For a stream,
feed both the same audio: in the app, use system audio (Windows) or the same
interface OBS uses; in a DAW, put the plugin on the master bus.

Tips:

- A minimised or fully covered window may stop updating. Keep the Output
  window visible, or fullscreen on a second screen.
- Lower **Quality** (Settings) if OBS's encoder and MilkDAWp compete for the
  graphics card; *Auto* adapts by itself.
- Hide the cursor over a fullscreen Output window by leaving the mouse still.

## Recording without OBS

- **Windows:** the Xbox Game Bar (`Win+G`) records the focused window, the
  Output window included.
- **macOS:** `Cmd+Shift+5`, *Record Selected Window*.
- **Linux:** OBS, or your desktop's screen recorder (GNOME: `Ctrl+Shift+Alt+R`
  records the whole screen).

## Projectors and second screens

**Settings > Output settings** picks the screen the Output window goes
fullscreen on and whether it opens fullscreen. With the plugin, the project
remembers it, so the visual comes back on the projector when you reopen the
project. `F11` toggles fullscreen and `Esc` leaves it.
