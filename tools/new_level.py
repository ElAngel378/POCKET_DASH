"""Makes a new level from the template.

    new_level.bat mylevel "My Level"

Creates levels/chr_data/tmx/mylevel.tmx and opens it in Tiled if Tiled is installed. The title and
song are map properties in Tiled (Map > Map Properties), so nothing else needs editing.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TMX = ROOT / "levels" / "chr_data" / "tmx"
TITLE_LETTERS = 18      # what fits on the Extra Levels screen
TILED = [Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Tiled" / "tiled.exe",
         Path("C:/Program Files (x86)/Tiled/tiled.exe")]


def main():
    if len(sys.argv) < 2:
        print('Usage: new_level mylevel "My Level"')
        return 1
    name = sys.argv[1].lower()
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or re.search(r"_\d+$", name):
        print("A level's file name can only have letters, numbers and _, starts with a letter, and can't end in _ and a number. For example: my_level2")
        return 1
    target = TMX / (name + ".tmx")
    if target.exists() or name == "template":
        print("There's already a level called '%s'. Pick another name, or open %s in Tiled." % (name, target))
        return 1
    title = re.sub(r"[^A-Z0-9 ]", "", (sys.argv[2] if len(sys.argv) > 2 else name.replace("_", " ")).upper())[:TITLE_LETTERS].strip()

    with (TMX / "template.tmx").open(newline="") as f:
        text = f.read()
    assert 'name="Title" value="MY LEVEL"' in text
    text = text.replace('name="Title" value="MY LEVEL"', 'name="Title" value="%s"' % title)
    with target.open("w", newline="") as f:
        f.write(text)
    sys.path.insert(0, str(ROOT / "tools"))
    import level_previews
    level_previews.main([name])

    print("Created %s" % target.relative_to(ROOT))
    print('Title: %s  (change it in Tiled: Map > Map Properties)' % title)
    print("In Tiled, press F5 to build and play it (or run:  build %s)" % name)
    tiled = next((str(p) for p in TILED if p.exists()), shutil.which("tiled"))
    if tiled:
        subprocess.Popen([tiled, "--project", str(ROOT / "levels" / "chr_data" / "pocket_dash.tiled-project"), str(target)])
    else:
        print("Install Tiled from https://www.mapeditor.org and open that file to start building.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
