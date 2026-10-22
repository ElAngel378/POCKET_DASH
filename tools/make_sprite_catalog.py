"""One-time generator of the sprite catalog (run 2026-10-22): levels/objects/sprites.tsx, the
Tiled tileset of the level sprite layer (SP): portals, orbs, pads, coins, decorations, triggers.

Tile id = the sprite's Famidash id (its cell of Famidash's sprite sheet), so the levels' SP layers
keep their data; the tileset is now a file of ours with a fixed path. Per tile properties:
    name        Famidash's name (famidash-main objdefines.h), ours for the ids the engine reuses
    engine_id   the object id the game gets (tools/tmx2sprites.py), -1 = not supported (ignored)
levels/objects/sprites.png is the art (Famidash's sprite sheet for now), sprites_preview.png what
Tiled shows (generated: unsupported sprites dimmed and crossed out, see objects_catalog.py).

    python tools/make_sprite_catalog.py ../famidash-main
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from objects_catalog import CATALOG_DIR, write_sprite_preview  # noqa: E402
from tmx2sprites import famidash_engine_ids  # noqa: E402

SHEET = ROOT / "levels" / "chr_data" / "graphics" / "sprites.png"
# ids the engine uses for something else than Famidash
OURS = {121: "MIRROR_EXIT", 126: "MIRROR_PORTAL", 38: "PINK_PAD_UP"}


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
    shutil.copyfile(SHEET, CATALOG_DIR / "sprites.png")
    (CATALOG_DIR / "sprites.tsx").write_text("\n".join(X), encoding="utf-8")
    write_sprite_preview()
    print("sprites.tsx: %d sprites, %d supported" % (256, len(engine)))


if __name__ == "__main__":
    main()
