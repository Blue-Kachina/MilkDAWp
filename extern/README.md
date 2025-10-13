This directory hosts Git submodules for dependency pinning (Phase 1).

Submodules:
- JUCE → https://github.com/juce-framework/JUCE.git (intended tag: 8.0.10)
- projectm → https://github.com/projectM-visualizer/projectm.git (intended tag: 4.1.4)

These are declared in the parent repo's .gitmodules. Initialize with:
- git submodule update --init --depth 1

Maintainers: after checking out desired tags inside each submodule, commit the gitlinks in the parent repository to pin exact revisions.
