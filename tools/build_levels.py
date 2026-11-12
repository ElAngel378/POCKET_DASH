import os
import sys
import glob
import re
import struct
import subprocess
import json
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gd_objects import ROWS_BELOW

REPO_ROOT = Path(__file__).resolve().parent.parent
TMX_DIR = REPO_ROOT / "levels" / "chr_data" / "tmx"
LEVEL_DATA_DIR = REPO_ROOT / "levels" / "level_data"
SRC_DIR = REPO_ROOT / "src"
MUSIC_DIR = SRC_DIR / "music"
ROOT_MUSIC_DIR = REPO_ROOT / "music" / "Legacy"   # hUGETracker songs (the legacy driver)
UGE2SOURCE_BIN = REPO_ROOT / "tools" / ("uge2source.exe" if os.name == "nt" else "uge2source")
SPRITES_DIR = SRC_DIR / "sprites"
LEVELS_DIR = SRC_DIR / "levels"
ASSETS_C_PATH = SRC_DIR / "assets.c"

# What a level is called, where it is listed and what it plays are the TMX's map properties
MENUS = ("Main", "Extra")                 # the "Menu" property; the game lists them in this order
TITLE_LETTERS = {"Main": 21, "Extra": 18}  # what fits on each menu
# the "Difficulty" property: the face on the menus (src/graphics/difficulty_faces.h order)
DIFFICULTIES = ("Auto", "Easy", "Normal", "Hard", "Harder", "Insane", "Demon", "Insane Demon", "NA", "Easy Demon")
NUM_SAVE_SLOTS = 64                       # include/save_manager.h NUM_SAVE_LEVELS
FUR_DIR = REPO_ROOT / "music" / "Furnace"
MENU_SONGS = {"menuloop", "menu", "menutheme"}   # the menu theme, no level plays it
NOT_LEVEL_SONGS = MENU_SONGS | {"songtemplate"}
NO_SONG_DIVIDER = 183                     # the music timer of a level without a hUGETracker song
HAND_WRITTEN_MUSIC = {"music_beats.c", "practice.c"}   # in src/music, not made from a level's song

def level_settings(tmx_path):
    """(BG colour, ground colour, 1 if the cube falls up to 7 px a frame) from the TMX's map properties"""
    from tmx2sprites import TRIGGER_COLOURS, nearest_colour
    from gen_famidash_bg_tables import NES
    props = {p.get('name'): p.get('value') for p in ET.parse(tmx_path).getroot().findall('properties/property')}
    def colour(name):
        if name not in props:
            return 17
        return TRIGGER_COLOURS[nearest_colour(props[name], TRIGGER_COLOURS, lambda n: NES[n])]
    return colour('BG colour'), colour('Ground colour'), 1 if props.get('Max fall') == '7' else 0


def read_uge_tempo(uge_path):
    """
    Extracts (divider/TMA, ticks_per_row) from a hUGETracker .uge file.
    Offset 63609: ticks_per_row (uint32)
    Offset 63613: timer_enabled (uint8)
    Offset 63614: timer_divider / TMA (uint32)
    """
    try:
        with open(uge_path, 'rb') as f:
            d = f.read()
        if len(d) >= 63618:
            ticks, = struct.unpack_from('<I', d, 63609)
            timer_en = d[63613]
            divider, = struct.unpack_from('<I', d, 63614)
            if timer_en and 0 <= divider <= 255:
                return divider, ticks
    except Exception as e:
        print(f"  Warning reading tempo from {uge_path}: {e}")
    return None, None

def norm_name(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())

def song_files():
    """The songs a level can play: {normalised name: [name in Tiled, its .fur, its .uge]}. A .fur and
    a .uge with the same name are the same song (the .uge plays with the LEGACY setting)."""
    found = {}
    for kind, folder, pattern in ((1, FUR_DIR, "*.fur"), (2, ROOT_MUSIC_DIR, "*.uge")):
        for p in sorted(folder.glob(pattern), key=lambda p: p.name.lower()):
            if norm_name(p.stem) not in NOT_LEVEL_SONGS:
                found.setdefault(norm_name(p.stem), [p.stem, None, None])[kind] = p
    return found

def update_song_list():
    """The Song dropdown in Tiled: the songs in the music folders"""
    path = TMX_DIR.parent / "pocket_dash.tiled-project"
    with path.open(encoding="utf-8", newline="") as f:
        text = f.read()
    project = json.loads(text)
    values = ["none"] + sorted((s[0] for s in song_files().values()), key=str.lower)
    song_type = next(t for t in project["propertyTypes"] if t["name"] == "Song")
    if song_type["values"] != values:
        song_type["values"] = values
        nl = "\r\n" if "\r\n" in text else "\n"
        with path.open("w", encoding="utf-8", newline="") as f:
            f.write(json.dumps(project, indent=4).replace("\n", nl) + nl)

def format_title(stem):
    return stem.replace("_", " ").replace("-", " ").upper()

def make_c_ident(name):
    return re.sub(r'[^a-zA-Z0-9_]', '_', name.lower())

def get_map_dimensions(tmx_path):
    tree = ET.parse(tmx_path)
    root = tree.getroot()
    width = int(root.attrib.get('width', 0))
    height = int(root.attrib.get('height', 0)) - ROWS_BELOW
    return width, height

def export_binary_map(tmx_path, out_bin_path):
    tree = ET.parse(tmx_path)
    root = tree.getroot()

    width = int(root.attrib['width'])
    height = int(root.attrib['height']) - ROWS_BELOW   # the level's rows

    import level_objects
    novel = {}
    if level_objects.is_object_tmx(root):
        # a level made of objects (tools/level_objects.py): their Famidash metatiles, and metatiles
        # of its own for the cells Famidash has none for (tools/compose.py)
        grid = level_objects.compile_tmx(Path(tmx_path), novel)
    else:
        grid = old_format_grid(root, tmx_path, width, height)
    # the level's own metatiles, for tools/build_bg_tiles.py: id -> CGB quarters, DMG quarters,
    # collision (a quarter: 64 pixels, or null = empty)
    side = Path(out_bin_path).with_name(Path(out_bin_path).stem + "_novel.json")
    if novel:
        side.write_text(json.dumps({str(m): {"cgb": v[0], "dmg": v[1], "col": v[2], "pal": v[3], "spin": v[4], "hide": v[5], "dmg_at": v[6] if len(v) > 6 else None} for m, v in sorted(novel.items())}))
    elif side.exists():
        side.unlink()

    # Maps are MAP_ROWS tall, bottom-aligned: shorter maps get empty rows on top, taller ones
    # lose their top rows
    crop_height = MAP_ROWS
    start_y = max(0, height - crop_height)
    cropped_grid = grid[start_y:height]

    while len(cropped_grid) < crop_height:
        cropped_grid.insert(0, [0] * width)

    os.makedirs(out_bin_path.parent, exist_ok=True)
    with open(out_bin_path, 'wb') as f:
        for x in range(width):
            for y in range(crop_height):
                f.write(bytes([cropped_grid[y][x] & 0xFF]))


def old_format_grid(root, tmx_path, width, height):
    """The metatile rows of a Famidash-format TMX (one layer of metatiles)"""
    bg_layer = None
    for layer in root.findall('layer'):
        if layer.get('name') in ['Tile Layer 1', 'background', 'BG', 'Level']:
            bg_layer = layer
            break
    if bg_layer is None:
        bg_layer = root.find('layer')

    data_el = bg_layer.find('data')
    if data_el is None or data_el.get('encoding') != 'csv':
        raise ValueError(f"Expected CSV encoding in {tmx_path}")

    csv_text = data_el.text.strip()
    raw_tiles = [int(x.strip()) for x in csv_text.split(',') if x.strip()]

    grid = []
    for y in range(height):
        row = raw_tiles[y * width : (y + 1) * width]
        row_0based = [(tid - 1 if tid > 0 else 0) for tid in row]
        grid.append(row_0based)
    return grid

# A map is MAP_ROWS rows tall, stored column by column (MAP_ROWS bytes per column), in chunks of
# MAP_BANK_COLS columns (16 KB, a whole ROM bank), one chunk per bank, in consecutive banks.
# Must match MAP_ROWS / MAP_BANK_COLS in include/collision.h.
MAP_ROWS = 64
MAP_BANK_COLS = 16384 // MAP_ROWS
MAP_MAX_COLS = 4095          # the camera is a 16-bit pixel position
FIRST_MAP_BANK = 30
LAST_BANK = 255


def used_banks(generated=()):
    """Banks of everything this script does not regenerate (#pragma bank in src/, except the
    level maps in src/levels/ and the `generated` files: the levels' sprite lists and songs)."""
    used = set()
    generated = {Path(g).resolve() for g in generated}
    for p in SRC_DIR.rglob("*.c"):
        if p.parent == LEVELS_DIR or p.resolve() in generated:
            continue
        m = re.search(r'^#pragma\s+bank\s+(\d+)', p.read_text(errors="ignore"), re.M)
        if m:
            used.add(int(m.group(1)))
    return used


def allocate_banks(count, start, taken):
    """First run of `count` consecutive free banks at or after `start`, then the gaps lower down."""
    for first in (start, 2):
        for b in range(first, LAST_BANK - count + 2):
            if not taken & set(range(b, b + count)):
                return b
    raise SystemExit("no %d consecutive free ROM banks for a level map" % count)


def write_map_wrappers(ident, short_name, out_bin, width, first_bank):
    """level_<short>.c holds the first chunk; level_<short>_<k>.c chunk k (k >= 1)."""
    data = out_bin.read_bytes()
    chunks = (width + MAP_BANK_COLS - 1) // MAP_BANK_COLS
    for old in list(LEVELS_DIR.glob(f"level_{short_name}_[0-9]*.c")) + \
               list(LEVEL_DATA_DIR.glob(f"{ident}_[0-9]*.bin")):
        old.unlink()
    for k in range(chunks):
        if chunks == 1:
            bin_name = out_bin.name
        else:
            bin_name = f"{ident}_{k}.bin"
            (LEVEL_DATA_DIR / bin_name).write_bytes(data[k * MAP_BANK_COLS * MAP_ROWS:(k + 1) * MAP_BANK_COLS * MAP_ROWS])
        c_path = LEVELS_DIR / (f"level_{short_name}.c" if k == 0 else f"level_{short_name}_{k}.c")
        sym = f"{ident}_map" if k == 0 else f"{ident}_map_{k}"
        with open(c_path, 'w') as f:
            f.write(f"#pragma bank {first_bank + k}\n")
            f.write('#include <gbdk/incbin.h>\n\n')
            if k == 0 and chunks > 1:
                f.write(f"// Columns 0..{MAP_BANK_COLS - 1}; the rest are in level_{short_name}_1.c.. "
                        "(next banks, same address)\n")
            elif k:
                f.write(f"// Columns {k * MAP_BANK_COLS}.. of {ident} (read through {ident}_map's address)\n")
            f.write(f'INCBIN({sym}, "levels/level_data/{bin_name}")\n')
            f.write(f'INCBIN_EXTERN({sym})\n')
    return chunks


# Famidash puts a diamond decoration (deco 50 / 51) on some big saw centres; the saw art has its
# own hub, so those are dropped. Metatile 120 is the big saw centre in levels with big saws.
BIG_SAW_PARTS = {116, 117, 118, 119, 121, 122, 123, 124}
SAW_CENTER_MT = 120


def saw_centre_deco(map_bytes):
    if not BIG_SAW_PARTS & set(map_bytes):
        return None

    def skip(x, y, obj):
        if obj not in (50, 51):
            return False
        i = (x // 16) * MAP_ROWS + y // 16
        return i < len(map_bytes) and map_bytes[i] == SAW_CENTER_MT
    return skip


def update_music_bank(music_path, target_bank):
    if not music_path.exists():
        return
    with open(music_path, 'r') as f:
        content = f.read()

    if re.search(r'#pragma\s+bank', content):
        new_content = re.sub(r'#pragma\s+bank\s+\d+', f'#pragma bank {target_bank}', content)
    else:
        new_content = f'#pragma bank {target_bank}\n\n' + content
    new_content = re.sub(r'__at\(\d+\)', f'__at({target_bank})', new_content)

    if new_content != content:
        with open(music_path, 'w') as f:
            f.write(new_content)

def generate_assets_c(levels):
    with open(ASSETS_C_PATH, 'w') as f:
        f.write('// Auto-generated by tools/build_levels.py - DO NOT EDIT DIRECTLY\n')
        f.write('#include <gbdk/incbin.h>\n')
        f.write('#include "assets.h"\n')
        f.write('#include "bg_tiles.h"\n\n')

        f.write('BANKREF(game_levels)\n')
        f.write('BANKREF_EXTERN(chr_gb)\n')
        f.write('extern const uint8_t chr_gb_tiles[];\n')
        f.write('extern const uint8_t chr_gb_tiles_rev[];\n\n')

        # External map declarations
        f.write('// External map data definitions from level files\n')
        for lvl in levels:
            ident = lvl["ident"]
            f.write(f'BANKREF_EXTERN({ident}_map)\n')
            f.write(f'extern const uint8_t {ident}_map[];\n')
        f.write('\n')

        # External sprite declarations
        f.write('// External sprite data definitions\n')
        for lvl in levels:
            ident = lvl["ident"]
            f.write(f'extern const SpDef {ident}_sp[];\n')
            f.write(f'extern const SpDef {ident}_sp_dmg[];\n')
            f.write(f'BANKREF_EXTERN({ident}_sp)\n')
        f.write('\n')

        # External music declarations
        f.write('// Music songs\n')
        for ident in dict.fromkeys(l["music_ident"] for l in levels if l["has_music"]):
            f.write(f'extern const hUGESong_t {ident};\n')
        f.write('\n')

        # level_songs array
        f.write('// Level songs array\n')
        f.write('const hUGESong_t * const level_songs[] = {\n')
        for lvl in levels:
            if lvl["has_music"]:
                f.write(f'  &{lvl["music_ident"]}, // level_{lvl["short_name"]}\n')
            else:
                f.write(f'  NULL, // level_{lvl["short_name"]} (silent)\n')
        f.write('};\n\n')

        # song_bank array
        f.write('// Per-level song banks: matches level_songs[]; 0 = silent\n')
        f.write('const uint8_t song_bank[] = {\n')
        for lvl in levels:
            f.write(f'  {lvl["music_bank"]}u, // level_{lvl["short_name"]}\n')
        f.write('};\n\n')

        # Level struct definitions
        f.write('// Level definitions with dimensions and bank info\n')
        for lvl in levels:
            ident = lvl["ident"]
            f.write(f'const Level level_{lvl["short_name"]} = {{\n')
            f.write(f'  "{lvl["title"]}",\n')
            f.write('  chr_gb_tiles,\n')
            f.write('  chr_gb_tiles_rev,\n')
            f.write(f'  {ident}_map,\n')
            f.write(f'  BG_BASE_TILE_COUNT, {lvl["width"]}, {lvl["height"]}, 0, 0,\n')
            f.write(f'  BANK({ident}_map),\n')
            f.write(f'  {lvl["divider"]},\n')
            f.write(f'  {ident}_sp,\n')
            f.write(f'  BANK({ident}_sp),\n')
            f.write(f'  {ident}_sp_dmg,\n')
            f.write(f'  {lvl["map_top"]},\n')
            f.write(f'  {lvl["bg_color"]},\n')
            f.write(f'  {lvl["g_color"]},\n')
            f.write(f'  {lvl["max_fall_7"]}\n')
            f.write('};\n\n')

        # game_levels array
        f.write('// Global level list used by the menu and gameplay systems\n')
        f.write('const Level* const game_levels[] = {\n')
        for lvl in levels:
            f.write(f'  &level_{lvl["short_name"]},\n')
        f.write('};\n')
        f.write('const uint8_t MAX_LEVELS = sizeof(game_levels) / sizeof(game_levels[0]);\n\n')

        # the main levels (the level select of PLAY) come first, then the extra ones (the wrench)
        n_main = sum(1 for l in levels if not l["extra"])
        n_extra = sum(1 for l in levels if l["extra"])
        assert all(not l["extra"] for l in levels[:n_main])
        assert all(l["extra"] for l in levels[n_main:n_main + n_extra])
        f.write(f'const uint8_t NUM_MAIN_LEVELS = {n_main};\n')
        f.write(f'const uint8_t NUM_EXTRA_LEVELS = {n_extra};\n\n')

        # save slots: a level keeps its slot when the level order changes
        f.write('// Save slot of each level (game_levels order)\n')
        f.write('const uint8_t level_save_slot[] = {\n')
        for lvl in levels:
            f.write(f'  {lvl["save_slot"]}u, // level_{lvl["short_name"]}\n')
        f.write('};\n\n')

        f.write('// Difficulty face of each level (game_levels order; difficulty_faces.h)\n')
        f.write('const uint8_t level_difficulty[] = {\n')
        for lvl in levels:
            f.write(f'  {lvl["difficulty"]}u, // level_{lvl["short_name"]}\n')
        f.write('};\n')

def map_properties(stem):
    return {p.get("name"): p.get("value") for p in ET.parse(TMX_DIR / (stem + ".tmx")).getroot().findall("properties/property")}

@lru_cache(maxsize=None)
def level_info(stem):
    """The level's map properties: menu, order, title, song (None: silent), save (None: not given yet)"""
    props = map_properties(stem)

    def number(name, default):
        value = props.get(name)
        if value is None:
            return default
        if not value.strip().isdigit():
            raise SystemExit(f"Level '{stem}': the {name} property has to be a number, not '{value}'.")
        return int(value)
    menu = props.get("Menu") or "Extra"
    if menu not in MENUS:
        raise SystemExit(f"Level '{stem}': the Menu property has to be Main or Extra, not '{menu}'.")
    title = re.sub(r"[^A-Z0-9 ]", "", (props.get("Title") or "").upper())[:TITLE_LETTERS[menu]].strip()
    song = (props.get("Song") or "").strip()
    difficulty = (props.get("Difficulty") or "NA").strip()
    if difficulty not in DIFFICULTIES:
        raise SystemExit(f"Level '{stem}': the Difficulty property has to be one of {', '.join(DIFFICULTIES)}, not '{difficulty}'.")
    return {"menu": menu, "order": number("Order", 999), "save": number("Save slot", None),
            "title": title or format_title(stem)[:TITLE_LETTERS[menu]],
            "difficulty": DIFFICULTIES.index(difficulty),
            "song": None if song.lower() in ("", "none") else song}

def level_files():
    """The levels' TMX files in game order (a level's index is its position here)."""
    tmx_files = [p for p in TMX_DIR.glob("*.tmx") if p.stem.lower() != "template"]
    for p in tmx_files:
        if re.search(r"_\d+$", p.stem):     # the build names a long level's pieces like that
            raise SystemExit(f"Level file name '{p.name}' can't end in _ and a number.")

    def sort_key(p):
        info = level_info(p.stem.lower())
        return (MENUS.index(info["menu"]), info["order"], p.stem.lower())

    return sorted(tmx_files, key=sort_key)

def level_title(stem):
    return level_info(stem)["title"]

def level_song(stem):
    """The song this level plays (the name of its file in the music folders), or None"""
    return level_info(stem)["song"]

def write_save_slot(tmx_path, slot):
    with tmx_path.open(encoding="utf-8", newline="") as f:
        text = f.read()
    nl = "\r\n" if "\r\n" in text else "\n"
    line = f' <property name="Save slot" type="int" value="{slot}"/>{nl}'
    end, first_tileset = text.find(" </properties>"), text.find("<tileset")
    if end != -1 and end < first_tileset:
        text = text[:end] + " " + line + text[end:]
    else:                                 # no map properties at all
        at = text.index(">", text.index("<map")) + 1 + len(nl)
        text = text[:at] + f" <properties>{nl} {line} </properties>{nl}" + text[at:]
    with tmx_path.open("w", encoding="utf-8", newline="") as f:
        f.write(text)

def give_save_slots():
    """A level keeps its save slot for good, whatever happens to the order: one without any gets
    the lowest free slot, written in its TMX"""
    files = level_files()
    owner = {}
    for p in files:
        slot = level_info(p.stem.lower())["save"]
        if slot is None:
            continue
        if slot >= NUM_SAVE_SLOTS:
            raise SystemExit(f"Level '{p.stem}': the Save slot property goes up to {NUM_SAVE_SLOTS - 1}, not {slot}.")
        if slot in owner:
            raise SystemExit(f"Levels '{owner[slot]}' and '{p.stem}' both have Save slot {slot}. "
                             "Delete the property from one of them and build again: it gets a free one.")
        owner[slot] = p.stem
    for p in files:
        if level_info(p.stem.lower())["save"] is None:
            slot = next((s for s in range(NUM_SAVE_SLOTS) if s not in owner), None)
            if slot is None:
                raise SystemExit(f"All {NUM_SAVE_SLOTS} save slots are taken, so '{p.stem}' can't have one.")
            owner[slot] = p.stem
            write_save_slot(p, slot)
            print(f"Level {p.stem}: gets save slot {slot}")
    level_info.cache_clear()

def build_all():
    print("=== Pocket Dash Automated Level Pipeline ===")

    os.makedirs(LEVEL_DATA_DIR, exist_ok=True)
    os.makedirs(MUSIC_DIR, exist_ok=True)
    os.makedirs(SPRITES_DIR, exist_ok=True)
    os.makedirs(LEVELS_DIR, exist_ok=True)

    update_song_list()
    give_save_slots()
    tmx_files = level_files()
    print(f"Found {len(tmx_files)} level(s): {[p.stem for p in tmx_files]}")

    # Auto-export menuLoop if present
    menu_uge = ROOT_MUSIC_DIR / "menuLoop.uge"
    if menu_uge.exists() and UGE2SOURCE_BIN.exists():
        menu_c = SRC_DIR / "menuloop.c"
        cmd = [str(UGE2SOURCE_BIN), str(menu_uge), "-b", "1", "menuloop", str(menu_c)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            print(f"Exported menuLoop: {menu_uge.name} -> {menu_c.name} (Bank 1)")
        else:
            print(f"Warning: uge2source failed for menuLoop.uge: {res.stderr}")

    levels_info = []
    songs = song_files()
    made_songs = {}     # the hUGETracker songs so far: the levels that play one share its data

    idents = [make_c_ident(p.stem.lower()) for p in tmx_files]
    taken = used_banks([SPRITES_DIR / f"{i}_sprites.c" for i in idents] +
                       [p for p in MUSIC_DIR.glob("*.c") if p.name not in HAND_WRITTEN_MUSIC])
    next_map_bank = FIRST_MAP_BANK
    BASE_SPRITE_BANK = 100
    BASE_MUSIC_BANK = 200
    LAST_MUSIC_BANK = 219
    next_music_bank = BASE_MUSIC_BANK
    # one bank per level's sprite list, from BASE_SPRITE_BANK (before the maps take banks)
    sprite_banks = []
    for _ in tmx_files:
        sprite_banks.append(allocate_banks(1, sprite_banks[-1] + 1 if sprite_banks else BASE_SPRITE_BANK, taken))
        taken.add(sprite_banks[-1])

    sys.path.insert(0, str(REPO_ROOT / "tools"))
    from tmx2sprites import extract_portals

    for idx, tmx_path in enumerate(tmx_files):
        stem = tmx_path.stem.lower()
        ident = make_c_ident(stem)
        sprite_bank = sprite_banks[idx]
        info = level_info(stem)
        title = info["title"]
        short_name = ident
        save_slot = info["save"]

        print(f"\n[{idx+1}/{len(tmx_files)}] Processing {title} ({stem})...")

        out_bin = LEVEL_DATA_DIR / f"{ident}.bin"
        export_binary_map(tmx_path, out_bin)
        width, height = get_map_dimensions(tmx_path)
        if width > MAP_MAX_COLS:
            raise SystemExit(f"{stem}: {width} columns, the maximum is {MAP_MAX_COLS}")
        print(f"  - Map binary: {out_bin.name} ({width}x{MAP_ROWS} metatiles, TMX height {height})")

        out_sprites_c = SPRITES_DIR / f"{ident}_sprites.c"
        extract_portals(str(tmx_path), str(out_sprites_c), ident, sprite_bank,
                        skip=saw_centre_deco(out_bin.read_bytes()))
        print(f"  - Sprites: {out_sprites_c.name} (Bank {sprite_bank})")

        chunks = (width + MAP_BANK_COLS - 1) // MAP_BANK_COLS
        map_bank = allocate_banks(chunks, next_map_bank, taken)
        taken |= set(range(map_bank, map_bank + chunks))
        next_map_bank = map_bank + chunks
        write_map_wrappers(ident, short_name, out_bin, width, map_bank)
        print(f"  - Level wrapper: level_{short_name}.c (Bank {map_bank}"
              + (f"..{map_bank + chunks - 1}, {chunks} chunks of {MAP_BANK_COLS} columns)" if chunks > 1 else ")"))

        song = info["song"]
        song_key = norm_name(song) if song else None
        if song and song_key not in songs:
            raise SystemExit(f"Level '{stem}': there's no song called '{song}'. Pick one from the Song property in Tiled.")
        uge_file = songs[song_key][2] if song else None
        music_ident = f"song_{song_key}"
        music_file = MUSIC_DIR / f"{music_ident}.c"
        divider = NO_SONG_DIVIDER

        if song_key in made_songs:
            has_music, music_bank, divider = made_songs[song_key]
            print(f"  - Music: {song}, already in the game")
        elif song and ((uge_file and UGE2SOURCE_BIN.exists()) or music_file.exists()):
            music_bank = allocate_banks(1, next_music_bank, taken)
            if music_bank > LAST_MUSIC_BANK:
                raise SystemExit("no free bank for the hUGETracker song of " + stem)
            taken.add(music_bank)
            next_music_bank = music_bank + 1
            if uge_file:
                uge_divider, uge_ticks = read_uge_tempo(uge_file)
                if uge_divider is not None:
                    divider = uge_divider
                    print(f"  - Music tempo: auto-detected TMA divider = {divider} (ticks={uge_ticks}) from {uge_file.name}")
                else:
                    # No timer: the song is meant to step once per VBlank. The game always drives
                    # the music from the 4096 Hz timer: TMA 187 = 4096 / 69 = 59.4 Hz, the nearest
                    # rate to VBlank (59.7 Hz).
                    divider = 187
                    print(f"  - Music tempo: {uge_file.name} steps on VBlank (no timer): divider = {divider}")
            res = None
            if uge_file and UGE2SOURCE_BIN.exists():
                cmd = [str(UGE2SOURCE_BIN), str(uge_file), "-b", str(music_bank), music_ident, str(music_file)]
                res = subprocess.run(cmd, capture_output=True, text=True)
            if res and res.returncode == 0:
                print(f"  - Music: exported {uge_file.name} -> {music_file.name} (Bank {music_bank})")
                has_music = True
            else:
                if res:
                    print(f"  - Warning: uge2source failed for {uge_file.name}: {res.stderr}")
                has_music = music_file.exists()
                if has_music:
                    update_music_bank(music_file, music_bank)
                    print(f"  - Music: Using existing {music_file.name} (Bank {music_bank})")
            made_songs[song_key] = (has_music, music_bank, divider)
        else:
            has_music = False
            music_bank = 0
            print(f"  - Music: no hUGETracker song ({song or 'silent'})")

        bg_color, g_color, max_fall_7 = level_settings(TMX_DIR / f"{stem}.tmx")

        levels_info.append({
            "stem": stem,
            "ident": ident,
            "music_ident": music_ident,
            "extra": info["menu"] == "Extra",
            "short_name": short_name,
            "save_slot": save_slot,
            "difficulty": info["difficulty"],
            "title": title,
            "width": width,
            "height": MAP_ROWS,
            "map_top": max(0, MAP_ROWS - height),
            "map_bank": map_bank,
            "sprite_bank": sprite_bank,
            "music_bank": music_bank if has_music else 0,
            "divider": divider,
            "has_music": has_music,
            "bg_color": bg_color,
            "g_color": g_color,
            "max_fall_7": max_fall_7
        })

    # src/levels/ only holds generated map wrappers: drop those of levels that are gone
    wanted = {f"level_{l['short_name']}" for l in levels_info}
    for p in LEVELS_DIR.glob("level_*.c"):
        if re.sub(r"_\d+$", "", p.stem) not in wanted:
            print(f"Removing stale {p.name}")
            p.unlink()
    # ...and the songs no level plays any more
    wanted = {f"{l['music_ident']}.c" for l in levels_info if l["has_music"]} | HAND_WRITTEN_MUSIC
    for p in MUSIC_DIR.glob("*.c"):
        if p.name not in wanted:
            print(f"Removing stale {p.name}")
            p.unlink()
    subprocess.run([sys.executable, "tools/make_music_beats.py"], cwd=str(REPO_ROOT), check=True, stdout=subprocess.DEVNULL)

    # Furnace songs: music/Furnace/*.fur, each level's is its Song property
    print("\nConverting Furnace songs (music/Furnace)...")
    import convert_furnace
    convert_furnace.convert([p.stem.lower() for p in tmx_files])

    print("\nGenerating src/assets.c...")
    generate_assets_c(levels_info)

    if (REPO_ROOT / "endStart_02.ogg").exists():
        print("Converting level complete SFX...")
        subprocess.run([sys.executable, "tools/convert_end_sfx.py"], cwd=str(REPO_ROOT), check=True)

    # Background tiles of every level, from levels/chr_data/game_art/bg_tiles.png (needs assets.c)
    print("Building level background tiles from bg_tiles.png...")
    subprocess.run([sys.executable, "tools/build_bg_tiles.py"], cwd=str(REPO_ROOT), check=True)

    # Gameplay sprite tiles, from levels/chr_data/game_art/sprite_tiles.png
    print("Building sprite tiles from sprite_tiles.png...")
    subprocess.run([sys.executable, "tools/build_sprite_tiles.py"], cwd=str(REPO_ROOT), check=True)

    # The sky and ground each level shows in Tiled, coloured by its triggers
    subprocess.run([sys.executable, "tools/level_previews.py"], cwd=str(REPO_ROOT), check=True)

    print("\n=== Level Pipeline Completed Successfully! ===")

if __name__ == "__main__":
    from keep_mtimes import keep_unchanged
    with keep_unchanged():
        build_all()
