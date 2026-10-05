# The plugin in your DAW

## Where to put it

MilkDAWp is an audio effect that passes audio through untouched: put it on
the track (or bus) you want it to react to. On the master bus it sees the whole
mix; on a drum bus it follows the drums. It adds no latency and changes no
audio.

The plugin window shows the picture with the drawer at the bottom. You can
make it any size; your project remembers the size, and also whether the
Output window was open, where, and whether the controls were floating.

## Tempo

With **BPM from DAW** on (Settings menu), changes follow the DAW's tempo and
bar lines exactly, and the Timed clock pauses while the transport is stopped.
Off, MilkDAWp listens for the beat itself, which also works when the transport
isn't running (playing along live, for example).

## Automation

Every setting is a plugin parameter your DAW can automate or map to a
controller: transition mode, bars, durations, reactivity, lock, shuffle,
quality, layer settings, and more. Two are buttons for automation: **Next
Preset** and **Previous Preset** trigger a change when they go from off to on,
so a step in an automation lane (or a MIDI-mapped pad) changes preset exactly
where you want. **Preset Index** jumps to a preset by its position in the
folder. The full list is in [parameters.md](../parameters.md).

A typical setup for a performance: transition mode **Manual**, and an
automation lane on *Next Preset* with a step wherever the song changes
section.

## Several instances and layers

Each instance is independent: its own folder, preset, mode and Output window.

Instances can also be **layered**: in **Settings > Output settings**, choose
another instance as this one's target, and its picture becomes a layer of the
target's canvas, with its own opacity, blend mode (Normal, Add, Screen,
Multiply, Luma key), order (which layers sit above which) and mute. Put one instance on the drums and one on
the pads, layer them, and the Output window shows both. The instance that
sends its picture says so in its own window. Instances can be renamed in the
same panel so the list stays readable.

Layer opacity, blend, order and mute are parameters too, so they can be
automated.

## Saving

Everything about the instance is saved in your project: settings, preset
folder, current preset, window layout, layer setup. Ratings, favourites and
tags are not project data: they are saved once for your user and shared by
every project and the app (see [Presets](presets.md)).

## Hosts

MilkDAWp is checked with pluginval and in REAPER on Windows; the release
builds are validated automatically on Windows, macOS (including `auval` for
the Audio Unit) and Linux. If your host has trouble with it, see
[Troubleshooting](troubleshooting.md) and please open an issue naming the host
and its version.

Some hosts keep keyboard shortcuts for themselves while a plugin window is
open. If typing in the preset browser's search box doesn't work, look for the
host's option to send keys to the plugin (in REAPER, *Send all keyboard input
to plug-in* in the FX window's menu).
