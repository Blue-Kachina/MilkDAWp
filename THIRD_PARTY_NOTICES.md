# Third-Party Notices

This document lists third-party components included with or required by MilkDAWp, along with their licenses and source locations. These notices should accompany all binary distributions.

Components:

1) JUCE
- License: AGPLv3 (GNU Affero General Public License v3.0)
- Upstream: https://github.com/juce-framework/JUCE
- Source in this distribution: via submodule (extern/JUCE) or fetched during build
- Full license text: LICENSES/JUCE-AGPL-3.0.txt

2) projectM (libprojectM)
- License: LGPL-2.1-or-later (GNU Lesser General Public License v2.1)
- Upstream: https://github.com/projectM-visualizer/projectm
- Source in this distribution: via submodule (extern/projectm) or fetched during build
- Linkage: dynamically linked at runtime (Phase 2+)
- Full license text: LICENSES/projectM-LGPL-2.1.txt

Notes on compliance:
- MilkDAWp itself is licensed under AGPL-3.0-or-later. When distributing binaries, publish the complete corresponding source code of MilkDAWp.
- For libprojectM (LGPL-2.1), we dynamically link to satisfy the license.

Where to find these notices at runtime:
- The plugin provides an About → Licenses entry that will open this file or a documentation page with the same contents.

For questions or to obtain the complete corresponding source, refer to the project repository README.md.
