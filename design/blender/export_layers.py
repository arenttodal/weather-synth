"""Renders the shipped scene layers and writes the manifest.

    xvfb-run -a blender -b --factory-startup --python design/blender/export_layers.py -- [--out assets/weather_scene] [--only noon,night] [--fast]

Outputs (all from the one fixed orthographic camera, so every layer is pixel-aligned):
  lighting/<anchor>.png   island + cabinet, transparent background, cropped to the shared art box
  masks/accent.png        upward-facing surfaces (snow, frost, ice accents), white with alpha
  mechanisms/vane_NN.png  wind vane at 16 headings, anemometer_NN.png at 6 rotation steps
  manifest.json           sizes, crop box, pivots, marker positions, shoreline polygons, hashes
Mechanisms are hidden in the lighting renders so they are never drawn twice.
"""
import bpy
import hashlib
import json
import math
import os
import sys
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_scene as B  # noqa: E402

CFG = B.CFG
ARGV = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []


def opt(name, default=None):
    return ARGV[ARGV.index(name) + 1] if name in ARGV else default


OUT = os.path.abspath(opt("--out", os.path.join(HERE, "..", "..", "assets", "weather_scene")))
ONLY = set(opt("--only", "").split(",")) - {""}
FAST = "--fast" in ARGV

# Art direction for each lighting anchor: sun (direction relative to the camera, colour,
# strength), ambient world light, and how bright the cabinet's own lamps are.
ANCHORS = {
    "dawn": {"sunAz": -55, "sunEl": 8, "sun": "#FFB592", "sunW": 2.2, "world": "#7A7AA6", "worldW": 0.75, "lamps": 3.0, "tint": "#D9B8C0"},
    "morning": {"sunAz": -40, "sunEl": 30, "sun": "#FFF0D8", "sunW": 2.8, "world": "#B8CCDD", "worldW": 0.75, "lamps": 1.2, "tint": "#F2F2F0"},
    "noon": {"sunAz": -15, "sunEl": 62, "sun": "#FFFFFF", "sunW": 2.7, "world": "#C3D3E0", "worldW": 0.75, "lamps": 1.0, "tint": "#FFFFFF"},
    "late_afternoon": {"sunAz": 40, "sunEl": 24, "sun": "#FFD7A0", "sunW": 3.2, "world": "#C4B39F", "worldW": 0.7, "lamps": 1.4, "tint": "#F6E2C8"},
    "sunset": {"sunAz": 55, "sunEl": 6, "sun": "#FF8C55", "sunW": 2.8, "world": "#9C6E86", "worldW": 0.6, "lamps": 2.5, "tint": "#E8B49A"},
    "dusk": {"sunAz": 50, "sunEl": 20, "sun": "#A497D8", "sunW": 1.2, "world": "#57568F", "worldW": 0.85, "lamps": 4.0, "tint": "#9C98C8"},
    "night": {"sunAz": -35, "sunEl": 40, "sun": "#8DA4D8", "sunW": 1.0, "world": "#2A3866", "worldW": 0.95, "lamps": 6.0, "tint": "#6F7FA8"},
    "overcast": {"sunAz": 0, "sunEl": 80, "sun": "#FFFFFF", "sunW": 0.0, "world": "#B9C2CB", "worldW": 1.25, "lamps": 1.6, "tint": "#D8DCE0"},
}


def ensure_dir(p):
    os.makedirs(p, exist_ok=True)
    return p


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]


def objs(prefix):
    return [o for o in bpy.data.collections[B.ROOT].all_objects if o.name.startswith(prefix)]


def mechanism_objects():
    return [o for o in bpy.data.collections["WS_MECHANISMS"].all_objects if o.type == "MESH"]


ENGINE = opt("--engine", "EEVEE")


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES" if ENGINE == "CYCLES" else "BLENDER_EEVEE"
    if ENGINE == "CYCLES":
        # Optional: CPU path tracing with a fixed seed (slower; EEVEE is the default and is repeatable)
        cy = sc.cycles
        cy.device = "CPU"
        cy.samples = 48 if FAST else 256
        cy.seed = 2600
        cy.use_animated_seed = False
        cy.use_denoising = False  # this Blender build has no OpenImageDenoise; enough samples instead
        cy.max_bounces = 3
        cy.diffuse_bounces = 2
        cy.glossy_bounces = 0
        cy.transmission_bounces = 0
        cy.transparent_max_bounces = 4
        cy.film_exposure = 1.0
        sc.render.threads_mode = "FIXED"
        sc.render.threads = 4
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.compression = 90
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    ee = sc.eevee
    ee.taa_render_samples = 16 if FAST else 64
    ee.use_gtao = True
    ee.gtao_distance = 0.6
    ee.gtao_factor = 0.8
    ee.use_soft_shadows = True
    ee.shadow_cube_size = "1024"
    ee.shadow_cascade_size = "2048"
    ee.use_bloom = False
    sc.render.filter_size = 1.2


def sun_object():
    data = bpy.data.lights.get("WS_Sun") or bpy.data.lights.new("WS_Sun", "SUN")
    ob = bpy.data.objects.get("WS_Sun")
    if ob is None:
        ob = bpy.data.objects.new("WS_Sun", data)
        bpy.data.collections[B.ROOT].objects.link(ob)
    data.angle = math.radians(4.0)
    data.use_shadow = True
    return ob


def world():
    w = bpy.data.worlds.get("WS_World") or bpy.data.worlds.new("WS_World")
    w.use_nodes = True
    bpy.context.scene.world = w
    return w


def apply_anchor(a):
    cam = bpy.context.scene.camera
    yaw = math.radians(CFG["camera"]["yawDeg"])
    # Sun azimuth is measured from the camera's view direction (negative = from the left)
    az = math.radians(a["sunAz"]) - yaw
    el = math.radians(a["sunEl"])
    to_sun = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    sun = sun_object()
    sun.rotation_euler = (-to_sun).to_track_quat("-Z", "Y").to_euler()
    sun.data.color = B.hex_rgba(a["sun"])[:3]
    sun.data.energy = a["sunW"]
    sun.hide_render = a["sunW"] <= 0
    w = world()
    bg = w.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = B.hex_rgba(a["world"])
    bg.inputs["Strength"].default_value = a["worldW"]
    m = bpy.data.materials.get("WS_lamp")
    if m:
        m.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = a["lamps"]


def render(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def alpha_box(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = list(img.pixels)
    xs, ys = [], []
    for y in range(0, h, 2):
        row = y * w * 4
        for x in range(0, w, 2):
            if px[row + x * 4 + 3] > 0.004:
                xs.append(x)
                ys.append(y)
    bpy.data.images.remove(img)
    if not xs:
        return None
    # Blender images are bottom-up; convert to top-down pixel box with a small margin
    x0, x1 = max(0, min(xs) - 4), min(w, max(xs) + 6)
    y0, y1 = max(0, h - max(ys) - 6), min(h, h - min(ys) + 4)
    return [x0, y0, x1, y1]


def crop(path, box):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    src = img.pixels[:]
    out = [0.0] * (cw * ch * 4)
    for ty in range(ch):
        sy = h - 1 - (y0 + ty)  # top-down row -> bottom-up index
        dy = ch - 1 - ty
        s = (sy * w + x0) * 4
        out[dy * cw * 4 : (dy + 1) * cw * 4] = src[s : s + cw * 4]
    o = bpy.data.images.new("crop", cw, ch, alpha=True)
    o.pixels = out
    o.filepath_raw = path
    o.file_format = "PNG"
    o.save()
    bpy.data.images.remove(o)
    bpy.data.images.remove(img)


def project(co):
    sc = bpy.context.scene
    v = world_to_camera_view(sc, sc.camera, Vector(co))
    return [round(v.x, 5), round(1.0 - v.y, 5)]  # top-down, 0..1 of the scene viewport


def hull(points):
    pts = sorted(set(map(tuple, points)))
    if len(pts) < 3:
        return [list(p) for p in pts]

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return [list(p) for p in lower[:-1] + upper[:-1]]


def waterline(objects, zmax=0.12):
    pts = []
    for ob in objects:
        for v in ob.data.vertices:
            co = ob.matrix_world @ v.co
            if co.z <= zmax:
                pts.append(project((co.x, co.y, 0.0)))
    return hull(pts)


def accent_material():
    m = bpy.data.materials.get("WS_AccentMask") or bpy.data.materials.new("WS_AccentMask")
    m.use_nodes = True
    m.blend_method = "BLEND"
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeMapRange")
    ramp.inputs["From Min"].default_value = 0.7
    ramp.inputs["From Max"].default_value = 0.9
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (1, 1, 1, 1)
    transp = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    patch = nt.nodes.new("ShaderNodeAttribute")
    patch.attribute_name = "ws_patch"
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = "MULTIPLY"
    nt.links.new(geo.outputs["Normal"], sep.inputs[0])
    nt.links.new(sep.outputs["Z"], ramp.inputs["Value"])
    nt.links.new(ramp.outputs["Result"], mul.inputs[0])
    nt.links.new(patch.outputs["Fac"], mul.inputs[1])
    nt.links.new(mul.outputs["Value"], mix.inputs["Fac"])
    nt.links.new(transp.outputs[0], mix.inputs[1])
    nt.links.new(emit.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    return m


def orchestrate():
    """Renders every layer in its own Blender process: EEVEE keeps state between renders in
    one session, so this is what makes each file identical however it was produced."""
    import subprocess
    mpath = os.path.join(OUT, "manifest.json")
    if os.path.exists(mpath):
        os.remove(mpath)
    exe = bpy.app.binary_path
    for part in list(ANCHORS) + ["accent", "mechanisms"]:
        cmd = [exe, "-b", "--factory-startup", "--python", os.path.abspath(__file__), "--", "--out", OUT, "--only", part] + (["--fast"] if FAST else [])
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or "WS_EXPORT_OK" not in r.stdout:
            print(r.stdout[-2000:], r.stderr[-2000:])
            raise SystemExit("export of %s failed" % part)
        print("WS_EXPORT part", part)
    print("WS_EXPORT_OK", mpath)


def main():
    if not ONLY:
        return orchestrate()
    B.build()
    B.isolate()
    setup_render()
    for d in ("lighting", "masks", "mechanisms"):
        ensure_dir(os.path.join(OUT, d))
    mech = mechanism_objects()
    vp = CFG["viewport"]
    manifest = {
        "version": 1,
        "generator": "design/blender/export_layers.py",
        "blender": bpy.app.version_string,
        "viewport": {"width": vp["logicalWidth"], "height": vp["logicalHeight"], "scale": vp["scale"], "horizonY": vp["horizonY"]},
        "alpha": "straight",
        "colorSpace": "sRGB",
        "lighting": {},
        "masks": {},
        "mechanisms": {},
        "markers": {},
    }

    # --- lighting anchors (mechanisms hidden)
    for o in mech:
        o.hide_render = True
    box = None
    tmp = os.path.join(OUT, "lighting", "_full.png")
    for name, a in ANCHORS.items():
        if ONLY and name not in ONLY:
            continue
        apply_anchor(a)
        render(tmp)
        if box is None:
            box = alpha_box(tmp)
        path = os.path.join(OUT, "lighting", name + ".png")
        os.replace(tmp, path)
        crop(path, box)
        manifest["lighting"][name] = {"file": "lighting/%s.png" % name, "tint": a["tint"], "sha256": sha(path)}
        print("WS_EXPORT lighting", name)
    S = vp["scale"]
    mpath = os.path.join(OUT, "manifest.json")
    if box is None and os.path.exists(mpath):  # accent/mechanism-only run: reuse the shared crop box
        box = json.load(open(mpath)).get("pixelBox")
    if box is None:  # first run without a lighting render: measure the box from a noon render
        apply_anchor(ANCHORS["noon"])
        render(tmp)
        box = alpha_box(tmp)
    manifest["artBox"] = [box[0] / S, box[1] / S, (box[2] - box[0]) / S, (box[3] - box[1]) / S] if box else None
    manifest["pixelBox"] = box

    # --- accent mask: upward-facing surfaces in white
    if not ONLY or "accent" in ONLY:
        acc = accent_material()
        saved = {}
        for ob in bpy.data.collections["WS_STATIC"].all_objects:
            if ob.type in ("MESH", "CURVE"):
                saved[ob.name] = [s.material for s in ob.material_slots]
                for s in ob.material_slots:
                    s.material = acc
        render(tmp)
        path = os.path.join(OUT, "masks", "accent.png")
        os.replace(tmp, path)
        crop(path, box)
        for ob in bpy.data.collections["WS_STATIC"].all_objects:
            if ob.name in saved:
                for s, m in zip(ob.material_slots, saved[ob.name]):
                    s.material = m
        manifest["masks"]["accent"] = {"file": "masks/accent.png", "role": "snow/frost/ice accent on upward faces", "sha256": sha(path)}
        print("WS_EXPORT mask accent")

    # --- mechanism sprites: everything else hidden, neutral noon light, tinted at runtime
    if not ONLY or "mechanisms" in ONLY:
        apply_anchor(ANCHORS["noon"])
        statics = [o for o in bpy.data.collections["WS_STATIC"].all_objects]
        for o in statics:
            o.hide_render = True
        vane = bpy.data.objects["WS_VanePivot"]
        anemo = bpy.data.objects["WS_AnemoPivot"]
        vane_obj = bpy.data.objects["WS_Vane"]
        anemo_obj = bpy.data.objects["WS_Anemometer"]

        def sprite_series(name, pivot, show, frames, step_deg):
            for o in mech:
                o.hide_render = o is not show
            entries = []
            pv = project(pivot.matrix_world.translation)
            for k in range(frames):
                pivot.rotation_euler = (0, 0, math.radians(step_deg * k))
                bpy.context.view_layer.update()
                render(tmp)
                sb = alpha_box(tmp)
                path = os.path.join(OUT, "mechanisms", "%s_%02d.png" % (name, k))
                os.replace(tmp, path)
                crop(path, sb)
                entries.append({"file": "mechanisms/%s_%02d.png" % (name, k), "box": [sb[0] / S, sb[1] / S, (sb[2] - sb[0]) / S, (sb[3] - sb[1]) / S], "sha256": sha(path)})
            pivot.rotation_euler = (0, 0, 0)
            manifest["mechanisms"][name] = {"frames": entries, "degreesPerFrame": step_deg, "pivot": pv}
            print("WS_EXPORT mechanism", name, frames)

        sprite_series("vane", vane, vane_obj, 16, 22.5)
        sprite_series("anemometer", anemo, anemo_obj, 6, 20.0)  # three cups: 120 degrees repeat
        for o in statics:
            o.hide_render = False

    # --- markers and shoreline
    for e in bpy.data.collections["WS_MARKERS"].objects:
        manifest["markers"][e.name[3:]] = project(e.matrix_world.translation)
    island = [bpy.data.objects["WS_Island"], bpy.data.objects["WS_Boulders"]]
    manifest["shore"] = {"island": waterline(island), "rocks": [waterline([o]) for o in objs("WS_Rock")]}
    if os.path.exists(tmp):
        os.remove(tmp)

    if ONLY and os.path.exists(mpath):  # partial export: merge into the existing manifest
        old = json.load(open(mpath))
        for k in ("lighting", "masks", "mechanisms"):
            old[k].update(manifest[k])
        for k in ("markers", "shore", "artBox", "pixelBox", "viewport", "blender"):
            old[k] = manifest[k]
        manifest = old
    json.dump(manifest, open(mpath, "w"), indent=1)
    print("WS_EXPORT_OK", mpath)


if __name__ == "__main__":
    main()
