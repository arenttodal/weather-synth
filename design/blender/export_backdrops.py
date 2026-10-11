"""Renders the sky's clouds and the regional backdrops for every lighting anchor.

    xvfb-run -a blender -b --factory-startup --python design/blender/export_backdrops.py -- [--parts clouds,city] [--anchors dusk,noon] [--fast]

Same art direction as the craft (export_layers.ANCHORS), so the whole picture shares one light.
Outputs in assets/weather_scene/backdrops/ (file names are unique: the plugin indexes its art by
file name):
  cloud_<k>_<anchor>.png       faceted cumulus, lit, transparent
  city_far_<anchor>.png        distant mountains and skyline (the runtime hazes it with the sky)
  city_near_<anchor>.png       towers with lit windows, a cable-stayed bridge, the near blocks
  backdrops.json               files, boxes in scene coordinates, kinds
Every (part, anchor) renders in its own Blender process, as export_layers does, so files are
repeatable. Geometry is deterministic (fixed seeds).
"""
import bpy
import bmesh
import json
import math
import os
import random
import subprocess
import sys
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_scene as B  # noqa: E402
import export_layers as E  # noqa: E402

ARGV = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []


def opt(name, default=None):
    return ARGV[ARGV.index(name) + 1] if name in ARGV else default


OUT = os.path.abspath(opt("--out", os.path.join(HERE, "..", "..", "assets", "weather_scene", "backdrops")))
PARTS = [p for p in opt("--parts", "clouds,city").split(",") if p]
ANCHOR_LIST = [a for a in opt("--anchors", ",".join(E.ANCHORS)).split(",") if a]
JOB = opt("--job")  # "part:anchor" inside a worker process
FAST = "--fast" in ARGV

VP = B.CFG["viewport"]
SW, SH = VP["logicalWidth"], VP["logicalHeight"]
HZ = VP["horizonY"] * SH          # horizon, logical px from the top
PPU = SW / 100.0                  # backdrop world: 100 units across the scene

B.CFG["palette"].update({
    "cloud": "#6D84A8", "towerA": "#434C68", "towerB": "#3A4360", "towerC": "#6E7590", "towerD": "#48516C",
    "blockNear": "#262C3E", "blockNear2": "#2C3245", "mountain": "#857CA3", "mountain2": "#9B90B4",
    "window": "#F4B65A", "windowDark": "#2A2F3E", "bridge": "#5E6480", "roof": "#3A3F50",
})
B.CFG["camera"]["yawDeg"] = 0  # backdrops face the camera square-on; anchors stay relative to it

ROOT = "WS_BACKDROP"


def collection():
    c = bpy.data.collections.get(ROOT) or bpy.data.collections.new(ROOT)
    if c.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(c)
    return c


def isolate(objs):
    keep = set(objs)
    for ob in bpy.context.scene.objects:
        ob.hide_render = ob not in keep and ob.name != "WS_Sun"  # the factory scene's own light stays off


def camera(ortho, centre_z, res_x, res_y, centre_x=0.0):
    data = bpy.data.cameras.get("WS_BackCam") or bpy.data.cameras.new("WS_BackCam")
    data.type = "ORTHO"
    data.ortho_scale = ortho
    data.clip_start = 1
    data.clip_end = 600
    cam = bpy.data.objects.get("WS_BackCam") or bpy.data.objects.new("WS_BackCam", data)
    if cam.name not in collection().objects:
        collection().objects.link(cam)
    cam.location = (centre_x, -250, centre_z)
    cam.rotation_euler = (math.radians(90), 0, 0)
    sc = bpy.context.scene
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = res_x, res_y
    sc.render.resolution_percentage = 100
    return cam


def mesh(name, bm, mats):
    return B.mesh_object(name, bm, collection(), [B.mat(m) for m in mats])


# ------------------------------------------------------------------ clouds
CLOUDS = [
    # kind, seed, lobes, spread, radius range, flatten
    ("cumulus", 11, 6, 3.2, (0.9, 1.7), 1.0),
    ("cumulus", 12, 5, 2.6, (0.8, 1.5), 1.0),
    ("cumulus", 13, 7, 3.8, (0.8, 1.6), 1.0),
    ("cumulus", 14, 4, 2.0, (0.9, 1.4), 1.0),
    ("cumulus", 15, 6, 3.4, (0.7, 1.3), 0.85),
    ("cumulus", 16, 8, 4.4, (0.8, 1.7), 0.9),
]


def build_cloud(k):
    kind, seed, n, spread, (r0, r1), squash = CLOUDS[k]
    rng = random.Random(seed)
    bm = bmesh.new()
    for i in range(n):
        t = (i / (n - 1)) * 2 - 1 if n > 1 else 0
        r = r0 + (r1 - r0) * (1 - abs(t)) * (0.6 + 0.4 * rng.random())
        for row in ((0, 1.0), (1, 0.75)) if (kind == "cumulus" and abs(t) < 0.6) else ((0, 1.0),):
            rr = r * row[1]
            res = bmesh.ops.create_icosphere(bm, subdivisions=1, radius=rr)  # 80 faces: big flat facets
            vs = res["verts"]
            rot = Matrix.Rotation(rng.random() * 6.28, 3, "Z") @ Matrix.Rotation(rng.random() * 6.28, 3, "X")
            bmesh.ops.rotate(bm, verts=vs, cent=(0, 0, 0), matrix=rot)
            cx = t * spread * 0.8 + (rng.random() - 0.5) * 0.4
            cy = (rng.random() - 0.5) * 0.6
            cz = (r * 0.35 + row[0] * r * 0.85) * squash
            for v in vs:
                v.co.x *= 1.35  # wide lobes merge into one mass
                v.co.z *= squash
                v.co += Vector((cx, cy, cz))
                v.co.z = max(v.co.z, 0.0)  # flat base
    for f in bm.faces:
        f.material_index = 0
    return mesh("Cloud%d" % k, bm, ["cloud"])


def render_cloud(k, anchor):
    ob = build_cloud(k)
    isolate([ob])
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    w = max(v.x for v in bb) - min(v.x for v in bb)
    h = max(v.z for v in bb) - min(v.z for v in bb)
    ortho = w * 1.12
    res_x = 560
    res_y = int(res_x * (h * 1.3) / ortho) + 8
    camera(ortho, min(v.z for v in bb) + h * 0.55, res_x, res_y, (max(v.x for v in bb) + min(v.x for v in bb)) / 2)
    E.apply_anchor(E.ANCHORS[anchor])
    name = "cloud_%d_%s.png" % (k, anchor)
    path = os.path.join(OUT, name)
    E.render(path)
    box = E.alpha_box(path)
    E.crop(path, box)
    return {"cloud": k, "kind": CLOUDS[k][0], "anchor": anchor, "file": "backdrops/" + name, "aspect": (box[2] - box[0]) / max(1, box[3] - box[1])}


# ------------------------------------------------------------------ city
def faceted_ridge(name, rng, y, z_base, hmin, hmax, step, mats, x0=-56, x1=56):
    """A mountain band as a strip of sloped facets (a ridge line pulled back in depth)."""
    bm = bmesh.new()
    xs = []
    x = x0
    while x < x1:
        xs.append(x)
        x += step * (0.6 + 0.8 * rng.random())
    xs.append(x1)
    top = [bm.verts.new((x, y + 3, z_base + hmin + (hmax - hmin) * rng.random() ** 0.8)) for x in xs]
    base = [bm.verts.new((x, y - 1, z_base)) for x in xs]
    mid = [bm.verts.new(((a.co.x + b.co.x) / 2, y + 1, z_base + (min(a.co.z, b.co.z) - z_base) * 0.45)) for a, b in zip(top[:-1], top[1:])]
    for i in range(len(xs) - 1):
        for tri, mi in (((base[i], mid[i], top[i]), 0), ((top[i], mid[i], top[i + 1]), 1), ((mid[i], base[i + 1], top[i + 1]), 0), ((base[i], base[i + 1], mid[i]), 1)):
            f = bm.faces.new(tri)
            f.material_index = mi if rng.random() < 0.8 else 1 - mi
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:  # face the camera
        if f.normal.y > 0:
            f.normal_flip()
    return mesh(name, bm, mats)


def windows(bm_lit, bm_dark, rng, x0, x1, zb, zt, yf, density, cell=(0.9, 1.15), size=(0.36, 0.5)):
    cw, chh = cell
    nx = max(1, int((x1 - x0 - 0.4) / cw))
    nz = max(1, int((zt - zb - 0.6) / chh))
    ox = x0 + (x1 - x0 - nx * cw) / 2 + cw / 2
    for i in range(nx):
        for j in range(nz):
            x, z = ox + i * cw, zb + 0.5 + j * chh
            if rng.random() < density:  # unlit windows are left out: only lit ones read, as in the hero
                B.box(bm_lit, x, yf - 0.03, z, size[0], 0.04, size[1])


def tower(bm_body, bm_lit, bm_dark, bm_roof, rng, x, y, w, d, zb, h, mat_index, density, setbacks=True):
    """A box tower, sometimes stepped back near the top, with an antenna or roof box."""
    z = zb
    tiers = [(w, h)] if not setbacks or rng.random() < 0.45 else [(w, h * 0.72), (w * 0.72, h * 0.18), (w * 0.45, h * 0.1)]
    for tw, th in tiers:
        B.box(bm_body, x, y, z, tw, d, th, mat_index)
        windows(bm_lit, bm_dark, rng, x - tw / 2, x + tw / 2, z, z + th, y - d / 2, density)
        z += th
    if rng.random() < 0.5:
        B.box(bm_roof, x, y, z, w * 0.3, d * 0.3, 0.35)
    if rng.random() < 0.35:
        B.cylinder(bm_roof, x, y, z + 1.0, 0.05, 2.0, segs=4)
    return z


def build_city(layer):
    rng = random.Random(777)
    objs = []
    dz = -3.6  # the hero's city sits low: its ground line a little under the scene horizon
    if layer == "far":
        objs.append(faceted_ridge("CityMountainsFar", rng, 120, dz - 0.5, 1.2, 3.2, 8.0, ["mountain2", "mountain"]))
        objs.append(faceted_ridge("CityMountains", rng, 100, dz - 0.5, 0.6, 2.2, 6.0, ["mountain", "mountain2"]))
        body, lit, dark, roof = bmesh.new(), bmesh.new(), bmesh.new(), bmesh.new()
        x = -52
        while x < 52:
            w = 1.6 + 2.2 * rng.random()
            h = 0.3 + 1.3 * rng.random() ** 1.6
            tower(body, lit, dark, roof, rng, x + w / 2, 60, w, 1.5, dz - 2.5, h + 2.5, rng.randrange(2), 0.04, setbacks=False)
            x += w + (0.4 if rng.random() < 0.3 else 0.0)
        objs += [mesh("CityFarBody", body, ["towerC", "towerD"]), mesh("CityFarLit", lit, ["window"]), mesh("CityFarDark", dark, ["windowDark"]), mesh("CityFarRoof", roof, ["roof"])]
        return objs

    body, lit, dark, roof = bmesh.new(), bmesh.new(), bmesh.new(), bmesh.new()
    # Mid towers: a loose skyline with a few tall ones, gaps around the craft's centre line
    x = -51
    while x < 51:
        tall = rng.random() < 0.34
        w = (1.3 + 1.2 * rng.random()) if tall else (1.6 + 2.0 * rng.random())
        h = (3.4 + 2.6 * rng.random()) if tall else (0.6 + 1.6 * rng.random())
        if not (14 < x < 36 and h > 1.8):  # keep the bridge readable
            tower(body, lit, dark, roof, rng, x + w / 2, 30, w, 2.2, dz - 6, h + 6, rng.randrange(3), 0.06)
        x += w + 0.8 + 2.5 * rng.random()
    # Near blocks: a dense low band filling the bottom of the scene
    x = -53
    while x < 53:
        w = 2.0 + 3.5 * rng.random()
        h = 0.3 + 1.2 * rng.random()
        tower(body, lit, dark, roof, rng, x + w / 2, 8, w, 2.0, -11, h + 11 - 7.2, 3 + rng.randrange(2), 0.05, setbacks=False)
        x += w + 0.2
    objs += [mesh("CityBody", body, ["towerA", "towerB", "towerD", "blockNear", "blockNear2"]), mesh("CityLit", lit, ["window"]), mesh("CityDark", dark, ["windowDark"]), mesh("CityRoof", roof, ["roof"])]

    # A cable-stayed bridge on the right: two A-shaped pylons, a deck, fans of cables
    bb = bmesh.new()
    deck_z, yb = dz + 0.5, 22
    B.box(bb, 26, yb, deck_z, 26, 0.6, 0.25)
    for px in (21, 31):
        for s in (-1, 1):
            v = B.cylinder(bb, 0, 0, 0, 0.1, 3.4, segs=4)
            bmesh.ops.rotate(bb, verts=v, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(6 * s), 3, "Y"))
            bmesh.ops.translate(bb, vec=(px - s * 0.2, yb, deck_z + 1.7), verts=v)
        for k in range(1, 6):
            for s in (-1, 1):
                a = Vector((px, yb, deck_z + 3.2))
                b = Vector((px + s * k * 0.75, yb, deck_z + 0.15))
                d = b - a
                v = B.cylinder(bb, 0, 0, 0, 0.03, d.length, segs=3)
                bmesh.ops.rotate(bb, verts=v, cent=(0, 0, 0), matrix=d.to_track_quat("Z", "Y").to_matrix())
                bmesh.ops.translate(bb, vec=(a + b) / 2, verts=v)
    objs.append(mesh("CityBridge", bb, ["bridge"]))
    return objs


def render_city(layer, anchor):
    objs = build_city(layer)
    isolate(objs)
    # The scene viewport, 2x: 100 units across, the horizon (z = 0) at the configured height
    camera(100.0, (HZ - SH / 2) / PPU, SW * 2, SH * 2)
    a = E.ANCHORS[anchor]
    E.apply_anchor(a)
    lit = bpy.data.materials.get("WS_window")
    if lit:  # windows light up as the day goes
        bsdf = lit.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Emission Color" if "Emission Color" in bsdf.inputs else "Emission"].default_value = B.hex_rgba(B.CFG["palette"]["window"])
        bsdf.inputs["Emission Strength"].default_value = max(0.0, a["lamps"] - 1.2) * 1.8
    name = "city_%s_%s.png" % (layer, anchor)
    path = os.path.join(OUT, name)
    E.render(path)
    box = E.alpha_box(path)
    E.crop(path, box)
    return {"family": "city", "layer": layer, "anchor": anchor, "file": "backdrops/" + name, "box": [box[0] / 2, box[1] / 2, (box[2] - box[0]) / 2, (box[3] - box[1]) / 2]}


# ------------------------------------------------------------------ jobs
def worker(job):
    part, anchor = job.split(":")
    E.setup_render()
    if B.ROOT not in bpy.data.collections:  # export_layers keeps its sun in the craft's collection
        bpy.context.scene.collection.children.link(bpy.data.collections.new(B.ROOT))
    collection()
    os.makedirs(OUT, exist_ok=True)
    out = []
    if part == "clouds":
        for k in range(len(CLOUDS)):
            out.append(render_cloud(k, anchor))
            for ob in list(collection().objects):
                if ob.name.startswith("WS_Cloud"):
                    bpy.data.objects.remove(ob, do_unlink=True)
    elif part.startswith("city_"):
        out.append(render_city(part[5:], anchor))
    json.dump(out, open(os.path.join(OUT, "_job_%s_%s.json" % (part, anchor)), "w"))
    print("WS_BACKDROP_OK", job)


def orchestrate():
    os.makedirs(OUT, exist_ok=True)
    exe = bpy.app.binary_path
    jobs = []
    for p in PARTS:
        for a in ANCHOR_LIST:
            if p == "city":
                jobs += ["city_far:" + a, "city_near:" + a]
            else:
                jobs.append(p + ":" + a)
    for j in jobs:
        cmd = [exe, "-b", "--factory-startup", "--python", os.path.abspath(__file__), "--", "--out", OUT, "--job", j] + (["--fast"] if FAST else [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or "WS_BACKDROP_OK" not in r.stdout:
            print(r.stdout[-3000:], r.stderr[-2000:])
            raise SystemExit("backdrop job %s failed" % j)
        print("WS_BACKDROP", j)
    # Merge the job results into the manifest (keeping entries from earlier partial runs)
    mpath = os.path.join(OUT, "..", "backdrops.json")
    man = json.load(open(mpath)) if os.path.exists(mpath) else {}
    man.update({"version": 1, "generator": "design/blender/export_backdrops.py", "viewport": {"width": SW, "height": SH, "horizonY": VP["horizonY"]}})
    clouds = {int(k): v for k, v in man.get("clouds", {}).items()}
    families = man.get("families", {})
    for f in sorted(os.listdir(OUT)):
        if not f.startswith("_job_"):
            continue
        for e in json.load(open(os.path.join(OUT, f))):
            if "cloud" in e:
                c = clouds.setdefault(e["cloud"], {"kind": e["kind"], "frames": {}})
                c["frames"][e["anchor"]] = e["file"]
                c["aspect"] = e["aspect"]
            else:
                fam = families.setdefault(e["family"], {"layers": {}})
                lay = fam["layers"].setdefault(e["layer"], {"frames": {}})
                lay["frames"][e["anchor"]] = e["file"]
                lay["box"] = e["box"]
        os.remove(os.path.join(OUT, f))
    # The city layers are flat facets: a 256-colour palette is visually identical and much smaller
    # (clouds keep full colour; their soft shading speckles when quantized)
    city = [os.path.join(OUT, f) for f in sorted(os.listdir(OUT)) if f.startswith("city_") and f.endswith(".png")]
    if city and subprocess.run(["python3", os.path.join(HERE, "..", "..", "tools", "quantize_png.py")] + city).returncode != 0:
        print("WS_WARN city layers left unquantized (needs python3 with Pillow)")
    man["clouds"] = {str(k): clouds[k] for k in sorted(clouds)}
    man["families"] = families
    json.dump(man, open(mpath, "w"), indent=1)
    print("WS_BACKDROPS_OK", mpath)


if __name__ == "__main__":
    if JOB:
        worker(JOB)
    else:
        orchestrate()
