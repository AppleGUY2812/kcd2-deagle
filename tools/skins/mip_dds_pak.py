#!/usr/bin/env python3
"""Give a texture mod's mip-less .dds files their mip chain (Rivers of Blood: KCDII HD, 2026-10-27).

A texture with one level (no mips) is sampled at full size however small it is on screen: a 4096 px blood splat
seen as 40 px makes the GPU read 4K texels per pixel, and the frame rate drops whenever blood is in view. Rivers of
Blood ships 28 of its 4K particle textures that way (BC7, BC7 sRGB and DXT5).

Each such texture is decoded (Pillow), its mips are built down to 4x4 by a 2x2 box filter (in linear light for sRGB
formats), and every level is encoded as BC3 / DXT5 (Pillow's encoder; there is no BC7 encoder here, so a BC7 source
becomes BC3: the same 1 byte a texel). Colour handling is kept as the source had it: BC7_UNORM -> BC3_UNORM,
BC7_UNORM_SRGB -> BC3_UNORM_SRGB, a legacy DXT5 header stays a legacy DXT5 header. Textures with mips, small ones
(< MIN_SIZE) and other formats are copied unchanged. Entries are stored, not deflated.

Usage: mip_dds_pak.py OUT.pak IN.pak   (prints a line per texture changed and the colour error of level 0)
"""
import io, struct, sys, zipfile

import numpy as np
from PIL import Image

MIN_SIZE = 256
BC7, BC7_SRGB, BC3, BC3_SRGB = 98, 99, 77, 78


def to_lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def to_srgb(a):
    a = np.clip(a, 0, 1)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055) * 255.0


def decode(data, dxgi):
    """level 0 as an RGBA uint8 array (Pillow decodes the UNORM formats; sRGB bytes are the same)"""
    d = bytearray(data)
    if dxgi in (BC7_SRGB, BC3_SRGB):
        struct.pack_into("<I", d, 128, dxgi - 1)
    return np.asarray(Image.open(io.BytesIO(bytes(d))).convert("RGBA"))


def encode_bc3(rgba):
    b = io.BytesIO()
    Image.fromarray(rgba, "RGBA").save(b, "DDS", pixel_format="DXT5")
    return b.getvalue()[128:]


def chain(rgba, srgb):
    """[level0, level1, ...] down to 4x4, as uint8 RGBA"""
    out = [rgba]
    f = rgba.astype(np.float64)
    if srgb:
        f[..., :3] = to_lin(f[..., :3])
    else:
        f = f / 255.0
    while min(f.shape[0], f.shape[1]) > 4:
        h, w = f.shape[0] // 2, f.shape[1] // 2
        f = f[: h * 2, : w * 2].reshape(h, 2, w, 2, 4).mean(axis=(1, 3))
        g = f.copy()
        if srgb:
            g[..., :3] = to_srgb(g[..., :3]) / 255.0
        out.append(np.clip(np.rint(g * 255.0), 0, 255).astype(np.uint8))
    return out


def header(src, w, h, levels, dxgi_out, legacy, mip0_bytes):
    if legacy:
        hd = bytearray(src[:128])
        struct.pack_into("<I", hd, 8, struct.unpack_from("<I", hd, 8)[0] | 0x20000)      # DDSD_MIPMAPCOUNT
        struct.pack_into("<I", hd, 28, levels)
        struct.pack_into("<I", hd, 108, struct.unpack_from("<I", hd, 108)[0] | 0x400008)  # COMPLEX | MIPMAP
        return bytes(hd)
    hd = bytearray(src[:148])
    struct.pack_into("<I", hd, 8, struct.unpack_from("<I", hd, 8)[0] | 0x20000)
    struct.pack_into("<I", hd, 20, mip0_bytes)
    struct.pack_into("<I", hd, 28, levels)
    struct.pack_into("<I", hd, 108, struct.unpack_from("<I", hd, 108)[0] | 0x401008)
    struct.pack_into("<I", hd, 128, dxgi_out)
    return bytes(hd)


def convert(data):
    """-> new bytes, or None to keep the file as it is"""
    if data[:4] != b"DDS ":
        return None
    h, w = struct.unpack_from("<II", data, 12)
    mips = struct.unpack_from("<I", data, 28)[0]
    four = data[84:88]
    if mips > 1 or min(w, h) < MIN_SIZE:
        return None
    if four == b"DX10":
        dxgi = struct.unpack_from("<I", data, 128)[0]
        if dxgi not in (BC7, BC7_SRGB, BC3, BC3_SRGB):
            return None
        legacy, srgb = False, dxgi in (BC7_SRGB, BC3_SRGB)
        out_dxgi = BC3_SRGB if srgb else BC3
    elif four == b"DXT5":
        dxgi, legacy, srgb, out_dxgi = None, True, False, None
    else:
        return None
    rgba = decode(data, dxgi)
    levels = chain(rgba, srgb)
    blobs = [encode_bc3(l) for l in levels]
    return header(data, w, h, len(blobs), out_dxgi, legacy, len(blobs[0])) + b"".join(blobs), rgba


def main():
    out, src = sys.argv[1], sys.argv[2]
    changed = 0
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(out, "w", zipfile.ZIP_STORED, allowZip64=True) as zo:
        for i in zi.infolist():
            data = zi.read(i.filename)
            r = convert(data) if i.filename.lower().endswith(".dds") else None
            if r is None:
                zo.writestr(i.filename, data)
                continue
            new, rgba = r
            back = np.asarray(Image.open(io.BytesIO(new if new[84:88] != b"DX10" else
                                                     new[:128] + struct.pack("<I", BC3) + new[132:])).convert("RGBA"))
            err = np.abs(back.astype(int) - rgba.astype(int)).mean(axis=(0, 1))
            levels = struct.unpack_from("<I", new, 28)[0]
            print(f"mips {levels:2}  level-0 mean error RGBA {err.round(1)}  {len(data) / 1e6:.1f} -> {len(new) / 1e6:.1f} MB  {i.filename}")
            zo.writestr(i.filename, new)
            changed += 1
    print(f"{changed} textures given mips")


if __name__ == "__main__":
    main()
