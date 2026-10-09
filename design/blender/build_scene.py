"""Builds the Weather Synth island scene from scene_config.json.

    blender -b --factory-startup --python design/blender/build_scene.py -- [--save path.blend] [--preview out.png]

Idempotent: everything lives in the WEATHER_SYNTH collection and every datablock is
prefixed WS_; a rebuild removes only those and recreates them, so unrelated objects in
an open file are left alone. Geometry is deterministic (fixed seed, no operators that
depend on the UI context).
"""
import bpy
import bmesh
import json
import math
import os
import random
import sys
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "scene_config.json")))
ROOT = "WEATHER_SYNTH"


# ------------------------------------------------------------------ helpers
def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_rgba(h, alpha=1.0):
    h = h.lstrip("#")
    return tuple(srgb_to_linear(int(h[i : i + 2], 16) / 255.0) for i in (0, 2, 4)) + (alpha,)


def clear_previous():
    root = bpy.data.collections.get(ROOT)
    if root:
        def walk(col):
            for ch in list(col.children):
                walk(ch)
                bpy.data.collections.remove(ch)
            for ob in list(col.objects):
                bpy.data.objects.remove(ob, do_unlink=True)
        walk(root)
    else:
        root = bpy.data.collections.new(ROOT)
        bpy.context.scene.collection.children.link(root)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.curves, bpy.data.lights, bpy.data.cameras):
        for d in list(coll):
            if d.name.startswith("WS_") and d.users == 0:
                coll.remove(d)
    return root


def sub_collection(root, name):
    c = bpy.data.collections.new(name)
    root.children.link(c)
    return c


MATS = {}


def mat(key, emission=None, strength=0.0):
    name = "WS_" + key
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    col = hex_rgba(CFG["palette"][key])
    bsdf.inputs["Base Color"].default_value = col
    bsdf.inputs["Roughness"].default_value = 1.0
    for spec in ("Specular IOR Level", "Specular"):
        if spec in bsdf.inputs:
            bsdf.inputs[spec].default_value = 0.0
    if emission:
        bsdf.inputs["Emission Color" if "Emission Color" in bsdf.inputs else "Emission"].default_value = hex_rgba(CFG["palette"][emission])
        bsdf.inputs["Emission Strength"].default_value = strength
    m.diffuse_color = col  # Workbench preview colour
    MATS[name] = m
    return m


def faces_of(verts):
    """Faces touching these vertices, in a stable order (a set would order them by memory
    address, which makes any random choice per face differ between runs)."""
    out, seen = [], set()
    for v in verts:
        for f in v.link_faces:
            if id(f) not in seen:
                seen.add(id(f))
                out.append(f)
    return out


def mesh_object(name, bm, coll, materials, flat=True):
    me = bpy.data.meshes.new("WS_" + name)
    for f in bm.faces:
        f.smooth = not flat
    bm.to_mesh(me)
    bm.free()
    for m in materials:
        me.materials.append(m)
    ob = bpy.data.objects.new("WS_" + name, me)
    coll.objects.link(ob)
    return ob


def box(bm, cx, cy, cz, sx, sy, sz, mat_index=0):
    """Axis-aligned box centred at (cx, cy) with its base at cz."""
    r = bmesh.ops.create_cube(bm, size=1.0)
    verts = r["verts"]
    bmesh.ops.scale(bm, vec=(sx, sy, sz), verts=verts)
    bmesh.ops.translate(bm, vec=(cx, cy, cz + sz / 2), verts=verts)
    for f in faces_of(verts):
        f.material_index = mat_index
    return verts


def cylinder(bm, cx, cy, cz, r, depth, segs=8, axis="Z", mat_index=0, r2=None):
    res = bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segs, radius1=r, radius2=r if r2 is None else r2, depth=depth)
    verts = res["verts"]
    if axis == "Y":
        bmesh.ops.rotate(bm, verts=verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, "X"))
    elif axis == "X":
        bmesh.ops.rotate(bm, verts=verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, "Y"))
    bmesh.ops.translate(bm, vec=(cx, cy, cz), verts=verts)
    for f in faces_of(verts):
        f.material_index = mat_index
    return verts


def empty(name, coll, loc):
    e = bpy.data.objects.new("WS_" + name, None)
    e.empty_display_size = 0.1
    e.location = loc
    coll.objects.link(e)
    return e


# ------------------------------------------------------------------ island
def build_island(coll, rng):
    I = CFG["island"]
    n, top, sea = I["facets"], I["topRadius"], I["seaRadius"]
    sq = I["squashY"]
    bm = bmesh.new()
    rings = []
    # Top rim (grass edge), a lip just below it, cliff rings, and the waterline
    profile = [(top * 0.55, I["topZ"] + 0.04), (top, I["topZ"]), (top * 1.04, I["topZ"] - 0.18)]
    for k in range(I["cliffRings"]):
        t = (k + 1) / (I["cliffRings"] + 1)
        profile.append((top * 1.04 + (sea - top * 1.04) * t, (I["topZ"] - 0.18) * (1 - t)))
    profile.append((sea, -0.06))
    for ri, (rad, z) in enumerate(profile):
        ring = []
        for i in range(n):
            a = 2 * math.pi * i / n + (0.5 * (ri % 2)) * 2 * math.pi / n * (0.35 if ri > 1 else 0)
            jr = 1 + I["jitter"] * (rng.random() - 0.5) * (0.4 if ri == 0 else 1.0)
            jz = (rng.random() - 0.5) * (0.18 if ri >= 2 else 0.05)
            ring.append(bm.verts.new((math.cos(a) * rad * jr, math.sin(a) * rad * jr * sq, z + jz)))
        rings.append(ring)
    centre = bm.verts.new((0, 0, I["topZ"] + 0.06))
    for i in range(n):
        f = bm.faces.new((centre, rings[0][i], rings[0][(i + 1) % n]))
        f.material_index = 0
    for ri in range(len(rings) - 1):
        a, b = rings[ri], rings[ri + 1]
        for i in range(n):
            j = (i + 1) % n
            # Triangulated quads read as chunky rock facets
            for tri in ((a[i], b[i], b[j]), (a[i], b[j], a[j])):
                f = bm.faces.new(tri)
                f.material_index = 0 if ri == 0 else (1 if (i + ri) % 3 else 2)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    island = mesh_object("Island", bm, coll, [mat("grass"), mat("rock"), mat("rockDark")])

    # A belt of big faceted boulders around the cliff gives the chunky low-poly edge
    bb = bmesh.new()
    nb = I.get("boulders", 0)
    for k in range(nb):
        a = 2 * math.pi * (k + 0.5 * rng.random()) / nb
        rad = (top * 1.02 + sea) / 2 * (0.95 + 0.12 * rng.random())
        s = 0.5 + 0.35 * rng.random()
        res = bmesh.ops.create_icosphere(bb, subdivisions=0, radius=1.0)  # 20 faces: chunky slabs
        vs = res["verts"]
        for v in vs:
            v.co.x *= s * (1.35 + 0.5 * rng.random())
            v.co.y *= s * (0.95 + 0.3 * rng.random())
            z = max(v.co.z, -0.45)
            v.co.z = (0.55 + 0.25 * (z - 0.55) if z > 0.55 else z) * s * (0.8 + 0.3 * rng.random())  # flattened tops
            v.co.x += (rng.random() - 0.5) * 0.12
            v.co.y += (rng.random() - 0.5) * 0.12
        bmesh.ops.rotate(bb, verts=vs, cent=(0, 0, 0), matrix=Matrix.Rotation(a + rng.random(), 3, "Z") @ Matrix.Rotation((rng.random() - 0.5) * 0.5, 3, "X"))
        bmesh.ops.translate(bb, vec=(math.cos(a) * rad, math.sin(a) * rad * sq, I["topZ"] * (0.1 + 0.3 * rng.random())), verts=vs)
        for f in faces_of(vs):
            f.material_index = 0 if rng.random() < 0.55 else 1
    mesh_object("Boulders", bb, coll, [mat("rock"), mat("rockDark")])
    return island


def build_rocks(coll, rng):
    for k, r in enumerate(CFG["rocks"]):
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=0, radius=1.0)
        for v in bm.verts:
            v.co.x *= r["s"] * (1.0 + 0.35 * rng.random())
            v.co.y *= r["s"] * 0.85
            z = max(v.co.z, -0.2)
            v.co.z = (0.6 + 0.3 * (z - 0.6) if z > 0.6 else z) * r["h"] * 1.3
        bmesh.ops.translate(bm, vec=(r["x"], r["y"] * CFG["island"]["squashY"], 0.0), verts=bm.verts)
        for f in bm.faces:
            f.material_index = 0 if rng.random() < 0.6 else 1
        mesh_object("Rock%d" % k, bm, coll, [mat("rock"), mat("rockDark")])


# ------------------------------------------------------------------ cabinet
def build_cabinet(coll, mech, markers):
    C = CFG["cabinet"]
    W, H, D, ch = C["width"], C["height"], C["depth"], C["cheek"]
    x0, y0, z0 = C["x"], C["y"], C["baseZ"]
    front = y0 - D / 2
    tw = C["trimWidth"]

    # Body: dark face block between cream cheeks
    bm = bmesh.new()
    box(bm, x0, y0, z0, W - 2 * ch, D - 0.08, H, 0)
    box(bm, x0 - W / 2 + ch / 2, y0, z0, ch, D, H + C["cheekRise"], 1)  # left cheek
    box(bm, x0 + W / 2 - ch / 2, y0, z0, ch, D, H + C["cheekRise"], 1)  # right cheek
    box(bm, x0, y0 + 0.04, z0 + H, W - 2 * ch, D - 0.12, 0.04, 2)  # roof cap
    mesh_object("CabinetBody", bm, coll, [mat("cabinetFace"), mat("cream"), mat("cabinetFaceDeep")])

    # Face: module frames (orange trim), faders, sockets, speakers, labels
    fz = front - 0.045  # just proud of the face
    rows = [(0.12, 0.9), (1.0, 1.78), (1.88, 2.62)]
    inner_l, inner_r = x0 - W / 2 + ch + 0.08, x0 + W / 2 - ch - 0.08
    modules = [
        (inner_l, x0 + 0.05, rows[2]), (x0 + 0.05, inner_r, rows[2]),
        (inner_l, x0 + 0.6, rows[1]), (x0 + 0.6, inner_r, rows[1]),
        (inner_l, inner_l + 0.95, rows[0]), (inner_l + 0.95, inner_r - 0.95, rows[0]), (inner_r - 0.95, inner_r, rows[0]),
    ]
    trim = bmesh.new()
    for (l, r, (b, t)) in modules:
        l += 0.03
        r -= 0.03
        zb, zt = z0 + b, z0 + t
        box(trim, (l + r) / 2, fz, zb, r - l, 0.03, tw)  # bottom
        box(trim, (l + r) / 2, fz, zt - tw, r - l, 0.03, tw)  # top
        box(trim, l + tw / 2, fz, zb, tw, 0.03, t - b)  # left
        box(trim, r - tw / 2, fz, zb, tw, 0.03, t - b)  # right
        if t > 1.0:  # small orange label plate in the corner
            box(trim, l + 0.17, fz, zt - 0.2, 0.2, 0.03, 0.08)
    mesh_object("Trim", trim, coll, [mat("trim")])

    faders = bmesh.new()
    sockets = bmesh.new()
    rng = random.Random(CFG["seed"] + 7)

    def fader_bank(l, r, b, t, count):
        span = r - l
        for i in range(count):
            u = l + span * (i + 0.5) / count
            box(faders, u, fz + 0.005, z0 + b + 0.1, 0.022, 0.02, (t - b) - 0.22, 1)  # slot
            cap = z0 + b + 0.14 + rng.random() * ((t - b) - 0.36)
            box(faders, u, fz - 0.03, cap, 0.07, 0.07, 0.085, 0)  # cap

    def socket_grid(l, b, cols, rows_, step, orange=()):
        for i in range(cols):
            for j in range(rows_):
                u, v = l + i * step, b + j * step
                m = 1 if (i, j) in orange else 0
                cylinder(sockets, u, fz - 0.02, z0 + v, 0.05, 0.05, segs=8, axis="Y", mat_index=m)
                cylinder(sockets, u, fz - 0.045, z0 + v, 0.024, 0.02, segs=6, axis="Y", mat_index=2)

    # Top row: two fader banks with a pair of sockets on the left
    fader_bank(inner_l + 0.3, x0 - 0.25, *rows[2], 6)
    fader_bank(x0 + 0.25, inner_r - 0.1, *rows[2], 7)
    socket_grid(inner_l + 0.16, rows[2][0] + 0.25, 1, 2, 0.25)
    # Middle row: fader bank, then a patch field
    fader_bank(inner_l + 0.3, x0 + 0.4, *rows[1], 6)
    socket_grid(x0 + 0.85, rows[1][0] + 0.18, 6, 3, 0.2, orange={(1, 0), (2, 0), (1, 1), (2, 1), (4, 2)})
    socket_grid(inner_l + 0.16, rows[1][0] + 0.38, 1, 1, 0.25)
    # Bottom row: speakers left and right, small fader bank and sockets in the middle
    sp = bmesh.new()
    for (l, r) in ((inner_l + 0.12, inner_l + 0.83), (inner_r - 0.83, inner_r - 0.12)):
        box(sp, (l + r) / 2, fz + 0.01, z0 + rows[0][0] + 0.1, r - l, 0.02, rows[0][1] - rows[0][0] - 0.2)
    mesh_object("Speakers", sp, coll, [mat("speaker")])
    fader_bank(inner_l + 1.35, inner_r - 1.35, *rows[0], 5)
    socket_grid(inner_l + 1.12, rows[0][0] + 0.25, 1, 2, 0.32, orange={(0, 0)})
    socket_grid(inner_r - 1.12, rows[0][0] + 0.25, 1, 2, 0.32, orange={(0, 0)})
    mesh_object("Faders", faders, coll, [mat("fader"), mat("cabinetFaceDeep")])
    mesh_object("Sockets", sockets, coll, [mat("socket"), mat("trim"), mat("doorDark")])

    # Cables: low-sided bevelled curves sagging off the face
    for k, c in enumerate(CFG["cables"]):
        cu = bpy.data.curves.new("WS_Cable%d" % k, "CURVE")
        cu.dimensions = "3D"
        cu.bevel_depth = 0.035
        cu.bevel_resolution = 0
        cu.resolution_u = 6
        sp_ = cu.splines.new("BEZIER")
        pts = c["points"]
        sp_.bezier_points.add(len(pts) - 1)
        for i, (u, v) in enumerate(pts):
            sag = 0.12 if 0 < i < len(pts) - 1 else 0.0
            bp = sp_.bezier_points[i]
            bp.co = (x0 + u, fz - 0.06 - sag, z0 + v)
            bp.handle_left_type = bp.handle_right_type = "AUTO"
        cu.materials.append(mat(c["color"]))
        ob = bpy.data.objects.new("WS_Cable%d" % k, cu)
        coll.objects.link(ob)

    # Door and steps on the left cheek (faces -X), near the front
    Dd = CFG["door"]
    side = x0 - W / 2 - 0.005
    door_y = front + 0.32
    dbm = bmesh.new()
    box(dbm, side - 0.012, door_y, z0, 0.02, Dd["width"], Dd["height"], 0)
    box(dbm, side - 0.03, door_y + 0.02, z0 + Dd["height"] * 0.45, 0.02, Dd["width"] * 0.38, Dd["height"] * 0.42, 1)  # lit window
    for s in range(Dd["steps"]):
        depth = Dd["stepDepth"] * (Dd["steps"] - s)
        box(dbm, side - depth / 2 - 0.02, door_y, z0 - 0.02 + 0, depth, Dd["width"] + 0.16, Dd["stepRise"] * (s + 1), 2)
    mesh_object("Door", dbm, coll, [mat("doorDark"), mat("lamp", "lamp", 4.0), mat("cream")])
    empty("MarkerDoorLight", markers, (side - 0.04, door_y + 0.02, z0 + Dd["height"] * 0.66))

    # Roof: antenna, beacon, dish, mast with wind vane and anemometer
    R = CFG["roof"]
    roof_z = z0 + H + 0.04
    rbm = bmesh.new()
    ax = x0 - W / 2 + ch + 0.35
    cylinder(rbm, ax, y0 + 0.25, roof_z + R["antennaHeight"] / 2, 0.035, R["antennaHeight"], segs=6, mat_index=0)
    cylinder(rbm, ax, y0 + 0.25, roof_z + 0.06, 0.09, 0.12, segs=6, mat_index=0)
    bx = ax + 0.55
    cylinder(rbm, bx, y0 - 0.1, roof_z + 0.05, 0.12, 0.1, segs=8, mat_index=0)
    cylinder(rbm, bx, y0 - 0.1, roof_z + 0.18, 0.09, 0.18, segs=8, mat_index=1)  # beacon glass
    dxp = x0 - 0.35
    cylinder(rbm, dxp, y0, roof_z + 0.12, 0.04, 0.24, segs=6, mat_index=0)
    mesh_object("Roof", rbm, coll, [mat("metal"), mat("lamp", "lamp", 6.0)])
    empty("MarkerBeacon", markers, (bx, y0 - 0.1, roof_z + 0.2))
    empty("MarkerAntennaTip", markers, (ax, y0 + 0.25, roof_z + R["antennaHeight"]))

    dish = bmesh.new()
    res = bmesh.ops.create_cone(dish, cap_ends=True, segments=10, radius1=R["dishRadius"], radius2=R["dishRadius"] * 0.35, depth=0.1)
    horn = bmesh.ops.create_cone(dish, cap_ends=True, segments=4, radius1=0.02, radius2=0.02, depth=0.22)
    bmesh.ops.translate(dish, vec=(0, 0, 0.14), verts=horn["verts"])
    allv = res["verts"] + horn["verts"]
    bmesh.ops.rotate(dish, verts=allv, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(-25), 3, "Z") @ Matrix.Rotation(math.radians(-55), 3, "X"))
    bmesh.ops.translate(dish, vec=(dxp, y0, roof_z + 0.36), verts=allv)
    mesh_object("Dish", dish, coll, [mat("dish")])

    # Mast (static) + separately pivoted vane and anemometer (rendered as sprites)
    mx = x0 + W / 2 - ch - 0.65
    my = y0 + 0.05
    mast = bmesh.new()
    cylinder(mast, mx, my, roof_z + R["mastHeight"] / 2, 0.03, R["mastHeight"], segs=6)
    cylinder(mast, mx, my, roof_z + 0.05, 0.09, 0.1, segs=6)
    mesh_object("Mast", mast, coll, [mat("metal")])

    vane_pivot = empty("VanePivot", mech, (mx, my, roof_z + R["mastHeight"] - 0.02))
    vbm = bmesh.new()
    box(vbm, 0, 0, -0.012, 0.62, 0.025, 0.025, 0)  # arm along +X
    head = [vbm.verts.new(p) for p in ((-0.42, 0, 0.0), (-0.26, 0, 0.13), (-0.26, 0, -0.13))]
    vbm.faces.new(head).material_index = 1
    tail = [vbm.verts.new(p) for p in ((0.22, 0, 0.0), (0.36, 0, 0.12), (0.36, 0, -0.12))]
    vbm.faces.new(tail).material_index = 1
    bmesh.ops.solidify(vbm, geom=vbm.faces[:], thickness=0.02)
    vane = mesh_object("Vane", vbm, mech, [mat("metal"), mat("vane")])
    vane.parent = vane_pivot

    anemo_pivot = empty("AnemoPivot", mech, (mx, my, roof_z + R["mastHeight"] - 0.28))
    abm = bmesh.new()
    for k in range(3):
        a = 2 * math.pi * k / 3
        rot = Matrix.Rotation(a, 3, "Z")
        arm = box(abm, 0.14, 0, -0.01, 0.28, 0.02, 0.02, 0)
        bmesh.ops.rotate(abm, verts=arm, cent=(0, 0, 0), matrix=rot)
        cup = cylinder(abm, 0.3, 0, 0.0, 0.07, 0.1, segs=6, axis="Y", mat_index=1, r2=0.03)
        bmesh.ops.rotate(abm, verts=cup, cent=(0, 0, 0), matrix=rot)
    anemo = mesh_object("Anemometer", abm, mech, [mat("metal"), mat("dish")])
    anemo.parent = anemo_pivot
    empty("MarkerVane", markers, vane_pivot.location)
    empty("MarkerAnemometer", markers, anemo_pivot.location)

    # Musical indicator spots on the face (used by the runtime for small lamps)
    empty("MarkerFaceLamp", markers, (x0 - W / 2 + ch + 0.25, front - 0.06, z0 + rows[2][1] - 0.2))
    empty("MarkerFaceFader", markers, (x0 + 0.6, front - 0.06, z0 + rows[2][0] + 0.4))
    empty("MarkerCabinetCentre", markers, (x0, front, z0 + H / 2))
    return roof_z


# ------------------------------------------------------------------ camera
def build_camera(root):
    K = CFG["camera"]
    cam_data = bpy.data.cameras.new("WS_Camera")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = K["orthoScale"]
    cam_data.shift_x = K["shiftX"]
    cam_data.shift_y = K["shiftY"]
    cam_data.clip_start = 0.1
    cam_data.clip_end = 200
    cam = bpy.data.objects.new("WS_Camera", cam_data)
    yaw, pitch = math.radians(K["yawDeg"]), math.radians(K["pitchDeg"])
    target = Vector(K["target"])
    direction = Vector((-math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
    cam.location = target + direction * 40.0
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    root.objects.link(cam)
    bpy.context.scene.camera = cam
    sc = bpy.context.scene
    vp = CFG["viewport"]
    sc.render.resolution_x = vp["logicalWidth"] * vp["scale"]
    sc.render.resolution_y = vp["logicalHeight"] * vp["scale"]
    sc.render.resolution_percentage = 100
    return cam


def add_patch_attribute(rng):
    """Per-face 'ws_patch' (0..1) for the accent mask: snow and frost settle on some facets
    and not others, so accents read as faceted patches instead of a flat white sheet.
    Roofs and cheek tops always take it; rocks often; grass only here and there."""
    for ob in bpy.data.collections["WS_STATIC"].all_objects:
        if ob.type != "MESH":
            continue
        me = ob.data
        attr = me.attributes.get("ws_patch") or me.attributes.new("ws_patch", "FLOAT", "FACE")
        names = [m.name if m else "" for m in me.materials]
        vals = []
        for poly in me.polygons:
            mname = names[poly.material_index] if poly.material_index < len(names) else ""
            r = rng.random()
            if ob.name in ("WS_CabinetBody", "WS_Roof", "WS_Mast", "WS_Door"):
                v = 1.0
            elif "grass" in mname:
                v = 0.4 * r if r < 0.5 else 0.08  # the grass top only ever takes a light dusting
            else:
                v = 1.0 if r < 0.55 else 0.35 * r
            vals.append(v)
        attr.data.foreach_set("value", vals)


def build():
    rng = random.Random(CFG["seed"])
    MATS.clear()
    root = clear_previous()
    static = sub_collection(root, "WS_STATIC")
    mech = sub_collection(root, "WS_MECHANISMS")
    markers = sub_collection(root, "WS_MARKERS")
    build_island(static, rng)
    build_rocks(static, rng)
    build_cabinet(static, mech, markers)
    add_patch_attribute(random.Random(CFG["seed"] + 11))
    build_camera(root)
    return root


def isolate():
    """Render only the WEATHER_SYNTH collection (a factory scene has its own cube, light and camera)."""
    ours = set(bpy.data.collections[ROOT].all_objects)
    for ob in bpy.context.scene.objects:
        ob.hide_render = ob not in ours


def preview(path):
    isolate()
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_shadows = True
    sc.render.film_transparent = True
    sc.view_settings.view_transform = "Standard"
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    build()
    if "--save" in argv:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(argv[argv.index("--save") + 1]), compress=True)
    if "--preview" in argv:
        preview(os.path.abspath(argv[argv.index("--preview") + 1]))
    print("WS_BUILD_OK", len(bpy.data.collections[ROOT].all_objects))
