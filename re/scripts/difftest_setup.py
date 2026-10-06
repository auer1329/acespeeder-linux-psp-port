#!/usr/bin/env python3
"""Differential test of the game-state part of Race_Setup (0x417070): replay
reset, time limit and starting grid, up to Camera_Init. The course is loaded
from the extracted data and Course_BuildTrack is stubbed out."""
import argparse
import random
import struct
import sys

from difftest_machine import MACHINE_SIZE, MACHINES, TRACK, PICKUPS, Port, load_course
from difftest_raceupdate import REGIONS, mask
from emu import Emu

RACE = 0x4513E0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=200)
    ap.add_argument("--seed", type=int, default=4)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    port, emu = Port(), Emu()
    emu.fpcw = 0x07F
    emu.stub(0x418530, 0, conv="thiscall", record=False)  # Course_BuildTrack: track preloaded
    emu.stub(0x43F122, 2, conv="cdecl", record=False)     # fopen: no replay file
    regions = [r for r in REGIONS if r[0] in ("race", "phase", "phaseTime", "machines", "camera", "save", "replay")]
    regions.append(("track", TRACK, 0x1A28, "g_track"))
    fails = 0
    for run in range(args.runs):
        stage, course = (run // 6) % 6 + 1, run % 6 + 1
        track, pick = load_course(stage, course)
        track = bytearray(track)
        if run >= 36:  # vary the time limit and start position
            struct.pack_into("<d", track, 0x1A18, struct.unpack_from("<d", track, 0x1A18)[0] * rng.uniform(0.6, 1.4))
            n = struct.unpack_from("<h", track, 0)[0]
            struct.pack_into("<f", track, 0x15D0, rng.uniform(0, n + 0.99))
        emu.write(TRACK, bytes(track))
        emu.write(PICKUPS, pick)
        emu.write(MACHINES, bytes(rng.getrandbits(8) for _ in range(8 * MACHINE_SIZE)))
        race = bytearray(0x58)
        struct.pack_into("<h", race, 0, 20)
        emu.write(RACE, bytes(race))
        save = bytearray(emu.read(0x451438, 0x2E4))
        struct.pack_into("<hhhh", save, 0, rng.randrange(3), rng.randrange(8), stage - 1, course - 1)
        struct.pack_into("<f", save, 0xC, rng.uniform(1, 100))
        emu.write(0x451438, bytes(save))
        for name, a, n, sym in regions:
            port.write(sym, emu.read(a, n))
        emu.call(0x417070, ecx=RACE, stop=0x417400)
        port.lib.Race_SetupStart(ctypes_ptr(port, "g_race"))
        port.lib.Race_SetupGrid()
        bad = []
        for name, a, n, sym in regions:
            x, y = mask(name, emu.read(a, n)), mask(name, port.read(sym, n))
            if x != y:
                offs = [o for o in range(0, n, 2) if x[o:o + 2] != y[o:o + 2]]
                o = offs[0] & ~3
                bad.append(f"{name} +{o:#x} ({len(offs)} halfwords) {x[o:o + 4].hex()}/{y[o:o + 4].hex()}")
        if bad:
            print(f"run {run} course {stage}-{course}: " + "; ".join(bad))
            fails += 1
    print(f"setup: {args.runs} runs, {fails} failures")
    sys.exit(1 if fails else 0)


def ctypes_ptr(port, name):
    import ctypes
    return ctypes.c_void_p(port.addr(name))


if __name__ == "__main__":
    main()
