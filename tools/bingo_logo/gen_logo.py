# Generate the BINGO64 title logo mesh and export it as an SM64 decomp
# display list via Fast64. Run headless:
#   blender -b --python gen_logo.py            (export only)
#   LOGO_PREVIEW=1 blender -b --python ...     (also render preview.png)
#
# Vanilla splash-logo letter structure (corroborated from intro leveldata):
#   bright front face
#   -> same-hue darker bevel ring (slightly fatter glyph, slightly deeper)
#   -> wood bevel ring (fatter again, deeper again)
#   -> wood extrusion, sheared downward (logo viewed from slightly below)
# Wood is the 32x32 grain texture modulated by gray vertex shading.
import bpy
import bmesh
import math
import os
from mathutils import Vector

FONT_PATH = "/home/matt/b64-logo/TypefaceMario64-ywA93.otf"
TEX_PATH = "/home/matt/b64-logo/logo_shine.png"  # copy of levels/intro/1.rgba16.png
WOOD_PATH = "/home/matt/b64-logo/logo_wood.png"  # copy of levels/intro/0.rgba16.png
OUT_DIR = "/home/matt/b64-logo/export"
DL_NAME = "bingo_logo"

# Face/bevel color pairs lifted straight from the vanilla logo's vertex
# data (face = bright front, ring = saturated bevel outline), per hue:
RED    = ((1.000, 0.184, 0.184), (0.843, 0.000, 0.000))
BLUE   = ((0.396, 0.498, 1.000), (0.125, 0.224, 0.898))
YELLOW = ((1.000, 1.000, 0.224), (0.730, 0.730, 0.000))
GREEN  = ((0.000, 0.945, 0.000), (0.000, 0.627, 0.000))
LETTERS = [
    # (char, row, (face, ring))
    ("B", 0, RED),
    ("I", 0, BLUE),
    ("N", 0, YELLOW),
    ("G", 0, GREEN),
    ("O", 0, RED),
    ("6", 1, BLUE),
    ("4", 1, GREEN),
]
SIZE_ROW0 = 10.0        # BINGO letter size (blender units; x100 = sm64 units)
SIZE_ROW1 = 16.0        # 64 size
# gap between letters as a fraction of size; negative = overlap.
# override per run: LOGO_GAP=-0.10 blender -b --python gen_logo.py
GAP_FRAC = float(os.environ.get("LOGO_GAP", "-0.055"))
# per-letter depth stagger (flat units, cycled) so overlapping letters
# interleave in front of / behind each other like the vanilla logo
Z_STAGGER = [0.0, -0.55, -0.15, -0.45, -0.3]
ROW0_CY = 7.0           # BINGO row center height (flat-space Y)
ROW1_CY = -2.8          # 64 row center height
EXTRUDE_FRAC = 0.16     # extrusion depth as fraction of size
SHEAR_Y = 0.5           # how far the extrusion slides down per unit depth

# layer stack: (kind, glyph fatten as size-frac, z depth as size-frac)
# THIN insets the bright face inward from the raw glyph (skinnier letters);
# ring/wood step outward from it by the same rims as before.
# override per run: LOGO_THIN=0.035 blender -b --python gen_logo.py
THIN = float(os.environ.get("LOGO_THIN", "0.028"))
# the same-hue bevel is a real sloped skirt built from the front face's
# inset rim (see bmesh step) — not a separate flat layer
RING_DEPTH_FRAC = 0.014
LAYER_DEFS = [
    ("front", 0.000,  0.000),
    ("step",  0.050 - THIN, -0.060),
    ("ext",   0.050 - THIN, -0.060),
]
LAYER_ID = {"ext": 0, "step": 1, "front": 2}

# Vanilla logo footprint (measured from the US DLs): x -1190..1414,
# y -381..864 — i.e. ~2600x1245 units centered near (+110, +240).
MAX_W = 26.0
MAX_H = 12.4
CENTER_X = 0.2
CENTER_Y = 4.0

def srgb_to_linear(c):
    return tuple((v / 12.92) if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c)

def lerp3(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

# --- clean scene ---
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()

font = bpy.data.fonts.load(FONT_PATH)

made = []     # flat list: (obj, row, color, kind)
letters = []  # per letter: dict(objs=[(obj, kind)...], row=..)
for letter_i, (ch, row, color) in enumerate(LETTERS):
    size = SIZE_ROW1 if row == 1 else SIZE_ROW0
    z_stag = Z_STAGGER[letter_i % len(Z_STAGGER)]
    group = []
    for kind, off_frac, z_frac in LAYER_DEFS:
        curve = bpy.data.curves.new(f"txt_{ch}_{kind}", "FONT")
        curve.body = ch
        curve.font = font
        curve.size = size
        curve.offset = size * off_frac
        curve.resolution_u = 6
        if kind == "ext":
            curve.extrude = size * EXTRUDE_FRAC / 2  # blender extrudes both ways
        obj = bpy.data.objects.new(f"letter_{ch}_{kind}", curve)
        bpy.context.collection.objects.link(obj)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.convert(target="MESH")
        obj = bpy.context.view_layer.objects.active
        obj.select_set(False)
        # flat layers sit at their depth plane; the extrusion's FRONT edge
        # starts at the step plane and goes back from there
        obj.location.z = z_stag + size * z_frac - (size * EXTRUDE_FRAC / 2 if kind == "ext" else 0)
        group.append((obj, kind))
        made.append((obj, row, color, kind))
    letters.append({"objs": group, "row": row})

def bbox(obj):
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector((min(p[i] for p in pts) for i in range(3)))
    hi = Vector((max(p[i] for p in pts) for i in range(3)))
    return lo, hi

# --- lay out rows in flat XY space using the widest flat layer (step) ---
for row, cy in ((0, ROW0_CY), (1, ROW1_CY)):
    row_letters = [l for l in letters if l["row"] == row]
    size = SIZE_ROW1 if row == 1 else SIZE_ROW0
    gap = size * GAP_FRAC
    x = 0.0
    for l in row_letters:
        step_obj = next(o for o, k in l["objs"] if k == "step")
        lo, hi = bbox(step_obj)
        loc_x = x - lo.x
        loc_y = cy - (lo.y + hi.y) / 2
        for o, _ in l["objs"]:
            o.location.x = loc_x
            o.location.y = loc_y
        x += (hi.x - lo.x) + gap
    total_w = x - gap
    for l in row_letters:
        for o, _ in l["objs"]:
            o.location.x -= total_w / 2

bpy.context.view_layer.update()

# --- fit whole logo into target box, centered ---
los, his = zip(*(bbox(o) for o, _, _, _ in made))
lo = Vector((min(v[i] for v in los) for i in range(3)))
hi = Vector((max(v[i] for v in his) for i in range(3)))
w, h = hi.x - lo.x, hi.y - lo.y
s = min(MAX_W / w, MAX_H / h)
cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
SQUASH_Y = 0.85  # vertical squish: fatter letters at the same width
for o, _, _, _ in made:
    o.location.x = (o.location.x - cx) * s + CENTER_X
    o.location.y = (o.location.y - cy) * s * SQUASH_Y + CENTER_Y
    o.location.z *= s
    o.scale = (s, s * SQUASH_Y, s)
print(f"layout: w={w * s:.1f} h={h * s:.1f} (blender units, x100 = sm64)")

# --- apply transforms, then per-layer cleanup + vertex paint + UVs ---
for o, _, _, _ in made:
    o.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

for o, row, color, kind in made:
    mesh = o.data
    size = SIZE_ROW1 if row == 1 else SIZE_ROW0
    bm = bmesh.new()
    bm.from_mesh(mesh)
    if kind == "front" and THIN > 0:
        # drop back side first, then mesh-inset the glyph fill inward; the
        # rim becomes the same-hue bevel: slant its outer edge backward so
        # it reads as a slope, not a vertical cliff
        backs = [f for f in bm.faces if f.normal.z < -0.85]
        bmesh.ops.delete(bm, geom=backs, context="FACES")
        rim = bmesh.ops.inset_region(bm, faces=list(bm.faces),
                                     thickness=THIN * size * s,
                                     use_even_offset=True)["faces"]
        bev_lay = bm.faces.layers.int.new("is_bevel")
        rim_set = set(rim)
        inner_verts = set()
        for f in bm.faces:
            if f not in rim_set:
                inner_verts.update(f.verts)
        for f in rim:
            f[bev_lay] = 1
            for v in f.verts:
                if v not in inner_verts:
                    v.co.z -= RING_DEPTH_FRAC * size * s
    bmesh.ops.triangulate(bm, faces=bm.faces)
    # no z-buffer on the splash path: painter's order back-to-front. Flat
    # layers keep only their front side; the extrusion keeps only its walls.
    if kind == "ext":
        doomed = [f for f in bm.faces if abs(f.normal.z) > 0.85]
    else:
        doomed = [f for f in bm.faces if f.normal.z < -0.85]
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bm.to_mesh(mesh)
    bm.free()

    lo_, hi_ = bbox(o)
    hgt = max(hi_.y - lo_.y, 1e-6)

    mesh.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="CORNER")
    mesh.color_attributes.new(name="Alpha", type="FLOAT_COLOR", domain="CORNER")
    mesh.uv_layers.new(name="UVMap")
    mesh.attributes.new(name="layer_id", type="INT", domain="FACE")
    # re-fetch: creating a layer invalidates prior layer references
    col_attr = mesh.color_attributes["Col"]
    alp_attr = mesh.color_attributes["Alpha"]
    uv = mesh.uv_layers["UVMap"]
    lay_attr = mesh.attributes["layer_id"]

    face_col, ring_col = color

    bev_attr = mesh.attributes.get("is_bevel")
    for poly in mesh.polygons:
        lay_attr.data[poly.index].value = LAYER_ID[kind]
        is_bevel = bev_attr is not None and bev_attr.data[poly.index].value
        # extrusion walls: wood lit from above (vanilla uses gray shading)
        if kind == "ext":
            ny = poly.normal.y
            gray = 0.95 if ny > 0.4 else (0.50 if ny < -0.4 else 0.72)
        for li in poly.loop_indices:
            vtx = mesh.vertices[mesh.loops[li].vertex_index]
            t = (vtx.co.y - lo_.y) / hgt
            if kind == "front":
                if is_bevel:
                    c = tuple(ch_ * (0.88 + 0.12 * t) for ch_ in ring_col)
                else:
                    c = tuple(ch_ * (0.94 + 0.06 * t) for ch_ in face_col)
                u, v = vtx.co.x * 0.12, vtx.co.y * 0.12
            elif kind == "step":
                g = 0.55 + 0.40 * t
                c = (g, g * 0.97, g * 0.94)
                u, v = vtx.co.x * 0.14, vtx.co.y * 0.14
            else:  # ext
                c = (gray, gray * 0.97, gray * 0.94)
                n = poly.normal
                perim = vtx.co.y if abs(n.x) > abs(n.y) else vtx.co.x
                u, v = perim * 0.14, vtx.co.z * 0.35
            col_attr.data[li].color = (*srgb_to_linear(c), 1.0)
            alp_attr.data[li].color = (1.0, 1.0, 1.0, 1.0)
            uv.data[li].uv = (u, v)

# --- shear: deeper layers slide down, giving the viewed-from-below look ---
for o, _, _, _ in made:
    for v in o.data.vertices:
        v.co.y += v.co.z * SHEAR_Y

# --- stand the logo up. The exporter maps blender->game as R(-90deg,X):
# blender +Z -> game +Y (up), blender -Y -> game +Z (toward camera). So a
# single +90deg X rotation of the flat text (width X, height Y, front +Z)
# lands it upright and facing the camera — no mirrors, no winding flips.
for o, _, _, _ in made:
    o.rotation_euler = (math.radians(90), 0, 0)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# --- join into one object; one material slot per layer so each layer gets
# its own tri DL and the wrapper can draw them strictly back-to-front ---
bpy.context.view_layer.objects.active = made[0][0]
bpy.ops.object.join()
logo = bpy.context.view_layer.objects.active
logo.name = "bingo_logo"

import addon_utils
addon_utils.enable("fast64")
from fast64.fast64_internal.f3d.f3d_material import createF3DMat

mats = []
for i in range(len(LAYER_ID)):
    m = createF3DMat(logo, preset="sm64_vertex_colored_texture")
    # texture per slot only needs to be valid — the post-process wrapper
    # rewrites all texture loads explicitly
    m.f3d_mat.tex0.tex = bpy.data.images.load(WOOD_PATH if i < 2 else TEX_PATH)
    m.f3d_mat.tex0.tex_format = "RGBA16"
    mats.append(m)

lay = logo.data.attributes["layer_id"]
for p in logo.data.polygons:
    p.material_index = lay.data[p.index].value

from collections import Counter
print("DEBUG face counts per layer slot:",
      dict(Counter(p.material_index for p in logo.data.polygons)))

# save the final scene for inspection (Windows: \\wsl$\Ubuntu\home\matt\b64-logo\)
bpy.ops.wm.save_as_mainfile(filepath="/home/matt/b64-logo/bingo_logo.blend")

# fast preview render matching the in-game camera (game +Z == blender -Y side)
if os.environ.get("LOGO_PREVIEW"):
    cam_data = bpy.data.cameras.new("preview_cam")
    cam_data.angle_y = math.radians(45)
    cam_data.sensor_fit = "VERTICAL"
    cam_data.clip_end = 200
    cam = bpy.data.objects.new("preview_cam", cam_data)
    bpy.context.collection.objects.link(cam)
    # mirror the game camera (levels/intro/geo.c GEO_CAMERA): game (x,y,z) maps
    # to blender (x, -z, y) here; game pos (0,-550,2650), focus (0,150,0)
    cam.location = (0, -26.5, -5.5)
    look = Vector((0, 0, 1.5)) - Vector(cam.location)
    cam.rotation_euler = look.to_track_quat("-Z", "Y").to_euler()
    scene = bpy.context.scene
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "VERTEX"
    scene.render.resolution_x = 640
    scene.render.resolution_y = 480
    scene.render.filepath = "/home/matt/b64-logo/preview.png"
    bpy.ops.render.render(write_still=True)
    print("PREVIEW OK")

# --- export ---
scene = bpy.context.scene
scene.fast64.sm64.export_type = "C"
scene.fast64.sm64.blender_to_sm64_scale = 100.0
props = scene.fast64.sm64.combined_export
props.export_header_type = "Custom"
props.custom_export_path = OUT_DIR
props.custom_include_directory = f"levels/intro/{DL_NAME}"
scene.DLExportisStatic = True
scene.saveTextures = True
scene.DLSeparateTextureDef = False
scene.DLincludeChildren = True
scene.DLName = DL_NAME

bpy.ops.object.select_all(action="DESELECT")
logo.select_set(True)
bpy.context.view_layer.objects.active = logo
bpy.ops.object.sm64_export_dl()

# --- post-process: replace fast64's material DLs + master DL with a clone of
# the vanilla intro logo's state setup (the splash screen's RDP state breaks
# fast64's generic materials). Layers are identified geometrically: the
# extrusion has a deep z-span; flat layers sort back-to-front by mean z.
import re as _re
model = f"{OUT_DIR}/{DL_NAME}/model.inc.c"
src = open(model).read()

def group_stats(n):
    body = _re.search(r"Gfx bingo_logo_bingo_logo_mesh_tri_%d\[\] = \{(.*?)\};" % n,
                      src, _re.S).group(1)
    arr = _re.search(r"gsSPVertex\((\w+) \+", body).group(1)
    ab = _re.search(r"Vtx %s\[\d*\] = \{(.*?)\};" % arr, src, _re.S).group(1)
    zs = [int(m.group(3)) for m in
          _re.finditer(r"\{\{ \{(-?\d+), (-?\d+), (-?\d+)\}", ab)]
    return max(zs) - min(zs), sum(zs) / len(zs)

ngroups = len(set(_re.findall(r"Gfx bingo_logo_bingo_logo_mesh_tri_(\d+)\[", src)))
assert ngroups == 3, f"expected 3 tri groups, got {ngroups}"
stats = {n: group_stats(n) for n in range(3)}
ext_n = max(stats, key=lambda n: stats[n][0])
assert stats[ext_n][0] > 40, "extrusion group not found by z-span"
flat = sorted((n for n in stats if n != ext_n), key=lambda n: stats[n][1])
step_n, front_n = flat
print(f"DEBUG wrapper order: ext={ext_n} step={step_n} front={front_n}")

WRAPPER = f"""Gfx bingo_logo_bingo_logo_mesh[] = {{
\tgsDPPipeSync(),
\tgsDPSetCombineMode(G_CC_MODULATEI, G_CC_MODULATEI),
\tgsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT),
\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
\tgsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
\tgsDPTileSync(),
\tgsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD),
\tgsDPSetTileSize(G_TX_RENDERTILE, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),
\tgsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, intro_seg7_texture_0),
\tgsDPLoadSync(),
\tgsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
\tgsSPDisplayList(bingo_logo_bingo_logo_mesh_tri_{ext_n}),
\tgsSPDisplayList(bingo_logo_bingo_logo_mesh_tri_{step_n}),
\tgsDPPipeSync(),
\tgsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, intro_seg7_texture_1),
\tgsDPLoadSync(),
\tgsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
\tgsSPDisplayList(bingo_logo_bingo_logo_mesh_tri_{front_n}),
\tgsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
\tgsDPPipeSync(),
\tgsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
\tgsSPSetGeometryMode(G_LIGHTING),
\tgsSPEndDisplayList(),
}};"""

src = _re.sub(r"Gfx mat_(?:revert_)?bingo_logo[^\[]*\[\] = \{.*?\};\n\n?", "", src, flags=_re.S)
src = _re.sub(r"Gfx bingo_logo_logo_\w+_aligner[^;]*;\n", "", src)
src = _re.sub(r"u8 bingo_logo_logo_\w+\[\] = \{\n\t#include \"[^\"]+\"\n\};\n", "", src)
src, n = _re.subn(r"Gfx bingo_logo_bingo_logo_mesh\[\] = \{.*?\};", WRAPPER, src, flags=_re.S)
assert n == 1, "master DL not found; update patch"
open(model, "w").write(src)
print("EXPORT OK")
