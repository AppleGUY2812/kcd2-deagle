#!/usr/bin/env bash
# v2 research only: rebuilds the game's HUD movie with the DiceAdvice panel. v1 does not ship a movie or HUD.xml, so
# this writes into dice/build/re/hud_v2 and never into dice/src (the packed mod folder).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
game=${KCD2_DIR:-/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2}
ffdec=${FFDEC:-$HOME/.local/share/ffdec/ffdec-cli.jar}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
out="$repo/dice/build/re/hud_v2"
mkdir -p "$out/UIElements"
python3 -I - "$game/Data/IPL_GameData.pak" "$work" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for name in ("Libs/UI/hud.gfx", "Libs/UI/UIElements/HUD.xml"):
    z.extract(name, sys.argv[2])
PY
ff() { nice -n 19 java -Djava.awt.headless=true -jar "$ffdec" "$@" >"$work/ffdec.log" 2>&1 || { cat "$work/ffdec.log"; exit 1; }; }
ff -selectclass frame_1 -export script "$work/dec" "$work/Libs/UI/hud.gfx" || true
as=$(find "$work/dec" -path '*/scripts/frame_1/DoAction.as' | head -1)
[[ -n $as ]] || { echo "no HUD frame_1 script decompiled"; exit 1; }
mkdir -p "$work/import/scripts/frame_1"
python3 "$here/patch.py" "$as" "$work/import/scripts/frame_1/DoAction.as"
ff -importScript "$work/Libs/UI/hud.gfx" "$out/hud.gfx" "$work/import/scripts"
python3 "$here/patch_xml.py" "$work/Libs/UI/UIElements/HUD.xml" "$out/UIElements/HUD.xml"
ff -selectclass frame_1 -export script "$work/check" "$out/hud.gfx" || true
check=$(find "$work/check" -path '*/scripts/frame_1/DoAction.as' | head -1)
grep -q "function fc_diceAdvice" "$check" || { echo "HUD advice patch missing after rebuild"; exit 1; }
echo "built $out/hud.gfx ($(head -c3 "$out/hud.gfx"), $(stat -c%s "$out/hud.gfx") bytes)"
