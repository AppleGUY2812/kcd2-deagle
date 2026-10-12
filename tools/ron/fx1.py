import sys,json,os; sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import dump,OUT,USMAP; from uasset import Pkg
u=U(USMAP)
def imppaths(p):
    m={}
    for cn,on,outer in p.imports:
        if outer<0: m[on]=p.imports[-outer-1][1]+'.'+on
    return m
res={}
for n in ['ImpactEffects_Pistol','AllImpactEffects','ShotgunImpactEffects','ExitImpactEffects','RicochetImpactEffects','DefaultImpactEffects','RunImpactEffects','WalkImpactEffects']:
    b=OUT+'/ReadyOrNot/Content/Blueprints/Logic/'+n; p=Pkg(b,u); paths=imppaths(p)
    d=dump(b,u,lambda x,c:x.startswith('Default__')); cdo=list(d.values())[0]
    props=cdo.get('props',{}); 
    res[n]={'error':cdo.get('error'),'parent':cdo.get('native'),'props':props,'paths':{k:v for k,v in paths.items()}}
json.dump(res,open(sys.argv[1],'w'),indent=1,default=str)
for n,v in res.items(): print(n,v['error'],[(k,x) for k,x in v['props'].items() if not isinstance(x,dict)][:12])
