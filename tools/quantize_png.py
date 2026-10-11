"""Rewrites flat-shaded PNGs as 256-colour palette PNGs (with alpha), losslessly enough for
faceted art and about 20 to 40 times smaller. Used by design/blender/export_backdrops.py.

    python3 tools/quantize_png.py file.png [...]
"""
import os
import sys

from PIL import Image

for path in sys.argv[1:]:
    im = Image.open(path).convert("RGBA")
    q = im.quantize(256, method=Image.FASTOCTREE, dither=Image.NONE)
    tmp = path + ".tmp.png"
    q.save(tmp, optimize=True)
    os.replace(tmp, path)
