MilkDAWp – Project-specific developer guidelines

Audience: Advanced C++/CMake/JUCE developers contributing to MilkDAWp.
Scope: Build/configuration specifics, pragmatic testing strategy (what exists today), and development/debugging conventions unique to this repo.

1) Build and configuration
- Toolchain
  - CMake 3.16+, MSVC x64 on Windows (64‑bit enforced; see CMakeLists.txt early check).
  - JUCE is pulled via FetchContent at configure time.
  - Optional: projectM v4 via vcpkg. If not found, the build falls back to an internal OpenGL renderer (HAVE_PROJECTM undefined).
- vcpkg / projectM (optional visualizer)
  - Install projectM v4 in your vcpkg instance (Windows triplet example):
    - vcpkg install projectm4:x64-windows
  - Configure CMake with the vcpkg toolchain so find_package(projectM4) succeeds:
    - -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
  - Behavior:
    - If projectM is found: links libprojectM::projectM, defines HAVE_PROJECTM and (if available) links libprojectM::playlist with HAVE_PROJECTM_PLAYLIST=1. Renderer switches to the projectM C API path.
    - If not found: build proceeds without projectM; fallback renderer is used.
  - Runtime DLL staging (Windows):
    - VCPKG_APPLOCAL_DEPS=ON is set so vcpkg DLLs are copied next to binaries. Additional staging around juce_vst3_helper exists and can be enabled to ease local runs (see MILKDAWP_APPLOCAL_MSVC and MILKDAWP_STAGE_ALL_VCPKG_DLLS options in CMakeLists.txt).
- CLion profiles (already configured in this workspace)
  - Use existing profiles only:
    - Debug → D:\Dev\Hobby\MilkDAWp\cmake-build-debug
    - Release → D:\Dev\Hobby\MilkDAWp\cmake-build-release
  - To enable projectM under CLion: add the vcpkg toolchain flag to CMake options in Settings → Build, Execution, Deployment → CMake.
- Manual command-line configure/build
  - Not required here because CLion profiles are pre-generated, but if you do configure outside, keep the 64‑bit generator and optional toolchain file consistent with README.md.

2) Testing: what exists today and how to use it
There is no formal CTest/UnitTest suite in this repository. Current validation is pragmatic and centers on:
  a) Building the plugin and its helper executable(s).
  b) Exercising discovery/metadata paths via the helper.
  c) Visual regression by curated projectM presets under resources/presets/tests.

2.1 Minimal sanity test (verified now)
- Purpose: ensure the codebase builds and JUCE’s VST3 helper for this plugin runs, outputting the plugin’s class/module metadata.
- Steps (Windows, CLion Debug profile):
  - Build the helper target:
    - cmake --build D:\Dev\Hobby\MilkDAWp\cmake-build-debug --target MilkDAWp_vst3_helper
  - Run it to print JSON metadata (no plugin bundle required for this mode):
    - & D:\Dev\Hobby\MilkDAWp\cmake-build-debug\MilkDAWp_vst3_helper.exe --help
  - Expected: JSON block including Name "MilkDAWp", Version "0.3.0", two classes (component/controller) with VST SDK version (e.g., 3.7.12). This was executed successfully during guideline preparation.
- Notes:
  - The helper path is derived from the active CMake profile’s build dir. Prefer running in Debug first for readable logs.

2.2 Building the plugin and generating moduleinfo.json
- Build the VST3 target: MilkDAWp_VST3 (Debug or Release profile).
- The CMake PRE_LINK step generates a post-build .bat that will invoke the helper to create Contents/Resources/moduleinfo.json for the bundle, provided the helper executable exists in the build tree.
- If you wish to run it manually:
  - Determine bundle path: typically .../cmake-build-<cfg>/MilkDAWp_VST3/<something>/MilkDAWp.vst3/Contents (use CLion’s build output to locate).
  - Execute helper with flags (example):
    - "<path to>\\MilkDAWp_vst3_helper.exe" -create -version 0.3.0 -path "<bundle root>" -output "<bundle root>\\Resources\\moduleinfo.json"
- Windows specifics:
  - If using dynamic vcpkg triplets, ensure relevant DLLs are on PATH or staged next to the helper/bundle. The CMakeLists has logic to copy glew32.dll, projectM-4.dll (and -playlist when present) to the helper dir when available.

3) Development and debugging tips unique to this repo
- Logging
  - Debug builds add JUCE_LOG_ASSERTIONS=1 and MILKDAWP_ENABLE_LOGGING=1. Consume logs via your debugger or the host console. ProjectMRenderer logs diagnostic lines such as:
    - GL test shader compile status (“Test program compiled (gl_VertexID path)”)
    - projectM presence/playlist linkage messages during configure/build.
- Environment switches
  - MILKDAWP_DISABLE_PROJECTM=1: Disables projectM even if built; forces fallback renderer (useful to bisect GL issues originating from projectM).
- Build flags and options (see top-level CMakeLists.txt)
  - MILKDAWP_APPLOCAL_MSVC (OFF by default): App-local MSVC/UCRT copy next to juce_vst3_helper.
  - MILKDAWP_STAGE_ALL_VCPKG_DLLS (OFF by default): Bulk-copy all vcpkg DLLs to the helper dir. Use only for local dev, not for packaging.
- OpenGL robustness
  - ProjectMRenderer contains a “TEST-only, attribute-free shader” path using gl_VertexID to decouple from VAO/VBO state issues. When diagnosing GL init problems, check for the log line indicating the test program compiled; this isolates shader compiler/GL context health independent of projectM.
- Preset discovery
  - ProjectMRenderer uses the repository’s resources/presets directory when running from the source tree. Ensure your working directory points at the repo root in dev tools; otherwise presets may not be found.

4) Simple demo: from clean repo to a passing sanity test
- Pre-reqs: CLion workspace with the provided Debug profile.
- Steps:
  1) Build MilkDAWp_vst3_helper in Debug.
     - cmake --build D:\Dev\Hobby\MilkDAWp\cmake-build-debug --target MilkDAWp_vst3_helper
  2) Run the helper and verify JSON metadata shows Version 0.3.0 and two classes.
     - & D:\Dev\Hobby\MilkDAWp\cmake-build-debug\MilkDAWp_vst3_helper.exe --help
  3) Optional: Build MilkDAWp_VST3 and verify that Contents/Resources/moduleinfo.json is generated via the post-build step.
  4) Optional: If vcpkg is configured with projectM, load a test preset in your host and check rendering; otherwise set MILKDAWP_DISABLE_PROJECTM=1 to force fallback and confirm the plugin window displays.

5) Housekeeping
- Do not add new build directories; use the CLion-provided cmake-build-debug or cmake-build-release.
- When creating ad-hoc scripts for validation, avoid committing them. For this guideline task, no additional files were added other than this document.
