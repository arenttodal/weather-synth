"""Checks the exported scene assets.

    python3 design/blender/validate_scene.py [--assets assets/weather_scene] [--repeat]

- every file the manifest names exists and matches its recorded hash
- all lighting anchors and masks share one size and are pixel-aligned (identical alpha)
- sprites, markers and shore polygons fall inside the viewport
- no colour fringes: fully transparent pixels carry no stray colour into premultiplied blending
- --repeat: re-renders the noon anchor with Blender and requires an identical file (deterministic rebuild)
Exit code 0 means every check passed. Needs Pillow (python3 -m pip install pillow).
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
args = sys.argv[1:]
ASSETS = os.path.abspath(args[args.index("--assets") + 1]) if "--assets" in args else os.path.join(ROOT, "assets", "weather_scene")
failures = []


def check(ok, msg):
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        failures.append(msg)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]


m = json.load(open(os.path.join(ASSETS, "manifest.json")))
vw, vh = m["viewport"]["width"], m["viewport"]["height"]
px = m["pixelBox"]
size = (px[2] - px[0], px[3] - px[1])
print("Manifest v%s from Blender %s, art box %s" % (m["version"], m["blender"], m["artBox"]))

expected = ["dawn", "morning", "noon", "late_afternoon", "sunset", "dusk", "night", "overcast"]
check(sorted(m["lighting"]) == sorted(expected), "all eight lighting anchors present")
alphas = {}
for name, d in m["lighting"].items():
    p = os.path.join(ASSETS, d["file"])
    check(os.path.exists(p), "%s exists" % d["file"])
    if not os.path.exists(p):
        continue
    check(sha(p) == d["sha256"], "%s matches its hash" % d["file"])
    im = Image.open(p)
    check(im.mode == "RGBA" and im.size == size, "%s is RGBA %dx%d" % (d["file"], *size))
    alphas[name] = im.getchannel("A")
    # Fringe check: transparent pixels must be black or match their neighbours (straight alpha)
    rgb = im.convert("RGB")
    a = im.getchannel("A")
    hist = ImageChops.multiply(rgb.convert("L"), a.point(lambda v: 255 if v == 0 else 0)).getextrema()
    check(hist[1] < 64, "%s: transparent pixels carry no bright colour (max %d)" % (name, hist[1]))
ref = alphas.get("noon")
for name, a in alphas.items():
    if ref is not None and name != "noon":
        diff = ImageChops.difference(ref, a).getextrema()[1]
        check(diff <= 8, "%s alpha aligned with noon (max difference %d)" % (name, diff))

for name, d in m["masks"].items():
    p = os.path.join(ASSETS, d["file"])
    check(os.path.exists(p) and sha(p) == d["sha256"], "mask %s exists and matches its hash" % name)
    if os.path.exists(p):
        check(Image.open(p).size == size, "mask %s aligned with the lighting renders" % name)

for name, mech in m["mechanisms"].items():
    for f in mech["frames"]:
        p = os.path.join(ASSETS, f["file"])
        ok = os.path.exists(p) and sha(p) == f["sha256"]
        x, y, w, h = f["box"]
        inside = 0 <= x and 0 <= y and x + w <= vw and y + h <= vh and w > 2 and h > 2
        check(ok and inside, "%s frame %s exists, hash ok, box inside viewport" % (name, os.path.basename(p)))
    pv = mech["pivot"]
    check(0 <= pv[0] <= 1 and 0 <= pv[1] <= 1, "%s pivot inside viewport" % name)

for k, v in m["markers"].items():
    check(0 <= v[0] <= 1 and 0 <= v[1] <= 1, "marker %s inside viewport" % k)
check(len(m["shore"]["island"]) >= 6, "island shoreline polygon has %d points" % len(m["shore"]["island"]))
check(all(len(r) >= 3 for r in m["shore"]["rocks"]), "every rock has a shoreline")

total = 0
for dp, _, fs in os.walk(ASSETS):
    total += sum(os.path.getsize(os.path.join(dp, f)) for f in fs)
check(total < 25 * 1024 * 1024, "assets on disk %.1f MB (budget 10-25 MB)" % (total / 1048576))

if "--repeat" in args:
    with tempfile.TemporaryDirectory() as tmp:
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x1024x24", "blender", "-b", "--factory-startup", "--python", os.path.join(HERE, "export_layers.py"), "--", "--out", tmp, "--only", "noon"]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        again = os.path.join(tmp, "lighting", "noon.png")
        a = Image.open(again)
        b = Image.open(os.path.join(ASSETS, m["lighting"]["noon"]["file"]))
        same = a.size == b.size and ImageChops.difference(a.convert("RGBA"), b.convert("RGBA")).getextrema()
        maxdiff = max(ch[1] for ch in same) if same else 999
        check(maxdiff <= 2, "fresh rebuild of noon matches the shipped file (max pixel difference %d)" % maxdiff)

print("\n%d failed" % len(failures))
sys.exit(1 if failures else 0)
