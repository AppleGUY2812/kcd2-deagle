#!/usr/bin/env bash
# Converts an imported GameBanana gun (tools/gb/gb_import.sh) into the mod, from its profile guns/<slug>.json:
#   Blender build (clips by role, fixes, extra clips, export) -> rc clips -> textures -> stage into src/ (viewmodel,
#   clips, Scripts/Deagle/guns/<slug>.lua, sounds) -> merge Animations.img -> tests -> pak [--install].
# In game: deagle_gun <slug>.  usage: tools/gb/gb_convert.sh <slug> [--install]
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
slug="${1:?usage: gb_convert.sh <slug> [--install]}"
install="${2:-}"
BLENDER='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
RC='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
G="E:\\Tools\\deagle-work\\guns\\$slug"
win() { wslpath -w "$1"; }
ps() { powershell.exe -NoProfile -Command "$1" | tr -d '\r'; }
blend() { ps "\$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$BLENDER' -b --factory-startup -P '$(win "$1")' -- $2 2>&1 | Select-String '$3' | % Line; exit 0"; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
[ -f "$here/guns/$slug.json" ] || { echo "no profile guns/$slug.json (run tools/gb/gb_import.sh first)" >&2; exit 1; }
odir="$here/src/Objects/manmade/weapons/guns/$slug"
adir="$here/src/Animations/guns/${slug}_vm"
sdir="$here/assets/build/sounds/guns/$slug"
mkdir -p "$odir" "$adir" "$sdir" "$here/assets/build/gun_imgs"

echo "== build (Blender)"
ps "Copy-Item '$(win "$here/guns/$slug.json")' '$G\\profile.json' -Force; Remove-Item -Recurse -Force '$G\\build' -ErrorAction SilentlyContinue"
out="$(blend "$here/tools/gb/gb_build.py" "'$G' '$G\\profile.json' '$RC'" 'NOTE|GB BUILD DONE|Traceback|Error')"
echo "$out" | grep -v "restored zeroed" || true
grep -q "GB BUILD DONE" <<<"$out" || { echo "build failed" >&2; exit 1; }

echo "== compile clips"
cafs="$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(win "$here/tools/gb/gb_anims.ps1")" -Gun "$G" -Slug "$slug" | tr -d '\r' | grep '^CAF:' || true)"
n="$(grep -o '\.caf' <<<"$cafs" | wc -l)"
ps "Copy-Item '$G\\build.json' '$(win "$tmp")\\build.json' -Force"
want="$(python3 -I -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["clips"]))' "$tmp/build.json")"
echo "$n of $want clips"
[ "$n" = "$want" ] || { echo "rc failed on some clips (see KCD2Mod/Tools/rc/rc_log_errors.log)" >&2; exit 1; }

echo "== clip timings (cancel / mag seated / ready / settled, from the clip tracks)"
out="$(ps "& '$BLENDER' -b '$G\\${slug}_vm_roles.blend' -P '$(win "$here/tools/gb/gb_tracks_dump.py")' -- '$G\\tracks.json' '$G\\profile.json' 2>&1 | Select-String 'DUMP DONE|Traceback|Error' | % Line; exit 0")"
grep -q "DONE" <<<"$out" || { echo "tracks dump failed: $out" >&2; exit 1; }
ps "Copy-Item '$G\\tracks.json' '$(win "$tmp")\\tracks.json' -Force"
swap="$(python3 -I -c 'import json,sys; print(",".join(json.load(open(sys.argv[1])).get("timing", {}).get("swapTail", {})))' "$tmp/build.json")"
python3 "$here/tools/clip_times.py" "$tmp/tracks.json" --profile "$here/guns/$slug.json" --swap "$swap" > "$tmp/times.json"
python3 -I -c 'import json,sys; [print("  ", k, v) for k, v in json.load(open(sys.argv[1])).items()]' "$tmp/times.json"

mtlfrom="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("mtlFrom", ""))' "$here/guns/$slug.json")"
if [ -n "$mtlfrom" ]; then   # a Ready or Not gun: its materials are already converted (tools/ron), only reordered here
	echo "== materials (from $mtlfrom)"
	python3 -I "$here/tools/ron/ron_vm_mtl.py" "$tmp/build.json" "$here/$mtlfrom" "$odir" "$slug" | sed 's/^/   /'
	ps "New-Item -ItemType Directory -Force '$G\\tex\\tif' | Out-Null"
else
	echo "== textures"
	out="$(blend "$here/tools/gb/gb_vtf.py" "'$G' 'E:\\Tools\\deagle-work\\guns'" 'MATERIAL|MISSING|FAILED|VTF DONE|Traceback')"
	echo "$out"
	grep -q "VTF DONE" <<<"$out" || { echo "texture decode failed" >&2; exit 1; }
	ps "Copy-Item '$G\\tex\\npy' '$(win "$tmp")' -Recurse -Force; Copy-Item '$G\\tex\\tex.json' '$(win "$tmp")\\tex.json' -Force"
	python3 "$here/tools/gb/gb_tex.py" "$tmp/npy" "$tmp/tex.json" "$tmp/tif" "$slug" "$odir/${slug}_vm.mtl" | grep -E "^(MTL|NO BASE)" || true
	ps "Remove-Item -Recurse -Force '$G\\tex\\tif' -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force '$G\\tex\\tif' | Out-Null; Copy-Item '$(win "$tmp/tif")\\*' '$G\\tex\\tif\\' -Force; Set-Location '$G\\tex\\tif'; & '$RC' '*.tif' /userdialog=0 /refresh /streaming=1 | Select-String 'processed|rror' | % Line"
fi

echo "== stage into src/"
python3 "$here/tools/gb/gb_stage.py" "$here" "$slug" "$here/guns/$slug.json" "$tmp/build.json" "$tmp/times.json" > "$tmp/stage.txt"
grep "^STAGED" "$tmp/stage.txt"
copies="Copy-Item '$G\\build\\model\\*.chr','$G\\build\\model\\*.skin','$G\\tex\\tif\\*.dds*' '$(win "$odir")\\' -Force; Copy-Item '$G\\build\\caf\\*.caf' '$(win "$adir")\\' -Force; Copy-Item '$G\\build\\anims.img' '$(win "$here/assets/build/gun_imgs")\\$slug.img' -Force"
while IFS=$'\t' read -r _ rel base; do
	copies+="; Copy-Item -LiteralPath '$G\\src\\${rel//\//\\}' '$(win "$sdir")\\$base' -Force"
done < <(grep $'^SOUND\t' "$tmp/stage.txt")
ps "$copies"
echo "   $(find "$odir" -name '*.dds' | wc -l) textures, $(find "$adir" -name '*.caf' | wc -l) clips, $(find "$sdir" -type f | wc -l) sounds"

python3 "$here/tools/shot_loudness.py" "$here" | tail -1   # even out every gun's gunshot samples (incl. this one's)

echo "== Animations.img"
ps "Copy-Item 'E:\\Tools\\deagle-work\\out\\deagle_vm_anims.img' '$(win "$tmp")\\deagle_vm_anims.img' -Force"
python3 "$here/tools/merge_img.py" "$here/assets/build/game_Animations.img" "$here/src/Animations/Animations.img" "$tmp/deagle_vm_anims.img" "$here"/assets/build/gun_imgs/*.img | tail -1

echo "== tests"
(cd "$here" && luajit tests/run_flows.lua | grep "TOTAL ERRORS" && luajit tests/run_transitions.lua | tail -1 && luajit tests/run_gun.lua "$slug" | tail -3)

echo "== pak"
if [ "$install" = "--install" ]; then "$here/build.sh" --install | grep -E "installed|packed|running" || true; else "$here/build.sh" | grep packed || true; fi
echo "CONVERT OK: in game, deagle_gun $slug"
