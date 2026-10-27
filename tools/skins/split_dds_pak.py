#!/usr/bin/env python3
"""Split a texture mod's whole .dds files into KCD2's streamed layout, so a load reads only the small mips.

KCD2 (CryEngine) stores a big texture as <name>.dds = header + the 4 smallest mips (32 px and below), plus one file
per larger mip: <name>.dds.1 = the 64 px mip ... <name>.dds.N = mip 0. The engine reads the small part at once and
streams the big mips when a surface needs them. A mod that ships whole 4K/8K .dds files makes every load read all
of them (CRT 4K/8K skins, 2026-10-27: 4.7 GB of a 6.6 GB load). The engine tells the two layouts apart by whether
<name>.dds.1 exists (vanilla has whole and split files with identical headers).

Usage: split_dds_pak.py OUT.pak IN.pak [IN.pak ...]   (entries are stored, not deflated: BC7 barely compresses and
inflating costs CPU at load). Prints a check per file; --verify re-joins each split and compares it to the input.
"""
import io, struct, sys, zipfile

PERSIST = 4          # mips kept in the base file (as vanilla: bNumPersistentMips at byte 116)
MIN_EDGE = 4         # vanilla chains stop at 4x4

BYTES8 = {70, 71, 72, 79, 80, 81}   # BC1 / BC4: 8-byte blocks; the rest (BC2/3/5/6/7) 16-byte


def mip_bytes(w, h, dxgi):
    bs = 8 if dxgi in BYTES8 else 16
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bs


SRGB_OF = {71: 72, 77: 78, 98: 99}   # BC1 / BC3 / BC7 UNORM -> _SRGB (same bytes)


def avg_brightness(level, dxgi, hdr, srgb):
    """The header's average brightness as vanilla stores it: the mean of R, G, B in linear light (sRGB decoded),
    measured on vanilla files (female_body_01_diff 0.143, _bgs 0.586). Taken from the smallest streamed mip."""
    w, h, raw = level
    hd = bytearray(hdr)
    struct.pack_into("<III", hd, 12, h, w, 0)
    struct.pack_into("<I", hd, 28, 1)
    struct.pack_into("<I", hd, 128, {v: k for k, v in SRGB_OF.items()}.get(dxgi, dxgi))   # Pillow decodes UNORM only
    from PIL import Image
    px = Image.open(io.BytesIO(bytes(hd) + raw)).convert("RGB").tobytes()
    lin = [((v / 255) / 12.92 if v <= 10 else (((v / 255) + 0.055) / 1.055) ** 2.4) if srgb else v / 255 for v in range(256)]
    return sum(lin[b] for b in px) / len(px)


def split(data):
    """-> (base bytes, [chunk1, chunk2, ...]) with chunk k = the k-th smallest streamed mip."""
    if data[:4] != b"DDS " or data[84:88] != b"DX10":
        raise ValueError("not a DX10 .dds")
    hlen = 148
    h, w = struct.unpack_from("<II", data, 12)
    mips = struct.unpack_from("<I", data, 28)[0]
    dxgi = struct.unpack_from("<I", data, 128)[0]
    if struct.unpack_from("<I", data, 140)[0] != 1:
        raise ValueError("array texture")
    levels, off = [], hlen
    for m in range(mips):
        mw, mh = max(1, w >> m), max(1, h >> m)
        n = mip_bytes(mw, mh, dxgi)
        levels.append((mw, mh, data[off:off + n]))
        off += n
    if off != len(data):
        raise ValueError(f"size mismatch: header says {off} B, file has {len(data)}")
    keep = [l for l in levels if min(l[0], l[1]) >= MIN_EDGE] or levels[:1]
    if len(keep) <= PERSIST:
        return None   # small: leave whole
    hdr = bytearray(data[:hlen])
    # CryEngine's extended header, as the game's own split textures have it (without "CRYF" the game misreads the
    # split: 2026-10-27, rainbow skins). Fields after the magic: flags @8, pitch @20, depth @24, mips @28,
    # image flags @36 (0x200000 as all vanilla files, 0x800 = sRGB), average brightness @40, min colour @44,
    # max colour @60 (texels are scaled by min..max: 0..1 = as stored), persistent mips @116, "CRYF" @124 (bytes "FYRC").
    struct.pack_into("<I", hdr, 8, 0x21007)
    struct.pack_into("<III", hdr, 20, 0, 0, len(keep))
    # colour handling as the source file had it: sRGB only if its format already says so (the mod's BC7 skins are
    # UNORM and were drawn as such; reading them as sRGB made them dark purple, 2026-10-27)
    srgb = dxgi in SRGB_OF.values()
    struct.pack_into("<If", hdr, 36, 0x200000 | (0x800 if srgb else 0), avg_brightness(keep[-PERSIST - 1], dxgi, hdr, srgb))
    struct.pack_into("<4f", hdr, 44, 0, 0, 0, 0)
    struct.pack_into("<4f", hdr, 60, 1, 1, 1, 1)
    hdr[116] = PERSIST
    hdr[124:128] = b"FYRC"   # the uint32 FOURCC "CRYF", little-endian as in vanilla
    base = bytes(hdr) + b"".join(l[2] for l in keep[-PERSIST:])
    streamed = keep[:-PERSIST]               # mip 0 first
    chunks = [l[2] for l in reversed(streamed)]   # .1 = smallest streamed mip
    return base, chunks


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    verify = "--verify" in sys.argv
    out, ins = args[0], args[1:]
    tot_in = tot_base = tot_out = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED, allowZip64=True) as zo:
        for p in ins:
            zi = zipfile.ZipFile(p)
            for i in zi.infolist():
                data = zi.read(i.filename)
                tot_in += len(data)
                if not i.filename.lower().endswith(".dds"):
                    zo.writestr(i.filename, data); tot_out += len(data); continue
                r = split(data)
                if r is None:
                    zo.writestr(i.filename, data); tot_base += len(data); tot_out += len(data)
                    print(f"whole  {i.filename}"); continue
                base, chunks = r
                zo.writestr(i.filename, base)
                for k, c in enumerate(chunks, 1):
                    zo.writestr(f"{i.filename}.{k}", c)
                tot_base += len(base); tot_out += len(base) + sum(len(c) for c in chunks)
                if verify:
                    mips = struct.unpack_from("<I", base, 28)[0]
                    joined = b"".join(reversed(chunks)) + base[148:]
                    if data[148:148 + len(joined)] != joined:
                        raise SystemExit(f"VERIFY FAILED {i.filename}")
                h, w = struct.unpack_from("<II", base, 12)
                print(f"split  {w}x{h} {len(chunks)} chunks, base {len(base)} B, mip0 {len(chunks[-1]) / 1e6:.1f} MB  {i.filename}")
    print(f"in {tot_in / 1e6:.0f} MB -> out {tot_out / 1e6:.0f} MB; read at once (base files) {tot_base / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
