import xml.etree.ElementTree as ET
import os
import sys
import argparse
import json
from pathlib import Path

def load_metadata_for_level(level_name, metadata_dir=None):
    """Searches levels/metadata/*.json for the specified level and returns its offset dictionary."""
    if metadata_dir is None:
        metadata_dir = Path(__file__).resolve().parent.parent / "levels" / "metadata"

    if not metadata_dir.is_dir():
        return {}

    target = level_name.lower().replace("_", "").replace("-", "")

    for mf in metadata_dir.glob("*.json"):
        try:
            with open(mf, 'r', encoding='utf-8') as fp:
                data = json.load(fp)
            g_offsets = data.get('globalObjectOffsets', [])
            all_lvls = data.get('official_levels', []) + data.get('community_levels', [])
            for lvl in all_lvls:
                cur_name = lvl.get('level', '').lower().replace("_", "").replace("-", "")
                if cur_name == target:
                    l_offsets = lvl.get('objectOffsets', [])
                    return build_offset_dict(g_offsets, l_offsets)
        except Exception:
            continue
    return {}

def build_offset_dict(global_settings, level_settings):
    """Builds offset dictionary matching FamiDash export_levels.py getDictFromOffsetSettings."""
    out = {}
    for setting_list in [global_settings, level_settings]:
        for s in setting_list:
            # FamiDash adds +8px to pads (NES 8x8 sprites). Our pads are already drawn
            # low in their 8x16 tile, so the +8px would sink them into the floor.
            if s.get('objectID'):
                oids = s['objectID'] if isinstance(s['objectID'], list) else [s['objectID']]
                if any(o in [10, 13, 37, 82, 86, 253] for o in oids) and s.get('offsetY', 0) == 8:
                    continue

            off = s.get('offset')
            if not off:
                off = [s.get('offsetX', 0), s.get('offsetY', 0)]
            if off == [0, 0]:
                continue
            if 'coordinates' in s:
                coords = s['coordinates']
                if len(coords) == 2 and isinstance(coords[0], int):
                    coords = [coords]
                for c in coords:
                    c = tuple(c)
                    if c not in out or s.get('override'):
                        out[c] = (off, s.get('override', False))
                    else:
                        prev = out[c][0]
                        out[c] = ([prev[0] + off[0], prev[1] + off[1]], False)
            elif 'objectID' in s:
                oids = s['objectID'] if isinstance(s['objectID'], list) else [s['objectID']]
                for o in oids:
                    if o not in out or s.get('override'):
                        out[o] = off
                    else:
                        prev = out[o]
                        out[o] = [prev[0] + off[0], prev[1] + off[1]]
    return out

def famidash_engine_ids():
    """Famidash sprite id (its sprite sheet's tile) -> engine object id, for the TMXs that still
    use the Famidash sprite sheet (levels/objects/sprites.tsx has these as engine_id)"""
    # sprite sheet tile -> engine object id
    ids = {
        0:  0,   # Cube portal
        1:  1,   # Ship portal
        2:  2,   # Ball portal
        3:  3,   # UFO portal (1.5)
        5:  5,   # Blue Orb (Gravity)
        6:  6,   # Pink Orb
        8:  8,   # Normal gravity trigger
        9:  9,   # Inverted gravity trigger
        10: 10,  # Yellow Pad
        11: 11,  # Yellow Orb
        12: 12,  # Yellow Pad (upside down)
        13: 13,  # Blue Pad
        14: 14,  # Blue Pad (upside down)
        15: 15,  # Level end
        16: 16,  # Normal gravity portal (horizontal down)
        17: 17,  # Normal gravity portal (horizontal up)
        18: 18,  # Inverted gravity portal (horizontal down)
        19: 19,  # Inverted gravity portal (horizontal up)
        7:  7,   # Coin 1 (1.4)
        24: 24,  # Mini portal (1.4)
        25: 25,  # Growth portal (1.4)
        26: 26,  # Coin 2
        27: 27,  # Coin 3
        37: 37,  # Pink pad
        38: 4,   # Pink pad upside down (engine id 4: Famidash's robot portal, unused)
        73: 73,  # Pulsing ball decoration
        74: 74,  # Music note decoration
        82: 20,  # Red pad (engine id 20: free below 38, so DMG keeps it)
        83: 21,  # Red pad upside down
        # 242 = PLAYER_TRAILS_ON: Famidash's cosmetic player trail, not supported
        126: 126,  # test
        121: 121,  # test
        253: 13,  # Blue Pad (invisible)
        254: 14,  # Blue Pad (invisible)
    }

    for i in range(42, 64):
        ids[i] = i

    # bg colour triggers (128..175), block outline colour triggers (176..191, CGB only) and
    # ground colour triggers (192..239) keep their ids
    for i in range(128, 192):
        ids[i] = i
    for i in range(192, 240):
        ids[i] = i

    return ids


def sprite_tileset(root, tmx_filepath):
    """(firstgid, {gid: engine object id}) of a TMX's sprite tileset: levels/objects/sprites.tsx
    (its tiles' engine_id property), or the old embedded 'sprites' tileset (famidash_engine_ids)"""
    for tileset in root.findall('tileset'):
        source = tileset.get('source') or ''
        if source.endswith('sprites.tsx'):
            firstgid = int(tileset.get('firstgid'))
            tsx = ET.parse(os.path.join(os.path.dirname(os.path.abspath(tmx_filepath)), source)).getroot()
            ids = {}
            for tile in tsx.findall('tile'):
                props = {p.get('name'): p.get('value') for p in tile.iter('property')}
                if int(props.get('engine_id', -1)) >= 0:
                    ids[firstgid + int(tile.get('id'))] = int(props['engine_id'])
            return firstgid, ids
        if tileset.get('name') == 'sprites':
            firstgid = int(tileset.get('firstgid'))
            return firstgid, {firstgid + k: v for k, v in famidash_engine_ids().items()}
    return None, {}


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

    sprites_firstgid, portals_map = sprite_tileset(root, tmx_filepath)
    if sprites_firstgid is None:
        print(f"Error: Could not find a tileset named 'sprites' (or sprites.tsx) in {tmx_filepath}.")
        sys.exit(1)

    offset_dict = load_metadata_for_level(file_base_name)
    if offset_dict:
        print(f"  - Loaded {len(offset_dict)} FamiDash offset rules for {file_base_name}")

    portal_data = []

    sp_layer_found = False
    for layer in root.findall('layer'):
        if layer.get('name') == 'SP':
            sp_layer_found = True
            data_element = layer.find('data')

            if data_element is not None and data_element.get('encoding') == 'csv':
                csv_data = data_element.text.replace('\n', '').replace('\r', '') if data_element.text else ""
                tiles = csv_data.split(',')

                # Maps are MAP_ROWS (64) rows tall and bottom-aligned (tools/build_levels.py)
                start_y = map_height - 64

                for index, tile_str in enumerate(tiles):
                    if not tile_str.strip():
                        continue

                    try:
                        tile_id = int(tile_str.strip())
                    except ValueError:
                        continue

                    if tile_id in portals_map:
                        col = index % map_width
                        row = index // map_width
                        obj_id = portals_map[tile_id]

                        offsetA = offset_dict.get(obj_id, [0, 0])
                        offsetB, override = offset_dict.get((col, row), [[0, 0], False])
                        if override:
                            dx = offsetB[0]
                            dy = offsetB[1]
                        else:
                            dx = offsetA[0] + offsetB[0]
                            dy = offsetA[1] + offsetB[1]

                        row_16 = row - start_y

                        x_px = col * 16 + dx
                        if row_16 < 0:
                            y_px = 0
                        else:
                            y_px = max(0, row_16 * 16 + dy)

                        portal_data.append((x_px, y_px, obj_id))
            break

    if not sp_layer_found:
        print(f"Warning: 'SP' layer was not found in {tmx_filepath}. Outputting terminator only.")

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

            # DMG: no decorations (38..99) or ground colour triggers (192..239)
            dmg_data = [p for p in portal_data if p[2] < 38 or (100 <= p[2] < 176)]
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
    parser = argparse.ArgumentParser(description="Convert the Tiled SP layer into portal and object data.")
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
