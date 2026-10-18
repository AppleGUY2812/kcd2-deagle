#!/usr/bin/env bash
# Builds the mesh muzzle flash cards as skins on each viewmodel's skeleton (tools/flash_skin.py) into <gun dir>/flash/,
# then writes them into the .cdf files and checks every skin against its gun's .chr (tools/flash_cdf.py).
# Run after changing the cards (tools/flash_mesh.py, tools/ron/flash_bake.py) or a gun's skeleton.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
W='E:\Tools\deagle-work'
BLENDER='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
RC='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
win() { wslpath -w "$1"; }
ps() { powershell.exe -NoProfile -Command "$1" | tr -d '\r'; }

build() {   # <gun blend> <gun dir in the repo> <.chr> <idle clip> <work name> [muzzle bone]
  local out="$W\\flash_skin\\$5"
  ps "Remove-Item -Recurse -Force '$out' -ErrorAction SilentlyContinue; \$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$BLENDER' -b --factory-startup -P '$(win "$here/tools/flash_skin.py")' -- '$1' '$(win "$here/$2/$3")' '$4' '$W\\flash_ron\\cards.json' '$out' '$RC' ${6:+'$6'} 2>&1 | Select-String -CaseSensitive 'FLASH|Traceback|Error' | % Line"
  # the toolkit's second rc pass (one rc per skin) runs on after Blender quits
  for _ in $(seq 1 120); do [[ $(tasklist.exe | grep -ci '^rc\.exe') -eq 0 ]] && break; sleep 2; done
  local n; n=$(find "$(wslpath "$out")" -maxdepth 1 -name '*.skin' | wc -l)
  [[ $n -gt 0 ]] || { echo "no skins built for $5" >&2; exit 1; }
  rm -rf "${here:?}/$2/flash" && mkdir -p "$here/$2/flash"
  cp "$(wslpath "$out")"/*.skin "$here/$2/flash/"
  echo "$5: $n skins -> $2/flash"
}

build "$W\\blend\\mwii_vm_extra.blend" src/Objects/manmade/weapons/deagle/vm deagle_vm.chr @idle1 deagle
build "$W\\guns\\glock_17\\glock_17_vm_roles.blend" src/Objects/manmade/weapons/guns/glock_17 glock_17_vm.chr @idle glock_17
build "$W\\guns\\ron_grizzly\\ron_grizzly_vm_roles.blend" src/Objects/manmade/weapons/guns/ron_grizzly ron_grizzly_vm.chr @idle ron_grizzly tag_muzzle
python3 "$here/tools/flash_cdf.py" "$here"
