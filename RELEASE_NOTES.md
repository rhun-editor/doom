DOOM runs in a native rhun editor tab, with the original shareware episode, sound effects and music.

Install [rhun 0.18.2 or newer](https://github.com/vshvedov/rhun/releases/latest), open
**Extensions → Install from GitHub…**, and enter **rhun-editor/doom**. Approve audio access,
then run **DOOM: Play** from the command palette. No separate editor preview is needed.
To select this version explicitly, enter `rhun-editor/doom@0.1.0`.

The extension archives include their native binary, shareware game data, music instruments and
licenses. You can also install `doom-TARGET.tar.gz` with **From an Archive**. Keep
`sha256sums.txt` beside it for verification.

Click the game. W/S move, A/D strafe, arrows turn, Space fires, E opens doors, P pauses, and M
mutes. It pauses when focus leaves the game. Supports Mac Apple silicon, Linux x86-64 and
Windows x64. All three platforms passed the automated release checks; Mac playback was also
checked locally with Core Audio. Hosted audio tests use test devices, not physical speakers.

The original standalone preview packages remain available below for trying a separate editor.
Extract the whole folder and run **Play DOOM**. The first Mac preview is not notarized; use the
regular rhun download for its signed, notarized editor.

The unchanged shareware WAD and its separate id Software license are included. It must remain
unmodified and cannot be charged for without permission. TimGM6mb instruments and their GPL
notice are included too. Complete source archives are provided alongside the binaries.

The standalone editor's [Unicode-data notice](https://github.com/rhun-editor/doom/releases/download/v0.1.0/EDITOR-UNICODE-LICENSE.txt)
is also provided alongside these downloads and in the editor source archive.
