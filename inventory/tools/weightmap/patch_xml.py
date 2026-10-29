"""Adds the weight map's entry points to a game UI definition (Libs/UI/UIElements/*.xml) so Lua can call the
patched movies (UIAction.CallFunction) and hear their events (UIAction.RegisterElementListener).
usage: patch_xml.py inv|tr|md <in.xml> <out.xml>   (each insert must hit its anchor exactly once)"""
import pathlib, sys

kind, src, dst = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
t = src.read_text(encoding="utf-8")
MODS = ('<function name="InvMods" funcname="fc_invMods"><param name="Shift" desc="" type="int"/>'
        '<param name="Ctrl" desc="" type="int"/></function>')
STAT = '<event name="OnInvStat" fscommand="onInvStat"><param name="Text" desc="" type="string"/></event>'
SEARCH = '<event name="OnInvSearchClick" fscommand="onInvSearchClick" />'
funcs = {
    "inv": ['<function name="InvFast" funcname="fc_invFast" />',
            '<function name="InvShopClick" funcname="fc_invShopClick" />', MODS,
            '<function name="InvSearch" funcname="fc_invSearch"><param name="Text" desc="" type="string"/>'
            '<param name="Open" desc="" type="int"/></function>'],
    "tr": ['<function name="InvFast" funcname="fc_invFast" />', MODS,
           '<function name="InvSearch" funcname="fc_invSearch"><param name="Text" desc="" type="string"/>'
           '<param name="Open" desc="" type="int"/></function>'],
    "md": ['<function name="InvAutoMax" funcname="fc_invAutoMax" />'],
}[kind]
events = {
    "inv": ['<event name="OnInvActive" fscommand="onInvActive"><param name="Active" desc="" type="bool"/></event>',
            '<event name="OnInvFastMove" fscommand="onInvFastMove" />',
            '<event name="OnInvFastShop" fscommand="onInvFastShop" />', STAT, SEARCH],
    "tr": ['<event name="OnInvActive" fscommand="onInvActive"><param name="Active" desc="" type="bool"/></event>',
           STAT, SEARCH],
    "md": [],
}[kind]
for tag, lines in (("</functions>", funcs), ("</events>", events)):
    if not lines:
        continue
    n = t.count(tag)
    if n != 1:
        sys.exit(f"patch_xml.py: {src.name}: expected one {tag}, got {n}")
    t = t.replace(tag, "".join("      " + l + "\n" for l in lines) + "    " + tag)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(t, encoding="utf-8")
print(f"patched {kind} xml -> {dst}")
