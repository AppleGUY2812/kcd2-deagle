#!/usr/bin/env bash
# Installs RonSlowMo (slow motion for recording Ready or Not's effects) with the UE4SS mod loader into Ready or Not:
#   Binaries/Win64/dwmapi.dll + ue4ss/ (UE4SS, pinned build below; winmm.dll there is someone else's, left alone)
#   ue4ss/Mods/RonSlowMo (this folder's mod); every UE4SS sample mod is switched off (ConsoleEnabler takes F10,
#   Keybinds' Ctrl+J dumps every object).
# Run only while Ready or Not is closed. Re-run after editing the mod; config.lua edits can also go straight into the
# installed copy (ue4ss/Mods/RonSlowMo/Scripts/config.lua), then restart the game.
# usage: tools/ron/slomo/install.sh            uninstall: tools/ron/slomo/install.sh --remove
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
BIN="/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not/ReadyOrNot/Binaries/Win64"
ZIP_NAME=UE4SS_v3.0.1-1181-g83330635.zip
ZIP_SHA=b9a166a756605b21058a64cde93bcaab1a855dba3553e5c7e6ce0fa8fdfad7b9
CACHE=/mnt/e/Tools/deagle-work/ron_slomo

if tasklist.exe | grep -qi readyornot; then echo "Ready or Not is running: close it first" >&2; exit 1; fi
[[ -d $BIN ]] || { echo "no Ready or Not at $BIN" >&2; exit 1; }

if [[ ${1:-} == --remove ]]; then
	rm -rf "${BIN:?}/ue4ss" "$BIN/dwmapi.dll"; echo "removed UE4SS + RonSlowMo"; exit 0
fi

mkdir -p "$CACHE"
if [[ ! -f $CACHE/$ZIP_NAME ]]; then
	curl -sfL -m 300 -o "$CACHE/$ZIP_NAME" "https://github.com/UE4SS-RE/RE-UE4SS/releases/download/experimental-latest/$ZIP_NAME"
fi
echo "$ZIP_SHA  $CACHE/$ZIP_NAME" | sha256sum -c --quiet || { echo "UE4SS zip checksum mismatch" >&2; exit 1; }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
unzip -q "$CACHE/$ZIP_NAME" -d "$tmp"
# Ready or Not is UE 5.3: naming it skips UE4SS's version scan (the ini has CRLF line endings: keep them)
sed -i -e 's/^MajorVersion = *\(\r\?\)$/MajorVersion = 5\1/' -e 's/^MinorVersion = *\(\r\?\)$/MinorVersion = 3\1/' "$tmp/ue4ss/UE4SS-settings.ini"
ini=$(tr -d '\r' < "$tmp/ue4ss/UE4SS-settings.ini")
if ! grep -qx 'MajorVersion = 5' <<< "$ini" || ! grep -qx 'MinorVersion = 3' <<< "$ini"; then
	echo "UE4SS-settings.ini: version lines not found" >&2; exit 1
fi
cp -r "$here/RonSlowMo" "$tmp/ue4ss/Mods/"
mapfile -t mods < <(cd "$tmp/ue4ss/Mods" && for d in */; do d=${d%/}; [[ $d == shared ]] || echo "$d"; done)
{ for m in "${mods[@]}"; do [[ $m == RonSlowMo ]] || echo "$m : 0"; done; echo "RonSlowMo : 1"; } > "$tmp/ue4ss/Mods/mods.txt"
python3 - "$tmp/ue4ss/Mods/mods.json" "${mods[@]}" <<'EOF'
import json, sys
json.dump([{"mod_name": m, "mod_enabled": m == "RonSlowMo"} for m in sys.argv[2:]], open(sys.argv[1], "w"), indent=4)
EOF

rm -rf "${BIN:?}/ue4ss"
cp -r "$tmp/ue4ss" "$BIN/"
cp "$tmp/dwmapi.dll" "$BIN/"
cmp -s "$here/RonSlowMo/Scripts/main.lua" "$BIN/ue4ss/Mods/RonSlowMo/Scripts/main.lua" || { echo "copy check failed" >&2; exit 1; }
echo "installed UE4SS ($ZIP_NAME) + RonSlowMo into $BIN"
cat "$tmp/ue4ss/Mods/mods.txt"
