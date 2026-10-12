# skel_dump.py [skeleton package]: write WORK/ron_skel.json (bone names + parents + reference pose) for skel.py /
# movecurves.py. Default: the player skeleton SKL_RoN_Player_Master_Shared (extracted on demand). Read-only on the paks.
import sys,os,json
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from usmap import U; from uasset import Pkg,R; from dump import OUT,WORK,USMAP,ensure
base=OUT+'/'+(sys.argv[1] if len(sys.argv)>1 else 'ReadyOrNot/Content/ReadyOrNot/Character/SWAT/Character_SWAT_3P/Shared/SKL_RoN_Player_Master_Shared')
assert ensure(base),'cannot extract '+base
u=U(USMAP); p=Pkg(base,u)
e=[x for x in p.exports if p.ref(x['cls'])=='Skeleton'][0]; d=p.data(e); r=R(d); p.props(r,'Skeleton'); r.u('i')
n=r.u('i'); bones=[(p.fn(r),r.u('i')) for _ in range(n)]   # (name, parent index)
m=r.u('i'); pose=[]
for i in range(m):
    q=r.u('dddd'); t=r.u('ddd'); s=r.u('ddd'); pose.append((q,t,s))   # rotation xyzw, translation cm, scale
json.dump({'bones':bones,'pose':pose},open(os.path.join(WORK,'ron_skel.json'),'w'))
print(len(bones),'bones,',m,'pose entries ->',os.path.join(WORK,'ron_skel.json'))
