# Troubleshooting

First, open the **diagnostics panel** (`D`). It shows whether projectM loaded,
the graphics driver, frame times, what MilkDAWp hears, and the last errors.
**Copy diagnostics** puts all of it on the clipboard; paste it into any bug
report.

## The picture is black

- **No sound reaches MilkDAWp.** Many presets draw nothing in silence. In the
  app, check the input meter (File > Audio input...); in a DAW, check the
  track plays and MilkDAWp sits on it, before any mute.
- **projectM didn't load.** The diagnostics panel says "projectM: unavailable"
  and why. The projectM library ships next to MilkDAWp (inside the plugin
  bundle, beside the app); reinstall if it is missing. Antivirus software
  sometimes quarantines it.
- **The graphics driver is too old.** MilkDAWp needs OpenGL 3.3. Update your
  graphics driver. Remote desktop sessions and some virtual machines only offer
  older OpenGL.
- **A preset is broken.** Press `→`. MilkDAWp skips presets projectM can't
  load and lists them in diagnostics.

## The DAW doesn't show the plugin

- Rescan plugins in the DAW (some only scan on request).
- Check the plugin is where the DAW looks: see [Installing](installing.md) for
  the folders.
- **Windows:** the Visual C++ runtime is required. The installer adds it; if
  you copied the plugin by hand, install
  [vc_redist.x64.exe](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- **macOS:** if the DAW blocks the plugin as "damaged" or from an unidentified
  developer, the files were probably copied rather than installed with the
  `.pkg`. Run once in Terminal:
  `xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/MilkDAWp.vst3 /Library/Audio/Plug-Ins/Components/MilkDAWp.component`
- **Audio Units (Logic, GarageBand):** if MilkDAWp is missing after install,
  restart the computer, or run `killall -9 AudioComponentRegistrar`, then
  reopen Logic.

## It's choppy

- Leave **Quality** on *Auto*, or set *Low*.
- Close other GPU-heavy programs. Overlays and background recorders (NVIDIA
  Instant Replay, Xbox Game Bar capture) can take a surprising share of the
  graphics card and steal focus from fullscreen windows.
- Some presets are much heavier than others. Rate the ones that stutter low,
  or mark them *never auto-select*.
- Several MilkDAWp instances share one graphics card; diagnostics shows how
  many.

## Presets change too often, or never

See [Transitions](transitions.md). In short: *Manual* never changes by
itself; *Lock* (`L`) stops changes in any mode; in *Beat quantized* mode, the
drawer shows whether a beat was found. If the beat isn't found, MilkDAWp
uses the Timed interval until it is.

## Keys don't work in the plugin

Click the plugin's picture first so it has the keyboard. Some hosts keep keys
for themselves; see the host notes in [The plugin in your DAW](plugin.md#hosts).

## The app hears nothing

- On macOS, allow microphone access (System Settings > Privacy & Security >
  Microphone > MilkDAWp).
- Choose the right input pair in File > Audio input...; many interfaces put
  the line inputs on channels 3/4 or higher.
- On Linux, choose the device in pavucontrol's *Recording* tab while MilkDAWp
  runs, or use a *Monitor* source to hear what plays.

## It crashed

The app saves a crash report and offers to collect it on the next launch
(File > Collect logs... does it any time). Please attach the zip to an issue.
Crashes inside a DAW are usually in the DAW's own crash log; attach that and
the diagnostics text.

## Starting over

Delete the settings folder listed in [The standalone app](app.md#settings-and-logs)
to reset the app. Deleting `preset-metadata.txt` in it clears every rating,
favourite and tag.
