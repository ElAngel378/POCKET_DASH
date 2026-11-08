"""One-time conversion, after tools/sp_to_objects.py: one colour trigger object per channel instead
of one sprite per colour, with Colour and Fade properties instead of the fade time marker sprites.

    python tools/sp_props.py

A marker set the fade time of every BG and ground trigger in its column. Levels already converted
are left alone.
"""
import re
from pathlib import Path

from gen_famidash_bg_tables import NES
from tmx2sprites import BG_TRIGGER, FADE_NAMES, GROUND_TRIGGER, OUTLINE_NES, OUTLINE_TRIGGER, SPRITES_FIRSTGID as FIRST

ROOT = Path(__file__).resolve().parent.parent
FOLDERS = [ROOT / "levels" / "chr_data" / "tmx", ROOT / "levels" / "chr_data" / "examples"]
OBJECT = re.compile(r'([ \t]*)<object id="(\d+)" gid="(\d+)" x="(-?[\d.]+)" y="(-?[\d.]+)" width="16" height="16"/>(\r?\n)')
MARKER_FADE = {246 + i: name for i, name in enumerate(n for n in FADE_NAMES if FADE_NAMES[n])}


def trigger(cell):
    """cell -> (trigger tile, NES colour), or None"""
    if 128 <= cell < 176:
        return BG_TRIGGER, NES[cell - 128]
    if 176 <= cell < 192:
        return OUTLINE_TRIGGER, NES[OUTLINE_NES[cell - 176]]
    if 192 <= cell < 240:
        return GROUND_TRIGGER, NES[cell - 192]
    return None


def convert(path):
    with path.open(newline="") as f:
        text = f.read()
    cells = [(m, int(m.group(3)) - FIRST) for m in OBJECT.finditer(text)]
    if not any(246 <= c <= 251 or 128 <= c < 240 for _m, c in cells):
        return None
    fade = {int(float(m.group(4))) // 16: MARKER_FADE[c] for m, c in cells if 246 <= c <= 251}
    triggers = 0

    def rewrite(m):
        nonlocal triggers
        ind, oid, gid, x, y, nl = m.groups()
        cell = int(gid) - FIRST
        if 246 <= cell <= 251:
            return ""
        found = trigger(cell)
        if found is None:
            return m.group(0)
        triggers += 1
        tile, rgb = found
        lines = ['%s<object id="%s" gid="%d" x="%s" y="%s" width="16" height="16">' % (ind, oid, FIRST + tile, x, y),
                 ind + ' <properties>',
                 '%s  <property name="Colour" type="color" value="#ff%06x"/>' % (ind, rgb)]
        col = int(float(x)) // 16
        if tile != OUTLINE_TRIGGER and col in fade:
            lines.append('%s  <property name="Fade" propertytype="FadeTime" value="%s"/>' % (ind, fade[col]))
        lines += [ind + ' </properties>', ind + '</object>']
        return nl.join(lines) + nl

    text = OBJECT.sub(rewrite, text)
    with path.open("w", newline="") as f:
        f.write(text)
    return triggers, len(fade)


def main():
    for folder in FOLDERS:
        for path in sorted(folder.glob("*.tmx")):
            result = convert(path)
            print("%-24s %s" % (path.stem, "nothing to convert" if result is None else "%d triggers, %d fade columns" % result))


if __name__ == "__main__":
    main()
