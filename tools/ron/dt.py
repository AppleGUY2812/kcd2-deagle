import sys,json,os
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from uasset import Pkg,R
def read_dt(base,u):
    p=Pkg(base,u); e=p.exports[0]
    for ex in p.exports:
        if p.ref(ex['cls']) in('DataTable','CompositeDataTable'): e=ex
    r=R(p.data(e)); props=p.props(r,p.ref(e['cls'])); r.u('i')
    rs=props.get('RowStruct'); rows={}
    for _ in range(r.u('i')):
        n=p.fn(r); rows[n]=p.props(r,rs)
    return dict(rowstruct=rs,consumed=f'{r.o}/{e["size"]}',rows=rows)
if __name__=='__main__':
    u=U(sys.argv[1]); d=read_dt(sys.argv[2],u); print(d['rowstruct'],d['consumed'],len(d['rows']))
    print(json.dumps(d,default=str,indent=1)[:int(sys.argv[3]) if len(sys.argv)>3 else 6000])
