#!/usr/bin/env python3
"""Read the password check of AceSpeeder.exe (Save_CheckCode, 0x418b70) into
data/unlock.dat, so the port's Password screen accepts the same code as the
original without the code being part of the source: the author asked that it
is never published.

unlock.dat: the 8 letters of the code (one byte each, 0..5 = A..F), then the
three 32-bit values the original stores in gamedata.dat to remember it.

Usage: extract_unlock.py [AceSpeeder.exe] [output]"""
import re
import struct
import sys
from pathlib import Path

port = Path(__file__).resolve().parent.parent
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else port.parent / "game" / "AceSpeeder.exe"
out = Path(sys.argv[2]) if len(sys.argv) > 2 else port / "data" / "unlock.dat"

# cmp word [eax+2k], imm8 / jne for each letter (the sixth via mov edx, imm32;
# cmp [eax+0xa], dx), then the three stores into the save data.
PATTERN = re.compile(
    rb"\x8b\x44\x24\x04\x66\x83\x38(.)\x75."
    rb"\x66\x83\x78\x02(.)\x75.\x66\x83\x78\x04(.)\x75.\x66\x83\x78\x06(.)\x75.\x66\x83\x78\x08(.)\x75."
    rb"\xba(....)\x66\x39\x50\x0a\x75."
    rb"\x66\x83\x78\x0c(.)\x75.\x66\x83\x78\x0e(.)\x75."
    rb"\x66\x89\x91\xe0\x02\x00\x00"
    rb"\xc7\x81\xd4\x02\x00\x00(....)\xc7\x81\xd8\x02\x00\x00(....)\xc7\x81\xdc\x02\x00\x00(....)",
    re.DOTALL)

m = PATTERN.search(exe.read_bytes())
if not m:
    sys.exit(f"{exe}: password check not found (unknown version of the game?)")
g = m.groups()
letters = [g[0][0], g[1][0], g[2][0], g[3][0], g[4][0], struct.unpack("<I", g[5])[0], g[6][0], g[7][0]]
if any(not 0 <= v <= 5 for v in letters):
    sys.exit(f"{exe}: unexpected password check")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(bytes(letters) + g[8] + g[9] + g[10])
print("wrote", out)
