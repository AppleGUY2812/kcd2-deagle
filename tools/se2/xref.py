#!/usr/bin/env python3
"""Disassemble around the code that references a string in a PE file, annotating rip-relative strings and floats.
usage: xref.py <exe> <string> [before=40] [after=120]"""
import re, struct, sys
import pefile, capstone

path, needle = sys.argv[1], sys.argv[2].encode()
before, after = int(sys.argv[3]) if len(sys.argv) > 3 else 40, int(sys.argv[4]) if len(sys.argv) > 4 else 120
pe = pefile.PE(path, fast_load=True)
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
srva = img.find(b"\0" + needle + b"\0") + 1
if srva <= 0:
    raise SystemExit("string not found")
text = [s for s in pe.sections if s.Name.startswith(b".text")][0]
t0, tdata = text.VirtualAddress, text.get_data()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

def note(rva):
    s = img[rva: rva + 48].split(b"\0")[0]
    if len(s) >= 3 and all(32 <= c < 127 for c in s):
        return f'"{s.decode()}"'
    if rva + 4 <= len(img):
        f = struct.unpack_from("<f", img, rva)[0]
        if f == f and abs(f) < 1e7:
            return f"float {f:g}"
    return ""

refs = [t0 + m.start() for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", tdata)
        if t0 + m.start() + 7 + struct.unpack_from("<i", tdata, m.start() + 3)[0] == srva]
print("string rva", hex(srva), "refs", [hex(r) for r in refs])
for at in refs[:1]:
    start = at - before * 5
    for back in range(0, 16):
        ins = list(md.disasm(img[start + back: at + after * 5], start + back))
        if any(i.address == at for i in ins):
            break
    k = next(i for i, x in enumerate(ins) if x.address == at)
    for x in ins[max(0, k - before): k + after]:
        line = f"{x.address:#x} {x.mnemonic} {x.op_str}"
        m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", x.op_str)
        if m:
            tgt = x.address + x.size + (int(m.group(2), 16) * (1 if m.group(1) == "+" else -1))
            line += f"   ; {note(tgt) or hex(tgt)}"
        print(line + ("   <== ref" if x.address == at else ""))
