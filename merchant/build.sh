#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
out="$here/dist/merchant"
rm -rf "$out" && mkdir -p "$out/data"
cp "$here/mod.manifest" "$out/"
python3 - "$here/src" "$out/data/merchant.pak" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
    for root, _, files in os.walk(src):
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
print("packed", dst, len(zipfile.ZipFile(dst).namelist()), "files")
PY
if [[ -f "$repo/native/dist/merchant.asi" ]]; then
  cp "$repo/native/dist/merchant.asi" "$here/dist/merchant.asi"
else
  echo "no native/dist/merchant.asi: packed the mod, but the required plugin is missing" >&2
  exit 1
fi
if [[ "${1:-}" == "--install" ]]; then
  game=${KCD2_DIR:-/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2}
  if powershell.exe -NoProfile -Command "if(Get-Process KingdomCome -ErrorAction SilentlyContinue){exit 1}"; then :; else
    echo "KCD2 is running: close the game first." >&2; exit 1
  fi
  win_game=$(wslpath -w "$game")
  powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$win_game\Mods' | Out-Null; Remove-Item -Recurse -Force '$win_game\Mods\merchant' -ErrorAction SilentlyContinue; Copy-Item -Recurse '$(wslpath -w "$out")' '$win_game\Mods\merchant'; Copy-Item '$(wslpath -w "$here/dist/merchant.asi")' '$win_game\Bin\Win64MasterMasterSteamPGO\merchant.asi' -Force"
  echo "installed Merchant Resale"
fi
