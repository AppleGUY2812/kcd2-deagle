import sys,json,glob,os
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from uasset import Pkg,R,bp_props
u=U(sys.argv[1]); out={}
for f in sorted(glob.glob(sys.argv[2]+'/*.uasset')):
    base=f[:-7]; name=os.path.basename(base)
    try:
        p=Pkg(base,u)
        cls=[e for e in p.exports if p.ref(e['cls'])=='BlueprintGeneratedClass'][0]
        sup=p.ref(cls['sup']); cdo=[e for e in p.exports if e['name'].startswith('Default__')][0]
        extra=bp_props(p,cls); r=R(p.data(cdo)); d=p.props(r,sup,extra)
        out[name]=dict(super=sup,consumed=f'{r.o}/{cdo["size"]}',props=d)
    except Exception as x: out[name]=dict(error=repr(x))
json.dump(out,open(sys.argv[3],'w'),indent=1,default=str)
for k,v in out.items(): print(k,v.get('super'),v.get('consumed'),v.get('error',''),v.get('props',{}).get('__stop__',''))
