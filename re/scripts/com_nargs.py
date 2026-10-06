#!/usr/bin/env python3
"""Write com_nargs.txt: '<vtable offset> <arg count>...' over the interfaces in annotate.USED.
FixComCalls.java uses it to pick a stack-purge size for each indirect call site."""
import json
from pathlib import Path

from annotate import USED

RE = Path(__file__).resolve().parent.parent
nargs = json.loads((RE / "vtable_nargs.json").read_text())
by_off = {}
for iface in USED.values():
    for i, n in enumerate(nargs[iface]):
        by_off.setdefault(i * 4, set()).add(n)
with open(RE / "com_nargs.txt", "w") as f:
    for off in sorted(by_off):
        f.write(f"{off} {' '.join(map(str, sorted(by_off[off])))}\n")
print(f"{len(by_off)} offsets -> com_nargs.txt")
