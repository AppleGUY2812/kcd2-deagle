import json,sys
D=json.load(open(sys.argv[1])); o=[]
def f(v):
    if v is None: return '-'
    if isinstance(v,bool): return 'T' if v else 'F'
    if isinstance(v,float): return f'{v:g}'
    if isinstance(v,(list,tuple)): return '/'.join(f(x) for x in v) if v else '[]'
    if isinstance(v,dict): return ' '.join(f'{k[0]}:{f(x)}' for k,x in v.items())
    return str(v)
def g(p,k): return f(p[k]) if k in p else '-'
P=D['player']['values']; M=D['character_movement']['values']; FPn=D['fp_anim_blueprint']['procedural_nodes']; W=D['weapons']
order=['Secondary_G19_V2','Secondary_M92FS_V2','Secondary_Python_V2','Secondary_M1911_V2','Secondary_USP','Secondary_P229','Secondary_PFC9','Secondary_P250','Secondary_M92A3','Secondary_FiveSeven_V2','Secondary_P99','Secondary_Kimber1911','Secondary_M45A1','Secondary_M2011','Secondary_TCR','Primary_MP5A3','Primary_M4A1','Primary_HK416','Primary_870mcs','Primary_B1301']
ls=FPn.get('AnimGraphNode_LazySpringBone',{})
o+=['# Ready or Not first-person weapon movement read-out','',
'Decoded from the Steam install (read-only). Raw: `ron_movement.json`. `-` = not overridden → C++ default (**not read**). Units: cm, degrees, seconds.','',
'## Headline: what is procedural vs baked','',
'- **Baked additive animations** drive sprint (start / loop / stop), jump (start / loop / end), stand↔crouch and walk/jog loco bob. The FP anim graph `ANIMBP_Player_FP_Master` plays them: '+', '.join('`'+s+'`' for s in D['fp_anim_blueprint']['baked_additive_sequences'] if 'sprint' in s or 'jump' in s or 'crouch' in s or 'loco' in s)+'.',
'- **Procedural, numeric**: per-gun inertia drag (look/strafe sway), mesh-space movement scale, free-aim (deadzone) interp, lean multipliers, mesh-space poses (default / ADS / back / draw / holster) with interp speeds, wall pushback / low-ready ranges, spread. There is also one **LazySpringBone** node on `weapon_right`.',
'- **Bob parameters** (`CameraBob*` / `WeaponBob*`, 21 fields on BaseItem) are **never overridden** by any of the 68 weapons, so they are C++ defaults (names only, listed at the end).','',
'## 1. Sway / bob / inertia','',
f"- LazySpringBone (`weapon_right`, ref `root`): DrivingForceStrength {g(ls,'DrivingForceStrength')}, TargetDistance {g(ls,'TargetDistance')}, Stiffness {g(ls,'Stiffness')}, Mass {g(ls,'Mass')}, Damping {g(ls,'Damping')}, Gravity {g(ls,'Gravity')}, SpringStiffness {g(ls,'SpringStiffness')}, SpringDamping {g(ls,'SpringDamping')}, all 6 axes enabled. Weapons scale it with `LazySpringStrength` / `LazySpringStrengthADS` (G19 ADS = -1, i.e. use default).",
f"- Player: CameraRotationRateMeshSpaceMultiplier {g(P,'CameraRotationRateMeshSpaceMultiplier')} (look-rate → mesh offset), MeshspaceInterp {g(P,'MeshspaceInterp')}, MeshspaceRecoilMovementMultiplier {g(P,'MeshspaceRecoilMovementMultiplier')}, MinMax {g(P,'MeshspaceRecoilMovementMinMax')}, idle camera shake DefaultIdleShake {g(P,'DefaultIdleShake')}.",
'- Per gun: see the table in section 7 (InertiaDrag*, MovementSpeedScale = mesh offset per unit velocity xyz, MovementSpeedRotationScalePitchYawRoll, MeshSpaceAimInterp, FreeAim*).','',
'## 2. Sprint','',
f"- SpeedModifier_Sprint {g(P,'SpeedModifier_Sprint')}, SpeedModifier_SprintTime {g(P,'SpeedModifier_SprintTime')} s, RunSpeed {g(P,'RunSpeed')} cm/s, SprintFovFactor {g(P,'SprintFovFactor')} (×FOV), SprintFovInterpTime {g(P,'SprintFovInterpTime')} s, SprintShakeAmount {g(P,'SprintShakeAmount')}, SprintShakeDecreaseRate {g(P,'SprintShakeDecreaseRate')}.",
'- Sprint pose = baked `fp_<gun>_add_loco_sprint_start/fwd/stop` (pistols use the G19 set, rifles the M4A1 set, heavy armour gets `_heavy` variants). No numeric sprint offset, blend time, or **sprint-to-fire delay** field exists in the mappings. Firing after a sprint is most likely gated by the sprint-stop anim (inference).','',
'## 3. Jump / land','',
f"- CharacterMovement: JumpZVelocity {g(M,'JumpZVelocity')} cm/s, GravityScale {g(M,'GravityScale')}, MaxAcceleration {g(M,'MaxAcceleration')}, BrakingDecelerationWalking {g(M,'BrakingDecelerationWalking')}, BrakingFriction {g(M,'BrakingFriction')}, MaxStepHeight {g(M,'MaxStepHeight')}.",
'- Weapon kick = baked `fp_gen_add_jump_start/loop/end` additives. `JumpRecoveryStrength/Time/AnimTime`, `bHasPrelanded` exist only as runtime anim variables (names only). There is no fall-height scaling field; only the landing sound threshold `NegativeVelocityRequiredForLandingSound` (not overridden).','',
'## 4. Crouch / lean / stance','',
f"- CrouchHeight {g(P,'CrouchHeight')}, CrouchedEyeHeight {g(P,'CrouchedEyeHeight')}, CrouchedHalfHeight {g(M,'CrouchedHalfHeight')}, MaxWalkSpeedCrouched {g(M,'MaxWalkSpeedCrouched')} cm/s. Transition = baked `fp_m4a1_stand_to_crouch` / `fp_m4a1_crouch_to_stand`.",
f"- Lean: LeanSpeedMultiplier {g(P,'LeanSpeedMultiplier')} (move speed while leaning). Per gun QuickLeanMultiplier / FreeLeanMultiplier (G19 200/200, most guns default). QuickLeanAmount / Intensity / InterpSpeed are names only.",
f"- Speeds: SpeedModifier_Aim {g(P,'SpeedModifier_Aim')}, SpeedModifier_AimFocus {g(P,'SpeedModifier_AimFocus')}, BackwardStrafeSpeedMultiplier {g(P,'BackwardStrafeSpeedMultiplier')}, MinWalkSpeedPercent {g(P,'MinWalkSpeedPercent')}, HitSpeedMultiplier {g(P,'HitSpeedMultiplier')}.",
'- Freelook limits: '+g(P,'FreelookSetting')+'. Ready or Not has no prone stance.','',
'## 5. Movement inaccuracy','',
'- Per gun: FirstShotSpread, SpreadPattern, ADSSpreadMultiplier, VelocitySpreadMultiplier (spread added per unit of movement speed), SpreadReturnRate, BulletsFiredUntilFullyAccurate (table below). There are **no crouch / jump / in-air spread fields** in the mappings, and attachments scale spread with `SpreadMultiplier` (GM9 suppressor 0.9).','',
'## 6. Wall pushback / low ready','',
'- Per gun: PushbackRangeOffset, LowReadyRangeOffset (cm added to the trace for a long gun), LowReadyPitchThreshold. Attachments add length, e.g. Pistol_Suppressor_GM9: LowReadyOffset 33, PushbackOffset 33 (also VerticalRecoilMultiplier 1.2, SpreadMultiplier 0.9, DeadzoneMultiplier 1.8, MuzzleVelocityMultiplier 1.3). Base trace distances (LowReadyTraceDistance, LowReadyDistance) are names only.','',
'## 7. Per-gun values','',
'| weapon | name | InertiaAim R/L | InertiaStrafe R/L | MoveSpeedScale | MoveRotScale PYR | MeshSpaceAimInterp | LazySpring/ADS | FreeAim speed/ADS/hip | FreeAimLimit/ADS | Default pos | ADS pos | QuickLean/FreeLean | Pushback/LowReady | FirstShotSpread | ADSSpread | VelSpread | SpreadReturn | ADSZoomIn/Out |','|'+'---|'*19]
for n in order:
    p=W[n]
    tr=lambda k: f((p.get(k) or {}).get('Translation')) if isinstance(p.get(k),dict) else '-'
    o.append('| '+' | '.join([n,g(p,'ItemName'),g(p,'InertiaDragAimRotation')+'/'+g(p,'InertiaDragAimLocation'),g(p,'InertiaDragStrafeRotation')+'/'+g(p,'InertiaDragStrafeLocation'),g(p,'MovementSpeedScale'),g(p,'MovementSpeedRotationScalePitchYawRoll'),g(p,'MeshSpaceAimInterp'),g(p,'LazySpringStrength')+'/'+g(p,'LazySpringStrengthADS'),g(p,'FreeAimInterpSpeed')+'/'+g(p,'FreeAimInterpADSModifier')+'/'+g(p,'FreeAimInterpHipModifier'),g(p,'FreeAimLimit')+'/'+g(p,'FreeAimLimitADS'),tr('MeshspaceTransform_Default'),tr('MeshspaceTransform_Aiming'),g(p,'QuickLeanMultiplier')+'/'+g(p,'FreeLeanMultiplier'),g(p,'PushbackRangeOffset')+'/'+g(p,'LowReadyRangeOffset'),g(p,'FirstShotSpread'),g(p,'ADSSpreadMultiplier'),g(p,'VelocitySpreadMultiplier'),g(p,'SpreadReturnRate'),g(p,'ADSZoomInSpeed')+'/'+g(p,'ADSZoomOutSpeed')])+' |')
o+=['','All 68 weapons are in `ron_movement.json → weapons`. Each gun links its montages through `AnimationData` (e.g. G19 → `AnimData/Mainline/BP_G19G5_M_AnimData`). Those are montage references only, with no numeric movement values. Correction to the earlier recoil notes: the G19 **does** play an arm fire montage (`fp_m2011_add_fire_single_Montage` / `_ads`), borrowed from the M2011.','',
'## 8. Damage-curve X units','',D['damage_curve_units'],'',
'## Names only (no value in data)','']
for k,v in D['names_only'].items(): o.append(f'- **{k}**: '+(', '.join(v) if isinstance(v,list) else v))
open(sys.argv[2],'w').write('\n'.join(o)+'\n'); print(len(o))
