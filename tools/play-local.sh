#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$PWD
editor=${DOOM_RHUN:-}
if [ -z "$editor" ] && [ -f .local-rhun ]; then editor=$(cat .local-rhun); fi
if [ -z "$editor" ]; then editor=$(command -v rhun || true); fi
[ -n "$editor" ] && [ -x "$editor" ] || { echo 'Set DOOM_RHUN to your rhun executable.' >&2; exit 1; }
make -s -j4
[ -f data/doom1.wad ] || sh tools/fetch-shareware.sh
local="$root/build/local"
ext="$local/extensions/rhun-editor/doom"
mkdir -p "$ext/bin" "$ext/data" "$local/config/rhun" "$local/state" "$local/tmp"
# Start automatically only in this disposable development install.
sed 's/^on = command$/on = startup/' extension.ini > "$ext/extension.ini"
cp bin/doom-macos-arm64 "$ext/bin/doom-macos-arm64.new"
mv "$ext/bin/doom-macos-arm64.new" "$ext/bin/doom-macos-arm64"
cp data/doom1.wad "$ext/data/doom1.wad"
cp data/SHAREWARE-LICENSE.txt "$ext/data/SHAREWARE-LICENSE.txt"
cat > "$local/config/rhun/config" <<'EOF'
[ui]
agents_panel = false
sidebar = false
[files]
restore_session = false
restore_project = false
[updates]
check = false
[git]
enabled = false
EOF
HOME="$local" XDG_CONFIG_HOME="$local/config" XDG_STATE_HOME="$local/state" TMPDIR="$local/tmp" \
  DOOM_AUTOSTART=1 exec "$editor" "$root" --empty --extensions "$local/extensions"
