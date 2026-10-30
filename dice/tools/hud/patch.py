"""Patch the decompiled KCD2 HUD with Dice Advisor's panel and the glow selector markers.
usage: patch.py <in frame_1/DoAction.as> <out frame_1/DoAction.as>
"""
import pathlib
import sys

src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = src.read_text(encoding="utf-8")
old = 'function fc_hideDiceScore()\n{\n   _root.bl.diceScoreContainer.score.gotoAndPlay("hide");\n}'
new = 'function fc_hideDiceScore()\n{\n   _root.bl.diceScoreContainer.score.gotoAndPlay("hide");\n   fc_diceAdvice(0,"","","","");\n}'
if text.count(old) != 1:
    sys.exit("patch.py: HUD hideDiceScore anchor did not match exactly once")
text = text.replace(old, new)

text += "\n" + (pathlib.Path(__file__).resolve().parent / "advice.as").read_text(encoding="utf-8")
text += "\nDA_SetupSelectors();\n"
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text, encoding="utf-8")
print(f"patched Dice Advisor HUD -> {dst}")
