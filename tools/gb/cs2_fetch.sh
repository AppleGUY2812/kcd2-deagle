#!/usr/bin/env bash
# Fetches CS2's current weapons.vdata (weapon stats, KV3 text) from SteamDB's GameTracking-CS2 mirror into
# assets/build/cs2/ (not committed: Valve's file). tools/gb/cs2_stats.py reads it.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$here/assets/build/cs2"
curl -sfL -o "$here/assets/build/cs2/weapons.vdata" \
	'https://raw.githubusercontent.com/SteamDatabase/GameTracking-CS2/master/game/csgo/pak01_dir/scripts/weapons.vdata'
python3 "$here/tools/gb/cs2_stats.py" "$here/assets/build/cs2/weapons.vdata" | tr ' ' '\n' | grep -c '^weapon_' | sed 's/^/weapons: /'
