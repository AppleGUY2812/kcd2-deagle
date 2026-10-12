import sys,struct,os
def fstr(b,o):
    n,=struct.unpack_from('<i',b,o); o+=4
    if n==0: return '',o
    if n<0: s=b[o:o-2*n].decode('utf-16-le')[:-1]; return s,o-2*n
    return b[o:o+n-1].decode('utf-8','replace'),o+n
out=open(sys.argv[2],'w')
for p in sys.argv[3:]:
    f=open(os.path.join(sys.argv[1],p),'rb'); size=os.path.getsize(f.name)
    f.seek(size-300); t=f.read(); i=t.rfind(struct.pack('<I',0x5A6F12E1))
    io,isz=struct.unpack_from('<qq',t,i+8)
    f.seek(io); b=f.read(isz)
    mount,o=fstr(b,0); o+=4+8
    has,=struct.unpack_from('<i',b,o); o+=4
    if has: o+=8+8+20
    hasf,=struct.unpack_from('<i',b,o); o+=4
    fo,fs=struct.unpack_from('<qq',b,o)
    f.seek(fo); d=f.read(fs); o=0
    nd,=struct.unpack_from('<i',d,o); o+=4
    for _ in range(nd):
        dn,o=fstr(d,o); nf,=struct.unpack_from('<i',d,o); o+=4
        for _ in range(nf):
            fn,o=fstr(d,o); o+=4
            out.write(mount+dn+fn+'\n')
