"""Adds the weight map's entry points to a game UI definition (Libs/UI/UIElements/*.xml) so Lua can call the
patched movies (UIAction.CallFunction) and hear their events (UIAction.RegisterElementListener).
usage: patch_xml.py inv|tr|md <in.xml> <out.xml>   (each insert must hit its anchor exactly once)"""
import pathlib, sys

kind, src, dst = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
t = src.read_text(encoding="utf-8")
MODS = ('<function name="DeagleMods" funcname="fc_deagleMods"><param name="Shift" desc="" type="int"/>'
        '<param name="Ctrl" desc="" type="int"/></function>')
STAT = '<event name="OnDeagleStat" fscommand="onDeagleStat"><param name="Text" desc="" type="string"/></event>'
funcs = {
    "inv": ['<function name="DeagleFast" funcname="fc_deagleFast" />',
            '<function name="DeagleShopClick" funcname="fc_deagleShopClick" />', MODS],
    "tr": ['<function name="DeagleFast" funcname="fc_deagleFast" />', MODS],
    "md": ['<function name="DeagleAutoMax" funcname="fc_deagleAutoMax" />'],
}[kind]
events = {
    "inv": ['<event name="OnDeagleActive" fscommand="onDeagleActive"><param name="Active" desc="" type="bool"/></event>',
            '<event name="OnDeagleFastMove" fscommand="onDeagleFastMove" />',
            '<event name="OnDeagleFastShop" fscommand="onDeagleFastShop" />', STAT],
    "tr": ['<event name="OnDeagleActive" fscommand="onDeagleActive"><param name="Active" desc="" type="bool"/></event>',
           STAT],
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
