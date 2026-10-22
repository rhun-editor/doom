# DOOM in rhun

The original Doom engine, running in a rhun editor tab. Native C, original 320×200 pixels,
keyboard controls, sound effects, and music. No web view and no separate game window.

This is a local macOS Apple silicon prototype. It has not been published to the extension index.

![DOOM running in a rhun tab](docs/doom-in-rhun.png)

## Play locally

Building needs the macOS command line developer tools (`clang` and `make`).

Double-click **Play DOOM.command**, or run:

```sh
make
DOOM_RHUN=/path/to/rhun sh tools/play-local.sh
```

The launcher builds the extension, creates a separate development install and separate rhun
config/state folders under `build/local`, and opens the game. Your normal editor settings and
installed extensions are untouched. Click the game to play.

Full controls and sound currently need the accompanying experimental rhun patch:

- `[needs] audio = yes` allows speaker output on macOS, without microphone access.
- The optional `view-key-state` feature supplies key releases, modifier changes and window focus.

`patches/rhun-local-media.patch` applies to rhun commit
`2a155fd417239e6baff8f714996472f7f96d4c01` (0.18.1). Apply it in a separate checkout and build rhun
there, then point `DOOM_RHUN` at that build. The local `.local-rhun` file may also hold that path;
it is ignored by Git.

Unmodified 0.18.1 can display and run the game, with short key pulses as a fallback. Its macOS
sandbox blocks sound playback, and it does not provide held-key releases. Use the patched build
for the playable local demo.

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

The game pauses when its tab loses focus or the editor window becomes inactive. Held controls
are cleared on focus loss. You can close and reopen the tab without starting a new process.
The command palette also has **DOOM: New Game**, **DOOM: Pause or Resume**, and **DOOM: Toggle Sound**.

## Game data

The repository includes the unmodified Doom 1.9 shareware episode in `data/doom1.wad`, so no
game-data download is needed after cloning. That IWAD contains the original graphics, music,
sound effects and first episode. It remains copyrighted by id Software under its separate
[shareware license](data/SHAREWARE-LICENSE.txt), which also preserves John Carmack's redistribution
clarification. The GPL license for the engine does not apply to the game data.

See [data/README.md](data/README.md) for the source and checksum, and [THIRD_PARTY.md](THIRD_PARTY.md)
for upstream versions and licensing. The shareware data must stay unmodified and must not be sold
or charged for without id Software's permission. No registered Doom or Doom II assets are
included. Game saves and settings stay in the extension's rhun state folder.

If the bundled WAD is missing, `sh tools/fetch-shareware.sh` restores the same file from Debian
and verifies its SHA-256 checksum. The local launcher also uses this fallback automatically.

## Tests

```sh
make test
sh tests/check.sh
DOOM_RHUN=/path/to/patched/rhun python3 tests/run.py
```

The unit tests validate lossless PNG frames and buffer bounds. The checker validates the
manifest, framing, views, commands, input bursts, restart and shutdown. Its local patch adds the
experimental audio manifest key and bounds each observation window, since an animated extension
never becomes quiet. It also permits one draw already in flight when the checker closes a tab;
the extension handles the resulting `noview` reply by releasing the view. Further draws to that
closed view still fail. The checker deliberately does not negotiate the new key-state feature,
exercising the legacy fallback.

The real-editor tests verify movement, turning, ammunition use, audio mixing, time freezing
while paused, focus changes, closing/reopening, resizing, sustained frame delivery, missing
data, invalid data, and audio denial without the declared permission. Every test uses isolated
config and state directories. Screenshots and logs are in `build/test-results`.

The extension needs no document, project, home, network or terminal access. Keyboard events
come from the focused rhun picture view. It does not install an event tap, read global keyboard
state, or request Accessibility or Input Monitoring access.

## How it works

DoomGeneric supplies the original engine. The extension sends indexed PNG frames through
rhun's existing image/picture protocol, retaining only the current image. The C SDK owns all
protocol framing. Sound effects use AudioQueue; MUS music events drive macOS's built-in MIDI
synthesizer. This prototype has no third-party runtime dependency.

The engine and extension are GPL-2.0-or-later. The vendored rhun C SDK is MIT licensed. See
[LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
