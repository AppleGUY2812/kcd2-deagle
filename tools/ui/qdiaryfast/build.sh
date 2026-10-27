#!/usr/bin/env bash
# Builds src/Libs/UI/ApseQuestLogDiary.gfx: the game's quest diary movie with the unchanged-quest skip (qdiary.py).
#   game IPL_GameData.pak -> Libs/UI/ApseQuestLogDiary.gfx -> decompile frame 1 (JPEXS) -> qdiary.py -> recompile.
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
zipfile.ZipFile(sys.argv[1]).extract("Libs/UI/ApseQuestLogDiary.gfx", sys.argv[2])
PY
ff() { nice -n 19 java -Djava.awt.headless=true -jar "$ffdec" "$@" >"$work/ffdec.log" 2>&1 || { cat "$work/ffdec.log"; exit 1; }; }
src="$work/Libs/UI/ApseQuestLogDiary.gfx"
ff -export script "$work/dec" "$src"
mkdir -p "$work/imp/frame_1"
python3 -I "$here/qdiary.py" "$work/dec/scripts/frame_1/DoAction.as" "$work/imp/frame_1/DoAction.as"
ff -importScript "$src" "$out/ApseQuestLogDiary.gfx" "$work/imp"
ff -export script "$work/chk" "$out/ApseQuestLogDiary.gfx"
grep -q '__qdLast' "$work/chk/scripts/frame_1/DoAction.as" || { echo "ApseQuestLogDiary: patch missing after rebuild"; exit 1; }
echo "built $out/ApseQuestLogDiary.gfx ($(stat -c%s "$out/ApseQuestLogDiary.gfx") bytes)"
