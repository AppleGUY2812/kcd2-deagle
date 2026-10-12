# fp_cdo.py: write WORK/fp_cdo.json, the decoded class defaults of the first-person anim blueprint
# ANIMBP_Player_FP_Master (anim graph nodes, LazySpringBone, blueprint variables), input of build_move.py.
# The decode stops at the end of the property block (19819 of 39292 bytes); the rest isn't property data.
import sys,os,json
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from usmap import U; from dump import dump,ensure,OUT,WORK,USMAP
b=OUT+'/ReadyOrNot/Content/Blueprints/Animation/Revised/Graphs_Body/ANIMBP_Player_FP_Master'
assert ensure(b),'cannot extract '+b
d=dump(b,U(USMAP),lambda n,c: n.startswith('Default__'))['Default__ANIMBP_Player_FP_Master_C']['props']
json.dump(d,open(os.path.join(WORK,'fp_cdo.json'),'w'),default=str,indent=1)
print(len(d),'properties ->',os.path.join(WORK,'fp_cdo.json'))
