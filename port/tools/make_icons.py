#!/usr/bin/env python3
"""Make the PSP menu images from the game's own bitmaps: ICON0.PNG (144x80) and
PIC1.PNG (480x272), the AceSpeeder! logo of ace.bmp over the sunset sky.
Usage: make_icons.py <game dir> <output dir>"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

game, out = Path(sys.argv[1]), Path(sys.argv[2])
# sky1.bmp is stored upside down like the other mesh textures
sky = Image.open(game / "sky1.bmp").convert("RGB").transpose(Image.FLIP_TOP_BOTTOM)
logo = Image.open(game / "ace.bmp").convert("RGB").crop((0, 0, 256, 96))
# opaque wherever any channel is lit: the letters are dark blue inside
r, g, b = logo.split()
mask = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v > 30 else 0)
bbox = mask.getbbox()
logo, mask = logo.crop(bbox), mask.crop(bbox)


def compose(w, h, logo_w):
    bg = sky.resize((w, w * sky.height // sky.width)).crop((0, 0, w, h))
    shade = Image.new("RGB", (w, h), (0, 0, 0))
    bg = Image.blend(bg, shade, 0.35)
    lh = logo.height * logo_w // logo.width
    lg = logo.resize((logo_w, lh), Image.LANCZOS)
    m = mask.resize((logo_w, lh), Image.LANCZOS)
    glow = m.filter(ImageFilter.GaussianBlur(max(2, logo_w // 60)))
    x, y = (w - logo_w) // 2, (h - lh) // 2
    bg.paste(Image.new("RGB", (logo_w, lh), (0, 0, 0)), (x, y), glow)
    bg.paste(lg, (x, y), m)
    return bg


compose(144, 80, 136).save(out / "ICON0.PNG")
compose(480, 272, 400).save(out / "PIC1.PNG")
print("wrote", out / "ICON0.PNG", out / "PIC1.PNG")
