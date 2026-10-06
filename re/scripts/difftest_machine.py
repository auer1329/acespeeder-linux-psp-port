#!/usr/bin/env python3
"""Differential test of the ported machine physics against the original binary.

The original Machine_Update (0x404aa0) runs under Unicorn; the port's version
(port/build/libgametest.so) runs natively through ctypes. Both get the same
track, pads, machine state, input and RNG seed every tick; the resulting state
is compared field by field, then the port is resynchronised from the original
so each tick is tested on its own.

Usage: difftest_machine.py [--course S C] [--runs N] [--ticks N] [--seed N]
"""
import argparse
import ctypes
import random
import struct
import sys
from pathlib import Path

from emu import Emu

ROOT = Path(__file__).resolve().parent.parent.parent
LIB = ROOT / "port" / "build" / "libgametest.so"

TRACK, PICKUPS, MACHINES, SPARKS, INPUT, SEED = 0x44C230, 0x451F78, 0x44B670, 0x44F490, 0x44F450, 0x44A82C
MACHINE_SIZE, TRACK_SIZE, SPARKS_SIZE = 0x178, 0x1A28, 0x5E0
SOUND = {0x41EBE0: (0, 1), 0x41EC00: (1, 1), 0x41EC20: (2, 1), 0x41EC40: (3, 2), 0x41EC60: (4, 2)}

# name, offset, format; derived TrackPos fields (t2..omu) are skipped on purpose
FIELDS = [("model", 0x0, "h"), ("tp.seg", 0x4, "h"), ("tp.t", 0x8, "f"), ("tp.slot", 0xc, "h"),
          ("tp.u", 0x10, "f"), ("tp.h", 0x14, "f")]
FIELDS += [(f"pos.{c}", 0x28 + 4 * i, "f") for i, c in enumerate("xyz")]
FIELDS += [(f"rot[{i}]", 0x34 + 4 * i, "f") for i in range(9)]
FIELDS += [("throttle", 0x88, "f")] + [(f"accel.{c}", 0x8c + 4 * i, "f") for i, c in enumerate("xyz")]
FIELDS += [(f"vel.{c}", 0x98 + 4 * i, "f") for i, c in enumerate("xyz")]
FIELDS += [("steer", 0xa4, "f"), ("alive", 0xa8, "h"), ("on_road", 0xaa, "h"), ("grounded", 0xac, "h"),
           ("lap", 0xae, "h"), ("power", 0xb0, "f"), ("airborne", 0xb4, "h"), ("air_time", 0xb8, "f"),
           ("path_idx", 0xbc, "h"), ("hit_time", 0xe4, "f"), ("sp_active", 0xe8, "h"), ("sp_kind", 0xea, "h"),
           ("sp_timer", 0xec, "f")]
POSE_FIELDS = [(f"pos.{c}", 0x28 + 4 * i, "f") for i, c in enumerate("xyz")]
POSE_FIELDS += [(f"rot[{i}]", 0x34 + 4 * i, "f") for i in range(9)]
POSE_FIELDS += [(f"dpos.{c}", 0x58 + 4 * i, "f") for i, c in enumerate("xyz")]
POSE_FIELDS += [(f"drot[{i}]", 0x64 + 4 * i, "f") for i in range(9)]
POSE_FIELDS += [("shadow_vis", 0x142, "h")] + [(f"shadow_pos.{c}", 0x144 + 4 * i, "f") for i, c in enumerate("xyz")]
POSE_FIELDS += [(f"shadow_rot[{i}]", 0x150 + 4 * i, "f") for i in range(9)]


class Port:
    def __init__(self):
        self.lib = ctypes.CDLL(str(LIB))

    def addr(self, name):
        return ctypes.addressof(ctypes.c_char.in_dll(self.lib, name))

    def write(self, name, data, off=0):
        ctypes.memmove(self.addr(name) + off, data, len(data))

    def read(self, name, n, off=0):
        return ctypes.string_at(self.addr(name) + off, n)


def load_course(stage, course):
    blob = (ROOT / "port" / "data" / f"track_{stage}_{course}.bin").read_bytes()
    n = struct.unpack_from("<I", blob, 12)[0]
    track = blob[16:16 + n]
    m = struct.unpack_from("<I", blob, 16 + n)[0]
    pick = blob[20 + n:20 + n + m]
    return track, pick


def make_machine(emu, rng, track):
    """A machine somewhere on the course, positioned by the original Track_PointFrame."""
    n = struct.unpack_from("<h", track, 0)[0]
    s = rng.uniform(0, n + 0.999)
    tp = TrackPos(s, rng.uniform(3.0, 4.0), rng.uniform(20, 60))
    m = bytearray(MACHINE_SIZE)
    struct.pack_into("<h", m, 0, rng.randrange(8))
    m[4:4 + 36] = tp
    # pos/rot via the original function (Track_PointFrame, 0x407f50)
    emu.write(0x00300000, bytes(48))
    emu.call(0x407F50, args=list(struct.unpack("<9I", tp)) + [0x00300000, 0x0030000C], ecx=TRACK)
    m[0x28:0x28 + 12] = emu.read(0x00300000, 12)
    m[0x34:0x34 + 36] = emu.read(0x0030000C, 36)
    struct.pack_into("<f", m, 0x88, rng.uniform(0, 3))
    struct.pack_into("<3f", m, 0x98, *(rng.uniform(-20, 20) for _ in range(3)))
    struct.pack_into("<f", m, 0xa4, rng.uniform(-0.03, 0.03))
    struct.pack_into("<hhhh", m, 0xa8, 1, rng.randrange(2), rng.randrange(2), rng.randrange(3))
    struct.pack_into("<f", m, 0xb0, 100.0)
    struct.pack_into("<d", m, 0xf0, rng.uniform(0, 30))
    auto = 1 if rng.random() < 0.3 else 0
    struct.pack_into("<hh", m, 0x174, auto, auto)
    return bytes(m)


def TrackPos(s, l, h):
    import math
    seg, slot = math.floor(s), math.floor(l)
    t, u = s - seg, l - slot
    t, u = struct.unpack("<2f", struct.pack("<2f", t, u))
    return struct.pack("<hhfhhf5f", seg, 0, t, slot, 0, u, h, t * t, 1 - t, (1 - t) ** 2, 1 - u)


def compare(a, b, tol, fields=FIELDS):
    bad = []
    for name, off, fmt in fields:
        va = struct.unpack_from("<" + fmt, a, off)[0]
        vb = struct.unpack_from("<" + fmt, b, off)[0]
        if fmt == "h":
            if va != vb:
                bad.append((name, va, vb))
        elif not (abs(va - vb) <= tol * max(1.0, abs(va)) or (va != va and vb != vb)):
            bad.append((name, va, vb))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--course", nargs=2, type=int, default=None)
    ap.add_argument("--runs", type=int, default=20)
    ap.add_argument("--ticks", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--tol", type=float, default=1e-5)
    ap.add_argument("--func", choices=["update", "pose"], default="update")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    port = Port()
    emu = Emu()
    emu.fpcw = 0x07F
    from fakerm import FakeRM
    rm = FakeRM(emu)
    scene = rm.new("IDirect3DRMFrame2", parent=None, pos=None, orient=None, visuals=[], children=[], lights=[])
    pframe = rm.new("IDirect3DRMFrame2", parent=scene, pos=None, orient=None, visuals=[], children=[], lights=[])
    emu.write(0x451720, struct.pack("<I", scene.addr))
    emu.write(0x4517B4, struct.pack("<I", pframe.addr))
    sounds = []
    for addr, (op, nargs) in SOUND.items():
        emu.stub(addr, nargs, conv="thiscall", record=False,
                 fn=lambda e, ecx, a, op=op: sounds.append(
                     (op, a[0], struct.unpack("<i", struct.pack("<I", a[1]))[0] if len(a) > 1 else 0)))
    fails = ticks = 0
    for run in range(args.runs):
        stage, course = args.course or (rng.randint(1, 6), rng.randint(1, 6))
        track, pick = load_course(stage, course)
        emu.write(TRACK, track)
        emu.write(PICKUPS, pick)
        port.write("g_track", track)
        port.write("g_pickups", pick)
        emu.write(SPARKS, bytes(SPARKS_SIZE))
        port.write("g_sparks", bytes(SPARKS_SIZE))
        m = make_machine(emu, rng, track)
        keys = [0] * 14
        for tick in range(args.ticks):
            if tick % rng.randint(5, 30) == 0:
                keys = [0] * 14
                keys[4] = int(rng.random() < 0.8)
                keys[2 + rng.randrange(2)] = int(rng.random() < 0.6)
            seed = rng.getrandbits(32)
            inp = struct.pack("<14i", *keys)
            emu.write(INPUT, inp)
            port.write("g_input", inp)
            emu.write(SEED, struct.pack("<I", seed))
            port.write("g_holdrand", struct.pack("<I", seed))
            sparks = emu.read(SPARKS, SPARKS_SIZE)
            port.write("g_sparks", sparks)
            emu.write(MACHINES, m)
            port.write("g_machines", m)
            sounds.clear()
            ctypes.c_int.in_dll(port.lib, "test_sound_count").value = 0
            phase = rng.uniform(0, 3000)
            emu.write(0x451F70, struct.pack("<d", phase))
            port.write("g_phaseTime", struct.pack("<d", phase))
            if args.func == "update":
                emu.call(0x404AA0, ecx=MACHINES)
                port.lib.Machine_Update(ctypes.c_void_p(port.addr("g_machines")))
            else:
                emu.call(0x407230, ecx=MACHINES)
                port.lib.Machine_UpdatePose(ctypes.c_void_p(port.addr("g_machines")))
            a = emu.read(MACHINES, MACHINE_SIZE)
            b = port.read("g_machines", MACHINE_SIZE)
            bad = compare(a, b, args.tol, FIELDS if args.func == "update" else POSE_FIELDS)
            if emu.read(SEED, 4) != port.read("g_holdrand", 4):
                bad.append(("rng", emu.read(SEED, 4).hex(), port.read("g_holdrand", 4).hex()))
            psounds = [tuple(ctypes.c_int.in_dll(port.lib, "test_sound_log").__class__.from_address(
                port.addr("test_sound_log") + 12 * i + 4 * j).value for j in range(3))
                for i in range(ctypes.c_int.in_dll(port.lib, "test_sound_count").value)]
            if sounds != psounds:
                bad.append(("sounds", sounds[:], psounds))
            ea = emu.read(SPARKS, SPARKS_SIZE)
            pa = port.read("g_sparks", SPARKS_SIZE)
            if ea[:0x68] != pa[:0x68]:
                bad.append(("sparks", "ring/active differ", ""))
            fe = struct.unpack_from("<350f", ea, 0x68)
            fp = struct.unpack_from("<350f", pa, 0x68)
            worst = max((abs(x - y) / max(1.0, abs(x)) for x, y in zip(fe, fp)), default=0)
            if worst > 1e-4:  # particle effects: only cosmetic
                bad.append(("sparks", f"rel err {worst:g}", ""))
            ticks += 1
            if bad:
                fails += 1
                st = struct.unpack_from("<h2xf", m, 4)
                print(f"course {stage}-{course} run {run} tick {tick} seg {st[0]} t {st[1]:.3f}:")
                for name, va, vb in bad[:8]:
                    print(f"    {name:10} orig={va} port={vb}")
                if fails >= 10:
                    sys.exit(1)
                break
            m = a  # continue from the original's state
            if args.func == "pose":  # keep the physics moving so poses vary
                emu.write(MACHINES, m)
                emu.call(0x404AA0, ecx=MACHINES)
                m = emu.read(MACHINES, MACHINE_SIZE)
            if struct.unpack_from("<h", m, 0xa8)[0] == 0:
                break
    print(f"{ticks} ticks, {fails} mismatching runs")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
