# BINGO64 splash logo generator

`gen_logo.py` builds the 3D "BINGO 64" title-screen logo and exports it to
`levels/intro/bingo_logo/` as an SM64 decomp display list.

Requirements (not vendored):
- Blender 3.6 LTS headless + the Fast64 addon (enabled in user prefs)
- The "Typeface Mario 64" font (fontspace f60208; shareware/non-commercial,
  so it is NOT committed) at the path in FONT_PATH

Run:

    blender -b --python gen_logo.py                 # export
    LOGO_PREVIEW=1 blender -b --python gen_logo.py  # + preview.png render
    LOGO_GAP=-0.10 blender -b --python gen_logo.py  # override letter overlap

Then copy `model.inc.c` / `header.h` / `*.png` into `levels/intro/bingo_logo/`.

The letter structure mirrors the vanilla logo (bright face, shaded same-hue
bevel, wood bevel, sheared wood extrusion); see comments in the script. The
splash camera lives in `levels/intro/geo.c` (GEO_CAMERA). The exported model
is our own asset — it replaces the decompiled Nintendo logo data, which still
sits unused in `levels/intro/leveldata.c`.
