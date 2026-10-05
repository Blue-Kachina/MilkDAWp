# MilkDAWp

A music visualizer for musicians, streamers and VJs: MilkDrop presets,
rendered by [projectM](https://github.com/projectM-visualizer/projectm), that
change in time with your music.

- **A plugin** for your DAW: VST3 on Windows, macOS and Linux, and Audio Unit
  on macOS. It passes audio through untouched, follows the DAW's tempo and
  bars, and every setting can be automated.
- **A standalone app** that listens to any audio input, or on Windows to
  whatever your computer is playing.
- **About 9,800 presets included** ("Cream of the Crop", curated by Jason
  Fletcher), with a searchable browser, favourites, star ratings and tags.
- **Transitions on the beat**: every N bars, on the drop, on a timer, or by
  hand, with blends timed so their middle lands on the downbeat.
- **An Output window** for a second screen, a projector or OBS, and plugin
  instances that can be layered into one picture.

## Download

From the [releases page](https://github.com/Blue-Kachina/MilkDAWp/releases):
an installer for Windows (10 21H2+), a `.pkg` for macOS (12+, Apple Silicon and
Intel), and an AppImage, VST3 tarball or `.deb` for Linux (glibc 2.35+, e.g.
Ubuntu 22.04+). See [Installing](docs/user-guide/installing.md).

## Documentation

The [user guide](docs/user-guide/README.md) covers installing, the controls
and shortcuts, presets, transitions, the plugin in a DAW, the app, capturing
with OBS, troubleshooting and licences.

Questions, bugs and ideas: [issues](https://github.com/Blue-Kachina/MilkDAWp/issues/new/choose).

## Building from source

CMake presets, vcpkg (for projectM) and JUCE 9; see
[CONTRIBUTING.md](CONTRIBUTING.md). Releases are built by
`.github/workflows/release.yml` ([docs/releasing.md](docs/releasing.md)), and
the plan and design decisions are in [development_roadmap.md](development_roadmap.md).

## Licence

MilkDAWp is free software under the GNU Affero General Public License v3.0 or
later ([LICENSE](LICENSE)). projectM is LGPL-2.1 and ships as a separate,
replaceable library; JUCE is used under the AGPL-3.0. The bundled presets keep
their authors' copyright. Details: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the [licence FAQ](docs/user-guide/faq.md).