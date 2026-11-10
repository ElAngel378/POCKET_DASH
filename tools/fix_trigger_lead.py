"""Updates a level made before colour triggers fired where they are placed.

    fix_triggers.bat mylevel                 (a level in levels/chr_data/tmx)
    fix_triggers.bat path/to/mylevel.tmx     (or drop the .tmx on fix_triggers.bat)

Colour triggers used to fire 10 blocks before the player reached them. Now they fire when the player
reaches them, so this moves every BG, ground and outline trigger 10 blocks left and the level plays
as before. A trigger in the first 10 blocks goes to the very start of the level.

The file is changed in place and the original is kept next to it as mylevel.tmx.bak. While that
backup exists the level is not moved again.
"""
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tmx2sprites import BG_TRIGGER, GROUND_TRIGGER, OUTLINE_TRIGGER, TRIGGER_LEAD_PX

GID_FLAGS = 0xE0000000
TAG = r'(?:[^>"]|"[^"]*")*'
OLD_TRIGGERS = range(128, 240)   # one sprite per colour, before the Colour and Fade properties


def attr(tag, name):
    return re.search(r'\s%s="([^"]*)"' % name, tag)


def move_triggers(text):
    """(text with the triggers moved, how many moved, how many stopped at x 0, old triggers found)"""
    tileset = re.search(r'<tileset\b[^>]*\bfirstgid="(\d+)"[^>]*\bsource="[^"]*sprites\.tsx"', text)
    if tileset is None:
        sys.exit("This level does not use the sprites tileset (objects/sprites.tsx).")
    first = int(tileset.group(1))
    triggers = {first + t for t in (BG_TRIGGER, GROUND_TRIGGER, OUTLINE_TRIGGER)}
    moved = at_start = old = 0

    def move_object(match):
        nonlocal moved, at_start, old
        tag = match.group(0)
        gid = attr(tag, 'gid')
        if gid is None:
            return tag
        gid = int(gid.group(1)) & ~GID_FLAGS
        if gid - first in OLD_TRIGGERS:
            old += 1
        if gid not in triggers:
            return tag
        x = attr(tag, 'x')
        new_x = float(x.group(1)) - TRIGGER_LEAD_PX
        if new_x < 0:
            new_x = 0
            at_start += 1
        moved += 1
        return tag[:x.start(1)] + ('%g' % new_x if new_x % 1 else str(int(new_x))) + tag[x.end(1):]

    def move_group(match):
        if attr(match.group(1), 'name').group(1) != 'SP':
            return match.group(0)
        return re.sub(r'<object\b%s>' % TAG, move_object, match.group(0))

    text = re.sub(r'(<objectgroup\b%s(?<!/)>).*?</objectgroup>' % TAG, move_group, text, flags=re.S)
    return text, moved, at_start, old


def find_level(name):
    tmx_dir = Path(__file__).resolve().parent.parent / 'levels' / 'chr_data' / 'tmx'
    for path in (Path(name), tmx_dir / name, tmx_dir / (name + '.tmx')):
        if path.is_file():
            return path
    sys.exit(f"Can't find the level {name}.")


def fix(path):
    backup = path.with_name(path.name + '.bak')
    if backup.exists():
        print(f"{path.name}: already fixed, not moved again (delete {backup.name} to fix it again).")
        return
    with open(path, encoding='utf-8', newline='') as f:
        text = f.read()
    text, moved, at_start, old = move_triggers(text)
    if old:
        print(f"{path.name}: warning: {old} old colour triggers (one sprite per colour) were not moved. "
              "Replace them with BG colour, Ground colour or Outline colour triggers.")
    if not moved:
        print(f"{path.name}: no colour triggers to move, file not changed.")
        return
    shutil.copy2(path, backup)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    print(f"{path.name}: moved {moved} colour triggers 10 blocks left. The original is in {backup.name}.")
    if at_start:
        print(f"  {at_start} of them were in the first 10 blocks and are now at the very start.")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for name in sys.argv[1:]:
        fix(find_level(name))
