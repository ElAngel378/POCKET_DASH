"""The object table: every background object a level can place, keyed by GD object ID.

Levels are built from these objects (docs/PLAN_1.6.md). An object is placed at any 8 px position,
rotated in 90 degree steps or flipped, and objects stack. Objects GD has are keyed "gd<ID>"; the
pieces Famidash has and GD does not (its ground strips, invisible blocks, collision-shifted spikes,
...) are keyed "fd_<name>".

OBJECTS: key -> (Tiled tile id, name, size in px, occupied 8x8 quarters at no rotation or None =
all). The tile id is the object's id in levels/objects/gd_objects.tsx: the GD ID for GD objects,
5000 + n for Famidash ones. Never renumber them: the levels store these ids.

FAMIDASH: how each Famidash metatile is made of objects (tools/level_objects.py builds a level's
metatile map from its objects with it, and converts Famidash maps to objects with it).
    (metatile, variant) -> [(key, dx, dy, transform), ...]   bottom to top
dx, dy: the object's top left corner from the cell's top left corner (px, multiples of 8).
variant: None, or the Famidash tile set the metatile looks like this in: "B" (block set B),
"SB" / "SC" (spike sets B / C), "NS" (a level without big saws: metatile 120 is not a saw centre).
transform: "" (none), "H" / "V" (flipped), "R90" / "R180" / "R270" (rotated clockwise).
"""

# Tiled's flip flags: rotations are made of them (R90 = transpose, then flip horizontally)
FLIP_H, FLIP_V, FLIP_D = 0x80000000, 0x40000000, 0x20000000
TRANSFORMS = {"": 0, "H": FLIP_H, "V": FLIP_V, "R90": FLIP_D | FLIP_H,
              "R180": FLIP_H | FLIP_V, "R270": FLIP_D | FLIP_V, "D": FLIP_D,
              "DHV": FLIP_D | FLIP_H | FLIP_V}

FD_BASE = 5000

TOP = {(0, 0), (1, 0)}
BOTTOM = {(0, 1), (1, 1)}

OBJECTS = {
    # GD objects
    "gd1": (1, "Block", (16, 16), None),
    "gd2": (2, "Grid block, one edge", (16, 16), None),
    "gd3": (3, "Grid block, corner", (16, 16), None),
    "gd4": (4, "Grid block, corner dot", (16, 16), None),
    "gd5": (5, "Grid block, no outline", (16, 16), None),
    "gd6": (6, "Grid block, three edges", (16, 16), None),
    "gd7": (7, "Grid block, two sides", (16, 16), None),
    "gd8": (8, "Spike", (16, 16), None),
    "gd9": (9, "Ground spikes (pit)", (16, 16), BOTTOM),
    "gd39": (39, "Small spike", (16, 16), BOTTOM),
    "gd40": (40, "Slab", (16, 16), TOP),
    "gd61": (61, "Wavy ground spikes", (16, 16), BOTTOM),
    "gd83": (83, "Grid block, outlined", (16, 16), None),
    "gd88": (88, "Saw, big", (48, 48), None),
    "gd89": (89, "Saw, medium", (32, 32), None),
    "gd98": (98, "Saw, small", (16, 16), None),
    "gd103": (103, "Medium spike", (8, 8), None),
    "gd116": (116, "Brick block, outlined", (16, 16), None),
    "gd117": (117, "Brick block, one edge", (16, 16), None),
    "gd118": (118, "Brick block, corner", (16, 16), None),
    "gd119": (119, "Brick block, corner dot", (16, 16), None),
    "gd120": (120, "Brick fill", (16, 16), None),
    "gd121": (121, "Brick block, two sides", (16, 16), None),
    "gd122": (122, "Brick block, three edges", (16, 16), None),
    "gd135": (135, "Bush ground spikes", (16, 16), BOTTOM),
    "gd191": (191, "Fake spike", (16, 16), None),
    "gd198": (198, "Fake small spike", (16, 16), BOTTOM),
    "gd199": (199, "Fake medium spike", (8, 8), None),
}


# Objects with art of their own: levels/chr_data/objects/art/<key>.png (GB colours 0..3, 0 shows
# what is behind), CGB palette DRAWN_PAL (background decoration) and no collision unless DRAWN_KIND says. Their occupied
# quarters are the ones the art draws in. tools/import_gda_art.py made the files: your .chr art
# where it exists, GDA's for the rest for now.
DRAWN_PAL = 2
DRAWN = {
    "gd48": ("Cloud 1", (48, 16)),
    "gd49": ("Cloud 2", (48, 16)),
    "gd76": ("Light line 1", (16, 16)),
    "gd77": ("Light line 2", (16, 16)),
    "gd78": ("Light line 3", (16, 16)),
    "gd81": ("Light line 4", (16, 16)),
    "gd82": ("Light side line", (16, 16)),
    "gd85": ("Cogwheel, big (a quarter)", (24, 24)),
    "gd86": ("Cogwheel, medium", (32, 32)),
    "gd87": ("Cogwheel, small", (24, 24)),
    "gd97": ("Cogwheel, tiny", (16, 16)),
    "gd106": ("Chain, long", (16, 40)),
    "gd107": ("Chain, short", (16, 24)),
    "gd110": ("Chain, thin", (16, 24)),
    "gd113": ("Stepped ground hill, big", (64, 16)),
    "gd114": ("Stepped ground hill, medium", (48, 16)),
    "gd115": ("Stepped ground hill, small", (40, 16)),
    "gd123": ("Thorn, spiral", (16, 16)),
    "gd124": ("Thorn, hook", (16, 16)),
    "gd125": ("Thorn, sprig", (16, 16)),
    "gd126": ("Thorn, curl", (16, 16)),
    "gd127": ("Thorn, tiny", (16, 16)),
    "gd128": ("Thorn, big", (32, 24)),
    "gd129": ("Cloud, big", (64, 32)),
    "gd130": ("Cloud, medium", (48, 16)),
    "gd131": ("Cloud, small", (24, 16)),
    "gd137": ("Gear ring, big (a half)", (24, 48)),
    "gd138": ("Gear ring, medium", (32, 32)),
    "gd139": ("Gear ring, small", (16, 16)),
    # 1.6 (phase 4)
    "gd143": ("Breakable brick", (16, 16)),
    "gd144": ("Fading spike", (16, 16)),
    "gd145": ("Fading small spike", (16, 16)),
    "gd146": ("Fading block", (16, 16)),
    "gd151": ("Spiky art, tall", (8, 24)),
    "gd152": ("Spiky art, medium", (8, 16)),
    "gd153": ("Spiky art, small", (8, 16)),
    "gd154": ("Spiked wheel (a quarter)", (24, 24)),
    "gd155": ("Spiked wheel, three points", (40, 40)),
    "gd157": ("Wave hill, middle", (16, 16)),
    "gd158": ("Wave hill, rising", (16, 16)),
    "gd159": ("Wave hill, falling", (16, 16)),
    "gd161": ("1.6 outline, line", (16, 16)),
    "gd162": ("1.6 outline, corner", (16, 16)),
    "gd163": ("1.6 outline, dot", (16, 16)),
    "gd164": ("1.6 striped fill", (16, 16)),
    "gd165": ("1.6 outline, U", (16, 16)),
    "gd166": ("1.6 outline, side", (16, 16)),
    "gd167": ("1.6 outline, dot 2", (16, 16)),
    "gd169": ("1.6 outline, square", (16, 16)),
    "gd170": ("1.6 block", (16, 16)),
    "gd171": ("1.6 block, edge", (16, 16)),
    "gd173": ("1.6 block, corner", (16, 16)),
    "gd174": ("1.6 block, corner 2", (16, 16)),
    "gd175": ("1.6 block, whole", (16, 16)),
    "gd176": ("1.6 block, small", (16, 16)),
    "gd177": ("Ice spike", (16, 16)),
    "gd178": ("Ice spike, half", (16, 8)),
    "gd179": ("Ice spike, small", (16, 16)),
    "gd180": ("Cart wheel, big (a quarter)", (16, 16)),
    "gd181": ("Cart wheel, medium", (24, 24)),
    "gd182": ("Cart wheel, small", (16, 16)),
    "gd183": ("Black saw, big (a quarter)", (24, 24)),
    "gd184": ("Black saw, medium", (40, 40)),
    "gd185": ("Black saw, small", (24, 24)),
    "gd186": ("1.6 saw, big (a quarter)", (24, 24)),
    "gd187": ("1.6 saw, medium", (32, 32)),
    "gd188": ("1.6 saw, star", (32, 32)),
    "gd190": ("Chain art", (8, 32)),
    "gd192": ("1.6 block, middle", (16, 16)),
    "gd193": ("1.6 dark fill", (16, 16)),
    "gd194": ("1.6 block, narrow corner", (16, 16)),
    "gd197": ("1.6 block, narrow", (16, 16)),
}
# DRAWN objects that are not decoration: their quarters collide. Palette 0, like blocks and spikes.
DRAWN_KIND = {k: "solid" for k in ("gd143", "gd146", "gd164", "gd170", "gd171", "gd173", "gd174", "gd175",
                                   "gd176", "gd192", "gd193", "gd194", "gd197")}
DRAWN_KIND.update({k: "deadly" for k in ("gd144", "gd145", "gd177", "gd178", "gd179", "gd183", "gd184", "gd185", "gd186",
                                         "gd187", "gd188")})
# The 1.6 outlines are solid blocks (GDA: COL_FULL) though only their lines are drawn: they fill their
# whole cell for collision, and what is under them shows
OUTLINE = {"gd146", "gd161", "gd162", "gd163", "gd165", "gd166", "gd167", "gd169"}
DRAWN_KIND.update({k: "solid" for k in OUTLINE})
# Objects that spin (CGB): key -> how many of them make the whole wheel (1, 2 or 4: rotate them in
# Tiled, a flipped piece would turn the other way). The build rotates the art (tools/level_objects.py
# spin_frames) and the game swaps the tiles (bg_stream.c)
SPIN = {"gd85": 4, "gd86": 1, "gd87": 1, "gd97": 1, "gd137": 2, "gd138": 1, "gd139": 1, "gd154": 4,
        "gd155": 1, "gd180": 4, "gd181": 1, "gd182": 1, "gd183": 4, "gd184": 1, "gd185": 1, "gd186": 4,
        "gd187": 1, "gd188": 1}
SPIN_FRAMES = 3
# Fading objects vanish as the player reaches them (still solid / deadly): the cell has a second
# metatile without them, drawn once its column is near the player (mt_renderer.c fade_*)
FADE = {"gd144", "gd145", "gd146"}
# Hitboxes. Ice spikes collide like these spikes (key -> key, dy to add when not turned):
HIT_LIKE = {"gd144": ("gd8", 0), "gd145": ("gd39", 0), "gd177": ("gd8", 0), "gd178": ("gd39", -8),
            "gd179": ("gd39", 0)}
# Saws are GDA's circles, radius in px: the 8x8 quarters of the object inside it are deadly
HIT_RADIUS = {"gd183": 8, "gd184": 10, "gd185": 2, "gd186": 16, "gd187": 12, "gd188": 8}
# Black is colour 2 on CGB, 3 on DMG, and an outline on it the other way round: the files hold the
# CGB colours, DMG swaps 2 and 3 in these
DMG_SWAP = {"gd146", "gd161", "gd162", "gd163", "gd164", "gd165", "gd166", "gd167", "gd169", "gd183", "gd184",
            "gd185", "gd193"}


# As GDA colours them: most decorations in the player's colour 1 (palette 2), these in colour 2
# (palette 6). Hazards are in the object colour, white (palette 0).
DRAWN_P2 = {"gd106", "gd107", "gd137", "gd138", "gd139", "gd180", "gd181", "gd182", "gd190"}


def drawn_pal(key):
    return 0 if key in DRAWN_KIND else 6 if key in DRAWN_P2 else DRAWN_PAL


for _k, (_name, _size) in DRAWN.items():
    OBJECTS[_k] = (int(_k[2:]), _name, _size, None)

# Famidash's own pieces: append only (the position is the tile id)
FD = [
    ("fd_ground_edge_bottom", "Ground strip edge (bottom)", None),
    ("fd_ground_bottom", "Ground strip (bottom)", None),
    ("fd_ground_edge_top", "Ground strip edge (top)", None),
    ("fd_ground_top", "Ground strip (top)", None),
    ("fd_bg5", "Ground fill edge", None),
    ("fd_bg6", "Ground fill", None),
    ("fd_fake_block", "Fake block (no collision)", None),
    ("fd_bg_spikes", "Background spikes", None),
    ("fd_bg_spikes_top", "Background spikes (ceiling)", None),
    ("fd_bg_spikes_b", "Background spikes (Xstep)", None),
    ("fd_bg_spikes_top_b", "Background spikes, ceiling (Xstep)", None),
    ("fd_half_left_spike_bg", "Half spike on background spikes, left", None),
    ("fd_half_right_spike_bg", "Half spike on background spikes, right", None),
    ("fd_half_spike_bg", "Small spike on background spikes", None),
    ("fd_half_spike_bg_top", "Small spike on background spikes (ceiling)", None),
    ("fd_orb_ring", "Orb ring (the orb's hitbox)", None),
    ("fd_pad", "Pad (the pad's hitbox)", BOTTOM),
    ("fd_wavy_top_l", "Wavy platform top, left end", TOP),
    ("fd_wavy_top_m", "Wavy platform top", TOP),
    ("fd_wavy_top_r", "Wavy platform top, right end", TOP),
    ("fd_wavy_top_s", "Wavy platform top, single", TOP),
    ("fd_wavy_bot_l", "Wavy platform underside, left end", BOTTOM),
    ("fd_wavy_bot_m", "Wavy platform underside", BOTTOM),
    ("fd_wavy_bot_r", "Wavy platform underside, right end", BOTTOM),
    ("fd_wavy_bot_s", "Wavy platform underside, single", BOTTOM),
    ("fd_xstep_special", "Xstep grid strip", None),
    ("fd_xstep_special_b", "Xstep grid strip (bricks)", None),
    ("fd_xstep_diamond", "Xstep diamond", None),
    ("fd_chipped_block", "Chipped block", None),
    ("fd_cloud_top_up", "Cloud top", None),
    ("fd_cloud_top_down", "Cloud top, hanging", None),
    ("fd_cloud_left", "Cloud, left", None),
    ("fd_cloud_left_top", "Cloud, left top", None),
    ("fd_cloud_right", "Cloud, right", None),
    ("fd_cloud_right_top", "Cloud, right top", None),
    ("fd_small_block", "Small block (8 px)", None),
    ("fd_invis_small_block", "Invisible small block (8 px)", None),
    ("fd_invis_block", "Invisible block", None),
    ("fd_diag_spikes", "Diagonal spikes", None),
    ("fd_top_slab_checkered", "Slab on grid block (top)", None),
    ("fd_bottom_slab_checkered", "Slab under grid block", None),
    ("fd_bottom_slab_checkered_b", "Slab under brick block", None),
    ("fd_spike_colshift_r", "Spike, hitbox shifted right", None),
    ("fd_spike_colshift_l", "Spike, hitbox shifted left", None),
    ("fd_block_end_l", "Block, left half solid (Every End)", None),
    ("fd_block_end_r", "Block, right half solid (Every End)", None),
    ("fd_cb8_everyend", "Grid block corner (Every End)", None),
    ("fd_cb5_everyend", "Grid block corner, half solid (Every End)", None),
    ("fd_cb6_polargeist", "Grid block corner, half solid (Polargeist)", None),
    ("fd_x_full", "Dark pyramid block, outlined", None),
    ("fd_x_line", "Dark pyramid block, one edge", None),
    ("fd_x_corner", "Dark pyramid block, corner", None),
    ("fd_x_dot", "Dark pyramid block, corner dot", None),
    ("fd_x_opp", "Dark pyramid block, two sides", None),
    ("fd_x_none", "Dark pyramid fill", None),
    ("fd_x_three", "Dark pyramid block, three edges", None),
    ("fd_black_full", "Black block, outlined", None),
    ("fd_black_line", "Black block, one edge", None),
    ("fd_black_corner", "Black block, corner", None),
    ("fd_black_dot", "Black block, corner dot", None),
    ("fd_black_opp", "Black block, two sides", None),
    ("fd_black_none", "Black fill", None),
    ("fd_black_three", "Black block, three edges", None),
    # pieces of a big saw, for the few cells where Famidash's overlapping saws show pieces no
    # stacking order gives (Theory of Everything)
    ("fd_big_saw_tl", "Big saw piece, top left", None),
    ("fd_big_saw_tm", "Big saw piece, top", None),
    ("fd_big_saw_tr", "Big saw piece, top right", None),
    ("fd_big_saw_ml", "Big saw piece, left", None),
    ("fd_big_saw_m", "Big saw piece, centre", None),
    ("fd_big_saw_mr", "Big saw piece, right", None),
    ("fd_big_saw_bl", "Big saw piece, bottom left", None),
    ("fd_big_saw_bm", "Big saw piece, bottom", None),
    ("fd_big_saw_br", "Big saw piece, bottom right", None),
]
for n, (key, name, mask) in enumerate(FD):
    OBJECTS[key] = (FD_BASE + n, name, (8, 8) if "small_block" in key else (16, 16), mask)

# The outlined block families: 20 metatiles each, in this order (Famidash's CHECKERBOARD,
# CHECKERBOARD_1..19 / X_* / BLACK_*), as (shape, transform). Shapes at no rotation: "line" = top
# edge, "corner" = top and left, "dot" = top left dot, "opp" = left and right, "three" = open bottom.
FAMILY = [("full", ""), ("line", ""), ("line", "R90"), ("line", "R180"), ("line", "R270"),
          ("corner", ""), ("corner", "R90"), ("corner", "R180"), ("corner", "R270"),
          ("dot", ""), ("dot", "R90"), ("dot", "R180"), ("dot", "R270"),
          ("opp", ""), ("opp", "R90"), ("none", ""),
          ("three", ""), ("three", "R90"), ("three", "R180"), ("three", "R270")]
GRID = {"full": "gd83", "line": "gd2", "corner": "gd3", "dot": "gd4", "opp": "gd7", "none": "gd5", "three": "gd6"}
BRICK = {"full": "gd116", "line": "gd117", "corner": "gd118", "dot": "gd119", "opp": "gd121",
         "none": "gd120", "three": "gd122"}


def _family(first, keys, variant=None, skip=()):
    out = {}
    for i, (shape, tf) in enumerate(FAMILY):
        if first + i not in skip:
            out[(first + i, variant)] = [(keys[shape] if isinstance(keys, dict) else keys + shape, 0, 0, tf)]
    return out


FAMIDASH = {}
FAMIDASH.update(_family(32, GRID))
FAMIDASH.update(_family(32, BRICK, "B"))
FAMIDASH.update(_family(64, "fd_x_"))
# BLACK_* only differ from the default sheet in block set B levels; BLACK_NONE (111) looks the same
FAMIDASH.update(_family(96, "fd_black_", "B", skip=(111,)))


def _one(key, dx=0, dy=0, tf=""):
    return [(key, dx, dy, tf)]


FAMIDASH.update({
    (0, None): [],
    (1, None): _one("fd_ground_edge_bottom"), (2, None): _one("fd_ground_bottom"),
    (136, None): _one("fd_ground_edge_top"), (137, None): _one("fd_ground_top"),
    (5, None): _one("fd_bg5"), (6, None): _one("fd_bg6"),
    (7, None): _one("fd_fake_block"),
    (16, None): _one("gd1"),
    (111, None): _one("fd_black_none"),
    # slabs
    (25, None): _one("gd40"), (3, None): _one("gd40", 0, 8),
    # saws
    (125, None): _one("gd98"), (127, None): _one("gd98", 0, 8), (4, None): _one("gd98", 0, -8),
    (8, None): _one("gd89", 0, 0), (9, None): _one("gd89", -16, 0),
    (10, None): _one("gd89", 0, -16), (11, None): _one("gd89", -16, -16),
    (116, None): _one("gd88", 0, 0), (117, None): _one("gd88", -16, 0), (118, None): _one("gd88", -32, 0),
    (119, None): _one("gd88", 0, -16), (120, None): _one("gd88", -16, -16), (121, None): _one("gd88", -32, -16),
    (122, None): _one("gd88", 0, -32), (123, None): _one("gd88", -16, -32), (124, None): _one("gd88", -32, -32),
    (120, "NS"): _one("fd_xstep_diamond"),
    # background spikes
    (12, None): _one("fd_bg_spikes"), (13, None): _one("fd_bg_spikes_top"),
    (12, "SB"): _one("fd_bg_spikes_b"), (13, "SB"): _one("fd_bg_spikes_top_b"),
    (14, None): _one("fd_half_left_spike_bg"), (15, None): _one("fd_half_right_spike_bg"),
    (19, None): _one("fd_half_spike_bg"), (20, None): _one("fd_half_spike_bg_top"),
    # spikes
    (17, None): _one("gd8"), (27, None): _one("gd8", 0, 0, "V"),
    (26, None): _one("gd8", 0, 0, "R90"), (28, None): _one("gd8", 0, 0, "R270"),
    (244, None): _one("gd8", 8, 0), (245, None): _one("gd8", -8, 0),
    (246, None): [("gd8", -8, 0, ""), ("gd8", 8, 0, "")],
    (249, None): _one("gd8", 8, 0, "V"), (250, None): _one("gd8", -8, 0, "V"),
    (251, None): [("gd8", -8, 0, "V"), ("gd8", 8, 0, "V")],
    (18, None): _one("gd39"), (30, None): _one("gd39", 0, 0, "V"),
    (29, None): _one("gd39", 0, 0, "R90"), (31, None): _one("gd39", 0, 0, "R270"),
    (240, None): _one("gd39", 8, 0), (241, None): _one("gd39", -8, 0),
    (242, None): _one("gd39", 8, 0, "V"), (243, None): _one("gd39", -8, 0, "V"),
    (21, None): [("gd40", 0, 0, ""), ("gd39", 0, 0, "")],
    (55, None): [("gd40", 0, 0, ""), ("gd39", 0, 8, "V")],
    (199, None): [("gd40", 0, 8, ""), ("gd39", 0, -8, "")],
    (173, None): _one("gd103", 0, 8), (174, None): _one("gd103", 8, 8),
    (175, None): [("gd103", 0, 8, ""), ("gd103", 8, 8, "")],
    (190, None): _one("gd103", 0, 0, "V"), (191, None): _one("gd103", 8, 0, "V"),
    (192, None): [("gd103", 0, 0, "V"), ("gd103", 8, 0, "V")],
    (193, None): _one("fd_diag_spikes"), (194, None): _one("fd_diag_spikes", 0, 0, "H"),
    (195, None): _one("fd_diag_spikes", 0, 0, "V"), (196, None): _one("fd_diag_spikes", 0, 0, "R180"),
    (217, None): _one("fd_spike_colshift_r"), (218, None): _one("fd_spike_colshift_l"),
    # fake spikes
    (197, None): _one("gd191"), (198, None): _one("gd191", 0, 0, "V"),
    (203, None): _one("gd198"), (204, None): _one("gd198", 0, 0, "V"),
    (205, None): _one("gd199", 0, 8), (206, None): _one("gd199", 8, 8),
    (207, None): [("gd199", 0, 8, ""), ("gd199", 8, 8, "")],
    (208, None): _one("gd199", 0, 0, "V"), (209, None): _one("gd199", 8, 0, "V"),
    # ground spikes
    (24, None): _one("gd9"), (23, None): _one("gd9", 0, 0, "V"),
    (22, None): [("gd40", 0, 0, ""), ("gd9", 0, 0, "")],
    (94, None): _one("gd9", 0, 0, "R270"), (95, None): _one("gd9", 0, 0, "R90"),
    (94, "SC"): _one("gd135", 0, 0, "R270"), (95, "SC"): _one("gd135", 0, 0, "R90"),
    (92, None): _one("gd61"), (93, None): _one("gd61", 0, 0, "V"),
    (130, None): [("gd9", 0, 0, "V"), ("gd61", 0, 0, "")],
    (131, None): [("gd61", 0, 0, "V"), ("gd9", 0, 0, "")],
    # orbs and pads (their hitboxes: the orb and pad themselves are sprites)
    (52, None): _one("fd_orb_ring"),
    (232, None): _one("fd_orb_ring", 8, 0), (233, None): _one("fd_orb_ring", -8, 0),
    (238, None): _one("fd_orb_ring", 0, 8), (239, None): _one("fd_orb_ring", 0, -8),
    (53, None): _one("fd_pad"), (54, None): _one("fd_pad", 0, 0, "V"),
    (142, None): [("fd_pad", -8, 0, ""), ("fd_pad", 8, 0, "")],
    (234, None): _one("fd_pad", 8, 0), (235, None): _one("fd_pad", -8, 0),
    (236, None): _one("fd_pad", 8, 0, "V"), (237, None): _one("fd_pad", -8, 0, "V"),
    # wavy platforms
    (84, None): _one("fd_wavy_top_l"), (85, None): _one("fd_wavy_top_m"),
    (86, None): _one("fd_wavy_top_r"), (87, None): _one("fd_wavy_top_s"),
    (88, None): _one("fd_wavy_bot_l"), (89, None): _one("fd_wavy_bot_m"),
    (90, None): _one("fd_wavy_bot_r"), (91, None): _one("fd_wavy_bot_s"),
    (56, None): [("fd_wavy_top_l", 0, 0, ""), ("fd_wavy_bot_l", 0, 0, "")],
    (57, None): [("fd_wavy_top_m", 0, 0, ""), ("fd_wavy_bot_m", 0, 0, "")],
    (58, None): [("fd_wavy_top_r", 0, 0, ""), ("fd_wavy_bot_r", 0, 0, "")],
    (60, None): [("fd_wavy_top_l", 0, 0, ""), ("gd61", 0, 0, "")],
    (61, None): [("fd_wavy_top_m", 0, 0, ""), ("gd61", 0, 0, "")],
    (62, None): [("fd_wavy_top_r", 0, 0, ""), ("gd61", 0, 0, "")],
    # Xstep
    (59, None): _one("fd_xstep_special"), (59, "B"): _one("fd_xstep_special_b"),
    # Clutterfunk's chipped blocks
    (126, "B"): _one("fd_chipped_block"),
    (140, "B"): _one("fd_chipped_block", 8, 0), (141, "B"): _one("fd_chipped_block", -8, 0),
    (138, "B"): [("gd61", 0, 0, ""), ("fd_chipped_block", 8, 0, "")],
    (139, "B"): [("gd61", 0, 0, ""), ("fd_chipped_block", -8, 0, "")],
    # clouds
    (128, None): _one("fd_cloud_top_up"), (129, None): _one("fd_cloud_top_down"),
    (132, None): _one("fd_cloud_left"), (133, None): _one("fd_cloud_left_top"),
    (134, None): _one("fd_cloud_right"), (135, None): _one("fd_cloud_right_top"),
    # small blocks
    (176, None): _one("fd_small_block", 0, 0), (177, None): _one("fd_small_block", 8, 0),
    (178, None): _one("fd_small_block", 0, 8), (179, None): _one("fd_small_block", 8, 8),
    (180, None): [("fd_small_block", 0, 0, ""), ("fd_small_block", 8, 0, "")],
    (181, None): [("fd_small_block", 0, 8, ""), ("fd_small_block", 8, 8, "")],
    (182, None): [("fd_small_block", 0, 0, ""), ("fd_small_block", 0, 8, "")],
    (183, None): [("fd_small_block", 8, 0, ""), ("fd_small_block", 8, 8, "")],
    (184, None): [("fd_small_block", 0, 0, ""), ("fd_small_block", 8, 0, ""), ("fd_small_block", 0, 8, "")],
    (185, None): [("fd_small_block", 0, 0, ""), ("fd_small_block", 8, 0, ""), ("fd_small_block", 8, 8, "")],
    (186, None): [("fd_small_block", 0, 0, ""), ("fd_small_block", 0, 8, ""), ("fd_small_block", 8, 8, "")],
    (187, None): [("fd_small_block", 8, 0, ""), ("fd_small_block", 0, 8, ""), ("fd_small_block", 8, 8, "")],
    (168, None): [("gd103", 0, 0, ""), ("fd_small_block", 0, 8, "")],
    (169, None): [("gd103", 8, 0, ""), ("fd_small_block", 8, 8, "")],
    (223, None): _one("fd_invis_small_block", 8, 0), (227, None): _one("fd_invis_small_block", 0, 0),
    (254, None): _one("fd_invis_small_block", 8, 8),
    (252, None): _one("fd_invis_block"),
    # one-offs
    (201, "B"): _one("fd_top_slab_checkered"),
    (202, None): _one("fd_bottom_slab_checkered"), (202, "B"): _one("fd_bottom_slab_checkered_b"),
    (221, None): _one("fd_block_end_l"), (222, None): _one("fd_block_end_r"),
    (226, None): _one("fd_cb8_everyend"), (229, None): _one("fd_cb5_everyend"),
    (253, None): _one("fd_cb6_polargeist"),
    (247, None): _one("fd_x_full", 8, 0), (248, None): _one("fd_x_full", -8, 0),
})

# Other ways to make a metatile, for the converter when the first one cannot be kept
FAMIDASH_ALT = {(m, None): _one("fd_big_saw_" + p)
                for m, p in zip((116, 117, 118, 119, 120, 121, 122, 123, 124),
                                ("tl", "tm", "tr", "ml", "m", "mr", "bl", "bm", "br"))}

# Objects with no picture: only a hitbox
INVISIBLE = {"fd_invis_small_block", "fd_invis_block"}
