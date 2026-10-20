# Blender: make the KCD2 toolkit's DAE export keep a mesh's custom split normals.
#   import bcry_normals; bcry_normals.install()     (before bpy.ops.bcry.export_to_game)
# The toolkit (bcry_exporter export.py _write_normals -> utils.get_custom_normals / get_normal_array) always recomputes
# area-weighted face normals from the bmesh, so normals set with normals_split_custom_set* never reach the game. Ready or
# Not's Grizzly: the exported DAE matched that recompute to 0.00 deg median, while RoN's own normals (which its normal
# map was baked against) differ by > 5 deg on 38 % of the gun's vertices (max 60): faceted shading in KCD2, smooth in
# RoN (the user, 2026-10-20). This writes mesh.corner_normals instead, for meshes that have custom normals only; any
# other mesh keeps the toolkit's own normals byte for byte. The toolkit indexes normals per face corner in bmesh face
# order (one running index, face.verts order), which bmesh.from_mesh keeps from the mesh's polygons and loops.
import addon_utils


def _exporter():
    for mod in addon_utils.modules():
        if mod.__name__.endswith("KCD2_Blender_Toolkit"):
            import importlib
            return importlib.import_module(mod.__name__ + ".bcry_exporter.export")
    raise SystemExit("bcry_normals: the KCD2 Blender toolkit isn't loaded")


def corner_normals(me):
    """the mesh's per-corner normals, flat [x, y, z, ...] in polygon / loop order (what the toolkit writes)"""
    cn = me.corner_normals
    out = []
    for poly in me.polygons:
        for li in poly.loop_indices:
            out.extend(cn[li].vector)
    return out


def install():
    ex = _exporter()
    cls = ex.CrytekDaeExporter
    if getattr(cls, "_bcry_normals", False):
        return
    orig = cls._write_normals
    utils = ex.utils

    def _write_normals(self, object_, bmesh_, mesh_node, geometry_name):
        me = object_.data
        if not me.has_custom_normals:
            return orig(self, object_, bmesh_, mesh_node, geometry_name)
        if len(me.polygons) != len(bmesh_.faces) or len(me.loops) != sum(len(f.verts) for f in bmesh_.faces):
            raise SystemExit(f"bcry_normals: {object_.name}: the exported mesh isn't the object's own "
                             f"({len(bmesh_.faces)} faces vs {len(me.polygons)})")
        nrm = corner_normals(me)
        source = utils.write_source(f"{geometry_name}-normal", "float", nrm, "XYZ")
        mesh_node.appendChild(source)
        print(f"BCRY NORMALS {object_.name}: {len(nrm) // 3} custom corner normals")

    cls._write_normals = _write_normals
    cls._bcry_normals = True
