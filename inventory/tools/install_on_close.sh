#!/usr/bin/env bash
# Installs Inventory Tools when KCD2 is not running: waits for the game to close first, then rebuilds the patched
# movies and the pak, and copies the mod + its plugin into the game, checking every file byte for byte afterwards.
# The game is never launched or closed from here; while it runs this only sleeps.
# usage: inventory/tools/install_on_close.sh      (log: inventory/dist/install_on_close.log)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
game='E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2'
unix=/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2
log="$repo/inventory/dist/install_on_close.log"
mkdir -p "$(dirname "$log")"
say() { printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$log"; }

# 0 while KingdomCome is running (KCD2's process name is KingdomCome)
kcd_running() {
	powershell.exe -NoProfile -Command "if (Get-Process KingdomCome -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }" >/dev/null 2>&1
}

# build first (a minute of ffdec), then wait: the copy lands within a second of the close, before a quick relaunch
# (2026-10-29: the user relaunched during the old build-after-close and got the previous build)
say "rebuilding the movies (tools/weightmap/build.sh)"
nice -n 19 bash "$here/weightmap/build.sh" >>"$log" 2>&1
say "packing src + localization (build.sh)"
nice -n 19 bash "$repo/inventory/build.sh" >>"$log" 2>&1

say "waiting for KCD2 to close (this script never starts or stops it): $game"
while kcd_running; do
	# blocks until the process exits (no 5 s polling gap)
	powershell.exe -NoProfile -Command "Wait-Process -Name KingdomCome -ErrorAction SilentlyContinue" >/dev/null 2>&1
done

say "installing"
nice -n 19 bash "$repo/inventory/build.sh" --install >>"$log" 2>&1

# verification: the installed files must be the ones just built
fails=0
check() {   # check <built> <installed> <label>
	if [ ! -f "$1" ]; then say "MISSING built $3: $1"; fails=$((fails + 1)); return; fi
	if [ ! -f "$2" ]; then say "MISSING installed $3: $2"; fails=$((fails + 1)); return; fi
	if cmp -s "$1" "$2"; then say "ok $3 ($(stat -c%s "$2") bytes)"; else say "MISMATCH $3: $1 != $2"; fails=$((fails + 1)); fi
}
check "$repo/inventory/dist/invtools/data/invtools.pak" "$unix/Mods/invtools/data/invtools.pak" "invtools.pak"
check "$repo/inventory/dist/invtools/mod.manifest" "$unix/Mods/invtools/mod.manifest" "mod.manifest"
check "$repo/inventory/dist/invtools/Localization/english_xml.pak" "$unix/Mods/invtools/Localization/english_xml.pak" "english_xml.pak"
check "$repo/native/dist/inventory.asi" "$unix/Bin/Win64MasterMasterSteamPGO/inventory.asi" "inventory.asi"

if [ "$fails" = 0 ]; then
	say "installed: start KCD2 and press Ctrl+F in the inventory to type a search"
else
	say "INSTALL INCOMPLETE: $fails file(s) differ (see above)"
	exit 1
fi
