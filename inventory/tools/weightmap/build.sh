#!/usr/bin/env bash
# Builds the weight map ("WizTree for the inventory") into the game's three item-list movies:
#   game IPL_GameData.pak -> Libs/UI/{ApseInventoryList,ItemTransfer,ApseModalDialog}.gfx -> decompile frame_1 (JPEXS)
#   -> patch.py (anchored edits + common.as + adapter) -> recompile -> inventory/src/Libs/UI/*.gfx (the mod's pak).
# Needs Java and JPEXS FFDec (FFDEC=path to ffdec-cli.jar, default ~/.local/share/ffdec/ffdec-cli.jar).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
game=${KCD2_DIR:-/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2}
ffdec=${FFDEC:-$HOME/.local/share/ffdec/ffdec-cli.jar}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
out="$repo/inventory/src/Libs/UI"
mkdir -p "$out"

python3 -I - "$game/Data/IPL_GameData.pak" "$work" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in ("Libs/UI/ApseInventoryList.gfx", "Libs/UI/ItemTransfer.gfx", "Libs/UI/ApseModalDialog.gfx",
          "Libs/UI/UIElements/ApseInventoryList.xml", "Libs/UI/UIElements/ItemTransfer.xml",
          "Libs/UI/UIElements/ApseModalDialog.xml"):
    z.extract(n, sys.argv[2])
EOF

ff() { nice -n 19 java -Djava.awt.headless=true -jar "$ffdec" "$@" >"$work/ffdec.log" 2>&1 || { cat "$work/ffdec.log"; exit 1; }; }

mkdir -p "$out/UIElements"
for pair in inv:ApseInventoryList tr:ItemTransfer md:ApseModalDialog; do
	python3 "$here/patch_xml.py" "${pair%%:*}" "$work/Libs/UI/UIElements/${pair#*:}.xml" "$out/UIElements/${pair#*:}.xml"
done
for pair in inv:ApseInventoryList tr:ItemTransfer md:ApseModalDialog; do
	kind=${pair%%:*} movie=${pair#*:}
	src="$work/Libs/UI/$movie.gfx"
	ff -selectclass frame_1 -export script "$work/dec_$kind" "$src" || true
	as=$(find "$work/dec_$kind" -path '*/scripts/frame_1/DoAction.as' | head -1)
	[[ -n $as ]] || { echo "no frame_1 script decompiled for $movie"; exit 1; }
	mkdir -p "$work/imp_$kind/scripts/frame_1"
	python3 "$here/patch.py" "$kind" "$as" "$work/imp_$kind/scripts/frame_1/DoAction.as"
	ff -importScript "$src" "$out/$movie.gfx" "$work/imp_$kind/scripts"
	# The rebuilt file must still decompile and contain the map (catches a silent compile drop).
	ff -selectclass frame_1 -export script "$work/chk_$kind" "$out/$movie.gfx" || true
	want="function WM_Build"; [[ $kind == md ]] && want="function fc_invAutoMax"
	grep -q "$want" "$(find "$work/chk_$kind" -path '*/scripts/frame_1/DoAction.as' | head -1)" \
		|| { echo "$movie: patch missing after rebuild"; exit 1; }
	echo "built $out/$movie.gfx ($(head -c3 "$out/$movie.gfx"), $(stat -c%s "$out/$movie.gfx") bytes)"
done
