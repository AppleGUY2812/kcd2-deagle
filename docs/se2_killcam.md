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
