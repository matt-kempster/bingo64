# BINGO64 splash logo generator

`gen_logo.py` builds the 3D "BINGO 64" title-screen logo and exports it to
`levels/intro/bingo_logo/` as an SM64 decomp display list. This doc covers
both how to run it and how to *iterate* on it — the workflow below was used
to design the current logo and is the fast path for future tweaks.

## One-time setup (already done on Matt's WSL box)

- Blender 3.6 LTS, headless: `/home/matt/opt/blender-3.6.19-linux-x64/blender`
- Fast64 addon cloned at `/home/matt/b64-logo/fast64`, symlinked into
  `~/.config/blender/3.6/scripts/addons/` and enabled in userprefs
- The "Typeface Mario 64" font (fontspace f60208; shareware/non-commercial,
  so it is NOT committed) at the path in `FONT_PATH`
- Working copies of this script + textures live in `/home/matt/b64-logo/`

## Running

    blender -b --python gen_logo.py                 # export
    LOGO_PREVIEW=1 blender -b --python gen_logo.py  # + preview.png render
    LOGO_GAP=-0.10 blender -b --python gen_logo.py  # override letter overlap

Then copy `model.inc.c` / `header.h` into `levels/intro/bingo_logo/`.
No textures are copied — the model references the vanilla intro textures
(`intro_seg7_texture_0/1`) directly, since `model.inc.c` is `#include`d
into `leveldata.c` (same translation unit, statics visible).

## The iteration workflow (fast → slow)

1. **Preview loop (~10s)**: `LOGO_PREVIEW=1` renders `preview.png` with a
   Workbench flat-vertex-color camera that provably matches the in-game
   view (same position/FOV as the GEO_CAMERA below). Use for layout,
   spacing, shape, and vertex-color work. It does NOT show textures.
2. **In-game loop (~2min)**: copy exports into the repo, rsync to the
   build copy (`/home/matt/b64-refresh`), `make VERSION=us -j8`, then
   `bash /home/matt/b64-logo/capture.sh` — boots the game under WSLg and
   screenshots the splash at ~1.5/2.2/2.9s (the logo holds at scale 1.0
   from ~0.7s to ~2.5s after the screen appears). Use for texture and
   final-look verification.
3. **Taste sweeps**: for aesthetic knobs (letter overlap, skinniness),
   render several preview values (`LOGO_GAP=... in a loop`), montage them
   into one labeled grid with PIL, and let a human pick; binary-search
   around their choice. This converged in 2 rounds for letter overlap.

## Knobs (all in gen_logo.py)

- `LETTERS` — per-letter color assignments (current palette: B red,
  I blue, N green, G yellow, O red / 6 blue, 4 green — chosen by eye
  after several live A/Bs; earlier variants: 4 yellow, I yellow + G blue —
  both rejected)
- `GAP_FRAC` (env `LOGO_GAP`) — letter overlap; -0.055 chosen by 2-round
  human binary search; `Z_STAGGER` interleaves overlapping letters in depth
- `SIZE_ROW0/1`, `ROW0_CY/ROW1_CY` — row sizes/positions ("64" size 16,
  tucked close under BINGO)
- `MAX_W/MAX_H/CENTER_X/CENTER_Y` — overall footprint (matches the
  vanilla logo's measured footprint, nudged up-left to clear the © line)
- `SHEAR_Y` — extrusion slides down per unit depth (viewed-from-below look)
- `LAYER_DEFS` — the per-letter layer stack (see below)
- Splash camera: `GEO_CAMERA` in `levels/intro/geo.c`
  (currently pos (0,-550,2650) → focus (0,150,0), i.e. close + below)

## How the model is structured (mirrors vanilla, verified from its data)

Each letter is 4 stacked layers, drawn strictly back-to-front:
wood extrusion → wood bevel ring → same-hue dark bevel ring → bright face.
The vanilla logo does exactly this — check `levels/intro/leveldata.c`
vertex colors: each hue appears bright + darkened (the inner bevel), and
the wood group is grays over the grain texture (lit from above).

## Hard-won gotchas (read before touching the pipeline)

- **Exporter axis convention**: fast64's DL export maps blender→game as
  R(-90°,X): blender +Z → game +Y (up), blender -Y → game +Z (toward
  camera). A single +90°X rotation of flat text is correct. Do NOT add
  mirrors or winding flips "to fix" orientation — that inverts depth and
  you end up staring into the open back of the model (cost us hours).
- **Splash-screen RDP state**: the logo DL runs with G_LIGHTING on,
  G_CULL_BACK on, and env alpha 0. Fast64's generic material DLs render
  invisible or wrong here. The script therefore *replaces* them in
  post-processing with a clone of the vanilla `intro_seg7_dl_logo` state
  block (MODULATEI combiner, lighting+culling cleared, same tile setup).
- **Draw-order safety**: each layer gets its own material slot → own tri
  DL; the post-process identifies groups *geometrically* (extrusion = big
  z-span; flats ordered by mean z) because fast64 shuffles slot order.
- **Blender API traps**: creating any mesh layer invalidates previously
  fetched layer references (re-fetch by name); negative curve `offset`
  self-intersects on angular glyphs (the '4') — use bmesh `inset_region`
  for insets instead.
- Textures looked wrong at most scales: faces use ~1 texture repeat per
  glyph (0.12 planar), wood ~0.14/0.35 — matched to vanilla ST density.

## Shelved experiments

Branch `logo-experiments` (pushed to origin) holds a working version with
skinny letters (mesh-inset `THIN` knob — liked), a sloped bevel skirt and
vanilla-measured face/ring colors (both rejected). The pieces are
separable; cherry-pick the THIN chunk if skinnier letters are wanted.
