#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$PWD
make -s -j4
checker="$root/build/checker"
commit=396830ce2b5295416f59d58a0599bde3aa663ba6
if [ ! -d "$checker/.git" ]; then
  git init -q "$checker"
  git -C "$checker" remote add origin https://github.com/rhun-editor/extension-protocol.git
  git -C "$checker" fetch -q --depth 1 origin "$commit"
  git -C "$checker" checkout -q --detach FETCH_HEAD
fi
if git -C "$checker" apply --check "$root/tests/checker.patch" 2>/dev/null; then
  git -C "$checker" apply "$root/tests/checker.patch"
else
  git -C "$checker" apply --reverse --check "$root/tests/checker.patch"
fi
cd "$checker"
exec go run ./conformance/ext "$root"
