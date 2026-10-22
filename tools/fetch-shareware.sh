#!/bin/sh
# Fetch the unmodified Doom 1.9 shareware episode separately from the source.
set -eu
cd "$(dirname "$0")/.."
mkdir -p data
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
curl --fail --location --proto '=https' --tlsv1.2 \
  https://deb.debian.org/debian/pool/non-free/d/doom-wad-shareware/doom-wad-shareware_1.9.fixed.orig.tar.gz \
  -o "$work/shareware.tar.gz"
tar -xzf "$work/shareware.tar.gz" -C "$work" doom-wad-shareware-1.9.fixed/doom1.wad
expected=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
actual=$(shasum -a 256 "$work/doom-wad-shareware-1.9.fixed/doom1.wad" | cut -d ' ' -f 1)
[ "$actual" = "$expected" ] || { echo 'Shareware checksum mismatch.' >&2; exit 1; }
cp "$work/doom-wad-shareware-1.9.fixed/doom1.wad" data/doom1.wad
echo 'Doom 1.9 shareware is ready in data/doom1.wad (4,196,020 bytes).'
echo 'The game data belongs to id Software and is not covered by this repository’s GPL license.'
