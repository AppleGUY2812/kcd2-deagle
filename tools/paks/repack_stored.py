#!/usr/bin/env python3
"""Repack a mod pak with stored (uncompressed) entries, same files, same order, same bytes.

Why: KCD2 streams a texture's mips by reading ranges of its .dds. In a deflated entry a range can't be reached without
inflating from the start, so every mip request re-reads and re-inflates the whole file (boot 37: the two Checkers mods'
4K .dds files read 5-9 times each, 90 % of the loading screen's disk time; scratchpad re_texture_early.md). Stored
entries are read by range. Vanilla paks stay under ~2.0 GB, so the output is split into <name>.pak, <name>-part1.pak ...
under --max-part bytes (no zip64).

usage: repack_stored.py SRC.pak OUTDIR [--max-part 1900000000]
Writes OUTDIR/<name>.pak (+ -partN.pak), then re-reads every output entry and checks its CRC and size against the source.
Never touches SRC. Exit 0 = all entries verified.
"""
import argparse
import os
import sys
import zipfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("outdir")
    ap.add_argument("--max-part", type=int, default=1_900_000_000)
    a = ap.parse_args()
    base = os.path.splitext(os.path.basename(a.src))[0]
    os.makedirs(a.outdir, exist_ok=True)
    src = zipfile.ZipFile(a.src)
    infos = [i for i in src.infolist() if not i.is_dir()]
    parts, cur, cur_size, out = [], None, 0, {}

    def open_part():
        n = len(parts)
        path = os.path.join(a.outdir, f"{base}.pak" if n == 0 else f"{base}-part{n}.pak")
        if os.path.exists(path):
            sys.exit(f"refusing to overwrite {path}")
        parts.append(path)
        return zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED, allowZip64=False)

    for i in infos:
        need = i.file_size + 30 + 46 + 2 * len(i.filename.encode()) + 64
        if need > a.max_part:
            sys.exit(f"{i.filename}: {i.file_size} B doesn't fit a part")
        if cur is None or cur_size + need > a.max_part:
            if cur:
                cur.close()
            cur, cur_size = open_part(), 0
        data = src.read(i)   # checks the source CRC
        z = zipfile.ZipInfo(i.filename, date_time=i.date_time)
        z.compress_type = zipfile.ZIP_STORED
        z.external_attr = i.external_attr
        cur.writestr(z, data)
        cur_size += need
        out[i.filename] = (len(parts) - 1, i.CRC, i.file_size)
    if cur:
        cur.close()

    # verify: every source entry once, same CRC and size, stored, nothing else
    seen = 0
    for n, path in enumerate(parts):
        with zipfile.ZipFile(path) as z:
            for i in z.infolist():
                p, crc, size = out.get(i.filename, (None, None, None))
                if p != n or i.CRC != crc or i.file_size != size or i.compress_type != zipfile.ZIP_STORED:
                    sys.exit(f"VERIFY FAIL {path}: {i.filename}")
                with z.open(i) as f:   # reads it all and checks the CRC
                    while f.read(1 << 22):
                        pass
                seen += 1
        if os.path.getsize(path) >= 2 ** 31:
            sys.exit(f"VERIFY FAIL {path}: over 2 GB")
    if seen != len(infos):
        sys.exit(f"VERIFY FAIL: {seen} entries written, {len(infos)} in the source")
    total = sum(os.path.getsize(p) for p in parts)
    print(f"repacked {len(infos)} entries into {len(parts)} pak(s), {total / 2**20:.0f} MB, all CRCs verified:")
    for p in parts:
        print(f"  {p} {os.path.getsize(p) / 2**20:.0f} MB")


if __name__ == "__main__":
    main()
