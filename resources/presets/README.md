MilkDAWp presets guide

Why your visuals look basic
- The presets under resources/presets/tests are intentionally minimal. They exist to verify specific rendering features during development and continuous integration.
- To experience rich, classic MilkDrop-style visuals, install a full projectM preset pack.

Where to get great presets
- Official projectM preset pack (curated, safe):
  - https://github.com/projectM-visualizer/presets-pack (recommended)
  - https://github.com/projectM-visualizer/projectm-preset-pack
- Community collections (use discretion):
  - Search GitHub for "projectM presets" or "MilkDrop presets".

Where to place presets so MilkDAWp can find them
MilkDAWp searches several locations automatically at runtime:
1) Inside the plugin bundle (deployed by CLion target deploy_vst3_user):
   - <UserVST3Dir>/MilkDAWp.vst3/Contents/Resources/presets
2) In the repository/dev tree (when running from IDE):
   - resources/presets
3) vcpkg-style shared data directory near the executable:
   - <...>/share/projectM/presets
4) User profile on Windows:
   - %APPDATA%/projectM/presets  (e.g., C:\Users\<You>\AppData\Roaming\projectM\presets)
5) System-wide on Windows:
   - C:\Program Files\projectM\presets

Install steps (Windows example)
1) Download one of the preset packs linked above and extract it.
2) Copy the extracted preset folders/files (*.milk) to one of the searched locations above.
   - Recommended for per-user install: %APPDATA%/projectM/presets
   - Recommended for the packaged plugin: <UserVST3Dir>/MilkDAWp.vst3/Contents/Resources/presets
3) Relaunch your host or reload the plugin. MilkDAWp will log the resolved preset directory and how many presets it found.

Tips
- If you only see simple line/circle drawings, you are likely still using the test set. Once a full pack is installed, visuals should be vibrant and reactive.
- In Debug builds, watch the "PM" log channel; it reports the preset directory detected and the number of presets.
- You can switch presets from the host if it exposes the preset index parameter, or let projectM auto-cycle.

Troubleshooting
- If zero or only a handful of presets are detected, ensure the path you chose matches one of the search locations above and that files end with .milk.
- Some packs contain nested folders. Keeping the folder hierarchy is fine; the scanner searches recursively.
