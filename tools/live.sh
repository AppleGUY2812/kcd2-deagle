#!/usr/bin/env bash
# Live dev loop while KCD2 runs: `tools/live.sh reload` re-runs the whole mod script,
# `tools/live.sh <file.lua>` runs a snippet, `tools/live.sh cmd <console command>` sends a command.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
rcon="$(wslpath -w "$here/tools/rcon.ps1")"
send() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$rcon" "$1" >/dev/null; }
case "${1:-reload}" in
  reload) "$here/build.sh" --dev >/dev/null; send "deagle_dev_reload" ;;
  cmd) shift; send "$*" ;;
  *) powershell.exe -NoProfile -Command "Copy-Item '$(wslpath -w "$1")' 'E:\\Tools\\deagle-dev\\snip.lua' -Force"
     send 'deagle_lua dofile([[E:/Tools/deagle-dev/snip.lua]])' ;;
esac
sleep 1
powershell.exe -NoProfile -Command "Select-String -Path 'E:\\SteamLibrary\\steamapps\\common\\KingdomComeDeliverance2\\kcd.log' -Pattern '\\[Deagle\\]' | select -last 4 | % Line"
