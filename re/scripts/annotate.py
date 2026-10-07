#!/usr/bin/env python3
"""Annotate COM vtable calls in Ghidra's decompiled C.

Usage: annotate.py <decomp-dir> <out-dir>

`(**(code **)(*X + 0x54))(X, ...)` becomes
`(**(code **)(*X + 0x54))/*Frame2.GetLights|MB2.X|...*/(X, ...)`.
When X is a global named `g_<Iface>_...` (see re/names.txt), only that
interface's method is shown. Otherwise the candidates from the interfaces
this game actually uses are listed.
"""
import json
import re
import struct
import sys
from pathlib import Path

import peread

HERE = Path(__file__).resolve().parent
TABLES = json.loads((HERE.parent / "vtables.json").read_text())
EXE = (HERE.parent.parent / "game" / "AceSpeeder.exe").read_bytes()
CONST = re.compile(r"\b(FLOAT|DOUBLE)_([0-9a-f]{8})\b")


def const_value(m):
    """FLOAT_004474a8 -> 60.0f (the value stored in .rdata)."""
    kind, va = m.group(1), int(m.group(2), 16)
    off = peread.va_to_off(EXE, va)
    if off is None:
        return m.group(0)
    if kind == "FLOAT":
        raw = EXE[off:off + 4]
        v = struct.unpack("<f", raw)[0]
        for digits in range(1, 10):  # shortest text that round-trips as float32
            txt = f"{v:.{digits}g}"
            if struct.pack("<f", float(txt)) == raw:
                break
        if "." not in txt and "e" not in txt and "n" not in txt:
            txt += ".0"
        return txt + "f"
    return repr(struct.unpack_from("<d", EXE, off)[0])

# Short alias -> Wine interface name. These are the interfaces used by an IDirect3DRM2 game.
USED = {
    "RM2": "IDirect3DRM2",
    "Frame2": "IDirect3DRMFrame2",
    "MB2": "IDirect3DRMMeshBuilder2",
    "Mesh": "IDirect3DRMMesh",
    "Tex2": "IDirect3DRMTexture2",
    "Light": "IDirect3DRMLight",
    "View": "IDirect3DRMViewport",
    "Dev2": "IDirect3DRMDevice2",
    "Mat": "IDirect3DRMMaterial",
    "Wrap": "IDirect3DRMWrap",
    "D3D2": "IDirect3D2",
    "DD": "IDirectDraw",
    "DDPal": "IDirectDrawPalette",
    "DDS": "IDirectDrawSurface",
    "DI": "IDirectInputA",
    "DID": "IDirectInputDeviceA",
}

CALL = re.compile(r"\(\*\*?\(code \*\*\)\(\*(?:\([\w ]+\*+\))?\(?([\w\[\]*]+?)\)? \+ (0x[0-9a-f]+|\d+)\)\)")
CALL0 = re.compile(r"\(\*\*?\(code \*\*\)\*(?:\(int \*\))?(\w+)\)")


def method(alias, off):
    m = TABLES[USED[alias]]
    i = off // 4
    return m[i] if i < len(m) else None


# Methods whose last argument receives a new interface pointer: (alias, method) -> alias of result
CREATES = {
    ("RM2", "CreateFrame"): "Frame2",
    ("RM2", "CreateMeshBuilder"): "MB2",
    ("RM2", "CreateMesh"): "Mesh",
    ("RM2", "CreateMaterial"): "Mat",
    ("RM2", "CreateLight"): "Light",
    ("RM2", "CreateLightRGB"): "Light",
    ("RM2", "LoadTexture"): "Tex2",
    ("RM2", "CreateTexture"): "Tex2",
    ("RM2", "CreateTextureFromSurface"): "Tex2",
    ("RM2", "CreateViewport"): "View",
    ("RM2", "CreateWrap"): "Wrap",
    ("RM2", "CreateDeviceFromD3D"): "Dev2",
    ("MB2", "CreateMesh"): "Mesh",
    ("Frame2", "GetParent"): "Frame2",
    ("Frame2", "GetScene"): "Frame2",
    ("DD", "CreateSurface"): "DDS",
    ("DI", "CreateDevice"): "DID",
}


def var_alias(var, known):
    if var in known:
        return known[var]
    for alias in USED:
        if f"g_{alias}_" in var:
            return alias
    return None


def describe(var, off, known):
    alias = var_alias(var, known)
    if alias:
        return f"{alias}.{method(alias, off)}"
    if off < 12:
        return ["QueryInterface", "AddRef", "Release"][off // 4]
    if off < 0x2c:  # IDirect3DRMObject methods are shared by all RM objects
        cands = [f"Obj.{TABLES['IDirect3DRMObject'][off // 4]}"]
        cands += [f"{a}.{method(a, off)}" for a in ("D3D2", "DD", "DDS", "DDPal", "DI", "DID") if method(a, off)]
        return "|".join(cands)
    cands = [f"{a}.{method(a, off)}" for a in USED if method(a, off)]
    return "|".join(cands)


LASTARG = re.compile(r"\(\w+,.*?(?:\(int\))?&?(\w+)\);")


def infer_types(text):
    """Find `x` in `<known>.CreateFrame(..., &x)` style calls; repeat until stable."""
    known = {}
    while True:
        before = len(known)
        for m in CALL.finditer(text):
            alias = var_alias(m.group(1), known)
            if not alias:
                continue
            made = CREATES.get((alias, method(alias, int(m.group(2), 0))))
            if not made:
                continue
            rest = text[m.end():text.find("\n", m.end())]
            a = LASTARG.match(rest)
            if a and a.group(1) not in known:
                known[a.group(1)] = made
        if len(known) == before:
            return known


COPY_FOR = re.compile(
    r"( *)for \((\w+) = (\d+); (?:\w+ = \w+, )?\2 != 0; \2 = \2 \+ -1\) \{\n"
    r" *\*(\w+) = \*(?:\([\w ]+\*\))?(\w+);\n"
    r" *\5 = (?:\([\w ]+\*\))?(?:\5 \+ 1|&\5->\w+);\n"
    r" *\4 = (?:\([\w ]+\*\))?(?:\4 \+ 1|&\4->\w+);\n"
    r" *\}\n")
COPY_WHILE = re.compile(
    r"( *)while\( true \) \{\n *if \((\w+) == 0\) break;\n *\2 = \2 \+ -1;\n"
    r" *\*(\w+) = \*(?:\([\w ]+\*\))?(\w+);\n"
    r" *\4 = (?:\([\w ]+\*\))?(?:\4 \+ 1|&\4->\w+);\n"
    r" *\3 = (?:\([\w ]+\*\))?(?:\3 \+ 1|&\3->\w+);\n"
    r" *\}\n")
# p_00.seg = ...; p_00.t._0_2_ = ...; -- the decompiler rebuilding a by-value TrackPos argument
STRUCT_ARG = re.compile(r"(?:( *)((?:p|m|v|a|b|from|to)(?:_[0-9a-f]+)?)\.[\w.\[\]]+ = [^;]+;\n)+")


LOOP9 = re.compile(r"^( *)for \((\w+) = (9|0x24|36); (?:\w+ = [^,;]+, )?\2 != 0; \2 = \2 \+ -1\) \{$")


ASSIGN = re.compile(r"^ *(\w+) = (.+);$")
STACKREF = re.compile(r"^\(?(?:\([\w ]+\*+\))?&stack0x([0-9a-f]+)\)?$")


def condense_loops(text):
    """Collapse 9-word struct-copy loops. When the copy targets an outgoing stack slot,
    remember what was copied there so struct arguments built from it can be named."""
    lines = text.split("\n")
    out, i = [], 0
    last = {}      # pointer variable -> last assigned expression
    slots = {}     # stack offset (int) -> description of the copied struct
    while i < len(lines):
        a = ASSIGN.match(lines[i])
        if a:
            last[a.group(1)] = a.group(2)
        m = LOOP9.match(lines[i])
        if m:
            j = i + 1
            body = []
            while j < len(lines) and lines[j].strip() != "}" and len(body) < 6:
                body.append(lines[j].strip())
                j += 1
            simple = (2 <= len(body) <= 4 and j < len(lines) and
                      all(re.fullmatch(r"[\w\->\.\[\]\*\(\) ]+ = [\w\->\.\[\]\*\(\)+ ]+;", b) for b in body))
            if simple:
                dstv = body[0].split(" = ")[0].lstrip("*").strip()
                srcv = re.sub(r"->m\[0\]$|\.m\[0\]$", "", body[0].split(" = ")[1].rstrip(";").lstrip("*").strip())
                srcv = re.sub(r"^\*?\([\w ]+\*+\)", "", srcv).lstrip("*")
                src = last.get(srcv, srcv)
                dst = last.get(dstv, dstv)
                st = STACKREF.match(dst.strip())
                if st:
                    slots[int(st.group(1), 16)] = src
                out.append(f"{m.group(1)}/* copy 9 words: {dst} <- {src} */")
                i = j + 1
                continue
        out.append(lines[i])
        i += 1
    return "\n".join(out), slots


INSTACK = re.compile(r"in_stack_([0-9a-f]{8})")


def name_struct_args(text, slots):
    """Replace `m_01.m[0] = in_stack_...;` runs by `/* m_01 = <what was copied there> */`."""
    def repl(m):
        block = m.group(0)
        offs = [int(x, 16) for x in INSTACK.findall(block)]
        lines = block.rstrip("\n").split("\n")
        plain = [l for l in lines if "in_stack_" not in l]
        if offs:
            base = min(offs)
            src = next((slots[o] for o in sorted(slots) if base - 4 <= o <= base), None)
            note = f"{m.group(1)}/* {m.group(2)} = {src if src else 'stack copy'} */"
            return "\n".join([note] + plain) + "\n"
        return block
    return STRUCT_ARG.sub(repl, text)


def condense(text):
    text, slots = condense_loops(text)
    text = name_struct_args(text, slots)
    text = COPY_FOR.sub(lambda m: f"{m.group(1)}/* copy {m.group(3)} words: {m.group(4)} <- {m.group(5)} */\n", text)
    text = COPY_WHILE.sub(lambda m: f"{m.group(1)}/* copy {m.group(2)} words: {m.group(3)} <- {m.group(4)} */\n", text)
    return text


def annotate(text):
    text = condense(text)
    text = CONST.sub(const_value, text)
    known = infer_types(text)
    text = CALL.sub(lambda m: f"{m.group(0)}/*{describe(m.group(1), int(m.group(2), 0), known)}*/", text)
    return CALL0.sub(lambda m: f"{m.group(0)}/*QueryInterface*/", text)


def redact(text):
    """Hide the game's password and the values that record it in the save: the
    author asked that the password is never published. Done by structure, so
    the values themselves appear nowhere in this repository."""
    if "Save_CheckCode(" in text:
        text = re.sub(r"(==\s*)(0x[0-9a-fA-F]+|\d+)", r"\1CODE", text)
        text = re.sub(r"(\+ 0x2d[48c]\) = )(0x[0-9a-fA-F]+|\d+)", r"\1UNLOCK", text)
    if "Save_Load(" in text:
        text = re.sub(r"(\+ 0x16[ace]\) == )(0x[0-9a-fA-F]+|\d+)", r"\1UNLOCK", text)
    return text


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for f in src.glob("*.c"):
        (dst / f.name).write_text(redact(annotate(f.read_text())))
    idx = src / "INDEX.txt"
    if idx.exists():
        (dst / "INDEX.txt").write_text(idx.read_text())


if __name__ == "__main__":
    main()
