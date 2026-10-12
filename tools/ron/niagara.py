import sys,os,json,struct
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import OUT,USMAP,class_schema,gamepath; from uasset import Pkg,R
u=U(USMAP)
FMT={'NiagaraFloat':'f','NiagaraInt32':'i','NiagaraBool':'i','Vector2f':'2f','Vector3f':'3f','Vector4f':'4f','LinearColor':'4f','Quat4f':'4f','NiagaraPosition':'3f','Vector2D':'2f','Vector':'3f','Vector4':'4f','NiagaraHalf':'e'}
def fullname(p,i):
    e=p.exports[i]; n=e['name']; o=e['outer']
    while o>0: n=p.exports[o-1]['name']+'/'+n; o=p.exports[o-1]['outer']
    return n
def decode_store(store,p):
    if not isinstance(store,dict): return {}
    data=bytes.fromhex(store.get('ParameterData','') or ''); dis=store.get('DataInterfaces',[]) or []; out={}
    for v in store.get('SortedParameterOffsets',[]) or []:
        t=str(v.get('Type')); off=v.get('Offset',0); nm=v['Name']
        if t in FMT:
            f=FMT[t]
            try: val=struct.unpack_from('<'+f,data,off); val=[round(x,5) if isinstance(x,float) else x for x in val]; out[nm]=val[0] if len(val)==1 else val
            except Exception: out[nm]='?'
            if t=='NiagaraBool' and out[nm]!='?': out[nm]=bool(out[nm])
        elif t.startswith('NiagaraDataInterface'): out[nm]={'DI':dis[off] if 0<=off<len(dis) else off}
        else:
            try: out[nm]={'enum_or_other':t,'int':struct.unpack_from('<i',data,off)[0]}
            except Exception: out[nm]={'type':t}
    return out
def read_system(base):
    p=Pkg(base,u); res={'emitters':{},'scripts':{},'data_interfaces':{},'renderers':{},'textures':[],'materials':[],'meshes':[]}
    for c,n,o in p.imports:
        if c=='Texture2D': res['textures'].append(p.imports[-o-1][1] if o<0 else n)
        if c in('MaterialInstanceConstant','Material'): res['materials'].append(p.imports[-o-1][1] if o<0 else n)
        if c=='StaticMesh': res['meshes'].append(p.imports[-o-1][1] if o<0 else n)
    for i,e in enumerate(p.exports):
        cls=p.ref(e['cls']); fn=fullname(p,i)
        try:
            nat,ex=class_schema(p,e['cls'],u); r=R(p.data(e)); d=p.props(r,nat,ex)
        except Exception as x: d={'__error__':repr(x)}
        if cls=='NiagaraScript':
            res['scripts'][fn]={'Usage':d.get('Usage'),'constants':decode_store(d.get('RapidIterationParameters'),p)} if '__error__' not in d else d
        elif cls=='NiagaraEmitter': res['emitters'][fn]=d
        elif 'RendererProperties' in str(cls): res['renderers'][fn]={'cls':cls,**d}
        elif 'DataInterface' in str(cls): res['data_interfaces'][fn]={'cls':cls,**d}
        elif cls=='NiagaraSystem': res['system']={k:v for k,v in d.items() if k not in('EmitterHandles',)}; res['emitter_handles']=d.get('EmitterHandles')
    return res
if __name__=='__main__':
    r=read_system(sys.argv[1]); print(json.dumps(r,default=str,indent=1)[:int(sys.argv[2]) if len(sys.argv)>2 else 5000])
