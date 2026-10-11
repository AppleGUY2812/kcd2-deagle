#!/usr/bin/env bash
# Compile every deagle i_caf to caf, build their index and merge it into the game's Animations.img (src/Animations).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
root='E:\Tools\deagle-work\animroot'
rc='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
tmpw='C:\Users\<user>\AppData\Local\Temp\kcd2re'
tmpl=/mnt/c/Users/<user>/AppData/Local/Temp/kcd2re
powershell.exe -NoProfile -Command "Set-Location '$root'; Remove-Item -Recurse -Force '$root\\out' -ErrorAction SilentlyContinue; & '$rc' 'Animations\\humans\\male\\deagle\\*.i_caf' /animConfigFolder=Animations /SourceRoot='$root' /TargetRoot='$root\\out' /p=PC /refresh /userdialog=0 /AnimationsCafFilePath=Animations/Animations.img | Out-Null; Copy-Item '$root\\out\\Animations\\humans\\male\\deagle\\*.caf' '$(wslpath -w "$here/src/Animations/humans/male/deagle")\\' -Force; Copy-Item '$root\\out\\Animations\\Animations.img' '$tmpw\\deagle_anims.img' -Force"
python3 "$here/tools/merge_img.py" "$here/assets/build/game_Animations.img" "$here/src/Animations/Animations.img" "$tmpl/deagle_anims.img" | tail -1
find "$here/src/Animations/humans/male/deagle" -name '*.caf' | wc -l
