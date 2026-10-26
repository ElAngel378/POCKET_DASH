"""Replaces GDA placeholder art in the object catalog with the user's art (the CHR files in
levels/objects/incoming_art/), group by group. Only the art changes (objects.png): names, ids and
collision stay, so levels keep working.

CHR formats: Game Boy 2bpp (2 bytes a row) or NES (8 bytes plane 0, 8 bytes plane 1); 16 tiles a
row, a 16x16 piece is 2 x 2 tiles, piece "r.c" = piece row r, column c. Colours are ours already
(0 background, 1 fill, 2 black, 3 white outline).

    python tools/apply_user_art.py [--dry]     (--dry: only the review sheet, docs/user_art.png)

Groups:
  1.6 blocks   1_6_objects.chr: each GDA 1.6 block / slab placeholder gets the piece (and flip)
               most like GDA's own art of it (>= 220 of 256 pixels, slabs 165; the redrawn slabs
               by name: PIECES), as tools/import_gda_16.py did on the object-catalog branch
  extras       BREAKABLE_BRICK (1_6_objects 6.0), the 1.6 wave decoration (1p6blocks 2.0)
  slopes       sopeslopeslopeslope.chr: 22 degrees up (0.0, 0.1) and 45 down (0.2), flipped for
               the other 22 / 45 degree slopes (the 66 degree ones: no art yet)
  pulsing      GDP_Pulsing_Obj.chr: the ring, heart, square (0.1..0.3, small 1.1..1.3) for sprites
               39..41 (tools/make_gda_pulse_tiles.py draws them)
  GDA sprites  whole drawings cut into the blocks tools/import_gda_sprites.py made (same cells,
               collision kept): ToE's stepped skylines (GDP_TOE) for the BG squares, the clouds
               (Wavy_Gspikes) for the BG clouds, the 1.6 saws (1p6blocks) for the outline saws and the 4-point star saws
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gda  # noqa: E402
from objects_catalog import ART, load_catalog, write_preview  # noqa: E402

INBOX = ROOT / "levels" / "objects" / "incoming_art"
NES = {"Clutterfunk_Blocks", "Wavy_Gspikes", "sopeslopeslopeslope", "y_did_i_do_this"}

# --- 1.6 blocks (tools/import_gda_16.py on the object-catalog branch) ---
WANTED = re.compile(r"1\.6_(SIDE|CORNER|INS_CRN|DBL|BLOCK|FILLER|SID_INS|SLAB)\w*")
MIN_SCORE = {"block": 220, "slab": 165}
PIECES = [
    (r"1\.6_SLAB_BLOCK_(TOP|BOT)", ["2.4"]), (r"1\.6_SLAB_BLOCK_(LFT|RGT)", ["3.4"]),
    (r"1\.6_SLAB_SIDE_(TOP|BOT)", ["2.6"]), (r"1\.6_SLAB_SIDE_(LFT|RGT)", ["3.6"]),
    (r"1\.6_SLAB_SIDE_H_\w\w", ["2.7"]), (r"1\.6_SLAB_SIDE_V_\w\w", ["3.7"]),
    (r"1\.6_SLAB_SIDE_H_\w\w_ALT", ["4.2"]), (r"1\.6_SLAB_SIDE_V_\w\w_ALT", ["5.2"]),
    (r"1\.6_SLAB_CORNER_H_\w\w", ["4.5"]), (r"1\.6_SLAB_CORNER_V_\w\w", ["5.5"]),
]
# (object name, file, piece, h flip, v flip)
EXTRAS = [
    ("BREAKABLE_BRICK", "1_6_objects", "6.0", 0, 0),
    ("1.6_WAVE_DECO_MIDDLE", "1p6blocks", "2.0", 0, 0),
    ("1.6_WAVE_DECO_MIDDLE_UD", "1p6blocks", "2.0", 0, 1),
]


def pieces(name):
    """{"r.c": 16 rows of 16 colour indices} of a CHR file's non-empty 16x16 pieces"""
    d = (INBOX / (name + ".chr")).read_bytes()
    n = len(d) // 16
    out = {}
    for r in range(n // 32):
        for c in range(8):
            ts = [2 * r * 16 + 2 * c, 2 * r * 16 + 2 * c + 1, (2 * r + 1) * 16 + 2 * c, (2 * r + 1) * 16 + 2 * c + 1]
            if not any(any(d[t * 16:t * 16 + 16]) for t in ts):
                continue
            px = [[0] * 16 for _ in range(16)]
            for q, t in enumerate(ts):
                b = d[t * 16:t * 16 + 16]
                for y in range(8):
                    lo, hi = (b[y], b[y + 8]) if name in NES else (b[2 * y], b[2 * y + 1])
                    for x in range(8):
                        px[(q >> 1) * 8 + y][(q & 1) * 8 + x] = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
            out["%d.%d" % (r, c)] = px
    return out


def flip(a, h, v):
    return [[a[15 - y if v else y][15 - x if h else x] for x in range(16)] for y in range(16)]


def gda_art(tiles):
    """GDA's art of a metatile in our colours, from its own tileset and palette (outline white 3,
    darkest 2, dark 1)"""
    chr_ = (gda.GDA / "graphics" / "tileset.chr").read_bytes()
    pal = (gda.GDA / "graphics" / "tileset.pal").read_bytes()
    to_index = {(255, 255, 255): 3, (8, 16, 57): 2, (16, 24, 99): 1}
    px = [[0] * 16 for _ in range(16)]
    for q, (tid, h, v, _) in enumerate(tiles):
        b = chr_[tid * 32:tid * 32 + 32]
        for y in range(8):
            for x in range(8):
                n = (b[y * 4 + x // 2] >> (4 * (x & 1))) & 15
                c = tuple(pal[n * 3:n * 3 + 3]) if n else None
                px[(q >> 1) * 8 + (7 - y if v else y)][(q & 1) * 8 + (7 - x if h else x)] = to_index.get(c, 0)
    return px


def group_16(cat):
    """{tile id: (art, source)} for the 1.6 block set"""
    ours = pieces("1_6_objects")
    mts = {n: t for n, t, _ in gda.metatiles()}
    out = {}
    for tid, o in sorted(cat.objects.items()):
        g = o.get("gda_name", "")
        if not WANTED.fullmatch(g) or g not in mts:
            continue
        ref = gda_art(mts[g])
        allowed = next((p for r, p in PIECES if re.fullmatch(r, g)), list(ours))
        score, _, k, h, v = max((sum(f[y][x] == ref[y][x] for y in range(16) for x in range(16)), -(h + v), k, h, v)
                                for k in allowed for h in (0, 1) for v in (0, 1) for f in [flip(ours[k], h, v)])
        if score < MIN_SCORE["slab" if "SLAB" in g else "block"]:
            print("  no piece for %-28s (best %s, %d)" % (g, k, score))
            continue
        out[tid] = (flip(ours[k], h, v), "1_6_objects %s%s%s" % (k, " H" if h else "", " V" if v else ""))
    return out


def group_extras(cat):
    out = {}
    for name, f, k, h, v in EXTRAS:
        if name in cat.by_name:
            out[cat.by_name[name]] = (flip(pieces(f)[k], h, v), "%s %s%s%s" % (f, k, " H" if h else "", " V" if v else ""))
    return out


# Famidash slope objects by slope type (include/collision.h COL_SLOPE_BASE order): (piece, h, v)
SLOPES = {
    "45_UP": ("0.2", 1, 0), "45_DOWN": ("0.2", 0, 0), "45_UP_UD": ("0.2", 1, 1), "45_DOWN_UD": ("0.2", 0, 1),
    "22_UP_1": ("0.0", 0, 0), "22_UP_2": ("0.1", 0, 0), "22_DOWN_1": ("0.1", 1, 0), "22_DOWN_2": ("0.0", 1, 0),
    "22_UP_UD_1": ("0.0", 0, 1), "22_UP_UD_2": ("0.1", 0, 1), "22_DOWN_UD_1": ("0.1", 1, 1), "22_DOWN_UD_2": ("0.0", 1, 1),
}
SLOPE_ORDER = ["45_UP", "45_DOWN", "45_UP_UD", "45_DOWN_UD", "22_UP_1", "22_UP_2", "22_DOWN_1", "22_DOWN_2",
               "22_UP_UD_1", "22_UP_UD_2", "22_DOWN_UD_1", "22_DOWN_UD_2"]


def group_slopes(cat):
    ps = pieces("sopeslopeslopeslope")
    out = {}
    for tid, o in sorted(cat.objects.items()):
        k = o["collision"] - 0x60
        if not o["name"][:2].isdigit() or not 0 <= k < len(SLOPE_ORDER):
            continue   # the Famidash slopes (22DEG_..., 45DEG_...), not GDA's brick / checkered ones
        p, h, v = SLOPES[SLOPE_ORDER[k]]
        out[tid] = (flip(ps[p], h, v), "slopes %s%s%s" % (p, " H" if h else "", " V" if v else ""))
    return out


def sheet(name):
    """the whole CHR file as an image of colour indices (128 px wide)"""
    d = (INBOX / (name + ".chr")).read_bytes()
    n = len(d) // 16
    img = Image.new("L", (128, n // 16 * 8), 0)
    for t in range(n):
        b = d[t * 16:(t + 1) * 16]
        for y in range(8):
            lo, hi = (b[y], b[y + 8]) if name in NES else (b[2 * y], b[2 * y + 1])
            for x in range(8):
                img.putpixel(((t % 16) * 8 + x, (t // 16) * 8 + y), ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1))
    return img


# GDA sprite type: (file, x0, y0, x1, y1 of the drawing in it, "bottom" / "centre": lined up with
# GDA's art by its bottom (on the ground) or its centre)
DRAWINGS = {
    "BIG_BG_SQUARES": ("GDP_TOE", 0, 24, 64, 40, "bottom"),
    "MEDIUM_BG_SQUARES": ("GDP_TOE", 72, 8, 120, 24, "bottom"),
    "SMALL_BG_SQUARES": ("GDP_TOE", 72, 32, 112, 48, "bottom"),
    "BIG_BG_CLOUDS": ("Wavy_Gspikes", 24, 0, 72, 16, "bottom"),
    "SMALL_BG_CLOUDS": ("Wavy_Gspikes", 24, 16, 72, 32, "bottom"),
    "BIG_OUTLINE_SAW": ("1p6blocks", 16, 32, 64, 80, "centre"),
    "MEDIUM_OUTLINE_SAW": ("1p6blocks", 64, 32, 96, 64, "centre"),
    "SMALL_OUTLINE_SAW": ("1p6blocks", 0, 48, 16, 64, "centre"),
    "ALT_BIG_OUTLINE_SAW": ("1p6blocks", 96, 32, 128, 64, "centre"),
}


def drawings():
    """{GDA type: (art, origin x, origin y)} for tools/import_gda_sprites.py refresh_art: the user's
    drawing where GDA's art is"""
    types = gda.sprite_types()
    out = {}
    for t, (f, x0, y0, x1, y1, how) in DRAWINGS.items():
        u = sheet(f).crop((x0, y0, x1, y1))
        u = u.crop(u.getbbox())
        g, gox, goy = gda.sprite_art(types.index(t))
        gb = g.getbbox()
        px0 = (gb[0] + gb[2] - u.width) // 2
        py0 = gb[3] - u.height if how == "bottom" else (gb[1] + gb[3] - u.height) // 2
        out[t] = (u, gox - px0, goy - py0)
    return out


def review(cat, changes, path):
    """before | after for every change"""
    pal = [(40, 40, 90), (110, 140, 220), (20, 20, 20), (255, 255, 255)]
    cols = 5
    rows = (len(changes) + cols - 1) // cols
    img = Image.new("RGB", (cols * 200, rows * 40), (30, 30, 30))
    dr = ImageDraw.Draw(img)

    def tile(a):
        im = Image.new("RGB", (16, 16))
        for y in range(16):
            for x in range(16):
                im.putpixel((x, y), pal[a[y][x]])
        return im.resize((32, 32), Image.NEAREST)
    for i, (tid, (a, src)) in enumerate(sorted(changes.items())):
        X, Y = (i % cols) * 200, (i // cols) * 40
        q = cat.art(tid)
        before = [[q[(y >> 3) * 2 + (x >> 3)][y & 7][x & 7] for x in range(16)] for y in range(16)]
        img.paste(tile(before), (X + 2, Y + 2))
        img.paste(tile(a), (X + 36, Y + 2))
        dr.text((X + 72, Y + 4), cat.objects[tid]["name"][:20], fill=(255, 255, 255))
        dr.text((X + 72, Y + 18), src[:20], fill=(170, 170, 170))
    img.save(path)


def main():
    cat = load_catalog()
    changes = {}
    for g in (group_16, group_extras):   # group_slopes: the user kept the placeholder slopes
        changes.update(g(cat))
    review(cat, changes, ROOT / "docs" / "user_art.png")
    print("%d objects get the user's art" % len(changes))
    if "--dry" in sys.argv:
        return
    img = Image.open(ART).convert("L")
    for tid, (a, _) in changes.items():
        for y in range(16):
            for x in range(16):
                img.putpixel(((tid % 16) * 16 + x, (tid // 16) * 16 + y), 255 - 85 * a[y][x])
    img.save(ART)
    write_preview()
    import import_gda_sprites
    missing = import_gda_sprites.refresh_art(drawings())
    if missing:
        print("cells the drawings reach that the blocks do not have (cut off):", ", ".join(missing))
    import make_gda_pulse_tiles
    make_gda_pulse_tiles.main(user=True)


if __name__ == "__main__":
    main()
