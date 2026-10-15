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
