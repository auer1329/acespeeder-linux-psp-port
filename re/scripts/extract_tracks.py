#!/usr/bin/env python3
"""Build the 36 courses with the original code under emulation and save them as
track_<stage>_<course>.bin for the port (see port/src/game/trackfile.h).

For each course this runs, exactly like the race setup (FUN_00417070) does:
  course definition function  -> Track control points, profiles, pickups
  FUN_0040b220                -> random scenery bands (fixed seed here)
  Track_BuildMeshes(1)        -> one mesh per segment (captured through fakerm.py)
  FUN_0040b520                -> segment bounding radii
and stores the resulting Track/pickup memory plus the generated geometry.

Usage: extract_tracks.py [outdir]   (default: port/data)
"""
import struct
import sys
from pathlib import Path

from emu import Emu
from extract_courses import COURSE_FUNCS, PICKUPS, PICKUPS_SIZE, TRACK
from fakerm import FakeRM

TRACK_SIZE = 0x1A28
SEG_FRAMES = 0x45172C   # Frame2* per segment
MAGIC = b"ASTR"
VERSION = 2
SEED = 0x12345  # crt_rand seed; the original seeds with timeGetTime()


def build(stage, course):
    e = Emu()
    rm = FakeRM(e)
    root = rm.new("IDirect3DRM2")
    scene = rm.new("IDirect3DRMFrame2", parent=None, pos=None, orient=None, visuals=[], children=[],
                   lights=[])
    e.write(0x44DE74, struct.pack("<I", root.addr))      # g_RM2_main
    e.write(0x451720, struct.pack("<I", scene.addr))     # scene root frame
    e.write(0x45143C, struct.pack("<hh", stage, course))
    e.write(0x44A82C, struct.pack("<I", SEED))
    e.call(COURSE_FUNCS[stage][course])
    e.call(0x40B220, ecx=TRACK)
    e.call(0x408470, args=[1], ecx=TRACK)
    e.call(0x40B520, ecx=TRACK)
    if rm.log:
        raise RuntimeError(f"unhandled RM calls: {rm.log[:5]}")
    n = struct.unpack("<h", e.read(TRACK, 2))[0]
    segs = []
    for i in range(n + 1):
        fr = rm.obj(e.read_u32(SEG_FRAMES + 4 * i))
        ref, px, py, pz = fr.state["pos"]
        assert ref == scene.id, "segment frame not positioned relative to the scene"
        o = fr.state["orient"]
        assert o is not None and o[1:] == (0.0, 0.0, 1.0, 0.0, 1.0, 0.0), f"unexpected orientation {o}"
        (mb,) = fr.state["visuals"]
        segs.append(((px, py, pz), mb.state))
    return e.read(TRACK, TRACK_SIZE), e.read(PICKUPS, PICKUPS_SIZE), segs


def encode(track, pickups, segs):
    out = bytearray(MAGIC + struct.pack("<II", VERSION, len(segs)))
    out += struct.pack("<I", len(track)) + track
    out += struct.pack("<I", len(pickups)) + pickups
    for (px, py, pz), mb in segs:
        faces = mb["faces"]
        out += struct.pack("<fffII", px, py, pz, mb["quality"], len(faces))
        for f in faces:
            st = f.state
            corners = st["corners"]
            color = st["color"]
            if isinstance(color, tuple):
                r, g, b = color
            else:  # packed D3DCOLOR
                r, g, b = ((color >> 16) & 255) / 255, ((color >> 8) & 255) / 255, (color & 255) / 255
            tex = 1 if st["texture"] else 0
            if st["texture"]:
                assert st["texture"].state["name"].startswith("course")
            out += struct.pack("<fffII", r, g, b, tex, len(corners))
            for k, (vi, ni) in enumerate(corners):
                x, y, z = mb["verts"][vi]
                nx, ny, nz = mb["normals"][ni]
                u, v = st["uv"].get(k, (0.0, 0.0))
                out += struct.pack("<8f", x, y, z, nx, ny, nz, u, v)
    return bytes(out)


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent.parent / "port" / "data"
    outdir.mkdir(parents=True, exist_ok=True)
    total = 0
    for s in range(6):
        for c in range(6):
            track, pickups, segs = build(s, c)
            blob = encode(track, pickups, segs)
            (outdir / f"track_{s + 1}_{c + 1}.bin").write_bytes(blob)
            nf = sum(len(mb["faces"]) for _, mb in segs)
            total += len(blob)
            print(f"track_{s + 1}_{c + 1}.bin: {len(segs)} segments, {nf} triangles, {len(blob)} bytes")
    print(f"total {total} bytes in {outdir}")


if __name__ == "__main__":
    main()
