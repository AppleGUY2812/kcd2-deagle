#!/usr/bin/env python3
"""ApseQuestLogDiary.gfx: skip rebuilding the diary text when the game sends the same quest again.

The game sends SetQuestDiary twice on every map open (8 ms each, 2026-10-26 menutime), almost always the same quest.
fc_setQuestDiary lays out the whole text through TextExtension.setContent each time. The patch keys the call on its
data (joined with \\x01) and only resets the scroll position when it's unchanged; fc_clear forgets the key.
Usage: qdiary.py <decompiled frame_1/DoAction.as> <out DoAction.as>
"""
import sys

t = open(sys.argv[1], newline="").read()
nl = "\r\n" if "\r\n" in t else "\n"
t = t.replace("\r\n", "\n")
a = 'function fc_setQuestDiary()\n{\n'
b = a + ('   var _qK = g_QuestDiaryA.join(String.fromCharCode(1));\n'
         '   if(_qK == _root.__qdLast)\n   {\n      g_Scrollbar.setPosition(1,false);\n'
         '      g_QuestDiaryA.length = 0;\n      return undefined;\n   }\n   _root.__qdLast = _qK;\n')
c = 'function fc_clear()\n{\n'
d = c + '   _root.__qdLast = undefined;\n'
for x in (a, c):
    assert t.count(x) == 1, f"anchor not found: {x!r}"
open(sys.argv[2], "w", newline="").write(t.replace(a, b).replace(c, d).replace("\n", nl))
print("qdiary: patched fc_setQuestDiary, fc_clear")
