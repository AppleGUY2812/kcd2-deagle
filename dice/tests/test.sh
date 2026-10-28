#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
bin=.test_dice_logic
cache=.test_dice_logic_cache
trap 'rm -f "$bin"; rm -rf "$cache"' EXIT
g++ -O3 -std=c++17 -pthread -I../../native/src -I../../tools/farkle test_logic.cpp -o "$bin"
nice -n 19 "./$bin" "$cache"
