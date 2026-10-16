import os
import sys
import glob
import re
import struct
import subprocess
import json
import xml.etree.ElementTree as ET
from pathlib import Path

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

KNOWN_LEVELS = {
    "stereomadness": {"title": "STEREO MADNESS", "divider": 192, "order": 1, "short": "sm"},
    "backontrack":   {"title": "BACK ON TRACK",   "divider": 184, "order": 2, "short": "bot"},
    "polargeist":    {"title": "POLARGEIST",      "divider": 193, "order": 3, "short": "pg"},
    "dryout":        {"title": "DRY OUT",         "divider": 185, "order": 4, "short": "du"},
    "baseafterbase": {"title": "BASE AFTER BASE", "divider": 183, "order": 5, "short": "bab"},
    "cantletgo":     {"title": "CANT LET GO",     "divider": 196, "order": 6, "short": "clg"},
    "jumper":        {"title": "JUMPER",          "divider": 142, "order": 7, "short": "ju"},
    "timemachine":   {"title": "TIME MACHINE",    "divider": 41,  "order": 8, "short": "tm"},
    "cycles":        {"title": "CYCLES",          "divider": 183, "order": 9, "short": "cy"},
    "xstep":         {"title": "XSTEP",           "divider": 138, "order": 10, "short": "xs"},
    "clutterfunk":   {"title": "CLUTTERFUNK",     "divider": 195, "order": 11, "short": "cf"},
    # "save": the level's save slot when it is not order - 1 (slots never move: the order can)
    "theoryofeverything": {"title": "THEORY OF EVERYTHING", "divider": 183, "order": 12, "short": "toe", "save": 12},
    "electromanadventures": {"title": "ELECTROMAN ADVENTURES", "divider": 183, "order": 13, "short": "ea", "save": 13},
    # Extra levels (the wrench in the main menu): the other Famidash levels Pocket Dash can play,
    # easiest first. "song": play that level's song (Famidash uses it too).
    # Best Automatic Lvl, Every Madness and Clubstep only use objects Pocket Dash has, but could not
    # be cleared yet (normal size spike collision is not Famidash's: side spikes kill on more of the
    # tile): their .tmx is not in levels/chr_data/tmx, the entries keep their order and save slot.
    "bestautomaticlvl": {"title": "BEST AUTOMATIC LVL", "divider": 183, "order": 14, "short": "bal", "save": 14, "extra": True, "song": "dryout"},
    "madness":       {"title": "MADNESS",         "divider": 183, "order": 15, "short": "mad", "save": 15, "extra": True},
    "leveleasy":     {"title": "LEVEL EASY",      "divider": 183, "order": 16, "short": "le", "save": 16, "extra": True, "song": "stereomadness"},
    "subzero":       {"title": "SUBZERO",         "divider": 183, "order": 17, "short": "sz", "save": 17, "extra": True, "song": "cantletgo"},
    "ultiatedestruction": {"title": "UTLIMATE DESTCTN", "divider": 183, "order": 18, "short": "ultiatedestruction", "save": 11, "extra": True},
    "everymadness":  {"title": "EVERY MADNESS",   "divider": 183, "order": 19, "short": "em", "save": 18, "extra": True},
    "demonpark":     {"title": "DEMON PARK",      "divider": 183, "order": 20, "short": "dp", "save": 19, "extra": True, "song": "timemachine"},
    "clubstep":      {"title": "CLUBSTEP",        "divider": 183, "order": 21, "short": "cs", "save": 20, "extra": True},
}
# Save slots (include/save_manager.h NUM_SAVE_LEVELS); a level not listed above has none
NO_SAVE_SLOT = 255

def load_level_colors(level_name, metadata_dir=None):
    if metadata_dir is None:
        metadata_dir = REPO_ROOT / "levels" / "metadata"
    target = level_name.lower().replace("_", "").replace("-", "")
    for mf in metadata_dir.glob("*.json"):
        try:
            with open(mf, "r", encoding="utf-8") as fp:
                data = json.load(fp)
            all_lvls = data.get("official_levels", []) + data.get("community_levels", [])
            for lvl in all_lvls:
                cur_name = lvl.get("level", "").lower().replace("_", "").replace("-", "")
                if cur_name == target:
                    return (lvl.get("startingBackgroundColor", 17), lvl.get("startingGroundColor", 17))
        except Exception:
            continue
    return (17, 17)

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

def find_uge_file(stem):
    """
    Finds a matching .uge file in music/ for a given level stem.
    Normalizes names (alphanumeric only) and handles common aliases.
    """
    if not ROOT_MUSIC_DIR.exists():
        return None
    norm_stem = re.sub(r'[^a-z0-9]', '', stem.lower())
    aliases = {
        'ultiatedestruction': 'ultimate',
        'ultimatedestruction': 'ultimate',
    }
    target = aliases.get(norm_stem, norm_stem)
    for p in ROOT_MUSIC_DIR.glob('*.uge'):
        if re.sub(r'[^a-z0-9]', '', p.stem.lower()) == target:
            return p
    return None

def format_title(stem):
    return stem.replace("_", " ").replace("-", " ").upper()

def make_c_ident(name):
    return re.sub(r'[^a-zA-Z0-9_]', '_', name.lower())

def get_map_dimensions(tmx_path):
    tree = ET.parse(tmx_path)
    root = tree.getroot()
    width = int(root.attrib.get('width', 0))
    height = int(root.attrib.get('height', 0))
    return width, height

def export_binary_map(tmx_path, out_bin_path):
    tree = ET.parse(tmx_path)
    root = tree.getroot()

    width = int(root.attrib['width'])
    height = int(root.attrib['height'])

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
    """First run of `count` consecutive free banks at or after `start`."""
    b = start
    while True:
        run = list(range(b, b + count))
        if run[-1] > LAST_BANK:
            raise SystemExit("no %d consecutive free ROM banks for a level map" % count)
        if not taken & set(run):
            return b
        b += 1


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
            f.write(f'  {lvl["g_color"]}\n')
            f.write('};\n\n')

        # game_levels array
        f.write('// Global level list used by the menu and gameplay systems\n')
        f.write('const Level* const game_levels[] = {\n')
        for lvl in levels:
            f.write(f'  &level_{lvl["short_name"]},\n')
        f.write('};\n')
        f.write('const uint8_t MAX_LEVELS = sizeof(game_levels) / sizeof(game_levels[0]);\n\n')

        # the main levels (the level select of PLAY) come first, then the extra ones (the wrench)
        n_main = sum(1 for l in levels if l["stem"] in KNOWN_LEVELS and not l["extra"])
        n_extra = sum(1 for l in levels if l["extra"])
        assert all(not l["extra"] for l in levels[:n_main])
        # the main levels' saves are in WRAM (include/save_manager.h NUM_RAM_SAVE_LEVELS)
        assert all(l["save_slot"] < 16 for l in levels[:n_main])
        assert all(l["extra"] for l in levels[n_main:n_main + n_extra])
        f.write(f'const uint8_t NUM_MAIN_LEVELS = {n_main};\n')
        f.write(f'const uint8_t NUM_EXTRA_LEVELS = {n_extra};\n\n')

        # save slots: a level keeps its slot when the level order changes
        f.write('// Save slot of each level (game_levels order)\n')
        f.write('const uint8_t level_save_slot[] = {\n')
        for lvl in levels:
            f.write(f'  {lvl["save_slot"]}u, // level_{lvl["short_name"]}\n')
        f.write('};\n')

def level_files():
    """The levels' TMX files in game order (a level's index is its position here)."""
    tmx_files = [p for p in TMX_DIR.glob("*.tmx") if p.stem.lower() != "template"]

    def sort_key(p):
        stem = p.stem.lower()
        if stem in KNOWN_LEVELS:
            return (0, KNOWN_LEVELS[stem]["order"], stem)
        return (1, 999, stem)

    return sorted(tmx_files, key=sort_key)

def level_title(stem):
    return KNOWN_LEVELS[stem]["title"] if stem in KNOWN_LEVELS else format_title(stem)

def build_all():
    print("=== Pocket Dash Automated Level Pipeline ===")

    os.makedirs(LEVEL_DATA_DIR, exist_ok=True)
    os.makedirs(MUSIC_DIR, exist_ok=True)
    os.makedirs(SPRITES_DIR, exist_ok=True)
    os.makedirs(LEVELS_DIR, exist_ok=True)

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

    idents = [make_c_ident(p.stem.lower()) for p in tmx_files]
    taken = used_banks([SPRITES_DIR / f"{i}_sprites.c" for i in idents] +
                       [MUSIC_DIR / f"{i}.c" for i in idents])
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
        meta = KNOWN_LEVELS.get(stem, {})

        if stem in KNOWN_LEVELS:
            title = meta["title"]
            divider = meta["divider"]
            short_name = meta["short"]
            save_slot = meta.get("save", meta["order"] - 1)
        else:
            title = format_title(stem)
            divider = 180
            short_name = ident
            save_slot = NO_SAVE_SLOT

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

        music_file = MUSIC_DIR / f"{ident}.c"
        uge_file = find_uge_file(stem)
        song_of = next((l for l in levels_info if l["stem"] == meta.get("song")), None)
        music_ident = ident
        if song_of or (uge_file and UGE2SOURCE_BIN.exists()) or music_file.exists():
            if not song_of:
                music_bank = allocate_banks(1, next_music_bank, taken)
                if music_bank > LAST_MUSIC_BANK:
                    raise SystemExit("no free bank for the hUGETracker song of " + stem)
                taken.add(music_bank)
                next_music_bank = music_bank + 1

        if song_of:
            # another level's song (Famidash plays it there too): the same song data
            if music_file.exists():
                music_file.unlink()
            has_music = song_of["has_music"]
            music_ident = song_of["music_ident"]
            music_bank = song_of["music_bank"]
            divider = song_of["divider"]
            print(f"  - Music: {song_of['title']}'s song")
        elif uge_file and UGE2SOURCE_BIN.exists():
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

            cmd = [str(UGE2SOURCE_BIN), str(uge_file), "-b", str(music_bank), ident, str(music_file)]
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode == 0:
                print(f"  - Music: exported {uge_file.name} -> {music_file.name} (Bank {music_bank})")
                has_music = True
            else:
                print(f"  - Warning: uge2source failed for {uge_file.name}: {res.stderr}")
                has_music = music_file.exists()
                if has_music:
                    update_music_bank(music_file, music_bank)
        elif music_file.exists():
            has_music = True
            update_music_bank(music_file, music_bank)
            print(f"  - Music: Using existing {music_file.name} (Bank {music_bank})")
        else:
            has_music = False
            print(f"  - Music: Not found for {stem} (Will play silent)")

        bg_color, g_color = load_level_colors(stem)

        levels_info.append({
            "stem": stem,
            "ident": ident,
            "music_ident": music_ident,
            "extra": meta.get("extra", False),
            "short_name": short_name,
            "save_slot": save_slot,
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
            "g_color": g_color
        })

    # src/levels/ only holds generated map wrappers: drop those of levels that are gone
    wanted = {f"level_{l['short_name']}" for l in levels_info}
    for p in LEVELS_DIR.glob("level_*.c"):
        if re.sub(r"_\d+$", "", p.stem) not in wanted:
            print(f"Removing stale {p.name}")
            p.unlink()

    # Furnace songs: music/Furnace/*.fur, matched to the levels by name like the .uge files
    print("\nConverting Furnace songs (music/Furnace)...")
    import convert_furnace
    convert_furnace.convert([p.stem.lower() for p in tmx_files])

    print("\nGenerating src/assets.c...")
    generate_assets_c(levels_info)

    if (REPO_ROOT / "endStart_02.ogg").exists():
        print("Converting level complete SFX...")
        subprocess.run([sys.executable, "tools/convert_end_sfx.py"], cwd=str(REPO_ROOT), check=True)

    # Background tiles of every level, from levels/chr_data/bg_tiles.png (needs assets.c)
    print("Building level background tiles from bg_tiles.png...")
    subprocess.run([sys.executable, "tools/build_bg_tiles.py"], cwd=str(REPO_ROOT), check=True)

    # Gameplay sprite tiles, from levels/chr_data/sprite_tiles.png
    print("Building sprite tiles from sprite_tiles.png...")
    subprocess.run([sys.executable, "tools/build_sprite_tiles.py"], cwd=str(REPO_ROOT), check=True)

    print("\n=== Level Pipeline Completed Successfully! ===")

if __name__ == "__main__":
    build_all()