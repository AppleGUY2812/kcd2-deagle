#!/usr/bin/env bash
# Waits without interacting with the game process; only a closed game is installed over.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
echo "Waiting for KingdomCome.exe to exit before installing Merchant Resale..."
while powershell.exe -NoProfile -Command "if (Get-Process KingdomCome -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"; do
  sleep 5
done
bash "$here/../build.sh" --install
