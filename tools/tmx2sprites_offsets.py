"""Famidash's per-level object offsets (levels/metadata): only tools/sp_to_objects.py still needs them."""
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

def _engine_ids():
    sprites_firstgid = 0
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


    return portals_map


ENGINE_ID = _engine_ids()


def offsets_for(level):
    return load_metadata_for_level(level)
