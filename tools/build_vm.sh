#!/usr/bin/env bash
# Rebuilds the viewmodel end to end and the mod pak:
#   [extra clips] -> export (Blender + KCD2 toolkit) -> compile clips (rc) -> stage into src/ -> merge Animations.img
#   -> tests (+ transition audit) -> build.sh [--install]
# Heavy files live in E:\Tools\deagle-work (see README.md). The game must be closed for --install.
# usage: tools/build_vm.sh [--extra] [--audit] [--install]
#   --extra    regenerate blend\mwii_vm_extra.blend from mwii_vm_filled.blend (after editing tools/vm_extra_anims.py)
#   --audit    dump every clip and run the transition audit (fails the build on a jump)
#   --install  install into the game's Mods folder at the end
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
W='E:\Tools\deagle-work'
BLENDER='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
RC='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
VMDIR='Objects\manmade\weapons\deagle\vm'
extra=0 audit=0 install=0
for a in "$@"; do
	case "$a" in
		--extra) extra=1 ;;
		--audit) audit=1 ;;
		--install) install=1 ;;
		*) echo "unknown option: $a" >&2; exit 2 ;;
	esac
done
win() { wslpath -w "$1"; }
ps() { powershell.exe -NoProfile -Command "$1" | tr -d '\r'; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ "$extra" = 1 ]; then
	echo "== extra clips (vm_extra_anims.py)"
	ps "& '$BLENDER' -b '$W\\blend\\mwii_vm_filled.blend' -P '$(win "$here/tools/vm_extra_anims.py")' -- '$W\\blend\\mwii_vm_extra.blend' '$W\\out\\renders' 2>&1 | Select-String 'MADE|SETTLED|EASED|EXTRA DONE|Traceback|Error' | % Line"
fi

echo "== export (vm_export.py)"
out="$(ps "Remove-Item -Recurse -Force '$W\\vm' -ErrorAction SilentlyContinue; \$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$BLENDER' -b --factory-startup -P '$(win "$here/tools/vm_export.py")' -- '$W\\blend\\mwii_vm_extra.blend' '$W\\vm\\$VMDIR' '$W\\vm\\anims' '$RC' 2>&1 | Select-String 'SECTION|ROUNDS|VM EXPORT DONE|Traceback|rror:' | % Line")"
echo "$out"
grep -q "VM EXPORT DONE" <<<"$out" || { echo "export failed" >&2; exit 1; }

echo "== compile clips (vm_anims.ps1)"
cafs="$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(win "$here/tools/vm_anims.ps1")" | tr -d '\r' | grep -E '^CAF:' || true)"
n="$(grep -o '\.caf' <<<"$cafs" | wc -l)"
echo "$n clips"
[ "$n" -gt 0 ] || { echo "compile failed" >&2; exit 1; }

echo "== stage into src/"
ps "Copy-Item '$W\\animroot\\out\\Animations\\deagle_vm\\*.caf' '$(win "$here/src/Animations/deagle_vm")' -Force; Copy-Item '$W\\vm\\$VMDIR\\*.chr','$W\\vm\\$VMDIR\\*.skin' '$(win "$here/src/Objects/manmade/weapons/deagle/vm")' -Force; Copy-Item '$W\\out\\deagle_vm_anims.img' '$(win "$tmp")\\deagle_vm_anims.img' -Force"
python3 "$here/tools/merge_img.py" "$here/assets/build/game_Animations.img" "$here/src/Animations/Animations.img" "$tmp/deagle_vm_anims.img" | tail -1

echo "== tests"
(cd "$here" && luajit tests/run_flows.lua | grep -E "TOTAL ERRORS" && luajit tests/run_fallback.lua | tail -1 && luajit tests/run_transitions.lua | tail -1)

if [ "$audit" = 1 ]; then
	echo "== transition audit"
	ps "& '$BLENDER' -b '$W\\blend\\mwii_vm_extra.blend' -P '$(win "$here/tools/vm_tracks_dump.py")' -- '$W\\out\\tracks.json' 2>&1 | Select-String 'DUMP DONE|Traceback|Error' | % Line"
	ps "Copy-Item '$W\\out\\tracks.json' '$(win "$tmp")\\tracks.json' -Force"
	python3 "$here/tools/vm_transition_audit.py" "$tmp/tracks.json" | tee "$tmp/audit.txt" | head -1
	grep -q "JUMPS 0 " "$tmp/audit.txt" || { grep "JUMP " "$tmp/audit.txt" >&2; echo "transition audit found jumps" >&2; exit 1; }
fi

echo "== pak"
if [ "$install" = 1 ]; then "$here/build.sh" --install | grep -E "installed|packed|running" || true; else "$here/build.sh" | grep -E "packed" || true; fi
echo "BUILD OK"
