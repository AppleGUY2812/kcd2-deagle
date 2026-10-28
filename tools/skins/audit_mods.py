#!/usr/bin/env python3
"""Audit KCD2 mod paks for expensive DDS layouts and cross-mod texture conflicts.

Scans Mods/*/Data and Workshop content/1771300/*/data without modifying them.
Only ZIP metadata and DDS headers are read. Run under WSL with:

    nice -n 19 python3 tools/skins/audit_mods.py
"""

import argparse
import struct
import sys
import zipfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

MODS_ROOT = Path("/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Mods")
WORKSHOP_ROOT = Path("/mnt/d/steam_bruh/steamapps/workshop/content/1771300")


@dataclass
class Texture:
    mod_id: str
    mod_name: str
    pak: str
    path: str
    key: str
    size: int
    width: int
    height: int
    mips: int
    fourcc: str
    dxgi: int | None
    reasons: list[str] = field(default_factory=list)


@dataclass
class ModReport:
    name: str
    textures: list[Texture] = field(default_factory=list)

    @property
    def flagged(self):
        return [texture for texture in self.textures if texture.reasons]

    @property
    def flagged_mb(self):
        return sum(texture.size for texture in self.flagged) / 1e6


def texture_key(path):
    key = path.replace("\\", "/").casefold()
    while key.startswith("./"):
        key = key[2:]
    return key.lstrip("/")


def read_header(archive, info):
    with archive.open(info) as stream:
        header = stream.read(148)
    if len(header) < 128 or header[:4] != b"DDS ":
        raise ValueError("not a complete DDS header")
    height, width = struct.unpack_from("<II", header, 12)
    mips = struct.unpack_from("<I", header, 28)[0]
    fourcc = header[84:88].decode("ascii", errors="replace")
    dxgi = None
    if fourcc == "DX10":
        if len(header) < 148:
            raise ValueError("truncated DX10 header")
        dxgi = struct.unpack_from("<I", header, 128)[0]
    return width, height, mips, fourcc, dxgi


def iter_paks(root, data_name):
    if not root.is_dir():
        print(f"warning: mod root not found: {root}", file=sys.stderr)
        return
    # the folder is "Data" or "data" (Mods/deagle: data, Workshop: Data); WSL paths are case-sensitive
    for data_dir in sorted(d for d in root.glob("*/*") if d.is_dir() and d.name.casefold() == data_name.casefold()):
        for pak in sorted(data_dir.iterdir()):
            if pak.is_file() and pak.suffix.casefold() == ".pak":
                yield pak


def scan_pak(pak, reports, path_owners):
    mod_dir = pak.parent.parent
    mod_id = str(mod_dir)
    report = reports.setdefault(mod_id, ModReport(mod_dir.name))
    with zipfile.ZipFile(pak) as archive:
        infos = [info for info in archive.infolist() if not info.is_dir()]
        names = {texture_key(info.filename) for info in infos}
        for info in infos:
            if not info.filename.casefold().endswith(".dds"):
                continue
            key = texture_key(info.filename)
            try:
                width, height, mips, fourcc, dxgi = read_header(archive, info)
            except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as exc:
                print(f"warning: {pak}: {info.filename}: {exc}", file=sys.stderr)
                continue
            texture = Texture(
                mod_id, mod_dir.name, pak.name, info.filename, key, info.file_size,
                width, height, mips, fourcc, dxgi,
            )
            largest_edge = max(width, height)
            if largest_edge >= 1024 and mips == 1:
                texture.reasons.append("no mipmaps")
            if largest_edge >= 2048 and key + ".1" not in names:
                texture.reasons.append("stored whole")
            report.textures.append(texture)
            path_owners[key].add(mod_id)


def scan(roots):
    reports = {}
    path_owners = defaultdict(set)
    pak_count = 0
    for root, data_name in roots:
        for pak in iter_paks(root, data_name):
            pak_count += 1
            try:
                scan_pak(pak, reports, path_owners)
            except (OSError, RuntimeError, zipfile.BadZipFile) as exc:
                print(f"warning: cannot scan {pak}: {exc}", file=sys.stderr)
    for report in reports.values():
        for texture in report.textures:
            others = path_owners[texture.key] - {texture.mod_id}
            if others:
                names = ", ".join(sorted(reports[mod_id].name for mod_id in others))
                texture.reasons.append(f"conflict: {names}")
    return reports, pak_count


def format_texture(texture):
    dimensions = f"{texture.width}x{texture.height}"
    fmt = f"{texture.fourcc}/DXGI {texture.dxgi}" if texture.dxgi is not None else texture.fourcc
    reasons = "; ".join(texture.reasons)
    return f"{texture.path} [{dimensions}, {fmt}, {texture.size / 1e6:.1f} MB: {reasons}]"


def print_report(reports, pak_count, worst_count):
    texture_count = sum(len(report.textures) for report in reports.values())
    flagged_count = sum(len(report.flagged) for report in reports.values())
    print(f"Audited {pak_count} paks and {texture_count} DDS files in {len(reports)} mods; "
          f"flagged {flagged_count} DDS files. MB is decimal and counts flagged base .dds files only.")
    mod_width = max([len("Mod")] + [len(report.name) for report in reports.values()])
    print(f"\n{'Mod':<{mod_width}}  {'Flagged':>7}  {'MB':>10}  Worst files")
    print(f"{'-' * mod_width}  {'-' * 7}  {'-' * 10}  {'-' * 11}")
    for mod_id in sorted(reports, key=lambda item: reports[item].name.casefold()):
        report = reports[mod_id]
        flagged = sorted(
            report.flagged,
            key=lambda texture: (max(texture.width, texture.height), texture.size, texture.path.casefold()),
            reverse=True,
        )
        limit = len(flagged) if worst_count == 0 else worst_count
        worst = [format_texture(texture) for texture in flagged[:limit]] or ["-"]
        for index, details in enumerate(worst):
            name = report.name if index == 0 else ""
            count = str(len(flagged)) if index == 0 else ""
            mb = f"{report.flagged_mb:.1f}" if index == 0 else ""
            print(f"{name:<{mod_width}}  {count:>7}  {mb:>10}  {details}")
        if len(flagged) > limit:
            print(f"{'':<{mod_width}}  {'':>7}  {'':>10}  ... and {len(flagged) - limit} more")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worst", type=int, default=5, metavar="N",
                        help="show up to N worst files per mod (default: 5; 0 shows all)")
    args = parser.parse_args()
    if args.worst < 0:
        parser.error("--worst must be zero or greater")
    reports, pak_count = scan(((MODS_ROOT, "Data"), (WORKSHOP_ROOT, "data")))
    if not pak_count:
        print("No .pak files found.", file=sys.stderr)
        return 1
    print_report(reports, pak_count, args.worst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
