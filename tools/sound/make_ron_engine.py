#!/usr/bin/env python3
# Ready or Not's own FMOD Studio 2.03.10 (DLLs + the banks the guns use) for the plugin to run live inside KCD2
# (assets/build/ron -> Mods/deagle/ron; private use, never in git). KCD2 already loads its own fmod.dll /
# fmodstudio.dll 2.02.21, and Windows binds a DLL import to an already-loaded module of the same name, so RoN's copies
# are renamed fmor.dll / fmorstudio.dll and the studio DLL's import of "fmod.dll" is patched to "fmor.dll".
# usage: python3 tools/sound/make_ron_engine.py   (from the repo root)
import os, shutil, sys
import pefile

DLLS = os.environ.get("RON_FMOD_DLLS", "/mnt/e/Tools/deagle-work/ron/fmodrender/bin")   # RoN's 2.03.10 fmod.dll + fmodstudio.dll
BANKS = os.environ.get("RON_BANKS", "/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not/ReadyOrNot/Content/FMOD/Desktop")
NEEDED = ["Master.bank", "Master.strings.bank", "Weapons.bank", "Interactions.bank", "UI.bank", "SWAT.bank", "Global Amb.bank"]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "build", "ron")
os.makedirs(out, exist_ok=True)

shutil.copyfile(os.path.join(DLLS, "fmod.dll"), os.path.join(out, "fmor.dll"))
pe = pefile.PE(os.path.join(DLLS, "fmodstudio.dll"))
data = bytearray(pe.__data__)
patched = 0
for imp in pe.DIRECTORY_ENTRY_IMPORT:
	if imp.dll.lower() == b"fmod.dll":
		off = pe.get_offset_from_rva(imp.struct.Name)
		assert data[off:off + 9].lower() == b"fmod.dll\0", data[off:off + 9]
		data[off:off + 8] = b"fmor.dll"
		patched += 1
if patched != 1:
	sys.exit(f"expected one fmod.dll import in fmodstudio.dll, found {patched}")
open(os.path.join(out, "fmorstudio.dll"), "wb").write(data)
check = pefile.PE(os.path.join(out, "fmorstudio.dll"))
print("fmorstudio.dll imports:", ", ".join(i.dll.decode() for i in check.DIRECTORY_ENTRY_IMPORT))

for b in NEEDED:
	src, dst = os.path.join(BANKS, b), os.path.join(out, b)
	if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src) or os.path.getmtime(dst) < os.path.getmtime(src):
		shutil.copyfile(src, dst)
	print(f"{b}: {os.path.getsize(dst) / 1e6:.1f} MB")
