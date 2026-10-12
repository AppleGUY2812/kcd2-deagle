#!/usr/bin/env python3
"""Every command the Lua script sends the native plugin ("[DN] <cmd> ...", through Deagle:Native / NativeHello) must
have a handler in native/src/*.cpp (strcmp(cmd, "<cmd>")), or it is silently ignored in game. A handler once got
cut out by an edit (the gun-model kick) and nothing said so.
usage: check_native.py <repo>"""
import os, re, sys

repo = sys.argv[1]
import glob
# every Lua file of the mod, every C++ file of the plugin (handlers live in main.cpp, gore.cpp, motion.cpp...)
lua = "\n".join(open(f, encoding="utf-8").read() for f in glob.glob(os.path.join(repo, "src", "Scripts", "Deagle", "**", "*.lua"), recursive=True))
cpp = "\n".join(open(f, encoding="utf-8").read() for f in glob.glob(os.path.join(repo, "native", "src", "*.cpp")))
W = r'([a-z][a-z0-9_]*)'
sent = set(re.findall(r'Native\(\s*(?:string\.format\(\s*)?"' + W, lua))
sent |= set(re.findall(r'"\[DN\] ' + W, lua))
sent |= set(re.findall(r'\bcmd\s*=\s*string\.format\(\s*"' + W, lua))     # a command built first, sent later
handled = set(re.findall(r'strcmp\(cmd, "' + W + r'"\)', cpp))
missing = sorted(sent - handled)
if missing:
    print("NATIVE MISSING handlers for:", " ".join(missing))
    sys.exit(1)
print(f"NATIVE OK: {len(sent)} commands sent, all handled ({' '.join(sorted(sent))})")
