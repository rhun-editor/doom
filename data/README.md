# Doom shareware game data

`doom1.wad` is the unmodified Doom 1.9 shareware IWAD. It contains the first episode's maps,
graphics, sound effects and music, copyrighted by id Software. Its license is separate from
the GPL license covering this repository's engine and extension code.

Keep [SHAREWARE-LICENSE.txt](SHAREWARE-LICENSE.txt) with the WAD when redistributing it. That
file preserves Debian's copyright notice, the original limited-use license, and John Carmack's
1999 clarification that the shareware WAD is freely distributable. The data must remain
unmodified. Do not sell or charge for its receipt or use without id Software's written consent.

Source archive:

https://deb.debian.org/debian/pool/non-free/d/doom-wad-shareware/doom-wad-shareware_1.9.fixed.orig.tar.gz

Archive member: `doom-wad-shareware-1.9.fixed/doom1.wad`

Size: 4,196,020 bytes

SHA-256:

```text
1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
```

License and redistribution clarification source:

https://sources.debian.org/src/doom-wad-shareware/1.9.fixed-4/debian/copyright/

`sh tools/fetch-shareware.sh` restores the same WAD from the source archive and checks this
hash. No registered Doom or Doom II data is included.

## General MIDI instruments

`TimGM6mb.sf2` supplies instrument samples for the original Doom MUS score on every platform.
It is TimGM6mb 1.3 from Debian, licensed under GPL-2.0. Its editable SoundFont file is included
unchanged with `SOUNDFONT-LICENSE.txt`; the full GPL text is in the repository's `LICENSE`.

Source: https://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/timgm6mb-soundfont_1.3.orig.tar.gz

SHA-256: `c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854`.
