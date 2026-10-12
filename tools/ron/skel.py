import json,math,os,sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from ronpaths import WORK
S=json.load(open(os.path.join(WORK,'ron_skel.json')))   # made by skel_dump.py
def qmul(a,b):
    ax,ay,az,aw=a; bx,by,bz,bw=b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
def qconj(q): return (-q[0],-q[1],-q[2],q[3])
def qrot(q,v):
    p=(v[0],v[1],v[2],0); r=qmul(qmul(q,p),qconj(q)); return r[:3]
def comp(i):
    b=S['bones'][i]; q,t,s=S['pose'][i]
    if b[1]<0: return q,t
    pq,pt=comp(b[1]); return qmul(pq,q), tuple(a+c for a,c in zip(pt,qrot(pq,t)))
def idx(name): return [b[0] for b in S['bones']].index(name)
