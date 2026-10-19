#!/usr/bin/env bash
# Voice-clone pilot, local side. The pod itself is created and deleted by Claude through the RunPod tools (billable).
# This script only talks to a running pod over SSH (key ~/.ssh/runpod_voice, sent as PUBLIC_KEY at create).
#   pilot.sh start <host> <port>            send pod_run.sh and start it: install + model downloads begin at once
#   pilot.sh send  <host> <port> <voice>... upload voices from E:\Tools\deagle-work\voice\<voice>\, then mark READY
#   pilot.sh tail  <host> <port>            show the run log
#   pilot.sh get   <host> <port> <voice>... fetch the results, build one blind listening folder per voice on the Desktop
set -euo pipefail
W=/mnt/e/Tools/deagle-work/voice
KEY=~/.ssh/runpod_voice
cmd=${1:?start|send|tail|get} host=${2:?host} port=${3:?port}
shift 3
ssh_() { ssh -i "$KEY" -p "$port" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=20 "root@$host" "$@"; }
scp_() { scp -i "$KEY" -P "$port" -o StrictHostKeyChecking=accept-new -q "$@"; }
case $cmd in
start)
	ssh_ 'mkdir -p /workspace/in /workspace/out'
	scp_ "$(dirname "$0")/pod_run.sh" "root@$host:/workspace/"
	ssh_ 'nohup bash /workspace/pod_run.sh >/dev/null 2>&1 </dev/null & echo started'
	;;
send)
	for v in "$@"; do
		ssh_ "mkdir -p /workspace/in/$v"
		for k in calm excited; do
			scp_ "$W/$v/${v}_ref_$k.wav" "root@$host:/workspace/in/$v/ref_$k.wav"
			scp_ "$W/$v/${v}_ref_$k.txt" "root@$host:/workspace/in/$v/ref_$k.txt"
		done
		scp_ "$W/$v/pilot_lines.json" "root@$host:/workspace/in/$v/"
		echo "sent $v"
	done
	ssh_ 'touch /workspace/in/READY'
	;;
tail) ssh_ 'tail -25 /workspace/out/run.log; ls /workspace/out/DONE 2>/dev/null' ;;
get)
	for v in "$@"; do
		mkdir -p "$W/$v/out"
		for m in $(ssh_ 'ls /workspace/out' | rg -v '\.' || true); do
			mkdir -p "$W/$v/out/$m"
			ssh_ "cd /workspace/out/$m/$v 2>/dev/null && tar cf - --exclude=codes ." | tar xf - -C "$W/$v/out/$m"
		done
		python3 -I "$(dirname "$0")/listen_folder.py" "$W/$v" "/mnt/c/Users/<user>/Desktop/Voice pilot/$v"
	done
	scp_ "root@$host:/workspace/out/run.log" "$W/run.log"
	;;
*) echo "usage: $0 start|send|tail|get <host> <port> [voice...]" >&2; exit 2 ;;
esac
