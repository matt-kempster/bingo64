#!/usr/bin/env python3
"""Generate bingo icon candidates with the Retro Diffusion API.

Needs an API key (https://retrodiffusion.ai — new accounts get ~50 free
credits) in ~/.config/retrodiffusion/key, one line, starts with rdpk-.

Generates pixel-art candidates for the subjects below at 64x64 with
transparent background, then downsizes to the game's 16x16 rgba16 (1-bit
alpha, 5-bit color) with the same pipeline used for the STROOP icons.
Outputs: shots/rd/<name>_<i>.png (raw 64px), shots/rd/icon_<name>_<i>.png
(16px game-format), shots/rd_icons.png (contact sheet).

Usage: python3 tools/rd_icons.py [--dry-run] [--per-subject N]
--dry-run asks the API for cost estimates only (free), generates nothing.
"""
import argparse, base64, io, json, os, sys, urllib.request

from PIL import Image, ImageDraw, ImageFilter

API = "https://api.retrodiffusion.ai/v1/inferences"
KEY_PATH = os.path.expanduser("~/.config/retrodiffusion/key")

# subject -> (prompt, style)  — prompts describe the subject only; the
# style token carries "pixel art". rd_fast__skill_icon suits icon-like
# subjects, rd_fast__game_asset suits characters.
SUBJECTS = {
    "splatoon": ("bright pink paint splatter, glossy ink splash blob, "
                 "energetic dripping splat", "rd_fast__skill_icon"),
    "deaths": ("cartoon skull, white bone skull with big dark eye "
               "sockets, slightly menacing grin", "rd_fast__skill_icon"),
    "whomp": ("big rectangular grey stone slab monster, angry cracked "
              "stone face with red eyes and open mouth, small white "
              "gloved hands, front view", "rd_fast__game_asset"),
    "skeeter": ("cute cartoon water strider insect, round teal body, "
                "googly eyes, four thin legs with yellow feet",
                "rd_fast__game_asset"),
}

def call(key, payload):
    req = urllib.request.Request(
        API, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "X-RD-Token": key})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())

def to_icon(im, alpha_thresh=100):
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)
    w, h = im.size
    side = max(w, h)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - w) // 2, (side - h) // 2))
    icon = sq.resize((16, 16), Image.LANCZOS)
    rgb = icon.convert("RGB").filter(
        ImageFilter.UnsharpMask(radius=1, percent=120, threshold=0))
    icon = Image.merge("RGBA", (*rgb.split(), icon.split()[3]))
    px = icon.load()
    for y in range(16):
        for x in range(16):
            r, g, b, a = px[x, y]
            if a < alpha_thresh:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = ((r >> 3) << 3, (g >> 3) << 3, (b >> 3) << 3, 255)
    return icon

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--per-subject", type=int, default=2)
    args = ap.parse_args()

    if not os.path.exists(KEY_PATH):
        sys.exit(f"no API key at {KEY_PATH} — sign up at retrodiffusion.ai "
                 "and save the rdpk- key there (one line)")
    key = open(KEY_PATH).read().strip()

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(repo, "shots", "rd")
    os.makedirs(outdir, exist_ok=True)

    results = []  # (subject, index, raw image, icon)
    for name, (prompt, style) in SUBJECTS.items():
        payload = {
            "prompt": prompt, "prompt_style": style,
            "width": 64, "height": 64,
            "num_images": args.per_subject,
            "remove_bg": True, "seed": 64,
        }
        if args.dry_run:
            est = call(key, {**payload, "check_cost": True})
            print(f"{name}: estimated cost {est}")
            continue
        resp = call(key, payload)
        print(f"{name}: cost={resp.get('balance_cost')} "
              f"remaining={resp.get('remaining_balance')}")
        for i, b64 in enumerate(resp.get("base64_images", [])):
            if "," in b64:
                b64 = b64.split(",", 1)[1]
            raw = Image.open(io.BytesIO(base64.b64decode(b64))).convert("RGBA")
            raw.save(os.path.join(outdir, f"{name}_{i}.png"))
            icon = to_icon(raw)
            icon.save(os.path.join(outdir, f"icon_{name}_{i}.png"))
            results.append((name, i, raw, icon))
    if args.dry_run or not results:
        return

    PAD, CW, S = 8, 130, 8
    cols = max(r[1] for r in results) + 1
    names = list(dict.fromkeys(r[0] for r in results))
    sheet = Image.new("RGBA",
                      (CW + (128 + 16 * S + PAD * 2) * cols + PAD,
                       (128 + PAD) * len(names) + PAD), (40, 40, 48, 255))
    d = ImageDraw.Draw(sheet)
    for row, name in enumerate(names):
        y = PAD + row * (128 + PAD)
        d.text((PAD, y + 56), name, fill=(255, 255, 255, 255))
        for (n, i, raw, icon) in results:
            if n != name:
                continue
            x = CW + i * (128 + 16 * S + PAD * 2)
            big = raw.resize((128, 128), Image.NEAREST)
            sheet.alpha_composite(big, (x, y))
            tile = Image.new("RGBA", (16 * S, 16 * S), (10, 10, 30, 255))
            tile.alpha_composite(icon.resize((16 * S, 16 * S), Image.NEAREST))
            sheet.alpha_composite(tile, (x + 128 + PAD, y))
    sheet.convert("RGB").save(os.path.join(repo, "shots", "rd_icons.png"))
    print("wrote shots/rd_icons.png")

if __name__ == "__main__":
    main()
