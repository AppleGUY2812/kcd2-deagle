#!/usr/bin/env bash
# The last steps of a Ready or Not flash re-bake (tools/ron/flash_bake.py), run with KCD2 closed (Blender + rc need the
# RAM): compiles the baked flash textures (E:\Tools\deagle-work\flash_ron\tif\*.tif) to streamed DDS for the mod's
# src/Textures/particles/deagle/ron_flash (gitignored: private-use textures; <repo> is where they're packed from), builds
# the card skins and .cdf entries (tools/flash_skin.sh), then runs the tests.
#   tools/ron/flash_finish.sh <repo the textures go to>
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
dest="${1:?repo the textures go to}"
if tasklist.exe | grep -qi '^KingdomCome\.exe'; then echo "KCD2 is running: close it first" >&2; exit 1; fi
free=$(powershell.exe -NoProfile -Command "[int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)" | tr -d '\r')
echo "Windows free RAM: ${free} MB"
[[ $free -gt 3000 ]] || { echo "under 3 GB free: close something first" >&2; exit 1; }

rc='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
tif='E:\Tools\deagle-work\flash_ron\tif'
out="$(wslpath -w "$dest/src/Textures/particles/deagle/ron_flash")"
powershell.exe -NoProfile -Command "\$ErrorActionPreference='Stop'; Set-Location '$tif'; & '$rc' '*.tif' /userdialog=0 /refresh /streaming=1 /preset=AlbedoWithGenericAlpha | Select-String 'processed|Exit code|rror' | % Line; New-Item -ItemType Directory -Force '$out' | Out-Null; Get-ChildItem -File | ? { \$_.Name -like '*.dds*' -and \$_.Name -notlike '*.\$dds' } | Copy-Item -Destination '$out' -Force; 'textures: ' + (Get-ChildItem '$out').Count + ' files'" | tr -d '\r'
# every texture newer than its tif, or rc didn't take it
for t in "$(wslpath "$tif")"/*.tif; do
  d="$dest/src/Textures/particles/deagle/ron_flash/$(basename "${t%.tif}").dds"
  [[ "$d" -nt "$t" ]] || { echo "not compiled: $(basename "$t")" >&2; exit 1; }
done

# the guns' .chr are gitignored: a worktree has none, so take them from <repo>
for c in src/Objects/manmade/weapons/deagle/vm/deagle_vm.chr src/Objects/manmade/weapons/guns/glock_17/glock_17_vm.chr; do
  [[ -f "$here/$c" ]] || cp "$dest/$c" "$here/$c"
done
nice -n 10 bash "$here/tools/flash_skin.sh"
nice -n 19 bash "$here/tests/run_all.sh"
