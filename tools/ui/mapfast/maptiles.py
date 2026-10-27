#!/usr/bin/env python3
"""ApseMap.gfx frame 1: keep the map's tile images between opens.

Vanilla: fc_clear (every close) removes every global-map and town-map tile clip, and fc_open loads them all again;
the movie shows its own cover with a "%" counter until every tile is streamed back (cry_onStreamed). That is the
"map frame first, map a moment later" on each open. The patch keeps the tiles when they had all arrived (state
LOADED) and skips the reload while the game sends the same map data (SetGlobalMap / SetLocalMaps keyed on their
data); different data (another region or town set) clears and reloads as before.
Usage: maptiles.py <decompiled frame_1/DoAction.as> <out DoAction.as>
"""
import sys

t = open(sys.argv[1], newline="").read()
nl = "\r\n" if "\r\n" in t else "\n"
t = t.replace("\r\n", "\n")
SEP = "String.fromCharCode(1)"


def sub(a, b):
    global t
    assert t.count(a) == 1, f"anchor not found once: {a[:60]!r}"
    t = t.replace(a, b)


clear_locals = ("   LocalMap.ClearCurrent();\n   g_Loading.ResetLocalMaps(g_LocalMapsData);\n"
                "   g_LocalMapsData.Call(function(localMap)\n   {\n      localMap.Clear();\n   }\n   );\n"
                "   g_LocalMapsData.Clear();\n")
helper = ("function __mtClearLocals()\n{\n" + clear_locals + "   _root.__mtLm = undefined;\n}\n")
# fc_clear: keep fully loaded tiles; clear them otherwise (vanilla). Its town-map block first (the helper repeats it).
sub(clear_locals, "   if(!_mtKeep)\n   {\n      __mtClearLocals();\n   }\n")
sub("function fc_clear()\n{\n   g_GlobalMap.Unload();\n",
    helper + "function fc_clear()\n{\n"
    "   var _mtKeep = g_GlobalMap.State == MEnum.LOAD_STATE_LOADED && (!LocalMap.LoadedLocalMap || "
    "LocalMap.LoadedLocalMap.LoadingState == MEnum.LOAD_STATE_LOADED);\n"
    "   if(!_mtKeep)\n   {\n      g_GlobalMap.Unload();\n      _root.__mtGm = undefined;\n   }\n")
# fc_setGlobalMap: same data -> keep the tiles; new data -> drop them first
sub("function fc_setGlobalMap()\n{\n   g_GlobalMap.SetData(g_GlobalMapA);\n",
    "function fc_setGlobalMap()\n{\n   var _mtK = g_GlobalMapA.join(" + SEP + ");\n"
    "   if(_mtK != _root.__mtGm)\n   {\n      if(g_GlobalMap.State != MEnum.LOAD_STATE_UNLOADED)\n      {\n"
    "         g_GlobalMap.Unload();\n      }\n      _root.__mtGm = _mtK;\n      g_GlobalMap.SetData(g_GlobalMapA);\n   }\n")
# fc_open: no reload while the kept tiles are all there
sub("   g_GlobalMap.Load();\n   ActivateMouse();\n",
    "   if(g_GlobalMap.State != MEnum.LOAD_STATE_LOADED)\n   {\n      g_GlobalMap.Load();\n   }\n   ActivateMouse();\n")
# fc_setLocalMaps: same town set -> keep the objects (and their loaded tiles)
a = 'function fc_setLocalMaps()\n{\n   DebugTrace.log("g_LocalMapsA: " + g_LocalMapsA.join(","),DebugTrace.C_DEBUG,"fc_setLocalMaps");\n'
sub(a, a + "   var _mtK = g_LocalMapsA.join(" + SEP + ");\n"
    "   if(_mtK == _root.__mtLm && g_LocalMapsData.Length > 0)\n   {\n      g_LocalMapsA.length = 0;\n      return undefined;\n   }\n"
    "   if(g_LocalMapsData.Length > 0)\n   {\n      __mtClearLocals();\n   }\n   _root.__mtLm = _mtK;\n")
open(sys.argv[2], "w", newline="").write(t.replace("\n", nl))
print("maptiles: patched fc_clear, fc_setGlobalMap, fc_open, fc_setLocalMaps")
