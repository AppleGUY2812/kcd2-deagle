# Henry idle CAF check

## Scope

Measurement only: decode frame 0 of `ron_grizzly_henry_vm_idle.caf`, reconstruct absolute joint transforms through the
compiled CHR hierarchy, and compare with frame 1 of `grizzly_idle` in the source blend. No build files are changed.

`tools/armswap/check_caf.py` implements the CryEngine 0x829 controller decoder (SmallTree48BitQuat and
SmallTree64BitExtQuat), default-transform fallback for missing tracks, and record-relative CHR parent links. Its
`--dump-blend` mode applies the build's 90-degree Z rotation, the exporter's bone-to-joint axis conversion, and rc's
180-degree Z conversion, matching the transform already verified by `check_chr.py`.

## Decoder validation

`python3 tools/armswap/check_caf.py --validate-game` passes on
`animations/humans/male/aimposes/rifle_aimposes_aim_upperbody_player.caf` against the game `male.chr`:

```
game validation: 206 tracks, 243 joints, finite=True
quaternion norm: 1.000000000 .. 1.000000000
human pose: z 0.000 .. 1.875 m, max distance from origin 1.888 m
Human_Male           [0.000 0.000 0.000] mm
RightArm             [364.366 -169.189 1180.290] mm
RightForeArm         [328.814 41.937 1338.607] mm
RightHand            [262.881 3.333 1588.724] mm
RightHandIndex1      [252.180 0.859 1674.000] mm
game decoder validation: PASS
```

The extended 64-bit quaternion is 21+21+20 data bits plus a two-bit dropped-component index, not three 20-bit fields.
The initial, incorrect field widths produced non-unit quaternions; the corrected decoder passes on all game tracks.

## Intended Blender pose

After KingdomCome.exe exited, Blender ran at Idle priority with `-t 3` and dumped 179 joints from `@grizzly_idle`,
frame 1. No model or animation was rebuilt. Command:

```
python3 tools/armswap/check_caf.py --intended /mnt/e/Tools/deagle-work/guns/ron_grizzly_henry/idle_pose.json
Henry idle frame 0: 133 joints; ten worst by position error
RightHand_IKTarget       pos  3236.687 mm  rot  180.000 deg  parent Human_Male
Human_Male               pos  3236.687 mm  rot  180.000 deg  parent root
blend_control_06         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
blend_control_05         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
blend_control_04         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
blend_control_03         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
blend_control_02         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
blend_control_01         pos  3236.687 mm  rot  119.999 deg  parent Human_Male
LeftHand_IKTarget        pos  3236.687 mm  rot   62.470 deg  parent Human_Male
Spine_IKBlend            pos  3236.687 mm  rot  180.000 deg  parent Human_Male
max pos 3236.687 mm (RightHand_IKTarget), max rot 180.000 deg (RightHand_IKTarget)
```

For arm-chain context, `RightArm` is 346.873 mm / 161.797 degrees off, `RightForeArm` 609.245 mm / 177.450 degrees,
`RightHand` 409.199 mm / 151.618 degrees, and `RightHandIndex1` 489.521 mm / 160.970 degrees.

## Pattern and proposed fix

VERIFIED: this is primarily a root/model-space failure, not a fixed per-joint roll offset. `Human_Male` is already
3236.687 mm and 180 degrees away before its children are evaluated; all ten largest position errors are that root or
its direct children. The varying descendant rotation errors also implicate the parent-relative track conversion, not
merely error accumulating down an otherwise correct chain.

Proposed fix (not implemented): for Henry joints, generate CAF local transforms from the intended absolute joint
matrices as `inverse(parent_absolute) @ child_absolute`, including the actual compiled-CHR parent at the root, instead
of rotating an already-relative matrix with `transform_animation_matrix`. Then rerun this compiled-CAF comparison;
do not change the verified CHR bind fix.

## Gun control and corrected reference

VERIFIED: the original reference conversion was wrong. Gun rotations matched (typically 0.007 degrees), but their
translations did not. Applying `Rz180 * bone_to_joint` to reference translations, plus one translation fitted at
`J_Gun`, makes the unchanged gun controls agree:

```
J_Gun          0.000 mm       tag_slide      0.005 mm
tag_slidelock  0.003 mm       tag_mag_01     0.006 mm
tag_muzzle     0.020 mm       tag_brass      0.003 mm
tag_barrel     0.010 mm
```

The second magazine and camera are excluded from this control: their pipeline-specific placement does not follow the
gun-body transform. No global matrix applied to the whole reference works; the correction applies to translations,
whose exporter convention differs from rotations.

With that corrected reference, Henry still fails. The worst idle errors are `RightWeaponSlot` 854.817 mm / 121.666
degrees, `HeadEnd` 328.093 / 120.442, `DaggerSlot` 302.916 / 110.189, `LeftWeaponSlot` 287.723 / 51.307,
`LeftArmRoll1` 286.038 / 50.764, `RightArmRoll1` 258.613 / 161.797, `LeftForeArmRoll2` 257.140 / 135.587,
`BackSlot_crossbow` 243.907 / 117.855, `RightForeArmRoll2` 241.991 / 177.450, and `LeftWeaponSheath` 203.783 /
51.307. Thus the earlier 3236 mm root diagnosis was a reference artifact; the Henry animation-local conversion is the
real remaining defect.

## Implementation

`tools/armswap/animation_pose.py` patches only Henry's DAE tracks. For each frame it calibrates the reference at
`J_Gun` and writes each Henry local as `inverse(parent_absolute) @ child_absolute`; gun DAE channels and the verified
CHR bind path are unchanged. `check_caf.py` performs the same gun calibration, checks seven gun controls, and supports
idle and reload CAFs. Henry tolerance is 1 mm / 0.5 degree; gun tolerance is 0.1 mm / 0.1 degree.

The first one-pass build failed idle verification at 374.614 mm / 180 degrees although its gun control remained
0.020 mm / 0.007 degrees. The cause was rc's static-track elimination: the patch simulated all DAE gun tracks, while
the compiled CAF omitted static ones and therefore reconstructed their CHR defaults instead.

The final implementation compiles once, then `animation_repatch.py` repatches Henry against the actual retained gun
tracks in each first-pass CAF and compiles again. Omitted tracks use CHR default locals. A negligible 0.0000001 mm
translation delta on Henry's final keys prevents static Henry tracks from being discarded; this is needed for joints
such as `RightArmRoll1`. `gb_convert.sh` runs this two-pass calibration only for `bodySkins` profiles. Final build and
verification results are pending.
