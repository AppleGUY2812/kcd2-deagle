import json,sys
D=json.load(open(sys.argv[1])); o=[]
def f(v):
    if v is None: return '-'
    if isinstance(v,bool): return 'T' if v else 'F'
    if isinstance(v,float): return f'{v:g}'
    if isinstance(v,list): return '/'.join(f(x) for x in v) if v else '[]'
    return str(v)
def g(p,k): return f(p[k]) if k in p else '-'
A=D['ammo']['rows']; C=D['damage_curves']['curves']
def curve(name):
    k=C.get(name)
    return ' '.join(f"{x['t']/100:g}m:{x['v']:g}" for x in k) if k else '-'
o+=['# Ready or Not ballistics read-out','',
'Values decoded from the user\'s Steam install (UE5.3, read-only). Raw data: `ron_ballistics.json` (RoN property names kept).',
'Every asset decoded byte-exact with the community Mappings.usmap (build 113804, 2026-09-04).','',
'**Legend:** `-` = not set on that asset → equals the C++ default in the exe (**not read**). `0`/`F` = explicitly zero/false. Distances cm unless marked m; speeds cm/s.','',
'## 1. Ammo (AmmoDataTable, 41 rows; every field stored per row)','',
'Damage curve = `ExternalCurve` (distance m : damage). Body multipliers H/U/L/A/Ha/Le/F = Head / UpperBody / LowerBody / Arm / Hand / Leg / Foot.','',
'| row | caliber | variety | Damage | damage curve | pellets | spread | PenLevel | PenDist | DurabilityDmg | Ricochet | Spall dmg/r | ArmouredHits | Hits | Artery | Dismember | Wound | H/U/L/A/Ha/Le/F | ragdoll D/H/A/L/T | IgnoresArmour |','|'+'---|'*20]
for n,p in A.items():
    cv=p.get('DamageOverRangeCurve',{}).get('ExternalCurve') or '-'
    o.append('| '+' | '.join([n,g(p,'AmmoCaliber'),g(p,'AmmoVariety'),g(p,'Damage'),f'{cv}: {curve(cv)}' if cv!='-' and cv!=0 else '-',g(p,'ProjectileCount'),g(p,'SpreadPattern'),g(p,'PenetrationLevel'),g(p,'PenetrationDistance'),g(p,'DurabilityDamage'),g(p,'RicochetChance'),g(p,'SpallingDamage')+'/'+g(p,'SpallingRadius'),g(p,'ArmouredHitsChance'),g(p,'HitsChance'),g(p,'ArteryHitChance'),g(p,'DismembermentDamage'),g(p,'WoundSize'),'/'.join(g(p,k) for k in ['HeadDamageMultiplier','UpperBodyDamageMultiplier','LowerBodyDamageMultiplier','ArmDamageMultiplier','HandDamageMultiplier','LegDamageMultiplier','FootDamageMultiplier']),'/'.join(g(p,k) for k in ['DefaultRagdollImpulseStrength','HeadRagdollImpulseStrength','ArmRagdollImpulseStrength','LegRagdollImpulseStrength','TorsoRagdollImpulseStrength']),g(p,'bIgnoresArmour')])+' |')
o+=['','**Deagle-relevant:** `50AEFMJ` / `50AEJHP` (".50 Action Express") rows exist with curves `Curve_Damage_Pistol_50AE(_HP)`, but no player weapon in `Items/WeaponsRevised` uses them.','',
'### All damage curves (distance m : damage)','']
for n,k in C.items(): o.append(f'- `{n}`: {curve(n)}')
W=D['weapons_ballistics']
o+=['','## 2. Weapon-side ballistics (68 weapon CDOs, `Items/WeaponsRevised/<name>`)','',
'Velocity = ProjectileMovementSpeed/100 (m/s). Weapon `Damage`/`DamageOverRange` are the weapon defaults; the loaded ammo row (`CurrentAmmoType`/`AmmunitionTypes`) carries the per-round values.','',
'| weapon | name | cartridge | ammo rows | vel m/s | Damage | weapon curve | HitScan | BulletDrag | PenDist | FirstShotSpread | ADSSpread | VelSpread | Wobble | Tracer |','|'+'---|'*15]
for n,p in W.items():
    v=f"{p['ProjectileMovementSpeed']/100:g}" if 'ProjectileMovementSpeed' in p else '-'
    cv=p.get('DamageOverRange',{}).get('ExternalCurve','-') if isinstance(p.get('DamageOverRange'),dict) else '-'
    o.append('| '+' | '.join([n,g(p,'ItemName'),g(p,'CartridgeText'),g(p,'AmmunitionTypes'),v,g(p,'Damage'),str(cv),g(p,'bHitScan'),g(p,'BulletDrag'),g(p,'PenetrationDistance'),g(p,'FirstShotSpread'),g(p,'ADSSpreadMultiplier'),g(p,'VelocitySpreadMultiplier'),g(p,'Wobble'),g(p,'bSpawnTracer')])+' |')
o+=['','## 3. Armour','','### Plate / soft materials (`ArmorRevised/ArmorMaterials/AM_*`, class ArmourMaterial)','',
'| material | DamageReduction | MinDamageReduction | DurabilityEnabled | Durability | ArmourLevel | SpallingChance | MoveSpeedMod | MoveAccelMod |','|'+'---|'*9]
for n,p in D['armour_materials']['items'].items():
    o.append('| '+' | '.join([n]+[g(p,k) for k in ['DamageReduction','MinDamageReduction','bDurabilityEnabled','Durability','ArmourLevel','SpallingChance','MovementSpeedModifier','MovementAccelerationModifier']])+' |')
o+=['','### Player vests & helmets (CDOs under `ArmorRevised/`)','','| item | name | MaxArmourCoverage | ArmourCoverage | CarrierMaterial | Durability | DamageReduction | MoveSpeedMult | Heavy | weight kg | other |','|'+'---|'*11]
for n,p in D['armor_items']['items'].items():
    other={k:p[k] for k in ('BlockAnyDamageFrom','MultiplyStunDamageFrom') if k in p}
    o.append('| '+' | '.join([n,g(p,'ItemName'),g(p,'MaxArmourCoverage'),g(p,'ArmourCoverage'),g(p,'CarrierMaterial'),g(p,'Durability'),g(p,'DamageReduction'),g(p,'MovementSpeedMultiplier'),g(p,'bIsHeavy'),g(p,'ItemWeight'),json.dumps(other) if other else ''])+' |')
o+=['','Base vest `BaseBodyArmor` sets CarrierMaterial = AM_Kevlar. The plate (`ArmourMaterial`) and coverage (`ArmourCoverage`, enum EArmourCoverage: AC_FrontBack / AC_FrontBackSides…) are chosen in the loadout (`SavedLoadout.ArmourMaterial/ArmourCoverage`), not fixed per vest.','',
'### Suspect armour (`DataTables/SuspectArmourDataTable`)','','| row | helmet | ArmourLevel | Durability | DamageMultiplier | SpallingChance | MoveSpeedMult |','|'+'---|'*7]
for n,p in D['suspect_armour']['rows'].items():
    o.append('| '+' | '.join([n]+[g(p,k) for k in ['bIsHelmet','ArmourLevel','Durability','DamageMultiplier','SpallingChance','MovementSpeedMultiplier']])+' |')
PEN=D['penetration']['materials']; S=D['physical_surfaces']['map']
o+=['','## 4. Surfaces (`Info/PenetrationData`, struct MaterialPenetration per physical surface)','',
'Surfaces not listed here are not overridden → all-default struct (defaults not read). Surface names ↔ SurfaceTypeN from `Config/DefaultEngine.ini`.','',
'| surface | bIsPenetrable | PenetrationDensity | PenetrationMultiplier | ArmourLevel | bCanRicochet | RicochetChanceMult | SpallingChance |','|'+'---|'*8]
for n,p in PEN.items():
    o.append('| '+' | '.join([n]+[g(p,k) for k in ['bIsPenetrable','PenetrationDensity','PenetrationMultiplier','ArmourLevel','bCanRicochet','RicochetChanceMultiplier','SpallingChance']])+' |')
o+=['','Surface ids: '+', '.join(f'{k[11:]}={v[4:]}' for k,v in S.items()),'',
'Impact FX naming: `Blueprints/Logic/ImpactEffects_<class>` (e.g. ImpactEffects_Pistol), `ExitImpactEffects`, `RicochetImpactEffects`, `AllImpactEffects` (projectile); armour hits use `P_VFX_Impact_<Ceramic|Steel|Galvanized|Concrete_Soft>`.','',
'## 5. Body / health','',
'- Health (CharacterHealthComponent, Resource/MaxResource): **BasePlayer 160**, **CyberneticsSuspect_V2 160** (LowResourceThreshold 0.35, bEnableIncapacitation T), **CyberneticsSwat_V2 160**, Civilian: not overridden (default, not read; incapacitation enabled). AI tables (e.g. AIDataTable_Hotel) list Health 160 for every row, bOverrideHealth F, and pick suspect vests from SuspectArmourDataTable rows (`Armour_3A`, `Armour_4`).',
'- Per-zone damage multipliers live on the **ammo row** (section 1: Head/UpperBody/LowerBody/Arm/Hand/Leg/Foot). Typical 9x19FMJ: '+'/'.join(g(A['9x19FMJ'],k) for k in ['HeadDamageMultiplier','UpperBodyDamageMultiplier','LowerBodyDamageMultiplier','ArmDamageMultiplier','HandDamageMultiplier','LegDamageMultiplier','FootDamageMultiplier'])+'.',
'- Limb health (`LimbHealthData` for Head/LeftArm/RightArm/LeftLeg/RightLeg: Health, MaxHealth, LowHealthThreshold, LimbDamageMultiplier, Tickets…): **names only**, no character overrides them → C++ defaults.',
'- Ragdoll impulse per hit: ammo row `*RagdollImpulseStrength` (mostly 3000) × weapon `RagdollImpulseMultiplier` / `BulletPhysicsImpulseMultiplier`; AP damage type DamageImpulse -800.',
'- Hit reactions / flinch / stagger: **names only** (`ReadyOrNotCharacterAnimData.HitReaction_<zone>`, `Flinches`, `Death_Bleedout_<zone>` are animation references; `IncapacitationHealthMultiplier`, `BoneSuppressionAmount`, `DismembermentDamageMap` not set in the decoded CDOs). No numeric stagger threshold found in data.','',
'## 6. How a bullet is simulated (from data)','',
'- Weapons are **`bHitScan = T`** (G19 and others) but also define `BulletProjectile = Projectile_Base_C`. Projectile_Base: BulletProjectile class, sphere collision r=10 with CCD, InitialLifeSpan 5 s, RequiredAngleToDeflect 30°, PercentageToDeflect 1, DeflectionAmount 1, HitAngleMultiplier 1, wizz min distance 500. Its `BulletProjectileMovementComponent` has **no overrides** (gravity scale, sub-stepping, MaxSimulationTimeStep = C++ defaults, not read).',
'- Most likely flow (inference from field names, native code not read): hitscan trace at fire → penetration per surface using ammo `PenetrationLevel`/`PenetrationDistance` against surface `PenetrationDensity`/`ArmourLevel`; ricochet if surface `bCanRicochet` and roll < ammo `RicochetChance` × surface `RicochetChanceMultiplier` and incidence angle past 30°; spall from `SpallingChance` × ammo `SpallingDamage/Radius`. Damage = ammo curve(distance) × zone multiplier, then armour: if ammo PenetrationLevel < armour level → blocked, reduced by ArmourMaterial DamageReduction (floor MinDamageReduction), durability loses `DurabilityDamage` per hit.',
'- Max range: no explicit field; curves are flat past their last key (5000 cm = 50 m for most pistols).','',
'## Names only (no value in data)','','LimbHealthData fields; BulletProjectileMovementComponent (ProjectileGravityScale, MaxSimulationTimeStep, MaxSimulationIterations); BulletProjectile Drag/SpeedLossMultiplierPerSurface/DamageLossMultiplierPerSurface/VelocityRequiredToRespawn (not overridden on Projectile_Base); flinch/stagger thresholds; ArmourCoverage per body part (only enum EArmourCoverage per vest); the C++ formulas.']
open(sys.argv[2],'w').write('\n'.join(o)+'\n'); print(len(o),'lines')
