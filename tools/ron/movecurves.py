import sys,os,json,math
sys.path.insert(0,os.path.dirname(__file__)); import animdec; from usmap import U; from dump import OUT,USMAP; from skel import comp,idx,qmul,qconj,qrot,S
u=U(USMAP); bones=[tuple(b) for b in S['bones']]
def q2m(q):
    x,y,z,w=q
    return [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
M=[-1,1,1]   # UE component (X left / -right, Y forward, Z up) -> ours (x right, y forward, z up)
def to_ours_rot(R):   # R' = M R M, then our LocalRot: R = Rz(yaw) Rx(pitch) Ry(roll)
    Rp=[[M[i]*R[i][j]*M[j] for j in range(3)] for i in range(3)]
    p=math.degrees(math.asin(max(-1,min(1,Rp[2][1])))); r=math.degrees(math.atan2(-Rp[2][0],Rp[2][2])); y=math.degrees(math.atan2(-Rp[0][1],Rp[1][1]))
    return [round(p,3),round(r,3),round(y,3)]
def bone_curve(res,bn,nframes):
    tr=res.get(bn,{}); par=bones[idx(bn)][1]; pq,_=comp(par)
    out=[]
    for f in range(nframes):
        T=animdec.sample(tr['T'],f) if 'T' in tr else [0,0,0]
        Q=animdec.sample(tr['R'],f) if 'R' in tr else [0,0,0,1]
        n=math.sqrt(sum(c*c for c in Q)) or 1; Q=[c/n for c in Q]
        tc=qrot(pq,T); qc=qmul(qmul(pq,tuple(Q)),qconj(pq))   # parent-space delta -> component space
        off=[round(M[i]*tc[i]/100,4) for i in range(3)]       # m, our axes
        out.append({'t':round(f/30,4),'off':off,'rot':to_ours_rot(q2m(qc))})
    return out
res_all={}
for line in open(sys.argv[1]):
    gp=line.strip(); 
    if not gp: continue
    base=OUT+'/'+gp
    try:
        res,L,fps,nf,add=animdec.decode(base,u,bones)
        res_all[os.path.basename(gp)]={'path':gp.replace('ReadyOrNot/Content/','/Game/'),'length':L,'fps':fps,'frames':nf,'additive':add,
            'weapon_right':bone_curve(res,'weapon_right',nf) if 'weapon_right' in res else None,
            'fp_camera':bone_curve(res,'fp_camera',nf) if 'fp_camera' in res else None,
            'tracks':sorted(res)}
    except Exception as x: res_all[os.path.basename(gp)]={'error':repr(x)}
json.dump(res_all,open(sys.argv[2],'w'),indent=0)
for k,v in res_all.items():
    if 'error' in v: print(k,v['error']); continue
    w=v['weapon_right'] or []; c=v['fp_camera'] or []
    def ext(cur,key,i): 
        vals=[s[key][i] for s in cur] if cur else [0]; return (round(min(vals),3),round(max(vals),3))
    print(f"{k:40s} {v['length']:.2f}s wr off x{ext(w,'off',0)} y{ext(w,'off',1)} z{ext(w,'off',2)} rot p{ext(w,'rot',0)} r{ext(w,'rot',1)} y{ext(w,'rot',2)} | cam rot p{ext(c,'rot',0)} r{ext(c,'rot',1)} z{ext(c,'off',2)}")
