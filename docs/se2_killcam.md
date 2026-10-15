# Sniper Elite V2 Remastered: how its kill cam is built (reference for ours)

Private-use reference. Read from the user's copy (D:\Games\Sniper Elite V2 Remastered); nothing from it is in this repo.
Tool: `tools/se2/asr_unpack.py` (`.asr` = "AsuraZlb": 8-byte magic, u32 0, u32 compressed size, u32 uncompressed size,
one zlib stream). `python3 tools/se2/asr_unpack.py <file.asr> --find <text> [n]`.

## What the program says (bin/SEV2_Remastered.exe, strings)
A kill cam is a parameter set, with a score so the game picks the best one (`.MinScore`, `.MaxScore`, `.Weight`):
- Camera: `Camera.Angle.Initial/Final`, `Camera.Offset.Initial/Final`, `Camera.FieldOfView.Initial/Final` (the camera
  eases from the first to the second), `Camera.LookAtBullet`, `Camera.TrackBullet`.
- Time: `TimeScale.PreHit` (bullet in flight), `TimeScale.Internal` (inside the body), `TimeScale.PostExit` (leaving it);
  `Duration`, `Duration.ImpactTime`, `Duration.ExitTime`.
- `InheritCamera`, `InheritTimeScale`, `InheritDuration`: a cam can build on another.
- X-ray: a sphere pass (`XRaySphereConstants*`, `XRayPrimitiveConstants`; `g_xAlbedoModifierXRay`, SH lighting
  constants for X-ray): the body is shown through a sphere round the bullet.
- Bullet camera target: `BulletCam_Target` on every scope (`m1_carbine`, `lee_enfield_no3_scope`, ...).

## What the data has (misc/Common.asr, unpacked 403 MB)
Full-screen effects `Bullet\Bullet_Camera_FSFX` and `Bullet\Bullet_KillCamera_FSFX` (.fsx), a depth-of-field mask
`graphics\specialfx\pfx\hud\killcam_dof_mask.tga`, and particle effects under `Impacts\Blood\Kill_Cam\`:
`Bullet_KillCam_Impact_Body_Entry`, `_Internal` (bones) and `_Internal_noBones`, `Blood_KillCam_Heart`,
`Blood_KillCam_Lung`, `Bullet_KillCam_Impact_intestines`, `Bullet_KillCam_NutBlood`, `Bullet_KillCam_Impact_Body_Exit`,
`klllcam_BloodBlob_trail`, `klllcam_BloodTeeth_trail`; and `Impacts\Explosions\Grenade_KillCam_Explosion`.
So the look = slow motion + the camera on the bullet + an X-ray sphere + one effect per organ the bullet crosses, in
order (entry, bones or not, heart or lung, intestines, exit), blood and teeth trails on a head shot.

## The numbers
Property records in the `.asr` / `.pc` archives are `[hash u32][3][0][0][1][float]`, hash = seed*31^n + H0(name) over the
lowercased name (the program: 0x3f1000 in bin/SEV2_Remastered.exe; `tools/se2/propfind.py`, `killcams.py`, `xref.py`).
Found, in `misc/Common.asr` at seed 0 (the default cam):

| property | value |
|---|---|
| Camera.Offset.Initial / Final | -1.1 / -2 (m, behind the bullet) |
| Camera.FieldOfView.Initial / Final | 10 / 30 |
| Duration | 2 s |
| Duration.ImpactTime / ExitTime | 1 s / 1 s |

Not found in any of the 97 `.asr` and 83 `.pc` archives, under seed 0 or any seed with a matching pair record: the
three `TimeScale.*`, `Camera.Angle.*`, `Camera.LookAtBullet`, `Camera.TrackBullet`. The program's default for a missing
property is 0 (xorps xmm2), so they are either set by code (constants in the exe) or by a record type this search
doesn't match. Next: read the exe code that applies `TimeScale.PreHit` (0x3f1159 onwards, caller of the cam setup) for
immediate constants, or measure from the user's recorded kill cam. Four search rounds spent; stop here.

## Plan for KCD2
1. Slow motion + camera on the round (`deagle_slowmo`, the free camera and the round's path exist): three time scales
   by phase (bullet in flight, in the body, past it), measured from the video.
2. Organ path through the body: KCD2's NPC bones (tools/gen_hitbones.py) give the places; one effect per organ
   (Claude 2's effects), the X-ray sphere as a see-through pass (needs a test: KCD2 has no X-ray look).

### Round 5-6 (loop re-run): how the cam properties are built
The function that reads them (bin/SEV2_Remastered.exe 0x3f0b90, 2.2 KB) reads, per numbered stage, `InheritCamera`,
`InheritTimeScale`, `InheritDuration`, then `Camera.Angle/Offset/FieldOfView.Initial|Final`, `LookAtBullet`,
`TrackBullet`, `TimeScale.PreHit|Internal|PostExit`, `Duration`, `.ImpactTime`, `.ExitTime`. Names are prefixed with the
cam's group and a `%02d.` stage number; a stage with its Inherit flag set copies the one before (0x38-byte entries).
No file in the install holds numbered-prefix float records (pair search, all `.asr`/`.pc`; no text in `.brf`, `.ts`,
`.txt`, `.xml`). The time scales therefore come from something this search can't see (a record type other than float,
or built in code). Measuring them from a recorded kill cam is the way; the camera numbers above are the real defaults.

### Round 7: seed voting finds a stage-numbered cam (misc/Common.asr), `killcams.py vote`
Every float record implies one seed per property name; a real camera's seed recurs. Seeds `47955e06`, `47955e25`,
`47955e44` differ by 31 each (the stage digit before the final `.`: stages 00, 01, 02 of one cam):

| stage | found | values |
|---|---|---|
| 00 | TimeScale.PreHit, FieldOfView.Initial | 0.03, 46.7 |
| 01 | TimeScale.PostExit, FieldOfView.Final, Duration | 0.1, 46.7, 6 s |
| 02 | FieldOfView.Initial, Duration | 46.7, 8 s |

Likely, not verified: 3 % speed before the hit, 10 % after the exit, FOV 46.7, the stages inherit what they don't set.
`TimeScale.Internal`, the angles, offsets and Look/Track flags aren't present for this group (inherited or another
record type). Other seeds in the file (22.9/46.7, 180/12, -0.4/4, 1.5/1.5) show up under 2-3 names each: the same
records read with a different name (names of different lengths alias by a factor of 31), so they are ambiguous.

### Round 8: extracting the real assets (the user: "use their assets exactly")
`tools/se2/rscf.py` lists / extracts the `RSCF` resources (textures as DDS, sounds as WAV) from any `.asr` / `.pc`:
`rscf.py <archive> --extract <outdir> <substring>`. Outdir is private (E:\Tools\deagle-work\se2), never the repo.
- Extracted from `Common.asr`: 21 textures, `graphics\characters\internal_skeleton\` (bones, heart and lungs, guts / liver /
  kidneys, eyeball, testes, finger bones) and `graphics\characters\wounds\` (heart splat, teeth, wounds, blood drops),
  each colour+spec and normal map. Contact sheet: Desktop\Deagle Progress\24 Sniper Elite organs.
- The archive is a chunk stream of 5,647 chunks: `RSCF` 879 (files), `HCAN` 1144 (animations), `HSKN` 158, `HSKL` 203,
  `HMPT` 59, `FXPT`/`FXST`/`FXET` (effects), `BLUE` 61, `CONA` 463. The `RSFL` directory (0x20) lists 1364 names.
- Organ / bone animations are `HCAN` chunks: Heart_Death_Anim, destroyed_heart_anim, Lung_Left/Right_Coll(apse)_Anim,
  Kidney, Liver, Windpipe, `Breakable_bones\*` (Skull, Jaw, Arm, Leg, Vertibrae_Shatter...), found by name and size.
- Not yet: the organ / bone MESH chunks (none of the file names in the lists is a mesh; HSKN are 9 KB skins).

### Round 9: the meshes are `HSKN` chunks
`HSKN` = a whole model: `HSKN`, u32 size, u32 0x12, u32 0xc9, 0, u32 count, then the model's name at +0x18, a bone /
control-point table (8-float records), the control point's name, then each sub-mesh named `<model>_01`... with its
vertex data after (16-bit values, not float32). 158 models in `Common.asr`, among them the organ / bone ones (names
as in the animations): BCV_Skull_04/05/06_Complete, Skull_fragments, BCV_Eyeball, BCV_eye_L/R_Complete, Jaw_01-03,
Arm_R_L1/U1/U2/U3, Leg_R_L1/U1/U2/U3, Vertibrae_Shatter (top / bottom / Burst23), Pelvis / Scapula shatter, Thigh_test2,
Clavicle L/R + Tube, Windpipe upper / lower, destroyed_heart, Lung left / right collapse, Extra_Bone_Detail, and
`...EntryWound` / `...ExitWound` / `...nExitWound` (62-71 KB). Also the weapons and the soldiers' hats and heads.
Next: the vertex / index layout (counts and stride after the sub-mesh name), then an OBJ exporter
(tools/se2/hskn.py) to bring the real organs and bones into KCD2.

### Round 10: HSKN sub-mesh header (partly decoded)
Per sub-mesh, after its name `<model>_NN\0`: `00` pad, u32 data size (LE; mesh 01 of BCV_Skull_04 = 0x11e0 = 4576 B,
the region to the next name is that + 5), `0d 00 00 00` (13, constant), two LE counts (36 / 86, 51 / 128, 26 / 60, 45 /
106, 31 / 68, 23 / 50 for the six meshes of BCV_Skull_04), `01 01 01 00` flags, `00 ff ff 00`, `c9`, then packed 16-bit
data. The two counts are not simply vertices and triangles at a fixed stride (no linear fit of size on them across the
six meshes), so there are more blocks than positions + indices (weights, UVs, the 8-float control-point records).
The loader is bin/SEV2_Remastered.exe 0x8fc50 (1756 B), called from the chunk switch at 0x91f80 (HSKN case 0x92e83): it
reads a 0x18-byte header, then through a stream vtable (`[rax+8]` = read n bytes: 1, 4 and 0x10 at a time; one 0xc8
block). Next: follow those reads to the vertex / index counts, or fit the data against a known mesh (BCV_Eyeball).

### Round 11: the mesh reader works (`tools/se2/hskn.py`)
Layout read from the exe (HSKN loader 0x8fc50, sub-mesh parser 0x1d1cc0, string reader 0x9ddb0): strings are read in
whole 4-byte words (padded to 4); per node: name, u8 (v >= 10), u32 size, the mesh. Mesh: version 10-13, nverts, nfaces,
three flags, a box, then **nverts x 3 float32 positions** and **nfaces x 4 u16 faces** (v0 v1 v2 + a 4th), then per-face
tables and (version 13) the attribute block (normals / UVs / weights; not read yet).
`hskn.py <archive> --list [name]` / `--obj <outdir> [name]`. 58 of 158 models in `Common.asr` parse, no face index out of
range: the breakable bone shards (BCV_Skull_04/05/06, Skull_fragments, Jaw_01-03, Arm_R_L1/U1/U2/U3, Leg_R_L1/U1/U2/U3,
Vertibrae_Shatter, Pelvis_Shatter1, L_Scapula_Shatter, Thigh_test2, Extra_Bone_Detail), the eyes (BCV_eye_L/R_Complete) and
the wounds (SmallEntryWound / SmallExitWound / OrganExitWound), exported as OBJ to E:\Tools\deagle-work\se2\models (private).
Skull picture: Desktop\Deagle Progress\24 Sniper Elite organs\skull model BCV_Skull_04 from their data.png: angular shards
of ~2.5 cm, the pieces a head shot throws apart (SE2's "shatter" rig).
Not parsed yet (0 meshes in their node list): Heart_Solid, destroyed_heart, Lung_Left_Collapse / Lung_Right_Coll,
Windpipe, BCV_Eyeball, Clavicle: their meshes sit behind another record type (to find next).

### Round 12: why the organs read as empty
Heart_Solid, Lung_*, BCV_Eyeball, Windpipe, ... are **rigs** in their `HSKN` chunk: flags 0xc9 has 0x40 set, so the nA
vertices (228 for the heart, 81 for the left lung) are not in the chunk: a bone chain (Heart: Dummy01, Bone60-63; lung:
Root, Bone01-13) and no sub-meshes. The geometry sits in the **`RSCF` resource named after the model, right after its
`HSKN` + `HSBB` (bounding box)**: `heart_solid` (19.4 KB), `lung_left_collapse` (5.3 KB). Such a resource is a packed stream,
not a plain float array: it starts `u32 461, u32 1356, u32 452, 0` (vertices?, indices = 3 x 452 triangles, triangles) then
records of `[float, hash, u32, u32, u32, 0]`. `rscf.py --list heart_solid` finds it. Next: the reader of that stream
(the code that fills the HSKN's nA vertex array, called from 0x8f070 / 0x8fc50 at the model's resource load) to get
positions, normals and UVs; the shards (BCV_*) already parse because their geometry is inside the chunk.

### Round 13: the organs read (`tools/se2/organ.py`)
The rigged models (flags 0x40: geometry outside the chunk) keep it in the `RSCF` named after them (lower case), payload:
`u32 nverts, u32 nidx, u32 ntris, 0`, then **36-byte vertices: position = 3 half floats (+ w) at payload offset
28 + 36 * i, in metres**; then `nidx` u16 triangle indices (a few bytes after the vertices: 60 + 36 * nverts, give or take
a footer of 8 or 32 B: found as the offset whose indices are in range with the shortest edges). Other fields of the 36
bytes: a duplicated u32 (UVs?), flags `00ff0000 00030000`, 6 u16 (normal / tangent / weights).
Heart_Solid 105 x 135 x 105 mm, lungs 127 x 240 x 186 mm, kidneys 64 x 97 x 61 mm, liver upper / lower 221 x 77 x 146 and
225 x 105 x 147 mm, windpipe upper 36 x 121 x 52 mm: real organ sizes. OBJ in E:\Tools\deagle-work\se2\organs; picture:
Desktop\Deagle Progress\24 Sniper Elite organs\organ models from their data.png.
Not through the index search yet: destroyed_heart, Windpipe_Lower_Section, BCV_Eyeball, Testicle_*, wounds (a different
footer). The same layout holds for all 158 models (weapons, helmets, soldiers: first test only on edge lengths).

### Round 14: more organs, the vertex block start varies
The vertex block starts at a different byte per model (Heart / lungs / kidneys / liver / windpipe upper: position at
28 + 36 k; the eyeball and lower windpipe at 88, destroyed_heart at 90). `organ.py` tries the usual phase first, then every
even offset, keeping the shortest triangle edges among organ-sized (1-50 cm) shapes. Read and recognisable: heart, lungs
(L / R), kidneys (with the ureter), liver (upper / lower), windpipe upper and lower (the trachea into the bronchi), eyeball
with the optic nerve (28 x 31 x 125 mm), testicle and tube. destroyed_heart (the blast) isn't right yet (its vertices
are probably morph / piece records). Picture: Desktop\Deagle Progress\24 Sniper Elite organs\organ models from their data.png.
Next: UVs (the duplicated u32 at the start of each 36-byte record) and the KCD2 import.
