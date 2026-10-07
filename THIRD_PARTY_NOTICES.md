# Third-party notices

MilkDAWp 2 is licensed under AGPL-3.0-or-later (see `LICENSE` / D10). It links
against the following third-party components. Full licence texts for
dependencies actually bundled in a release installer are regenerated at
build/packaging time (Phase 6.2-6.4, §9) from each vcpkg port's copyright
file; this document is the human-curated index of what's used and why.

| Component                                                     | License                     | Linkage                                                                                                | Source                                                                                                                                                                                               |
|---------------------------------------------------------------|-----------------------------|--------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [JUCE 9](https://github.com/juce-framework/JUCE)              | AGPL-3.0 (open-source path) | Statically compiled in via `FetchContent` (`cmake/FetchJuce.cmake`)                                    | Pinned tag + commit hash                                                                                                                                                                             |
| [projectM 4](https://github.com/projectM-visualizer/projectm) | LGPL-2.1                    | **Dynamically** linked (required by LGPL; see `triplets/*.cmake` and `cmake/ProjectMDependency.cmake`) | vcpkg overlay port `vcpkg-overlays/projectm`, pre-release 4.2.0 built from upstream commit `1e7ef7803b69024d1e0656705670adda2ffac817` (ADR-0008), **modified** by MilkDAWp's patches in `vcpkg-overlays/projectm/*.patch` (ADR-0012). The LGPL source offer refers to that exact commit plus those patches. |
| [Lucide](https://lucide.dev) icons                            | ISC                         | Icon path data compiled in (`ui/src/Icons.cpp`, which carries the licence notice)                      | Copied from lucide.dev, redrawn as absolute SVG path strings                                                                                                                                         |
| [Catch2 v3](https://github.com/catchorg/Catch2)               | BSL-1.0                     | Test-only, not shipped in any release binary                                                           | `FetchContent` (`cmake/FetchCatch2.cmake`)                                                                                                                                                           |
| zlib                                                          | zlib                        | JUCE's bundled copy, compiled in (§4.11; vcpkg's copy is not linked since 2026-09-26)                  | JUCE                                                                                                                                                                                                 |
| libpng                                                        | libpng-2.0                  | JUCE's bundled copy, compiled in (§4.11; vcpkg's copy is not linked since 2026-09-26)                  | JUCE                                                                                                                                                                                                 |
| projectM's transitive dependencies                            | various (see each port)     | None ship: the 4.2 pin needs only system libraries and the C/C++ runtime (`ProjectMDependency.cmake`)  | vcpkg                                                                                                                                                                                                |
| ["Cream of the Crop" presets](https://github.com/projectM-visualizer/presets-cream-of-the-crop) | none formal (see below) | Data files, shipped as "Cream of the CrAWp": each preset as a `.milkdawp`, its original text kept byte for byte with MilkDAWp control lines added; the 67 projectM can't load left out; `LICENSE.md` and `README.md` unchanged (6.1, D12, ADR-0014) | Pinned commit and SHA-256 in `cmake/BundledContent.cmake` |
| [MilkDrop texture pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack) | none stated (see below) | Image files, shipped unchanged with their `README.md` (6.1, D12) | Pinned commit and SHA-256 in `cmake/BundledContent.cmake` |
| Microsoft Visual C++ 2015-2022 Redistributable (x64) | Microsoft's redistributable licence (Visual Studio) | Windows installer only: Microsoft's own `vc_redist.x64.exe`, unmodified, run when the runtime is missing or too old (6.2) | `https://aka.ms/vs/17/release/vc_redist.x64.exe` at packaging time |

## Bundled presets and textures

MilkDAWp ships projectM's "Cream of the Crop" preset pack (9,795 MilkDrop
presets, curated and sorted by Jason Fletcher / ISOSCELES) and the MilkDrop
texture pack its presets reference, the same content projectM's own releases
ship (D12). The pack's `LICENSE.md` says the presets were, in almost all
cases, released without a specific licence: each author keeps the copyright,
and after two decades of free redistribution they are treated as public
domain. The texture pack's README states no licence; it holds the textures
originally released with MilkDrop plus others common in presets.

We ship the textures unchanged. The presets ship as "Cream of the CrAWp"
(8.11, ADR-0014): each one as a `.milkdawp`, which is the original preset's
text byte for byte plus lines that give it MilkDAWp controls (taking those
lines out gives the original back); the 67 that projectM can't load are left
out. Both ship with their own notices, and we credit the curator in the
release README (and in About, 6.7). If an author asks for a preset or texture
to be removed, we remove it and follow any removal upstream makes.

## Why projectM must be dynamically linked

projectM is LGPL-2.1. Statically linking an LGPL library into a
non-(L)GPL-compatible-licensed binary would extend LGPL obligations to the
whole binary; MilkDAWp avoids that by linking it as a shared library on every
platform (enforced by the `*-dynamic` triplets and checked at configure time).
The compiled shared library, its licence, and a written offer for its source
ship alongside every installer (Phase 6.2-6.4). Since ADR-0012 we ship a
modified projectM, so pointing at the upstream project is no longer enough:
the offer must include our patches (LGPL-2.1 §2 also requires the modified
files to carry a notice of the change, which each patched file carries at its top).

## Generating a release's full notices

Phase 6 packaging generates a complete, per-release third-party notices file
from the actual dependency set resolved by vcpkg for that build (license
files live under `vcpkg_installed/<triplet>/share/*/copyright`). This file is
the curated summary for contributors and reviewers, not the shipped legal
document.
