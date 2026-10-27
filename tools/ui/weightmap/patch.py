"""Patch a decompiled KCD2 inventory movie script (frame_1/DoAction.as) with the weight map.

usage: patch.py inv|tr <in DoAction.as> <out DoAction.as>
Each edit must match exactly once, so a game update that changes the script fails loudly instead of half-patching.
"""
import sys, pathlib

here = pathlib.Path(__file__).resolve().parent
kind, src, dst = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
text = src.read_text(encoding="utf-8")
if kind == "md":   # the inventory's quantity dialog: only the fast-transfer auto-confirm
    text += "\n" + (here / "fast.as").read_text(encoding="utf-8") + "\n" + (here / "md.as").read_text(encoding="utf-8")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(text, encoding="utf-8")
    print(f"patched md -> {dst}")
    sys.exit(0)

def key_guard(d):
    return f"   if(WM_Key({d}))\n   {{\n      return undefined;\n   }}\n"

edits = [
    ("function fc_focusPrev()\n{\n", "function fc_focusPrev()\n{\n" + key_guard(-1)),
    ("function fc_focusNext()\n{\n", "function fc_focusNext()\n{\n" + key_guard(1)),
    ("function UpdateWeight()\n{\n", "function UpdateWeight()\n{\n   WM_Dirty();\n"),
]
if kind == "inv":
    edits += [
        ("var _loc1_ = g_View.GetIdOnMousePosition();", "var _loc1_ = WM_IdOnMouse(g_View);"),
        ("function fc_setShopMode(isTrader, playerId)\n{\n", "function fc_setShopMode(isTrader, playerId)\n{\n   WM_HideAll();\n"),
        # time the item-list fill on each open (2026-10-26, menu-open work): "[Deagle] fastmove: ... setItems ..."
        ("function fc_setItems(idx)\n{\n   DebugTrace.log(\"g_ItemsA: \" + g_ItemsA.join(\",\"),DebugTrace.C_DEBUG,\"fc_setItems\");\n   g_Data.SetDataFromArray(g_ItemsA);\n",
         "function fc_setItems(idx)\n{\n   var _st0 = getTimer();\n   var _stN = g_ItemsA.length;\n"
         "   DebugTrace.log(\"g_ItemsA: \" + g_ItemsA.join(\",\"),DebugTrace.C_DEBUG,\"fc_setItems\");\n   var _st1 = getTimer();\n"
         "   g_Data.SetDataFromArray(g_ItemsA);\n   WM_StatSet(_st0,_st1,getTimer(),_stN);\n"),
    ]
elif kind == "tr":
    edits += [
        ("var _loc1_ = GetView(g_Idx).GetIdOnMousePosition();", "var _loc1_ = WM_IdOnMouse(GetView(g_Idx));"),
        ("function fc_open(mode, selectedInv, name)\n{\n", "function fc_open(mode, selectedInv, name)\n{\n   WM_ResetAll();\n"),
    ]
else:
    sys.exit("kind must be inv or tr")

for old, new in edits:
    n = text.count(old)
    if n != 1:
        sys.exit(f"patch.py: expected 1 match, found {n}: {old!r}")
    text = text.replace(old, new)

text += "\n" + (here / "common.as").read_text(encoding="utf-8") + "\n" + (here / "total.as").read_text(encoding="utf-8") + "\n" + (here / "fast.as").read_text(encoding="utf-8") + "\n" + (here / f"{kind}.as").read_text(encoding="utf-8") + "\nWM_Setup();\n"
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(text, encoding="utf-8")
print(f"patched {kind}: {len(edits)} edits -> {dst}")
