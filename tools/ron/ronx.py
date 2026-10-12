# usage: ronx.py <paksdir> <outdir> <libooz> <regex>   -- extracts matching files (read-only on paks)
# Only the game's own pakchunk<N>-Windows.pak files are read. The user's Paks folder also holds mod paks
# (pakchunk97/99/999/9999-*), and some replace vanilla files at the same path (Cazanus Visceral Blud: Blood_Standard,
# blood decals and particles; Restoration AIO: the suspect / civilian blueprints). Paks are read in sorted order and a
# later match overwrites an earlier one, so without this filter a mod's file wins. RON_ALL_PAKS=1 reads every .pak.
import sys,struct,os,re,ctypes
paks,outd,lib,pat=sys.argv[1:5]; rx=re.compile(pat,re.I)
ooz=ctypes.CDLL(lib)
def fstr(b,o):
    n,=struct.unpack_from('<i',b,o); o+=4
    if n==0: return '',o
    if n<0: return b[o:o-2*n].decode('utf-16-le')[:-1],o-2*n
    return b[o:o+n-1].decode('utf-8','replace'),o+n
def decode(enc,o):
    v,=struct.unpack_from('<I',enc,o); o+=4
    bs=v&0x3f
    if bs==0x3f: bs,=struct.unpack_from('<I',enc,o); o+=4
    else: bs<<=11
    meth=(v>>23)&0x3f; encd=(v>>22)&1; nb=(v>>6)&0xffff
    def rd(safe):
        nonlocal o
        if safe: x,=struct.unpack_from('<I',enc,o); o+=4
        else: x,=struct.unpack_from('<Q',enc,o); o+=8
        return x
    off=rd(v&(1<<31)); usz=rd(v&(1<<30)); sz=rd(v&(1<<29)) if meth else usz
    hdr=8+8+8+4+20+(4+16*nb if meth else 0)+1+4
    blocks=[]
    if nb==1 and not encd: blocks=[sz]
    elif nb>0:
        for _ in range(nb): x,=struct.unpack_from('<I',enc,o); o+=4; blocks.append(x)
    return dict(off=off,usz=usz,sz=sz,meth=meth,enc=encd,bs=bs,blocks=blocks,hdr=hdr)
n=0
VANILLA=re.compile(r'^pakchunk\d+-Windows\.pak$'); ALL=os.environ.get('RON_ALL_PAKS')=='1'
for p in sorted(os.listdir(paks)):
    if not p.endswith('.pak') or not (ALL or VANILLA.match(p)): continue
    f=open(os.path.join(paks,p),'rb'); size=os.path.getsize(f.name)
    f.seek(size-300); t=f.read(); i=t.rfind(struct.pack('<I',0x5A6F12E1))
    io,isz=struct.unpack_from('<qq',t,i+8)
    names=[t[i+44+32*k:i+76+32*k].split(b'\0')[0].decode() for k in range(5)]
    f.seek(io); b=f.read(isz)
    mount,o=fstr(b,0); o+=4+8
    has,=struct.unpack_from('<i',b,o); o+=4
    if has: o+=8+8+20
    hasf,=struct.unpack_from('<i',b,o); o+=4
    fo,fs=struct.unpack_from('<qq',b,o); o+=16+20
    el,=struct.unpack_from('<i',b,o); o+=4; enc=b[o:o+el]
    f.seek(fo); d=f.read(fs); o=0
    nd,=struct.unpack_from('<i',d,o); o+=4
    for _ in range(nd):
        dn,o=fstr(d,o); nf,=struct.unpack_from('<i',d,o); o+=4
        for _ in range(nf):
            fn,o=fstr(d,o); eo,=struct.unpack_from('<i',d,o); o+=4
            full=(mount+dn+fn).replace('../../../','')
            if not rx.search(full) or eo<0: continue
            e=decode(enc,eo)
            if e['enc']: print('ENCRYPTED',full); continue
            if e['meth']==0:
                f.seek(e['off']+e['hdr']-(0 if True else 0)); data=f.read(e['usz'])
                # uncompressed header is 53 bytes (no block fields)
                f.seek(e['off']+8+8+8+4+20+1+4); data=f.read(e['usz'])
            else:
                m=names[e['meth']-1]; out=bytearray(); pos=e['off']+e['hdr']; left=e['usz']
                for bl in e['blocks']:
                    f.seek(pos); cb=f.read(bl); pos+=bl
                    want=min(e['bs'] or left,left); buf=ctypes.create_string_buffer(want+64)
                    if m.lower()!='oodle':
                        import zlib; out+=zlib.decompress(cb); left-=want; continue
                    r=ooz.ooz_decompress(cb,len(cb),buf,want)
                    if r!=want: print('DECOMP FAIL',full,r,want); break
                    out+=buf.raw[:want]; left-=want
                data=bytes(out)
            dst=os.path.join(outd,full); os.makedirs(os.path.dirname(dst),exist_ok=True)
            open(dst,'wb').write(data); n+=1
print('extracted',n)
