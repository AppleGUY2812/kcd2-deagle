#!/usr/bin/env bash
# Live dev loop while KCD2 runs: `tools/live.sh reload` re-runs the whole mod script,
# `tools/live.sh <file.lua>` runs a snippet, `tools/live.sh cmd <console command>` sends a command.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
rcon="$(wslpath -w "$here/tools/rcon.ps1")"
send() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$rcon" "$1" >/dev/null < /dev/null; }
# A dev reload puts every setting back as shipped; a normal load has the plugin replay the user's kept ones
# (deagle_settings.txt next to it), so a reload replays them too (the user lost theirs mid-test, 2026-10-14).
# DevReload replays them itself since cbbfe20: drop replay() once a pak with it is installed (the old one's Set
# doesn't fill Deagle.kept after a save load)
settings="/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Bin/Win64MasterMasterSteamPGO/deagle_settings.txt"
replay() {   # one console line: the console runs ;-separated commands one by one
  [ -f "$settings" ] || return 0
  local line
  line="$(grep -v '^#' "$settings" | tr -d '\r' | awk 'NF { printf "%sdeagle_set %s", (n++ ? "; " : ""), $0 }')"
  [ -n "$line" ] && { sleep 1; send "$line"; }
  return 0
}
case "${1:-reload}" in
  reload) "$here/build.sh" --dev >/dev/null; send "deagle_dev_reload"; replay ;;
  cmd) shift; send "$*" ;;
  *) powershell.exe -NoProfile -Command "Copy-Item '$(wslpath -w "$1")' 'E:\\Tools\\deagle-dev\\snip.lua' -Force"
     send 'deagle_lua dofile([[E:/Tools/deagle-dev/snip.lua]])' ;;
esac
sleep 1
powershell.exe -NoProfile -Command "Select-String -Path 'E:\\SteamLibrary\\steamapps\\common\\KingdomComeDeliverance2\\kcd.log' -Pattern '\\[Deagle\\]' | select -last 4 | % Line"
