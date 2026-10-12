# build_curves.py <move_curves_raw.json> <out.json>: format movecurves.py output as docs/ron/ron_movement_curves.json
# (one row per 1/30 s: t, x, y, z in metres, pitch, roll, yaw in degrees, OUR camera axes). 9f ran this inline
# (2026-10-12 15:23 UTC); saved here unchanged apart from the arguments.
import json,sys
D=json.load(open(sys.argv[1]))
out={'_meta':{
 'source':"Ready or Not AnimSequences (UE5.3, AnimCompress_PerTrackCompression, additive AAT_LocalSpaceBase vs frame 0), decoded read-only from the user's install",
 'skeleton':'SKL_RoN_Player_Master_Shared: weapon_right (parent spine_3), fp_camera. Bone deltas are converted from parent space to component space with the skeleton ref pose, then to OUR camera axes',
 'axes':'off = metres in our plugin camera space (x right, y forward, z up); rot = degrees as our LocalRot (pitch about x, roll about y, yaw about z; R = Rz(yaw) Rx(pitch) Ry(roll)). UE component: right = -X, forward = +Y, up = +Z. Mapping verified only by the ref pose (camera axes = component axes); the roll/yaw SIGN still needs one in-game check',
 'caveat':'Additive deltas apply on top of the gun idle pose. Small differences remain because the idle pose of spine_3 differs from the T-pose used for the axis conversion. The fp_g19/m4a1_add_tac_up_* variants carry float curves that were not decoded (skipped)',
 'sample_rate':30},'anims':{}}
for k,v in D.items():
    if 'error' in v: out['anims'][k]={'error':v['error']}; continue
    out['anims'][k]={'path':v['path'],'length':v['length'],'frames':v['frames'],
        'weapon_right':[[s['t'],*s['off'],*s['rot']] for s in v['weapon_right']] if v['weapon_right'] else None,
        'fp_camera':[[s['t'],*s['off'],*s['rot']] for s in v['fp_camera']] if v['fp_camera'] else None,
        'columns':['t s','x m','y m','z m','pitch deg','roll deg','yaw deg']}
json.dump(out,open(sys.argv[2],'w'),separators=(',',':'))
print(len(out['anims']),'anims ->',sys.argv[2])
