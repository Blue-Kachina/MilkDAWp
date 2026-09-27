# ADR-0010: Android standalone app as a planned post-1.0 target

Status: Recommended (D16). Amends §1's non-goals; adds Phase 7.

## Context

The original plan for MilkDAWp included an Android version of the standalone
app. The roadmap listed "iOS/Android" as a 1.0 non-goal (§1), with the
video-first UI kept touch-compatible "so a mobile shell is not ruled out
later", but nothing scheduled it. An audit on 2026-09-27, against the pinned
JUCE 9.0.2 and projectM sources, found:

**Already in Android's favour**

- `milkdawp_core` is plain C++20 with no JUCE, so it ports as is.
- projectM at the ADR-0008 pin forces OpenGL ES on Android
  (`cmake_dependent_option(ENABLE_GLES ... "NOT ... CMAKE_SYSTEM_NAME STREQUAL
  Android" ON)`), and its shaders are `#version 300 es`. vcpkg ships
  `arm64-android` and `x64-android` triplets.
- The engine's own GL calls (`GlFrameTarget`, FBOs, `GL_RGBA8`,
  `glReadPixels`, pixel-pack buffers) are valid in OpenGL ES 3.0. Surfaces
  draw with JUCE's `copyTexture`, which JUCE also uses on ES.
- `ProjectMLibrary` already tries `libprojectM-4.so` by bare name, which
  Android's linker resolves from the APK's `lib/` folder. projectM stays a
  separate, dynamically loaded LGPL library.
- Presets go to projectM as text (`projectm_load_preset_data`), so projectM
  never reads the filesystem itself.
- The engine-owned offscreen context (ADR-0009) is a better fit for Android
  than for the desktop platforms. Android destroys window surfaces whenever
  the app goes to the background; a context that belongs to no window keeps
  projectM and its visual state across that.
- The drawer reveals on tap as well as hover, and every action is
  pointer-reachable (§4.9). JUCE 9 has Oboe and OpenSL audio backends.

**Gaps**

1. **JUCE 9 cannot share an Android GL context.** In JUCE 9.0.2,
   `juce_OpenGL_android.h:125` takes the share context as
   `void* /*contextToShareWith*/` and never uses it.
   `juce_OpenGLContext.cpp:1818` always creates the context with
   `EGL_NO_CONTEXT`, and recreates it on every `surfaceCreated`. EGL shares
   only at creation, so ADR-0009's shared-texture path cannot work on stock
   JUCE for Android.
2. **`OffscreenGLContext` would build the wrong context on Android.**
   Android defines `__linux__`, so the Linux branch would run. That branch
   asks for desktop GL 3.3 core (`eglBindAPI(EGL_OPENGL_API)`), which Android
   does not have.
3. **JUCE's CMake API does not support Android.** Its `docs/CMake API.md`
   says: "Android targets are not currently supported."
   `juce_add_gui_app` cannot produce an APK.
4. **No system-audio loopback like the desktop's.** The options are
   `AudioPlaybackCapture` (Android 10+, MediaProjection consent plus a
   foreground service; apps can opt out of capture) and the `Visualizer`
   audio effect (8-bit, about 1024-sample snapshots, probably too coarse for
   our onset and tempo tracking). Microphone input works through JUCE.
5. **User folders are `content://` trees.** Under scoped storage, a folder
   the user picks comes from the Storage Access Framework, not a path.
   `Playlist::scanFolder` uses `std::filesystem`.
6. **Desktop-only shell features.** These are the menu bar,
   single-instance guard, window geometry, file associations, the Output
   window on a second display, and the F11/Esc shortcuts. Phase 4 has not
   built them yet.
7. **Performance.** Mobile GPUs vary widely with MilkDrop presets. The
   render loop's `GL_TIME_ELAPSED` query is not core ES 3.0; it needs
   `EXT_disjoint_timer_query` or has to be skipped.

## Decision

- **Scope.** Android is a planned post-1.0 target: the standalone app only
  (there is no Android plugin format worth targeting). Recommended floor:
  Android 10 (API 29, for `AudioPlaybackCapture`), OpenGL ES 3.0, arm64-v8a,
  plus x86_64 for the emulator. The work is Phase 7. Nothing here moves
  1.0's scope.
- **Build.** A Gradle project under `android/` builds our CMake tree
  through the NDK (`externalNativeBuild`), with vcpkg chainloaded for the
  `*-android` triplets. It produces one shared library holding
  `milkdawp_core`, `milkdawp_engine`, `milkdawp_ui` and the app entry point,
  plus JUCE's Android Java glue. The glue has to be supplied by hand,
  because `juce_add_gui_app` does not do it. Keeping a Projucer project only
  for Android is ruled out unless the Phase 7.1 spike shows the Gradle route
  can't work: it would be a second build system (§11, "one way").
- **Rendering.**
  - `OffscreenGLContext` gets an `__ANDROID__` branch, checked before
    `__linux__`: an `EGL_OPENGL_ES_API` ES 3.x context, surfaceless where
    `EGL_KHR_surfaceless_context` exists, else a 1×1 pbuffer.
  - Surfaces use the **CPU readback fallback** from day one. It was built in
    2.15 for drivers that refuse to share and for Linux (ADR-0009), and it
    needs no JUCE change.
  - A JUCE patch that keeps the share pointer and passes it to
    `initEGLContext` is an optimisation, proposed upstream first. Readback
    stays the fallback.
  - The GPU timer query is optional; `RenderStats::gpuFrameMs` already
    reports -1 when unavailable.
- **Audio input tiers.**
  - Microphone through JUCE's `AudioDeviceManager` (Oboe) first, with the
    `RECORD_AUDIO` runtime permission.
  - Second, an Android implementation of `SystemAudioCapture`, the Phase 4.7
    interface, backed by `AudioPlaybackCapture` (Kotlin + JNI, foreground
    service).
  - The `Visualizer` effect is not the primary source; revisit only if
    measurements show our analysis copes with 8-bit input.
- **Presets.** The engine always scans a real directory the app can read.
  On Android that is app-private storage. The shell fills it in two ways:
  it extracts the bundled pack from APK assets (first run, and again after
  an app update), and an "Import presets" flow copies a user-picked SAF
  tree into it, textures included. The core and engine gain no `content://`
  support.
- **Shell boundary rules for Phase 4.** These keep Android cheap.
  - `milkdawp_ui` holds only what every shell shares.
  - The desktop-only items from gap 6 live in the desktop app shell, not in
    shared code: menu bar, single-instance guard, window geometry, file
    associations, second-display Output window. Keyboard shortcuts stay
    shared (a hardware keyboard or TV remote on Android still benefits),
    but no action may be keyboard-only.
  - `SystemAudioCapture` is an interface in the app with per-platform
    implementation files, chosen at runtime, so an Android one plugs in
    without touching the others.
  - Preferences use `juce::PropertiesFile`, which works on Android.
- **Distribution and licences.** Distribution is through Play and
  sideloaded APKs. AGPL-3.0 on Play is allowed; the store listing and the
  About screen carry the source offer. projectM ships as a separate `.so`
  inside the APK (LGPL-2.1, dynamic, as D10 requires). The Play upload key
  is a new open item next to D11.

## Consequences

- **1.0 is unaffected.** Phase 7 starts after 1.0. The only pre-1.0 work
  is the readback fallback (2.15, which also fixes Linux windows showing
  black) and following the Phase 4 boundary rules above.
- **The biggest unknown is the build (gap 3).** Phase 7.1 is a spike (Gradle
  + NDK + vcpkg android triplet, core and engine built, projectM loaded and
  rendering on a device) before anything else is scheduled.
- **Platforms differ in where frames come from.** On Android, and on Linux
  until EGL sharing exists, surfaces show CPU copies one frame behind the
  engine: a full-frame GPU→CPU→GPU copy per surface per frame. At 1080p60
  that is about 500 MB/s each way, which is acceptable for one surface on a
  modern phone. Adaptive quality (5.3) scales the frame size and with it
  this cost. The JUCE sharing patch removes it.
- **Android's audio-input tier differs from the desktop's.** "No
  configuration needed" (the Phase 4 exit) becomes "one permission prompt"
  on Android: microphone, or screen-capture consent for playback capture.
  Some streaming apps block playback capture, so the app must explain what
  it can't hear.
- **Presets on Android are copies.** An imported pack does not follow later
  edits in the source folder; re-import refreshes it. That is acceptable for
  a consume-only app (§1 non-goals: no preset authoring).
- **The preset UI must scale to phones.** The drawer's popup preset tree at
  a 14 px font does not work on a phone, so the post-1.0 searchable preset
  browser becomes a Phase 7 requirement.
- **The pinned JUCE and projectM have not been built for Android.** The
  projectM overlay port has only built for desktop triplets. JUCE bumps
  (§4.11) gain an Android build in CI once Phase 7 adds the job.
- **Ruled out:**
  - iOS (a different shell, App Store rules for dynamic libraries, and no
    Mac here to build on).
  - An Android plugin format.
  - Rendering projectM directly in JUCE's Android context. That would
    reset the visual on every trip to the background, which is v1's bug
    (§2.4).
  - The `Visualizer` effect as the main audio source.
  - A Projucer-only Android project, unless the 7.1 spike fails.
