# MilkDAWp

MilkDAWp is a JUCE-based VST3 audio plug-in with an OpenGL visualizer and optional projectM integration.

This README documents a working Windows setup (CLion + CMake + vcpkg) that produces a Cubase‑friendly VST3 bundle. It also applies to macOS and Linux with minor path differences.

## Highlights

- VST3 plugin built with JUCE 8
- C++20, CMake-based build
- Optional projectM v4 integration for visuals (auto-detected)
- Cross-platform (Windows, macOS, Linux)
- Zero vendored third-party sources: dependencies are fetched/installed at configure time

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

### Does projectM support preset playlists? What would that look like in a VST?
Provides an optional playlist component that can manage ordered preset sequences, randomization and filtering. In MilkDAWp we detect and link this component when available via vcpkg and expose a VST-friendly surface on top.

Implementation notes in this repo
- Build-time detection: CMake calls find_package(projectM4 QUIET COMPONENTS Playlist). When the playlist library is present it links libprojectM::playlist and defines HAVE_PROJECTM_PLAYLIST=1.
- Runtime behavior: When built with the C API, MilkDAWp creates a playlist manager and connects it to the projectM instance at init time. You will see a log line like "projectM playlist API available: playlist manager created and connected". If the playlist API is not present in your projectM build, we log that and continue without it.
- Fallback: If projectM is not available at all, MilkDAWp uses its internal OpenGL renderer and the concept of a playlist does not apply.

What a VST UX typically exposes
- Source of presets
  - Scan a folder tree for .milk files (recursive) and build an internal list.
  - Optionally load/save .m3u or projectM JSON playlists if available.
- Transport and selection controls (host-automatable where sensible)
  - Preset Index (int): selects a preset by index from the current list.
  - Next/Previous Preset (momentary buttons): step through the list.
  - Random/Shuffle (toggle): enable randomized selection.
  - Lock Playlist (toggle): prevents auto-advance (useful for A/B or rehearsing).
  - Crossfade Time (seconds): maps to projectm_set_soft_cut_duration.
  - Preset Duration (seconds): maps to projectm_set_preset_duration (when not locked).
  - Beat Hard Cuts (toggle + sensitivity): maps to the hard-cut parameters for on-beat jumps.
- Display and feedback
  - Current preset name and position (e.g., 12/250).
  - Optional search/filter box to narrow to a subset (e.g., author or keyword), then play within that subset.

Current status in MilkDAWp
- We already support loading a preset directly by file path and selecting by index from a scanned folder.
- When compiled with playlist support (HAVE_PROJECTM_PLAYLIST), a projectM playlist manager is created and connected internally; this paves the way for richer playlist operations (shuffle, filters, next/prev) without changing the rendering thread model.
- UI wiring for full playlist transport is intentionally minimal at the moment; expect future updates to expose Next/Prev/Shuffle and optional playlist file I/O via simple buttons in the editor.

How to enable playlist support
- Install projectM v4 via vcpkg with playlist component (the default port provides it):
  - vcpkg install projectm4:x64-windows
- Configure CMake with your vcpkg toolchain so find_package(projectM4) succeeds.
- Build the plugin or helper. Look for build logs: "Linking projectM playlist component". At runtime, watch the "PM" log channel for the playlist availability line mentioned above.

Limitations and notes
- The playlist API is optional in projectM. If your package of projectM omits it, MilkDAWp still builds and runs; only playlist-specific features are disabled.
- Preset artistic parameters remain preset-defined; playlists control ordering and timing, not visual styles.

Playlist files: formats, where to find them, and how we’ll use them
- What formats does projectM accept?
  - The playlist component itself is an in-memory manager; it does not mandate a single on-disk format. In practice, common, interoperable formats are:
    - M3U/M3U8: simple text, one preset path per line; lines beginning with '#' are comments. UTF‑8 (m3u8) recommended on modern systems.
    - JSON (projectM tools): some projectM GUI tools export/import a JSON array of file paths and options. We may add import/export for this later.
    - Plain text list: a pragmatic subset identical to M3U without the header (also works for our importer when added).
- Do we ship any playlist files today?
  - No. The repo ships only a tiny set of test presets under resources/presets/tests/*.milk for visual regression. There are no .m3u/.json playlists included by default.
- Where should I put my playlists so MilkDAWp can find them?
  - Next to presets inside the bundle (good for packaging): <Bundle>/Contents/Resources/presets/*.m3u
  - Or in your profile preset folder: %APPDATA%/projectM/presets/*.m3u
  - Relative paths in an M3U are resolved relative to the playlist’s location; absolute paths are also fine.
- How will the plugin manage playlists?
  - Short term (current build):
    - We scan folders for .milk files and let you pick by index or direct file path. The internal projectM playlist manager is created (when available) and will be used for Next/Prev/Shuffle once UI is exposed.
  - Near term (planned, trivial to wire):
    - Import M3U/M3U8: read one path per line, ignore comments, de‑dupe, and feed into projectm_playlist_* APIs.
    - Export current list to M3U for portability.
    - UI buttons: Next, Prev, Shuffle toggle, and a display of “current preset (n/total)”. These map to playlist C API calls (set_shuffle, set_position, etc.).
  - Advanced (optional, later):
    - JSON import/export compatible with projectM GUI tools, and simple text search/filter mapped to projectm_playlist_set_filter/apply_filter.

Tips for creating a quick M3U playlist
- Create a text file presets.m3u in one of the locations above with contents like:
  # MilkDAWp demo playlist
  resources/presets/tests/001-line.milk
  resources/presets/tests/100-square.milk
  resources/presets/tests/250-wavecode.milk
- Restart or reload the plugin; once M3U import is wired, these will populate the internal playlist. Until then, you can still select by index or load by path from the scanned folder.

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

There are convenience targets to copy the built plugin and presets into the user/system VST3 folder:

- Manual targets:
  - `deploy_vst3_user` → Copies MilkDAWp.vst3 into the per-user VST3 folder.
  - `deploy_vst3_user_presets` → Also copies presets into the bundle after deploy.

Examples (use your existing CLion build directories):
- Debug profile:
  - `cmake --build D:\\Dev\\Hobby\\MilkDAWp\\cmake-build-debug --target deploy_vst3_user`
  - `cmake --build D:\\Dev\\Hobby\\MilkDAWp\\cmake-build-debug --target deploy_vst3_user_presets`
- Release profile:
  - `cmake --build D:\\Dev\\Hobby\\MilkDAWp\\cmake-build-release --target deploy_vst3_user`
  - `cmake --build D:\\Dev\\Hobby\\MilkDAWp\\cmake-build-release --target deploy_vst3_user_presets`

Install locations (Windows):
- Per-user: `%LOCALAPPDATA%\\Programs\\Common\\VST3` (no admin rights required)
- System-wide: `%COMMONPROGRAMFILES%\\VST3` (requires admin rights)

Note: Older docs referenced `deploy_vst3` and an `MILKDAWP_DEV_DEPLOY` option; current targets are `deploy_vst3_user` and `deploy_vst3_user_presets`.

---

## Distributing MilkDAWp to other people (Windows)

### Simple self‑extracting installer (Windows, optional)
Make a self‑extracting ZIP (SFX) that auto‑extracts to the per‑user VST3 folder. We’ve added a tiny helper and config to make this easy.

Per‑user install destination (no admin rights):
- %LOCALAPPDATA%\Programs\Common\VST3

Two ways to package:
1) One‑click SFX EXE using 7‑Zip (preferred if you have 7‑Zip installed)
- Build the plugin bundle (MilkDAWp_VST3 target), then locate the built bundle folder MilkDAWp.vst3 in your build tree.
- Run the helper script:
  - PowerShell example:
    - .\\tools\\installer\\make_sfx_installer.ps1 -Vst3Path "<path to>\\MilkDAWp.vst3" -OutputDir "D:\\temp"
- Output:
  - D:\\temp\\MilkDAWp_VST3_Installer.exe (self‑extracting; extracts to %LOCALAPPDATA%\Programs\Common\VST3)
  - D:\\temp\\MilkDAWp_vst3.zip (also produced)
- Note: The script auto‑detects WinRAR or 7‑Zip in common locations and prefers WinRAR when available. If neither is found, see option 2.

2) ZIP + install.bat (no external tools required)
- The same script will emit a ZIP and a portable Install_MilkDAWp_per_user.bat if 7‑Zip isn’t available.
- Ship both files to your tester; they can double‑click the BAT to extract the ZIP to %LOCALAPPDATA%\Programs\Common\VST3.

Manual 7‑Zip method (if you prefer doing it yourself)
- Create an archive whose root contains MilkDAWp.vst3
- Use the provided SFX config at tools\\installer\\sfx-config.txt
- Build the EXE by concatenation in a command prompt:
  - copy /b "C:\\Program Files\\7-Zip\\7z.sfx" + tools\\installer\\sfx-config.txt + payload.7z MilkDAWp_VST3_Installer.exe

Manual WinRAR method (if you prefer WinRAR)
- Ensure WinRAR is installed (rar.exe or WinRAR.exe available).
- Use the provided SFX comment at tools\\installer\\winrar-sfx-comment.txt
- From a command prompt, package the folder where the archive root contains MilkDAWp.vst3:
  - cd <folder containing MilkDAWp.vst3>
  - "C:\\Program Files\\WinRAR\\rar.exe" a -sfx -r -ep1 -ztools\\installer\\winrar-sfx-comment.txt D:\\temp\\MilkDAWp_VST3_Installer.exe *
  - The resulting EXE will extract to %LOCALAPPDATA%\\Programs\\Common\\VST3 when run.

Notes
- SFX extracts only; it does not modify registry or require admin privileges.
- For uninstall, just delete %LOCALAPPDATA%\Programs\Common\VST3\MilkDAWp.vst3
- DAWs typically pick up the plugin from that folder; if not, trigger a rescan in your host.

Short answer
Zip the MilkDAWp.vst3 bundle and ask users to copy it into a VST3 folder. See install locations below.
- No extra installs are typically needed besides the Microsoft Visual C++ x64 Redistributable. All other runtime DLLs used via vcpkg (e.g., projectM) are staged next to the plugin automatically by the build.

What to ship
- The single folder bundle: `MilkDAWp.vst3` (it is a directory). Inside it, our build places:
  - Contents/Resources/moduleinfo.json (generated by the helper)
  - Contents/Resources/presets (minimal test presets; you can add more)
  - Any required vcpkg DLLs in the same directory as the host/helper when applicable; the plugin itself loads them from the standard DLL search path. CMake sets `VCPKG_APPLOCAL_DEPS=ON`, and there are options to stage more DLLs during local runs.

Where users should install it
- Per-user (no admin): `%LOCALAPPDATA%\Programs\Common\VST3` → Create the `VST3` folder if missing and place `MilkDAWp.vst3` inside.
- System-wide (admin): `%COMMONPROGRAMFILES%\VST3` → Place `MilkDAWp.vst3` here for all users.
- Many DAWs scan these folders automatically; otherwise, trigger a plug‑in rescan in the host.

Runtime dependencies
- Microsoft Visual C++ Redistributable for Visual Studio 2015–2022 (x64). Most systems already have it. If a user is missing it, point them to: https://aka.ms/vs/17/release/vc_redist.x64.exe
- You do NOT need JUCE installed on the target machine.
- projectM is optional; users do not need to install it separately. If the plugin was built with projectM, the required DLLs are copied next to the helper/build output and typically resolved by the host when the plugin is installed in the standard location. If projectM DLLs aren’t present, the plugin falls back to its internal renderer.

Presets for better visuals
- The shipped bundle contains a tiny test set. Users can drop additional .milk files into:
  - The bundle: `%...%/MilkDAWp.vst3/Contents/Resources/presets`
  - Or their profile: `%APPDATA%/projectM/presets`

Developer convenience targets (repeat)
- `deploy_vst3_user`: Copies to per‑user VST3 folder.
- `deploy_vst3_user_presets`: Same but ensures presets are present.

Notes for advanced packaging
- CMake options exist to ease local runs:
  - `MILKDAWP_APPLOCAL_MSVC` (OFF by default): Copy MSVC/UCRT next to the helper.
  - `MILKDAWP_STAGE_ALL_VCPKG_DLLS` (OFF by default): Bulk‑copy all vcpkg DLLs to the helper dir.
- For end‑users, prefer relying on the MSVC redistributable and keeping only necessary DLLs alongside the plugin.

## Troubleshooting: black visualization window

If the visual window briefly shows and then turns black:
- Try lowering your audio driver buffer size (ASIO block size). Very large buffers can starve the render timer on some hosts and GPUs; reducing the buffer often restores steady rendering.
- Toggle projectM to isolate the path:
  - Set MILKDAWP_DISABLE_PROJECTM=1 and relaunch the host. If the fallback renderer keeps drawing, the issue is likely specific to projectM/GL state.
- Use a Debug build for richer logs. Watch for lines mentioning:
  - "C API: initial preset loaded" / projectM init
  - "Test program compiled (gl_VertexID path)" (our GL health check)
  - Any GL error lines around render
- Try a simple preset from resources/presets/tests (e.g., 001-line.milk) to rule out preset-specific issues.
- Resize the plugin window slightly or move it between monitors to force a GL re-layout; on some drivers this recovers.

If the problem persists, please share:
- Host name/version, GPU and driver version, buffer size, and a short log excerpt from opening the UI until it goes black.

## Development notes

- Language standard: C++20
- CMake minimum: 3.16
- JUCE splash, web browser, and CURL are disabled for smaller footprint.
- The visualizer supports two modes:
    - projectM mode (when `HAVE_PROJECTM` is defined because `projectM4` was found)
    - Fallback OpenGL mode (simple quad rendering) when projectM isn’t present

### Presets: finding and installing

Rich visuals come from MilkDrop/projectM preset files (*.milk). The repo includes only a tiny test set. To get the full experience, install a larger preset pack.

Where to get presets
- Curated “cream of the crop” collection (commonly used):
  - https://github.com/projectM-visualizer/presets-cream-of-the-crop
- Official projectM packs:
  - https://github.com/projectM-visualizer/presets-pack (recommended)
  - https://github.com/projectM-visualizer/projectm-preset-pack

Where to put presets so MilkDAWp can find them
MilkDAWp searches several locations automatically at runtime (recursive scan for .milk):
1) Inside the plugin bundle (what the build/deploy step uses):
   - <Bundle>/Contents/Resources/presets
     - Windows VST3 bundle path (per-user deploy): %LOCALAPPDATA%/Programs/Common/VST3 or %COMMONPROGRAMFILES%/VST3/MilkDAWp.vst3
   - Our CMake copies resources/presets into this location for the built plugin on Windows.
2) In your checkout/dev tree (useful while developing):
   - resources/presets
3) vcpkg-style shared data directory near the executable:
   - <...>/share/projectM/presets
4) Windows user profile:
   - %APPDATA%/projectM/presets  (e.g., C:\Users\<You>\AppData\Roaming\projectM\presets)
5) Windows system-wide install:
   - C:\Program Files\projectM\presets

Quick install steps (Windows example)
1) Download and extract a preset pack (e.g., presets-cream-of-the-crop).
2) Copy the extracted folders/files (*.milk) into one of the locations above. Good options:
   - Per-user: %APPDATA%/projectM/presets
   - Packaged plugin: %COMMONPROGRAMFILES%/VST3/MilkDAWp.vst3/Contents/Resources/presets
3) Restart your host or reload the plugin. MilkDAWp logs the resolved preset directory (“PM” log channel) and will show richer visuals.

Notes
- Nested folders are fine; scanning is recursive.
- You can also use the UI button “Load Preset…” to pick a single preset file directly; this doesn’t require scanning.
- For more details, see resources/presets/README.md.

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

## Project structure (short)
```
src/                     # Plugin source (processor, editor, renderers, utils)
resources/               # Assets (e.g., presets)
CMakeLists.txt           # Build configuration
THIRD_PARTY_LICENSES     # Licenses for third-party components
```

## Upgrading dependencies

- JUCE: change the `GIT_TAG` in CMake to the desired release tag.
- projectM: upgrade via your package manager (`vcpkg upgrade` or equivalent).

---

## License

See `THIRD_PARTY_LICENSES` for third-party components. Project license is provided in `LICENSE` (if present).
