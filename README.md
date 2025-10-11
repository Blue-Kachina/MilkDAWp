# MilkDAWp
## Vendor: Otitis Media

### Purpose & Scope
MilkDAWp is a JUCE-based VST3 effect that renders realtime projectM/libprojectm visualizations using the audio from the channel it was inserted on.
It does this without having a negative impact on the audio since its visualizations will be rendered off-thread.
It has several interactive inputs that can drastically change the visualization being rendered.
All of the inputs are host-automatable, and if your host DAW supports MIDI assigning, then you can make use of that too.
Since its inputs are host-automatable, it means that you can use your DAW to act like a video director.
It can be used in live environments too.  Its typical use-case would be for the DJ to be able to see the controls on their own monitor,
while having the visualization displaying on another one (possibly in fullscreen).  It could be audience-facing.

### Platforms & Distributions
- VST3 Insert Effect Format
- Windows, macOS, Linux
- Installation is simple: A single, self-contained installer per OS; no separate dependencies

### Technical Details
- Statically linking is preferred to avoid requiring the VC++ Redistributable. Prefer static linking for third‑party libraries only where licenses allow and it meaningfully reduces external DLLs; otherwise, ship required DLLs side-by-side with the binaries
- Audio path: zero-latency; visualization processed off-thread
- Threading:
  - Audio thread: capture FFT/beat cues.
  - Visualization thread: drives projectM and paints GPU surface.
  - Message thread: UI and parameter updates.
- Multi-Instance Resource Sharing: Share cached assets and preset data between plugin instances to reduce load times
- Robust logging capabilities
- Test-driven development
- Robust window management
  - Main plugin window can spawn external window
  - External window gets destroyed when main window does
  - External window can receive realtime messages from main window and vice-versa
- The main plugin window will have several interactive controls on its main window
  - Beat Sensitivity: JUCE knob (range of 0 - 2.0)
  - JUCE button for pop-out-window (when docked -- otherwise JUCE button for docking it again)
  - JUCE button for fullscreen
  - A file/folder picker: Allows selection of a single projectM preset file OR selection of an entire folder of projectM presets (to be treated as a preset playlist)
  - A playlist transport panel that only becomes visible when using a playlist
    - Lock current preset: JUCE toggle button (default true) 
    - Shuffle: JUCE toggle button (default false)
    - Transition Duration: JUCE knob (with a range of 0.1 to 30.0)
    - Prev/Next: JUCE buttons to navigate to the next/previous preset in the playlist
    - Transition style
- All inputs need to be wired directly to their corresponding projectM/libprojectM parameter counterpart, and hence, will need to be updated in any external window that might exist
- All inputs maintain their own state
- Current preset will be maintained in state
- Current playlist will be maintained in state
- Current preset index number will be maintained in state (and host automatable -- up to a max of 128 values)
- All controls will be both midi-assignable and host-automatable (though we can rely on the DAW to provide UI for midi assignment).
- All controls should appear like professional hardware knobs/buttons/etc...
- All knob inputs will have suitable values/ranges, and their values will be able to be easily read by humans.
- The plugin will open up initially with the controls all respecting those that are saved into state (if any state already exists). A visualization will appear as soon as we can make that happen.
- The visualization will have its own context that can appear embedded/detached/detached+fullscreen.
- When going fullscreen, it will become fullscreen on whatever monitor the visualization was being displayed on (if it was embedded, then use the plugin's main window's monitor, if it was detached, then go fullscreen on whichever monitor the popped-out window resided on)
- When going fullscreen, it will automatically become detached.  When docking, it will automatically deactivate fullscreen
- Fullscreen can be toggled with F11 when plugin or plugin visualization window are active
- Visualization window has a transparent button on it in the bottom-right that only appears when hovering over it.  It can toggle fullscreen also
- Visualization window can be easily identified by streaming software like OBS
- Visualization window can be transparent for use in software like OBS
- Plugin window will be 1200x650 by default which gives sufficient space for the docked visualization
- Plugin will display a branded logo
- Plugin

### Dependencies
I'm not sure of what the best way to include these in our project is, however, I have identified that we will need the following dependencies.
- https://github.com/projectM-visualizer/projectm,
- https://github.com/juce-framework/JUCE
I want to use the latest versions of these that we can successfully implement given our requirements
We might need to take their licensing into consideration also

### Looking For Recommendations
- Is there a more elegant way to handle the DJ window situation than pop-out, move visualization window to proper monitor, toggle fullscreen?

### Future requirements
- Snapshot Morphing ("Scenes"): Save/recall all control settings as snapshots. Support morphing between scenes over configurable time spans. Enables planned live performance transitions.
- Setlists & Cues: Curated preset order with per-cue overrides (duration, transition style). Trigger via host automation, MIDI, or DAW transport alignment.
- OSC / Web Remote (Phase 2): Lightweight OSC/WebSocket remote for mobile control (Next, Lock, Shuffle, Scenes, Crossfader).
- Offline hi-res render mode.
- Tagging, ratings, smart shuffle.

## Acceptance Criteria (MVP)
- Builds and runs as VST3 + standalone on Win/macOS/Linux.
- Loads projectM presets and playlist folders.
- Zero added audio latency.
- All controls automatable and MIDI-assignable.
- State restores deterministically.
- Detached + fullscreen window modes operate correctly.
- Adaptive Quality works under load.
- Diagnostics and First-Run Benchmark operational.
- Bundled presets and assets included.