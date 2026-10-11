#!/usr/bin/env bash
# One clip: Mixamo FBX -> retarget (+FPS framing) -> arm-only DAE -> i_caf -> caf -> merged Animations.img into src/.
# usage: tools/build_anim.sh <name> <mixamo.fbx> [key=value framing opts, e.g. fwd=0.42 right=0.06 up=-0.30]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
name="$1"; fbx="$2"; shift 2
root='E:\Tools\deagle-work\animroot'
outdir="$root\\Animations\\humans\\male\\deagle"
rc='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
blender='E:\Tools\blender-4.3.2-windows-x64\blender.exe'
skin='E:\Tools\deagle-work\henry\Objects\characters\humans\male\body\male_body.skin'
tmpw='C:\Users\<user>\AppData\Local\Temp\kcd2re'
tmpl=/mnt/c/Users/<user>/AppData/Local/Temp/kcd2re
scratch="$here/assets/build/anim"; mkdir -p "$scratch"

powershell.exe -NoProfile -Command "\$env:DOTNET_ROOT='E:\\Tools\\dotnet9'; \$env:BLENDER_USER_SCRIPTS='E:\\Tools\\blender-user'; & '$blender' -b --factory-startup -P '$(wslpath -w "$here/tools/retarget.py")' -- '$skin' '$(wslpath -w "$fbx")' '$name' '$outdir' '$rc' none '$(wslpath -w "$here/assets/renders")' $* 2>&1 | Select-String 'RETARGET|FRAMING|DAE EXPORTED|Traceback|Error' | % Line" || true  # blender exits 1 after background scripts
powershell.exe -NoProfile -Command "Copy-Item '$outdir\\$name.dae' '$tmpw\\$name.dae' -Force"
python3 "$here/tools/filter_dae.py" "$tmpl/$name.dae" "$tmpl/${name}_arms.dae"
printf '{\n\t"build": {\n\t\t"skeletonAlias": "Human_Male",\n\t\t"additive": false\n\t}\n}\n' > "$tmpl/$name.animsettings"
powershell.exe -NoProfile -Command "Copy-Item '$tmpw\\${name}_arms.dae' '$outdir\\$name.dae' -Force; Copy-Item '$tmpw\\$name.animsettings' '$outdir\\' -Force; Set-Location '$outdir'; & '$rc' '$name.dae' /refresh /userdialog=0 | Out-Null; Set-Location '$root'; Remove-Item -Recurse -Force '$root\\out' -ErrorAction SilentlyContinue; & '$rc' 'Animations\\humans\\male\\deagle\\*.i_caf' /animConfigFolder=Animations /SourceRoot='$root' /TargetRoot='$root\\out' /p=PC /refresh /userdialog=0 /AnimationsCafFilePath=Animations/Animations.img | Out-Null; Copy-Item '$root\\out\\Animations\\humans\\male\\deagle\\*.caf' '$(wslpath -w "$here/src/Animations/humans/male/deagle")\\' -Force; Copy-Item '$root\\out\\Animations\\Animations.img' '$tmpw\\deagle_anims.img' -Force"
python3 "$here/tools/merge_img.py" "$here/assets/build/game_Animations.img" "$here/src/Animations/Animations.img" "$tmpl/deagle_anims.img"
ls -la "$here/src/Animations/humans/male/deagle/"
