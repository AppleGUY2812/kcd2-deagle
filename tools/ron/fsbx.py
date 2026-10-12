# fsbx.py <bank> <outdir> <regex> [--wav]: decode matching FSB5 samples to .ogg (vorbis) / raw, read-only on the bank.
# --wav: convert each to 16-bit PCM .wav with ffmpeg and drop the .ogg (how scratchpad ron_python/ was made for the
# sound tests: Weapons.bank, regex in docs/ron_extraction.md). Needs the pip package fsb5 (+ libvorbis) and ffmpeg.
import sys,os,re,subprocess,fsb5
bank,out,pat=sys.argv[1:4]; wav='--wav' in sys.argv[4:]; rx=re.compile(pat); os.makedirs(out,exist_ok=True)
data=open(bank,'rb').read(); i=data.find(b'FSB5'); n=0; seen=set()
while i>=0:
    f=fsb5.FSB5(data[i:])
    for s in f.samples:
        if rx.search(s.name) and s.name not in seen:
            seen.add(s.name); ext=f.get_sample_extension(); p=os.path.join(out,s.name+'.'+ext)
            open(p,'wb').write(f.rebuild_sample(s)); n+=1
            if wav:
                subprocess.run(['ffmpeg','-v','quiet','-y','-i',p,os.path.join(out,s.name+'.wav')],check=True); os.remove(p)
    i=data.find(b'FSB5',i+4)
print(os.path.basename(bank),n)
