# Submodules — Phase 1

Purpose: Pin JUCE and projectM to known-good versions via Git submodules under extern/.
This keeps dependency versions reproducible across machines and in CI, and sets up for Phase 2 where we will integrate them via CMake from the local workspace.

Contents
- What gets added
- One-time setup (existing clones)
- Fresh clone workflow
- Pinning to specific tags/commits
- Updating to newer versions
- Syncing URLs/paths
- Troubleshooting

What gets added
- extern/JUCE → https://github.com/juce-framework/JUCE.git (target tag: 8.0.10)
- extern/projectm → https://github.com/projectM-visualizer/projectm.git (target tag: 4.1.4)

Notes
- In Phase 1 we do NOT change CMake. The current build still uses FetchContent unless you opt-in to a local path.
- In Phase 2 we will switch to building from these submodule sources.

One-time setup for existing clones
If you already have a working tree of this repo:

1) Initialize and fetch submodules:
   - git submodule init
   - git submodule update --depth 1

2) Optionally check out the intended tags explicitly (detached HEAD) to pin to known-good versions:
   - git -C extern/JUCE fetch --tags
   - git -C extern/JUCE checkout tags/8.0.10
   - git -C extern/projectm fetch --tags
   - git -C extern/projectm checkout tags/v4.1.4

3) Commit the gitlinks to record the exact revisions (maintainers only):
   - git add extern/JUCE extern/projectm
   - git commit -m "Pin submodules: JUCE 8.0.10, projectM 4.1.4"

Fresh clone workflow
- git clone <this-repo>
- cd MilkDAWp_Take3
- git submodule update --init --depth 1

Pinning to specific tags/commits (maintainers)
To ensure everyone gets the same revisions, maintainers should check out exact tags/commits inside the submodule paths and commit the resulting gitlinks in the parent repo.

Example:
- git -C extern/JUCE fetch --tags
- git -C extern/JUCE checkout tags/8.0.10
- git -C extern/projectm fetch --tags
- git -C extern/projectm checkout tags/v4.1.4
- git add extern/JUCE extern/projectm
- git commit -m "Pin submodules: JUCE 8.0.10, projectM 4.1.4"

Updating to newer versions
- Choose a new tag for each dependency.
- git -C extern/JUCE fetch --tags && git -C extern/JUCE checkout tags/<new-juce-tag>
- git -C extern/projectm fetch --tags && git -C extern/projectm checkout tags/<new-projectm-tag>
- git add extern/JUCE extern/projectm && git commit -m "Update submodules"

Syncing .gitmodules changes
If the URL or path changes in .gitmodules, run:
- git submodule sync --recursive
- git submodule update --init --recursive

Optional: CMake local-path overrides (Phase 1 only)
You can point the build to these local submodules today without changing CMake by configuring:
- -DJUCE_LOCAL_PATH="${CMAKE_SOURCE_DIR}/extern/JUCE"
- -DPROJECTM_LOCAL_PATH="${CMAKE_SOURCE_DIR}/extern/projectm"

Troubleshooting
- Submodule path has local modifications → stash/commit inside the submodule directory first.
- Submodule not checked out → ensure you ran `git submodule update --init` from repo root.
- CI using shallow clones → add: `git submodule update --init --depth 1 --recursive`.
- You see "Initializing submodules" but nothing appears in extern/: This repo currently tracks .gitmodules, but the submodule gitlinks may not be committed in your clone/branch. In that case, `git submodule update --init` is a no‑op. The bootstrap scripts will fall back to shallow‑cloning JUCE and projectM into extern/ so local builds work. To convert those into true pinned submodules later, do:
  - rm -rf extern/JUCE extern/projectm
  - git submodule add https://github.com/juce-framework/JUCE extern/JUCE
  - git submodule add https://github.com/projectM-visualizer/projectm extern/projectm
  - (optionally) check out specific tags inside each, then `git add extern/JUCE extern/projectm && git commit` to record gitlinks.
