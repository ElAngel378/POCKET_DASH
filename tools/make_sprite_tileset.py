"""Our own Tiled tileset of sprite objects: levels/chr_data/objects/sprites.tsx, with one image per
sprite in levels/chr_data/objects/sprites/. Only what the game has: no crosses, no Famidash
toggles it ignores.

    python tools/make_sprite_tileset.py

The tile id is the sprite's cell in levels/chr_data/graphics/sprites.png (tools/tmx2sprites.py
turns it into the engine's object id), so never renumber one. Each tile's Class in Tiled says
what it is. The mirror portals have no art in sprites.png: theirs comes from the game's tiles.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
GFX = ROOT / "levels" / "chr_data"
SHEET = GFX / "graphics" / "sprites.png"
GAME_TILES = GFX / "sprite_tiles.png"
OUT_DIR = GFX / "objects" / "sprites"
TSX = GFX / "objects" / "sprites.tsx"

NAMES = {
    0: "Cube portal", 1: "Ship portal", 2: "Ball portal", 3: "UFO portal",
    5: "Blue orb", 6: "Pink orb", 7: "Coin 1", 26: "Coin 2", 27: "Coin 3", 11: "Yellow orb",
    8: "Gravity portal, normal", 9: "Gravity portal, inverted",
    16: "Gravity portal, normal (going down)", 17: "Gravity portal, normal (going up)",
    18: "Gravity portal, inverted (going down)", 19: "Gravity portal, inverted (going up)",
    10: "Yellow pad", 12: "Yellow pad, upside down", 13: "Blue pad", 14: "Blue pad, upside down",
    37: "Pink pad", 38: "Pink pad, upside down", 82: "Red pad", 83: "Red pad, upside down",
    253: "Blue pad, invisible", 254: "Blue pad, invisible, upside down",
    15: "Level end", 24: "Mini portal", 25: "Growth portal",
    121: "Mirror portal, exit", 126: "Mirror portal",
    73: "Pulsing ball", 74: "Music note", 112: "Pulsing ring", 113: "Pulsing heart",
    246: "Colour fade: instant", 247: "Colour fade: 0.5 s", 248: "Colour fade: 1 s",
    249: "Colour fade: 2 s", 250: "Colour fade: 4 s", 251: "Colour fade: 10 s",
}
NAMES.update({i: "Decoration %d" % i for i in range(42, 64)})
NAMES.update({128 + i: "BG colour %02X" % i for i in range(48)})
NAMES.update({176 + i: "Outline colour %02X" % i for i in range(16)})
NAMES.update({192 + i: "Ground colour %02X" % i for i in range(48)})
# Cells in those ranges that are not colours and no level places: Famidash's crosses and toggles
for i in (141, 142, 157, 158, 159, 173, 174, 175, 205, 206, 221, 222, 223, 237, 238, 239):
    del NAMES[i]

# Mirror portals: 16 tile pairs of the game's art (top row, middle row; the bottom is the top
# flipped), first pair of each -> ring colour (tools/make_mirror_portal_gfx.py)
MIRRORS = {126: ((8, 6), (255, 140, 0)), 121: ((0, 18), (30, 220, 255))}


def mirror_icon(start, ring):
    col, row = start
    tiles = Image.open(GAME_TILES).convert("L")
    art = Image.new("RGBA", (32, 48))
    colours = {0: (0, 0, 0, 0), 1: (0, 0, 0, 255), 2: ring + (255,), 3: ring + (255,)}
    for y in range(48):
        source = y if y < 32 else 47 - y    # the bottom row mirrors the top one
        band = source // 16
        for x in range(32):
            px = tiles.getpixel((col * 8 + band * 32 + x, row * 16 + source % 16))
            art.putpixel((x, y), colours[(255 - px) // 85])
    small = art.resize((11, 16), Image.NEAREST)
    icon = Image.new("RGBA", (16, 16))
    icon.paste(small, (3, 0))
    return icon


def main():
    OUT_DIR.mkdir(exist_ok=True)
    sheet = Image.open(SHEET).convert("RGBA")
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<tileset version="1.10" tiledversion="1.11.2" name="sprites" tilewidth="16" tileheight="16" tilecount="%d" columns="0">' % len(NAMES),
             ' <grid orientation="orthogonal" width="1" height="1"/>']
    for cell in sorted(NAMES):
        if cell in MIRRORS:
            icon = mirror_icon(*MIRRORS[cell])
        else:
            x, y = (cell % 16) * 16, (cell // 16) * 16
            icon = sheet.crop((x, y, x + 16, y + 16))
        icon.save(OUT_DIR / ("s%d.png" % cell))
        lines.append(' <tile id="%d" type="%s">' % (cell, NAMES[cell]))
        lines.append('  <image width="16" height="16" source="sprites/s%d.png"/>' % cell)
        lines.append(' </tile>')
    lines.append('</tileset>')
    TSX.write_text("\n".join(lines) + "\n", newline="\n")
    print("%s: %d sprites" % (TSX.name, len(NAMES)))


if __name__ == "__main__":
    main()
