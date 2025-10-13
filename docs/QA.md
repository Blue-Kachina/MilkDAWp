# Phase 7 — Validation & QA

Purpose: Provide lightweight automated checks and a manual checklist to validate host compatibility and packaging with dynamic linking of libprojectM.

Contents
- Automated checks (CI)
- Manual DAW scan/load checklist
- Packaging checklist mapping
- Troubleshooting notes

Automated checks (CI)
- Unit tests: Built and run on Windows/macOS/Linux in GitHub Actions (job: build-and-test).
- Windows plugin build + runtime layout check (job: windows-plugin-build):
  - Builds the VST3 with MILKDAWP_WITH_PROJECTM=ON.
  - Runs scripts/ci/check_runtime_win.ps1 to verify a projectM .dll is copied next to the .vst3 bundle.
  - Uploads the .vst3 and copied DLL as CI artifacts for inspection.

Manual DAW scan/load checklist
- Windows (Reaper, Cubase, Ableton Live):
  1) Ensure VC++ Redistributable is installed (MSVC /MD runtime).
  2) Place MilkDAWp.vst3 and projectM DLL side-by-side in the scanning VST3 folder (by default, the build already copies there).
  3) Launch DAW, force plugin rescan, confirm MilkDAWp appears and instantiates without missing-DLL errors.
  4) Insert on an audio track, verify audio passes through and UI displays. Toggle About → Licenses link; verify documentation opens.

- macOS (Reaper, Logic Pro via AU wrapper, Cubase):
  1) Confirm libprojectM.dylib is present under Contents/Frameworks in the bundle.
  2) Verify rpath is @loader_path/../Frameworks (otool -l on the binary inside the bundle).
  3) Codesigning/notarization as needed for distribution (not enforced in CI).

- Linux (Reaper, Bitwig, Ardour):
  1) Confirm libprojectM.so is present under Contents/Resources/lib.
  2) Verify rpath $ORIGIN/../Resources/lib (readelf -d or ldd checks).

Packaging checklist mapping (from DEPENDENCY_REQUIREMENTS.md)
- Windows:
  - [x] Copy projectM.dll next to YourPlugin.vst3 (automated CI check).
  - [ ] Consider unique filename to avoid collisions (future enhancement).
- macOS:
  - [x] Copy libprojectM.dylib into Contents/Frameworks (implemented in CMake; manual verification recommended).
  - [ ] Codesign bundle + dylib for distribution (out of scope for CI).
- Linux:
  - [x] Copy libprojectM.so into Contents/Resources/lib (implemented in CMake; manual verification recommended).
- Notices & licenses:
  - [x] Include LICENSES/ and THIRD_PARTY_NOTICES.md in distribution payloads.

Troubleshooting
- Missing DLL on Windows:
  - Check CI log for "[Phase3] Copying" messages and the QA script results.
  - Ensure MILKDAWP_WITH_PROJECTM=ON and projectM target is SHARED (configure fails if not).
- Host can’t see plugin:
  - Confirm JUCE_VST3_COPY_DIR is set to a scanned directory, or copy artefacts manually.
- Rpath issues on macOS/Linux:
  - Verify link options and relative paths in the built bundle as outlined above.
