"""One-time converter: Famidash TMX levels -> the object catalog (levels/objects/objects.tsx).

    python tools/convert_tmx_to_catalog.py [level.tmx ...]     (default: levels/chr_data/tmx/*.tmx)

- the embedded Famidash tileset becomes <tileset firstgid="1" source="../../objects/objects.tsx"/>
- the map layer's cells: Famidash metatile -> the catalog object the level shows for it
  (levels/objects/famidash_ids.json; other levels / unused ids: the object with that famidash_id
  and no variant suffix), Tiled's flip bits kept
- the sprite tileset moves to firstgid SPRITES_FIRSTGID, the SP layer's cells with it

A file that already uses the catalog is left alone.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from objects_catalog import ROOT, CATALOG_DIR, SPRITES_FIRSTGID, load_catalog  # noqa: E402

FLAGS = 0xF0000000


def convert(path, cat, ids):
    text = path.read_text(encoding="utf-8")
    if "objects.tsx" in text:
        print("%s: already converted" % path.name)
        return
    level = ids.get(path.stem, {})
    default = {}
    for tid, o in cat.objects.items():
        if o["name"].endswith(("_SB", "_SC", "_BB", "_V", "_DIAMOND")):
            continue
        default.setdefault(o["famidash_id"], tid)

    m = re.search(r'<tileset firstgid="1" name="famidash".*?</tileset>', text, re.S)
    assert m, path
    text = text[:m.start()] + '<tileset firstgid="1" source="../../objects/objects.tsx"/>' + text[m.end():]
    m = re.search(r'<tileset firstgid="(\d+)" name="sprites"', text)
    old_sp = int(m.group(1))
    text = text[:m.start(1)] + str(SPRITES_FIRSTGID) + text[m.end(1):]

    def layer(match):
        head, body = match.group(1), match.group(2)
        is_sp = 'name="SP"' in head
        cells = []
        for v in body.split(","):
            s = v.strip()
            if not s:
                cells.append(v)
                continue
            g = int(s)
            flags, gid = g & FLAGS, g & ~FLAGS
            if gid == 0:
                new = 0
            elif is_sp or gid >= old_sp:
                assert gid >= old_sp, (path.name, "map tile in the SP layer", gid)
                new = gid - old_sp + SPRITES_FIRSTGID
            else:
                fid = gid - 1
                new = level.get(str(fid), default.get(fid)) + 1
            cells.append(v.replace(s, str(new | flags)))
        return head + ",".join(cells) + match.group(3)

    text = re.sub(r'(<layer[^>]*>\s*<data encoding="csv">)(.*?)(</data>)', layer, text, flags=re.S)
    path.write_text(text, encoding="utf-8")
    print("%s: converted" % path.name)


def main():
    cat = load_catalog()
    ids = json.loads((CATALOG_DIR / "famidash_ids.json").read_text())
    paths = [Path(p) for p in sys.argv[1:]] or sorted((ROOT / "levels" / "chr_data" / "tmx").glob("*.tmx"))
    for p in paths:
        convert(p, cat, ids)


if __name__ == "__main__":
    main()
