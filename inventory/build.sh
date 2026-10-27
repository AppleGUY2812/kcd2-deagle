#!/usr/bin/env bash
# Packs inventory/src into data/invtools.pak (stored zip, as KCD2 requires), packs the localization, and optionally
# installs the mod and its plugin (native/dist/inventory.asi) into the game.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
out="$here/dist/invtools"
rm -rf "$out" && mkdir -p "$out/data"
cp "$here/mod.manifest" "$out/"
python3 - "$here/src" "$out/data/invtools.pak" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            full = os.path.join(root, f)
            z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
print("packed", dst, len(zipfile.ZipFile(dst).namelist()), "files")
PY
mkdir -p "$out/Localization"
python3 - "$here/loc/english" "$out/Localization/english_xml.pak" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:
    for f in sorted(os.listdir(src)):
        z.write(os.path.join(src, f), f)
print("packed", dst)
PY
if [[ "${1:-}" == "--install" ]]; then
  game='E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2'
  if powershell.exe -NoProfile -Command "if(Get-Process KingdomCome -ErrorAction SilentlyContinue){exit 1}"; then :; else
    echo "KCD2 is running: close the game first (its paks are locked)." >&2; exit 1
  fi
  powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force '$game\Mods' | Out-Null; Remove-Item -Recurse -Force '$game\Mods\invtools' -ErrorAction SilentlyContinue; Copy-Item -Recurse '$(wslpath -w "$out")' '$game\Mods\invtools'; Get-ChildItem -Recurse '$game\Mods\invtools' | % FullName"
  if [[ -f "$repo/native/dist/inventory.asi" ]]; then
    powershell.exe -NoProfile -Command "Copy-Item '$(wslpath -w "$repo/native/dist/inventory.asi")' '$game\Bin\Win64MasterMasterSteamPGO\inventory.asi' -Force; 'installed inventory.asi ' + (Get-Item '$game\Bin\Win64MasterMasterSteamPGO\inventory.asi').Length"
  else
    echo "no native/dist/inventory.asi: built the movies and scripts only" >&2
  fi
fi
