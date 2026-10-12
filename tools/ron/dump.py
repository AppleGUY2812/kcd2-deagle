import sys,json,os,subprocess
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from uasset import Pkg,R,bp_props
from ronpaths import WORK,OUT,PAKS,USMAP,OOZ,TOOLS   # paths: tools/ron/ronpaths.py (env RON_WORK, RON_PAKS, ...)
SCR=WORK   # the folder that holds ronout/ and the intermediate JSON files
def gamepath(pkg):
    if pkg.startswith('/Game/'): return OUT+'/ReadyOrNot/Content/'+pkg[6:]
    parts=pkg.strip('/').split('/',1)  # plugin mount e.g. /ReadyOrNotDLC4/...
    return None
def ensure(base):
    if os.path.exists(base+'.uasset'): return True
    rel=base[len(OUT)+1:]
    subprocess.run([sys.executable,'-I',TOOLS+'/ronx.py',PAKS,OUT,OOZ,'^'+rel.replace('.','\\.')+r'\.(uasset|uexp)$'],capture_output=True)
    return os.path.exists(base+'.uasset')
_cache={}
def class_schema(p,clsref,u):
    """returns (nativeName, extra) for a class reference (import or export)"""
    if clsref>0:
        e=p.exports[clsref-1]; extra=bp_props(p,e); sup=e['sup']
        n,ex2=class_schema(p,sup,u); return n,extra+ex2
    cn,on,outer=p.imports[-clsref-1]
    if on in u.structs: return on,[]
    # BP class in another package
    pkgname=p.imports[-outer-1][1]
    key=(pkgname,on)
    if key in _cache: return _cache[key]
    base=gamepath(pkgname)
    if not base or not ensure(base): raise KeyError('cannot load parent '+pkgname)
    q=Pkg(base,u); ce=[e for e in q.exports if e['name']==on][0]
    n,ex=class_schema(q,q.exports.index(ce)+1,u); _cache[key]=(n,ex); return n,ex
def dump(base,u,only=None):
    p=Pkg(base,u); res={}
    for i,e in enumerate(p.exports):
        cls=p.ref(e['cls'])
        if only and not only(e['name'],cls): continue
        if cls in('BlueprintGeneratedClass','AnimBlueprintGeneratedClass','Function','SimpleConstructionScript','SCS_Node','UserDefinedStruct'): continue
        try:
            n,extra=class_schema(p,e['cls'],u); r=R(p.data(e)); d=p.props(r,n,extra)
            res[e['name']]=dict(cls=cls,native=n,consumed=f"{r.o}/{e['size']}",props=d)
        except Exception as x: res[e['name']]=dict(cls=cls,error=repr(x))
    return res
if __name__=='__main__':
    u=U(USMAP)
    for b in sys.argv[1:]:
        print(json.dumps({os.path.basename(b):dump(b,u)},default=str,indent=1))
