#!/usr/bin/env python3
"""Extract COM vtable layouts from Wine headers into re/vtables.json.

Usage: vtables.py <header-dir> <out.json>
Each DECLARE_INTERFACE_ block in Wine lists every method (inherited ones too),
so a method's vtable offset is simply its index * 4.
"""
import json
import re
import sys
from pathlib import Path

DECL = re.compile(r"DECLARE_INTERFACE_\((\w+)\s*,\s*\w+\)\s*\{(.*?)\n\};", re.S)
METHOD = re.compile(r"STDMETHOD(?:_\(\s*[^,]+,\s*(\w+)\s*\)|\((\w+)\))\s*\((THIS_?)([^;]*?)\)\s*PURE;", re.S)


def stack_args(this, rest):
    """Number of 4-byte stack slots a method takes, including `this`."""
    if this == "THIS":
        return 1
    n = 1
    for arg in rest.split(","):
        # D3DVALUE/float are 4 bytes; D3DCOLOR too. No by-value structs > 4 bytes in these APIs
        # except D3DRMBOX-style pointers, so each argument is one slot.
        n += 1
    return n


def main():
    hdr_dir, out = Path(sys.argv[1]), Path(sys.argv[2])
    tables, nargs = {}, {}
    for h in sorted(hdr_dir.glob("*.h")):
        for name, body in DECL.findall(h.read_text(errors="replace")):
            found = METHOD.findall(body)
            if found:
                tables[name] = [a or b for a, b, _, _ in found]
                nargs[name] = [stack_args(t, r) for _, _, t, r in found]
    out.write_text(json.dumps(tables, indent=1))
    out.with_name("vtable_nargs.json").write_text(json.dumps(nargs))
    print(f"{len(tables)} interfaces -> {out}")


if __name__ == "__main__":
    main()
