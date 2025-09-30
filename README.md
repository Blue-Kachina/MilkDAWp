>>>>>>> # MilkDAWp

MilkDAWp at its core is a VST3 plugin that uses projectM for visuals.\
It's still a work in progress, but its basics are already usable.\
Lots still in store for this.



## Technical Highlights

- VST3 plugin built with [JUCE 8](https://github.com/juce-framework/JUCE)
- C++20, CMake-based build
- Optional [projectM v4](https://github.com/projectM-visualizer/projectm) integration for visuals (auto-detected)
- Cross-platform (Windows, macOS, Linux)
- Zero vendored third-party sources: dependencies are fetched/installed at configure time

## Origin Story
In the past, I have tried to make music videos using the visualizations from Winamp\
*Geiss and Milkdrop were always my favourites*

Not only was it clunky, but I had very limited control the behaviour of the visualizations. \
*I was using keyboard shortcuts to navigate between presets and settings which required precise timing, and a bit of hope/luck that the next visualization would be one that I'd want to keep in my video*

Recently, I found out about [ReaPlugs VST FX Suite](https://www.reaper.fm/reaplugs). \
Big shoutout to those devs!\
I started using `reastream` to get audio from my DAW into OBS for live-streaming.\
It saved me from having to run extra wires just for loopback, and also got me thinking that VST can be used for other things instead of just for manipulating audio.
So, if I could only get a VST plugin to render visualizations, then I could also use automation features built into most DAWs in order to get the precision of control that I wanted.  


## Quick Start

### More details on projectM usage in MilkDAWp
For comprehensive information about projectM runtime parameters, playlists, formats, and integration details, see:

- [PROJECTM_DETAILS.md](PROJECTM_DETAILS.md)

This keeps the README concise while providing a deep-dive reference for projectM-specific behavior.
