# DOOM in rhun

The original Doom engine in a rhun editor tab. Native C, original 320×200 pixels, keyboard
controls, sound effects and music. Builds for macOS Apple silicon, Linux x86-64 and Windows x64.

![DOOM running in a rhun tab](docs/doom-in-rhun.png)

This is a preview. Smooth held controls and audio need the experimental editor support shipped
with the separate preview downloads. It has not been submitted to the extension index.

## Play without compiling

Open this repository's [Releases](https://github.com/rhun-editor/doom/releases).

Download and extract the entire **rhun-doom-preview** package for your system:

| System | Download | Start |
| --- | --- | --- |
| macOS Apple silicon | `rhun-doom-preview-macos-arm64.zip` | `Play DOOM.command` |
| Linux x86-64 | `rhun-doom-preview-linux-x86_64.tar.gz` | `sh "Play DOOM.sh"` |
| Windows x64 | `rhun-doom-preview-windows-x64.zip` | `Play DOOM.cmd` |

These packages include a separate rhun preview, the installed extension, shareware game data and
music instruments. No compiler, Python, JavaScript runtime or additional download is required.
The preview leaves your installed rhun and its settings alone. Its configuration and game state
stay inside the extracted folder. Click the game to play.

The Mac preview needs macOS 12 or newer and is not notarized. Linux needs glibc 2.35 or newer and a working PulseAudio
(or PipeWire's PulseAudio service) or ALSA installation. Windows needs 64-bit Windows 10 or newer.
The Windows binary includes its compiler runtime and zlib, so MSYS2 is not needed to play.

## Install only the extension

If you already have a rhun build with the companion audio and paired-key support, download
`doom-TARGET.tar.gz`. In rhun, open **Extensions → Install → From an Archive** and select it.
The archive includes the native binary, all game data and licenses. `sha256sums.txt` in the
release covers every download. Keep it beside the archive to let rhun verify it at installation.

For this preview, **Install from GitHub → rhun-editor/doom@0.1.0** also works. Include the
version: GitHub's default latest-release link excludes preview releases.

Stock rhun 0.18.1 can render the game and uses short key pulses as a fallback. Full held controls
and audio require the companion support in [rhun PR #107](https://github.com/vshvedov/rhun/pull/107).
The source patch is `patches/rhun-local-media.patch`, against rhun commit
`2a155fd417239e6baff8f714996472f7f96d4c01`.

## Controls

| Key | Action |
| --- | --- |
| W / S | Forward / backward |
| A / D | Strafe left / right |
| Left / Right | Turn |
| Space | Fire |
| E | Open doors / use |
| Shift | Run |
| 1 to 7 | Select weapon |
| Escape / Enter | Game menu / confirm |
| P | Pause or resume |
| M | Mute or unmute |

The game pauses when its tab loses focus or the editor becomes inactive. Held controls clear
on focus loss. Close and reopen the tab to continue. The command palette also has **DOOM: New
Game**, **DOOM: Pause or Resume**, and **DOOM: Toggle Sound**.

## Build locally

macOS needs the command line developer tools. Linux needs a C11 compiler and make.
Windows builds use MSYS2's MINGW64 environment with GCC and make. Dependencies are
vendored; no library downloads occur during compilation.

```sh
make -j4
DOOM_RHUN=/path/to/patched/rhun sh tools/play-local.sh
```

The launcher builds the extension and creates a separate development install under `build/local`.
The ignored `.local-rhun` file can hold the editor path. The optional shareware fetch script
restores a missing WAD from Debian and verifies its checksum; clones already contain the WAD.

## Tests and automated releases

`.github/workflows/release.yml` builds on native macOS, Ubuntu and Windows runners. It runs PNG
unit tests, protocol conformance, real-editor gameplay tests, archive installation, picture-view
regressions and extracted-preview launcher tests. Linux and Windows also exercise their native
window key handlers. Preview extraction tests include spaces and non-ASCII paths. Windows runs
the extracted preview without MSYS2 or LLVM on PATH to catch missing runtime DLLs.

Linux CI uses a PulseAudio test sink. Hosted Mac and Windows runners use an explicitly selected
test audio device: they verify real-time effects and music synthesis, without claiming physical
speaker playback. Local Mac testing also uses Core Audio and checks denial without the audio
permission. Screenshots, logs and gameplay measurements are uploaded as workflow artifacts.

```sh
make test
python3 tools/package.py macos-arm64
DOOM_PACKAGE_ROOT="$PWD/build/package/macos-arm64/doom" python3 tools/check.py
DOOM_PACKAGE_ROOT="$PWD/build/package/macos-arm64/doom" DOOM_RHUN=/path/to/rhun python3 tests/run.py
```

A `vVERSION` tag equal to `extension.ini` builds and tests all three targets. Only after every
platform passes does the workflow publish a preview release with extension archives, standalone
preview downloads, SHA-256 checksums and complete corresponding source archives. Public releases
also receive GitHub build-provenance attestations for the extension archives.

## Game data and licenses

The repository and every binary download include the unchanged Doom 1.9 shareware IWAD. It
contains the first episode's graphics, maps, sounds and score. It remains copyrighted by id
Software under its [separate license and redistribution notice](data/SHAREWARE-LICENSE.txt).
Keep it unmodified and do not charge for it without id Software's permission. No registered Doom
or Doom II assets are included.

The portable music synthesizer uses the GPL-2.0 TimGM6mb General MIDI instrument bank, included
with its own notice. [data/README.md](data/README.md) records asset origins and checksums.

The extension needs no document, project, home, network or terminal access. Keyboard events come
from the focused picture view. Audio access is platform specific: Linux sound services can also
expose recording, and rhun currently has no Windows extension sandbox. This extension opens
playback streams only. It does not read global keyboard state or request Accessibility access.

## How it works

DoomGeneric supplies the engine. Indexed PNG frames travel through rhun's image/picture protocol.
The C SDK owns all protocol framing. Miniaudio supplies native playback and TinySoundFont renders
the original MUS score with the bundled instruments on all three platforms.

The engine and extension are GPL-2.0-or-later. Source for the exact release is provided in
`doom-source.tar.gz`; the preview editor's patched source is in `rhun-preview-source.tar.gz`.
The vendored libraries have their own notices. See [LICENSE](LICENSE) and
[THIRD_PARTY.md](THIRD_PARTY.md).
