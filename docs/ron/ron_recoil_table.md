A) Aim/camera kick  [RecoilPattern = (pitch,yaw) deg per shot, cycles]
gun|cal|FireRate s|vel m/s|dmg|kg|RecoilPattern|FirstShotRecoil|RecoilInterpSpeed|RecoilReturnRate|RecoilReturnPercentage|RecoilReturnInterpSpeed|ADSRecoilMultiplier|RecoilMultiplierPitch|FireCameraShake
G19 (Secondary_G19_V2)|9mm|0.1|374.9|30|0.6|(2,-0.5) (1.5,0.5)|0.25|1.5|2.5|0|0|-|-|-
M11 Compact (Secondary_P229)|9mm|0.1|374.9|30|0.6|(2,-0.5) (1.5,0.5)|0.25|1.5|2.5|0|0|-|-|-
PC19 (Secondary_PFC9)|9mm|0.1|374.9|30|0.6|(2,-0.5) (1.5,0.5)|0.25|1.5|2.5|0|0|-|-|-
P250 (Secondary_P250)|9mm|0.045|374.9|-|0.6|(2,-0.5) (2,0.5) (2,-0.5) (2,0.5)|1|0.5|2.5|0.5|1.5|-|0.15|Generic_Fire_Small
USP45 (Secondary_USP)|9mm|0.075|374.9|-|0.6|(6,-0.5) (6,0.5) (6,-0.5) (5.5,0.5)|0.35|1.5|2.5|0.5|1.5|-|0.15|Generic_Fire_Small
M9A1 (Secondary_M92FS_V2)|9mm|0.09|374.9|30|0.6|(4.5,-0.5) (5,0.5) (5.5,-0.4) (6,0.4)|0.25|1.5|2.5|0|0|1.5|0.15|-
B92X (Secondary_M92A3)|9mm|0.09|374.9|30|0.6|(4.5,-0.5) (5,0.5) (5.5,-0.4) (6,0.4)|0.25|1.5|2.5|0|0|1.5|0.15|-
FiveSeven (Secondary_FiveSeven_V2)|FN 5.7x28mm|0.075|716.28|25|0.61|(1,0.2) (1,0.5) (1,0) (1,-0.5) (1,-0.2) (1,0.15)|0.05|2.5|1|-|-|0.6|-|C_Generic_Fire_Shaky_MPX
G19 (Secondary_P99)|.40 S&W|0.05|374.9|40|0.6|(0.4,0) (0.32,-0.1) (0.6,-0.15) (0.7,-0.125) (0.4,-0.1) (0.3,-0.15) (0.2,-0.2) (0.4,-0.15) (0.3,0)|0.8|3|2.5|-|-|0.6|-|C_Generic_Fire_Shaky_G19
M1911 (Secondary_M1911_V2)|.45|0.02|374.9|35|0.6|(2,-0.1) (2.5,-0.1) (1.6,0.1) (0.5,0.1)|0.4|3|2.5|-|-|0.9|-|Generic_Fire_Small
TLE 1911 (Secondary_Kimber1911)|.45|0.1|253|45|1.2|(3,-0.5) (2.75,0.5) (3,-0.5) (3.23,0.5)|0.35|1.5|2.5|0|0|-|0.15|-
M45A1 (Secondary_M45A1)|.45|0.1|253|45|1.2|(3,-0.5) (2.75,0.5) (3,-0.5) (3.23,0.5)|0.35|1.5|2.5|0|0|-|0.15|-
M2011 (Secondary_M2011)|.45|0.1|253|45|1.2|(3,-0.5) (2.75,0.5) (3,-0.5) (3.23,0.5)|0.35|1.5|2.5|0|0|-|0.15|-
.357 Magnum (Secondary_Python_V2)|.357 Magnum|0.2|457.2|65|1.4|(6,0.2) (5,-0.2) (5.5,0.1)|1|1.5|1.25|0|1.5|0.8|0.15|C_Generic_Fire_M14
TPL (Secondary_TCR)|5.56mm x 45mm|0.1|200|1|2.82|(2.4,0.5) (2.4,-0.5) (2.4,0.5) (2.4,-0.5)|0.2|1.25|2.5|0.7|2|1.2|-0.04|C_Generic_Fire_Shaky_MPX
MP5A3 (Primary_MP5A3)|9mm|0.075|400|30|2.55|(1,0.3) (1,-0.5) (1,0.1)|1|0.005|1.5|0.2|-|0.8|-0.05|-
M4A1 (Primary_M4A1)|5.56mm x 45mm|0.08|905.256|50|2.8|(1.5,-0.4) (1.5,0.4)|1|0.02|0.05|0|0|1.2|-|Generic_Fire_M4A1
GA416 (Primary_HK416)|5.56mm x 45mm|0.0706|870|42|3.04|(2.5,0)|1|0.02|0.05|0.5|1|-|-|Generic_Fire_M4A1
870mcs (Primary_870mcs)|12 Gauge|0.04|253|55|2.38|(7,-0.5) (7,0.5) (7,-0.5) (7,0.5)|1|1.25|2.5|1|5|1.2|-|C_Generic_Fire_Shaky_590A
B1301 (Primary_B1301)|12 Gauge|0.12|221|40|3.82|(7,0.5) (7,-0.5)|-|1.25|2.5|0|0|-|0.3|C_Generic_Fire_M1014

B) Procedural viewmodel kick (bCalculateProcRecoil=true on all)
gun|FireStrength|FireStrengthFirst|FireTime s|DampStrength|AngleStrength|Randomness|FireADSMod|AngleADSMod|HasBuildup|RotationBuildup xyz|PositionBuildup xyz|BuildupADSMod|BuildupDamp|MeshSpaceAimInterp|InertiaDragAimRotation
G19_V2|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-12|0/0/7|0.75|2.5|55|0.6
P229|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-12|0/0/7|0.75|2.5|55|0.6
PFC9|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-12|0/0/7|0.75|2.5|15|0.6
P250|8|5|0.1|1.4|0.05|0.09|-|-|True|0/0/-8|0/0/8|0.75|2.5|15|-
USP|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-12|0/0/12|0.75|2.5|55|0.6
M92FS_V2|8|5|0.2|1.4|0.075|0.06|0.2|0.1|True|0/0/-12|0/0/7|0.75|2.5|15|0.6
M92A3|8|5|0.2|1.4|0.075|0.06|0.2|0.1|True|0/0/-12|0/0/7|0.75|2.5|55|0.6
FiveSeven_V2|8|5|0.1|1.4|0.075|0.125|0.2|0.1|True|0/0/-10|0/0/10|1|2.5|55|0.6
P99|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-10|0/0/10|1|2.5|15|0.6
M1911_V2|25|15|0.12|2|0.05|0.09|-|-|True|-10/0/-30|0/-5/15|1|2.5|15|0.6
Kimber1911|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-15|0/0/12|1.25|1.5|55|0.6
M45A1|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-15|0/0/12|1.25|1.5|55|0.6
M2011|8|5|0.1|1.4|0.075|0.12|0.2|0.1|True|0/0/-15|0/0/12|1.25|1.5|-|0.6
Python_V2|8|8|0.2|1.5|0.075|-|-|-|True|0/0/-10|0/0/2|-0.2|1.5|55|0.6
TCR|0|0|0.1|2.5|0|0|0|0|-|-|-|-|-|55|0.8
MP5A3|25|12|0.12|2.5|0.1|0.15|0.25|0.15|True|0/0/-8|0/5/1.5|0.15|2.2|55|0.8
M4A1|6|6|0.125|3.5|0.075|0.025|0.1|0.125|True|0/0/-10|0/0/4|0.65|3.5|11|1.6
HK416|6|6|0.125|3.5|0.075|0.025|0.1|0.125|True|0/0/-8.5|0/-1/2|0.75|3|11|0.8
870mcs|6|6|0.15|2|0.02|0.075|0.3|0.15|True|0/0/-12|0/0/4|1|1.5|-|0.8
B1301|10|10|0.15|3|0.075|0.3|0.075|0.1|True|0/0/-10|0/0/1|1|2.15|55|0.8

## Measured from the user's recordings (2026-10-12)

Source: `Deagle Progress\test records\2026-10-12 11-12-34.mp4` (ours: Deagle 8 shots, Glock semi + burst),
`11-50-38.mp4` (Ready or Not G19: 10 slow singles, 2 x 15-shot spam), `11-53-12.mp4` (Ready or Not MK V .50 AE:
8 singles, 7-shot spam). All 1664x936, 60 fps. Contact sheets (one shot per gun, every frame labelled with offset,
ms and the measured camera angle) and the curves: `Deagle Progress\recoil_kcd_glock.png`, `recoil_kcd_deagle.png`,
`recoil_ron_g19.png`, `recoil_ron_mk5.png`, `recoil_curves.png`. Ready or Not frames stayed out of the repo.

How: the camera is the rotation that maps background features from the frame before the shot to each later frame
(RANSAC, pure rotation, gun / HUD masked); px -> deg through the focal length. KCD2: vfov 1.101 rad -> f = 762 px; the
footage self-calibrates to 756-786 px. Ready or Not: `GameUserSettings.ini` has FieldOfView=90 with
bUseVerticalFOV=True, and the footage self-calibrates to f = 832-835 px on 12-13 deg turns = **90 deg horizontal at
16:9** (vfov 58.7). Frames: KCD2 ran ~120 fps (plugin log: 600 frames per 4.8-5.0 s), Ready or Not 116-183 fps, so
each video frame is a new game frame except capture repeats (7 % of moving frames in the KCD2 clip, ~1 % in Ready or
Not; marked on the sheets). Timings are +-17 ms. The KCD2 clip is dark and the encoder skips static dark blocks, so
frame-to-frame trackers lock onto the macroblock grid there; ours were measured against the pre-shot frame directly.
The user's mouse is in the footage: Deagle shots 1-2 and every Glock burst come back further than the code can, with
an S-shaped return starting ~170 ms after the shot (a hand pulling down). Cross-check against the mock (PhysicsRecoil
at 120 fps, same shot times): **pitch lands as coded** (Deagle shot 3: +6.8 peak vs +6.5 asked, -3.48 back by 0.67 s
vs -3.46; 5-shot string +18.2 vs +19.4; Glock 3-round burst peak +7.3 vs +7.7).

| per shot | Glock 17 (ours) | G19 (RoN) | Desert Eagle (ours) | MK V .50 AE (RoN) |
|---|---|---|---|---|
| camera peak | +2.2 deg in the shot frame (<= 17 ms); mock 2.5 | first **-0.4 dip** (17-33 ms), then +0.78 / +0.56 by ~150 ms | +4.4 to +6.8 at 17-50 ms; mock 7.6 / 6.5 | **shake** -5..+7 pitch, +-2-3 yaw for ~100 ms, then +7.7-8.3 by 150-230 ms |
| camera settled | ~1.2 (mock; the video only shows 2.2 -> 1.5 by 133 ms, next shot at 150 ms) | +0.78 / +0.56 alternating (mean 0.67) | +3.3 at 0.67 s (shot 3); +1.0-1.2 with the user pulling down | **+8.49 +- 0.09** (creeps +0.6 from 230 to 580 ms) |
| spring-back | 31 % by 133 ms (mock 49 % over ~0.5 s) | **0 %** | 51 % over ~0.6 s (exponential); 73-81 % with the mouse | **0 %** (-10 %) |
| sideways | 0.0-0.15 per shot, no pattern (one -1.2 step in 7: mouse; code asked +0.6 there) | **-0.30 / +0.29 alternating** (zig-zag), stays | -0.16 / -0.14 / +0.03 (code asked -0.4 / -0.2 / -0.5 settled, -0.7 / -0.4 / -1.0 peak) | settled +-0.3-0.5 (6 of 8 left) + a +1.2..+2.8 transient yaw swing at 50-80 ms |
| gun model | rear sight **108 px** down (~+7 deg of view) at 33 ms, ~10 px sideways, **no twist**; slide 146 -> 235 px on screen; back by 133 ms | rear sight **186 px** down-right (~+11 deg) at 50 ms, slide **twisted ~25 deg**, 115 -> 280 px; back by 130-180 ms, 20 px overshoot | 0-67 ms **pushed into the camera (~3x size)**; 83-100 ms twisted 30-35 deg; near rest by 170 ms, then sags ~100 px low-left for > 0.5 s (sway from the camera kick) | front sight **234 px** up (~16 deg), slide flips to near vertical (270 -> 720 px: ~50-70 deg) at 67-83 ms; back by ~220 ms |
| rapid fire, climb | 7 @ 156 ms: peak +7.6, ~+5.2 settled; 17 @ 176 ms: +20.5 (1.2/shot); burst 3 rounds in ~80 ms: +7.3 | 15 @ 157 ms: +12.8 (0.85/shot); 15 @ 138 ms: +13.5 | 5 @ 158 ms: +18.2 (3.6/shot) | 6 @ 300 ms: +53.5 (8.9/shot), then the pitch limit |
| rapid fire, sideways | -1.0 over 17 shots | -1.9 over 15 (zig-zag +-0.15-0.3 on a leftward drift) | -0.4 over 5 (code asked -2.3) | -0.5 to -2.0 |

The data table vs the video: the G19's (2,-0.5) (1.5,0.5) and 0 % return are right in shape (alternating, the first
entry bigger and to the left, nothing comes back) but land at x0.39 in pitch and x0.6 in yaw if the table is in degrees.
FirstShotRecoil 0.25 doesn't show: the clip's first shot is +0.71, like the other odd shots. The MK V isn't in the
table; the video says 8.5 deg a shot, 0 % return and a camera shake as big as the kick. So the table's numbers are not
on-screen degrees: calibrate to these measurements, not to the table (camPerFlip's "above the .357 Python's 5-6 deg" is a
table value).

9 mm vs .50 AE in Ready or Not: camera **0.08x** (settled) / ~0.1x (peak), gun model **~0.8x** (186 vs 234 px). Ours:
camera 0.36x settled / 0.32x peak, gun model 0.49x by design (4.6 vs 9.5 deg).

What it means:
- The Glock's "too weak" isn't the camera. Its camera kick is ~3x the G19's peak and it climbs at least as much in rapid
  fire. What Ready or Not has and we don't: the gun model (1.7x our Glock's jump plus a 25 deg twist) and the kick's
  shape (a short dip before the climb, the climb spread over ~150 ms instead of landing in the shot frame).
- Sideways is lost in game. Pitch lands within ~5 % of the mock but yaw shows ~20 % of what PhysicsRecoil asks for.
  Even at 100 %, the designed sideways (sideSigma 0.15: Glock sigma ~0.35 deg peak, 60 % springing back) is below what
  reads on screen. Ready or Not's horizontal reads because it is a fixed alternating zig-zag that stays, plus the gun
  model's twist, plus a 2-3 deg yaw shake on the .50.
- The Deagle's gun model enters the camera for ~4 frames after every shot. The Glock (4.3 cm push) doesn't, so it's
  more than gunBackPerVel's 5.5 cm.

Tuning recommendations (cfg.physics):
1. Leave camPerFlip 0.35 / kickExponent 0.67 for the camera; don't raise them to fix "weak" (the Glock's camera already
   exceeds the G19's; Ready or Not puts the 9 mm at ~0.1x the .50 on the camera).
2. Put the 9 mm's punch in the gun model: give gunFlipPerFlip / gunBackPerVel their own, flatter caliber curve (a
   gun-model exponent ~0.3 instead of kickExponent, or a Glock multiplier ~1.6): Glock flip 4.6 -> ~7.5 deg, push
   4.3 -> ~6 cm, aiming at a ~180 px rear-sight jump (G19: 186 px). Leave the Deagle's (its model already overshoots).
3. Horizontal: first make the yaw land (log the dy asked in CamRecoilTick against the yaw read back the next frame).
   Then go Ready-or-Not style: a per-gun side bias that alternates sign each shot of a string (Glock ~+-0.1 x up =
   +-0.35 deg, Deagle ~+-0.07 x up = +-0.5 deg), not sprung back (holdYaw 0.5 -> 0), sideSigma 0.15 -> ~0.05 as jitter
   on top, and a visible twist on the model (gunRollPerYaw 0.5 -> ~3; the G19 twists ~25 deg).
4. .50 climb: Ready or Not keeps 100 % (MK V 8.5 deg a shot), the Deagle keeps 3.3. Keep holdMax 0.6 (the user's pick)
   but lower holdRange 4.7 -> ~3.0: the Deagle's spring-back caps at 3 deg (keeps ~4 deg, 58 %), a single Glock shot's
   (0.6 x 3.5 = 2.1 deg) doesn't change (its rapid fire climbs a bit more).
5. Kick shape: riseRate 70 lands ours in the shot frame; Ready or Not climbs over 150-200 ms after a dip / shake. Try
   riseRate ~25 (63 % at 40 ms, 95 % at 120 ms; it trims the peak, since the elastic part already returns while the
   kick rises, so re-check the mock's peak) and a short view shake for the .50 on the physics path (SetViewShake is only
   in the fallback Recoil()).
6. Check the Deagle's into-the-camera frames live with gunBackPerVel 0: if it stays, it's the shoot clip or the plugin
   spring, not the push.
