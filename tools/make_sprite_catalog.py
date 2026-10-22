"""One-time generator of the sprite catalog (run 2026-10-22): levels/objects/sprites.tsx, the
Tiled tileset of the level sprite layer (SP): portals, orbs, pads, coins, decorations, triggers.

Tile id = the sprite's Famidash id (its cell of Famidash's sprite sheet), so the levels' SP layers
keep their data; the tileset is now a file of ours with a fixed path. Per tile properties:
    name        Famidash's name (famidash-main objdefines.h), ours for the ids the engine reuses
    engine_id   the object id the game gets (tools/tmx2sprites.py), -1 = not supported (ignored)
levels/objects/sprites.png is the art (Famidash's sprite sheet, with our own sprites drawn over the
ids the engine uses for them: the mirror portals, see MIRROR), sprites_preview.png what
Tiled shows (generated: unsupported sprites dimmed and crossed out, see objects_catalog.py).

    python tools/make_sprite_catalog.py ../famidash-main
"""
import re
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import CATALOG_DIR, write_sprite_preview  # noqa: E402
from tmx2sprites import famidash_engine_ids  # noqa: E402

SHEET = ROOT / "levels" / "chr_data" / "graphics" / "sprites.png"
# ids the engine uses for something else than Famidash
OURS = {121: "MIRROR_EXIT", 126: "MIRROR_PORTAL", 38: "PINK_PAD_UP"}
# our pulsing shapes (engine ids 64..72), their art from levels/objects/source (the big size,
# like tools/build_sprite_tiles.py PULSE_GDP): sprite id -> (name, chr file, x, y)
PULSING = {111 + k: (n, f, x, y) for k, (n, f, x, y) in enumerate([
    ("PULSE_CIRCLE", "GDP Pulsing Obj.chr", 0, 0), ("PULSE_RING", "GDP Pulsing Obj.chr", 16, 0),
    ("PULSE_HEART", "GDP Pulsing Obj.chr", 32, 0), ("PULSE_SQUARE", "GDP Pulsing Obj.chr", 48, 0),
    ("PULSE_TRIANGLE", "GDP Pulsing Obj.chr", 64, 0), ("PULSE_X", "GDP Pulsing Obj.chr", 80, 0),
    ("PULSE_EXCLAMATION", "GDP TOE.chr", 88, 80), ("PULSE_ARROW", "GDP TOE.chr", 72, 80),
    ("PULSE_QUESTION", "GDP TOE.chr", 104, 80)])}
PULSE_COLOUR = (123, 255, 0, 255)   # the decorations' colour in the default player colours
# our own art in the sheet: the mirror portals (entrance 126 orange, exit 121 cyan, their colours
# in the game), drawn like Famidash's portals: its mini portal (a crescent with teeth, like our
# mirror portals' rings) with its stripe recoloured
MIRROR = {126: (247, 160, 24), 121: (0, 236, 240)}
MINI_PORTAL = 24


def paint_ours(sheet):
    """Draws our own sprites over Famidash's in the sheet (an RGBA image)"""
    cell = lambda i: ((i % 16) * 16, (i // 16) * 16)
    mx, my = cell(MINI_PORTAL)
    mini = sheet.crop((mx, my, mx + 16, my + 16))
    for sid, colour in MIRROR.items():
        art = mini.copy()
        px = art.load()
        for y in range(16):
            for x in range(16):
                r, g, b, a = px[x, y]
                if a and max(r, g, b) - min(r, g, b) > 60:   # the (pink) stripe, not black, white, grey
                    px[x, y] = colour + (a,)
        sheet.paste(art, cell(sid))
    for sid, (_, name, x0, y0) in PULSING.items():
        d = (CATALOG_DIR / "source" / name).read_bytes()
        art = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        for y in range(16):
            for x in range(16):
                t = ((y0 + y) // 8) * 16 + (x0 + x) // 8
                r = (y0 + y) % 8
                if (d[t * 16 + 2 * r] | d[t * 16 + 2 * r + 1]) >> (7 - (x0 + x) % 8) & 1:
                    art.putpixel((x, y), PULSE_COLOUR)
        sheet.paste(art, cell(sid))


def main():
    famidash = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT.parent / "famidash-main")
    names = {}
    for name, v in re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)\b",
                              (famidash / "LEVELS" / "include" / "lvlset_A" / "objdefines.h").read_text()):
        if name not in ("DECO_START", "DECO_END", "TURN_OFF"):
            names.setdefault(int(v, 16), name)
    for i in range(128, 240):   # the engine's colour triggers (tools/tmx2sprites.py)
        kind = "BG_COLOR" if i < 176 else "OBJ_COLOR" if i < 192 else "GROUND_COLOR"
        names[i] = "%s_%02X" % (kind, i)
    names.update(OURS)
    names.update({sid: p[0] for sid, p in PULSING.items()})
    engine = famidash_engine_ids()
    X = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<tileset version="1.10" tiledversion="1.11.2" name="sprites" tilewidth="16" tileheight="16" '
         'tilecount="256" columns="16">',
         ' <image source="sprites_preview.png" width="256" height="256"/>']
    for i in range(256):
        X += [' <tile id="%d">' % i, '  <properties>',
              '   <property name="name" value="%s"/>' % names.get(i, "UNUSED_%02X" % i),
              '   <property name="engine_id" type="int" value="%d"/>' % engine.get(i, -1),
              '  </properties>', ' </tile>']
    X += ['</tileset>', '']
    sheet = Image.open(SHEET).convert("RGBA")
    paint_ours(sheet)
    sheet.save(CATALOG_DIR / "sprites.png")
    (CATALOG_DIR / "sprites.tsx").write_text("\n".join(X), encoding="utf-8")
    write_sprite_preview()
    print("sprites.tsx: %d sprites, %d supported" % (256, len(engine)))


if __name__ == "__main__":
    main()
