"""Patch Henry's DAE animation locals into the coordinate frame established by the unchanged gun tracks."""
import math
import os
import sys
import xml.etree.ElementTree as ET

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_binds
import check_chr
import check_caf

NS = 'http://www.collada.org/2005/11/COLLADASchema'
ET.register_namespace('', NS)
Q = lambda tag: '{%s}%s' % (NS, tag)


def transform_animation_matrix(matrix):
    euler = matrix.to_euler()
    euler.rotate_axis('Z', math.pi / 2)
    euler.rotate_axis('X', math.pi)
    out = euler.to_matrix().to_4x4()
    out.translation = matrix.translation
    return out


def patch(dae_path, arm, action, chr_path, anchor_caf=None):
    check_binds.GAME_BODY = r'E:\Tools\deagle-work\henry\Objects\characters\humans\male\body'
    henry = set(check_binds.game_binds())
    bones = check_caf.chr_bones(open(chr_path, 'rb').read())
    by_name = {b['name']: i for i, b in enumerate(bones)}
    if not henry <= set(by_name):
        raise RuntimeError('Henry joints missing from compiled CHR')
    compiled_tracks = None
    anchor_world = None
    anchor_shift = None
    if anchor_caf:
        tracks, _ = check_caf.caf_tracks(open(anchor_caf, 'rb').read())
        compiled_tracks = set(tracks)
        anchor_world = Matrix(check_caf.reconstruct(bones, tracks)[by_name['J_Gun']])
    bone_to_joint = Matrix(((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, -1, 0), (0, 0, 0, 1)))
    rc_yaw = Matrix.Rotation(math.radians(180), 4, 'Z')
    translation_frame = rc_yaw @ bone_to_joint
    f0, f1 = (int(v) for v in action.frame_range)
    arm.animation_data.action = action
    wanted = {}
    for frame in range(f0, f1 + 1):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        converted = {pb.name: rc_yaw @ bone_to_joint @ arm.matrix_world @ pb.matrix
                     for pb in arm.pose.bones}
        transformed = {pb.name: transform_animation_matrix(pb.matrix) for pb in arm.pose.bones}
        local = {}
        for pb in arm.pose.bones:
            matrix = transformed[pb.name]
            if pb.parent and pb.parent.parent:
                matrix = transformed[pb.parent.name].inverted() @ matrix
            elif pb.name == 'Locator_Locomotion':
                matrix = pb.matrix.copy()
            elif not pb.parent:
                matrix = Matrix.Identity(4)
            local[pb.name] = matrix
        gun_world = []
        for i, bone in enumerate(bones):
            parent = bone['parent']
            matrix = local.get(bone['name'])
            if compiled_tracks is not None and bone['crc'] not in compiled_tracks:
                matrix = None
            if matrix is None:
                matrix = Matrix(bone['default'])
                if parent is not None:
                    matrix = Matrix(bones[parent]['default']).inverted() @ matrix
            gun_world.append(matrix if parent is None else gun_world[parent] @ matrix)
        anchor = by_name['J_Gun']
        if anchor_world is not None and frame == f0:
            anchor_shift = anchor_world.translation - gun_world[anchor].translation
        # Only the anchor moves to the compiled J_Gun. Henry's parents (root) keep their simulated world, which is
        # what the compiled clip uses; shifting them too put every Henry joint off by anchor_shift (434 mm).
        anchor_pos = gun_world[anchor].translation + (anchor_shift if anchor_shift is not None else Vector())
        offset = anchor_pos - translation_frame @ converted['J_Gun'].translation
        desired = {}
        for name in henry:
            matrix = converted[name].copy()
            matrix.translation = translation_frame @ matrix.translation + offset
            desired[name] = matrix
        wanted[frame] = {}
        for name in henry:
            bone = bones[by_name[name]]
            parent = bone['parent']
            parent_world = desired[bones[parent]['name']] if bones[parent]['name'] in desired else gun_world[parent]
            wanted[frame][name] = parent_world.inverted() @ desired[name]

    tree = ET.parse(dae_path)
    root = tree.getroot()
    sources = {s.get('id'): s for s in root.findall('.//' + Q('source'))}
    outputs = {}
    for sampler in root.findall('.//' + Q('sampler')):
        output = next(i for i in sampler.findall(Q('input')) if i.get('semantic') == 'OUTPUT')
        outputs['#' + sampler.get('id')] = sources[output.get('source')[1:]].find(Q('float_array'))
    channels = {c.get('target'): outputs[c.get('source')] for c in root.findall('.//' + Q('channel'))}
    for node in root.findall('.//' + Q('node')):
        name = node.get('id', '').split('%', 1)[0]
        if name not in henry:
            continue
        nid = node.get('id')
        elements = {}
        for element in node:
            if element.get('sid') in ('translation', 'rotation_z', 'rotation_y', 'rotation_x'):
                elements[element.get('sid')] = element
        for frame in range(f0, f1 + 1):
            matrix = wanted[frame][name]
            location = matrix.translation
            rotation = matrix.to_euler('XYZ')
            values = {'translation.X': location.x, 'translation.Y': location.y, 'translation.Z': location.z,
                      'rotation_X.ANGLE': math.degrees(rotation.x),
                      'rotation_Y.ANGLE': math.degrees(rotation.y),
                      'rotation_Z.ANGLE': math.degrees(rotation.z)}
            for suffix, value in values.items():
                target = nid + '/' + suffix
                if target in channels:
                    array = channels[target]
                    numbers = array.text.split()
                    numbers[frame - f0] = '%.9f' % value
                    array.text = ' '.join(numbers)
                elif frame == f0:
                    raise RuntimeError('missing DAE animation channel ' + target)
            if frame == f0:
                elements['translation'].text = '%.9f %.9f %.9f' % tuple(location)
                for axis in ('x', 'y', 'z'):
                    elements['rotation_' + axis].text = '%s %s %s %.9f' % (
                        '1' if axis == 'x' else '0', '1' if axis == 'y' else '0', '1' if axis == 'z' else '0',
                        math.degrees(getattr(rotation, axis)))
    tree.write(dae_path, encoding='utf-8', xml_declaration=True)
    print('HENRY ANIM LOCALS', os.path.basename(dae_path), len(henry), 'joints', f0, f1, flush=True)
