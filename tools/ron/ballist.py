import sys,os,json,glob
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import dump,OUT,USMAP; from dt import read_dt
u=U(USMAP); C=OUT+'/ReadyOrNot/Content/'
res={}; errs=[]
def bases(pat): return sorted(set(f[:-7] for f in glob.glob(C+pat+'.uasset')))
def keep(n,c): return n.startswith('Default__') or c in('CurveFloat','PhysicalMaterial','ArmourMaterial','PenetrationData') or 'Physical' in str(c)
for grp,pat in [('damage_curves','Blueprints/Items/DamageCurves/*'),('armor','Blueprints/Items/ArmorRevised/*'),('armor','Blueprints/Items/ArmorRevised/Helmets/*'),
                ('armour_materials','Blueprints/Items/ArmorRevised/ArmorMaterials/*'),('physmats','ThirdParty/BallisticsVFX/PhysMats/*'),
                ('projectile','Blueprints/Items/Projectiles/*'),('damage_types','Blueprints/Logic/ArmorPiercingBulletDamage'),('penetration','Blueprints/Info/PenetrationData')]:
    for b in bases(pat):
        d=dump(b,u,keep); res.setdefault(grp,{})[os.path.relpath(b,OUT)]=d
        for k,v in d.items():
            if 'error' in v or '__stop__' in v.get('props',{}): errs.append((os.path.basename(b),k,v.get('error') or v['props']['__stop__']))
for t in ['AmmoDataTable','SuspectArmourDataTable']:
    res.setdefault('datatables',{})['ReadyOrNot/Content/Blueprints/DataTables/'+t]=read_dt(C+'Blueprints/DataTables/'+t,u)
json.dump(res,open(sys.argv[1],'w'),default=str,indent=1)
print('groups',{k:len(v) for k,v in res.items()}); print('errors',len(errs)); [print(' ',e) for e in errs[:30]]
