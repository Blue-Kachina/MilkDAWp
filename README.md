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
I grew up loving the visualizations from Winamp\
*Geiss and Milkdrop were always my favourites*\
I would sometimes try to record video of the output, and try to recombine it after with the original audio again to make videos.\

It worked, but was it clunky -- I had very limited control over the behaviour of the visualizations. \
*I was using keyboard shortcuts to navigate between presets and settings which required precise timing to perform in realtime, and a bit of hope/luck that the next visualization would be one that I'd want to keep in my video*

Recently, I found out about [ReaPlugs VST FX Suite](https://www.reaper.fm/reaplugs). \
Big shoutout to those devs!\
I started using `reastream` to get audio from my DAW into OBS for live-streaming.\
It saved me from having to run extra wires just for loopback, and also got me thinking that VST can be used for other things instead of just for manipulating audio.
So, if I could only get a VST plugin to render visualizations, then I could also use automation features built into most DAWs in order to get the level of precision control that I was seeking.  
*One of my goals right now is to get this to a point where preset changes can be automated*

## Usage Notes
If you're like me, and are also looking for a way to make music videos, then it seems like using this with reastream into OBS is a good option.  Unless of course you have an audio interface that is equipped with loopback already.

## Quick Start

### Cross‑platform support (Windows, macOS, Linux)
MilkDAWp is built with JUCE 8 and targets VST3, so it is portable across desktop platforms. The repository is actively developed and validated on Windows, but the codebase and CMake configuration are platform‑agnostic, with Windows‑specific staging/deploy logic wrapped in WIN32 guards.

What works today
- Windows: Fully supported. See the guidelines for helper runs, deploy, and packaging.
- macOS: Builds as a VST3 plugin with CMake + Xcode toolchain. Post‑build helper invocation/staging is currently Windows‑only, but not required to load the plugin. You can generate the moduleinfo.json manually using the helper if desired (see below).
- Linux: Builds as a VST3 with standard compilers (GCC/Clang). As on macOS, Windows‑only staging/deploy targets are skipped.

Optional visualizer dependency (projectM)
- If libprojectM v4 is available via vcpkg (recommended), the build links it and defines HAVE_PROJECTM. If not found, MilkDAWp falls back to its internal OpenGL path.
- End users do NOT need to install projectM separately. When projectM is linked at build time, the build now bundles the required runtime libraries into the .vst3 on Windows/macOS/Linux; otherwise MilkDAWp automatically uses its fallback renderer.
- To enable projectM via vcpkg on non‑Windows, pass your vcpkg toolchain file to CMake (paths will differ):
  - -DCMAKE_TOOLCHAIN_FILE="/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
  - Then install the port: vcpkg install projectm4:x64-linux or projectm4:x64-osx

Minimal build recipes
- macOS
  1) Ensure a recent Xcode and CMake (3.16+).
  2) Optional: Configure vcpkg and install projectM: vcpkg install projectm4:x64-osx
  3) Configure and build:
     - cmake -B cmake-build-release -G "Xcode" [-DCMAKE_TOOLCHAIN_FILE="/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"]
     - cmake --build cmake-build-release --target MilkDAWp_VST3 --config Release
  4) The resulting MilkDAWp.vst3 bundle will be under cmake-build-release. Copy it to ~/Library/Audio/Plug-Ins/VST3.

- Linux (example with GCC/Clang)
  1) Ensure build tools: cmake, gcc/clang, make/ninja, and OpenGL drivers. JUCE dependencies are handled via FetchContent.
  2) Optional: Configure vcpkg and install projectM: vcpkg install projectm4:x64-linux
  3) Configure and build:
     - cmake -B cmake-build-release -G Ninja [-DCMAKE_TOOLCHAIN_FILE="/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"]
     - cmake --build cmake-build-release --target MilkDAWp_VST3
  4) Install the bundle to ~/.vst3 or /usr/lib/vst3 depending on your host’s search paths.

Helper and moduleinfo.json on macOS/Linux
- The juce_vst3_helper (or MilkDAWp_vst3_helper on JUCE 8) target is available cross‑platform. You can run it manually to generate Contents/Resources/moduleinfo.json for the bundle:
  - "<path to>/MilkDAWp_vst3_helper[.exe]" -create -version 0.4.0 -path "<bundle root>" -output "<bundle root>/Resources/moduleinfo.json"
- The Windows‑only post‑build .bat that automates this step is intentionally skipped on macOS/Linux.

Distributables (Win/macOS/Linux)
- After building the plugin target (MilkDAWp_VST3), you can create a redistributable archive using the same CMake target on all platforms:
  - Windows (CLion Debug example):
    - cmake --build D:\Dev\Hobby\MilkDAWp\cmake-build-debug --target package_vst3_zip
    - Output: D:\Dev\Hobby\MilkDAWp\cmake-build-debug\MilkDAWp-v<version>.zip
  - macOS (example):
    - cmake --build cmake-build-release --target package_vst3_zip --config Release
    - Output: cmake-build-release/MilkDAWp-v<version>.zip
  - Linux (example):
    - cmake --build cmake-build-release --target package_vst3_zip
    - Output: cmake-build-release/MilkDAWp-v<version>.tar.gz
- Each archive contains the MilkDAWp.vst3 bundle ready to distribute.

Known caveats
- On macOS, code signing/notarization is out of scope here; signing may be required by your host or for distribution.
- On Linux, ensure your DAW supports VST3 and that your GPU/driver supports the OpenGL features required by projectM or the fallback renderer.
- If projectM is present but you want to force the fallback renderer for debugging, set the environment variable MILKDAWP_DISABLE_PROJECTM=1.

### More details on projectM usage in MilkDAWp
For comprehensive information about projectM runtime parameters, playlists, formats, and integration details, see:

- [PROJECTM_DETAILS.md](PROJECTM_DETAILS.md)

