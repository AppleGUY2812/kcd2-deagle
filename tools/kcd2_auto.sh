#!/usr/bin/env bash
# Autonomous KCD2 test runs (the user's go, 2026-10-18: "make a script to automate loading the save ... then you can be
# fully autonomous"). Only when the game isn't already running: a running KingdomCome.exe means the user is playing.
#   tools/kcd2_auto.sh start [timeout s]   back up the saves (once a day), turn on wh_sys_AutoLoadLastSave for this run
#                                          ("Newest game is auto loaded on game start", WHGame.dll; off by default) in
#                                          user.cfg, launch through Steam, wait until the mod logs "gameplay started",
#                                          focus the window (KCD2 pauses unfocused)
#   tools/kcd2_auto.sh shot <out.png>      a frame of the game window
#   tools/kcd2_auto.sh stop                close it by its exact PID, take this run's lines out of user.cfg
#   tools/kcd2_auto.sh status
# Install between runs only after stop (the game must be closed for an install). Never pkill -f.
set -euo pipefail
G=/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2
CFG="$G/user.cfg"
LOG="$G/kcd.log"
SAVES="/mnt/c/Users/<user>/Saved Games/kingdomcome2/saves"
BACKUP=/mnt/e/Tools/deagle-work/save_backup
MARK="-- kcd2_auto.sh (remove with tools/kcd2_auto.sh stop)"
um() { command um "$@"; }
pid() { tasklist.exe /FI "IMAGENAME eq KingdomCome.exe" /FO CSV /NH 2>/dev/null | tr -d '\r' | awk -F'","' '/KingdomCome/ {gsub(/"/, "", $2); print $2}' | head -1; }

cfg_on() {
	[ -f "$CFG" ] || : > "$CFG"
	grep -qF -- "$MARK" "$CFG" || printf '%s\nwh_sys_AutoLoadLastSave = 1\n' "$MARK" >> "$CFG"
}
cfg_off() {
	[ -f "$CFG" ] || return 0
	python3 -I - "$CFG" "$MARK" <<'EOF'
import sys
p, mark = sys.argv[1], sys.argv[2]
lines = open(p).read().split("\n"); out = []; skip = False
for l in lines:
    if l == mark: skip = True; continue
    if skip and l.startswith("wh_sys_AutoLoadLastSave"): skip = False; continue
    skip = False; out.append(l)
t = "\n".join(out).strip("\n")
open(p, "w").write(t + "\n" if t else "")
EOF
	[ -s "$CFG" ] || rm -f "$CFG"   # we made it: leave no empty file behind
}

case "${1:-status}" in
start)
	[ -z "$(pid)" ] || { echo "KCD2 is already running (pid $(pid)): the user is playing, not starting" >&2; exit 1; }
	free=$(powershell.exe -NoProfile -Command "[int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)" | tr -d '\r')
	[ "$free" -gt 6000 ] || { echo "only ${free} MB free on Windows: close something first (KCD2 needs ~6 GB)" >&2; exit 1; }
	day="$BACKUP/$(date +%F)"
	if [ ! -d "$day" ]; then mkdir -p "$day"; cp -r "$SAVES/." "$day/"; echo "saves backed up -> $day ($(du -sh "$day" | cut -f1))"; fi
	cfg_on
	lines0=$(wc -l < "$LOG" 2>/dev/null || echo 0)
	start=$(date +%s)
	um win launch --steam 1771300 >/dev/null
	echo "launched; waiting for the save to load"
	timeout="${2:-420}"
	while :; do
		[ $(( $(date +%s) - start )) -lt "$timeout" ] || { echo "not in game after ${timeout} s (see kcd.log)" >&2; exit 1; }
		p=$(pid)
		# kcd.log starts over each launch: count lines from the top once it's newer than our start
		if [ -n "$p" ] && [ -f "$LOG" ] && [ "$(stat -c %Y "$LOG")" -ge "$start" ] && grep -q "\[Deagle\] gameplay started" "$LOG"; then break; fi
		sleep 5
	done
	um win drive --proc KingdomCome "focus" >/dev/null 2>&1 || true
	echo "in game: pid $(pid), $(( $(date +%s) - start )) s"
	: "$lines0"
	;;
shot)
	um win shot --exe KingdomCome.exe "${2:?out.png}" --scale "${3:-0.5}"
	;;
stop)
	p=$(pid)
	if [ -n "$p" ]; then um win kill "$p" >/dev/null; echo "closed pid $p"; else echo "not running"; fi
	for _ in $(seq 1 30); do [ -z "$(pid)" ] && break; sleep 1; done
	cfg_off
	echo "user.cfg: this run's lines removed"
	;;
status)
	p=$(pid); echo "KingdomCome.exe: ${p:-not running}"
	grep -qsF -- "$MARK" "$CFG" && echo "user.cfg: auto-load on (a run is in progress or wasn't stopped)" || echo "user.cfg: clean"
	;;
*) echo "usage: kcd2_auto.sh start [timeout] | shot <png> [scale] | stop | status" >&2; exit 2 ;;
esac
