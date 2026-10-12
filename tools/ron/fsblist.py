import sys,os,fsb5,json
D=sys.argv[1]; out={}
for b in sorted(os.listdir(D)):
    if not b.endswith('.bank') or b in('Music.bank',): continue
    data=open(os.path.join(D,b),'rb').read(); i=data.find(b'FSB5'); names=[]
    while i>=0:
        try:
            f=fsb5.FSB5(data[i:]); names+=[(s.name,f.header.mode.name,s.frequency,s.channels,s.samples) for s in f.samples]
        except Exception as e: names.append(('ERR '+repr(e)[:80],'',0,0,0))
        i=data.find(b'FSB5',i+4)
    out[b]=names
json.dump(out,open(sys.argv[2],'w'),indent=0)
for b,n in out.items(): print(b,len(n))
