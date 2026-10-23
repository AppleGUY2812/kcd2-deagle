#!/usr/bin/env python3
"""tools/loadtime.py reads the numbers off a KCD2 log; this feeds it two made-up logs."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import loadtime

ok = True


def check(name, got, want):
    global ok
    good = got == want
    ok = ok and good
    print(("PASS " if good else "FAIL ") + name + ("" if good else "  got %r want %r" % (got, want)))


def write(tmp, lines):
    with open(tmp, "w") as f:
        f.write("\n".join(lines) + "\n")
    return tmp


tmp = os.path.join(os.environ.get("TMPDIR", "/tmp"), "loadtime_test.log")

# a session with times: launch, a level, a save load, a second save load, a fast travel
write(tmp, [
    'BackupNameAttachment=" Build(0) 23 Oct 26 (14 10 30)"',
    "Log Started at 2026-10-23  14:10:30",
    "<14:10:31> startup",
    "<14:11:18> Loading saved game '%USER%/saves/playline0/autosave584.whs' AutoSave ...",
    "<14:11:20> ============================ Loading level trosecko ============================",
    "<14:12:22> [Deagle] gameplay started",
    "<14:20:00> Loading saved game '%USER%/saves/playline0/quick.whs' ...",
    "<14:20:04> [Deagle] gameplay started",
    "<14:30:00> CryGFxFileOpener::OpenFile(), 'Libs/UI//SkipTime.gfx', flags:33",
    "<14:30:31> [Deagle] gameplay started",
])
rows = loadtime.spans(tmp)
got = [(k, s) for k, s, *_ in rows]
want = [("boot", 112.0), ("load", 64.0), ("load", 4.0), ("travel", 31.0)]
check("boot, the load into a new level, a save load, a fast travel", got, want)

# without log_IncludeTime the markers are still found, but there are no times
write(tmp, [
    "Log Started at 2026-10-23  14:10:30",
    "Loading saved game '%USER%/saves/playline0/autosave584.whs' AutoSave ...",
    "============================ Loading level trosecko ============================",
    "[Deagle] gameplay started",
])
rows = loadtime.spans(tmp)
check("no timestamps: still a marker, no time", [(k, s) for k, s, *_ in rows], [("boot", None)])

# an Apse open, from the plugin log (open -> fade in, ms)
plug = write(os.path.join(os.environ.get("TMPDIR", "/tmp"), "loadtime_plugin.log"), [
    "14:40:00.100 invtime on: open the inventory or map",
    "14:40:01.500 invtime open (fade to black) screen 0x1234",
    "14:40:01.800 invtime skip: gate told 1 at 300.0 ms",
    "14:40:02.150 invtime readiness done, fade in at 650.0 ms",
])
check("Apse open: 650 ms open -> fade in", [(k, s) for k, s, *_ in loadtime.apse(plug)], [("apse", 0.65)])

print("loadtime: %d/%d checks pass" % (3 if ok else 0, 3))
sys.exit(0 if ok else 1)
