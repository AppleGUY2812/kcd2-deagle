#!/usr/bin/env python3
"""Read the real numbers off KCD2's own log: boot, level load, save load, fast travel, Apse open.

The game only writes clock times when `log_IncludeTime 1` is set (the plugin sends it at start, and
`tools/live.sh cmd "log_IncludeTime 1"` sets it in a running game); before that line the log has no
times and this prints "-" for those spans.

    python3 tools/loadtime.py                     # the running session (kcd.log)
    python3 tools/loadtime.py --all               # every kept log, oldest first
    python3 tools/loadtime.py path/to/kcd.log     # one file

Spans, and the markers they are measured between (the first line and the last line of the span):
  boot      Log Started -> gameplay started          the whole way from launch to Henry being controllable
  level     Loading level X -> gameplay started      a new level (a fresh boot, or a region change)
  save      Loading saved game -> gameplay started   a load into a level that is already up
  travel    SkipTime.gfx -> its next line that ends the skip   (map fast travel / sleep)
  apse      invtime open -> invtime readiness done (deagle_native.log, needs deagle_invtime on)

`-` means the log has no timestamps there; `?` means the start or the end marker never appeared.
"""

import os
import re
import sys

GAME = os.environ.get("KCD2", "/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2")
TS = re.compile(r"^<(\d\d):(\d\d):(\d\d)>\s?(.*)$")
STARTED = re.compile(r"^Log Started at (\d{4})-(\d\d)-(\d\d)\s+(\d\d):(\d\d):(\d\d)")


def stamp(m):
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))


def spans(path):
    """[(kind, seconds or None, label, start line no, end line no)] for one kcd.log."""
    out = []
    lines = open(path, "rb").read().decode("utf-8", "replace").splitlines()

    started = None   # (seconds into the day, line no, "HH:MM:SS") of the log's own first line
    for i, line in enumerate(lines[:40]):
        m = STARTED.match(line)
        if m:
            h, mi, s = int(m.group(4)), int(m.group(5)), int(m.group(6))
            started = (h * 3600 + mi * 60 + s, i + 1, "%02d:%02d:%02d" % (h, mi, s))
            break

    def find(start_at, preds, limit=100000):
        """first line after start_at matching any pred -> (line index, match)"""
        for j in range(start_at, min(len(lines), start_at + limit)):
            m = TS.match(lines[j])
            if not m:
                continue
            body = m.group(4)
            for p in preds:
                if p.search(body):
                    return j, m, p
        return None, None, None

    def next_ts(i):
        for j in range(i, len(lines)):
            m = TS.match(lines[j])
            if m:
                return j, m
        return None, None

    # boot: the log's first line to the first gameplay start
    j, mm = next_ts(started[1] if started else 0)
    while j is not None and "[Deagle] gameplay started" not in TS.match(lines[j]).group(4):
        j, mm = next_ts(j + 1)
    out.append(("boot", stamp(mm) - started[0] if (mm and started) else None,
                "%s (launch) -> gameplay started" % (started[2] if started else "?"),
                started[1] if started else None, (j + 1) if j is not None else None))

    # save loads: "Loading saved game" to the gameplay start; one superseded by a later load is dropped
    loads = []
    for i, line in enumerate(lines):
        m = TS.match(line)
        if not m or not m.group(4).startswith("Loading saved game"):
            continue
        j, mm, _ = find(i + 1, [re.compile(r"\[Deagle\] gameplay started")], limit=40000)
        loads.append((i, j, m, mm))
    for n, (i, j, m, mm) in enumerate(loads):
        later = [x for x in loads[n + 1:] if j is None or x[0] < j]
        if later:
            continue    # the game loaded again before it ever got to gameplay: not a load time
        name = os.path.basename(m.group(4).split("'")[1]) if "'" in m.group(4) else m.group(4)[:60]
        withlevel = any("Loading level " in TS.match(l).group(4) for l in lines[i:j if j else i + 1] if TS.match(l))
        out.append(("load", stamp(mm) - stamp(m) if mm else None,
                    name + (" (into a new level)" if withlevel else ""), i + 1, j + 1 if j is not None else None))

    # a level load on its own (a region change mid-session): not inside the boot or a save load
    inside = [(a, b) for kind, _, _, a, b in out if kind in ("load", "boot") and a]
    for i, line in enumerate(lines):
        m = TS.match(line)
        if not m or "Loading level " not in m.group(4):
            continue
        if any((a or 0) <= i + 1 <= (b or 0) for a, b in inside):
            continue    # it is the level behind a save load, counted there
        j, mm, _ = find(i + 1, [re.compile(r"\[Deagle\] gameplay started")], limit=40000)
        lvl = m.group(4).split("Loading level ")[1].split()[0].strip("=")
        out.append(("level", stamp(mm) - stamp(m) if mm else None, lvl, i + 1, j + 1 if j is not None else None))

    # fast travel / sleep: the skip movie comes up, the next load or gameplay start ends it
    for i, line in enumerate(lines):
        m = TS.match(line)
        if not m or "SkipTime.gfx" not in m.group(4):
            continue
        j, mm, _ = find(i + 1, [re.compile(r"\[Deagle\] gameplay started"),
                                re.compile(r"^Loading level "),
                                re.compile(r"^Loading saved game")], limit=40000)
        out.append(("travel", stamp(mm) - stamp(m) if mm else None, "fast travel / sleep",
                    i + 1, j + 1 if j is not None else None))
    out.sort(key=lambda r: r[3] or 0)
    return out


def apse(path):
    """Apse opens from the plugin log: open -> fade in, in ms (deagle_invtime)."""
    if not os.path.exists(path):
        return []
    out, start = [], None
    for line in open(path, "rb").read().decode("utf-8", "replace").splitlines():
        m = re.match(r"^(\d\d):(\d\d):(\d\d)\.\d+ invtime open \(fade to black\)", line)
        if m:
            start = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
            continue
        m = re.match(r"^\S+ invtime readiness done, fade in at ([\d.]+) ms", line)
        if m and start is not None:
            out.append(("apse", float(m.group(1)) / 1000.0, "open -> fade in", 0, 0))
            start = None
    return out


def report(path, plugin):
    print("== %s" % os.path.basename(path))
    rows = spans(path) + apse(plugin)
    if all(secs is None for kind, secs, *_ in rows):
        print("   no timestamps in this log (log_IncludeTime was off); %d markers, all without times" % len(rows))
        return
    if not rows:
        print("   no load, travel or Apse-open markers")
        return
    for kind, secs, label, a, b in rows:
        if secs is None:
            val = "-"
        elif kind == "apse":
            val = "%.0f ms" % (secs * 1000)
        else:
            val = "%.1f s" % secs
        print("   %-6s %8s  %s%s" % (kind, val, label, "" if not a else "  (line %d -> %d)" % (a, b or 0)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    all_logs = "--all" in sys.argv
    plugin = os.path.join(GAME, "Bin/Win64MasterMasterSteamPGO/deagle_native.log")
    if args:
        for p in args:
            report(p, plugin)
        return
    if all_logs:
        back = os.path.join(GAME, "logbackups")
        logs = sorted((os.path.join(back, f) for f in os.listdir(back) if f.startswith("kcd")),
                      key=os.path.getmtime)
    else:
        logs = [os.path.join(GAME, "kcd.log")]
    for p in logs:
        report(p, plugin)


if __name__ == "__main__":
    main()
