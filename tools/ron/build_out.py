import json,re,os,sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from ronpaths import WORK,OUT
S=WORK   # ron_ballistics_raw.json, ron_weapons_all.json
raw=json.load(open(S+'/ron_ballistics_raw.json')); W=json.load(open(S+'/ron_weapons_all.json'))
ini=open(OUT+'/ReadyOrNot/Config/DefaultEngine.ini',encoding='utf-8',errors='replace').read()
surf={m[0]:m[1] for m in re.findall(r'Type=(SurfaceType\d+),Name="([^"]+)"',ini)}
def P(group,key): 
    for f,ex in raw[group].items():
        if f.endswith('/'+key) or f.endswith(key): return ex
CUR={os.path.basename(f):ex[os.path.basename(f)]['props']['FloatCurve']['Keys'] for f,ex in raw['damage_curves'].items()}
AM=raw['datatables']['ReadyOrNot/Content/Blueprints/DataTables/AmmoDataTable']['rows']
SA=raw['datatables']['ReadyOrNot/Content/Blueprints/DataTables/SuspectArmourDataTable']['rows']
PEN=list(raw['penetration'].values())[0]['PenetrationData']['props']
WK=['ItemName','CartridgeText','AmmunitionTypes','Damage','DamageOverRange','FireRate','ProjectileMovementSpeed','BulletDrag','bHitScan','PenetrationDistance','Wobble','InitialWobbleDelay','BulletPhysicsImpulseMultiplier','RagdollImpulseMultiplier','bArmorPiercing','DamageSeverityMultiplier','DamageSeverityChance','BleedoutDamageMultiplier','BleedoutDamageChance','FirstShotSpread','SpreadPattern','ADSSpreadMultiplier','VelocitySpreadMultiplier','SpreadReturnRate','BulletsFiredUntilFullyAccurate','bSpawnTracer','bNoSpawnTracerForFiringPlayer','bAttachBulletOnHit','bDestroyBulletOnHit','BulletProjectile','ArmorPiercingBulletProjectile','ArmorPiercingDamageType','CurrentAmmoType','LockIntegrityMinDamage','LockIntegrityMaxDamage']
weap={n:{'source':f'ReadyOrNot/Content/Blueprints/Items/WeaponsRevised/{n}.uasset (Default__{n}_C)','parent':v['super'],**{k:v['props'][k] for k in WK if k in v['props']}} for n,v in W.items() if n.split('_')[0] in('Primary','Secondary','Sniper') and 'props' in v}
out={
 '_meta':{'game':'Ready or Not (UE5.3), Steam install, read-only','mappings':'Unofficial Modding Guide Mappings.usmap build 113804 (2026-09-04)',
  'decode':'pak (Oodle via ooz) -> unversioned properties; every export decoded byte-exact (consumed == size minus 4/8-byte trailer)',
  'absent_means':'A property missing from an object = not overridden, i.e. it equals the C++ class/struct default, which lives in the exe and was NOT read. 0/false shown = explicitly zero.',
  'units':{'distance':'cm (UE)','speed':'cm/s','curve_time':'distance in cm','ItemWeight':'kg','FireRate':'seconds between shots'}},
 'ammo':{'source':'ReadyOrNot/Content/Blueprints/DataTables/AmmoDataTable.uasset (RowStruct AmmoTypeData; all fields serialized per row)','rows':AM},
 'damage_curves':{'source':'ReadyOrNot/Content/Blueprints/Items/DamageCurves/<name>.uasset (CurveFloat; t = distance cm, v = damage)','curves':CUR},
 'penetration':{'source':'ReadyOrNot/Content/Blueprints/Info/PenetrationData.uasset (PenetrationData, struct MaterialPenetration per surface)','struct_fields':['bIsPenetrable','PenetrationDensity','PenetrationMultiplier','ArmourLevel','bCanRicochet','RicochetChanceMultiplier','SpallingChance'],'materials':PEN},
 'physical_surfaces':{'source':'ReadyOrNot/Config/DefaultEngine.ini [PhysicalSurfaces]','map':surf,
   'physmat_assets':{os.path.basename(f):list(ex.values())[0]['props'] for f,ex in raw['physmats'].items()}},
 'armour_materials':{'source':'ReadyOrNot/Content/Blueprints/Items/ArmorRevised/ArmorMaterials/AM_*.uasset (ArmourMaterial)','items':{os.path.basename(f):list(ex.values())[0]['props'] for f,ex in raw['armour_materials'].items()}},
 'armor_items':{'source':'ReadyOrNot/Content/Blueprints/Items/ArmorRevised/**.uasset CDOs (SWATArmour / helmets)','items':{os.path.basename(f):list(ex.values())[0]['props'] for f,ex in raw['armor'].items()}},
 'suspect_armour':{'source':'ReadyOrNot/Content/Blueprints/DataTables/SuspectArmourDataTable.uasset (SuspectArmourData)','rows':SA},
 'projectile':{'source':'ReadyOrNot/Content/Blueprints/Items/Projectiles/Projectile_Base.uasset (BulletProjectile; Movement = BulletProjectileMovementComponent, no overrides)','objects':{k:v.get('props') for k,v in list(raw['projectile'].values())[0].items()}},
 'damage_types':{k:v['props'] for f,ex in raw['damage_types'].items() for k,v in ex.items()},
 'weapons_ballistics':weap,
}
json.dump(out,open(sys.argv[1],'w'),indent=1,default=str)
print('ok',len(AM),len(CUR),len(PEN),len(weap))
