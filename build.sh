#!/usr/bin/env bash
# Packs src/ into data/deagle.pak (stored zip, as KCD2 requires) and optionally installs into the game's Mods folder.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/dist/deagle"
rm -rf "$out" && mkdir -p "$out/data"
cp "$here/mod.manifest" "$out/"
python3 - "$here/src" "$out/data/deagle.pak" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            full = os.path.join(root, f)
            z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
print("packed", dst, len(zipfile.ZipFile(dst).namelist()), "files")
PY
if [[ "${1:-}" == "--install" ]]; then
  game='E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2'
  if powershell.exe -NoProfile -Command "if(Get-Process KingdomCome -ErrorAction SilentlyContinue){exit 1}"; then :; else
    echo "KCD2 is running: close the game first (its paks are locked)." >&2; exit 1
  fi
  powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$game\Mods' | Out-Null; Remove-Item -Recurse -Force '$game\Mods\deagle' -ErrorAction SilentlyContinue; Copy-Item -Recurse '$(wslpath -w "$out")' '$game\Mods\deagle'; Get-ChildItem -Recurse '$game\Mods\deagle' | % FullName"
fi
