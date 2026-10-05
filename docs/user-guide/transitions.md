# Transitions

A **transition** is a change from one preset to the next. MilkDAWp decides
*when* from the music and *how* from your settings. Open **Settings >
Transition settings** for everything below.

## When: the transition mode

Chosen in the drawer or the Transitions panel (and automatable in a DAW).

| Mode | Changes preset |
|---|---|
| **Manual** | Never by itself. Use Previous/Next, the browser, or automation. |
| **Timed** | Every *Duration* seconds. With **Jitter** on, a random time between *Min* and *Max*. In a DAW the clock pauses while the transport is stopped. |
| **Beat quantized** (default) | Every *Bars* bars (1-16), landing on the downbeat. If the beat is unclear for more than 4 seconds it falls back to Timed until the beat returns. |
| **Hybrid** | On the Timed schedule, but moved forward to the next bar line. |
| **Energy** | Cuts hard on a drop: when the bass comes back after a breakdown or build-up. Between drops it behaves like Beat quantized. *Energy threshold* sets how big the return must be. |

**Sync cuts to bar grid** makes changes fall on a fixed beat grid instead of
counting bars from when this instance started. With the DAW's tempo, every
instance using the same *Bars* and **Grid offset** changes together; giving
instances different offsets (which beat of the cycle to cut on) staggers them.

## How: blends and hard cuts

- By default a change **blends**: the old preset cross-fades into the new one
  over **Blend time** seconds, timed so the middle of the blend lands on the
  beat.
- With **Hard cuts** on, changes are instant instead. (Energy mode always cuts
  hard on a drop.)

## Which preset next: selection

**Preset selection**: *Sequential* (folder order), *Shuffle* (random, without
repeating recent ones), or *Weighted* (random, favouring your
[ratings](presets.md#ratings-favourites-and-tags)). The drawer's **Shuffle**
button switches between Sequential and Shuffle. **Only tags** limits all of
them to tagged presets; presets marked *never auto-select* are always skipped.

## Reactivity

**Reactivity** (*Beat sensitivity*) is how strongly presets react to the
music: 0 calm, 1 MilkDrop's usual, 2 twice as jumpy. It doesn't change when
transitions happen.

## The tempo

In the plugin, with **BPM from DAW** on (Settings menu), the beat comes from
your DAW's tempo and transport, which is exact. Otherwise MilkDAWp detects the
beat from the audio; the drawer shows the tempo it found and how confident it
is. Detection works best on music with a clear pulse; on rubato or ambient
material it falls back to Timed as described above.
