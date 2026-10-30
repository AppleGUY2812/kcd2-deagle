"""Copy KCD2's HUD.xml, adding only Dice Advisor's function entry."""
import pathlib
import sys

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = src.read_text(encoding="utf-8")
anchor = '      <function name="HideDiceScore" funcname="fc_hideDiceScore"/>'
entry = anchor + '''
      <function name="DiceAdvice" funcname="fc_diceAdvice">
        <param name="Mode" desc="0 hide, 1 calculating, 2 advice, 3 dice picker" type="int" />
        <param name="Faces" desc="dice to set aside as 1,6,6,J" type="string" />
        <param name="Action" desc="then BANK / then THROW AGAIN / Now ..." type="string" />
        <param name="Line2" desc="advice: throwing-on line; picker: rows separated by |" type="string" />
        <param name="Line3" desc="why line, detail and mistake lines (\\n separated)" type="string" />
      </function>'''
if text.count(anchor) != 1:
    sys.exit("patch_xml.py: HideDiceScore anchor did not match exactly once")
text = text.replace(anchor, entry)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text, encoding="utf-8")
print(f"patched Dice Advisor HUD.xml -> {dst}")
