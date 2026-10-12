"""Where the Ready or Not tools read and write (docs/ron_extraction.md).

Every path can be overridden with an environment variable. Defaults: the Steam install on C:, heavy files in
E:\\Tools\\deagle-work\\ron\\work (mounted in WSL as /mnt/e). Nothing here ever writes into the game folder.
"""
import os

GAME = os.environ.get("RON_GAME", "/mnt/c/Program Files (x86)/Steam/steamapps/common/Ready Or Not")
PAKS = os.environ.get("RON_PAKS", GAME + "/ReadyOrNot/Content/Paks")                # read-only
BANKS = os.environ.get("RON_BANKS", GAME + "/ReadyOrNot/Content/FMOD/Desktop")      # read-only (FMOD .bank files)
WORK = os.environ.get("RON_WORK", "/mnt/e/Tools/deagle-work/ron/work")             # intermediate JSON, fxout/, tif_impacts/
OUT = os.environ.get("RON_OUT", os.path.join(WORK, "ronout"))                      # extracted .uasset/.uexp/.ubulk, pak layout
USMAP = os.environ.get("RON_USMAP", "/mnt/e/Tools/deagle-work/ron/exporter/Mappings.usmap")
OOZ = os.environ.get("RON_OOZ", os.path.expanduser("~/.cache/kcd2-deagle/libooz.so"))  # tools/ron/build_ooz.sh
TOOLS = os.path.dirname(os.path.abspath(__file__))
