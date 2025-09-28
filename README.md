>>>>>>> # MilkDAWp

MilkDAWp is a JUCE-based VST3 audio plug-in with an OpenGL visualizer and optional projectM integration.

This README documents the working Windows setup (CLion + CMake + vcpkg) that produces a Cubase‑friendly, statically linked VST3 bundle.

## Highlights

- VST3 plugin built with JUCE 8
- C++20, CMake-based build
- Optional projectM v4 integration for visuals (auto-detected)
- Cross-platform (Windows, macOS, Linux)
- Zero vendored third-party sources: dependencies are fetched/installed at configure time


Point CMake to the vcpkg toolchain file when configuring. For example (Windows):
```
-DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"

```

Adjust the path to where you installed vcpkg.

### 2) Configure and build (command line)

- Windows (MSVC, x64):
```
bash cmake -S . -B build ^ -A x64 ^ -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake" cmake --build build --config Release
```
=======

If you want the projectM visualizer, install `vcpkg` and the projectM v4 package:

- Windows (PowerShell):
    - `vcpkg install projectm4:x64-windows`
- macOS:
    - `vcpkg install projectm4`
- Linux:
    - `vcpkg install projectm4`

Point CMake to the vcpkg toolchain file when configuring. For example (Windows):
```
-DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"

```

Adjust the path to where you installed vcpkg.

### 2) Configure and build (command line)

- Windows (MSVC, x64):
```
bash cmake -S . -B build ^ -A x64 ^ -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake" cmake --build build --config Release
```

## Quick Start

### projectM runtime parameters available in MilkDAWp (v4 C API)
The bundled projectM v4 headers expose a small set of global/runtime parameters that can be changed at run time. These affect preset switching behavior, timing, mesh resolution, and rendering window size. They do not include per‑preset artistic variables like scale, line thickness, or ghosting — those are defined inside individual Milkdrop presets and are not globally adjustable via the C API.

Available functions/parameters (see projectM-4/parameters.h):
- Texture search paths:
  - projectm_set_texture_search_paths(instance, paths, count)
- Beat detection sensitivity:
  - projectm_set_beat_sensitivity(instance, float)
  - projectm_get_beat_sensitivity(instance)
- Hard cuts (instant preset changes on strong beats):
  - projectm_set_hard_cut_enabled(instance, bool)
  - projectm_get_hard_cut_enabled(instance)
  - projectm_set_hard_cut_duration(instance, double seconds)
  - projectm_get_hard_cut_duration(instance)
  - projectm_set_hard_cut_sensitivity(instance, float)
  - projectm_get_hard_cut_sensitivity(instance)
- Soft cuts (crossfade time between presets):
  - projectm_set_soft_cut_duration(instance, double seconds)
  - projectm_get_soft_cut_duration(instance)
- Preset display duration (max time before switching using a soft cut):
  - projectm_set_preset_duration(instance, double seconds)
  - projectm_get_preset_duration(instance)
- Per‑pixel mesh size (resolution for per‑pixel equations; clamped to even values in [8,300]):
  - projectm_set_mesh_size(instance, size_t width, size_t height)
  - projectm_get_mesh_size(instance, size_t* width, size_t* height)
- FPS hint (passed to presets; presets may use it for timing):
  - projectm_set_fps(instance, int32_t fps)
  - projectm_get_fps(instance)
- Aspect ratio correction flag (if a preset supports it):
  - projectm_set_aspect_correction(instance, bool)
  - projectm_get_aspect_correction(instance)
- "Easter egg" factor (affects randomized preset display time distribution):
  - projectm_set_easter_egg(instance, float value)
  - projectm_get_easter_egg(instance)
- Preset lock (disables automatic transitions):
  - projectm_set_preset_locked(instance, bool)
  - projectm_get_preset_locked(instance)
- Window/viewport size in pixels (resets internal GL renderer):
  - projectm_set_window_size(instance, size_t width, size_t height)
  - projectm_get_window_size(instance, size_t* width, size_t* height)

Notes:
- There is no global parameter for “scale”, “line thickness”, or “ghosting”. Those are preset‑level variables in the Milkdrop equations. To affect them globally, you would have to modify presets or use a preset that exposes its own runtime controls.
- In MilkDAWp, we already map some host controls to these projectM parameters at runtime:
  - Speed control -> projectm_set_fps and projectm_set_preset_duration
  - Color Hue -> projectm_set_beat_sensitivity (as a creative mapping)
  - Color Saturation -> projectm_set_soft_cut_duration
  - Seed -> projectm_set_easter_egg

Tip: You can disable projectM entirely by setting the env var MILKDAWP_DISABLE_PROJECTM=1 before launching the host. Conversely, projectM is enabled by default when the library is available.

### 1) Install vcpkg (for projectM)

If you want the projectM visualizer, install `vcpkg` and the projectM v4 package:

- Windows (PowerShell):
    - `vcpkg install projectm4:x64-windows`
- macOS:
    - `vcpkg install projectm4`
- Linux:
    - `vcpkg install projectm4`

Point CMake to the vcpkg toolchain file when configuring. For example (Windows):
```
-DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"

```

Adjust the path to where you installed vcpkg.

### 2) Configure and build (command line)

- Windows (MSVC, x64):
```
bash cmake -S . -B build ^ -A x64 ^ -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake" cmake --build build --config Release
```

- macOS:
```
bash cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
-DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" cmake --build build --config Release

```

- Linux:
```
bash cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
-DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" cmake --build build --config Release

```

If you omit the toolchain file, the build still succeeds but will disable projectM (fallback renderer).

### 3) Configure in CLion

- Open: Settings (or Preferences on macOS) → Build, Execution, Deployment → CMake
- In “CMake options” add (example path shown for Windows):
  ```
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg-master/scripts/buildsystems/vcpkg.cmake"
  ```
- Apply, then “Reload CMake Project”.

---

## How dependencies are resolved

- JUCE
    - Declared with `FetchContent` in CMake and fetched at configure time.
    - Lives under your build directory (e.g. `build/_deps/juce-src`), not in the repo.
- projectM v4 (optional)
    - Found via `find_package(projectM4 QUIET)`.
    - When found, the build links `libprojectM::projectM` and defines `HAVE_PROJECTM` for conditional compilation.
    - If not found, the build continues without projectM and uses the fallback renderer.

During configure, you’ll see either:
- “Found projectM4 via find_package(projectM4)” (projectM enabled), or
- “projectM4 not found; building WITHOUT projectM (fallback renderer only)”.

---

## Outputs and deployment

- Build targets include the VST3: `MilkDAWp_VST3` (format-specific target generated by JUCE).
- On Windows, post-build steps copy `resources/presets` into the VST3 bundle under `Contents/Resources/presets`.

### Optional developer deploy (Windows)

There is a convenience target to copy the built plugin into the system VST3 folder:

- Option: `MILKDAWP_DEV_DEPLOY` (OFF by default)
- Manual target: `deploy_vst3`

To deploy manually:
```
bash cmake --build build --config Release --target deploy_vst3
```

To auto-deploy after each VST3 build (requires Administrator privileges), configure with:
```
-DMILKDAWP_DEV_DEPLOY=ON
```

This copies the built bundle into `%COMMONPROGRAMFILES%/VST3/MilkDAWp.vst3`.

---

## Development notes

- Language standard: C++20
- CMake minimum: 3.16
- JUCE splash, web browser, and CURL are disabled for smaller footprint.
- The visualizer supports two modes:
    - projectM mode (when `HAVE_PROJECTM` is defined because `projectM4` was found)
    - Fallback OpenGL mode (simple quad rendering) when projectM isn’t present

### Presets

- At build time on Windows, presets are copied into the VST3 bundle under `Contents/Resources/presets`.
- You can add or update preset files in `resources/presets/` and they will be staged accordingly.



JUCE sources are fetched into your build directory under `_deps/` at configure time.

- A simple file logger writes logs to the user’s application data directory, under `MilkDAWp/logs/`.
- Use the `MDW_LOG` macro in code to write tagged log lines.


## Troubleshooting

- “projectM4 not found; building WITHOUT projectM …”
    - Ensure you installed `projectm4` via vcpkg (or system package manager).
    - Make sure CMake is told to use the vcpkg toolchain file.
    - Clear your CMake cache if switching between toolchains and re-configure.

- “64-bit build required. Configure with -A x64.”
    - On Windows/MSVC, add `-A x64` to CMake configure or select the x64 generator in your IDE.

- VST3 not picked up by your DAW
    - On Windows, ensure the bundle is deployed to `%COMMONPROGRAMFILES%/VST3`.
    - Some hosts cache plug-ins; you may need to rescan or restart the host.

- OpenGL issues
    - Make sure your GPU drivers are up to date.
    - Headless/remote sessions may not provide hardware GL; use a local session.

---
=======

## Project structure (short)
```
src/ # Plugin source (processor, editor, renderers, utils) resources/ # Assets (e.g., presets) CMakeLists.txt # Build configuration CMakePresets.json # Optional presets for common configs THIRD_PARTY_LICENSES
```


## Upgrading dependencies

- JUCE: change the `GIT_TAG` in CMake to the desired release tag.
- projectM: upgrade via your package manager (`vcpkg upgrade` or equivalent).

---

## License

See `THIRD_PARTY_LICENSES` for third-party components. Project license is provided in `LICENSE` (if present).
