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
