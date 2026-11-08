import xml.etree.ElementTree as ET
import os
import sys
import argparse
from pathlib import Path

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
    map_height = int(root.attrib['height'])

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

    # bg colour triggers (128..175), block outline colour triggers (176..191, CGB only) and
    # ground colour triggers (192..239) keep their ids
    for i in range(128, 192):
        portals_map[sprites_firstgid + i] = i
    for i in range(192, 240):
        portals_map[sprites_firstgid + i] = i

    # colour trigger fade time markers (sprites.png 246..251, tools/make_fade_time_icons.py): a
    # marker anywhere in a column sets the fade time of the colour triggers of that column (no
    # marker: 0.3 s). Written as the triggers' y (include/famidash_bg.h FAMIDASH_FADE_*): the
    # engine never reads a colour trigger's position other than its x.
    fade_markers = {
        sprites_firstgid + 246: 1,  # instant
        sprites_firstgid + 247: 2,  # 0.5 s
        sprites_firstgid + 248: 3,  # 1 s
        sprites_firstgid + 249: 4,  # 2 s
        sprites_firstgid + 250: 5,  # 4 s
        sprites_firstgid + 251: 6,  # 10 s
    }
    fade_of_col = {}

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
            if gid in fade_markers:
                if fade_of_col.get(col, fade_markers[gid]) != fade_markers[gid]:
                    print(f"Error: {tmx_filepath}: two different fade time markers in column {col}")
                    sys.exit(1)
                fade_of_col[col] = fade_markers[gid]
                continue
            if gid not in portals_map:
                continue
            obj_id = portals_map[gid]
            y_px = max(0, top + rows_above)
            if 128 <= obj_id < 176 or 192 <= obj_id < 240:
                y_px = ('fade', col)   # the column's fade time, once every marker is read
            portal_data.append((x_px, y_px, obj_id))

    if not sp_layer_found:
        print(f"Warning: 'SP' object layer was not found in {tmx_filepath}. Outputting terminator only.")

    portal_data = [(x, fade_of_col.get(y[1], 0) if isinstance(y, tuple) else y, o) for x, y, o in portal_data]

    if skip is not None:
        portal_data = [p for p in portal_data if not skip(*p)]

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
