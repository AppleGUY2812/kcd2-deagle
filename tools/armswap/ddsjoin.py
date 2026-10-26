# KCD2 split DDS -> PNG of mip0. base.dds = header (+ small mips); base.dds.N (largest N) = mip0; ".a" = the separate
# alpha texture (same layout: base.dds.a header, base.dds.Na chunks).
import sys, struct, io, glob, os, re
from PIL import Image
def mip0(base, alpha=False):
    hdr_path = base + (".a" if alpha else "")
    d = open(hdr_path, "rb").read()
    hlen = 148 if d[84:88] == b"DX10" else 128
    chunks = [p for p in glob.glob(base + ".*") if re.fullmatch(r"\d+" + ("a" if alpha else ""), p[len(base) + 1:])]
    if not chunks:
        data = d[hlen:]
    else:
        top = max(chunks, key=lambda p: int(re.match(r"\d+", p[len(base) + 1:]).group()))
        data = open(top, "rb").read()
    h = bytearray(d[:hlen]); struct.pack_into("<I", h, 28, 1)   # mip count 1
    if hlen == 148:
        fmt = struct.unpack_from("<I", h, 128)[0]
        srgb = {72: 71, 75: 74, 78: 77, 99: 98}   # BCn _SRGB -> _UNORM: same bytes, Pillow decodes only the latter
        if fmt in srgb: struct.pack_into("<I", h, 128, srgb[fmt])
    return Image.open(io.BytesIO(bytes(h) + data))
if __name__ == "__main__":
    for base in sys.argv[2:]:
        out = os.path.join(sys.argv[1], os.path.basename(base).replace(".dds", ".png"))
        try:
            im = mip0(base); im.save(out); print("ok", out, im.mode, im.size)
        except Exception as e:
            print("FAIL", base, e)
        if os.path.exists(base + ".a"):
            try:
                a = mip0(base, True); a.save(out.replace(".png", "_alpha.png")); print("ok alpha", a.mode, a.size)
            except Exception as e:
                print("FAIL alpha", base, e)
