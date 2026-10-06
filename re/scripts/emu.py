#!/usr/bin/env python3
"""Run pieces of AceSpeeder.exe under the Unicorn x86 emulator.

Used to extract data that the original computes in code (the 36 course tables),
bit-exactly, without hand-transcribing the decompilation.

    from emu import Emu
    e = Emu()
    e.call(0x424b10)                 # cdecl, no args
    e.read_f32(0x44c234, 10)
"""
import struct
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_MODE_32, Uc
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP,
                               UC_X86_REG_ESP, UC_X86_REG_FPCW)

EXE = Path(__file__).resolve().parent.parent.parent / "game" / "AceSpeeder.exe"
STACK_TOP = 0x00200000
STACK_SIZE = 0x00100000
RET_MAGIC = 0x00FFF000
SCRATCH = 0x00300000  # 64 KB free for callers (output buffers etc.)


class EmuError(Exception):
    pass


class Emu:
    def __init__(self, exe=EXE):
        data = exe.read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        optsz = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 52)[0]
        image_size = struct.unpack_from("<I", data, pe + 80)[0]
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(self.base, (image_size + 0xFFF) & ~0xFFF)
        self.uc.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        self.uc.mem_map(RET_MAGIC & ~0xFFF, 0x1000)
        self.uc.mem_map(SCRATCH, 0x10000)
        self.sections = []
        sec = pe + 24 + optsz
        for i in range(nsec):
            name = data[sec + i * 40: sec + i * 40 + 8].rstrip(b"\0").decode()
            vsz, va, rsz, roff = struct.unpack_from("<IIII", data, sec + i * 40 + 8)
            self.uc.mem_write(self.base + va, data[roff:roff + min(rsz, vsz or rsz)])
            self.sections.append((name, self.base + va, max(vsz, rsz)))
        self.stubs = {}
        self.calls = []  # (addr, args) recorded by stubs
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_bad_mem)
        self._pending_stub = None

    # --- memory helpers
    def section(self, name):
        for n, va, size in self.sections:
            if n == name:
                return va, size
        raise KeyError(name)

    def read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def write(self, addr, b):
        self.uc.mem_write(addr, b)

    def read_cstr(self, addr, maxlen=256):
        b = self.read(addr, maxlen)
        return b.split(b"\0", 1)[0].decode("latin-1")

    def read_f32(self, addr, n=1):
        v = struct.unpack(f"<{n}f", self.read(addr, 4 * n))
        return v if n > 1 else v[0]

    def read_i16(self, addr, n=1):
        v = struct.unpack(f"<{n}h", self.read(addr, 2 * n))
        return v if n > 1 else v[0]

    def read_u32(self, addr, n=1):
        v = struct.unpack(f"<{n}I", self.read(addr, 4 * n))
        return v if n > 1 else v[0]

    # --- stubs
    def stub(self, addr, nargs, ret=0, conv="cdecl", record=True, fn=None):
        """Replace the function at addr. conv: cdecl (caller pops) or stdcall/thiscall (callee pops).
        fn(emu, args) may compute a return value."""
        self.stubs[addr] = (nargs, ret, conv, record, fn)
        self.uc.hook_add(UC_HOOK_CODE, self._on_code, begin=addr, end=addr)

    def _on_code(self, uc, addr, size, user):
        if addr in self.stubs:
            nargs, ret, conv, record, fn = self.stubs[addr]
            esp = uc.reg_read(UC_X86_REG_ESP)
            retaddr = struct.unpack("<I", uc.mem_read(esp, 4))[0]
            args = list(struct.unpack(f"<{nargs}I", uc.mem_read(esp + 4, 4 * nargs))) if nargs else []
            ecx = uc.reg_read(UC_X86_REG_ECX)
            if record:
                self.calls.append((addr, ecx, args))
            if fn:
                ret = fn(self, ecx, args)
            uc.reg_write(UC_X86_REG_EAX, (ret or 0) & 0xFFFFFFFF)
            pop = 4 + (4 * nargs if conv in ("stdcall", "thiscall") else 0)
            uc.reg_write(UC_X86_REG_ESP, esp + pop)
            uc.reg_write(UC_X86_REG_EIP, retaddr)

    def _on_bad_mem(self, uc, access, addr, size, value, user):
        eip = uc.reg_read(UC_X86_REG_EIP)
        raise EmuError(f"invalid memory access at {addr:#x} (eip {eip:#x})")

    # --- calls
    # x87 control word used for calls: 0x27F is the Win32 default (53-bit), but once
    # Direct3D has created its device the game runs with 0x07F (24-bit precision).
    fpcw = 0x27F

    def call(self, addr, args=(), ecx=None, edx=None, max_insns=50_000_000, stop=None):
        """Call a function; args are 32-bit ints or Python floats (passed as float32).
        With stop, emulation ends when that instruction is reached (partial run)."""
        esp = STACK_TOP - 0x1000
        words = []
        for a in args:
            if isinstance(a, float):
                words.append(struct.unpack("<I", struct.pack("<f", a))[0])
            else:
                words.append(a & 0xFFFFFFFF)
        frame = struct.pack(f"<I{len(words)}I", RET_MAGIC, *words)
        esp -= len(frame)
        self.write(esp, frame)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_FPCW, self.fpcw)
        if ecx is not None:
            self.uc.reg_write(UC_X86_REG_ECX, ecx)
        if edx is not None:
            self.uc.reg_write(UC_X86_REG_EDX, edx)
        hook = None
        if stop is not None:
            hook = self.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, ud: uc.emu_stop(), begin=stop, end=stop)
        try:
            self.uc.emu_start(addr, RET_MAGIC, count=max_insns)
        finally:
            if hook is not None:
                self.uc.hook_del(hook)
        return self.uc.reg_read(UC_X86_REG_EAX)

    # --- diffing
    def snapshot(self, name=".data"):
        va, size = self.section(name)
        return va, self.read(va, size)

    def diff(self, snap, min_gap=16):
        """Changed byte ranges since snapshot, merged when closer than min_gap."""
        va, before = snap
        after = self.read(va, len(before))
        ranges = []
        i = 0
        while i < len(before):
            if before[i] != after[i]:
                j = i
                while j < len(before) and (before[j] != after[j] or any(
                        before[k] != after[k] for k in range(j, min(j + min_gap, len(before))))):
                    j += 1
                ranges.append((va + i, j - i))
                i = j
            else:
                i += 1
        return ranges
