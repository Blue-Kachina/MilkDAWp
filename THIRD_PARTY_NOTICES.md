# Third-Party Notices

This document lists third-party components included with or required by MilkDAWp, along with their licenses and source locations. These notices should accompany all binary distributions.

Components:

1) JUCE
- License: AGPLv3 (GNU Affero General Public License v3.0)
- Version: 8.0.7
- Upstream: https://github.com/juce-framework/JUCE
- Source in this distribution: managed via vcpkg (see vcpkg.json)
- Full license text: LICENSES/JUCE-AGPL-3.0.txt

2) projectM (libprojectM)
- License: LGPL-2.1-or-later (GNU Lesser General Public License v2.1)
- Version: 4.1.4
- Upstream: https://github.com/projectM-visualizer/projectm
- Source in this distribution: managed via vcpkg (see vcpkg.json)
- Linkage: dynamically linked (shared library) for LGPL compliance
- Full license text: LICENSES/projectM-LGPL-2.1.txt

Notes on compliance:
- MilkDAWp itself is licensed under AGPL-3.0-or-later. When distributing binaries, publish the complete corresponding source code of MilkDAWp.
- For libprojectM (LGPL-2.1), we use dynamic linking to satisfy the license. The shared library is bundled with the plugin.

Where to find these notices at runtime:
- The plugin provides an About → Licenses entry that will open this file or a documentation page with the same contents.

For questions or to obtain the complete corresponding source, refer to the project repository README.md.
