"""Crops transparent renders to their content and prints each box as JSON (art pixels):
{"file.png": [x, y, w, h], ...}. Used by design/blender/hero_env.py.

    python3 tools/crop_alpha.py file.png [...]
"""
import json
import sys

from PIL import Image

out = {}
for path in sys.argv[1:]:
    im = Image.open(path).convert("RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a > 1 else 0).getbbox()
    if box is None:
        out[path] = None
        continue
    im.crop(box).save(path, optimize=True)
    out[path] = [box[0], box[1], box[2] - box[0], box[3] - box[1]]
print(json.dumps(out))
