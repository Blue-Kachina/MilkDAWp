# Licences and FAQ

## Is MilkDAWp free?

Yes. MilkDAWp is free software under the **GNU Affero General Public License,
version 3 or later** (AGPL-3.0-or-later). You may use it for anything,
including paid gigs, streams and commercial music videos; study and change it;
and share it, changed or not, as long as you share the source of what you
share under the same licence.

## Can I use videos I make with it?

Yes. The licence covers MilkDAWp's code, not what you make with it. The
pictures come from the presets: see the next question.

## Who made the presets?

The bundled presets, "Cream of the CrAWp", are the "Cream of the Crop" pack:
about 9,700 MilkDrop presets curated and sorted by **Jason Fletcher
(ISOSCELES)**, packaged by the projectM project with the MilkDrop texture pack.
Each preset's author keeps its copyright. MilkDrop presets were, almost without
exception, released freely and without a formal licence, and have been shared
in visualizers for more than twenty years. MilkDAWp ships each one as a
`.milkdawp`: the preset exactly as its author wrote it, plus lines that give it
Macros. The pack's `LICENSE.md` ships unchanged beside them.
If you wrote a preset and want it removed, open an issue and it will be.

## What is projectM, and why is it a separate file?

[projectM](https://github.com/projectM-visualizer/projectm) is the open-source
reimplementation of MilkDrop that renders the presets. Its licence is the
**LGPL-2.1**, which lets MilkDAWp use it as long as it stays a separate
library you can replace. That's why `projectM-4.dll` / `libprojectM-4.so` /
`libprojectM-4.dylib` sits next to MilkDAWp: you may swap in your own build of
projectM 4.2 or newer. Its source is linked from `THIRD_PARTY_NOTICES.md`.

## What else is inside?

[JUCE](https://juce.com) (the framework MilkDAWp is built with, used under the
AGPL-3.0) and [Lucide](https://lucide.dev) icons (ISC). Every licence text
ships with MilkDAWp (`LICENSE`, `LICENSES/`, `THIRD_PARTY_NOTICES.md`) and
**Help > About** lists them.

## Does MilkDAWp collect data or go online?

No. The only network access is the optional update check in the app, off
until you turn it on (Help > About): once a day it asks GitHub for the list of
releases. Nothing about you or your music is sent. The plugin never goes
online.

## Why isn't it signed?

Code-signing certificates and Apple's developer programme cost money this
project doesn't have. Windows signing through the free SignPath Foundation
programme is being applied for. Every release has checksums and GitHub build
attestations so you can check a download came from this repository's build.

## Where's the source?

[github.com/Blue-Kachina/MilkDAWp](https://github.com/Blue-Kachina/MilkDAWp)
(the Source code link in About).
