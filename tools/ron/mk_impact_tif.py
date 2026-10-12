# Build the impact TIFs (RGBA) from the RoN PNG exports. Private use only.
import sys,os,json
from PIL import Image,ImageChops
import numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from ronpaths import WORK
S=WORK; P=S+'/fxout/particles/'; D=S+'/fxout/decals/'; O=S+'/tif_impacts/'; os.makedirs(O,exist_ok=True)
def save(im,name): im.save(O+name+'.tif',compression='raw'); return name
def keep(src): return Image.open(src).convert('RGBA')
def additive(src):  # premultiply onto black, opaque alpha: safe for additive blending
    a=np.array(keep(src)).astype(float); a[...,:3]*=a[...,3:4]/255; a[...,3]=255; return Image.fromarray(a.round().astype('uint8'),'RGBA')
def lum_alpha(src,rgb=(255,255,255)):  # white mask in RGB -> alpha, solid colour
    g=np.array(Image.open(src).convert('L')); a=np.zeros(g.shape+(4,),'uint8'); a[...,:3]=rgb; a[...,3]=g; return Image.fromarray(a,'RGBA')
def mask_tint(src,rgb):  # keep alpha, recolour (white silhouettes)
    a=np.array(keep(src)).astype(float); a[...,:3]=a[...,:3]/255*np.array(rgb); return Image.fromarray(a.round().astype('uint8'),'RGBA')
made={}
made['imp_dust_diff']=('T_thinSmoke_02_8x4','8x4 anim, alpha',save(keep(P+'T_thinSmoke_02_8x4.png'),'imp_dust_diff'))
made['imp_dustjet_diff']=('T_dustImpact_16x4','16x2 anim, alpha',save(keep(P+'T_dustImpact_16x4.png'),'imp_dustjet_diff'))
made['imp_squib_diff']=('Squib_one_1024_8x8','8x8 anim, alpha',save(keep(P+'Squib_one_1024_8x8.png'),'imp_squib_diff'))
made['imp_wisp_diff']=('whisp_One_512_8x8','8x8 anim, alpha',save(keep(P+'whisp_One_512_8x8.png'),'imp_wisp_diff'))
made['imp_spray_diff']=('T_debrisSpray_4x1','4x1 variants, alpha, velocity aligned',save(keep(P+'T_debrisSpray_4x1.png'),'imp_spray_diff'))
made['imp_sparks_diff']=('T_longSpark_02_8x2','4x2 variants, additive (premultiplied)',save(additive(P+'T_longSpark_02_8x2.png'),'imp_sparks_diff'))
made['imp_glow_diff']=('T_Glow_A','single, additive',save(additive(P+'T_Glow_A.png'),'imp_glow_diff'))
made['imp_stones_diff']=('Stones2x2','2x2 variants, alpha',save(keep(P+'Stones2x2.png'),'imp_stones_diff'))
made['imp_splinters_diff']=('Wood_Splinters2x2','2x2 variants, alpha',save(keep(P+'Wood_Splinters2x2.png').resize((512,512),Image.LANCZOS),'imp_splinters_diff'))
made['imp_glass_diff']=('T_Glass_Chunks_3x3_Mask','3x3 variants, alpha, tinted pale blue',save(mask_tint(P+'T_Glass_Chunks_3x3_Mask.png',(215,232,240)),'imp_glass_diff'))
made['imp_fabric_diff']=('Fabric','2x2 variants, alpha',save(keep(P+'Fabric.png').resize((512,512),Image.LANCZOS),'imp_fabric_diff'))
made['imp_straw_diff']=('PaperShreds2x2','2x2 variants, alpha, tinted straw',save(mask_tint(P+'PaperShreds2x2.png',(222,196,120)),'imp_straw_diff'))
made['imp_drop_diff']=('T_mud_drop','single teardrop, alpha from luminance',save(lum_alpha(P+'T_mud_drop.png'),'imp_drop_diff'))
json.dump(made,open(O+'_particles.json','w'),indent=1); print(len(made))
# ---- bullet-hole decals: diffuse+opacity and normal (UE green-down -> CryEngine green-up) + smoothness alpha
HOLES={'metal':('Metal_Hard_BO','Metal_Hard_N',0.55),'stone':('Concrete_Hard_BO','Concrete_Hard_N',0.2),'brick':('Brick_BO','Brick_N',0.15),
 'plaster':('Plaster_BO','Plaster_N',0.15),'wood':('WoodHard_BO','WoodHard_N',0.2),'wood_soft':('WoodSoft_BO','WoodSoft_N',0.15),
 'soil':('Dirt_BO','Dirt_N',0.1),'cloth':('Fabric_BO','Cardboard_N',0.1),'glass':('Glass_BO','Metal_Hard_N',0.8),'ceramic':('Tile_BO','Tile_N',0.6)}
FLIP_G=os.environ.get('FLIP_G','1')=='1'
holes={}
for s,(bo,n,sm) in HOLES.items():
    save(keep(D+bo+'.png'),f'hole_{s}_diff')
    a=np.array(Image.open(D+n+'.png').convert('RGB')).astype(float)
    if FLIP_G: a[...,1]=255-a[...,1]
    out=np.zeros(a.shape[:2]+(4,),'uint8'); out[...,:3]=a.round(); out[...,3]=int(sm*255)
    save(Image.fromarray(out,'RGBA'),f'hole_{s}_ddna')
    holes[s]={'diffuse':bo,'normal':n,'smoothness':sm,'green_flipped':FLIP_G}
json.dump(holes,open(O+'_holes.json','w'),indent=1); print('holes',len(holes))
