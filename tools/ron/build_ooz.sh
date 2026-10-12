#!/usr/bin/env bash
# Build libooz.so, the open-source Oodle (Kraken) decompressor ronx.py loads through ctypes. Ready or Not's paks are
# Oodle-compressed and the game ships no Oodle DLL. Usage: tools/ron/build_ooz.sh [out.so]
# (default ~/.cache/kcd2-deagle/libooz.so, the RON_OOZ default in ronpaths.py). Needs git and g++.
set -euo pipefail
out="${1:-$HOME/.cache/kcd2-deagle/libooz.so}"
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
git clone -q https://github.com/powzix/ooz.git "$tmp/ooz"
git -C "$tmp/ooz" checkout -q 05038060aa68f9187ae9923b2388ca8db40e58d1   # the commit 9f built from (2019-02-11)
cd "$tmp/ooz"
cp "$here/ooz_stdafx.h" stdafx.h          # Linux shim: types, _BitScan*, byteswap / rotate builtins
head -4150 kraken.cpp > k.cpp             # drop ooz's command-line main(); line 4150 ends Kraken_Decompress
echo 'extern "C" int ooz_decompress(const byte*s,size_t sl,byte*d,size_t dl){return Kraken_Decompress(s,sl,d,dl);}' >> k.cpp
mkdir -p "$(dirname "$out")"
g++ -O2 -fPIC -shared -w -fpermissive -o "$out" k.cpp bitknit.cpp lzna.cpp
ls -la "$out"
