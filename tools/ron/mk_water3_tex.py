#!/usr/bin/env python3
"""Textures for the water_v3 hit (deagle_impacts.xml), cut from KCD2's own water particle textures:
  imp_w3_jet_diff    textures/particles/water_splash_02 (36-frame jet breaking into spray, 6x6) at 2048 instead of
                     4096: KCD2 streams the 4096 one in late, so the first hit of a session drew a blurry block
                     (in game 2026-10-14)
  imp_w3_crown_diff  textures/particles/water_splash_03 (4 crown shapes, 2x2)
  imp_w3_burst_diff  textures/particles/water_splash (16-frame droplet burst, 4x4) at 1024
Colour: white with a faint blue, alpha kept. KCD2 tints the jet and the crown tan (its muddy-water material and
texture), which read orange under Henry's lantern at night.
Reads the split, streamed DDS straight out of the game's IPL_Textures-part1.pak (header + the chosen mip).
Output: PNG previews + TIF (LZW) for tools/fx_impact_textures.ps1 (imp_*_diff -> AlbedoWithGenericAlpha).
usage: mk_water3_tex.py <out_dir> [pak]"""
import io, os, struct, sys, zipfile
import numpy as np
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else '.'
PAK = sys.argv[2] if len(sys.argv) > 2 else \
    '/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data/IPL_Textures-part1.pak'
TINT = np.array([0.88, 0.94, 1.0], np.float32)


def mip(z, name, size):
    """one mip of a CryEngine split DDS: the header from <name>.dds, the pixels from <name>.dds.<k>"""
    hdr = z.read(name + '.dds')
    h, w = struct.unpack_from('<II', hdr, 12)
    dx10 = hdr[84:88] == b'DX10'
    names = set(z.namelist())
    n = 0                                   # .dds.n holds the full size, .dds.(n-i) the size >> i
    while name + '.dds.%d' % (n + 1) in names:
        n += 1
    i = int(np.log2(w // size))
    data = z.read(name + '.dds.%d' % (n - i))
    nh = bytearray(hdr[:148 if dx10 else 128])
    struct.pack_into('<II', nh, 12, h >> i, w >> i)
    struct.pack_into('<I', nh, 28, 1)
    struct.pack_into('<I', nh, 8, struct.unpack_from('<I', nh, 8)[0] & ~0x20000)
    struct.pack_into('<I', nh, 20, len(data))
    if dx10:
        fmt = struct.unpack_from('<I', nh, 128)[0]
        if fmt in (72, 78, 99):              # *_SRGB -> the plain format (Pillow decodes those)
            struct.pack_into('<I', nh, 128, fmt - 1)
    im = Image.open(io.BytesIO(bytes(nh) + data))
    im.load()
    return np.asarray(im.convert('RGBA')).astype(np.float32) / 255.0


def save(name, a):
    rgba = np.dstack([np.broadcast_to(TINT, a.shape + (3,)), a])
    px = (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8)
    Image.fromarray(px, 'RGBA').save(os.path.join(out, name + '.png'))
    Image.fromarray(px, 'RGBA').save(os.path.join(out, name + '.tif'), compression='tiff_lzw')
    print(name, a.shape, 'alpha mean %.3f' % a.mean())


os.makedirs(out, exist_ok=True)
with zipfile.ZipFile(PAK) as z:
    save('imp_w3_jet_diff', mip(z, 'Textures/particles/water_splash_02_diff', 2048)[..., 3])
    save('imp_w3_crown_diff', mip(z, 'Textures/particles/water_splash_03_diff', 512)[..., 3])
    save('imp_w3_burst_diff', mip(z, 'Textures/particles/water_splash_diff', 1024)[..., 3])
