# build_recoil_md.py <ron_recoil.json> > table.md: the recoil table docs/ron/ron_recoil_table.md (as committed in
# bb23dca) from ronrecoil.py output. 9f ran this inline (2026-10-12 13:18 UTC); saved unchanged apart from the argument.
import json,sys
d=json.load(open(sys.argv[1]))
order=['Secondary_G19_V2','Secondary_P229','Secondary_PFC9','Secondary_P250','Secondary_USP','Secondary_M92FS_V2','Secondary_M92A3','Secondary_FiveSeven_V2','Secondary_P99','Secondary_M1911_V2','Secondary_Kimber1911','Secondary_M45A1','Secondary_M2011','Secondary_Python_V2','Secondary_TCR','Primary_MP5A3','Primary_M4A1','Primary_HK416','Primary_870mcs','Primary_B1301']
def g(p,k):
    if k not in p: return '-'
    v=p[k]
    if isinstance(v,list) and v and isinstance(v[0],list): return ' '.join(f'({a:g},{b:g})' for a,b,c in v)
    if isinstance(v,list): return '/'.join(f'{x:g}' for x in v)
    if isinstance(v,float): return f'{v:g}'
    return str(v).replace('_C','')
print('A) Aim/camera kick  [RecoilPattern = (pitch,yaw) deg per shot, cycles]')
print('gun|cal|FireRate s|vel m/s|dmg|kg|RecoilPattern|FirstShotRecoil|RecoilInterpSpeed|RecoilReturnRate|RecoilReturnPercentage|RecoilReturnInterpSpeed|ADSRecoilMultiplier|RecoilMultiplierPitch|FireCameraShake')
for n in order:
    p=d[n]['props']; vel=f"{p['ProjectileMovementSpeed']/100:g}" if 'ProjectileMovementSpeed' in p else '-'
    print('|'.join([p.get('ItemName','')+f' ({n})',g(p,'CartridgeText'),g(p,'FireRate'),vel,g(p,'Damage'),g(p,'ItemWeight'),g(p,'RecoilPattern'),g(p,'FirstShotRecoil'),g(p,'RecoilInterpSpeed'),g(p,'RecoilReturnRate'),g(p,'RecoilReturnPercentage'),g(p,'RecoilReturnInterpSpeed'),g(p,'ADSRecoilMultiplier'),g(p,'RecoilMultiplierPitch'),g(p,'FireCameraShake')]))
print()
print('B) Procedural viewmodel kick (bCalculateProcRecoil=true on all)')
print('gun|FireStrength|FireStrengthFirst|FireTime s|DampStrength|AngleStrength|Randomness|FireADSMod|AngleADSMod|HasBuildup|RotationBuildup xyz|PositionBuildup xyz|BuildupADSMod|BuildupDamp|MeshSpaceAimInterp|InertiaDragAimRotation')
for n in order:
    p=d[n]['props']
    print('|'.join([n.split('_',1)[1]]+[g(p,k) for k in ['RecoilFireStrength','RecoilFireStrengthFirst','RecoilFireTime','RecoilDampStrength','RecoilAngleStrength','RecoilRandomness','RecoilFireADSModifier','RecoilAngleADSModifier','RecoilHasBuildup','RecoilRotationBuildup','RecoilPositionBuildup','RecoilBuildupADSModifier','RecoilBuildupDampStrength','MeshSpaceAimInterp','InertiaDragAimRotation']]))
