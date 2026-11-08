import xml.etree.ElementTree as ET
import os
import re
import sys
import argparse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_famidash_bg_tables import NES
from gd_objects import ROWS_BELOW

SPRITES_FIRSTGID = 10001
# Colour trigger tiles of objects/sprites.tsx (properties Colour, Fade) -> engine ids: base + colour
BG_TRIGGER, GROUND_TRIGGER, OUTLINE_TRIGGER = 256, 257, 258
# Fade property -> include/famidash_bg.h FAMIDASH_FADE_* (written as the trigger's y: the engine
# never reads a colour trigger's position other than its x)
FADE_NAMES = {"0.3 s": 0, "instant": 1, "0.5 s": 2, "1 s": 3, "2 s": 4, "4 s": 5, "10 s": 6}
# Colours the BG and ground triggers can be: not Famidash's repeated blacks and its toggles
TRIGGER_COLOURS = [i for i in range(48) if i not in (13, 14, 29, 30, 31, 45, 46, 47)]
OUTLINE_NES = [int(v, 0) for v in re.search(r'outline_nes_color\[16\] = \{(.*?)\}',
    (Path(__file__).resolve().parent.parent / 'src' / 'famidash_bg.c').read_text(), re.S).group(1).replace(' ', '').replace('\n', '').split(',') if v]


def nearest_colour(value, choices, nes_of):
    """'#ffRRGGBB' -> the position in choices of the closest NES colour"""
    wanted = int(value[-6:], 16)
    def distance(i):
        have = nes_of(choices[i])
        return sum((((wanted >> s) & 255) - ((have >> s) & 255)) ** 2 for s in (16, 8, 0))
    return min(range(len(choices)), key=distance)


def trigger_id(tile, props, tmx):
    """The engine id of a colour trigger object"""
    colour = props.get('Colour', '#ff%06x' % NES[0x20])
    if tile == OUTLINE_TRIGGER:
        return 176 + nearest_colour(colour, OUTLINE_NES, lambda n: NES[n])
    i = TRIGGER_COLOURS[nearest_colour(colour, TRIGGER_COLOURS, lambda n: NES[n])]
    return (128 if tile == BG_TRIGGER else 192) + i

def extract_portals(tmx_filepath, output_c_filepath, file_base_name, bank=None, skip=None):
    """skip(x_px, y_px, obj_id) -> True drops that object"""
    try:
        tree = ET.parse(tmx_filepath)
        root = tree.getroot()
    except FileNotFoundError:
        print(f"Error: Could not find {tmx_filepath}")
        sys.exit(1)
    except ET.ParseError as e:
        print(f"Error parsing XML in {tmx_filepath}: {e}")
        sys.exit(1)

    if 'width' not in root.attrib or 'height' not in root.attrib:
        print(f"Error: TMX file {tmx_filepath} is missing width or height attributes.")
        sys.exit(1)
    map_width = int(root.attrib['width'])
    map_height = int(root.attrib['height']) - ROWS_BELOW

    sprites_firstgid = None
    for tileset in root.findall('tileset'):
        if tileset.get('source', '').endswith('sprites.tsx'):
            sprites_firstgid = int(tileset.get('firstgid'))
            break

    if sprites_firstgid is None:
        print(f"Error: {tmx_filepath} does not use objects/sprites.tsx.")
        sys.exit(1)

    # TMX global id -> engine object id
    portals_map = {
        sprites_firstgid + 0:  0,   # Cube portal
        sprites_firstgid + 1:  1,   # Ship portal
        sprites_firstgid + 2:  2,   # Ball portal
        sprites_firstgid + 3:  3,   # UFO portal (1.5)
        sprites_firstgid + 5:  5,   # Blue Orb (Gravity)
        sprites_firstgid + 6:  6,   # Pink Orb
        sprites_firstgid + 8:  8,   # Normal gravity trigger
        sprites_firstgid + 9:  9,   # Inverted gravity trigger
        sprites_firstgid + 10: 10,  # Yellow Pad
        sprites_firstgid + 11: 11,  # Yellow Orb
        sprites_firstgid + 12: 12,  # Yellow Pad (upside down)
        sprites_firstgid + 13: 13,  # Blue Pad
        sprites_firstgid + 14: 14,  # Blue Pad (upside down)
        sprites_firstgid + 15: 15,  # Level end
        sprites_firstgid + 16: 16,  # Normal gravity portal (horizontal down)
        sprites_firstgid + 17: 17,  # Normal gravity portal (horizontal up)
        sprites_firstgid + 18: 18,  # Inverted gravity portal (horizontal down)
        sprites_firstgid + 19: 19,  # Inverted gravity portal (horizontal up)
        sprites_firstgid + 7:  7,   # Coin 1 (1.4)
        sprites_firstgid + 24: 24,  # Mini portal (1.4)
        sprites_firstgid + 25: 25,  # Growth portal (1.4)
        sprites_firstgid + 26: 26,  # Coin 2
        sprites_firstgid + 27: 27,  # Coin 3
        sprites_firstgid + 37: 37,  # Pink pad
        sprites_firstgid + 38: 4,   # Pink pad upside down (engine id 4: Famidash's robot portal, unused)
        sprites_firstgid + 73: 73,  # Pulsing ball decoration
        sprites_firstgid + 74: 74,  # Music note decoration
        sprites_firstgid + 112: 65,  # Pulsing ring (your art; a Famidash cell the engine never used)
        sprites_firstgid + 113: 66,  # Pulsing heart
        sprites_firstgid + 82: 20,  # Red pad (engine id 20: free below 38, so DMG keeps it)
        sprites_firstgid + 83: 21,  # Red pad upside down
        # 242 = PLAYER_TRAILS_ON: Famidash's cosmetic player trail, not supported
        sprites_firstgid + 126: 126,  # test
        sprites_firstgid + 121: 121,  # test
        sprites_firstgid + 253: 13,  # Blue Pad (invisible)
        sprites_firstgid + 254: 14,  # Blue Pad (invisible)
    }

    for i in range(42, 64):
        portals_map[sprites_firstgid + i] = i

    portal_data = []

    # Maps are MAP_ROWS (64) rows tall and bottom-aligned (tools/build_levels.py)
    rows_above = (64 - map_height) * 16
    sp_layer_found = False
    for group in root.findall('objectgroup'):
        if group.get('name') != 'SP':
            continue
        sp_layer_found = True
        for obj in group.findall('object'):
            if obj.get('gid') is None:
                continue
            gid = int(obj.get('gid')) & 0x1FFFFFFF
            x_px = int(float(obj.get('x')))
            top = int(float(obj.get('y'))) - int(float(obj.get('height', 16)))
            col = x_px // 16
            tile = gid - sprites_firstgid
            props = {p.get('name'): p.get('value') for p in obj.findall('properties/property')}
            y_px = max(0, top + rows_above)
            if tile in (BG_TRIGGER, GROUND_TRIGGER, OUTLINE_TRIGGER):
                obj_id = trigger_id(tile, props, tmx_filepath)
                if tile != OUTLINE_TRIGGER:
                    y_px = FADE_NAMES.get(props.get('Fade', '0.3 s'))
                    if y_px is None:
                        print(f"Error: {tmx_filepath}: unknown Fade {props.get('Fade')!r} at x {x_px}")
                        sys.exit(1)
                portal_data.append((x_px, y_px, obj_id))
                continue
            if gid not in portals_map:
                continue
            portal_data.append((x_px, y_px, portals_map[gid]))

    if not sp_layer_found:
        print(f"Warning: 'SP' object layer was not found in {tmx_filepath}. Outputting terminator only.")

    if skip is not None:
        portal_data = [p for p in portal_data if not skip(*p)]

    # A level with no Level end object ends at its last column, where the real levels have it
    if not any(p[2] == 15 for p in portal_data):
        portal_data.append(((map_width - 1) * 16, map_height * 16 - 144 + rows_above, 15))

    portal_data.sort(key=lambda p: p[0])

    c_var_name = file_base_name.lower().replace(" ", "_").replace("-", "_")

    try:
        with open(output_c_filepath, 'w') as f:
            if bank is not None:
                f.write(f'#pragma bank {bank}\n')
            f.write('#include "assets.h"\n\n')

            if bank is not None:
                f.write(f'BANKREF({c_var_name}_sp)\n\n')

            f.write(f"// Extracted {len(portal_data)} objects from SP layer\n")
            f.write(f"const SpDef {c_var_name}_sp[] = {{\n")

            for x, y, obj in portal_data:
                f.write(f"    {{{x}, {y}, {obj}}},\n")

            f.write("    {0xFFFF, 0, 0}\n")
            f.write("};\n\n")

            # DMG: no decorations (38..99) but the arrow (51, the half diamond: it points the way),
            # no ground colour triggers (192..239)
            dmg_data = [p for p in portal_data if p[2] < 38 or p[2] == 51 or (100 <= p[2] < 176)]
            f.write(f"// Extracted {len(dmg_data)} gameplay-critical objects for DMG mode\n")
            f.write(f"const SpDef {c_var_name}_sp_dmg[] = {{\n")
            for x, y, obj in dmg_data:
                f.write(f"    {{{x}, {y}, {obj}}},\n")
            f.write("    {0xFFFF, 0, 0}\n")
            f.write("};\n")
    except IOError as e:
        print(f"Error writing to output file {output_c_filepath}: {e}")
        sys.exit(1)

    print(f"Success! Extracted {len(portal_data)} portals into {output_c_filepath}")

def main():
    parser = argparse.ArgumentParser(description="Convert the Tiled SP object layer into portal and object data.")
    parser.add_argument("input", type=Path, help="Input TMX map")
    parser.add_argument("-o", "--out-dir", type=Path, help="Output directory for .c file")
    parser.add_argument("-n", "--name", help="Output file stem name")
    parser.add_argument("-b", "--bank", type=int, help="ROM bank for the data")

    args = parser.parse_args()

    input_file = str(args.input)
    file_raw_name = args.name if args.name else args.input.stem

    if args.out_dir:
        output_file = str(args.out_dir / f"{file_raw_name}_sprites.c")
    else:
        base_path_without_ext = os.path.splitext(input_file)[0]
        output_file = f"{base_path_without_ext}_sprites.c"

    extract_portals(input_file, output_file, file_raw_name, args.bank)

if __name__ == "__main__":
    main()
