"""Copy KCD2's HUD.xml, adding only Dice Advisor's function entry."""
import pathlib
import sys

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = src.read_text(encoding="utf-8")
anchor = '      <function name="HideDiceScore" funcname="fc_hideDiceScore"/>'
entry = anchor + '''
      <function name="DiceAdvice" funcname="fc_diceAdvice">
        <param name="Mode" desc="0 hide, 1 calculating, 2 advice" type="int" />
        <param name="Keep" desc="" type="string" />
        <param name="Roll" desc="" type="string" />
        <param name="Bank" desc="" type="string" />
        <param name="RecommendBank" desc="" type="bool" />
      </function>'''
if text.count(anchor) != 1:
    sys.exit("patch_xml.py: HideDiceScore anchor did not match exactly once")
text = text.replace(anchor, entry)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text, encoding="utf-8")
print(f"patched Dice Advisor HUD.xml -> {dst}")
