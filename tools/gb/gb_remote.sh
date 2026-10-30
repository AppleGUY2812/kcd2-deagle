#!/usr/bin/env bash
# gb_convert's Blender steps on the ProLiant, so a build doesn't load the PC the game runs on. There is no rc.exe
# there: Blender writes DAE files only (GB_NO_RC=1), the compile stays on the PC (rc-model / gb_anims.ps1).
# usage: gb_remote.sh <slug> push | build | rc-model | repatch | tracks
#   push      tools/ + the gun's <slug>_vm.blend (only when it changed) + profile.json + Henry's two body glbs
#   build     gb_build.py on the ProLiant, pulls build/{model,anims} + build.json back
#   rc-model  rc on the pulled model DAE -> .chr + .skin (PC, low priority, 3 threads), like the toolkit's two passes
#   repatch   pushes the compiled .chr/.caf, runs animation_repatch.py there, pulls the repatched anim DAEs
#   tracks    gb_tracks_dump.py there, pulls tracks.json
# Everything stays in ~/gunwork/<slug> on the ProLiant (scratch; the game files it holds are the user's own, never published).
set -euo pipefail
slug="${1:?usage: gb_remote.sh <slug> push|build|rc-model|repatch|tracks}"; cmd="${2:?}"
here="$(cd "$(dirname "$0")/../.." && pwd)"
H=<user>@<server>
K=(-i "$HOME/.ssh/proliant_key" -o ConnectTimeout=10 -o BatchMode=yes)
GL="/mnt/e/Tools/deagle-work/guns/$slug"                       # the gun's work dir, WSL view
BODY=/mnt/e/Tools/deagle-work/henry/Objects/characters/humans/male/body
RC='E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
R() { ssh "${K[@]}" "$H" "$@"; }
BL='BLENDER_USER_SCRIPTS=$HOME/apps/blender-user nice -n 10 $HOME/apps/blender-4.3.2-linux-x64/blender -b --factory-startup -t 8'
W='$HOME/gunwork'

case "$cmd" in
push)
	R "mkdir -p $W/$slug $W/repo $W/henry_body"
	tar -C "$here" --exclude=__pycache__ -cf - tools | R "tar -C $W/repo -xf -"
	files=(profile.json); local_md5="$(md5sum < "$GL/${slug}_vm.blend" | cut -d' ' -f1)"
	remote_md5="$(R "md5sum < $W/$slug/${slug}_vm.blend 2>/dev/null | cut -d' ' -f1" || true)"
	[ "$local_md5" = "$remote_md5" ] || files+=("${slug}_vm.blend")
	tar -C "$GL" -cf - "${files[@]}" | R "tar -C $W/$slug -xf -"
	tar -C "$BODY" -cf - male_body_arms.glb male_body_hands.glb | R "tar -C $W/henry_body -xf -"
	echo "pushed: tools ${files[*]}" ;;
build)
	R "rm -rf $W/$slug/build $W/$slug/build.json"
	R "cd $W/$slug && GB_NO_RC=1 $BL -P $W/repo/tools/gb/gb_build.py -- $W/$slug $W/$slug/profile.json /none/rc.exe 2>&1" \
		| grep -E 'NOTE|GB BUILD DONE|Traceback|Error' | grep -v "restored zeroed" || true
	R "test -f $W/$slug/build.json" || { echo "remote build failed" >&2; exit 1; }
	R "cd $W/$slug && tar -cf - build.json build/model build/anims" | tar -C "$GL" -xf - ;;
rc-model)
	nodes="$(python3 -I -c 'import json,sys; print(" ".join(json.load(open(sys.argv[1]))["exportNodes"]))' "$GL/build.json")"
	md="$(wslpath -w "$GL/build/model")"
	powershell.exe -NoProfile -Command "\$p = Start-Process '$RC' -ArgumentList '$md\\${slug}_vm.dae','/verbose','/threads=3','/refresh','/userdialog=0' -PassThru -WindowStyle Hidden; \$p.PriorityClass='BelowNormal'; \$p.WaitForExit(); foreach (\$n in '$nodes'.Split(' ')) { \$q = Start-Process '$RC' -ArgumentList '/p=pc','/vertexindexformat=u32','/threads=3','/verbose=2','/refresh','/vertexpositionformat=f32','/userdialog=0',(\"$md\\\" + \$n) -PassThru -WindowStyle Hidden; \$q.PriorityClass='BelowNormal'; \$q.WaitForExit() }" | tr -d '\r' || true
	for n in $nodes; do [ -s "$GL/build/model/$n" ] || { echo "rc left no $n" >&2; exit 1; }; done
	echo "rc-model: $(echo "$nodes" | wc -w) files" ;;
repatch)
	(cd "$GL" && tar -cf - build/model/*.chr build/caf) | R "mkdir -p $W/$slug && tar -C $W/$slug -xf -"
	R "cd $W/$slug && HENRYVM_BODY=$W/henry_body $BL -P $W/repo/tools/armswap/animation_repatch.py -- $W/$slug 2>&1" \
		| grep -E 'HENRY ANIM|Traceback|Error' | tee /dev/stderr | grep -q "HENRY ANIMATION REPACHED"
	R "cd $W/$slug && tar -cf - build/anims" | tar -C "$GL" -xf - ;;
tracks)
	R "cd $W/$slug && $BL ${slug}_vm_roles.blend -P $W/repo/tools/gb/gb_tracks_dump.py -- $W/$slug/tracks.json $W/$slug/profile.json 2>&1" \
		| grep -E 'DUMP DONE|Traceback|Error' | grep -q DONE
	R "cat $W/$slug/tracks.json" > "$GL/tracks.json" ;;
*) echo "unknown command $cmd" >&2; exit 2 ;;
esac
