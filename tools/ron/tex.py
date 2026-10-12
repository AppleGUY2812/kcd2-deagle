# tex.py <list-of-/Game/paths file> <outdir> : export mip0 of Texture2D packages to PNG (read-only on paks)
import sys,os,struct,subprocess,io
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from uasset import Pkg,R
from dump import OUT,SCR,PAKS,USMAP,OOZ,TOOLS,gamepath
from PIL import Image
u=U(USMAP)
DX={'PF_DXT1':71,'PF_DXT3':74,'PF_DXT5':77,'PF_BC4':80,'PF_BC5':83,'PF_BC7':98}
def dresources(h):
    for cnt in range(1,40):
        st=len(h)-8-44*cnt
        if st<0: break
        ver,c=struct.unpack_from('<Ii',h,st)
        if ver==1 and c==cnt: return [struct.unpack_from('<IqqqqiI',h,st+8+44*i) for i in range(cnt)]
    return None
def extract_many(paths):
    need=[p for p in paths if gamepath(p) and not os.path.exists(gamepath(p)+'.uasset')]
    for i in range(0,len(need),60):
        rx='^('+'|'.join(gamepath(p)[len(OUT)+1:].replace('.','\\.') for p in need[i:i+60])+r')\.(uasset|uexp|ubulk)$'
        subprocess.run([sys.executable,'-I',TOOLS+'/ronx.py',PAKS,OUT,OOZ,rx],capture_output=True)
def export(gp,dst):
    base=gamepath(gp); p=Pkg(base,u); e=[x for x in p.exports if 'Texture' in str(p.ref(x['cls']))][0]
    d=p.data(e); r=R(d); props=p.props(r,p.ref(e['cls'])); 
    i=d.find(b'PF_',r.o); n=struct.unpack_from('<i',d,i-4)[0]; pf=d[i:i+n-1].decode(); o=i+n
    sx,sy,packed=struct.unpack_from('<iiI',d,i-4-12)
    if packed & (1<<30): o+=8
    first,nm=struct.unpack_from('<ii',d,o); o+=8
    idx=struct.unpack_from('<i',d,o)[0]; mx,my=max(1,sx>>first),max(1,sy>>first)
    h=open(base+'.uasset','rb').read(); dr=dresources(h)
    fl,so,dso,ss,rs,oi,lf=dr[idx]
    if lf & 0x100: raw=open(base+'.ubulk','rb').read()[so:so+ss]
    else: raw=open(base+'.uexp','rb').read()[so:so+ss]
    if pf in DX:
        hdr=b'DDS '+struct.pack('<7I',124,0x1|0x2|0x4|0x1000|0x80000,my,mx,len(raw),0,1)+b'\0'*44+struct.pack('<8I',32,4,0x30315844,0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)+struct.pack('<5I',DX[pf],3,0,1,0)
        im=Image.open(io.BytesIO(hdr+raw)); im.load()
        if pf=='PF_BC5':
            import numpy as np
            a=np.array(im.convert('RGB')).astype(float)/255*2-1; z=np.sqrt(np.clip(1-a[...,0]**2-a[...,1]**2,0,1))
            a[...,2]=z; im=Image.fromarray(((a+1)/2*255).round().astype('uint8'),'RGB')
    elif pf=='PF_B8G8R8A8': im=Image.frombytes('RGBA',(mx,my),raw,'raw','BGRA')
    elif pf=='PF_G8': im=Image.frombytes('L',(mx,my),raw)
    else: raise ValueError('format '+pf)
    os.makedirs(os.path.dirname(dst),exist_ok=True); im.save(dst)
    return dict(format=pf,size=[mx,my],mips=nm,srgb=props.get('SRGB',True),compression=props.get('CompressionSettings','TC_Default'),png=dst)
if __name__=='__main__':
    import json
    paths=[l.strip() for l in open(sys.argv[1]) if l.strip().startswith('/Game/')]
    extract_many(paths); res={}
    for gp in paths:
        try: res[gp]=export(gp,os.path.join(sys.argv[2],os.path.basename(gp)+'.png'))
        except Exception as x: res[gp]={'error':repr(x)}
    json.dump(res,open(sys.argv[2]+'/_index.json','w'),indent=1,default=str)
    ok=sum('png' in v for v in res.values()); print('exported',ok,'of',len(res)); [print(k,v['error']) for k,v in res.items() if 'error' in v]
