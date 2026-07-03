#!/usr/bin/env python3
"""Render the flat-vector eco-tree stages to PNG (RGBA, true alpha) and,
optionally, to LVGL v8 C arrays (LV_IMG_CF_TRUE_COLOR_ALPHA, RGB565 LE + A8).

Shapes mirror the hand-authored SVGs in this folder. Drawing is done at a
supersampled resolution and downscaled with LANCZOS for clean anti-aliased
edges and correct alpha. Pure Pillow -> no native deps.

Usage:
    python gen_assets.py            # write PNGs to ./png
    python gen_assets.py --c        # also write LVGL C arrays
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
PNG_DIR = os.path.join(HERE, "png")
# LVGL C arrays are emitted straight into the firmware UI source tree.
UI_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "main", "ui"))

W, H = 320, 360          # target asset size (matches display layout)
SS = 4                   # supersampling factor

# ---- palette ---------------------------------------------------------------
SOIL_DK   = "#5e3f24"
SOIL_LT   = "#7a5230"
GRASS     = "#6cc04a"
TRUNK     = "#8a5a2b"
TRUNK_DK  = "#6f4720"
LEAF_LT   = "#7cc659"
LEAF_MID  = "#5aa83f"
LEAF_DK   = "#3f8a2e"
PETAL     = "#f7b6cf"
PETAL_CTR = "#ffd34d"
FRUIT     = "#e74c3c"
FRUIT_DK  = "#c0392b"
# withered
W_SOIL_DK = "#4a3724"
W_SOIL_MD = "#6b5238"
W_SOIL_LT = "#8a7152"
W_STEM    = "#7a6a4a"
W_LEAF1   = "#9a7b3c"
W_LEAF2   = "#8a6a30"
W_SEED    = "#5e4426"
W_SEED_DK = "#3a2c18"


def cubic(p0, p1, p2, p3, n=48):
    """Sample a cubic Bezier into n points."""
    pts = []
    for i in range(n + 1):
        t = i / n
        mt = 1 - t
        x = (mt**3) * p0[0] + 3*(mt**2)*t*p1[0] + 3*mt*(t**2)*p2[0] + (t**3)*p3[0]
        y = (mt**3) * p0[1] + 3*(mt**2)*t*p1[1] + 3*mt*(t**2)*p2[1] + (t**3)*p3[1]
        pts.append((x, y))
    return pts


class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def s(self, v):
        return v * SS

    def ellipse(self, cx, cy, rx, ry, color):
        self.d.ellipse(
            [self.s(cx - rx), self.s(cy - ry), self.s(cx + rx), self.s(cy + ry)],
            fill=color,
        )

    def circle(self, cx, cy, r, color):
        self.ellipse(cx, cy, r, r, color)

    def stroke(self, start, segs, width, color):
        """segs: list of (c1, c2, end) cubic segments. Round caps + curve joints."""
        pts = [start]
        cur = start
        for c1, c2, end in segs:
            pts += cubic(cur, c1, c2, end)[1:]
            cur = end
        spts = [(self.s(x), self.s(y)) for x, y in pts]
        self.d.line(spts, fill=color, width=int(self.s(width)), joint="curve")
        r = width / 2.0
        for cx, cy in (start, cur):
            self.circle(cx, cy, r, color)

    def fillpath(self, start, segs, color):
        """Closed filled path from cubic segments -> polygon."""
        pts = [start]
        cur = start
        for c1, c2, end in segs:
            pts += cubic(cur, c1, c2, end)[1:]
            cur = end
        self.d.polygon([(self.s(x), self.s(y)) for x, y in pts], fill=color)

    def rrect(self, x, y, w, h, r, color):
        self.d.rounded_rectangle(
            [self.s(x), self.s(y), self.s(x + w), self.s(y + h)],
            radius=self.s(r), fill=color,
        )

    def finish(self):
        return self.img.resize((self.w, self.h), Image.LANCZOS)


def ground(c, dry=False):
    if dry:
        c.ellipse(160, 330, 120, 26, W_SOIL_DK)
        c.ellipse(160, 322, 120, 22, W_SOIL_MD)
        c.ellipse(160, 313, 116, 13, W_SOIL_LT)
    else:
        c.ellipse(160, 330, 120, 26, SOIL_DK)
        c.ellipse(160, 322, 120, 22, SOIL_LT)
        c.ellipse(160, 312, 116, 14, GRASS)


def wide_ground(c):
    c.ellipse(160, 332, 128, 28, SOIL_DK)
    c.ellipse(160, 324, 128, 24, SOIL_LT)
    c.ellipse(160, 314, 124, 15, GRASS)


def mature_canopy(c):
    """Branches + full canopy shared by stages 4/5/6."""
    c.stroke((160, 318), [((152, 260), (152, 210), (160, 176))], 22, TRUNK)
    c.stroke((158, 226), [((140, 214), (126, 206), (110, 202))], 11, TRUNK)
    c.stroke((162, 238), [((182, 226), (198, 220), (214, 216))], 11, TRUNK)
    c.stroke((159, 200), [((150, 188), (144, 178), (140, 168))], 9, TRUNK)
    c.circle(160, 150, 62, LEAF_DK)
    c.circle(108, 186, 46, LEAF_DK)
    c.circle(212, 186, 46, LEAF_DK)
    c.circle(160, 120, 48, LEAF_MID)
    c.circle(124, 156, 40, LEAF_MID)
    c.circle(196, 156, 40, LEAF_MID)
    c.circle(160, 138, 40, LEAF_LT)


# ---- stages ----------------------------------------------------------------
def stage0_withered():
    c = Canvas()
    ground(c, dry=True)
    for st, sg in [((120, 312), [((128, 320), (124, 326), (124, 326))]),  # crack stubs (approx)
                   ]:
        pass
    # cracks
    c.stroke((120, 312), [((124, 316), (128, 320), (124, 326))], 2, W_SOIL_DK)
    c.stroke((196, 314), [((190, 321), (190, 321), (195, 326))], 2, W_SOIL_DK)
    c.stroke((160, 316), [((161, 320), (162, 324), (162, 324))], 2, W_SOIL_DK)
    # drooping dead stem
    c.stroke((160, 312), [((161, 300), (166, 294), (174, 292))], 5, W_STEM)
    c.fillpath((174, 292), [((182, 290), (188, 294), (189, 302)),
                            ((180, 304), (174, 300), (174, 292))], W_LEAF1)
    c.fillpath((168, 296), [((162, 300), (158, 306), (160, 314)),
                            ((167, 310), (170, 304), (168, 296))], W_LEAF2)
    c.ellipse(158, 311, 7, 5, W_SEED)
    return c.finish()


def stage1_seed():
    c = Canvas()
    ground(c)
    c.stroke((160, 312), [((158, 296), (158, 288), (160, 278))], 6, LEAF_MID)
    c.fillpath((160, 286), [((146, 280), (138, 286), (136, 296)),
                            ((150, 298), (158, 294), (160, 286))], LEAF_LT)
    c.fillpath((160, 286), [((174, 280), (182, 286), (184, 296)),
                            ((170, 298), (162, 294), (160, 286))], LEAF_LT)
    c.ellipse(160, 310, 7, 5, TRUNK)
    return c.finish()


def stage2_seedling():
    c = Canvas()
    ground(c)
    c.stroke((160, 314), [((157, 280), (157, 258), (160, 232))], 8, TRUNK_DK)
    c.fillpath((160, 270), [((140, 262), (128, 268), (124, 282)),
                            ((146, 286), (158, 280), (160, 270))], LEAF_MID)
    c.fillpath((160, 270), [((180, 262), (192, 268), (196, 282)),
                            ((174, 286), (162, 280), (160, 270))], LEAF_LT)
    c.fillpath((160, 246), [((144, 240), (134, 246), (131, 258)),
                            ((150, 261), (159, 255), (160, 246))], LEAF_MID)
    c.fillpath((160, 246), [((176, 240), (186, 246), (189, 258)),
                            ((170, 261), (161, 255), (160, 246))], LEAF_LT)
    c.circle(160, 230, 12, LEAF_LT)
    return c.finish()


def stage3_sapling():
    c = Canvas()
    ground(c)
    c.stroke((160, 316), [((156, 270), (156, 230), (160, 200))], 14, TRUNK)
    c.stroke((159, 244), [((148, 236), (140, 230), (132, 226))], 7, TRUNK)
    c.stroke((161, 252), [((172, 244), (180, 240), (188, 236))], 7, TRUNK)
    c.circle(160, 188, 46, LEAF_DK)
    c.circle(128, 206, 34, LEAF_MID)
    c.circle(192, 206, 34, LEAF_MID)
    c.circle(160, 172, 34, LEAF_LT)
    c.circle(142, 196, 26, LEAF_LT)
    c.circle(180, 196, 26, LEAF_LT)
    return c.finish()


def stage4_mature():
    c = Canvas()
    wide_ground(c)
    mature_canopy(c)
    c.circle(138, 132, 28, LEAF_LT)
    c.circle(184, 132, 28, LEAF_LT)
    return c.finish()


def stage5_flowering():
    c = Canvas()
    wide_ground(c)
    mature_canopy(c)
    blossoms = [(126, 128), (196, 132), (160, 108), (112, 176), (208, 178), (150, 156)]
    for x, y in blossoms:
        for dx, dy in [(-7, 0), (7, 0), (0, -7), (0, 7)]:
            c.circle(x + dx, y + dy, 5, PETAL)
    for x, y in blossoms:
        c.circle(x, y, 4, PETAL_CTR)
    return c.finish()


def fruiting_canopy(c):
    """Mature canopy decorated with fruits (no ground)."""
    mature_canopy(c)
    fruits = [(126, 132), (196, 136), (160, 112), (112, 180),
              (208, 182), (150, 160), (178, 170)]
    for x, y in fruits:
        c.circle(x, y, 9, FRUIT)
    for x, y in fruits:
        c.circle(x - 3, y + 3, 3, FRUIT_DK)
    for x, y in [(126, 123), (196, 127), (160, 103)]:
        c.rrect(x - 2, y - 3, 4, 6, 2, LEAF_DK)


def stage6_fruiting():
    c = Canvas()
    wide_ground(c)
    fruiting_canopy(c)
    return c.finish()


# ---- full-screen meadow background ------------------------------------------
# 800x480 opaque background for the whole AI view. Left half (behind the
# growing tree) is a pale, sparse "delicate" grass with a soft light halo;
# it blends smoothly into a denser meadow on the right (the orchard side).
# Screen edges get a slight blur so the scene fades out softly.
MEADOW_W, MEADOW_H = 800, 480
MEADOW_SS = 2            # supersampling for the meadow (full screen -> keep it light)
GRASS_LT   = "#7fd35c"
GRASS_DK   = "#5db63f"
GRASS_SOFT = "#b7e39c"   # pale base on the left
SOFT_LT    = "#cdeeb4"
SOFT_DK    = "#a3d489"
HALO       = "#ddf3c9"   # light spot behind the growing tree


def _hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def _lerp_color(c1, c2, t):
    a, b = _hex_rgb(c1), _hex_rgb(c2)
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def meadow_bg():
    ss = MEADOW_SS
    w, h = MEADOW_W * ss, MEADOW_H * ss

    # Base: horizontal blend, pale grass (left) -> regular grass (right).
    # Ramp runs from 20% to 75% of the width so both ends are uniform.
    ramp = [min(255, max(0, int(255 * (x / w - 0.20) / 0.55))) for x in range(w)]
    grad_row = Image.new("L", (w, 1))
    grad_row.putdata(ramp)
    grad = grad_row.resize((w, h))
    img = Image.composite(
        Image.new("RGB", (w, h), GRASS),
        Image.new("RGB", (w, h), GRASS_SOFT),
        grad,
    )

    # Soft light halo behind the growing tree (left half, slightly above center).
    halo_mask = Image.new("L", (100, 60), 0)
    ImageDraw.Draw(halo_mask).ellipse([4, 8, 48, 52], fill=120)
    halo_mask = halo_mask.filter(ImageFilter.GaussianBlur(9)).resize((w, h))
    img = Image.composite(Image.new("RGB", (w, h), HALO), img, halo_mask)

    d = ImageDraw.Draw(img)
    rng = random.Random(7)

    def blend_t(x):
        return ramp[min(w - 1, max(0, int(x)))] / 255.0

    # Darker patches, denser toward the right.
    for _ in range(30):
        x, y = rng.uniform(20, MEADOW_W - 20), rng.uniform(20, MEADOW_H - 20)
        t = blend_t(x * ss)
        if rng.random() > 0.30 + 0.70 * t:
            continue
        rx, ry = rng.uniform(10, 22), rng.uniform(5, 10)
        col = _lerp_color(SOFT_DK, GRASS_DK, t)
        d.ellipse([(x - rx) * ss, (y - ry) * ss, (x + rx) * ss, (y + ry) * ss], fill=col)

    # Blade tufts: sparse and pale on the left, denser and stronger on the right.
    for _ in range(150):
        x, y = rng.uniform(12, MEADOW_W - 12), rng.uniform(16, MEADOW_H - 8)
        t = blend_t(x * ss)
        if rng.random() > 0.25 + 0.75 * t:
            continue
        lt = _lerp_color(SOFT_LT, GRASS_LT, t)
        dk = _lerp_color(SOFT_DK, GRASS_DK, t)
        wdt = int(2.5 * ss)
        for c1, c2, end, col in [
            ((x - 3, y - 5), (x - 4, y - 8), (x - 5, y - 11), lt),
            ((x + 1, y - 5), (x + 1, y - 9), (x, y - 13), lt),
            ((x + 4, y - 4), (x + 5, y - 7), (x + 7, y - 10), dk),
        ]:
            pts = [(px * ss, py * ss) for px, py in cubic((x, y), c1, c2, end, n=12)]
            d.line(pts, fill=col, width=wdt, joint="curve")

    # Light speckles.
    for _ in range(90):
        x, y = rng.uniform(8, MEADOW_W - 8), rng.uniform(8, MEADOW_H - 8)
        t = blend_t(x * ss)
        if rng.random() > 0.30 + 0.70 * t:
            continue
        col = _lerp_color(SOFT_LT, GRASS_LT, t)
        r = 1.6 * ss
        d.ellipse([x * ss - r, y * ss - r, x * ss + r, y * ss + r], fill=col)

    final = img.resize((MEADOW_W, MEADOW_H), Image.LANCZOS)

    # Slight blur at the screen edges (fades the scene out softly).
    blurred = final.filter(ImageFilter.GaussianBlur(3))
    edge = Image.new("L", final.size, 255)
    ImageDraw.Draw(edge).rectangle([56, 56, MEADOW_W - 56, MEADOW_H - 56], fill=0)
    edge = edge.filter(ImageFilter.GaussianBlur(28))
    final = Image.composite(blurred, final, edge)

    return final.convert("RGBA")


STAGES = [
    ("eco_tree_0_withered", stage0_withered),
    ("eco_tree_1_seed", stage1_seed),
    ("eco_tree_2_seedling", stage2_seedling),
    ("eco_tree_3_sapling", stage3_sapling),
    ("eco_tree_4_mature", stage4_mature),
    ("eco_tree_5_flowering", stage5_flowering),
    ("eco_tree_6_fruiting", stage6_fruiting),
]

def orchard_tree():
    """Small fruiting tree for the orchard: no ground mound (it stands directly
    on the meadow), cropped tight and pre-scaled (no runtime zoom)."""
    c = Canvas()
    fruiting_canopy(c)
    img = c.finish()
    img = img.crop(img.getbbox())
    h = 72
    w = round(img.width * h / img.height)
    return img.resize((w, h), Image.LANCZOS)


# Standalone assets emitted alongside the stages (not part of eco_tree_stages).
# Third field: emit with alpha channel (True) or opaque RGB565 only (False).
EXTRAS = [
    ("eco_meadow_bg", meadow_bg, False),
    ("eco_tree_orchard", orchard_tree, True),
]


def rgb565_le_a8(img):
    """Return bytes: per pixel 2B RGB565 little-endian + 1B alpha."""
    px = img.load()
    out = bytearray()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out.append(v & 0xFF)
            out.append((v >> 8) & 0xFF)
            out.append(a)
    return bytes(out)


def rgb565_le(img):
    """Return bytes: per pixel 2B RGB565 little-endian (opaque, no alpha)."""
    px = img.load()
    out = bytearray()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = px[x, y][:3]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out.append(v & 0xFF)
            out.append((v >> 8) & 0xFF)
    return bytes(out)


def emit_c(name, img, fh, alpha=True):
    data = rgb565_le_a8(img) if alpha else rgb565_le(img)
    cf = "LV_IMG_CF_TRUE_COLOR_ALPHA" if alpha else "LV_IMG_CF_TRUE_COLOR"
    fh.write(f"const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{\n")
    for i in range(0, len(data), 16):
        fh.write("    " + "".join(f"0x{b:02x}," for b in data[i:i+16]) + "\n")
    fh.write("};\n\n")
    fh.write(f"const lv_img_dsc_t {name} = {{\n")
    fh.write("    .header.always_zero = 0,\n")
    fh.write("    .header.reserved = 0,\n")
    fh.write(f"    .header.w = {img.width},\n")
    fh.write(f"    .header.h = {img.height},\n")
    fh.write(f"    .data_size = sizeof({name}_map),\n")
    fh.write(f"    .header.cf = {cf},\n")
    fh.write(f"    .data = {name}_map,\n")
    fh.write("};\n\n")


def main():
    os.makedirs(PNG_DIR, exist_ok=True)
    stage_imgs = []
    extra_imgs = []
    for name, fn in STAGES:
        img = fn()
        img.save(os.path.join(PNG_DIR, name + ".png"))
        stage_imgs.append((name, img))
        print("wrote", name + ".png")
    for name, fn, alpha in EXTRAS:
        img = fn()
        img.save(os.path.join(PNG_DIR, name + ".png"))
        extra_imgs.append((name, img, alpha))
        print("wrote", name + ".png")

    if "--c" in sys.argv:
        out_c = os.path.join(UI_DIR, "eco_tree_images.c")
        out_h = os.path.join(UI_DIR, "eco_tree_images.h")
        with open(out_c, "w") as fh:
            fh.write("// Auto-generated by assets/tree/gen_assets.py -- do not edit by hand.\n")
            fh.write('#include "eco_tree_images.h"\n\n')
            fh.write("#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n")
            for name, img in stage_imgs:
                emit_c(name, img, fh)
            for name, img, alpha in extra_imgs:
                emit_c(name, img, fh, alpha=alpha)
        with open(out_h, "w") as fh:
            fh.write("// Auto-generated by assets/tree/gen_assets.py -- do not edit by hand.\n")
            fh.write("#ifndef ECO_TREE_IMAGES_H\n#define ECO_TREE_IMAGES_H\n\n")
            fh.write('#include "lvgl.h"\n\n')
            fh.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
            fh.write(f"#define ECO_TREE_STAGE_COUNT {len(stage_imgs)}\n\n")
            for name, _ in stage_imgs:
                fh.write(f"extern const lv_img_dsc_t {name};\n")
            for name, _, _ in extra_imgs:
                fh.write(f"extern const lv_img_dsc_t {name};\n")
            fh.write("\n// Stages ordered worst -> best (index 0 = withered, last = fruiting).\n")
            fh.write("extern const lv_img_dsc_t *const eco_tree_stages[ECO_TREE_STAGE_COUNT];\n\n")
            fh.write("#ifdef __cplusplus\n}\n#endif\n\n")
            fh.write("#endif // ECO_TREE_IMAGES_H\n")
        # append the ordered lookup table to the .c
        with open(out_c, "a") as fh:
            fh.write("const lv_img_dsc_t *const eco_tree_stages[ECO_TREE_STAGE_COUNT] = {\n")
            for name, _ in stage_imgs:
                fh.write(f"    &{name},\n")
            fh.write("};\n")
        print("wrote", out_c)
        print("wrote", out_h)


if __name__ == "__main__":
    main()
