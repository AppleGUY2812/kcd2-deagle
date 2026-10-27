#!/usr/bin/env bash
# Builds src/Libs/UI/ApseMap.gfx: the game's map movie with the k-d tree fix (kdtree.py) and the kept tiles
# (maptiles.py), for a faster map open.
#   game IPL_GameData.pak -> Libs/UI/ApseMap.gfx -> decompile (JPEXS) -> kdtree.py + maptiles.py -> recompile.
# Needs Java and JPEXS FFDec (FFDEC=path to ffdec-cli.jar, default ~/.local/share/ffdec/ffdec-cli.jar).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
game=${KCD2_DIR:-/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2}
ffdec=${FFDEC:-$HOME/.local/share/ffdec/ffdec-cli.jar}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
out="$repo/src/Libs/UI"
mkdir -p "$out"
python3 -I - "$game/Data/IPL_GameData.pak" "$work" <<'PY'
import sys, zipfile
zipfile.ZipFile(sys.argv[1]).extract("Libs/UI/ApseMap.gfx", sys.argv[2])
PY
ff() { nice -n 19 java -Djava.awt.headless=true -jar "$ffdec" "$@" >"$work/ffdec.log" 2>&1 || { cat "$work/ffdec.log"; exit 1; }; }
src="$work/Libs/UI/ApseMap.gfx"
ff -export script "$work/dec" "$src"
pk=$(dirname "$(fd -HI -t f '^KDTreeNode\.as$' "$work/dec" | head -1)")
mkdir -p "$work/imp/__Packages" "$work/imp/frame_1"
python3 -I "$here/kdtree.py" "$pk" "$work/imp/__Packages"
python3 -I "$here/maptiles.py" "$work/dec/scripts/frame_1/DoAction.as" "$work/imp/frame_1/DoAction.as"
ff -importScript "$src" "$out/ApseMap.gfx" "$work/imp"
ff -export script "$work/chk" "$out/ApseMap.gfx"
grep -q 'sortOn("__kdx"' "$(fd -HI -t f '^KDTreeNode\.as$' "$work/chk" | head -1)" || { echo "ApseMap: k-d tree patch missing after rebuild"; exit 1; }
grep -q '__mtClearLocals' "$work/chk/scripts/frame_1/DoAction.as" || { echo "ApseMap: tile patch missing after rebuild"; exit 1; }
echo "built $out/ApseMap.gfx ($(stat -c%s "$out/ApseMap.gfx") bytes)"
