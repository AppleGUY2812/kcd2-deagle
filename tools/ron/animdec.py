# Decode UE5 AnimSequence compressed with AnimCompress_PerTrackCompression (cooked, unversioned). Read-only.
import struct,math,json,sys,os
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from uasset import Pkg,R
VARIANT={'ipack':'xhigh','idec':'off'}   # interval fixed32: bit order, decode formula
def align4(o): return (o+3)&~3
def dec_track(s,o,kind,nframes):
    h,=struct.unpack_from('<I',s,o); o0=o; o+=4
    nk=h&0xFFFFFF; ff=(h>>24)&0xF; kf=(h>>28)&0xF; mask=ff&7 if ff&7 else 7; tm=bool(ff&8)
    comps=[i for i in range(3) if mask>>i&1]; keys=[]
    if kf==1:   # Float96: float per present comp
        for k in range(nk):
            v=[0.0,0.0,0.0]
            for c in comps: v[c],=struct.unpack_from('<f',s,o); o+=4
            keys.append(v)
    elif kf==2: # Fixed48: uint16 per present comp
        for k in range(nk):
            v=[0.0,0.0,0.0]
            for c in comps:
                q,=struct.unpack_from('<H',s,o); o+=2
                v[c]=(q-32767)/32767.0 if kind=='R' else (q-32767)/32767.0*128   # translation fixed48 range guess
            keys.append(v)
    elif kf==3: # IntervalFixed32: min,range per present comp then packed 11/11/10
        mr={}
        for c in comps: mn,rg=struct.unpack_from('<ff',s,o); o+=8; mr[c]=(mn,rg)
        for k in range(nk):
            P,=struct.unpack_from('<I',s,o); o+=4
            if VARIANT['ipack']=='xhigh': ux,uy,uz=(P>>21)&0x7FF,(P>>10)&0x7FF,P&0x3FF
            else: ux,uy,uz=P&0x7FF,(P>>11)&0x7FF,(P>>22)&0x3FF
            bits=[11,11,10]; v=[0.0,0.0,0.0]
            for c,uv in zip(range(3),(ux,uy,uz)):
                if c not in mr: continue
                N=bits[c]
                f=(uv-((1<<(N-1))-1))/((1<<(N-1))-1) if VARIANT['idec']=='off' else uv/((1<<N)-1)
                v[c]=f*mr[c][1]+mr[c][0]
            keys.append(v)
    elif kf==6: keys=[[0,0,0]]
    else: raise ValueError('key format %d'%kf)
    frames=list(range(nk)) if nk==nframes else ([0] if nk==1 else None)
    if tm:
        frames=list(s[o:o+nk]) if nframes<=256 else list(struct.unpack_from('<%dH'%nk,s,o)); o+=nk if nframes<=256 else 2*nk
    elif frames is None:  # evenly spread over the sequence (e.g. 2 keys = first, last)
        frames=[round(i*(nframes-1)/(nk-1)) for i in range(nk)]
    out=[]
    for fr,v in zip(frames,keys):
        if kind=='R':
            w2=1-(v[0]**2+v[1]**2+v[2]**2); v=v+[math.sqrt(max(0,w2))]
        out.append((fr,v))
    return out,kf,align4(o)-o0
def sample(keys,f):
    if f<=keys[0][0]: return keys[0][1]
    for (f0,a),(f1,b) in zip(keys,keys[1:]):
        if f0<=f<=f1:
            t=(f-f0)/max(1,f1-f0); return [x+(y-x)*t for x,y in zip(a,b)]
    return keys[-1][1]
def quat_to_euler_deg(q):   # UE rotator: pitch (Y), yaw (Z), roll (X)
    x,y,z,w=q
    sp=2*(w*y-z*x); pitch=math.degrees(math.asin(max(-1,min(1,sp))))
    yaw=math.degrees(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z)))
    roll=math.degrees(math.atan2(2*(w*x+y*z),1-2*(x*x+y*y)))
    return [round(pitch,3),round(yaw,3),round(roll,3)]
def decode(base,u,bones):
    p=Pkg(base,u); e=p.exports[0]; d=p.data(e); r=R(d); pr=p.props(r,'AnimSequence')
    o=r.o+4+16+2   # guid bool, raw data guid, strip flags
    bser,rawsz,ntr=struct.unpack_from('<iii',d,o); o+=12
    tmap=struct.unpack_from('<%di'%ntr,d,o); o+=4*ntr
    ncurve,=struct.unpack_from('<i',d,o); o+=4
    assert ncurve==0,'curves not handled'
    nb,=struct.unpack_from('<i',d,o); o+=4; o+=4; s=d[o:o+nb]   # one int32 before the stream
    length=pr.get('SequenceLength'); fps=(pr.get('TargetFrameRate') or {}).get('Numerator',30)
    nframes=round(length*fps)+1
    offs=struct.unpack_from('<%di'%(2*ntr),s,0); sc=struct.unpack_from('<%di'%ntr,s,8*ntr); basep=12*ntr
    res={}
    for t in range(ntr):
        bn=bones[tmap[t]][0]; tr={}
        for kind,off in (('T',offs[2*t]),('R',offs[2*t+1]),('S',sc[t])):
            if off<0: continue
            keys,kf,sz=dec_track(s,basep+off,kind,nframes); tr[kind]=keys
        res[bn]=tr
    return res,length,fps,nframes,pr.get('AdditiveAnimType')
