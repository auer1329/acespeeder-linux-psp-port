#!/usr/bin/env python3
"""Differential test of Race_Update (0x411530): random race phases, substates,
timers, game modes and inputs; several ticks per run, comparing every global
the race logic touches plus the order of sound, music, door and cleanup calls."""
import argparse
import ctypes
import random
import struct
import sys

from difftest_machine import (INPUT, MACHINE_SIZE, MACHINES, SEED, SOUND, TRACK, PICKUPS, Port, load_course,
                              make_machine)
from emu import Emu
from fakerm import FakeRM

RACE, CAMERA = 0x4513E0, 0x44B4D0
REGIONS = [  # name, original address, size, port symbol
    ("race", RACE, 0x58, "g_race"), ("phase", 0x451F68, 4, "g_racePhase"), ("phaseTime", 0x451F70, 8, "g_phaseTime"),
    ("gameState", 0x44A228, 4, "g_gameState"), ("machines", MACHINES, 8 * MACHINE_SIZE, "g_machines"),
    ("camera", CAMERA, 0x1A0, "g_camera"), ("save", 0x451438, 0x2E4, "g_save"), ("replay", 0x4531D0, 0x4E38, "g_replay"),
    ("hud", 0x453150, 0x80, "g_hudMsg"), ("explosion", 0x458718, 0x324, "g_explosion"), ("sparks", 0x44F490, 0x5E0, "g_sparks"),
    ("scenery", 0x458008, 0x710, "g_scenery"), ("gates", 0x44F048, 0x3C4, "g_gates"), ("input", INPUT, 56, "g_input"),
    ("inputPrev", INPUT - 0x38, 56, "g_inputPrev"), ("seed", SEED, 4, "g_holdrand"),
]
PHASES = [0, 10, 20, 20, 20, 30, 30, 40, 42, 44, 46, 90, 100, 110, 200]
TIMES = [0.0, 5.0, 9.0, 11.0, 39.0, 41.0, 49.0, 51.0, 79.0, 99.0, 101.0, 149.0, 151.0, 199.0, 201.0, 249.0, 251.0,
         299.0, 301.0, 449.0, 451.0, 499.0, 501.0, 549.0, 551.0, 1499.0, 1501.0]


def mask(name, data):
    """Blank TrackPos padding, which holds stale stack bytes in the original."""
    if name == "camera":
        tps = (0xE4, 0x108, 0x12C)
    elif name == "machines":
        tps = [m * MACHINE_SIZE + o for m in range(8) for o in (0x4, 0xC0)]
    else:
        return data
    data = bytearray(data)
    for o in tps:
        data[o + 2:o + 4] = data[o + 10:o + 12] = b"\0\0"
    return bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=300)
    ap.add_argument("--ticks", type=int, default=40)
    ap.add_argument("--seed", type=int, default=9)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    port, emu = Port(), Emu()
    emu.fpcw = 0x07F
    rm = FakeRM(emu)
    scene = rm.new("IDirect3DRMFrame2", parent=None, pos=None, orient=None, visuals=[], children=[], lights=[])
    for a in (0x4517B4, 0x451724):
        f = rm.new("IDirect3DRMFrame2", parent=scene, pos=None, orient=None, visuals=[], children=[], lights=[])
        emu.write(a, struct.pack("<I", f.addr))
    emu.write(0x451720, struct.pack("<I", scene.addr))
    calls = []

    def sint(x):
        return struct.unpack("<i", struct.pack("<I", x))[0]

    def flt(x):
        return struct.unpack("<f", struct.pack("<I", x))[0]

    for a, (op, nargs) in SOUND.items():
        emu.stub(a, nargs, conv="thiscall", record=False,
                 fn=lambda e, ecx, x, op=op: calls.append(("snd", op, x[0], sint(x[1]) if len(x) > 1 else 0)))
    hooks = {0x417070: (1, 0), 0x414FE0: (2, 0), 0x40DC90: (3, 10), 0x40DD60: (4, 10), 0x41C510: (5, 1), 0x41C540: (6, 0)}
    for a, (op, nargs) in hooks.items():
        emu.stub(a, nargs, conv="thiscall", record=False,
                 fn=lambda e, ecx, x, op=op: calls.append(
                     ("hook", op, sint(x[0]) if op == 5 else 0, flt(x[0]) if op in (3, 4) else 0.0)))
    emu.stub(0x418A10, 0, conv="thiscall", record=False)  # Save_Write
    emu.stub(0x43F122, 2, conv="cdecl", record=False)     # fopen in Replay_Save: no file
    snd_log = (ctypes.c_int * 3 * 64).in_dll(port.lib, "test_sound_log")
    snd_n = ctypes.c_int.in_dll(port.lib, "test_sound_count")
    hook_log = (ctypes.c_int * 2 * 256).in_dll(port.lib, "test_hook_log")
    hook_arg = (ctypes.c_float * 256).in_dll(port.lib, "test_hook_arg")
    hook_n = ctypes.c_int.in_dll(port.lib, "test_hook_count")
    race_ptr = ctypes.c_void_p(port.addr("g_race"))
    fails = ticks = 0
    for run in range(args.runs):
        stage, course = rng.randint(1, 6), rng.randint(1, 6)
        track, pick = load_course(stage, course)
        emu.write(TRACK, track)
        emu.write(PICKUPS, pick)
        port.write("g_track", track)
        port.write("g_pickups", pick)
        emu.write(MACHINES, b"".join(make_machine(emu, rng, track) for _ in range(8)))
        emu.call(0x401180, ecx=CAMERA)
        m0 = emu.read(MACHINES, MACHINE_SIZE)  # replay shot chosen at the start, as Race_Update does
        emu.call(0x4032F0, args=list(struct.unpack_from("<9I", m0, 4)) + [struct.unpack_from("<H", m0, 0xAE)[0], 0, 99],
                 ecx=CAMERA)
        race = bytearray(0x58)
        struct.pack_into("<h", race, 0, 10)
        struct.pack_into("<h", race, 0x34, rng.randrange(8))
        struct.pack_into("<d", race, 0x38, rng.choice(TIMES) if rng.random() < 0.7 else rng.uniform(0, 1600))
        struct.pack_into("<hhhhi", race, 0x40, rng.randrange(2), 0, 0, 0, rng.randrange(-1, 16))
        struct.pack_into("<hh", race, 0x48, rng.randrange(2), rng.randrange(2))
        emu.write(RACE, bytes(race))
        emu.write(0x451F68, struct.pack("<i", rng.choice(PHASES)))
        emu.write(0x451F70, struct.pack("<d", rng.choice(TIMES) if rng.random() < 0.7 else rng.uniform(0, 400)))
        emu.write(0x44A228, struct.pack("<i", 500))
        save = bytearray(0x2E4)
        struct.pack_into("<hhhhh", save, 0, rng.randrange(3), rng.randrange(8), stage - 1, course - 1, rng.randrange(-1, 4))
        struct.pack_into("<f", save, 0xC, rng.uniform(0, 100))
        struct.pack_into("<6f", save, 0x10, *(rng.uniform(30, 200) for _ in range(6)))
        struct.pack_into("<36d", save, 0x30, *(rng.uniform(30, 200) for _ in range(36)))
        struct.pack_into("<36d", save, 0x150, *(rng.uniform(10, 60) for _ in range(36)))
        struct.pack_into("<6h", save, 0x270, *(rng.randrange(6) for _ in range(6)))
        struct.pack_into("<h", save, 0x2E0, rng.randrange(2))
        emu.write(0x451438, bytes(save))
        rep = bytearray(0x4E38)
        cnt = rng.randrange(0, 200)
        struct.pack_into("<ii", rep, 0, rng.randrange(0, cnt + 2), cnt)
        struct.pack_into(f"<{cnt}h", rep, 8, *(rng.randrange(8) for _ in range(cnt)))
        emu.write(0x4531D0, bytes(rep))
        emu.call(0x41CE60, ecx=0x453150)
        for name, a, n, sym in REGIONS[9:13]:
            emu.write(a, bytes(n))
        emu.write(SEED, struct.pack("<I", rng.getrandbits(32)))
        emu.call(0x41E0E0, ecx=0x458008)
        emu.call(0x41E280, args=[struct.unpack_from("<h", track, 0)[0]], ecx=0x458008)
        for name, a, n, sym in REGIONS:
            port.write(sym, emu.read(a, n))
        keys = [0] * 14
        for tick in range(args.ticks):
            prev = keys
            keys = [0] * 14
            for k in (2, 3, 4, 5):
                keys[k] = int(rng.random() < (0.5 if k == 4 else 0.15))
            if rng.random() < 0.5:
                keys = list(prev)
            for w, base in ((emu.write, None), (port.write, None)):
                pass
            emu.write(INPUT - 0x38, struct.pack("<14i", *prev))
            emu.write(INPUT, struct.pack("<14i", *keys))
            port.write("g_inputPrev", struct.pack("<14i", *prev))
            port.write("g_input", struct.pack("<14i", *keys))
            calls.clear()
            snd_n.value = 0
            hook_n.value = 0
            phase_before = struct.unpack("<i", emu.read(0x451F68, 4))[0]
            emu.call(0x411530, ecx=RACE)
            port.lib.Race_Update(race_ptr)
            ticks += 1
            mine = []
            # interleave by type: compare each stream separately (both are in call order)
            emu_snd = [c[1:] for c in calls if c[0] == "snd"]
            emu_hook = [c[1:] for c in calls if c[0] == "hook"]
            port_snd = [tuple(snd_log[i]) for i in range(snd_n.value)]
            port_hook = [(hook_log[i][0], hook_log[i][1], hook_arg[i]) for i in range(hook_n.value)]
            bad = []
            if emu_snd != port_snd:
                bad.append(f"sounds {emu_snd} / {port_snd}")
            if [(o, a, round(f, 3)) for o, a, f in emu_hook] != [(o, a, round(f, 3)) for o, a, f in port_hook] or \
                    [struct.pack("<f", f) for o, a, f in emu_hook] != [struct.pack("<f", f) for o, a, f in port_hook]:
                bad.append(f"hooks {emu_hook} / {port_hook}")
            for name, a, n, sym in REGIONS:
                x, y = mask(name, emu.read(a, n)), mask(name, port.read(sym, n))
                if x != y:
                    offs = [o for o in range(0, n, 2) if x[o:o + 2] != y[o:o + 2]]
                    o = offs[0] & ~3
                    bad.append(f"{name} +{o:#x} ({len(offs)} halfwords) {x[o:o + 4].hex()}/{y[o:o + 4].hex()}")
            if bad:
                print(f"run {run} tick {tick} phase {phase_before} sub {struct.unpack_from('<h', emu.read(RACE, 0x58), 0x34)[0]}"
                      f" mode {struct.unpack('<h', emu.read(0x451438, 2))[0]}: " + "; ".join(bad))
                fails += 1
                if args.verbose:
                    for nm, a, n, sym in REGIONS:
                        if nm == "camera":
                            x, y = emu.read(a, n), port.read(sym, n)
                            print("emu  shot", struct.unpack_from("<hhff", x, 0xd8), struct.unpack_from("<9f", x, 0x150), struct.unpack_from("<4f", x, 0x174))
                            print("port shot", struct.unpack_from("<hhff", y, 0xd8), struct.unpack_from("<9f", y, 0x150), struct.unpack_from("<4f", y, 0x174))
                            print("emu  shot_a", x[0xe4:0xe4+36].hex(), "\nport shot_a", y[0xe4:0xe4+36].hex())
                break
            if struct.unpack("<i", emu.read(0x44A228, 4))[0] != 500:
                break
        if fails >= 15:
            break
    print(f"race update: {ticks} ticks, {fails} failing runs")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
