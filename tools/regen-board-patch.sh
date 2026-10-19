#!/bin/sh
# Rebuild xnu/patches/0082-lemonadep-board.patch from a clean xnu checkout:
# apply patches 0001-0081, run tools/lemonadep_overlay.py, capture the diff.
set -e
TAG=xnu-8796.141.3
TREE=${TREE:-/home/venus/xnu-8796}
REPO=$(cd "$(dirname "$0")/.." && pwd)
P="$REPO/xnu/patches"

cd "$TREE"
git reset --hard "$TAG" >/dev/null
git clean -fdxq
for s in "$P"/*.sh; do MACHINE_CONFIG=SWEET MACOS_VERSION=13.5 sh "$s"; done
for p in "$P"/0*.patch; do
	case $p in *0082-lemonadep-board.patch) continue ;; esac
	git apply "$p"
done
git add -A
git commit -qm "sweet patches 0001-0081"
python3 "$REPO/tools/lemonadep_overlay.py" "$TREE"
git add -A
git diff --cached > "$REPO/xnu/patches/0082-lemonadep-board.patch"
echo "wrote $REPO/xnu/patches/0082-lemonadep-board.patch ($(wc -l < "$REPO/xnu/patches/0082-lemonadep-board.patch") lines)"
