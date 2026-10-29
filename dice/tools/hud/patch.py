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

# The plugin sends ids 100+ for its own dice: draw our glow instead of the game's ring. Ids below 100 go to the
# original function untouched, which keeps the vanilla selectors exactly as they were.
text += """
var DA_ORIGINAL_ADD_DICE_SELECTOR = fc_addDiceSelector;
var DA_ORIGINAL_REMOVE_DICE_SELECTOR = fc_removeDiceSelector;
function fc_addDiceSelector(selectorId, xPos, yPos, isPlayer)
{
   if(selectorId >= 100)
   {
      DA_Glow(selectorId - 100, xPos, yPos);
      return undefined;
   }
   return DA_ORIGINAL_ADD_DICE_SELECTOR(selectorId, xPos, yPos, isPlayer);
}
function fc_removeDiceSelector(selectorId)
{
   if(selectorId >= 100)
   {
      DA_GlowRemove(selectorId - 100);
      return undefined;
   }
   return DA_ORIGINAL_REMOVE_DICE_SELECTOR(selectorId);
}
"""
text += "\n" + (pathlib.Path(__file__).resolve().parent / "advice.as").read_text(encoding="utf-8")
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text, encoding="utf-8")
print(f"patched Dice Advisor HUD -> {dst}")
