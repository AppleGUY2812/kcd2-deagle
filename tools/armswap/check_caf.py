#!/usr/bin/env python3
"""Decode the first CAF frame and reconstruct joint-to-model matrices through the compiled CHR hierarchy.

Compare Henry's idle against a pose dumped from its Blender source, or validate the decoder on a game animation:
  python3 tools/armswap/check_caf.py --validate-game
  python3 tools/armswap/check_caf.py --intended /mnt/e/Tools/deagle-work/guns/ron_grizzly_henry/idle_pose.json

Dump the intended pose from the source blend (Idle priority, three Blender threads):
  blender -b ron_grizzly_henry_vm.blend -t 3 -P tools/armswap/check_caf.py -- --dump-blend idle_pose.json
"""
import argparse
import json
import math
import os
import struct
import sys
import zipfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHR = os.path.join(REPO, 'src', 'Objects', 'manmade', 'weapons', 'guns', 'ron_grizzly_henry',
                   'ron_grizzly_henry_vm.chr')
CAF = os.path.join(REPO, 'src', 'Animations', 'guns', 'ron_grizzly_henry_vm',
                   'ron_grizzly_henry_vm_idle.caf')
INTENDED = '/mnt/e/Tools/deagle-work/guns/ron_grizzly_henry/idle_pose.json'
GAME_DIRS = ['/mnt/d/steam_bruh/steamapps/common/KingdomComeDeliverance2/Data',
             '/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data']
GAME_CHR = 'objects/characters/humans/male/skeleton/male.chr'
GAME_CAF = 'animations/humans/male/aimposes/rifle_aimposes_aim_upperbody_player.caf'


def chunks(data):
    _, count, table = struct.unpack_from('<III', data, 4)
    return [struct.unpack_from('<HHIII', data, table + i * 16) for i in range(count)]


def chr_bones(data):
    _, _, _, size, offset = next(c for c in chunks(data) if c[0] == 0x2000)
    out = []
    for i in range((size - 32) // 584):
        b = offset + 32 + i * 584
        name = data[b + 312:b + 568].split(b'\0')[0].decode()
        values = struct.unpack_from('<12f', data, b + 264)
        matrix = np.array([values[:4], values[4:8], values[8:12], [0, 0, 0, 1]], float)
        parent_offset = struct.unpack_from('<i', data, b + 572)[0]
        out.append({'name': name, 'default': matrix,
                    'parent': i + parent_offset if parent_offset else None,
                    'crc': struct.unpack_from('<I', data, b)[0]})
    return out


def quaternion(data, offset, fmt):
    if fmt == 1:
        q = np.array(struct.unpack_from('<4f', data, offset))
        return q, offset + 16
    if fmt == 5:
        a, b, c = struct.unpack_from('<3H', data, offset)
        packed, bits, index_shift, offset = a | (b << 16) | (c << 32), 15, 46, offset + 6
    elif fmt == 8:
        a, b = struct.unpack_from('<2I', data, offset)
        packed, bits, index_shift, offset = a | (b << 32), 20, 62, offset + 8
    else:
        raise ValueError('unsupported rotation format %d' % fmt)
    largest = packed >> index_shift
    if largest > 3:
        raise ValueError('invalid largest quaternion component %d' % largest)
    q, shift, squares, component = np.zeros(4), 0, 0.0, 0
    for i in range(4):
        if i == largest:
            continue
        if fmt == 8 and component < 2:
            bits, scale = 21, 1482909.0
        else:
            bits = 15 if fmt == 5 else 20
            scale = 23170.0 if fmt == 5 else 741454.0
        q[i] = ((packed >> shift) & ((1 << bits) - 1)) / scale - math.sqrt(0.5)
        squares += q[i] * q[i]
        shift += bits
        component += 1
    q[largest] = math.sqrt(max(0.0, 1.0 - squares))
    return q, offset


def quaternion_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)],
                     [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)],
                     [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)]])


def caf_tracks(data):
    tracks, quat_norms = {}, []
    for kind, version, _, size, offset in chunks(data):
        if kind != 0x100d:
            continue
        if version != 0x829:
            raise ValueError('unsupported controller version 0x%x' % version)
        crc, nr, np_, rf, rt, pf, _, pt, aligned = struct.unpack_from('<IHH6B', data, offset)
        p = offset + 16
        rotations = []
        for _ in range(nr):
            q, p = quaternion(data, p, rf)
            rotations.append(q)
            quat_norms.append(float(np.linalg.norm(q)))
        if aligned:
            p = (p + 3) & ~3
        time_sizes = {0: 4, 1: 2, 2: 1, 3: 4, 4: 2, 5: 1, 6: 2}
        if rt not in time_sizes:
            raise ValueError('unsupported rotation time format %d' % rt)
        p += nr * time_sizes[rt]
        if aligned and np_:
            p = (p + 3) & ~3
        positions = []
        for _ in range(np_):
            if pf not in (0, 2):
                raise ValueError('unsupported position format %d' % pf)
            positions.append(np.array(struct.unpack_from('<3f', data, p)))
            p += 12
        if p > offset + size:
            raise ValueError('controller 0x%08x overruns its chunk' % crc)
        tracks[crc] = (rotations[0] if rotations else None, positions[0] if positions else None)
    return tracks, quat_norms


def reconstruct(bones, tracks):
    out = []
    for bone in bones:
        parent = bone['parent']
        local = bone['default'].copy() if parent is None else np.linalg.inv(bones[parent]['default']) @ bone['default']
        q, position = tracks.get(bone['crc'], (None, None))
        if q is not None:
            local[:3, :3] = quaternion_matrix(q)
        if position is not None:
            local[:3, 3] = position
        out.append(local if parent is None else out[parent] @ local)
    return out


def game_entry(entry):
    for directory in GAME_DIRS:
        path = os.path.join(directory, 'Animations.pak' if entry == GAME_CAF else 'IPL_Characters-part3.pak')
        if not os.path.exists(path):
            continue
        with zipfile.ZipFile(path) as pak:
            hit = next((n for n in pak.namelist() if n.replace('\\', '/').lower() == entry), None)
            if hit:
                return pak.read(hit)
    raise FileNotFoundError(entry)


def validate_game():
    bones = chr_bones(game_entry(GAME_CHR))
    tracks, norms = caf_tracks(game_entry(GAME_CAF))
    pose = np.array(reconstruct(bones, tracks))
    finite = bool(np.isfinite(pose).all())
    positions = pose[:, :3, 3]
    heights = positions[:, 2]
    radius = float(np.linalg.norm(positions, axis=1).max())
    print('game validation: %d tracks, %d joints, finite=%s' % (len(tracks), len(bones), finite))
    print('quaternion norm: %.9f .. %.9f' % (min(norms), max(norms)))
    print('human pose: z %.3f .. %.3f m, max distance from origin %.3f m' % (heights.min(), heights.max(), radius))
    by_name = {b['name']: i for i, b in enumerate(bones)}
    for name in ('Human_Male', 'RightArm', 'RightForeArm', 'RightHand', 'RightHandIndex1'):
        print('%-20s %s mm' % (name, np.array2string(positions[by_name[name]] * 1000, precision=3)))
    ok = finite and max(abs(n - 1) for n in norms) < 1e-5 and heights.min() > -0.5 and heights.max() < 3 and radius < 4
    print('game decoder validation:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


def error_rows(bones, actual, intended, names):
    by_name = {b['name']: i for i, b in enumerate(bones)}
    rows = []
    for name in names:
        i = by_name[name]
        error = actual[i] @ np.linalg.inv(intended[name])
        pos = float(np.linalg.norm(actual[i][:3, 3] - intended[name][:3, 3])) * 1000
        rotation = error[:3, :3]
        det = float(np.linalg.det(rotation))
        if det > 0:
            rotation /= det ** (1 / 3)
        degrees = float(np.degrees(np.arccos(np.clip((np.trace(rotation) - 1) / 2, -1, 1))))
        parent = bones[bones[i]['parent']]['name'] if bones[i]['parent'] is not None else '-'
        rows.append((pos, degrees, name, parent))
    return rows


def compare(intended_path, caf_path):
    bones = chr_bones(open(CHR, 'rb').read())
    tracks, _ = caf_tracks(open(caf_path, 'rb').read())
    actual = reconstruct(bones, tracks)
    intended = {n: np.array(m, float).reshape(4, 4) for n, m in json.load(open(intended_path)).items()}
    by_name = {b['name']: i for i, b in enumerate(bones)}
    axis = np.array([[0, -1, 0], [-1, 0, 0], [0, 0, -1]], float)
    offset = actual[by_name['J_Gun']][:3, 3] - axis @ intended['J_Gun'][:3, 3]
    for matrix in intended.values():
        matrix[:3, 3] = axis @ matrix[:3, 3] + offset
    controls = ('J_Gun', 'tag_slide', 'tag_slidelock', 'tag_mag_01', 'tag_muzzle', 'tag_brass', 'tag_barrel')
    gun = error_rows(bones, actual, intended, controls)
    print('gun control: max pos %.3f mm, max rot %.3f deg' % (max(r[0] for r in gun), max(r[1] for r in gun)))
    import check_chr
    henry_names = set(check_chr.game_chr()) & set(by_name)
    rows = error_rows(bones, actual, intended, henry_names)
    print('%s: %d Henry joints; ten worst by position error' % (os.path.basename(caf_path), len(rows)))
    for pos, degrees, name, parent in sorted(rows, reverse=True)[:10]:
        print('%-24s pos %9.3f mm  rot %8.3f deg  parent %s' % (name, pos, degrees, parent))
    print('max pos %.3f mm (%s), max rot %.3f deg (%s)' % (
        max(rows)[0], max(rows)[2], max(rows, key=lambda r: r[1])[1], max(rows, key=lambda r: r[1])[2]))
    ok = max(r[0] for r in rows) <= 1 and max(r[1] for r in rows) <= 0.5
    ok = ok and max(r[0] for r in gun) <= 0.1 and max(r[1] for r in gun) <= 0.1
    print('compiled CAF comparison:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


def dump_blend(path, reload_path=None):
    import bpy
    from mathutils import Matrix
    arm = max((o for o in bpy.context.scene.objects if o.type == 'ARMATURE'), key=lambda o: len(o.data.bones))
    rotate = Matrix.Rotation(math.radians(90), 4, 'Z')
    bone_to_joint = Matrix(((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, -1, 0), (0, 0, 0, 1)))
    rc_yaw = Matrix.Rotation(math.radians(180), 4, 'Z')

    def dump(names, frame, output):
        action = next((bpy.data.actions.get(n) for n in names if bpy.data.actions.get(n)), None)
        if action is None:
            raise RuntimeError('action not found: ' + '/'.join(names))
        arm.animation_data.action = action
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        result = {}
        for bone in arm.pose.bones:
            matrix = rc_yaw @ bone_to_joint @ rotate @ arm.matrix_world @ bone.matrix
            result[bone.name] = [matrix[r][c] for r in range(4) for c in range(4)]
        with open(output, 'w') as f:
            json.dump(result, f)
        print('CAF intended pose dumped:', action.name, frame, len(result), 'joints,', output)

    dump(('grizzly_idle', '@grizzly_idle', '@idle'), 1, path)
    if reload_path:
        dump(('grizzly_reload_tac', '@grizzly_reload_tac', '@reload'), 1, reload_path)


def main():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    if args and args[0] == '--dump-blend':
        dump_blend(*args[1:])
        return 0
    parser = argparse.ArgumentParser()
    parser.add_argument('--validate-game', action='store_true')
    parser.add_argument('--intended', default=INTENDED)
    parser.add_argument('--caf', default=CAF)
    options = parser.parse_args(args)
    if options.validate_game:
        return validate_game()
    return compare(options.intended, options.caf)


if __name__ == '__main__':
    sys.exit(main())
