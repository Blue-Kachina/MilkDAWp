>>>>>>> # MilkDAWp
## Vendor: Otitis Media

MilkDAWp at its core is a VST3 plugin that uses projectM for visuals.

### Purpose & Scope
MilkDAWp is a JUCE-based VST3 effect that renders realtime projectM/libprojectm visualizations using the audio from the channel it was inserted on.
It does this without having a negative impact on the audio since its visualizations will be rendered off-thread.

It has several interactive inputs that can drastically change the visualization being rendered.
All of the inputs are host-automatable, and if your host DAW supports MIDI assigning, then you can make use of that too.

Since its inputs are host-automatable, it means that you can use your DAW to act like a video director.
Alternatively, it can be used in live environments too.  Its typical use-case would be for the DJ to be able to see the controls on their own monitor,
while having the visualization displaying on another one (in fullscreen).

### Platforms & Distributions
- VST3 Insert Effect Format
- Windows, macOS, Linux


## Acceptance Criteria (MVP)
- Builds and runs as VST3 + standalone on Win/macOS/Linux.
- Loads projectM presets and playlist folders.
- Zero added audio latency.
- All controls automatable and MIDI-assignable.
- State restores deterministically.
- Fullscreen window mode operates correctly.
