#!/usr/bin/env python3
"""Fake Direct3D Retained Mode objects for the emulator (see emu.py).

COM objects live in emulated memory as [vtable ptr, id]; every vtable slot points
into a stub page that Python intercepts. Unhandled methods return S_OK and are
logged. This lets init-time code that *builds* scenes (e.g. Track_BuildMeshes)
run unmodified while we record the geometry it produces.
"""
import json
import struct
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

RE = Path(__file__).resolve().parent.parent
TABLES = json.loads((RE / "vtables.json").read_text())
NARGS = json.loads((RE / "vtable_nargs.json").read_text())

OBJ_BASE = 0x03000000
VTBL_BASE = 0x03800000
STUB_BASE = 0x04000000
IFACES = ["IDirect3DRM2", "IDirect3DRMFrame2", "IDirect3DRMMeshBuilder2", "IDirect3DRMFace",
          "IDirect3DRMTexture2", "IDirect3DRMMaterial", "IDirect3DRMLight", "IDirect3DRMDevice2",
          "IDirect3DRMViewport"]


def f32(u):
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


class Obj:
    def __init__(self, oid, iface, addr):
        self.id = oid
        self.iface = iface
        self.addr = addr
        self.state = {}


class FakeRM:
    def __init__(self, emu):
        self.emu = emu
        uc = emu.uc
        uc.mem_map(OBJ_BASE, 0x400000)
        uc.mem_map(VTBL_BASE, 0x100000)
        uc.mem_map(STUB_BASE, 0x100000)
        uc.mem_write(STUB_BASE, b"\xc3" * 0x100000)
        for i, name in enumerate(IFACES):
            n = len(TABLES[name])
            stubs = [STUB_BASE + i * 0x1000 + k * 4 for k in range(n)]
            uc.mem_write(VTBL_BASE + i * 0x1000, struct.pack(f"<{n}I", *stubs))
        uc.hook_add(UC_HOOK_CODE, self._hook, begin=STUB_BASE, end=STUB_BASE + 0x100000)
        self.objs = {}
        self.next_addr = OBJ_BASE
        self.log = []  # (iface, method, obj id, args) for unhandled calls

    def new(self, iface, **state):
        addr = self.next_addr
        self.next_addr += 16
        oid = len(self.objs) + 1
        o = Obj(oid, iface, addr)
        o.state.update(state)
        self.emu.write(addr, struct.pack("<II", VTBL_BASE + IFACES.index(iface) * 0x1000, oid))
        self.objs[addr] = o
        return o

    def obj(self, addr):
        return self.objs.get(addr)

    def _hook(self, uc, addr, size, user):
        off = addr - STUB_BASE
        iface = IFACES[off // 0x1000]
        idx = (off % 0x1000) // 4
        method = TABLES[iface][idx]
        nargs = NARGS[iface][idx]
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret_addr, *args = struct.unpack(f"<{nargs + 1}I", uc.mem_read(esp, 4 * (nargs + 1)))
        this = self.obj(args[0])
        short = iface.replace("IDirect3DRM", "") or "RM"
        handler = getattr(self, f"{'RM2' if short == '2' else short}_{method}", None)
        if handler:
            try:
                ret = handler(this, *args[1:])
            except Exception as ex:  # unicorn swallows exceptions raised in hooks
                import traceback
                traceback.print_exc()
                uc.emu_stop()
                raise ex
        elif method in ("AddRef", "Release"):
            ret = 0
        else:
            self.log.append((iface, method, this.id if this else None, args[1:]))
            ret = 0
        uc.reg_write(UC_X86_REG_EAX, (ret or 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 * (nargs + 1))
        uc.reg_write(UC_X86_REG_EIP, ret_addr)

    def _out(self, ptr, o):
        self.emu.write(ptr, struct.pack("<I", o.addr))

    # ---- IUnknown
    def _release(self, this, *a):
        return 0

    # ---- RM2
    def RM2_CreateFrame(self, this, parent, out):
        o = self.new("IDirect3DRMFrame2", parent=self.obj(parent), pos=None, orient=None, visuals=[],
                     children=[], lights=[])
        if o.state["parent"]:
            o.state["parent"].state["children"].append(o)
        self._out(out, o)

    def RM2_CreateMeshBuilder(self, this, out):
        self._out(out, self.new("IDirect3DRMMeshBuilder2", verts=[], normals=[], faces=[], quality=None,
                                perspective=None, loads=[], scale=None, color=None, material=None,
                                texture=None))

    def RM2_LoadTexture(self, this, name, out):
        self._out(out, self.new("IDirect3DRMTexture2", name=self.emu.read_cstr(name)))

    def RM2_CreateMaterial(self, this, power, out):
        self._out(out, self.new("IDirect3DRMMaterial", power=f32(power)))

    def RM2_CreateLightRGB(self, this, ltype, r, g, b, out):
        self._out(out, self.new("IDirect3DRMLight", type=ltype, rgb=(f32(r), f32(g), f32(b))))

    # ---- Frame2
    def Frame2_SetPosition(self, this, ref, x, y, z):
        this.state["pos"] = (self.obj(ref).id if self.obj(ref) else None, f32(x), f32(y), f32(z))

    def Frame2_SetOrientation(self, this, ref, dx, dy, dz, ux, uy, uz):
        this.state["orient"] = (self.obj(ref).id if self.obj(ref) else None,
                                *(f32(v) for v in (dx, dy, dz, ux, uy, uz)))

    def Frame2_AddVisual(self, this, vis):
        this.state["visuals"].append(self.obj(vis))

    def Frame2_AddLight(self, this, light):
        this.state["lights"].append(self.obj(light))

    # ---- MeshBuilder2
    def MeshBuilder2_AddVertex(self, this, x, y, z):
        this.state["verts"].append((f32(x), f32(y), f32(z)))
        return len(this.state["verts"]) - 1

    def MeshBuilder2_AddNormal(self, this, x, y, z):
        this.state["normals"].append((f32(x), f32(y), f32(z)))
        return len(this.state["normals"]) - 1

    def MeshBuilder2_CreateFace(self, this, out):
        f = self.new("IDirect3DRMFace", corners=[], uv={}, color=None, texture=None, material=None)
        this.state["faces"].append(f)
        self._out(out, f)

    def MeshBuilder2_SetQuality(self, this, q):
        this.state["quality"] = q

    def MeshBuilder2_SetPerspective(self, this, p):
        this.state["perspective"] = p

    def MeshBuilder2_Load(self, this, name, *rest):
        this.state["loads"].append(self.emu.read_cstr(name))

    def MeshBuilder2_Scale(self, this, sx, sy, sz):
        this.state["scale"] = (f32(sx), f32(sy), f32(sz))

    def MeshBuilder2_SetColor(self, this, c):
        this.state["color"] = c

    def MeshBuilder2_SetMaterial(self, this, m):
        this.state["material"] = self.obj(m)

    def MeshBuilder2_SetTexture(self, this, t):
        this.state["texture"] = self.obj(t)

    # ---- Face
    def Face_AddVertexAndNormalIndexed(self, this, v, n):
        this.state["corners"].append((v, n))

    def Face_SetColorRGB(self, this, r, g, b):
        this.state["color"] = (f32(r), f32(g), f32(b))

    def Face_SetColor(self, this, c):
        this.state["color"] = c

    def Face_SetTexture(self, this, t):
        this.state["texture"] = self.obj(t)

    def Face_SetTextureCoordinates(self, this, i, u, v):
        this.state["uv"][i] = (f32(u), f32(v))

    def Face_SetMaterial(self, this, m):
        this.state["material"] = self.obj(m)
