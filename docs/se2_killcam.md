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

## Not found yet: the numbers
The values (angles, offsets, FOV, the three time scales, durations) aren't plain text in any archive (75 scanned).
They are binary properties. Two ways: measure them from a recorded kill cam (cheap), or read them out of the
program's data tables with the reverse-engineering tools (expensive). The user will send a video.

## Plan for KCD2
1. Slow motion + camera on the round (`deagle_slowmo`, the free camera and the round's path exist): three time scales
   by phase (bullet in flight, in the body, past it), measured from the video.
2. Organ path through the body: KCD2's NPC bones (tools/gen_hitbones.py) give the places; one effect per organ
   (Claude 2's effects), the X-ray sphere as a see-through pass (needs a test: KCD2 has no X-ray look).
