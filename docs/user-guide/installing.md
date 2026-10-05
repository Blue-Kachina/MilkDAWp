# Installing

Download the file for your system from the
[releases page](https://github.com/Blue-Kachina/MilkDAWp2/releases). Each
release lists a `SHA256SUMS.txt`; to check a download, compare its SHA-256 with
the one listed (or run `gh attestation verify <file> --repo Blue-Kachina/MilkDAWp2`,
which proves GitHub built it from this repository).

## Windows (10 21H2 or newer, 64-bit)

Run `MilkDAWp-<version>-windows-x64-setup.exe`. It installs:

| What | Where |
|---|---|
| VST3 plugin | `C:\Program Files\Common Files\VST3\MilkDAWp.vst3` |
| App | `C:\Program Files\MilkDAWp\MilkDAWp.exe`, in the Start menu |
| Presets | `C:\ProgramData\MilkDAWp\Presets` and `Textures` |
| Microsoft Visual C++ runtime | only if it's missing or too old |

You can leave out the presets ("App and plugin" type), add a desktop
shortcut, and choose whether `.milk` files open in MilkDAWp.

The installer isn't code-signed yet, so Windows SmartScreen may say it
"protected your PC": choose **More info**, then **Run anyway**.

Uninstall from **Settings > Apps**. Your settings, ratings and favourites in
`%APPDATA%\MilkDAWp` are kept.

## macOS (12 Monterey or newer, Apple Silicon or Intel)

Open `MilkDAWp-<version>-macos-universal.pkg`. Because MilkDAWp has no paid
Apple developer account, macOS first says it "can't be opened": **Control-click
(or right-click) the .pkg, choose Open, then Open again**. On recent macOS you
may instead need **System Settings > Privacy & Security > Open Anyway**. The
installer then puts:

| What | Where |
|---|---|
| App | `/Applications/MilkDAWp.app` |
| VST3 plugin | `/Library/Audio/Plug-Ins/VST3/MilkDAWp.vst3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components/MilkDAWp.component` |
| Presets | `/Library/Application Support/MilkDAWp` |

"Customize" in the installer lets you skip any of them. To uninstall, delete
those four items.

The first time the app listens to a microphone or audio interface, macOS asks
for permission; allow it, or MilkDAWp hears silence.

## Linux (Ubuntu 22.04 or newer, or any x86_64 distribution with glibc 2.35+)

Pick one:

- **The app, anywhere:** `MilkDAWp-<version>-x86_64.AppImage`. Make it
  executable and run it (`chmod +x MilkDAWp-*.AppImage && ./MilkDAWp-*.AppImage`).
  The presets are inside it. No FUSE library is needed.
- **The plugin, for your user:** unpack `MilkDAWp-<version>-linux-x64-vst3.tar.gz`
  and run `./install-vst3.sh`. It copies the plugin to `~/.vst3` and the
  presets to `~/.local/share/milkdawp`.
- **Both, system-wide, on Debian/Ubuntu:**
  `sudo apt install ./milkdawp_<version>_amd64.deb`. The app appears in your
  applications menu, the plugin in `/usr/lib/vst3`. Remove with
  `sudo apt remove milkdawp`.

You need OpenGL 3.3 (any current Mesa or vendor driver), ALSA or JACK, and an
X11 or XWayland session.

## The first run

The app and every new plugin instance open on the bundled presets, starting
with *Geiss - Many Colors 1*, which looks good even before any sound arrives.
Play something and the picture starts to move with it; presets change on the
beat every four bars (see [Transitions](transitions.md)).

To use your own presets instead, open the preset browser (click the preset
name, or press `B`) and choose **Folder...**.
