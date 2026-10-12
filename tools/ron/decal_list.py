# decal_list.py: extract the bullet-hole decal materials and write WORK/decal_textures.txt (the /Game/ paths of every
# Texture2D they import), the input of `tex.py decal_textures.txt fxout/decals`. Read-only on the paks.
import sys,os,glob,subprocess
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from usmap import U; from uasset import Pkg; from dump import OUT,WORK,PAKS,OOZ,TOOLS,USMAP
rx=(r'(Assets/Decals/Impact/M_BulletImpact_[^/]*|Assets/Weapon_Decals/Decal_Impact_Inst_[^/]*|VFXImpacts/Decal_Impact_Inst_[^/]*'
    r'|DAMAGE/BLOOD/Blood_Carpet_02_Inst|Bullet_Holes/Body/Decals_Body_M_Inst_7|Weapon_Decals/BP_ImpactMesh_(Glass|Windshield))'
    r'\.(uasset|uexp|ubulk)$')
subprocess.run([sys.executable,'-I',TOOLS+'/ronx.py',PAKS,OUT,OOZ,rx],check=True)
u=U(USMAP)
pats=['Assets/Decals/Impact/M_BulletImpact_*','Assets/Weapon_Decals/Decal_Impact_Inst_*','VFXImpacts/Decal_Impact_Inst_*',
      'Assets/Decals/DAMAGE/BLOOD/Blood_Carpet_02_Inst']
fs=sorted(set(f[:-7] for pat in pats for f in glob.glob(OUT+'/ReadyOrNot/Content/ReadyOrNot/'+pat+'.uasset')))
T=set()
for b in fs:
    p=Pkg(b,u)
    for c,n,o in p.imports:
        if c=='Texture2D' and o<0: T.add(p.imports[-o-1][1])
open(os.path.join(WORK,'decal_textures.txt'),'w').write('\n'.join(sorted(T)))
print(len(fs),'materials,',len(T),'textures ->',os.path.join(WORK,'decal_textures.txt'))
