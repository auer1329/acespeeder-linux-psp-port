#!/usr/bin/env python3
"""Differential tests of the race helpers that work on the whole machine array:
collisions (0x406f70), rank (0x4071a0), crash (0x406da0), init (0x4047e0),
launch (0x4049a0) and best lap (0x407840). See difftest_machine.py."""
import argparse
import ctypes
import random
import struct
import sys

from difftest_machine import (FIELDS, INPUT, MACHINE_SIZE, MACHINES, POSE_FIELDS, SEED, SOUND, SPARKS,
                              SPARKS_SIZE, TRACK, PICKUPS, Port, compare, load_course, make_machine)
from emu import Emu
from fakerm import FakeRM

ALL = MACHINE_SIZE * 8
EXTRA = [("lap_time[0]", 0xf8, "d"), ("best_lap", 0x138, "d"), ("rank", 0x140, "h"), ("time", 0xf0, "d")]
FUNCS = {  # name: (orig address, port symbol, machine index or None for array functions, returns)
    "collide": (0x406F70, "Race_Collide", None, False),
    "rank": (0x4071A0, "Race_PlayerRank", None, True),
    "crash": (0x406DA0, "Machine_Crash", 0, False),
    "init": (0x4047E0, "Machine_Init", 0, False),
    "launch": (0x4049A0, "Machine_Launch", 0, False),
    "bestlap": (0x407840, "Machine_BestLap", 0, False),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("func", choices=FUNCS)
    ap.add_argument("--runs", type=int, default=300)
    ap.add_argument("--seed", type=int, default=3)
    args = ap.parse_args()
    addr, sym, idx, returns = FUNCS[args.func]
    rng = random.Random(args.seed)
    port = Port()
    emu = Emu()
    emu.fpcw = 0x07F
    rm = FakeRM(emu)
    scene = rm.new("IDirect3DRMFrame2", parent=None, pos=None, orient=None, visuals=[], children=[], lights=[])
    pframe = rm.new("IDirect3DRMFrame2", parent=scene, pos=None, orient=None, visuals=[], children=[], lights=[])
    emu.write(0x451720, struct.pack("<I", scene.addr))
    emu.write(0x4517B4, struct.pack("<I", pframe.addr))
    sounds = []
    for a, (op, nargs) in SOUND.items():
        emu.stub(a, nargs, conv="thiscall", record=False,
                 fn=lambda e, ecx, x, op=op: sounds.append(
                     (op, x[0], struct.unpack("<i", struct.pack("<I", x[1]))[0] if len(x) > 1 else 0)))
    fails = 0
    for run in range(args.runs):
        stage, course = rng.randint(1, 6), rng.randint(1, 6)
        track, pick = load_course(stage, course)
        for e_, p_ in ((emu.write, None),):
            pass
        emu.write(TRACK, track)
        emu.write(PICKUPS, pick)
        port.write("g_track", track)
        port.write("g_pickups", pick)
        ms = [make_machine(emu, rng, track) for _ in range(7)]
        if args.func == "collide":  # put opponents around the player
            p = bytearray(ms[0])
            px, py, pz = struct.unpack_from("<3f", p, 0x28)
            for i in range(1, 7):
                o = bytearray(ms[i])
                struct.pack_into("<3f", o, 0x28, *(c + rng.uniform(-45, 45) for c in (px, py, pz)))
                ms[i] = bytes(o)
        if args.func == "crash":
            p = bytearray(ms[0])
            struct.pack_into("<f", p, 0x2c, rng.uniform(-30, 300))
            ms[0] = bytes(p)
        if args.func == "bestlap":
            p = bytearray(ms[0])
            for k in range(8):
                struct.pack_into("<d", p, 0xf8 + 8 * k, rng.uniform(10, 100))
            ms[0] = bytes(p)
        image = b"".join(ms) + bytes(MACHINE_SIZE)  # slot 7 stays zero
        seed = rng.getrandbits(32)
        for w in (emu.write,):
            w(MACHINES, image)
            w(SEED, struct.pack("<I", seed))
            w(SPARKS, bytes(SPARKS_SIZE))
            w(INPUT, struct.pack("<14i", *(rng.randrange(2) for _ in range(14))))
        port.write("g_machines", image)
        port.write("g_holdrand", struct.pack("<I", seed))
        port.write("g_sparks", bytes(SPARKS_SIZE))
        port.write("g_input", emu.read(INPUT, 56))
        sounds.clear()
        ctypes.c_int.in_dll(port.lib, "test_sound_count").value = 0
        if idx is None:
            r1 = emu.call(addr)
            r2 = getattr(port.lib, sym)()
        else:
            r1 = emu.call(addr, ecx=MACHINES + idx * MACHINE_SIZE)
            r2 = getattr(port.lib, sym)(ctypes.c_void_p(port.addr("g_machines") + idx * MACHINE_SIZE))
        bad = []
        if returns and r1 != r2:
            bad.append(("return", r1, r2))
        a, b = emu.read(MACHINES, ALL), port.read("g_machines", ALL)
        for i in range(8):
            for name, off, fmt in FIELDS + POSE_FIELDS + EXTRA:
                bad += [(f"m{i}.{n}", x, y) for n, x, y in
                        compare(a[i * MACHINE_SIZE:], b[i * MACHINE_SIZE:], 0, [(name, off, fmt)])]
        se, sp = emu.read(SPARKS, SPARKS_SIZE), port.read("g_sparks", SPARKS_SIZE)
        if se != sp:
            i = next(k for k in range(SPARKS_SIZE) if se[k] != sp[k]) & ~3
            bad.append(("sparks", f"+{i:#x}", (struct.unpack_from("<f", se, i), struct.unpack_from("<f", sp, i))))
        if emu.read(SEED, 4) != port.read("g_holdrand", 4):
            bad.append(("rng", "", ""))
        n = ctypes.c_int.in_dll(port.lib, "test_sound_count").value
        log = (ctypes.c_int * 192).in_dll(port.lib, "test_sound_log")
        ps = [tuple(log[3 * k:3 * k + 3]) for k in range(n)]
        if ps != sounds:
            bad.append(("sounds", sounds, ps))
        if bad:
            fails += 1
            print(f"run {run} course {stage}-{course}:", bad[:6])
            if fails > 5:
                break
    print(f"{args.func}: {args.runs} runs, {fails} failures")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
