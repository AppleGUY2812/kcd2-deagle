#!/usr/bin/env python3
"""Every command the Lua script sends the native plugin ("[DN] <cmd> ...", through Deagle:Native / NativeHello) must
have a handler in native/src/main.cpp (strcmp(cmd, "<cmd>")), or it is silently ignored in game. A handler once got
cut out by an edit (the gun-model kick) and nothing said so.
usage: check_native.py <repo>"""
import os, re, sys

repo = sys.argv[1]
lua = open(os.path.join(repo, "src", "Scripts", "Deagle", "deagle_main.lua"), encoding="utf-8").read()
cpp = open(os.path.join(repo, "native", "src", "main.cpp"), encoding="utf-8").read()
sent = set(re.findall(r'Native\(\s*(?:string\.format\(\s*)?"([a-z_]+)', lua))
sent |= set(re.findall(r'"\[DN\] ([a-z_]+)', lua))
handled = set(re.findall(r'strcmp\(cmd, "([a-z_]+)"\)', cpp))
missing = sorted(sent - handled)
if missing:
    print("NATIVE MISSING handlers for:", " ".join(missing))
    sys.exit(1)
print(f"NATIVE OK: {len(sent)} commands sent, all handled ({' '.join(sorted(sent))})")
