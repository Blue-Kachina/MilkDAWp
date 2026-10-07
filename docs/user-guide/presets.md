# Presets

A preset is a MilkDrop `.milk` file: a small program that draws a visual. Your
**preset folder** is the folder MilkDAWp plays from, including its subfolders.
It starts on the bundled pack, "Cream of the Crop" (about 9,800 presets,
sorted into folders such as *Dancer*, *Fractal*, *Geometric*, *Waveform*).

## The preset browser

Click the preset name in the drawer, or press `B`.

- **Search** as you type. Every word must appear somewhere in the preset's
  name, its folder or its tags, in any order: `geiss wave` finds
  *Waveform/Wire Tangle/Geiss - 3D - Shockwaves*. Typing a folder name, like
  `fractal`, lists that folder.
- **Tabs:** *All*, *Favourites*, *Rated* (best first) and, in the app,
  *Recent* (recently played, newest first).
- **Click** a preset to play it. **Up/Down** move without playing, **Return**
  plays the selected one, **Esc** closes.
- **Folder...** chooses another preset folder; **Rescan** picks up presets you
  added or removed.

Each row shows a heart and five stars:

- **Favourite:** click the heart, or select the preset and press `F` (with the
  list focused: press `Tab` from the search box).
- **Rate** 1 to 5 stars: click a star, or press `1`-`5`; `0` or clicking the
  same star again clears it.
- **Right-click** for the rest: blacklist, never auto-select, and tags.

## Ratings, favourites and tags

These are yours, not the project's: they are saved once for your user and
shared by the app and every plugin instance, in every project. They follow a
preset by its file name, so moving a folder or having the same preset in two
packs keeps them.

- **Ratings** steer **Weighted** shuffle (Settings > Transition settings >
  Preset selection): an unrated preset has weight 1, and each star doubles it
  (1 star 0.25, 3 stars 1, 5 stars 4). A 5-star preset comes up 16 times as
  often as a 1-star one.
- **Never auto-select** keeps a preset out of automatic changes; you can still
  play it by hand.
- **Tags** are words you attach (`calm`, `red`, `for-the-drop`...). The
  Transitions panel's **Only tags** field limits automatic changes to presets
  with any of those tags. If no preset has them, MilkDAWp says so and ignores
  the filter rather than stopping.
- **Blacklist** keeps a preset from playing at all (MilkDAWp also blacklists
  presets projectM can't load).

## Your own presets

Any folder of `.milk` files works: choose it with **Folder...** in the
browser. Presets that use textures look for them in the bundled texture pack.

Where the bundled presets live, if you want to look at them:

| System | Folder |
|---|---|
| Windows | `C:\ProgramData\MilkDAWp\Presets` |
| macOS | `/Library/Application Support/MilkDAWp/Presets` |
| Linux (.deb) | `/usr/share/milkdawp/Presets` |
| Linux (tarball) | `~/.local/share/milkdawp/Presets` |
| Linux (AppImage) | inside the AppImage |

## Opening a preset file

In the app, double-click a `.milk` file (if you chose that when installing on
Windows), drop it or a folder on the window, or pass it on the command line.
Its folder becomes the preset folder, starting on that preset.

## `.milkdawp` presets

A `.milkdawp` file is a `.milk` preset with extra lines that give it
**controls**: it says what each Macro does (Macro 1 might be *Swirl*, Macro 2
*Blob colour*), and the Visual panel shows those names. Automate a Macro and the
preset itself changes, not just the finished picture. With every Macro on the
preset's starting value it looks exactly like the `.milk` it was made from.

- They play anywhere `.milk` files do: in the preset folder, the browser, and
  by opening or dropping one on the app.
- A `.milk` with a `.milkdawp` of the same name beside it is left out of the
  list, since the `.milkdawp` plays the same preset. The `.milk` stays on disk
  as the original. Ratings, favourites and tags carry over between the two.
- Other MilkDrop programs (projectM, MilkDrop itself) open a `.milkdawp` as the
  plain preset if you rename it to `.milk`; they ignore the controls. MilkDrop's
  preset editor drops the controls if you save the preset from it.
- If a control in a file is broken, that control is skipped, the preset still
  plays, and Diagnostics' recent errors say what was wrong.

### The bundled presets come with Macros

Almost every preset that ships with MilkDAWp comes as a `.milkdawp` beside its
original `.milk`, with up to 8 Macros chosen for it automatically. Each Macro
starts in the middle, where the preset looks exactly as its author made it;
turn it either way from there. A Macro the preset doesn't use is dimmed.

| Macro | What it does |
|---|---|
| **q1**, **q2**, ... | A value the preset's own code works out every frame and hands to its shaders or its warp, often from the music. Left of centre weakens it, right strengthens it. What it changes depends on the preset: try it. |
| **Wave** | How strongly the main waveform is drawn |
| **Outer Border**, **Inner Border** | How strongly the borders are drawn (they smear into trails through the feedback) |
| **Motion Vectors** | How strongly the motion-vector grid is drawn |
| **Echo** | How strongly the video echo (a zoomed copy of the picture) is mixed in |
| **Drift X**, **Drift Y** | Slides the picture sideways or up and down, every frame, so it streams through the feedback |
| **Centre X**, **Centre Y** | Moves the point the preset zooms and rotates around |
| **Squash** | Stretches the picture one way and squeezes it the other, every frame |

The ones that matter most to a preset come first: values its code drives from
the music, then what its code animates, then what it shows, then the motion
every preset has.

The few presets that don't come with Macros are ones whose `.milkdawp` didn't
render like the original when it was checked, or that projectM can't
play at all. They're still there as plain `.milk`.
