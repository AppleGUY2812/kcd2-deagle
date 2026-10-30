"""Repatch Henry animation locals against the first-pass compiled gun pose."""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import animation_pose

gun = sys.argv[sys.argv.index('--') + 1]
with open(os.path.join(gun, 'build.json')) as f:
    info = json.load(f)
bpy.ops.wm.open_mainfile(filepath=os.path.join(gun, info['slug'] + '_vm_roles.blend'))
arm = max((o for o in bpy.context.scene.objects if o.type == 'ARMATURE'), key=lambda o: len(o.data.bones))
arm.matrix_world = Matrix.Rotation(math.radians(90), 4, 'Z') @ arm.matrix_world
chr_path = os.path.join(gun, 'build', 'model', info['slug'] + '_vm.chr')
for role, clip in info['clips'].items():
    action_role = role
    if action_role in ('shoot_add', 'shoot_empty_add'):
        action_role = action_role[:-4]
    action = bpy.data.actions['@' + action_role]
    dae = os.path.join(gun, 'build', 'anims', clip['name'] + '.dae')
    caf = os.path.join(gun, 'build', 'caf', clip['name'] + '.caf')
    animation_pose.patch(dae, arm, action, chr_path, anchor_caf=caf)
print('HENRY ANIMATION REPACHED', len(info['clips']), 'clips', flush=True)
