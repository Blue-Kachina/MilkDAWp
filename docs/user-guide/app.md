# The standalone app

The app shows the picture in a window of its own and listens to an audio input
instead of a DAW track. Its menus are File, Playback, View and Help; the
drawer's Settings button holds the same items.

## Audio input

**File > Audio input...** chooses the device and the input pair (a
microphone, a line input, an audio interface). A meter shows the level. If
nothing arrives, the picture shows "No audio input" or "No signal from ...";
click it to open the same dialog. The app remembers the device and falls back
to the default input if it's gone.

### Listening to what your computer plays (Windows)

On Windows, tick **Capture system audio (loopback)** in the same dialog:
MilkDAWp then hears whatever your speakers play (a music player, a browser, a
game) with no cables or extra software.

On macOS and Linux this isn't built in yet. Use a loopback device instead:

- **macOS:** install [BlackHole](https://existential.audio/blackhole/), make a
  Multi-Output Device in *Audio MIDI Setup* with your speakers and BlackHole,
  play to it, and choose BlackHole as MilkDAWp's input.
- **Linux:** with PipeWire or PulseAudio, choose the *Monitor of ...* source of
  your output device as the input (pavucontrol's *Recording* tab can switch it
  while MilkDAWp runs).

## MIDI learn

Any control in the app can follow a MIDI controller. **Right-click a control**
(a button, the mode, a slider in the Transitions panel), choose **MIDI
Learn...**, then move a knob or press a pad: that control now follows it.
**Clear MIDI mapping** in the same menu removes it, and a control's tooltip
shows its mapping. The app listens to every connected MIDI input; mappings are
saved.

Useful mappings: a pad on *Next preset*, a knob on *Reactivity*, a button on
*Lock*.

## Fullscreen and screens

`F11` (View > Fullscreen) fills the screen with the picture. For a second
screen or a projector, use the [Output window](controls.md#the-output-window)
and its settings.

## Settings and logs

The app remembers everything: device, folder, preset, settings, windows,
mappings. They are saved in:

| System | Folder |
|---|---|
| Windows | `%APPDATA%\MilkDAWp` |
| macOS | `~/Library/Application Support/MilkDAWp` |
| Linux | `~/.config/MilkDAWp` |

Ratings, favourites and tags (shared with the plugin) are in the same folder.

**File > Write a log file** turns on a log; **File > Collect logs...** makes a
zip of the log, settings, diagnostics and any crash reports to attach to a bug
report. After a crash, the next launch offers to collect them.

## Updates

**Help > About** has **Check for updates once a day**, off until you turn it
on. When on, the app asks GitHub at most once a day whether a newer MilkDAWp
exists (nothing else is sent) and says so in About and the Help menu.
**Check now** checks once. Beta testers hear about the next beta; everyone
else only about final releases. The plugin never goes online; its About shows
what the app last found.
