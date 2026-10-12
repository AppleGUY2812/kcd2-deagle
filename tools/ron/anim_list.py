# anim_list.py <AnimData package> [out.txt]: the first-person AnimSequences a gun plays, for RonExport's list.
# AnimData package e.g. ReadyOrNot/Content/Blueprints/Animation/Revised/AnimData/Mainline/BP_G19G5_M_AnimData
# (extracted on demand). Its AnimMontage imports -> the fp_ montages (extracted) -> their AnimSequence imports.
# Prints each montage's sequences, length and notifies (ReloadComplete, DropSpeedReloadMagazine, ShowMag ...), writes the
# sorted /Game/... sequence paths. 9f did this inline for the G19 (2026-10-12 15:37 UTC); the final G19 list
# (E:\Tools\deagle-work\ron\exporter\g19_list.txt) = SK_Pistol_G19 + Skinning_Arms_03 + these + idle / sprint / fire
# extras picked by name.
import sys,os,subprocess
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from usmap import U; from uasset import Pkg; from dump import OUT,WORK,PAKS,OOZ,TOOLS,USMAP,ensure,dump
u=U(USMAP); a=OUT+'/'+sys.argv[1]
assert ensure(a),'cannot extract '+a
p=Pkg(a,u)
mons=sorted(set(p.imports[-o-1][1] for c,n,o in p.imports if c=='AnimMontage' and o<0))
fp=[m.replace('/Game/','ReadyOrNot/Content/',1) for m in mons if m.split('/')[-1].startswith('fp_')]
rx='^('+'|'.join(x.replace('.','\\.') for x in fp)+r')\.(uasset|uexp)$'
subprocess.run([sys.executable,'-I',TOOLS+'/ronx.py',PAKS,OUT,OOZ,rx],check=True)
seqs=set()
for l in fp:
    q=Pkg(OUT+'/'+l,u); s=[q.imports[-o-1][1] for c,n,o in q.imports if c=='AnimSequence' and o<0]; seqs|=set(s)
    m=list(dump(OUT+'/'+l,u,lambda n,c: c=='AnimMontage').values())[0]['props']
    nt=[(x.get('NotifyName') or x.get('Notify'),round(x.get('LinkValue',0) or 0,3)) for x in m.get('Notifies',[]) or []]
    print(l.split('/')[-1],[x.split('/')[-1] for x in s],'len',m.get('SequenceLength'),nt[:12])
out=sys.argv[2] if len(sys.argv)>2 else os.path.join(WORK,'anim_seqs.txt')
open(out,'w').write('\n'.join(sorted(seqs)))
print(len(mons),'montages,',len(fp),'first-person,',len(seqs),'sequences ->',out)
