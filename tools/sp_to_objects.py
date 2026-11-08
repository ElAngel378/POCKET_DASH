"""One-time conversion: every level's SP tile layer and Famidash offsets (levels/metadata) into a
Tiled object layer that uses levels/chr_data/objects/sprites.tsx.

    python tools/sp_to_objects.py     (then tools/sp_props.py)

Objects sit at their real pixel position, offsets included, so tools/tmx2sprites.py needs no
offsets file. Levels already converted are left alone. Cells the game ignores are dropped and
reported.
"""
import re
from collections import Counter
from pathlib import Path

from make_sprite_tileset import NAMES
from tmx2sprites_offsets import ENGINE_ID, offsets_for

ROOT = Path(__file__).resolve().parent.parent
FOLDERS = [ROOT / "levels" / "chr_data" / "tmx", ROOT / "levels" / "chr_data" / "examples"]
SPRITES_FIRSTGID = 10001
FLAGS = 0xE0000000


def convert(path):
    with path.open(newline="") as f:
        text = f.read()
    tileset = re.search(r'[ \t]*<tileset firstgid="(\d+)" name="sprites".*?</tileset>', text, flags=re.S)
    layer = re.search(r'([ \t]*)<layer ([^>]*name="SP"[^>]*)>\s*<data encoding="csv">(.*?)</data>\s*</layer>', text, flags=re.S)
    if tileset is None or layer is None:
        return None
    nl = "\r\n" if "\r\n" in text else "\n"
    first = int(tileset.group(1))
    w, h = (int(v) for v in re.search(r'<map [^>]*?\bwidth="(\d+)" height="(\d+)"', text).groups())
    offsets = offsets_for(path.stem)
    values = [int(v) for v in layer.group(3).replace("\r", "").replace("\n", "").split(",") if v.strip()]
    objects, dropped = [], Counter()
    for i, gid in enumerate(values):
        if not gid:
            continue
        cell = (gid & ~FLAGS) - first
        if cell not in NAMES and not 128 <= cell < 240 and not 246 <= cell <= 251:
            dropped[cell] += 1
            continue
        col, row = i % w, i // w
        dx = dy = 0
        if cell in ENGINE_ID and not 246 <= cell <= 251:
            engine = ENGINE_ID[cell]
            a = offsets.get(engine, [0, 0])
            b, override = offsets.get((col, row), [[0, 0], False])
            dx, dy = b if override else (a[0] + b[0], a[1] + b[1])
        objects.append((cell, col * 16 + dx, (row + 1) * 16 + dy))
    ind = layer.group(1)
    attrs = re.sub(r'\s*(width|height)="\d+"', "", layer.group(2))
    body = [ind + '<objectgroup ' + attrs.strip() + '>']
    for n, (cell, x, y) in enumerate(objects, 1):
        body.append('%s <object id="%d" gid="%d" x="%d" y="%d" width="16" height="16"/>' % (ind, n, SPRITES_FIRSTGID + cell, x, y))
    body.append(ind + '</objectgroup>')
    text = text[:layer.start()] + nl.join(body) + text[layer.end():]
    tileset = re.search(r'[ \t]*<tileset firstgid="(\d+)" name="sprites".*?</tileset>', text, flags=re.S)
    text = text[:tileset.start()] + ' <tileset firstgid="%d" source="../objects/sprites.tsx"/>' % SPRITES_FIRSTGID + text[tileset.end():]
    text = re.sub(r'(<map [^>]*?\bnextobjectid=")\d+', r"\g<1>%d" % (len(objects) + 1), text, count=1)
    with path.open("w", newline="") as f:
        f.write(text)
    return len(objects), dropped


def main():
    for folder in FOLDERS:
        for path in sorted(folder.glob("*.tmx")):
            result = convert(path)
            if result is None:
                print("%-24s nothing to convert" % path.stem)
            else:
                print("%-24s %5d objects, dropped %s" % (path.stem, result[0], dict(result[1])))


if __name__ == "__main__":
    main()
