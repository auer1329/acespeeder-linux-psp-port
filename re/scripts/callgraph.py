#!/usr/bin/env python3
"""Print callers/callees for game functions from the annotated decompilation.
Usage: callgraph.py [addr-or-name ...]   (no args: whole tree from MainFrame)"""
import re
import sys
from pathlib import Path

D = Path(__file__).resolve().parent.parent / "decomp"
files = {f.name.split("_", 1)[0]: f for f in D.glob("*.c")}
name_of = {a: f.stem.split("_", 1)[1] for a, f in files.items()}
addr_of = {n: a for a, n in name_of.items()}
calls = {}
for a, f in files.items():
    if a >= "0043da00":
        continue
    text = f.read_text()
    body = text[text.find("{"):]
    callees = []
    for n in re.findall(r"\b([A-Za-z_]\w*)\(", body):
        if n in addr_of and addr_of[n] != a and addr_of[n] < "0043da00" and n not in callees:
            callees.append(n)
    calls[name_of[a]] = callees
callers = {}
for c, cs in calls.items():
    for x in cs:
        callers.setdefault(x, []).append(c)


def tree(n, depth=0, seen=None):
    seen = seen if seen is not None else set()
    print("  " * depth + n + ("" if n not in seen else " ..."))
    if n in seen:
        return
    seen.add(n)
    for c in calls.get(n, []):
        tree(c, depth + 1, seen)


if len(sys.argv) == 1:
    tree("WinMain_")
else:
    for q in sys.argv[1:]:
        n = name_of.get(q.rjust(8, "0"), q)
        print(f"{n}\n  callers: {callers.get(n, [])}\n  callees: {calls.get(n, [])}")
