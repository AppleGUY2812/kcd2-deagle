# Blood TIFs (RGBA) from the RoN PNG exports. Private use only.
import os,json,sys
import numpy as np
from PIL import Image,ImageFilter
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from ronpaths import WORK
S=WORK; B=S+'/fxout/blood/'; O=S+'/tif_impacts/'; os.makedirs(O,exist_ok=True)
def L(n,ch=None):
    a=np.array(Image.open(B+n+'.png').convert('RGBA')).astype(float)
    return a[...,:3].max(axis=2) if ch is None else a[...,ch]
def save(a,name): Image.fromarray(np.clip(a,0,255).round().astype('uint8'),'RGBA').save(O+name+'.tif',compression='raw'); return name
def white(mask):
    o=np.zeros(mask.shape+(4,)); o[...,:3]=255; o[...,3]=mask; return o
def colour(mask,rgb,var=None):
    o=np.zeros(mask.shape+(4,)); o[...,:3]=rgb; o[...,3]=mask
    if var is not None: o[...,:3]*=var[...,None]
    return o
def ddna_from_height(h,strength=4.0,smooth=0.85,src_normal=None):
    if src_normal is not None:
        n=np.array(Image.open(B+src_normal+'.png').convert('RGB')).astype(float); n[...,1]=255-n[...,1]   # UE green-down -> Cry green-up
        o=np.zeros(n.shape[:2]+(4,)); o[...,:3]=n; o[...,3]=smooth*255; return o
    hb=np.array(Image.fromarray(h.astype('uint8')).filter(ImageFilter.GaussianBlur(1.5))).astype(float)/255
    gy,gx=np.gradient(hb); nx=-gx*strength; ny=gy*strength; nz=np.ones_like(nx)   # rows go down; green-up
    l=np.sqrt(nx*nx+ny*ny+nz*nz); o=np.zeros(h.shape+(4,))
    o[...,0]=(nx/l+1)*127.5; o[...,1]=(ny/l+1)*127.5; o[...,2]=(nz/l+1)*127.5; o[...,3]=smooth*255; return o
made={}
# particles: white RGB, alpha = mask (tinted in the XML)
for name,src,ch,note in [('bld_splat_diff','T_Blood_SubUV_2x2_01',None,'2x2 splat variants'),('bld_clip_diff','T_Blood_Clip_A',None,'blob'),
                         ('bld_string_diff','T_Blood_String_02',None,'strand'),('bld_slash_diff','T_Blood_Slash_A',None,'slash streak 2:1'),
                         ('bld_spray_diff','T_Blood_Spray_SubUV_4x1',1,'4x1 spray streaks (G channel)'),('bld_drop_diff','T_MortDropM',None,'drop')]:
    made[name]=(src,note,save(white(L(src,ch)),name))
# decals: blood colour + opacity, normal/smoothness
RED=np.array([92,6,4.]); POOL=np.array([62,3,2.])
D={}
def decal(name,mask,rgb,src,normal=None,smooth=0.85,var=None,size=None):
    save(colour(mask,rgb,var),f'blooddecal_{name}_diff'); save(ddna_from_height(mask,src_normal=normal,smooth=smooth),f'blooddecal_{name}_ddna'); D[name]={'source':src,'size_m':size}
decal('spatter_1',L('Decal_Blood_Splat_1'),RED,'Decal_Blood_Splat_1 (+_N)','Decal_Blood_Splat_1_N',size=0.45)
decal('spatter_2',L('Decal_Blood_Splat_2'),RED,'Decal_Blood_Splat_2 (+_N)','Decal_Blood_Splat_2_N',size=0.5)
decal('spatter_3',L('Decal_BloodSpray_04_Mask'),RED,'Decal_BloodSpray_04_Mask',size=0.4)
decal('spatter_4',L('Decal_BloodSpray_Sideways_01_Mask'),RED,'Decal_BloodSpray_Sideways_01_Mask',size=0.45)
alb=np.array(Image.open(B+'se1sfhkp_2K_Albedo.png').convert('RGB')).astype(float); op=L('se1sfhkp_2K_Opacity')
o=np.zeros(op.shape+(4,)); o[...,:3]=alb; o[...,3]=op; save(o,'blooddecal_spatter_5_diff'); save(ddna_from_height(op,src_normal='se1sfhkp_2K_Normal',smooth=0.8),'blooddecal_spatter_5_ddna')
D['spatter_5']={'source':'Megascans se1sfhkp (RoN headshot splatter MI_Blood_Headshot_01)','size_m':1.0}
decal('drips_1',L('Decal_BloodDroplets_04_Inst'),RED,'Decal_BloodDroplets_04 (artery drops)',size=0.18)
decal('drips_2',L('Decal_BloodDroplets_05_Inst'),RED,'Decal_BloodDroplets_05 (artery drops)',size=0.18)
pr=L('T_BloodPool_Packed',0)/210.0; pm=np.clip((pr-0.30)/0.10,0,1)*255   # RoN pool: BloodExpansion 1, EdgeSize -0.045 -> fully grown, soft edge
decal('pool',pm,POOL,'T_BloodPool_Packed R channel, thresholded (RoN M_Decal_BloodPool_01)',smooth=0.95,size=1.28)
# Decal_Blood_Splat_2 is 797x797 (not a power of two): both spatter_2 maps go to 512x512. 9f did this inline right after
# the first run (2026-10-12); folded in here so a rebuild gives the same files as E:\Tools\deagle-work\fx_tif_impacts.
for n in ('blooddecal_spatter_2_diff','blooddecal_spatter_2_ddna'):
    Image.open(O+n+'.tif').resize((512,512),Image.LANCZOS).save(O+n+'.tif',compression='raw')
json.dump({'particles':made,'decals':D},open(O+'_blood.json','w'),indent=1); print(len(made),len(D))
