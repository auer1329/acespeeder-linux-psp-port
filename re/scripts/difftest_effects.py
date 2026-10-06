#!/usr/bin/env python3
"""Differential tests of the small race objects: HUD message quads, sparks,
explosion, scenery and gates updates."""
import argparse
import ctypes
import random
import struct
import sys

from difftest_machine import SEED, TRACK, PICKUPS, Port, load_course
from emu import Emu

HUDMSG, SPARKS, EXPLOSION, SCENERY, GATES = 0x453150, 0x44F490, 0x458718, 0x458008, 0x44F048
HUD_FUNCS = [(0x41D100, "HudMsg_Fail"), (0x41D320, "HudMsg_Start"), (0x41D5A0, "HudMsg_Goal"),
             (0x41D7C0, "HudMsg_GameOver"), (0x41D9C0, "HudMsg_Wide")]


def rand_floats(rng, n, lo, hi):
    return struct.pack(f"<{n}f", *(rng.uniform(lo, hi) for _ in range(n)))


def check(name, a, b, fails, ctx=""):
    if a == b:
        return fails
    diffs = [o for o in range(0, len(a), 4) if a[o:o + 4] != b[o:o + 4]]
    print(f"{name} {ctx}: {len(diffs)} words differ, first at +{diffs[0]:#x}: "
          f"{a[diffs[0]:diffs[0] + 4].hex()} / {b[diffs[0]:diffs[0] + 4].hex()}")
    return fails + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=5)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    port, emu = Port(), Emu()
    emu.fpcw = 0x07F
    fails = 0
    hud = ctypes.c_void_p(port.addr("g_hudMsg"))
    special = [0.0, 0.2, 0.25, 0.5, 0.999, 1.0, 1.5, 2.0, 2.5]
    for run in range(args.runs):
        # HUD messages
        addr, sym = HUD_FUNCS[run % 5]
        idx = rng.randrange(8)
        s = rng.choice(special) if rng.random() < 0.2 else rng.uniform(0, 2.3)
        emu.call(0x41CE60, ecx=HUDMSG)
        getattr(port.lib, "HudMsg_Init")(hud)
        emu.call(addr, args=[idx, float(s)], ecx=HUDMSG)
        getattr(port.lib, sym)(hud, idx, ctypes.c_float(s))
        fails = check(sym, emu.read(HUDMSG, 0x80), port.read("g_hudMsg", 0x80), fails, f"id {idx} s {s}")
        # sparks
        img = struct.pack("<h", rng.randrange(50)) + struct.pack("<50h", *(rng.randrange(2) for _ in range(50))) + bytes(2)
        img += rand_floats(rng, 50, 0, 30) + rand_floats(rng, 300, -500, 500)
        emu.write(SPARKS, img)
        port.write("g_sparks", img)
        emu.call(0x410DB0, ecx=SPARKS)
        port.lib.Sparks_Update()
        fails = check("sparks", emu.read(SPARKS, len(img)), port.read("g_sparks", len(img)), fails)
        # explosion
        img = struct.pack("<h", rng.randrange(50)) + struct.pack("<50h", *(rng.randrange(2) for _ in range(50)))
        img += struct.pack("<50h", *(rng.randrange(60) for _ in range(50))) + bytes(2) + rand_floats(rng, 150, -3000, 3000)
        seed = struct.pack("<I", rng.getrandbits(32))
        for w, a in ((emu.write, EXPLOSION), (port.write, "g_explosion")):
            w(a, img)
        emu.write(SEED, seed)
        port.write("g_holdrand", seed)
        pos = rand_floats(rng, 3, -3000, 3000)
        emu.call(0x41E740, args=list(struct.unpack("<3I", pos)) + [0, 0, 0], ecx=EXPLOSION)
        port.lib.test_explosion_add(ctypes.create_string_buffer(pos, 12))
        emu.call(0x41E690, ecx=EXPLOSION)
        port.lib.Explosion_Update()
        fails = check("explosion", emu.read(EXPLOSION, len(img)) + emu.read(SEED, 4),
                      port.read("g_explosion", len(img)) + port.read("g_holdrand", 4), fails)
        # gates
        img = bytes(0x40) + struct.pack("<hh", rng.randrange(17), 0) + bytes(0x300) + rand_floats(rng, 32, -10, 10)
        emu.write(GATES, img)
        port.write("g_gates", img)
        emu.call(0x40E4C0, ecx=GATES)
        port.lib.Gates_Update()
        fails = check("gates", emu.read(GATES, len(img)), port.read("g_gates", len(img)), fails)
        # scenery
        if run % 20 == 0:
            track, pick = load_course(rng.randint(1, 6), rng.randint(1, 6))
            emu.write(TRACK, track)
            port.write("g_track", track)
            n = struct.unpack_from("<h", track, 0)[0]
            emu.write(SCENERY, bytes(0x710))
            port.write("g_scenery", bytes(0x710))
            emu.write(SEED, seed)
            port.write("g_holdrand", seed)
            emu.call(0x41E0E0, ecx=SCENERY)
            emu.call(0x41E280, args=[n], ecx=SCENERY)
            emu.call(0x41E340, ecx=SCENERY)
            port.lib.Scenery_Init()
            port.lib.Scenery_Populate(n)
            port.lib.Scenery_Update()
            fails = check("scenery", emu.read(SCENERY, 0x710) + emu.read(SEED, 4),
                          port.read("g_scenery", 0x710) + port.read("g_holdrand", 4), fails, f"n {n}")
        if fails > 10:
            break
    print(f"effects: {args.runs} runs, {fails} failures")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
