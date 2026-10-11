#!/usr/bin/env bash
# Rebuilds the Deagle model assets: textures -> rc -> Blender CGFs -> copied into src/Objects for packing.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
work='E:\Tools\deagle-work\Objects\manmade\weapons\deagle'
rc='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
blender='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
dest="$here/src/Objects/manmade/weapons/deagle"

python3 "$here/tools/convert_textures.py" "$here/assets/source/deagle_gltf/textures" "$here/assets/build/textures" 2048
powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$work' | Out-Null; Copy-Item '$(wslpath -w "$here/assets/build/textures")\\*.tif' '$work' -Force; Set-Location '$work'; & '$rc' '*.tif' /userdialog=0 /refresh /streaming=1 | Select-String 'processed|Exit code' | % Line"
powershell.exe -NoProfile -Command "\$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$blender' -b --factory-startup -P '$(wslpath -w "$here/tools/build_model.py")' -- '$(wslpath -w "$here/assets/source/deagle_gltf/scene.gltf")' '$work' '$rc' 2>&1 | Select-String 'PART|EXPORT DONE|Traceback|Error' | % Line"

mkdir -p "$dest"
find "$dest" -type f ! -name '*.mtl' -delete
# E: is not mounted in WSL, so copy from the Windows side
powershell.exe -NoProfile -Command "Set-Location '$work'; Copy-Item deagle_frame.cgf,deagle_slide.cgf,deagle_mag.cgf,deagle_case.cgf '$(wslpath -w "$dest")'; Get-ChildItem -File | ? { \$_.Name -like '*.dds*' -and \$_.Name -notlike '*.\$dds' } | Copy-Item -Destination '$(wslpath -w "$dest")'"
find "$dest" -type f | wc -l
