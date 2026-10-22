# Third-party code and game data

The extension and Doom engine are distributed under GPL-2.0-or-later. See LICENSE. The game
data in `data/doom1.wad` has its own license, described below.

## DoomGeneric

Source: https://github.com/ozkl/doomgeneric

Pinned commit: `e257a09527062bfb986b6e5e0780839b6cbd5b71`.

The C source and headers are vendored in `vendor/doomgeneric`. The port removes the unused
SDL_mixer include in `i_sound.c`, because the extension supplies portable sound and music modules.
`i_system.c` delegates the game's Quit action to the extension, closing its tab while leaving
the extension ready for another Play command. Settings are saved when the extension shuts down.
Original copyright and license notices are preserved in each file.

## rhun C SDK

Source: https://github.com/rhun-editor/extension-protocol

Pinned commit: `396830ce2b5295416f59d58a0599bde3aa663ba6`.

`vendor/rhun/rhunext.h` is MIT licensed, as noted in its header. This copy adds an optional
`RX_IDLE(ext)` hook before a blocking read, allowing the game loop to run while the editor is
idle. All messages, requests, image uploads, and picture drawing still use the SDK.
It also adds `rx_unimage_async`, ordered image cleanup that avoids a third blocking round trip
per frame. Its acknowledgment is consumed by the next ordinary SDK request.

## Doom shareware data

`data/doom1.wad` is the unmodified Doom 1.9 shareware IWAD from Debian. It is included in the
repository alongside `data/SHAREWARE-LICENSE.txt`, Debian's complete copyright file containing
the original limited-use license and John Carmack's redistribution clarification. Keep that
notice with every copy of the WAD. `data/README.md` records the source and SHA-256 checksum.

The IWAD contains the original first episode's graphics, maps, sound effects and music.
It is copyrighted by id Software and is separate from the GPL engine code. Debian reproduces
the limited-use license and distribution clarification at:

https://sources.debian.org/src/doom-wad-shareware/1.9.fixed-4/debian/copyright/

The shareware WAD must remain unmodified and cannot be sold or charged for without id Software's
prior written consent. It is not GPL licensed. This repository distributes it free of charge.
No registered Doom or Doom II assets are included or downloaded.

## Portable audio and music

`vendor/miniaudio` is from https://github.com/mackron/miniaudio at commit
`9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`. It is used under its MIT No Attribution option;
the complete upstream license is retained in that folder and included in binary packages.

`vendor/tinysoundfont` is from https://github.com/schellingb/TinySoundFont at commit
`853a0a171759f1ddba0de1442133a75912bbeffa`, under MIT. Its license is retained and packaged.
The Doom MUS score drives this synthesizer on all three platforms.

`data/TimGM6mb.sf2` is the TimGM6mb 1.3 General MIDI instrument bank from Debian's
`timgm6mb-soundfont_1.3.orig.tar.gz`. It is GPL-2.0 licensed, copyrighted by Tim Brechbill
(2004) and David Bolton (2010). The complete notice is in `data/SOUNDFONT-LICENSE.txt` and
the GPL text is in `LICENSE`. The SoundFont itself is its editable source and is included
unchanged in both the source repository and binary packages.

SoundFont SHA-256: `c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854`.
