"""The hero environment as one lit 3D scene: sky, sun, clouds, distant ridges, a city in three
depths and a cable-stayed bridge, all under one light and one atmosphere.

    xvfb-run -a blender -b --factory-startup --python design/blender/hero_env.py -- [--out still.png] [--fast]

Composition follows design/references/balloon-hero.png: things are placed by where they appear
on screen (x, y in hero pixels of the 1536 x 756 scene area) and how far away they are, then
lifted into the world along the camera ray, so the picture can be matched element by element.
Depth is real: every material fades towards the sky colour behind it with distance (aerial
perspective), which is what gives the hero its depth.
"""
import bpy
import bmesh
import math
import os
import random
import sys
from mathutils import Vector

ARGV = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []


def opt(name, default=None):
    return ARGV[ARGV.index(name) + 1] if name in ARGV else default


FAST = "--fast" in ARGV
OUT = os.path.abspath(opt("--out", "/tmp/hero_env.png"))

# Hero scene area (pixels of the reference): the macro band starts below this
W, H = 1536, 756
HZ = 645.0                 # horizon (the setting sun's centre)
FOCAL_MM, SENSOR_MM = 50.0, 36.0
F = W * FOCAL_MM / SENSOR_MM  # focal length in pixels
CAM_Z = 40.0               # camera height above the ground

# Light and atmosphere per lighting anchor (the same eight the craft uses). Dusk is the hero
# itself (monsoon dusk); the others are measured on design/references/balloon-lighting.png
# (dawn, noon, golden hour = sunset, dusk, night, overcast day); morning and late afternoon
# sit between their neighbours. sky: elevation (deg) -> colour, clear sky, row y -> atan((HZ - y) / F)
ANCHORS = {
    "dusk": {
        "sky": [(-8.0, (96, 98, 130)), (-2.0, (128, 110, 140)), (0.0, (205, 120, 106)), (0.67, (217, 122, 99)), (1.75, (208, 115, 99)),
                (2.8, (159, 104, 111)), (3.9, (133, 96, 116)), (5.0, (115, 88, 113)), (6.0, (105, 85, 110)), (7.1, (90, 78, 107)),
                (8.1, (74, 70, 98)), (9.2, (72, 71, 99)), (11.3, (68, 71, 99)), (13.3, (64, 69, 96)), (15.3, (62, 69, 94)), (30.0, (56, 62, 88))],
        "haze": (112, 108, 145), "ridge": (168, 124, 150),
        "key": ((255, 128, 92), 6.0, (0.95, 0.2, 0.25)), "fill": ((205, 200, 235), 2.1, (-0.3, -0.25, 0.92)),
        "cloud": (150, 146, 176), "cloudLow": (132, 132, 168), "cloudFar": (150, 140, 172),
        "streakLit": (226, 132, 108), "streakDark": (132, 106, 136), "win": 1.0, "amb": 1.0,
        "glow": (255, 160, 115), "sun": (242, 142, 102),
    },
    "dawn": {
        "sky": [(-8.0, (92, 104, 132)), (-2.0, (130, 128, 146)), (0.0, (250, 184, 128)), (0.6, (255, 185, 124)), (1.8, (245, 177, 135)),
                (4.1, (200, 163, 160)), (7.6, (140, 149, 178)), (11.0, (95, 133, 183)), (14.5, (70, 117, 172)), (30.0, (58, 100, 160))],
        "haze": (120, 128, 156), "ridge": (172, 150, 160),
        "key": ((255, 190, 150), 5.0, (-0.85, 0.25, 0.3)), "fill": ((170, 190, 230), 1.9, (0.3, -0.25, 0.9)),
        "cloud": (196, 168, 186), "cloudLow": (170, 156, 180), "cloudFar": (210, 176, 180),
        "streakLit": (250, 186, 150), "streakDark": (150, 140, 168), "win": 0.6, "amb": 1.0,
        "glow": (255, 200, 140), "sun": (255, 214, 140),
    },
    "noon": {
        "sky": [(-8.0, (100, 130, 160)), (-2.0, (130, 165, 195)), (0.0, (150, 192, 222)), (1.8, (134, 182, 219)), (4.1, (113, 173, 221)),
                (7.6, (94, 160, 216)), (11.0, (86, 149, 209)), (14.5, (75, 135, 197)), (30.0, (62, 120, 186))],
        "haze": (128, 158, 188), "ridge": (140, 170, 200),
        "key": ((255, 252, 245), 4.4, (-0.35, -0.3, 0.88)), "fill": ((185, 210, 240), 2.4, (0.4, -0.3, 0.86)),
        "cloud": (236, 240, 248), "cloudLow": (200, 210, 228), "cloudFar": (225, 232, 242),
        "streakLit": (236, 242, 250), "streakDark": (190, 205, 225), "win": 0.0, "amb": 1.15,
        "glow": (255, 250, 230), "sun": (255, 248, 220),
    },
    "sunset": {
        "sky": [(-8.0, (104, 96, 112)), (-2.0, (150, 120, 120)), (0.0, (255, 183, 104)), (1.8, (254, 181, 104)), (4.1, (253, 177, 105)),
                (7.6, (222, 160, 124)), (11.0, (195, 150, 133)), (14.5, (154, 134, 142)), (30.0, (128, 118, 140))],
        "haze": (140, 122, 128), "ridge": (196, 150, 128),
        "key": ((255, 168, 96), 6.0, (0.9, 0.2, 0.22)), "fill": ((175, 160, 205), 1.7, (-0.3, -0.25, 0.92)),
        "cloud": (190, 150, 156), "cloudLow": (160, 132, 150), "cloudFar": (230, 164, 136),
        "streakLit": (255, 196, 128), "streakDark": (176, 130, 132), "win": 0.7, "amb": 1.0,
        "glow": (255, 214, 140), "sun": (255, 226, 140),
    },
    "night": {
        "sky": [(-8.0, (24, 44, 74)), (-2.0, (30, 58, 100)), (0.0, (33, 72, 130)), (1.8, (27, 65, 120)), (4.1, (24, 58, 106)),
                (7.6, (20, 49, 91)), (11.0, (18, 43, 80)), (14.5, (20, 43, 79)), (30.0, (16, 36, 68))],
        "haze": (34, 58, 96), "ridge": (38, 64, 108),
        "key": ((150, 172, 230), 1.6, (-0.5, -0.3, 0.8)), "fill": ((70, 90, 150), 1.2, (0.4, -0.3, 0.86)),
        "cloud": (78, 96, 146), "cloudLow": (60, 78, 124), "cloudFar": (70, 90, 140),
        "streakLit": (60, 86, 136), "streakDark": (36, 58, 98), "win": 1.6, "amb": 0.42,
        "glow": (60, 90, 150), "sun": (238, 226, 190),
    },
    "overcast": {
        "sky": [(-8.0, (96, 120, 142)), (-2.0, (130, 150, 170)), (0.0, (172, 186, 201)), (1.8, (165, 180, 196)), (4.1, (147, 165, 185)),
                (7.6, (145, 160, 181)), (11.0, (144, 161, 180)), (14.5, (138, 158, 179)), (30.0, (128, 148, 170))],
        "haze": (128, 146, 166), "ridge": (140, 158, 178),
        "key": ((235, 238, 242), 1.2, (0.0, -0.3, 0.95)), "fill": ((205, 212, 225), 3.0, (0.2, -0.4, 0.9)),
        "cloud": (186, 196, 210), "cloudLow": (156, 168, 186), "cloudFar": (180, 192, 206),
        "streakLit": (190, 200, 214), "streakDark": (150, 164, 182), "win": 0.4, "amb": 0.95,
        "glow": (220, 226, 232), "sun": (230, 232, 236),
    },
}


def _mix_anchor(a, b, t):
    """An anchor between two measured ones (morning, late afternoon)."""
    A_, B_ = ANCHORS[a], ANCHORS[b]

    def mc(x, y):
        return tuple(int(round(x[i] + (y[i] - x[i]) * t)) for i in range(3))

    out = {}
    els = sorted({e for e, _ in A_["sky"]} | {e for e, _ in B_["sky"]})

    def at_el(sky, e):
        for (e0, c0), (e1, c1) in zip(sky[:-1], sky[1:]):
            if e0 <= e <= e1:
                u = (e - e0) / (e1 - e0)
                return mc(c0, c1) if False else tuple(int(round(c0[i] + (c1[i] - c0[i]) * u)) for i in range(3))
        return sky[0][1] if e < sky[0][0] else sky[-1][1]

    out["sky"] = [(e, mc(at_el(A_["sky"], e), at_el(B_["sky"], e))) for e in els]
    for k in A_:
        if k == "sky":
            continue
        if k in ("key", "fill"):
            (ca, ea, da), (cb, eb, db) = A_[k], B_[k]
            out[k] = (mc(ca, cb), ea + (eb - ea) * t, tuple(da[i] + (db[i] - da[i]) * t for i in range(3)))
        elif isinstance(A_[k], tuple):
            out[k] = mc(A_[k], B_[k])
        else:
            out[k] = A_[k] + (B_[k] - A_[k]) * t
    return out


ANCHORS["morning"] = _mix_anchor("dawn", "noon", 0.65)
ANCHORS["late_afternoon"] = _mix_anchor("noon", "sunset", 0.6)

ANCHOR_ORDER = ["dawn", "morning", "noon", "late_afternoon", "sunset", "dusk", "night", "overcast"]
ANCHOR = opt("--anchor", "dusk")
A = ANCHORS[ANCHOR]
SKY, HAZE, HAZE_RIDGE = A["sky"], A["haze"], A["ridge"]
SUN_X = 1065.0  # the hero's sun sits behind the bridge (stills only: the plugin draws its own sun)
SUN_COLOUR = A["sun"]


def lin(c):
    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    return (f(c[0]), f(c[1]), f(c[2]), 1.0)


# ------------------------------------------------------------------ scene
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.filter_size = 1.6
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    ee = sc.eevee
    ee.taa_render_samples = 16 if FAST else 64
    ee.use_gtao = True
    ee.gtao_distance = 40
    ee.gtao_factor = 0.6
    ee.use_soft_shadows = True
    ee.shadow_cascade_size = "2048"
    ee.use_bloom = True
    ee.bloom_threshold = 0.9
    ee.bloom_radius = 6.5
    ee.bloom_intensity = 0.06
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    return sc


def camera(sc):
    cam = bpy.data.cameras.new("HeroCam")
    cam.lens = FOCAL_MM
    cam.sensor_width = SENSOR_MM
    cam.sensor_fit = "HORIZONTAL"
    cam.clip_start = 1
    cam.clip_end = 60000
    # Horizon low in the frame: shift the view up rather than tilting (verticals stay vertical)
    cam.shift_y = (HZ - H / 2) / W
    ob = bpy.data.objects.new("HeroCam", cam)
    sc.collection.objects.link(ob)
    ob.location = (0, 0, CAM_Z)
    ob.rotation_euler = (math.radians(90), 0, 0)  # looking along +Y
    sc.camera = ob
    return ob


def at(x, y, d):
    """World point that appears at hero pixel (x, y) at distance d along +Y."""
    return Vector(((x - W / 2) / F * d, d, CAM_Z + (HZ - y) / F * d))


def px(d, n):
    """World size of n hero pixels at distance d."""
    return n / F * d


def elevation_ramp(nodes, links, socket_in):
    """ColorRamp from elevation (degrees, through a map range) to the sky colours."""
    lo, hi = SKY[0][0], SKY[-1][0]
    mr = nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value = lo
    mr.inputs["From Max"].default_value = hi
    links.new(socket_in, mr.inputs["Value"])
    ramp = nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, lin(SKY[0][1])
    els[1].position, els[1].color = 1.0, lin(SKY[-1][1])
    for e, c in SKY[1:-1]:
        el = els.new((e - lo) / (hi - lo))
        el.color = lin(c)
    links.new(mr.outputs["Result"], ramp.inputs["Fac"])
    return ramp


def elevation_from_dir(nodes, links, dir_socket):
    """Degrees above the horizon of a direction vector."""
    norm = nodes.new("ShaderNodeVectorMath")
    norm.operation = "NORMALIZE"
    links.new(dir_socket, norm.inputs[0])
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(norm.outputs[0], sep.inputs[0])
    asin = nodes.new("ShaderNodeMath")
    asin.operation = "ARCSINE"
    links.new(sep.outputs["Z"], asin.inputs[0])
    deg = nodes.new("ShaderNodeMath")
    deg.operation = "MULTIPLY"
    deg.inputs[1].default_value = 180 / math.pi
    links.new(asin.outputs[0], deg.inputs[0])
    return deg.outputs[0], norm.outputs[0]


def sun_dir():
    d = at(SUN_X, HZ - 2, 10000) - Vector((0, 0, CAM_Z))
    return d.normalized()


def world(sc):
    w = bpy.data.worlds.new("HeroSky")
    w.use_nodes = True
    sc.world = w
    nt = w.node_tree
    nodes, links = nt.nodes, nt.links
    bg = nodes["Background"]
    tc = nodes.new("ShaderNodeTexCoord")
    deg, ndir = elevation_from_dir(nodes, links, tc.outputs["Generated"])
    ramp = elevation_ramp(nodes, links, deg)
    # Glow around the low sun: warm, wide along the horizon
    sd = sun_dir()
    dot = nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    dot.inputs[1].default_value = (sd.x, sd.y, sd.z)
    links.new(ndir, dot.inputs[0])
    pw = nodes.new("ShaderNodeMath")
    pw.operation = "POWER"
    pw.use_clamp = True
    pw.inputs[1].default_value = 900.0
    links.new(dot.outputs["Value"], pw.inputs[0])
    glow = nodes.new("ShaderNodeMix")
    glow.data_type = "RGBA"
    glow.blend_type = "SCREEN"
    sc_ = nodes.new("ShaderNodeMath")
    sc_.operation = "MULTIPLY"
    sc_.inputs[1].default_value = 0.45
    links.new(pw.outputs[0], sc_.inputs[0])
    links.new(sc_.outputs[0], glow.inputs["Factor"])
    links.new(ramp.outputs["Color"], glow.inputs["A"])
    glow.inputs["B"].default_value = lin(A["glow"])
    lp = nodes.new("ShaderNodeLightPath")
    sel = nodes.new("ShaderNodeMix")
    sel.data_type = "RGBA"
    links.new(lp.outputs["Is Camera Ray"], sel.inputs["Factor"])
    sel.inputs["A"].default_value = (0, 0, 0, 1)
    links.new(glow.outputs["Result"], sel.inputs["B"])
    links.new(sel.outputs["Result"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.0
    return w


def lamp(sc, name, colour, energy, d):
    data = bpy.data.lights.new(name, "SUN")
    data.color = lin(colour)[:3]
    data.energy = energy
    data.angle = math.radians(3)
    data.use_shadow = False
    ob = bpy.data.objects.new(name, data)
    sc.collection.objects.link(ob)
    ob.rotation_euler = (-Vector(d).normalized()).to_track_quat("-Z", "Y").to_euler()  # light arrives from d
    return ob


def sun_lamp(sc):
    # Key: the low sun, swung to the right so flanks facing it warm up (the hero's peach facets)
    # (Strength 1 gives about 0.25 on a facing surface; materials read light as a 0..1 term)
    lamp(sc, "Key", A["key"][0], A["key"][1], A["key"][2])
    # Fill: the cool sky from above and a little in front
    lamp(sc, "Fill", A["fill"][0], A["fill"][1], A["fill"][2])


# ------------------------------------------------------------------ materials
MATS = {}


def hazed(name, colour, emission=None, haze=1.0, density=1.0, fog=None, amb=0.55, gain=0.6):
    """Flat colour lit as base * (amb + gain * light), faded with distance towards the sky colour
    behind it (or a fixed haze colour), then output unlit so the result is exactly controlled.
    The light comes from the key and fill lamps (EEVEE Shader to RGB of a white diffuse)."""
    if name in MATS:
        return MATS[name]
    amb *= A["amb"]
    if emission:
        emission = (emission[0], emission[1] * A["win"])
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nodes, links = nt.nodes, nt.links
    for n in list(nodes):
        nodes.remove(n)
    out = nodes.new("ShaderNodeOutputMaterial")
    dif = nodes.new("ShaderNodeBsdfDiffuse")
    dif.inputs["Color"].default_value = (1, 1, 1, 1)
    s2r = nodes.new("ShaderNodeShaderToRGB")
    links.new(dif.outputs[0], s2r.inputs[0])
    lit = nodes.new("ShaderNodeVectorMath")
    lit.operation = "MULTIPLY_ADD"
    links.new(s2r.outputs["Color"], lit.inputs[0])
    lit.inputs[1].default_value = (gain, gain, gain)
    lit.inputs[2].default_value = (amb, amb, amb)
    shade = nodes.new("ShaderNodeVectorMath")
    shade.operation = "MULTIPLY"
    links.new(lit.outputs[0], shade.inputs[0])
    shade.inputs[1].default_value = lin(colour)[:3]
    col = shade.outputs[0]
    if emission:
        add = nodes.new("ShaderNodeVectorMath")
        add.operation = "ADD"
        links.new(col, add.inputs[0])
        e = lin(emission[0])
        add.inputs[1].default_value = (e[0] * emission[1], e[1] * emission[1], e[2] * emission[1])
        col = add.outputs[0]
    geo = nodes.new("ShaderNodeNewGeometry")
    cam = nodes.new("ShaderNodeVectorMath")
    cam.operation = "SUBTRACT"
    cam.inputs[1].default_value = (0, 0, CAM_Z)
    links.new(geo.outputs["Position"], cam.inputs[0])
    length = nodes.new("ShaderNodeVectorMath")
    length.operation = "LENGTH"
    links.new(cam.outputs[0], length.inputs[0])
    k = nodes.new("ShaderNodeMath")  # fog = haze * (1 - exp(-d * density / 6000))
    k.operation = "MULTIPLY"
    k.inputs[1].default_value = -density / 6000.0
    links.new(length.outputs["Value"], k.inputs[0])
    ex = nodes.new("ShaderNodeMath")
    ex.operation = "EXPONENT"
    links.new(k.outputs[0], ex.inputs[0])
    fogf = nodes.new("ShaderNodeMath")
    fogf.operation = "SUBTRACT"
    fogf.inputs[0].default_value = 1.0
    links.new(ex.outputs[0], fogf.inputs[1])
    amt = nodes.new("ShaderNodeMath")
    amt.operation = "MULTIPLY"
    amt.use_clamp = True
    amt.inputs[1].default_value = haze
    links.new(fogf.outputs[0], amt.inputs[0])
    mix = nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    links.new(amt.outputs[0], mix.inputs["Factor"])
    links.new(col, mix.inputs["A"])
    if fog is None:
        deg, _ = elevation_from_dir(nodes, links, cam.outputs[0])
        links.new(elevation_ramp(nodes, links, deg).outputs["Color"], mix.inputs["B"])
    else:
        mix.inputs["B"].default_value = lin(fog)
    em = nodes.new("ShaderNodeEmission")
    links.new(mix.outputs["Result"], em.inputs["Color"])
    links.new(em.outputs[0], out.inputs["Surface"])
    MATS[name] = m
    return m


def obj(name, bm, mats):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def box(bm, x0, x1, y0, y1, z0, z1, mi=0):
    vs = [bm.verts.new(v) for v in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for f in ((0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 6, 7), (3, 2, 1, 0)):
        bm.faces.new([vs[i] for i in f]).material_index = mi


def quad(bm, a, b, c, d, mi=0):
    bm.faces.new([bm.verts.new(a), bm.verts.new(b), bm.verts.new(c), bm.verts.new(d)]).material_index = mi


# ------------------------------------------------------------------ clouds
def cloud(name, x0, x1, y_base, d, seed, domes, slab=0.17, tail=0.15, mats=None):
    """A faceted cumulus as the hero draws them: a wide flat slab with a sloped front (the
    lower half), rounded domes sitting on it, and a low pointed tip on one side. Spans hero
    pixels x0..x1, base at y_base, at distance d. domes: (centre, radius, height) as fractions
    of the width, standing on the slab."""
    rng = random.Random(seed)
    base = at((x0 + x1) / 2, y_base, d)
    wu = px(d, x1 - x0)
    bm = bmesh.new()

    def P(x, y, z):
        return Vector((base.x + x * wu, base.y + y * wu, base.z + z * wu))

    def hull(pts):
        sub = bmesh.new()
        vs = [sub.verts.new(p) for p in pts]
        bmesh.ops.convex_hull(sub, input=vs)
        me = bpy.data.meshes.new("tmp")
        sub.to_mesh(me)
        sub.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)

    sgn = 1 if tail >= 0 else -1
    # Slab: bottom ring, a top ring set back (so the front slopes), uneven
    pts = []
    for k in range(9):
        a = (k + rng.random() * 0.5) / 9 * math.tau
        reach = 0.5 - abs(tail) * (1 if math.cos(a) * sgn > 0.5 else 0)
        pts.append(P(math.cos(a) * reach, math.sin(a) * 0.16, 0))
    for k in range(7):
        a = (k + rng.random() * 0.5) / 7 * math.tau
        pts.append(P(math.cos(a) * (0.38 - abs(tail) * 0.5) - sgn * 0.03, 0.05 + math.sin(a) * 0.1, slab * (0.85 + 0.3 * rng.random())))
    hull(pts)
    # Domes: rounded caps on the slab (no apex point, so no spikes)
    for cx, r, hh in domes:
        pts = []
        for ring, (e, n) in enumerate(((0.0, 8), (0.55, 7), (1.05, 5))):
            for k in range(n):
                a = (k + rng.random() * 0.6 + ring * 0.3) / n * math.tau
                rr = r * math.cos(e) * (0.9 + 0.2 * rng.random())
                pts.append(P(cx + math.cos(a) * rr, 0.04 + math.sin(a) * rr * 0.6, slab * 0.6 + math.sin(e) * hh * (0.92 + 0.12 * rng.random())))
        pts.append(P(cx + (rng.random() - 0.5) * r * 0.3, 0.04, slab * 0.6 + hh * 1.02))
        hull(pts)
    if tail:
        # a low pointed tip, stepped down from the slab
        root = sgn * (0.5 - abs(tail) - 0.06)
        pts = [P(root, dy * 0.12, dz) for dy, dz in ((-1, 0), (1, 0), (0, 0.11), (-0.6, 0.08), (0.5, 0.06))]
        pts.append(P(sgn * 0.5, 0.0, 0.035))
        hull(pts)
    return obj(name, bm, [mats or hazed("cloud", (150, 146, 178), haze=0.55)])


def loft_cloud(name, profile, base_l, base_r, d, seed, mats, depth=0.38, step=1):
    """A cloud lofted from its silhouette, measured on the hero: profile is the top edge as
    (x, y) hero pixels, base_l/base_r the flat base y at either end. Each profile point gets a
    coarse cross-section (sloped lower front, rounded top, back), jittered, so the hull breaks
    into the hero's big uneven facets; the ends close to points (the tips)."""
    rng = random.Random(seed)
    bm = bmesh.new()
    # fewer, bigger facets: keep every step-th section (ends and the highest point always)
    peak = min(range(len(profile)), key=lambda i: profile[i][1])
    profile = [p for i, p in enumerate(profile) if i % step == 0 or i in (0, peak, len(profile) - 1)]
    x0, x1 = profile[0][0], profile[-1][0]
    rings = []
    for i, (x, y) in enumerate(profile):
        t = (x - x0) / (x1 - x0)
        yb = base_l + (base_r - base_l) * t
        b = at(x, yb, d)
        top = at(x, y, d)
        h = max(px(d, 2), top.z - b.z)
        dp = h * depth
        jit = lambda k: (rng.random() - 0.5) * k * h
        if i in (0, len(profile) - 1):
            rings.append([bm.verts.new((b.x, d, top.z if i == 0 else b.z + h * 0.35))])
            continue
        sec = [(-dp, 0.0), (-dp * 0.8, 0.55), (0.0, 1.0), (dp * 0.8, 0.55), (dp, 0.0)]
        ring = []
        for k, (yy, zz) in enumerate(sec):
            jz = 0 if k in (0, len(sec) - 1) else jit(0.14)
            jx = jit(0.2) if k != 2 else jit(0.08)
            ring.append(bm.verts.new((b.x + jx, d + yy + jit(0.1), b.z + zz * h + jz)))
        rings.append(ring)
    for a, b in zip(rings[:-1], rings[1:]):
        if len(a) == 1 or len(b) == 1:
            tip, ring = (a[0], b) if len(a) == 1 else (b[0], a)
            for k in range(len(ring) - 1):
                bm.faces.new([tip, ring[k], ring[k + 1]] if len(a) == 1 else [ring[k + 1], ring[k], tip])
            bm.faces.new([tip, ring[-1], ring[0]] if len(a) == 1 else [ring[0], ring[-1], tip])
            continue
        for k in range(len(a) - 1):
            if rng.random() < 0.5:
                bm.faces.new([a[k], b[k], b[k + 1]])
                bm.faces.new([a[k], b[k + 1], a[k + 1]])
            else:
                bm.faces.new([a[k], b[k], a[k + 1]])
                bm.faces.new([a[k + 1], b[k], b[k + 1]])
        bm.faces.new([a[-1], b[-1], b[0], a[0]])  # flat base
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj(name, bm, [mats])


def tiered_cloud(name, profile, base_l, base_r, d, seed, mats, lower=0.62):
    """The hero's two-tier build: the full silhouette set back, and a lower body bulging out in
    front of it (its top edge is the crease across the cloud)."""
    loft_cloud(name, profile, base_l, base_r, d, seed, mats, depth=0.3)
    x0, x1 = profile[0][0], profile[-1][0]
    low = []
    for x, y in profile:
        t = (x - x0) / (x1 - x0)
        yb = base_l + (base_r - base_l) * t
        low.append((x, yb - (yb - y) * lower))
    loft_cloud(name + "Low", low, base_l, base_r, d - px(d, 6), seed + 100, hazed("cloudLow", A["cloudLow"], haze=0.22, amb=0.36, gain=0.7), depth=0.55)


def clouds():
    m = hazed("cloud", A["cloud"], haze=0.22, amb=0.38, gain=1.0)
    mf = hazed("cloudFar", A["cloudFar"], haze=0.55, amb=0.55, gain=0.6)
    # Silhouettes measured on the hero: top edge (x, y), base y at the ends, distance
    tiered_cloud("CloudUL", [(80, 258), (92, 215), (118, 178), (165, 172), (205, 160), (235, 143), (262, 141), (292, 160), (322, 185), (352, 194),
                           (378, 205), (402, 226), (428, 236), (448, 248), (466, 272)], 297, 284, 3000, 1, m)
    tiered_cloud("CloudL", [(-60, 430), (-20, 372), (30, 348), (70, 340), (110, 345), (150, 352), (185, 375), (215, 400), (240, 425), (256, 446)],
               470, 456, 2600, 2, m)
    tiered_cloud("CloudLL", [(140, 545), (168, 505), (205, 488), (245, 478), (280, 468), (318, 466), (352, 472), (385, 486), (418, 494), (450, 506),
                           (480, 515), (506, 530)], 557, 545, 3400, 3, m)
    tiered_cloud("CloudR", [(1103, 548), (1148, 522), (1195, 484), (1238, 458), (1270, 446), (1299, 437), (1330, 441), (1356, 458), (1382, 470),
                          (1410, 478), (1440, 492), (1464, 506), (1486, 528)], 552, 536, 3800, 4, m)
    tiered_cloud("CloudUR", [(1162, 290), (1186, 268), (1215, 256), (1242, 216), (1272, 200), (1305, 184), (1340, 190), (1370, 200), (1400, 222),
                           (1430, 228), (1456, 246), (1484, 284)], 312, 308, 3000, 5, m)
    # Small far mounds low over the horizon
    cloud("CloudF1", 960, 1065, 600, 9000, 7, [(-0.05, 0.25, 0.22)], slab=0.12, tail=0.1, mats=mf)
    cloud("CloudF2", 1080, 1200, 606, 9000, 8, [(0.0, 0.22, 0.2)], slab=0.12, tail=-0.1, mats=mf)
    cloud("CloudF3", 1350, 1530, 626, 9500, 9, [(-0.1, 0.18, 0.16), (0.15, 0.15, 0.13)], slab=0.1, tail=0.12, mats=mf)


def stratus():
    """Broad flat cloud bands across the glowing horizon (lavender, thin lit edges), measured
    on the hero: (left x, right x, centre y, thickness px, distance, lit)."""
    dark = hazed("streakDark", A["streakDark"], haze=0.35, amb=0.75, gain=0.3)
    lit = hazed("streakLit", A["streakLit"], haze=0.3, amb=0.9, gain=0.2)
    bands = [(-20, 560, 566, 14, 9000, 0), (380, 980, 548, 9, 9500, 0), (600, 1536, 576, 12, 10000, 0), (1150, 1536, 600, 5, 10500, 1),
             (-20, 1100, 616, 9, 11000, 0), (780, 1000, 612, 5, 11500, 1), (-20, 400, 586, 4, 10000, 1), (1250, 1536, 560, 5, 9500, 1)]
    bm = bmesh.new()
    for x0, x1, y, t, d, is_lit in bands:
        a, b = at(x0, y + t / 2, d), at(x1, y + t / 2, d)
        th = px(d, t)
        taper = px(d, 40)
        quad(bm, (a.x, d, a.z), (b.x, d, b.z), (b.x - taper, d, b.z + th), (a.x + taper * 1.5, d, a.z + th), is_lit)
    obj("Streaks", bm, [dark, lit])


# ------------------------------------------------------------------ terrain
def ridge(name, d, y_base, peaks, colour, haze, seed):
    """A faceted mountain band at distance d: peaks are (screen x, screen top y)."""
    rng = random.Random(seed)
    bm = bmesh.new()
    pts = []
    for x, ytop in peaks:
        pts.append(at(x, ytop, d))
    base = [at(p_x, y_base, d) for p_x, _ in peaks]
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        mid = (a + b) / 2
        mid.z = min(a.z, b.z) - (abs(a.z - b.z) * 0.3 + px(d, 6)) * rng.random()
        mid.y += px(d, 30)
        ba, bb = base[i], base[i + 1]
        bm.faces.new([bm.verts.new(ba), bm.verts.new(a), bm.verts.new(mid)])
        bm.faces.new([bm.verts.new(a), bm.verts.new(b), bm.verts.new(mid)])
        bm.faces.new([bm.verts.new(mid), bm.verts.new(b), bm.verts.new(bb)])
        bm.faces.new([bm.verts.new(ba), bm.verts.new(mid), bm.verts.new(bb)])
    # the measured (dusk) ridge colours, pulled towards this anchor's ridge haze
    tint = tuple(int(colour[i] * 0.45 + HAZE_RIDGE[i] * 0.55 * (colour[i] / 150.0)) for i in range(3)) if ANCHOR != "dusk" else colour
    return obj(name, bm, [hazed(name, tint, haze=haze, fog=HAZE_RIDGE)])


def mountains():
    """Three ridge layers, measured on the hero: broad low triangles, lighter with distance."""
    far = [(-40, 640), (60, 628), (180, 646), (330, 622), (470, 640), (600, 618), (760, 642), (900, 630), (1010, 652), (1120, 660),
           (1250, 650), (1380, 655), (1470, 645), (1580, 652)]
    mid = [(-40, 650), (120, 638), (250, 655), (415, 610), (560, 640), (700, 625), (840, 648), (960, 662), (1080, 668), (1200, 676),
           (1330, 664), (1420, 672), (1580, 668)]
    near = [(-40, 672), (90, 660), (230, 676), (380, 664), (520, 680), (680, 668), (820, 682), (1000, 688), (1100, 680), (1240, 692),
            (1420, 684), (1580, 690)]
    ridge("RidgeFar", 16000, 700, far, (172, 122, 140), 0.35, 1)
    ridge("RidgeMid", 11000, 710, mid, (126, 106, 142), 0.3, 2)
    ridge("RidgeNear", 7000, 720, near, (106, 100, 138), 0.3, 3)


# ------------------------------------------------------------------ city
def lit_windows(bm, rng, xc, w, z_lo, z_hi, y, cell, n_groups, mi):
    """A few groups of lit windows (the hero lights pairs and short columns, not grids)."""
    for g in range(n_groups):
        cols = rng.choice((1, 2, 2, 2))
        rows = rng.choice((1, 1, 2))
        gx = xc + (rng.random() - 0.5) * (w - cell * 3)
        gz = z_lo + rng.random() * max(0.0, z_hi - z_lo - rows * cell * 2)
        for c in range(cols):
            for r in range(rows):
                x0 = gx + c * cell * 1.7
                z0 = gz + r * cell * 2.0
                quad(bm, (x0, y - 0.1, z0), (x0 + cell, y - 0.1, z0), (x0 + cell, y - 0.1, z0 + cell * 1.35), (x0, y - 0.1, z0 + cell * 1.35), mi)


def building(bm, x0px, x1px, top_y, d, rng, n_windows=0, cell_px=5, antenna=0, roofbox=True):
    """A box whose front spans hero pixels x0..x1 and whose roof is at top_y, at distance d."""
    a, b = at(x0px, top_y, d), at(x1px, top_y, d)
    w = b.x - a.x
    depth = w * (0.5 + 0.4 * rng.random())
    box(bm, a.x, b.x, d, d + depth, -60, a.z, 0)
    if roofbox and rng.random() < 0.5:
        bw = w * (0.2 + 0.25 * rng.random())
        bx = a.x + w * (0.15 + 0.5 * rng.random())
        box(bm, bx, bx + bw, d + depth * 0.2, d + depth * 0.6, a.z, a.z + px(d, 3 + 4 * rng.random()), 0)
    if antenna:
        ax = a.x + w * (0.3 + 0.4 * rng.random())
        box(bm, ax, ax + px(d, 1.2), d + depth * 0.4, d + depth * 0.4 + px(d, 1.2), a.z, a.z + px(d, antenna), 0)
    if n_windows:
        lit_windows(bm, rng, (a.x + b.x) / 2, w, a.z - (a.z + 60) * 0.85, a.z - px(d, 10), d, px(d, cell_px), n_windows, 1)


def city():
    win = (255, 178, 84)
    rng = random.Random(31)
    # Far: a hazy skyline just over the ridges, filling the gaps
    far = bmesh.new()
    x = -20
    while x < 1560:
        w = 14 + rng.random() * 26
        top = 668 + rng.random() * 26 - (18 if rng.random() < 0.25 else 0)
        building(far, x, x + w, top, 3200, rng, n_windows=rng.random() < 0.15, cell_px=3)
        x += w + rng.random() * 10
    obj("CityFar", far, [hazed("cityFar", (96, 100, 136), haze=0.94, fog=HAZE), hazed("winFar", win, emission=(win, 0.8), haze=0.45, fog=HAZE)])
    # Mid: the tall slim towers, measured on the hero (x0, x1, roof y, antenna px)
    mid = bmesh.new()
    towers = [(42, 90, 602, 0), (105, 125, 664, 0), (137, 160, 650, 0), (164, 187, 610, 0), (237, 280, 586, 0), (290, 312, 670, 0),
              (318, 370, 646, 0), (382, 405, 672, 0), (440, 482, 605, 10), (502, 530, 642, 12), (565, 605, 676, 0), (612, 652, 668, 0),
              (1300, 1347, 664, 0), (1387, 1420, 687, 0), (1437, 1467, 665, 0), (1480, 1522, 642, 16), (1525, 1560, 660, 0)]
    for x0, x1, top, ant in towers:
        building(mid, x0, x1, top, 1800, rng, n_windows=rng.choice((0, 1, 1, 2)), cell_px=4, antenna=ant)
    obj("CityMid", mid, [hazed("cityMid", (72, 80, 114), haze=0.42, fog=HAZE), hazed("winMid", win, emission=(win, 1.0), haze=0.12, fog=HAZE)])
    # Near: a dense low skyline across the bottom, with a few lit pairs
    near = bmesh.new()
    # Middle: separate hazy blocks between the towers and the near roofs
    midlow = bmesh.new()
    x = -30
    while x < 1570:
        w = 26 + rng.random() * 40
        top = 676 + rng.random() * 34
        if not (960 < x < 1290):
            building(midlow, x, x + w, top, 1300, rng, n_windows=rng.choice((0, 0, 1)), cell_px=4)
        x += w + 4 + rng.random() * 22
    obj("CityMidLow", midlow, [hazed("cityMidLow", (70, 76, 108), haze=0.5, fog=HAZE), hazed("winMidLow", (255, 178, 84), emission=((255, 178, 84), 1.0), haze=0.12, fog=HAZE)])
    # Near: separate roofs along the bottom with gaps, a few lit pairs
    x = -30
    while x < 1570:
        w = 40 + rng.random() * 70
        low = 960 < x < 1300  # keep the bridge clear
        top = (728 if low else 704) + rng.random() * 30
        building(near, x, x + w, top, 900, rng, n_windows=rng.choice((0, 1, 1, 2)), cell_px=5)
        x += w + 6 + rng.random() * 40
    # Low blocks at middle distance under the bridge (the hero has no gap there)
    x = 930
    while x < 1330:
        w = 22 + rng.random() * 34
        building(near, x * 1.0, x + w, 712 + rng.random() * 16, 1400, rng, n_windows=rng.choice((0, 0, 1)), cell_px=4)
        x += w + rng.random() * 10
    obj("CityNear", near, [hazed("cityNear", (70, 76, 104), haze=0.34, fog=HAZE), hazed("winNear", win, emission=(win, 1.0), haze=0.04, fog=HAZE)])
    # Foreground: dark rounded treetops along the very bottom
    trees = bmesh.new()
    x = -20
    while x < 1560:
        r = 10 + rng.random() * 14
        c = at(x, 758 - r * 0.3, 600)
        rr = px(600, r)
        n = 7
        vs = [trees.verts.new((c.x + math.cos(k / n * math.pi) * rr, 600, c.z + math.sin(k / n * math.pi) * rr * 0.8)) for k in range(n + 1)]
        vs.append(trees.verts.new((c.x - rr, 600, c.z - px(600, 40))))
        vs.append(trees.verts.new((c.x + rr, 600, c.z - px(600, 40))))
        trees.faces.new(vs[: n + 1] + [vs[-1], vs[-2]])
        x += r * 1.4
    obj("Trees", trees, [hazed("trees", (66, 74, 100), haze=0.38, fog=HAZE)])


def bridge():
    """Cable-stayed bridge measured on the hero: deck y 705 from x 980 to 1265, two slim
    two-legged pylons at x 1135 and 1155 rising to y 642, cable fans x 1077..1212, piers."""
    d = 2600
    m = hazed("bridge", (66, 68, 104), haze=0.38, fog=HAZE)
    bm = bmesh.new()
    deck_y, top_y, foot_y = 705, 642, 725
    a, b = at(980, deck_y, d), at(1265, deck_y, d)
    t = px(d, 3.5)
    box(bm, a.x, b.x, d, d + px(d, 10), a.z - t, a.z, 0)
    for xp in (1210, 1235, 1258):
        p = at(xp, deck_y, d)
        box(bm, p.x - px(d, 2.5), p.x + px(d, 2.5), d, d + px(d, 5), -60, p.z - t, 0)
    leg = px(d, 2.6)
    for xc in (1135, 1157):
        foot, top = at(xc, foot_y, d), at(xc, top_y, d)
        for sgn in (-1, 1):
            f = Vector((foot.x + sgn * px(d, 5), d + px(d, 4), -60))
            tp = Vector((top.x + sgn * px(d, 1.6), d + px(d, 4), top.z))
            v = [bm.verts.new((f.x - leg, f.y, f.z)), bm.verts.new((f.x + leg, f.y, f.z)), bm.verts.new((tp.x + leg * 0.7, tp.y, tp.z)), bm.verts.new((tp.x - leg * 0.7, tp.y, tp.z))]
            bm.faces.new(v)
        cross = at(xc, deck_y + 2, d)
        box(bm, cross.x - px(d, 6), cross.x + px(d, 6), d + px(d, 3), d + px(d, 5), cross.z - px(d, 2), cross.z, 0)
        for k in range(1, 8):
            for sgn in (-1, 1):
                anchor = at(xc, top_y + 4 + k * 1.6, d)
                end = at(xc + sgn * (12 + k * 9), deck_y - 2, d)
                cw = px(d, 0.45)
                bm.faces.new([bm.verts.new((anchor.x - cw, d, anchor.z)), bm.verts.new((anchor.x + cw, d, anchor.z)), bm.verts.new((end.x + cw, d, end.z)), bm.verts.new((end.x - cw, d, end.z))])
    obj("Bridge", bm, [m])


def sun_disc():
    """The setting sun: a pale disc half behind the far ridge (hero: centre 1065, 642, r 42)."""
    d = 14000
    c = at(1065, 642, d)
    r = px(d, 42)
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, segments=48, radius=r)
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=__import__("mathutils").Matrix.Rotation(math.radians(90), 3, "X"))
    bmesh.ops.translate(bm, verts=bm.verts, vec=c)
    m = bpy.data.materials.new("sun")
    m.use_nodes = True
    nodes = m.node_tree.nodes
    for n in list(nodes):
        nodes.remove(n)
    em = nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = lin(A["sun"])
    em.inputs["Strength"].default_value = 1.0
    out = nodes.new("ShaderNodeOutputMaterial")
    m.node_tree.links.new(em.outputs[0], out.inputs["Surface"])
    obj("Sun", bm, [m])


def build():
    sc = reset()
    camera(sc)
    world(sc)
    sun_lamp(sc)
    sun_disc()
    stratus()
    mountains()
    bridge()
    city()
    clouds()
    return sc


# ------------------------------------------------------------------ export for the plugin
EXPORT_SCALE = 4.0 / 3.0   # hero pixels (1536 wide) -> plugin art pixels (2048 wide = 1024 logical x 2)
LOGICAL = 1024.0 / W       # hero pixels -> logical scene pixels
CLOUD_NAMES = ["CloudUL", "CloudUR", "CloudL", "CloudR", "CloudLL"]  # in the order they appear as cover grows
BAND_OBJECTS = ("Streaks", "CloudF1", "CloudF2", "CloudF3")
CITY_OBJECTS = ("RidgeFar", "RidgeMid", "RidgeNear", "CityFar", "CityMid", "CityMidLow", "CityNear", "Trees", "Bridge")


def sky_rows(n=48):
    """The sky gradient as colours down the scene (logical rows), as Blender's ramp blends them
    (linear), so the plugin can draw exactly this sky and blend anchors."""
    def to_srgb(v):
        v = max(0.0, min(1.0, v))
        return round(255 * (v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))
    out = []
    for k in range(n + 1):
        y = H * k / n
        e = math.degrees(math.atan((HZ - y) / F))
        keys = SKY
        if e <= keys[0][0]:
            c = lin(keys[0][1])
        elif e >= keys[-1][0]:
            c = lin(keys[-1][1])
        else:
            for (e0, c0), (e1, c1) in zip(keys[:-1], keys[1:]):
                if e0 <= e <= e1:
                    u = (e - e0) / (e1 - e0)
                    a0, a1 = lin(c0), lin(c1)
                    c = tuple(a0[i] + (a1[i] - a0[i]) * u for i in range(3))
                    break
        out.append([round(y * LOGICAL, 2), [to_srgb(c[0]), to_srgb(c[1]), to_srgb(c[2])]])
    return out


def alpha_crop(path):
    """Crop a transparent render to its content (system Python with Pillow: Blender's own
    Python may lack numpy); returns the box in plugin art pixels."""
    import json
    import subprocess
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "crop_alpha.py")
    r = subprocess.run(["python3", tool, path], capture_output=True, text=True, check=True)
    return json.loads(r.stdout)[path]


def worker(job, out_dir):
    anchor, layer = job.split(":")
    sc = build()
    sc.render.film_transparent = True
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.resolution_x, sc.render.resolution_y = int(W * EXPORT_SCALE), int(H * EXPORT_SCALE)
    for name in ("Sun",):
        if name in bpy.data.objects:
            bpy.data.objects[name].hide_render = True
    results = []

    def only(names, file):
        for ob in sc.objects:
            if ob.type == "MESH":
                ob.hide_render = not any(ob.name == n or ob.name.startswith(n + "Low") for n in names)
        path = os.path.join(out_dir, file)
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        box = alpha_crop(path)
        return [round(v / 2.0, 2) for v in box] if box else None  # logical scene pixels

    if layer == "city":
        results.append({"layer": "city", "anchor": anchor, "file": "hero/city_%s.png" % anchor, "box": only(CITY_OBJECTS, "city_%s.png" % anchor)})
    elif layer == "bands":
        results.append({"layer": "bands", "anchor": anchor, "file": "hero/bands_%s.png" % anchor, "box": only(BAND_OBJECTS, "bands_%s.png" % anchor)})
    elif layer == "clouds":
        for k, n in enumerate(CLOUD_NAMES):
            f = "cloud%d_%s.png" % (k, anchor)
            results.append({"layer": "cloud", "index": k, "anchor": anchor, "file": "hero/" + f, "box": only((n,), f)})
    import json
    json.dump(results, open(os.path.join(out_dir, "_job_%s_%s.json" % (anchor, layer)), "w"))
    print("WS_HERO_JOB_OK", job)


def orchestrate(out_dir, anchors, layers=("city", "bands", "clouds")):
    import json
    import subprocess
    os.makedirs(out_dir, exist_ok=True)
    for a in anchors:
        for layer in layers:
            cmd = [bpy.app.binary_path, "-b", "--factory-startup", "--python", os.path.abspath(__file__), "--", "--anchor", a,
                   "--job", a + ":" + layer, "--export", out_dir] + (["--fast"] if FAST else [])
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or "WS_HERO_JOB_OK" not in r.stdout:
                print(r.stdout[-3000:], r.stderr[-2000:])
                raise SystemExit("hero job %s:%s failed" % (a, layer))
            print("WS_HERO_JOB", a, layer, flush=True)
    mpath = os.path.join(out_dir, "..", "hero.json")
    man = json.load(open(mpath)) if os.path.exists(mpath) else {}
    man.update({"version": 1, "generator": "design/blender/hero_env.py", "viewport": {"width": 1024, "height": 504, "horizonY": round(HZ / H, 4)}})
    sky = man.setdefault("sky", {})
    for a in anchors:
        global SKY
        SKY = ANCHORS[a]["sky"]
        sky[a] = sky_rows()
    layers = man.setdefault("layers", {})
    clouds = {int(c["index"]): c for c in man.get("clouds", [])}
    for f in sorted(os.listdir(out_dir)):
        if not f.startswith("_job_"):
            continue
        for e in json.load(open(os.path.join(out_dir, f))):
            if e["layer"] == "cloud":
                c = clouds.setdefault(e["index"], {"index": e["index"], "frames": {}})
                c["frames"][e["anchor"]] = e["file"]
                c["box"] = e["box"]
            else:
                L = layers.setdefault(e["layer"], {"frames": {}})
                L["frames"][e["anchor"]] = e["file"]
                L["box"] = e["box"]
        os.remove(os.path.join(out_dir, f))
    man["clouds"] = [clouds[k] for k in sorted(clouds)]
    json.dump(man, open(mpath, "w"), indent=1)
    print("WS_HERO_EXPORT_OK", mpath)


if __name__ == "__main__":
    export = opt("--export")
    if export and opt("--job"):
        worker(opt("--job"), os.path.abspath(export))
    elif export:
        orchestrate(os.path.abspath(export), [a for a in opt("--anchors", ",".join(ANCHOR_ORDER)).split(",") if a],
                    [l for l in opt("--layers", "city,bands,clouds").split(",") if l])
    else:
        sc = build()
        sc.render.filepath = OUT
        bpy.ops.render.render(write_still=True)
        print("WS_HERO_OK", OUT)
