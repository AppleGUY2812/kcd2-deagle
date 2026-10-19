#!/usr/bin/env bash
# Voice-clone pilot, local side. The pod itself is created and deleted by Claude through the RunPod tools (billable:
# the user approves the price first). This script only talks to a running pod over SSH.
#   pilot.sh up   <host> <port>   send the inputs + pod_run.sh, start the run (detached on the pod)
#   pilot.sh tail <host> <port>   show the run log
#   pilot.sh get  <host> <port>   fetch the results, build the listening folder on the Desktop
# Inputs from E:\Tools\deagle-work\voice\<slug>\ (made by the extraction step): pilot_lines.json, ref wav/txt, heldout/.
set -euo pipefail
SLUG=${SLUG:-henry}
W=/mnt/e/Tools/deagle-work/voice/$SLUG
KEY=~/.ssh/runpod_voice
DESK="/mnt/c/Users/<user>/Desktop/Voice pilot - $SLUG"
cmd=${1:?up|tail|get} host=${2:?host} port=${3:?port}
ssh_() { ssh -i "$KEY" -p "$port" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=20 "root@$host" "$@"; }
case $cmd in
up)
	ssh_ 'mkdir -p /workspace/in /workspace/out'
	scp -i "$KEY" -P "$port" -q "$W/pilot_lines.json" "$W"/"$SLUG"_ref_*.wav "$W"/"$SLUG"_ref_*.txt \
		"root@$host:/workspace/in/"
	scp -i "$KEY" -P "$port" -q "$(dirname "$0")/pod_run.sh" "root@$host:/workspace/"
	ssh_ 'nohup bash /workspace/pod_run.sh >/dev/null 2>&1 </dev/null & echo started'
	;;
tail) ssh_ 'tail -25 /workspace/out/run.log; ls /workspace/out/DONE 2>/dev/null' ;;
get)
	mkdir -p "$W/out"
	ssh_ 'cd /workspace/out && tar cf - --exclude=codes --exclude=.drama_ready .' | tar xf - -C "$W/out"
	python3 -I "$(dirname "$0")/listen_folder.py" "$W" "$DESK"
	;;
*) echo "usage: $0 up|tail|get <host> <port>" >&2; exit 2 ;;
esac
