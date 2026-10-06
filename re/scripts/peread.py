#!/usr/bin/env python3
"""Read bytes at a virtual address from a PE file: peread.py <exe> <hexva> <len>"""
import struct, sys

def va_to_off(data, va):
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 52)[0]
    sec = pe + 24 + optsz
    for i in range(nsec):
        vsz, vaddr, rsz, roff = struct.unpack_from("<IIII", data, sec + i * 40 + 8)
        rva = va - base
        if vaddr <= rva < vaddr + max(vsz, rsz):
            return roff + rva - vaddr if rva - vaddr < rsz else None
    return None

if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    va, n = int(sys.argv[2], 16), int(sys.argv[3])
    off = va_to_off(data, va)
    if off is None:
        sys.exit("unmapped or uninitialised (bss)")
    sys.stdout.buffer.write(data[off:off + n])
