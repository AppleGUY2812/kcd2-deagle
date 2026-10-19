import struct,sys
from usmap import U
NATIVE_SIZES={'KeyHandleMap':0,'Vector':24,'Vector2D':16,'Vector4':32,'Rotator':24,'Quat':32,'LinearColor':16,'Color':4,'Guid':16,
 'IntPoint':8,'IntVector':12,'Box':49,'Box2D':33,'FrameNumber':4,'DateTime':8,'Timespan':8,'Vector3f':12,'Vector2f':8,
 'RichCurveKey':31,'SimpleCurveKey':8,'Plane':32,'Vector_NetQuantize':24,'Vector_NetQuantize10':24,'Vector_NetQuantize100':24,'Vector_NetQuantizeNormal':24}
class R:
    def __init__(s,b,o=0): s.b=b; s.o=o
    def u(s,fmt):
        v=struct.unpack_from('<'+fmt,s.b,s.o); s.o+=struct.calcsize('<'+fmt); return v if len(v)>1 else v[0]
    def fstr(s):
        n=s.u('i')
        if n==0: return ''
        if n<0: v=s.b[s.o:s.o-2*n].decode('utf-16-le')[:-1]; s.o-=2*n; return v
        v=s.b[s.o:s.o+n-1].decode('utf-8','replace'); s.o+=n; return v
class Pkg:
    dbg=False
    def __init__(s,path,um):
        s.um=um; h=open(path+'.uasset','rb').read(); s.ex=open(path+'.uexp','rb').read()
        r=R(h,0x1c); s.hdrsize=r.u('i'); r.fstr(); s.flags=r.u('I')
        nc,no,_,_,_,_,ec,eo,ic,io=r.u('10i')
        r.o=no; s.names=[]
        for _ in range(nc): s.names.append(r.fstr()); r.u('I')
        r.o=io; s.imports=[]
        for _ in range(ic):
            cp=s.fn(r); cn=s.fn(r); outer=r.u('i'); on=s.fn(r); r.u('i')  # bImportOptional
            s.imports.append((cn,on,outer))
        r.o=eo; s.exports=[]
        for _ in range(ec):
            ci,si,ti,oi=r.u('4i'); on=s.fn(r); of=r.u('I'); sz,so=r.u('qq'); r.u('3i'); r.u('i'); r.u('I'); r.u('3i'); r.u('5i')
            s.exports.append(dict(cls=ci,sup=si,name=on,flags=of,size=sz,off=so,outer=oi))
    def fn(s,r):
        i,n=r.u('ii'); return s.names[i]+(f'_{n-1}' if n else '')
    def ref(s,i):
        if i<0: return s.imports[-i-1][1]
        if i>0:
            if getattr(s,'fullrefs',False):
                e=s.exports[i-1]; n=e['name']; o=e['outer']
                while o>0: n=s.exports[o-1]['name']+'/'+n; o=s.exports[o-1]['outer']
                return n
            return s.exports[i-1]['name']
        return None
    def local_structs(s):
        if not hasattr(s,'_ls'):
            s._ls={}
            for e in s.exports:
                if s.ref(e['cls'])=='ScriptStruct':
                    sup=s.ref(e['sup']) if e['sup'] else None
                    s._ls[e['name']]=(sup if sup in s.um.structs else None, bp_props(s,e))
        return s._ls
    def data(s,e): return s.ex[e['off']-s.hdrsize:e['off']-s.hdrsize+e['size']]
    # ---- unversioned
    def props(s,r,struct_name,extra=None):
        schema=s.um.flat(struct_name) if struct_name else []; frags=[]
        if extra:
            pre=[]
            for n,pt,ad in extra: pre+=[(n,pt)]*ad
            schema=pre+schema
        while True:
            v=r.u('H'); frags.append((v&0x7f,(v>>7)&1,v>>9)); 
            if (v>>8)&1: break
        zn=sum(f[2] for f in frags if f[1]); mask=[]
        if zn:
            if zn<=8: m=r.u('B'); nb=8
            elif zn<=16: m=r.u('H'); nb=16
            else:
                words=(zn+31)//32; m=0
                for k in range(words): m|=r.u('I')<<(32*k)
            mask=[(m>>k)&1 for k in range(zn)]
        out={}; idx=0; zi=0
        for skip,hz,num in frags:
            idx+=skip
            for k in range(num):
                zero=False
                if hz: zero=mask[zi]; zi+=1
                if idx>=len(schema) or schema[idx] is None:
                    out['__stop__']=f'index {idx} beyond native schema {struct_name}({len(schema)})'; return out
                pn,pt=schema[idx]
                if s.dbg and isinstance(s.dbg,bool): print('  @',r.o,idx,pn,pt,'ZERO' if zero else '')
                out[pn]= 0 if zero else s.val(r,pt)
                if s.dbg and not zero: print('     =',str(out[pn])[:120])
                idx+=1
        return out
    def val(s,r,pt,elem=False):
        t=pt[0]
        if elem and t=='Enum': return s.fn(r).split('::')[-1]
        if t=='Bool': return bool(r.u('B'))
        if t in('Byte','Int8'): return r.u('B')
        if t=='Enum':
            inner=pt[1][0]; v=r.u({'Byte':'B','Int':'i','UInt32':'I','Int64':'q','UInt16':'H','Int16':'h','Int8':'b'}.get(inner,'B'))
            names=s.um.enums.get(pt[2]); return names[v] if names and v<len(names) else v
        if t=='Int': return r.u('i')
        if t=='UInt32': return r.u('I')
        if t=='Int64': return r.u('q')
        if t=='UInt64': return r.u('Q')
        if t=='Int16': return r.u('h')
        if t=='UInt16': return r.u('H')
        if t=='Float': return round(r.u('f'),5)
        if t=='Double': return round(r.u('d'),5)
        if t in('Object','Interface','WeakObject'): return s.ref(r.u('i'))
        if t=='LazyObject': r.o+=16; return 'lazy'
        if t=='Name': return s.fn(r)
        if t in('Str','Utf8Str','AnsiStr'): return r.fstr()
        if t in('SoftObject','AssetObject'): a=s.fn(r); b=s.fn(r); c=r.fstr(); return f'{a}.{b}{":"+c if c else ""}'
        if t=='Text':
            r.u('I'); ht=r.u('b')
            if ht==-1: return r.fstr() if r.u('i') else ''
            if ht==0: r.fstr(); r.fstr(); return r.fstr()
            if ht==11: return s.fn(r)+':'+r.fstr()
            raise ValueError('text history %d'%ht)
        if t=='Delegate': return (s.ref(r.u('i')),s.fn(r))
        if t=='MulticastDelegate': return [ (s.ref(r.u('i')),s.fn(r)) for _ in range(r.u('i'))]
        if t=='FieldPath': return [s.fn(r) for _ in range(r.u('i'))]+[r.u('i')]
        if t=='Array':
            n=r.u('i')
            if pt[1]==('Byte',): v=r.b[r.o:r.o+n].hex(); r.o+=n; return v
            return [s.val(r,pt[1],True) for _ in range(n)]
        if t=='Set':
            rm=r.u('i'); [s.val(r,pt[1],True) for _ in range(rm)]; n=r.u('i'); return [s.val(r,pt[1],True) for _ in range(n)]
        if t=='Map':
            rm=r.u('i'); [s.val(r,pt[1],True) for _ in range(rm)]; n=r.u('i'); return {str(s.val(r,pt[1],True)):s.val(r,pt[2],True) for _ in range(n)}
        if t=='Optional': return s.val(r,pt[1]) if r.u('i') else None
        if t=='Struct':
            sn=pt[1]
            if sn in('Vector','Rotator'): return tuple(round(x,4) for x in r.u('ddd'))
            if sn=='Vector2D': return tuple(round(x,4) for x in r.u('dd'))
            if sn=='LinearColor': return r.u('ffff')
            if sn=='RichCurveKey':
                im,tm,twm=r.u('BBB'); tv=r.u('6f'); return dict(interp=im,t=round(tv[0],4),v=round(tv[1],4),arrive=round(tv[2],4),leave=round(tv[4],4))
            if sn=='PerPlatformFrameRate':   # native: u32 bCooked, FFrameRate Default (num, den); cooked = no per-platform map
                ck=r.u('i'); num,den=r.u('ii')
                assert ck,'uncooked PerPlatformFrameRate'
                return {'Numerator':num,'Denominator':den}
            if sn in NATIVE_SIZES: d=s.b_hex(r,NATIVE_SIZES[sn]); return d
            if sn in('GameplayTagContainer',): return [s.fn(r) for _ in range(r.u('i'))]
            if sn=='GameplayTag': return s.fn(r)
            if sn in('SoftObjectPath','SoftClassPath'): a=s.fn(r); b=s.fn(r); c=r.fstr(); return f'{a}.{b}'
            if sn in('NiagaraVariableWithOffset','NiagaraVariableBase','NiagaraVariable'):
                nm=s.fn(r); td=s.props(r,'NiagaraTypeDefinition'); out={'Name':nm,'Type':td.get('ClassStructOrEnum'),'UnderlyingType':td.get('UnderlyingType',0)}
                if sn=='NiagaraVariableWithOffset': out['Offset']=r.u('i')
                if sn=='NiagaraVariable': n=r.u('i'); out['VarData']=r.b[r.o:r.o+n].hex(); r.o+=n
                return out
            if sn=='NiagaraTypeDefinitionHandle': return s.props(r,'NiagaraTypeDefinition')
            loc=s.local_structs().get(sn)
            if loc: return s.props(r,loc[0],loc[1])
            if sn not in s.um.structs: raise ValueError('unknown struct '+sn)
            return s.props(r,sn)
        raise ValueError('type '+t)
    def b_hex(s,r,n): v=r.b[r.o:r.o+n].hex(); r.o+=n; return v

def read_ffield(p,r):
    t=p.fn(r)
    if t=='None': return None
    name=p.fn(r); r.u('I')
    ad,es=r.u('ii'); r.u('Q'); r.u('H'); p.fn(r); r.u('B')
    T=t.replace('Property','')
    if T in('Object','WeakObject','LazyObject','Interface','SoftObject'): r.u('i'); pt=(T,)
    elif T=='Class': r.u('ii'); pt=('Object',)
    elif T=='SoftClass': r.u('ii'); pt=('SoftObject',)
    elif T=='Struct': sn=p.ref(r.u('i')); pt=('Struct',sn)
    elif T in('Array','Set','Optional'): inner=read_ffield(p,r); pt=(T,inner[1])
    elif T=='Map': k=read_ffield(p,r); v=read_ffield(p,r); pt=('Map',k[1],v[1])
    elif T=='Enum': en=p.ref(r.u('i')); und=read_ffield(p,r); pt=('Enum',und[1],en)
    elif T=='Byte':
        en=p.ref(r.u('i')); pt=('Enum',('Byte',),en) if en else ('Byte',)
    elif T=='Bool': r.u('6B'); pt=('Bool',)
    elif T in('Delegate','MulticastDelegate','MulticastInlineDelegate','MulticastSparseDelegate'): r.u('i'); pt=('MulticastDelegate',) if 'Multicast' in T else ('Delegate',)
    elif T=='FieldPath': r.u('i'); pt=('FieldPath',)
    else: pt=(T,)
    return (name,pt,ad)

def bp_props(p,clsexp):
    r=R(p.data(clsexp)); p.props(r,p.ref(clsexp['cls'])); r.u('i')
    r.u('i'); n=r.u('i'); r.o+=4*n
    out=[]
    for _ in range(r.u('i')): out.append(read_ffield(p,r))
    return out
