"""Builds the gameplay sprite tiles from ONE image: levels/chr_data/sprite_tiles.png.

    python tools/build_sprite_tiles.py

The image is laid out like sprite VRAM in 8x16 mode: every cell is one tile pair
(tile 2n on top of tile 2n+1), 16 pairs per 16 px row. The grey level of a pixel is its
Game Boy colour index (white = 0, light grey = 1, dark grey = 2, black = 3).

    pair rows  0-7   VRAM bank 0 sprite tiles 0..255 (DMG and CGB). Tiles 8..11 (old ship art, a
                     copy of the neutral ship frame at 48..51) are not loaded; 12, 13 and 16, 17
                     (ball) are loaded to 8..11; 14, 15 (UFO) and 18, 19 (mini UFO, both
                     tools/make_ufo_gfx.py) to 16..19. VRAM tiles 12..15 are free (the pause
                     menu borrows them on CGB)
    pair rows  8-15  CGB VRAM bank 1 sprite tiles 0..255
    pair rows 16-17  DECO_CLOUD: bank 1 tiles 160..199 in levels with Famidash's
                     cloud decorations (Xstep, Clutterfunk)
    pair row  18     CGB_MIRROR_EXIT: CGB version of bank 0 tiles 224..239 (mirror exit
                     portal drawn with colour 3 instead of 2: both mirror portals share
                     sprite palette 6, palette 7 is the HUD text palette; drawn by
                     tools/make_mirror_portal_gfx.py)

Decorations (CGB_DECO, CGB_DECO_CLOUD, ROD_PULSE) are drawn in the player's sprite palette 0, so
in the cube's colour 1: their colour 1 (black) is loaded as colour 3 (black in that palette).

ROD_PULSE (generated, not in the image): the rods' (Famidash "lights") ball tops, deco pairs
D_C9 / D_CB / D_CD (bank 1 pairs 81..83), with a 5 px and a 3 px ball instead of the 7 px one:
CGB bank 1 tiles 222..233 (5 px: 222..227, 3 px: 228..233). sp_draw.c shows them between the
music's hits (src/music/music_beats.c), so the balls pulse to the music like GD's pulse rods.

PULSE_BALL (generated, not in the image): Famidash's pulsing ball decoration (object 73, a lone
ball in the lower half of its tile pair, $F9), 7 / 5 / 3 px like the rod balls and pulsing with
them: CGB bank 1 tiles 250..255.

Only the ranges in LOADS are loaded; the other slots are empty in the image because
gameplay fills them from the font at runtime ("PAUSED" 76..87, debug HUD and % 90..115)
or they are background tiles (bank 0 128..159; DMG coins use 144..159), or the coin
(levels/chr_data/coin.chr, tools/make_coin_tiles.py).

Outputs: levels/chr_data/sprite_tiles.bin, src/sprites/sprite_tile_tables.h
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PNG = ROOT / "levels" / "chr_data" / "sprite_tiles.png"

SECTION_ROW = {"BANK0": 0, "BANK1": 8, "DECO_CLOUD": 16, "CGB_MIRROR_EXIT": 18}
# load conditions (must match src/sprites/famidash_sprite_tiles.c)
ALWAYS, CGB, DMG, CGB_DECO, CGB_DECO_CLOUD = 0, 1, 2, 3, 4
LOADS = [  # condition, VRAM bank, first tile, count, source section, first tile in section
    (ALWAYS, 0, 0, 8, "BANK0", 0),         # player: cube 0..7
    (ALWAYS, 0, 8, 2, "BANK0", 12),        # ball frame 0 (the right half is the left half rotated 180 deg)
    (ALWAYS, 0, 10, 2, "BANK0", 16),       # ball frame 1
    (ALWAYS, 0, 16, 2, "BANK0", 14),       # UFO (the right half is the left one mirrored)
    (ALWAYS, 0, 18, 2, "BANK0", 18),       # mini UFO
    (ALWAYS, 0, 20, 16, "BANK0", 20),      # death effect
    (ALWAYS, 0, 36, 28, "BANK0", 36),      # ship rotation frames 0..6 (pause menu buttons are embedded in pause_button_tiles.c)
    (ALWAYS, 0, 88, 2, "BANK0", 88),       # pause menu cursor
    (ALWAYS, 0, 116, 10, "BANK0", 116),    # mini player (cube 0/22/45 deg, ship, ball)
    (ALWAYS, 0, 126, 2, "BANK0", 126),     # mini / growth portal pair 0
    (ALWAYS, 0, 160, 64, "BANK0", 160),    # Famidash objects: portals, pads, orbs, entrance mirror portal 208..223
    (ALWAYS, 0, 240, 8, "BANK0", 240),     # chain
    (ALWAYS, 0, 248, 8, "BANK0", 248),     # mini / growth portal pairs 1..4
    (CGB, 0, 224, 16, "CGB_MIRROR_EXIT", 0),   # exit portal 224..239; DMG draws the entrance mirrored
    (CGB_DECO, 1, 160, 40, "BANK1", 160),  # level decorations
    (CGB_DECO_CLOUD, 1, 160, 40, "DECO_CLOUD", 0),
    (CGB, 1, 222, 12, "ROD_PULSE", 0),     # rod balls between beats (both deco sets draw the same rods)
    (CGB, 1, 250, 6, "PULSE_BALL", 0),     # pulsing ball decoration, 7 / 5 / 3 px
    (CGB, 1, 234, 16, "BANK1", 234),       # pad / orb animation frames 2, 3 (Famidash $FB $FD, $9D $9F, $BD $BF, $5D $5F)
    # coins: levels/chr_data/coin.chr (tools/make_coin_tiles.py), loaded by famidash_sprite_tiles.c
]


def main():
    img = Image.open(PNG).convert("L")
    assert img.size[0] == 128 and img.size[1] % 16 == 0, "sprite_tiles.png: 128 px wide, 16 px rows"
    px = img.load()
    idx = lambda v: min(range(4), key=lambda i: abs(v - (255 - 85 * i)))

    def rod_pulse_tiles():
        """Pairs 81..83 of BANK1 with the ball (its colour 2 disc, 7 px) drawn 5 / 3 px wide."""
        out = []
        for size in (5, 3):
            for pair in (81, 82, 83):
                x0, y0 = (pair % 16) * 8, (SECTION_ROW["BANK1"] + pair // 16) * 16
                g = [[idx(px[x0 + x, y0 + y]) for x in range(8)] for y in range(16)]
                ball = [(x, y) for y in range(16) for x in range(8) if g[y][x] == 2]
                cx = (min(x for x, _ in ball) + max(x for x, _ in ball)) / 2
                cy = (min(y for _, y in ball) + max(y for _, y in ball)) / 2
                for x, y in ball:
                    g[y][x] = 0
                rad = size / 2
                for y in range(16):
                    for x in range(8):
                        dx, dy = abs(x - cx), abs(y - cy)
                        # a pixel-art disc: the corners of the square cut (3 px: a full square)
                        if dx < rad and dy < rad and (size == 3 or dx + dy < rad + 1):
                            g[y][x] = 2
                for half in (0, 8):
                    t = bytearray()
                    for r in range(8):
                        lo = hi = 0
                        for x in range(8):
                            v = g[half + r][x]
                            lo = (lo << 1) | (v & 1)
                            hi = (hi << 1) | (v >> 1)
                        t += bytes([lo, hi])
                    out.append(bytes(t))
        return out
    rod_pulse = rod_pulse_tiles()

    def pulse_ball_tiles():
        """Famidash's pulsing ball ($F9): a colour 2 disc centred at (3, 12) of the pair, in the
        rod balls' sizes and shape"""
        out = []
        for size in (7, 5, 3):
            rad = size / 2
            g = [[0] * 8 for _ in range(16)]
            for y in range(16):
                for x in range(8):
                    dx, dy = abs(x - 3), abs(y - 12)
                    if dx < rad and dy < rad and (size == 3 or dx + dy < rad + 1):
                        g[y][x] = 2
            for half in (0, 8):
                t = bytearray()
                for r in range(8):
                    lo = hi = 0
                    for x in range(8):
                        v = g[half + r][x]
                        lo = (lo << 1) | (v & 1)
                        hi = (hi << 1) | (v >> 1)
                    t += bytes([lo, hi])
                out.append(bytes(t))
        return out
    pulse_ball = pulse_ball_tiles()

    def tile(section, i):
        if section == "ROD_PULSE":
            return rod_pulse[i]
        if section == "PULSE_BALL":
            return pulse_ball[i]
        p = i >> 1
        x0, y0 = (p % 16) * 8, (SECTION_ROW[section] + p // 16) * 16 + (i & 1) * 8
        out = bytearray()
        for r in range(8):
            lo = hi = 0
            for x in range(8):
                v = idx(px[x0 + x, y0 + r])
                lo = (lo << 1) | (v & 1)
                hi = (hi << 1) | (v >> 1)
            out += bytes([lo, hi])
        return bytes(out)

    def deco_colours(t):
        """Decorations are drawn in the player's palette (0): colour 1 (black) -> 3, its black."""
        t = bytearray(t)
        for r in range(8):
            t[2 * r + 1] |= t[2 * r]
        return bytes(t)

    blob = bytearray()
    rows = []
    for cond, bank, first, count, section, src in LOADS:
        rows.append((cond, bank, first, count, len(blob) // 16))
        deco = cond in (CGB_DECO, CGB_DECO_CLOUD) or section in ("ROD_PULSE", "PULSE_BALL")
        for i in range(count):
            blob += deco_colours(tile(section, src + i)) if deco else tile(section, src + i)
    (ROOT / "levels" / "chr_data" / "sprite_tiles.bin").write_bytes(blob)

    L = ["/* Generated by tools/build_sprite_tiles.py from levels/chr_data/sprite_tiles.png - do not edit */",
         "#ifndef SPRITE_TILE_TABLES_H", "#define SPRITE_TILE_TABLES_H", "",
         "#define SPR_ALWAYS 0", "#define SPR_CGB 1", "#define SPR_DMG 2",
         "#define SPR_CGB_DECO 3", "#define SPR_CGB_DECO_CLOUD 4", "",
         "// condition, VRAM bank, first tile, count, first tile in sprite_tiles.bin (lo, hi)",
         "#define SPRITE_LOAD_COUNT %d" % len(rows),
         "static const uint8_t sprite_loads[SPRITE_LOAD_COUNT][6] = {"]
    for cond, bank, first, count, off in rows:
        L.append("    { %d, %d, %d, %d, %d, %d }," % (cond, bank, first, count, off & 255, off >> 8))
    L += ["};", "", "#endif", ""]
    (ROOT / "src" / "sprites" / "sprite_tile_tables.h").write_text("\n".join(L), newline="\n")
    # where the tiles that are reloaded after the pause menu are in sprite_tiles.bin (in tiles)
    B = ["/* Generated by tools/build_sprite_tiles.py - do not edit */", "#ifndef SPRITE_BLOB_OFFSETS_H",
         "#define SPRITE_BLOB_OFFSETS_H", ""]
    for name, tile in (("DEATH", 20), ("SHIP", 36)):
        off = next(o for _, bank, first, count, o in rows if bank == 0 and first == tile)
        B.append("#define SPRITE_BLOB_%s %d   // sprite_tiles.bin tile of VRAM tile %d" % (name, off, tile))
    B += ["", "#endif", ""]
    (ROOT / "src" / "sprites" / "sprite_blob_offsets.h").write_text("\n".join(B), newline="\n")
    print("sprite_tiles.png: %d tiles in %d load ranges" % (len(blob) // 16, len(rows)))


if __name__ == "__main__":
    main()
