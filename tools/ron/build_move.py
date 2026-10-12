import sys,json,re,os
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import dump,OUT,USMAP,WORK
u=U(USMAP); S=WORK   # ron_weapons_all.json, fp_cdo.json
W=json.load(open(S+'/ron_weapons_all.json')); FP=json.load(open(S+'/fp_cdo.json'))
C=OUT+'/ReadyOrNot/Content/'
MK=['ItemName','ActiveMotionBlock','InertiaDragAimRotation','InertiaDragAimLocation','InertiaDragStrafeRotation','InertiaDragStrafeLocation','MovementSpeedScale','MovementSpeedRotationScalePitchYawRoll','MeshSpaceAimInterp','LazySpringStrength','LazySpringStrengthADS','FreeAimInterpSpeed','FreeAimInterpADSModifier','FreeAimInterpHipModifier','FreeAimSlowMoveModifier','FreeAimSlowMoveTolerance','FreeAimLimit','FreeAimLimitADS','FP_ADS_Motion_Weight','MeshspaceTransform_Default','MeshspaceTransform_Aiming','MeshspaceTransform_Back','MeshspaceTransform_OnDraw','OnDrawMeshspaceInterp','MeshspaceTransform_OnHolster','OnHolsterMeshspaceInterp','bDisableMeshspaceMovement','QuickLeanMultiplier','FreeLeanMultiplier','LeanOffset','bUseLowReady','PushbackRangeOffset','LowReadyRangeOffset','LowReadyPitchThreshold','ADSZoom','ADSZoomInSpeed','ADSZoomOutSpeed','DrawPlayRate','HolsterPlayRate','FirstShotSpread','SpreadPattern','ADSSpreadMultiplier','VelocitySpreadMultiplier','SpreadReturnRate','BulletsFiredUntilFullyAccurate','AnimationData']
BOB=[p[2] for p in u.structs['BaseItem'][2] if re.search('Bob',p[2])]
weap={n:{'source':f'ReadyOrNot/Content/Blueprints/Items/WeaponsRevised/{n}.uasset (Default__{n}_C)',**{k:v['props'][k] for k in MK if k in v['props']},
          **({'bob_overrides':{k:v['props'][k] for k in BOB if k in v['props']}} if any(k in v['props'] for k in BOB) else {})} for n,v in W.items() if 'props' in v and n.split('_')[0] in('Primary','Secondary','Sniper')}
bp=dump(C+'Blueprints/Characters/BasePlayer',u,lambda n,c: n.startswith('Default__') or n=='CharMoveComp')
pc=bp['Default__BasePlayer_C']['props']
PK=[k for k in pc if re.search(r'Sprint|Lean|LowReady|Crouch|Jump|Land|Speed|Meshspace|MeshSpace|Shake|Fov|fov|Freelook|Walk|Run|Strafe|Interp|Bob',k)]
player={k:pc[k] for k in PK}
cm=bp['CharMoveComp']['props']
fpnodes={k:v for k,v in FP.items() if re.match(r'AnimGraphNode_(LazySpringBone|ModifyBone|PivotBone|ApplyAdditive|BlendSpacePlayer)',k)}
fpvars={k:FP[k] for k in ['CameraRotationRate','AimInterp','IdlePlayrate','StanceAdjustment','ShakeAmount','DirBlendTime','LeanHeadOffset','CameraBobTrans','WeaponBobTrans','WeaponBobRot'] if k in FP}
seqs=sorted(set(str(v.get('Sequence')) for k,v in FP.items() if k.startswith('AnimGraphNode_Sequence') and isinstance(v,dict) and v.get('Sequence') not in (0,None,'None')))
gm9=dump(C+'Blueprints/Items/Attachments/Pistol_Suppressor_GM9',u,lambda n,c:n.startswith('Default__'))
gm9={k:v for k,v in list(gm9.values())[0]['props'].items() if not isinstance(v,str) or 'Offset' in k}
names_only={
 'BaseItem (weapon bob, never overridden on any of 68 weapons -> C++ defaults)':BOB+['CameraBobAdditionFactor (WeaponAttachment)'],
 'PlayerCharacter':['CameraBobTrans','WeaponBobTrans','WeaponBobRot','CameraBobRot','bIsPelvisFPMovementBobActive','MeshSpaceMovementMultiplier1P','LowReadyTraceDistance','SpeedModifier_Crouch','SpeedModifier_SprintMax','SprintSpeedRampUpMultiplier','MaxCrouchRunSpeedPercent','NegativeVelocityRequiredForLandingSound'],
 'ReadyOrNotCharacter':['QuickLeanAmount','QuickLeanIntensity','QuickLeanInterpSpeed','FreeLeanX','FreeLeanZ','LowReadyDistance'],
 'ReadyOrNotAnimInstance':['bHasPrelanded','bJumpRecoveryActive','JumpRecoveryAnimTime','JumpRecoveryStrength','JumpRecoveryTime','SprintAlpha','SprintFPAlpha','PelvisMovementBobAlpha','MovementJogThreshold'],
 'RoNAnimInstance_PlayerFP':['LeanAngleY','LeanAngleZ','MeshPostureLeanOffset','MeshWeaponLeanOffset','MeshWeaponLeanRotation','LazySpringStrength','ADS_Movement_Weight'],
 'sprint_to_fire_delay':'no property found in mappings (only SpeedModifier_SprintTime, SprintFovInterpTime); likely gated by the sprint-stop additive animation'}
out={'_meta':{'game':'Ready or Not UE5.3, read-only','absent_means':'property missing = not overridden = C++ default (not read)','units':'cm, cm/s, degrees, seconds; MeshspaceTransform = FTransform (Translation cm)'},
 'player':{'source':'ReadyOrNot/Content/Blueprints/Characters/BasePlayer.uasset (Default__BasePlayer_C, native PlayerCharacter)','values':player},
 'character_movement':{'source':'BasePlayer.uasset export CharMoveComp (ReadyOrNotCharMovementComp)','values':cm},
 'fp_anim_blueprint':{'source':'ReadyOrNot/Content/Blueprints/Animation/Revised/Graphs_Body/ANIMBP_Player_FP_Master.uasset (Default__, parent RoNAnimInstance_PlayerFP)',
   'note':'CDO decoded up to the end of its property block (19819 of 39292 bytes; the rest is not property data and was not interpreted). Node Translation/Rotation = 0 means pin-driven at runtime.',
   'blueprint_variables':fpvars,'procedural_nodes':fpnodes,'baked_additive_sequences':seqs},
 'attachment_example':{'source':'ReadyOrNot/Content/Blueprints/Items/Attachments/Pistol_Suppressor_GM9.uasset','values':gm9},
 'weapons':weap,'names_only':names_only,
 'damage_curve_units':'cm. A G19 curve read in cm falls off from 1.5 m to 50 m. The 870 buck curve runs 25 m to 151 m. Read in metres, the pistol would fall off out to 5 km. There is no UI range text to cross-check (weapons only have velocity/RPM text).'}
json.dump(out,open(sys.argv[1],'w'),indent=1,default=str); print('ok',len(weap),len(player),len(seqs))
