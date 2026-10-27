#!/usr/bin/env python3
"""ApseMap.gfx k-d tree: cache each marker's position once, sort natively.

KDTreeNode re-sorts its objects at every tree level with an AS2 comparator (a.Position.x > b.Position.x through a
getter): O(n log^2 n) script calls per map open, >= 57 ms (2026-10-26 profile). The patch stores __kdx/__kdy on each
object in KDTree.AddArray and uses Array.sortOn(field, NUMERIC) (native compares; the game's own map script uses
sortOn the same way). Same tree, ties may order differently (the original comparator never returned 0).
Usage: kdtree.py <decompiled __Packages dir> <out __Packages dir>
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
t = open(f"{src}/KDTree.as", newline="").read()
nl = "\r\n" if "\r\n" in t else "\n"
t = t.replace("\r\n", "\n")
a = "      if(_loc3_.length > 0)\n      {\n         _loc6_"
b = ("      var _kI = 0;\n      var _kP;\n      while(_kI < _loc3_.length)\n      {\n"
     "         _kP = _loc3_[_kI].Position;\n         _loc3_[_kI].__kdx = _kP.x;\n         _loc3_[_kI].__kdy = _kP.y;\n"
     "         _kI++;\n      }\n" + a)
assert t.count(a) == 1, "KDTree.AddArray anchor not found"
open(f"{dst}/KDTree.as", "w", newline="").write(t.replace(a, b).replace("\n", nl))
n = open(f"{src}/KDTreeNode.as", newline="").read()
for ax in "XY":
    a = f"objectsA.sort({ax}CoordinateSort);"
    assert n.count(a) == 1, f"KDTreeNode {ax} sort anchor not found"
    n = n.replace(a, f'objectsA.sortOn("__kd{ax.lower()}",Array.NUMERIC);')
open(f"{dst}/KDTreeNode.as", "w", newline="").write(n)
print("kdtree: patched KDTree.as, KDTreeNode.as")
