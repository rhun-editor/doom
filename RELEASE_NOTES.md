DOOM runs in a native rhun editor tab, with the original shareware episode, sound effects and music.

For the easiest first run, download the **rhun-doom-preview** package for your platform, extract
all files, then run **Play DOOM**. It includes a separate editor preview and the installed
extension. Your normal rhun installation and settings are untouched.

- macOS Apple silicon: `rhun-doom-preview-macos-arm64.zip`
- Linux x86-64: `rhun-doom-preview-linux-x86_64.tar.gz`
- Windows x64: `rhun-doom-preview-windows-x64.zip`

If you already have the experimental rhun audio/key support, install `doom-TARGET.tar.gz` through
**Extensions → Install → From an Archive**. Keep `sha256sums.txt` beside it for verification.
Or use **Install from GitHub → rhun-editor/doom@0.1.0**. The version is required for this preview.

Click the game. WASD moves, arrows turn, Space fires, E opens doors, P pauses, and M toggles sound.

This is a preview. Full controls and sound need the companion editor support; stock 0.18.1 uses
basic key pulses. The Mac preview needs macOS 12+ and is not notarized. Linux needs glibc 2.35+ and PulseAudio or ALSA.
Windows needs 64-bit Windows 10+. Hosted Mac/Windows CI tests audio synthesis with a test device;
Linux CI uses a PulseAudio sink. No physical-speaker test is claimed for the hosted runners.

The unchanged shareware WAD and its separate id Software license are included. It must remain
unmodified and cannot be charged for without permission. TimGM6mb instruments and their GPL
notice are included too. Both complete source archives are provided alongside the binaries.
