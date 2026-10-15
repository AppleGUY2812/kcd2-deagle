#!/usr/bin/env bash
# Sniper Elite V2 organs -> KCD2 CGFs + DDS in src/Objects/se2/organs (private assets; needs the OBJs from organ.py and the
# extracted textures from rscf.py, both under E:\Tools\deagle-work\se2). Rebuild: this script, then ./build.sh.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
W='E:\Tools\deagle-work'
work="$W\\Objects\\se2\\organs"
rc='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
blender='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
dest="$here/src/Objects/se2/organs"
tex="$W\\se2\\build\\textures"

python3 "$here/tools/se2/organ_textures.py" /mnt/e/Tools/deagle-work/se2/graphics /mnt/e/Tools/deagle-work/se2/build/textures
powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$work' | Out-Null; Copy-Item '$tex\\*.tif' '$work' -Force; Set-Location '$work'; & '$rc' '*.tif' /userdialog=0 /refresh /streaming=1 | Select-String 'processed|Exit code' | % Line"
powershell.exe -NoProfile -Command "\$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$blender' -b --factory-startup -P '$(wslpath -w "$here/tools/se2/build_organs.py")' -- '$W\\se2\\organs' '$work' '$rc' 'C:\\Users\\<user>\\Desktop\\Deagle Progress\\24 Sniper Elite organs\\organs in KCD2 preview.png' 2>&1 | Select-String 'PART|EXPORT DONE|Traceback|Error|missing' | % Line"
mkdir -p "$dest"
find "$dest" -type f ! -name '*.mtl' -delete
powershell.exe -NoProfile -Command "Set-Location '$work'; Copy-Item se2_*.cgf '$(wslpath -w "$dest")'; Get-ChildItem -File | ? { \$_.Name -like '*.dds*' -and \$_.Name -notlike '*.\$dds' } | Copy-Item -Destination '$(wslpath -w "$dest")'"
find "$dest" -type f | wc -l
