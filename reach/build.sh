#!/usr/bin/env bash
# Packs the Unarmed Reach mod; --install copies it into the game (only while KCD2 is closed).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/dist/unarmed_reach"
rm -rf "$out" && mkdir -p "$out/data"
cp "$here/mod.manifest" "$out/"
python3 - "$here/src" "$out/data/unarmed_reach.pak" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
    for root, _, files in os.walk(src):
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
print("packed", dst, len(zipfile.ZipFile(dst).namelist()), "files")
PY
if [[ "${1:-}" == "--install" ]]; then
  game=${KCD2_DIR:-/mnt/d/steam_bruh/steamapps/common/KingdomComeDeliverance2}
  if tasklist.exe 2>/dev/null | grep -qi kingdomcome; then
    echo "KCD2 is running: close the game first." >&2; exit 1
  fi
  rm -rf "$game/Mods/unarmed_reach"
  cp -r "$out" "$game/Mods/unarmed_reach"
  echo "installed Unarmed Reach -> $game/Mods/unarmed_reach"
fi
