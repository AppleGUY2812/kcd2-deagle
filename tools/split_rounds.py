# Blender helper (tools/vm_export.py, tools/gb/gb_build.py): splits a magazine's rounds object into one object per
# round, top of the magazine first, so the script can show as many rounds as the magazine holds. The rounds are loose
# parts (case, bullet, primer...) stacked along the magazine.
import bpy
from mathutils import Vector


def split_rounds(obj, top_point, name_fmt, gap=0.006):
    """obj: the rounds mesh; top_point: a world point at the top of the magazine (the chamber, the slide);
    name_fmt: e.g. "deagle_vm_round_{}"; gap: metres between two rounds' heights in the stack (a .50 AE is 12.9 mm)."""
    bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True); bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.mesh.separate(type="LOOSE")
    bpy.ops.object.mode_set(mode="OBJECT")
    parts = list(bpy.context.selected_objects)
    centre = lambda o: sum((o.matrix_world @ v.co for v in o.data.vertices), Vector()) / max(1, len(o.data.vertices))
    cs = [(o, centre(o)) for o in parts]
    mean = sum((c for _, c in cs), Vector()) / len(cs)
    axis = Vector((0, 0, 1))
    if len(cs) > 1:                     # the stack runs along the direction the part centres spread most
        import numpy as np
        m = np.array([list(c - mean) for _, c in cs])
        axis = Vector(np.linalg.eigh(m.T @ m)[1][:, -1]).normalized()
    if (top_point - mean).dot(axis) < 0:
        axis = -axis
    # every round is built from the same parts, so each part size occurs once per round (or k times): the gcd of how
    # often the sizes occur is the round count. Each part type's copies, sorted down the stack, belong to round 1, 2...
    # in that order (by height alone, tilted rounds overlap their neighbours; nearest-case is close to ambiguous)
    from collections import Counter, defaultdict
    from math import gcd
    from functools import reduce
    sizes = Counter(len(o.data.vertices) for o, _ in cs)
    n = reduce(gcd, sizes.values())
    if n > 1:
        by_size = defaultdict(list)
        for o, c in cs:
            by_size[len(o.data.vertices)].append((o, c))
        groups = [[0, []] for _ in range(n)]
        for members in by_size.values():
            members.sort(key=lambda oc: -(oc[1] - mean).dot(axis))     # top of the magazine first
            k = len(members) // n
            for i in range(n):
                groups[i][1].extend(o for o, _ in members[i * k:(i + 1) * k])
    else:                               # parts don't repeat: one piece per height band
        cs.sort(key=lambda oc: -(oc[1] - mean).dot(axis))
        groups = []
        for o, c in cs:
            h = (c - mean).dot(axis)
            if groups and groups[-1][0] - h < gap:
                groups[-1][1].append(o); groups[-1][0] = h
            else:
                groups.append([h, [o]])
    out = []
    for i, (_, objs) in enumerate(groups, 1):
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        if len(objs) > 1:
            bpy.ops.object.join()
        r = bpy.context.view_layer.objects.active
        r.name = name_fmt.format(i)
        out.append(r)
    print("ROUNDS split into", len(out), "rounds, top first, verts", [len(o.data.vertices) for o in out])
    return out
