#!/usr/bin/env python3
"""Differential test of the race cameras: runs the physics (original) for a
machine and checks each camera update of the port against the original."""
import argparse
import ctypes
import random
import struct
import sys

from difftest_machine import (INPUT, MACHINE_SIZE, MACHINES, SEED, SOUND, TRACK, PICKUPS, Port,
                              load_course, make_machine)
from emu import Emu
from fakerm import FakeRM

CAMERA, CAMERA_SIZE = 0x44B4D0, 0x1A0
CAMS = {"chase": (0x4011F0, "Camera_Chase")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cam", choices=CAMS)
    ap.add_argument("--runs", type=int, default=20)
    ap.add_argument("--ticks", type=int, default=300)
    ap.add_argument("--seed", type=int, default=11)
    args = ap.parse_args()
    addr, sym = CAMS[args.cam]
    rng = random.Random(args.seed)
    port = Port()
    emu = Emu()
    emu.fpcw = 0x07F
    rm = FakeRM(emu)
    scene = rm.new("IDirect3DRMFrame2", parent=None, pos=None, orient=None, visuals=[], children=[], lights=[])
    for a in (0x4517B4, 0x451724):
        f = rm.new("IDirect3DRMFrame2", parent=scene, pos=None, orient=None, visuals=[], children=[], lights=[])
        emu.write(a, struct.pack("<I", f.addr))
    emu.write(0x451720, struct.pack("<I", scene.addr))
    for a, (op, nargs) in SOUND.items():
        emu.stub(a, nargs, conv="thiscall", record=False)
    fails = ticks = 0
    for run in range(args.runs):
        track, pick = load_course(rng.randint(1, 6), rng.randint(1, 6))
        emu.write(TRACK, track)
        emu.write(PICKUPS, pick)
        port.write("g_track", track)
        port.write("g_pickups", pick)
        emu.write(MACHINES, make_machine(emu, rng, track))
        emu.call(0x401180, ecx=CAMERA)  # Camera_Init
        prev = [0] * 14
        keys = [0] * 14
        for tick in range(args.ticks):
            if tick % rng.randint(5, 30) == 0:
                keys = [0] * 14
                keys[4] = int(rng.random() < 0.8)
                keys[2 + rng.randrange(2)] = int(rng.random() < 0.6)
                keys[8 + rng.randrange(4)] = int(rng.random() < 0.3)
            emu.write(INPUT - 0x38, struct.pack("<14i", *prev))
            emu.write(INPUT, struct.pack("<14i", *keys))
            prev = keys
            emu.write(0x451F70, struct.pack("<d", tick * 1.0))
            emu.write(SEED, struct.pack("<I", rng.getrandbits(32)))
            emu.call(0x404AA0, ecx=MACHINES)  # physics by the original
            for name, a, n in (("g_machines", MACHINES, MACHINE_SIZE), ("g_camera", CAMERA, CAMERA_SIZE),
                               ("g_input", INPUT, 56), ("g_inputPrev", INPUT - 0x38, 56),
                               ("g_phaseTime", 0x451F70, 8)):
                port.write(name, emu.read(a, n))
            emu.call(addr, ecx=CAMERA)
            getattr(port.lib, sym)(ctypes.c_void_p(port.addr("g_camera")))
            a, b = emu.read(CAMERA, CAMERA_SIZE), port.read("g_camera", CAMERA_SIZE)
            ticks += 1
            if a != b:
                diffs = [(o, struct.unpack_from("<f", a, o)[0], struct.unpack_from("<f", b, o)[0])
                         for o in range(0, CAMERA_SIZE, 4) if a[o:o + 4] != b[o:o + 4]]
                print(f"run {run} tick {tick}: " + ", ".join(f"+{o:#x} {x:.9g}/{y:.9g}" for o, x, y in diffs[:6]))
                fails += 1
                break
            if struct.unpack_from("<h", emu.read(MACHINES, MACHINE_SIZE), 0xa8)[0] == 0:
                break
    print(f"{args.cam}: {ticks} ticks, {fails} failing runs")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
