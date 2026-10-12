import struct
PT=['Byte','Bool','Int','Float','Object','Name','Delegate','Double','Array','Struct','Str','Text','Interface','MulticastDelegate','WeakObject','LazyObject','AssetObject','SoftObject','UInt64','UInt32','UInt16','Int64','Int16','Int8','Map','Set','Enum','FieldPath','Optional','Utf8Str','AnsiStr']
class U:
    def __init__(s,path):
        b=open(path,'rb').read(); mg,ver=struct.unpack_from('<HB',b,0); o=3
        assert mg==0x30C4
        if ver>=1:
            hv,=struct.unpack_from('<i',b,o); o+=4
            assert hv==0, 'versioned usmap not handled'
        comp,cs,ds=struct.unpack_from('<BII',b,o); o+=9; assert comp==0
        d=b[o:o+ds]; o=0; s.ver=ver
        def u8():
            nonlocal o; v=d[o]; o+=1; return v
        def u16():
            nonlocal o; v,=struct.unpack_from('<H',d,o); o+=2; return v
        def u32():
            nonlocal o; v,=struct.unpack_from('<I',d,o); o+=4; return v
        n=u32(); s.names=[]
        for _ in range(n):
            l=u16() if ver>=3 else u8(); s.names.append(d[o:o+l].decode('utf-8','replace')); o+=l
        nm=lambda: (lambda i: None if i==0xFFFFFFFF else s.names[i])(u32())
        s.enums={}
        for _ in range(u32()):
            e=nm(); c=u16() if ver>=4 else u8()
            s.enums[e]=[nm() for _ in range(c)]
        def ptype():
            t=PT[u8()]
            if t=='Enum': return (t,ptype(),nm())
            if t=='Struct': return (t,nm())
            if t in('Array','Set','Optional'): return (t,ptype())
            if t=='Map': return (t,ptype(),ptype())
            return (t,)
        s.structs={}
        for _ in range(u32()):
            name=nm(); sup=nm(); pc=u16(); spc=u16(); props=[]
            for _ in range(spc):
                idx=u16(); ad=u8(); pn=nm(); props.append((idx,ad,pn,ptype()))
            s.structs[name]=(sup,pc,props)
    def flat(s,name):
        # list of (pname,ptype) indexed by schema index, super first
        chain=[]
        while name:
            if name not in s.structs: raise KeyError(name)
            chain.append(name); name=s.structs[name][0]
        out=[]
        for c in chain:  # most-derived first (matches engine/CUE4Parse)
            sup,pc,props=s.structs[c]; base=len(out); out+= [None]*pc
            for idx,ad,pn,pt in props:
                for k in range(ad): out[base+idx+k]=(pn if ad==1 else f'{pn}[{k}]',pt)
        return out
