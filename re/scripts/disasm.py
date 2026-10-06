#!/usr/bin/env python3
"""Annotated disassembly of a function range: constants from .rdata/.data are shown
with their values, globals and call targets with names from names.txt/sigs.txt.

Usage: disasm.py <start-hex> [end-hex]   (end defaults to the next function in decomp/INDEX.txt)
"""
import re
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

import peread

RE = Path(__file__).resolve().parent.parent
EXE = (RE.parent / "game" / "AceSpeeder.exe").read_bytes()


def load_names():
    names = {}
    for line in (RE / "names.txt").read_text().splitlines():
        line = line.split("#")[0].split()
        if len(line) >= 2:
            names[int(line[0], 16)] = line[1]
    for line in (RE / "sigs.txt").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            addr, proto = line.split(" ", 1)
            m = re.search(r"(\w+)\(", proto)
            if m:
                names[int(addr, 16)] = m.group(1)
    return names


NAMES = load_names()
FUNCS = sorted(int(l.split()[0], 16) for l in (RE / "decomp" / "INDEX.txt").read_text().splitlines())


def label(addr):
    if addr in NAMES:
        return NAMES[addr]
    best = max((a for a in NAMES if a <= addr and addr - a < 0x2000 and not NAMES[a][0].isupper()), default=None)
    if best is not None and NAMES[best].startswith("g_"):
        return f"{NAMES[best]}+{addr - best:#x}"
    return None


def const(addr, size):
    off = peread.va_to_off(EXE, addr)
    if off is None:
        return None
    if size == 8:
        return repr(struct.unpack_from("<d", EXE, off)[0])
    v = struct.unpack_from("<f", EXE, off)[0]
    return f"{v:g}f"


def main():
    start = int(sys.argv[1], 16)
    end = int(sys.argv[2], 16) if len(sys.argv) > 2 else next(a for a in FUNCS if a > start)
    code = EXE[peread.va_to_off(EXE, start):peread.va_to_off(EXE, end)]
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for ins in md.disasm(code, start):
        note = []
        for m in re.finditer(r"(qword|dword) ptr \[(0x[0-9a-f]+)\]", ins.op_str):
            a = int(m.group(2), 16)
            if 0x447000 <= a < 0x449000 and ins.mnemonic.startswith("f"):
                note.append(f"{m.group(2)}={const(a, 8 if m.group(1) == 'qword' else 4)}")
            elif label(a):
                note.append(label(a))
        for m in re.finditer(r"\[(\w+)\*\d \+ (0x[0-9a-f]+)\]|\[(?:\w+ \+ )?(0x4[4-5][0-9a-f]{4})\]", ins.op_str):
            a = int(m.group(2) or m.group(3), 16)
            if label(a) and label(a) not in note:
                note.append(label(a))
        if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            note.append(NAMES.get(t, f"FUN_{t:08x}"))
        if ins.mnemonic in ("push", "mov") and re.fullmatch(r".*0x[34][0-9a-f]{7}", ins.op_str):
            v = int(ins.op_str.split(",")[-1].strip(), 16)
            note.append(f"{struct.unpack('<f', struct.pack('<I', v))[0]:g}f?")
        s = f"{ins.address:06x}  {ins.mnemonic:6} {ins.op_str}"
        print(f"{s:58}; {', '.join(note)}" if note else s)


if __name__ == "__main__":
    main()
