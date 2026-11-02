# GD objects in the official levels, and what Famidash has

Every object ID the 14 official levels use (decoded from GD 2.2's `Resources/levels/1..14.txt`), its texture (`ObjectToolbox`'s ID-to-frame table, read from GeometryDash.exe), the levels using it, and Famidash's equivalent (`include/famidash_metatiles.c` names; sprite ids from `src/sp_draw.c` / `src/sprites/famidash_sprites.c`; and Famidash's per-level tile sets, tools/build_bg_tiles.py LEVEL_SETS: block set B, spike sets B and C). Matched by eye: textures are GD 2.2's, Famidash's art is NES-style.

Status: **HAVE** Famidash has it. **PART** an approximation or substitute. **MISSING** no object and no art. **ART** (1.6) no object yet, but your art exists (the .chr files). **ENGINE** not a drawn object (enter effects, ghost trail, triggers): an engine feature.

Famidash's per-level tile sets (block set B, spike sets B and C) are not kept: in the new version
every object exists in every level, so an entry "block set B" / "spike set C" means the art exists and
becomes an object of its own (Theory of Everything's bricks next to the grid blocks, its bush spikes next
to the pit spikes).

Levels: 1 SM, 2 BoT, 3 PG, 4 DO, 5 BaB, 6 CLG, 7 Jmp, 8 TM, 9 Cyc, 10 xS, 11 CF, 12 ToE, 13 EA, 14 CS

| GD ID | Version | Object | GD texture | Used in (level:count) | Famidash | Status |
|---|---|---|---|---|---|---|
| 1 | 1.0-1.4 | Default block | `square_01_001.png` | SM:168 BoT:271 PG:134 DO:50 BaB:199 CLG:168 Jmp:343 TM:155 Cyc:921 xS:141 ToE:2 EA:1 | BLOCK (16) | **HAVE** |
| 2 | 1.0-1.4 | Grid block, outlined variant | `square_02_001.png` | SM:356 PG:102 DO:70 BaB:238 CLG:160 Jmp:199 TM:249 Cyc:49 xS:171 | CHECKERBOARD_* (32-51) | **HAVE** |
| 3 | 1.0-1.4 | Grid block variant | `square_03_001.png` | SM:32 PG:46 DO:82 BaB:53 CLG:49 Jmp:4 TM:149 Cyc:26 xS:61 | CHECKERBOARD_* | **HAVE** |
| 4 | 1.0-1.4 | Grid block variant | `square_04_001.png` | SM:18 PG:2 DO:1 BaB:13 CLG:2 TM:12 Cyc:9 xS:47 | CHECKERBOARD_* | **HAVE** |
| 5 | 1.0-1.4 | Grid block, no outline (fill) | `square_05_001.png` | SM:661 BoT:351 PG:64 DO:51 BaB:348 CLG:1 Jmp:314 TM:528 Cyc:1199 xS:807 EA:50 | CHECKERBOARD_* | **HAVE** |
| 6 | 1.0-1.4 | Grid block variant | `square_06_001.png` | SM:30 PG:61 DO:84 BaB:22 CLG:60 Jmp:10 TM:98 Cyc:3 xS:28 | CHECKERBOARD_* | **HAVE** |
| 7 | 1.0-1.4 | Grid block variant | `square_07_001.png` | SM:69 BoT:51 PG:129 DO:229 BaB:74 CLG:157 Jmp:550 TM:549 Cyc:22 xS:40 | CHECKERBOARD_* | **HAVE** |
| 8 | 1.0-1.4 | Spike | `spike_01_001.png` | SM:166 BoT:130 PG:123 DO:191 BaB:197 CLG:510 Jmp:369 TM:369 Cyc:236 xS:245 CF:72 ToE:51 EA:87 CS:89 | SPIKE_UP/TOP/LEFT/RIGHT (17, 26-28) | **HAVE** |
| 9 | 1.0-1.4 | Ground spikes (pit) | `pit_01_001.png` | SM:423 BoT:370 PG:470 DO:396 BaB:534 CLG:231 Jmp:385 TM:731 Cyc:102 xS:139 ToE:47 | PITSPIKES (24), background spikes (12, 13) | **HAVE** |
| 10 | 1.0-1.4 | Gravity portal down (blue) | `portal_01_front_001.png` | BoT:1 PG:1 DO:1 BaB:2 CLG:1 Jmp:6 TM:5 Cyc:10 xS:12 CF:9 ToE:11 EA:4 CS:11 | sprite 8 | **HAVE** |
| 11 | 1.0-1.4 | Gravity portal up (yellow) | `portal_02_front_001.png` | DO:1 BaB:2 CLG:1 Jmp:6 TM:5 Cyc:12 xS:8 CF:8 ToE:10 EA:2 CS:12 | sprite 9 | **HAVE** |
| 12 | 1.0-1.4 | Cube portal | `portal_03_front_001.png` | SM:1 BoT:1 PG:1 DO:1 BaB:1 CLG:1 Jmp:2 TM:1 Cyc:1 xS:2 CF:1 ToE:4 EA:4 CS:4 | sprite 0 | **HAVE** |
| 13 | 1.0-1.4 | Ship portal | `portal_04_front_001.png` | SM:3 BoT:1 PG:1 DO:1 BaB:1 CLG:1 Jmp:2 TM:2 Cyc:1 xS:3 CF:2 ToE:3 EA:3 CS:6 | sprite 1 | **HAVE** |
| 15 | 1.0-1.4 | Pulse rod, long | `rod_01_001.png` | SM:6 PG:12 BaB:2 CLG:2 Jmp:3 TM:5 Cyc:11 xS:1 CF:5 ToE:8 | sprite 42 | **HAVE** |
| 16 | 1.0-1.4 | Pulse rod, medium | `rod_02_001.png` | SM:27 BoT:24 PG:37 DO:37 BaB:22 CLG:8 Jmp:62 TM:75 Cyc:53 xS:23 CF:34 ToE:25 EA:30 CS:5 | sprite 43 | **HAVE** |
| 17 | 1.0-1.4 | Pulse rod, short | `rod_03_001.png` | SM:69 BoT:41 PG:5 DO:6 BaB:29 CLG:3 Jmp:7 TM:35 Cyc:53 xS:19 CF:32 ToE:29 EA:49 CS:16 | sprite 44 | **HAVE** |
| 18 | 1.0-1.4 | Ground spike deco 1 | `d_spikes_01_001.png` | SM:19 BoT:19 PG:16 DO:27 BaB:41 CLG:20 Jmp:25 TM:101 Cyc:87 xS:38 | sprite 46 | **HAVE** |
| 19 | 1.0-1.4 | Ground spike deco 2 | `d_spikes_02_001.png` | SM:16 BoT:12 PG:20 DO:29 BaB:35 CLG:12 Jmp:29 TM:78 Cyc:59 xS:26 | sprite 47 | **HAVE** |
| 20 | 1.0-1.4 | Ground spike deco 3 | `d_spikes_03_001.png` | SM:27 BoT:26 PG:31 DO:32 BaB:29 CLG:14 Jmp:23 TM:37 Cyc:16 xS:71 CS:8 | sprite 48 | **HAVE** |
| 21 | 1.0-1.4 | Ground spike deco 4 | `d_spikes_04_001.png` | SM:16 BoT:17 PG:19 DO:11 BaB:13 CLG:4 Jmp:19 TM:3 Cyc:21 xS:7 | sprite 49 | **HAVE** |
| 22 | 1.0-1.4 | Enter effect: none | `edit_eeNoneBtn_001.png` | SM:1 BoT:1 PG:1 BaB:1 Cyc:1 CF:1 | - | **ENGINE** |
| 23 | 1.0-1.4 | Enter effect: from bottom | `edit_eeFBBtn_001.png` | SM:2 BoT:2 PG:2 DO:2 BaB:2 CLG:2 Jmp:5 TM:2 Cyc:10 xS:1 ToE:1 EA:5 CS:9 | - | **ENGINE** |
| 24 | 1.0-1.4 | Enter effect: from top | `edit_eeFTBtn_001.png` | CLG:1 Jmp:1 Cyc:9 EA:2 CS:8 | - | **ENGINE** |
| 25 | 1.0-1.4 | Enter effect: from left | `edit_eeFLBtn_001.png` | Cyc:3 ToE:1 EA:3 | - | **ENGINE** |
| 26 | 1.0-1.4 | Enter effect: from right | `edit_eeFRBtn_001.png` | BaB:1 Jmp:1 TM:1 Cyc:5 CF:1 EA:2 CS:1 | - | **ENGINE** |
| 27 | 1.0-1.4 | Enter effect: scale up | `edit_eeSUBtn_001.png` | SM:2 BoT:2 PG:1 DO:2 BaB:2 CLG:2 Jmp:4 TM:1 Cyc:2 xS:1 CF:3 ToE:1 EA:1 CS:1 | - | **ENGINE** |
| 28 | 1.0-1.4 | Enter effect: scale down | `edit_eeSDBtn_001.png` | xS:3 ToE:1 EA:1 CS:2 | - | **ENGINE** |
| 29 | 1.0-1.4 | Colour trigger: background | `edit_eTintCol01Btn_001.png` | SM:7 BoT:5 PG:8 DO:23 BaB:6 CLG:5 Jmp:25 TM:12 Cyc:24 xS:152 CF:105 ToE:96 EA:75 CS:95 | sprites 128-175 (BG triggers) | **HAVE** |
| 30 | 1.0-1.4 | Colour trigger: ground | `edit_eTintCol01Btn_001.png` | SM:7 BoT:5 PG:8 DO:15 BaB:6 CLG:3 Jmp:9 TM:12 Cyc:7 xS:24 CF:11 ToE:14 EA:7 CS:1 | sprites 192-239 (G triggers) | **HAVE** |
| 32 | 1.0-1.4 | Ghost trail on | `edit_eGhostEBtn_001.png` | PG:1 BaB:1 TM:1 Cyc:3 xS:3 CF:1 EA:3 | - | **ENGINE** |
| 33 | 1.0-1.4 | Ghost trail off | `edit_eGhostDBtn_001.png` | PG:1 BaB:1 Jmp:1 TM:1 Cyc:2 xS:1 EA:2 | - | **ENGINE** |
| 35 | 1.0-1.4 | Yellow pad | `bump_01_001.png` | BoT:19 PG:7 DO:5 BaB:6 CLG:12 Jmp:2 TM:21 Cyc:16 xS:14 CF:19 ToE:7 EA:17 CS:9 | sprite 10 / 12 | **HAVE** |
| 36 | 1.0-1.4 | Yellow orb | `ring_01_001.png` | PG:29 DO:2 BaB:7 CLG:26 Jmp:6 TM:17 Cyc:25 xS:21 CF:35 ToE:20 EA:8 CS:40 | sprite 11 | **HAVE** |
| 39 | 1.0-1.4 | Small spike | `spike_02_001.png` | SM:7 BoT:19 PG:61 DO:17 BaB:17 CLG:110 Jmp:318 TM:100 Cyc:752 xS:98 EA:3 CS:45 | SPIKE_HALF (18), HALF_SPIKE_* (29-31) | **HAVE** |
| 40 | 1.0-1.4 | Slab | `plank_01_001.png` | SM:111 BoT:133 PG:64 DO:30 BaB:174 CLG:73 Jmp:48 TM:97 Cyc:81 xS:23 | PLATFORM (25), HALF_SLAB_* (3, 55) | **HAVE** |
| 41 | 1.0-1.4 | Hanging chain | `chain_01_001.png` | SM:20 BoT:31 PG:56 DO:41 BaB:63 CLG:58 Jmp:29 TM:136 Cyc:76 xS:208 CF:148 ToE:59 EA:42 CS:20 | sprite 45 / 61 | **HAVE** |
| 45 | 1.0-1.4 | Mirror portal (orange) | `portal_05_front_001.png` | TM:3 Cyc:2 CF:1 ToE:2 EA:2 | sprite 126 (CGB) | **HAVE** |
| 46 | 1.0-1.4 | Mirror portal (blue) | `portal_06_front_001.png` | TM:3 Cyc:1 CF:1 ToE:2 EA:2 | sprite 121 (CGB) | **HAVE** |
| 47 | 1.0-1.4 | Ball portal | `portal_07_front_001.png` | Cyc:2 xS:2 CF:1 ToE:2 EA:2 CS:1 | sprite 2 | **HAVE** |
| 48 | 1.0-1.4 | Cloud 1 | `d_cloud_01_001.png` | xS:138 CF:117 ToE:24 CS:16 | CLOUD_* (128-135) | **PART** |
| 49 | 1.0-1.4 | Cloud 2 | `d_cloud_02_001.png` | xS:115 CF:75 ToE:21 CS:12 | CLOUD_* (128-135) | **PART** |
| 50 | 1.0-1.4 | Pulsing circle | `d_ball_01_001.png` | xS:91 CF:26 ToE:50 EA:114 CS:146 | sprite 73 (pulsing ball) | **HAVE** |
| 51 | 1.0-1.4 | Pulsing ring | `d_ball_02_001.png` | xS:9 EA:23 CS:5 | - | **MISSING** |
| 52 | 1.0-1.4 | Pulsing heart | `d_ball_03_001.png` | EA:4 | - | **MISSING** |
| 53 | 1.0-1.4 | Pulsing diamond | `d_ball_04_001.png` | xS:76 ToE:13 EA:5 CS:11 | sprite 50 | **HAVE** |
| 54 | 1.0-1.4 | Pulsing star | `d_ball_05_001.png` | SM:8 xS:87 CF:18 ToE:40 EA:74 CS:56 | sprite 54? | **PART** |
| 56 | 1.0-1.4 | Enter effect: fade left | `edit_eeFALBtn_001.png` | EA:1 | - | **ENGINE** |
| 57 | 1.0-1.4 | Enter effect: fade right | `edit_eeFARBtn_001.png` | EA:3 CS:1 | - | **ENGINE** |
| 58 | 1.0-1.4 | Enter effect: from right half | `edit_eeFRHBtn_001.png` | xS:4 CF:2 ToE:3 EA:4 CS:5 | - | **ENGINE** |
| 59 | 1.0-1.4 | Enter effect: inverted | `edit_eeFRHInvBtn_001.png` | xS:1 CF:1 EA:3 CS:4 | - | **ENGINE** |
| 60 | 1.0-1.4 | Pulsing music note | `d_ball_06_001.png` | CF:11 | sprite 74 | **HAVE** |
| 61 | 1.0-1.4 | Wavy ground spikes | `pit_04_001.png` | xS:1109 CF:841 ToE:718 EA:830 CS:771 | BOTTOM/TOP_WAVYPIT (92, 93) | **HAVE** |
| 62 | 1.0-1.4 | Wavy platform block | `square_b_01_001.png` | SM:1 BoT:9 PG:9 BaB:1 xS:141 CF:33 ToE:21 | WAVY_PLATFORM_* (56-58, 84-91) | **HAVE** |
| 65 | 1.0-1.4 | Wavy platform variant | `square_b_04_001.png` | SM:1 xS:66 CF:3 ToE:10 | WAVY_PLATFORM_* | **HAVE** |
| 66 | 1.0-1.4 | Wavy platform variant | `square_b_05_001.png` | xS:66 CF:6 ToE:10 | WAVY_PLATFORM_* | **HAVE** |
| 67 | 1.0-1.4 | Blue pad | `gravbump_01_001.png` | BoT:1 PG:1 xS:68 CF:19 ToE:11 EA:17 CS:16 | sprite 13 / 14 | **HAVE** |
| 68 | 1.0-1.4 | Wavy platform variant | `square_b_06_001.png` | DO:4 CLG:3 CF:17 ToE:13 EA:2 | WAVY_PLATFORM_* | **HAVE** |
| 69 | 1.0-1.4 | Outline square (black block set) | `blockOutline_01_001.png` | xS:110 CF:123 ToE:16 EA:25 CS:1 | BLACK_* (96-115) | **HAVE** |
| 70 | 1.0-1.4 | Outline line | `lightsquare_01_02_001.png` | xS:25 CF:36 ToE:58 EA:152 CS:20 | BLACK_* | **HAVE** |
| 71 | 1.0-1.4 | Outline corner | `blockOutline_03_001.png` | xS:14 CF:27 ToE:26 EA:80 | BLACK_* | **HAVE** |
| 72 | 1.0-1.4 | Outline dot | `blockOutline_06_001.png` | xS:4 CF:7 EA:31 CS:1 | BLACK_* dots | **HAVE** |
| 73 | 1.0-1.4 | Dark pyramid fill (X blocks) | `square_c_05_001.png` | Jmp:1 Cyc:6 xS:86 CF:19 ToE:125 EA:12 CS:910 | X_* (64-83) | **HAVE** |
| 74 | 1.0-1.4 | Outline U piece | `blockOutline_04_001.png` | xS:10 CF:49 ToE:14 EA:17 | BLACK_* | **HAVE** |
| 75 | 1.0-1.4 | Outline side line | `blockOutline_05_001.png` | xS:15 CF:76 ToE:39 EA:2 CS:4 | BLACK_* | **HAVE** |
| 76 | 1.0-1.4 | Light line (Clutterfunk) | `lightsquare_04_02_001.png` | CF:23 | Clutterfunk patches | **PART** |
| 77 | 1.0-1.4 | Light line (Clutterfunk) | `lightsquare_04_02_001.png` | CF:44 | Clutterfunk patches | **PART** |
| 78 | 1.0-1.4 | Light line (Clutterfunk) | `lightsquare_04_02_001.png` | CF:19 | Clutterfunk patches | **PART** |
| 80 | 1.0-1.4 | Dark square fill (Clutterfunk) | `square_d_05_001.png` | CF:29 | 3D_* (212-214) / BLACK_NONE | **PART** |
| 81 | 1.0-1.4 | Light line (Clutterfunk) | `lightsquare_04_02_001.png` | CF:47 | Clutterfunk patches | **PART** |
| 82 | 1.0-1.4 | Light side line (Clutterfunk) | `lightsquare_04_sideLine_001.png` | CF:55 | Clutterfunk patches | **PART** |
| 83 | 1.0-1.4 | Grid block with outline | `square_08_001.png` | xS:5 | CHECKERBOARD_* | **HAVE** |
| 84 | 1.0-1.4 | Blue orb | `gravring_01_001.png` | xS:6 CF:16 ToE:17 EA:12 CS:62 | sprite 5 | **HAVE** |
| 85 | 1.0-1.4 | Cogwheel deco, half | `d_cogwheel_01_001.png` | CF:77 ToE:15 EA:8 CS:34 | - | **MISSING** |
| 86 | 1.0-1.4 | Cogwheel deco, big | `d_cogwheel_02_001.png` | CF:91 ToE:78 EA:10 CS:54 | - | **MISSING** |
| 87 | 1.0-1.4 | Cogwheel deco, medium | `d_cogwheel_03_001.png` | CF:68 ToE:34 EA:20 CS:5 | - | **MISSING** |
| 88 | 1.0-1.4 | Saw, big | `sawblade_01_001.png` | CF:44 ToE:58 EA:8 CS:30 | BIG_SAW_* (116-124) | **HAVE** |
| 89 | 1.0-1.4 | Saw, medium | `sawblade_02_001.png` | CF:63 ToE:23 EA:15 CS:28 | MED_SAW_* (8-11) | **HAVE** |
| 90 | 1.0-1.4 | Outline square (set 2) | `blockOutline_01_001.png` | CF:2 ToE:2 | BLACK_* | **HAVE** |
| 91 | 1.0-1.4 | Outline line (set 2) | `lightsquare_01_02_001.png` | CF:519 ToE:176 CS:106 | BLACK_* | **HAVE** |
| 92 | 1.0-1.4 | Outline corner (set 2) | `blockOutline_03_001.png` | CF:95 ToE:42 CS:44 | BLACK_* | **HAVE** |
| 93 | 1.0-1.4 | Outline dot (set 2) | `blockOutline_06_001.png` | CF:91 ToE:22 CS:2 | BLACK_* dots | **HAVE** |
| 94 | 1.0-1.4 | Plain colour square (fill under outlines) | `lightsquare_01_05_color_001.png` | xS:26 CF:1330 ToE:375 EA:294 CS:178 | BLACK_NONE (111) | **HAVE** |
| 95 | 1.0-1.4 | Outline U (set 2) | `blockOutline_04_001.png` | CF:8 ToE:2 CS:14 | BLACK_* | **HAVE** |
| 96 | 1.0-1.4 | Outline side (set 2) | `blockOutline_05_001.png` | CF:3 ToE:7 CS:66 | BLACK_* | **HAVE** |
| 97 | 1.0-1.4 | Cogwheel deco, small | `d_cogwheel_04_001.png` | CF:1 ToE:23 EA:4 | - | **MISSING** |
| 98 | 1.0-1.4 | Saw, small | `sawblade_03_001.png` | CF:21 ToE:23 EA:20 CS:4 | SMALL_SAW (125) | **HAVE** |
| 99 | 1.0-1.4 | Mini portal | `portal_08_front_001.png` | CF:4 ToE:5 EA:2 CS:2 | sprite 24 | **HAVE** |
| 101 | 1.0-1.4 | Normal size portal | `portal_09_front_001.png` | CF:5 ToE:6 EA:2 CS:3 | sprite 25 | **HAVE** |
| 103 | 1.0-1.4 | Medium spike | `spike_03_001.png` | SM:14 Jmp:2 xS:1 CF:205 ToE:222 EA:1 CS:72 | SMALL_SPIKE_* (173-175) | **HAVE** |
| 104 | 1.0-1.4 | ? (no texture: a trigger or invisible object) | `?` | CF:4 ToE:7 | - | **ENGINE** |
| 105 | 1.0-1.4 | Colour trigger: object / line | `edit_eTintCol01Btn_001.png` | ToE:21 EA:19 CS:8 | sprites 176-191 (outline triggers) | **HAVE** |
| 106 | 1.5 | Chain with base | `d_02_chain_01_001.png` | ToE:21 EA:29 CS:22 | sprite 45 (approx) | **PART** |
| 107 | 1.5 | Chain with base, short | `d_02_chain_02_001.png` | ToE:1 EA:2 CS:1 | sprite 45 (approx) | **PART** |
| 110 | 1.5 | Chain, small | `d_chain_02_001.png` | PG:5 ToE:2 EA:1 CS:18 | sprite 45 (approx) | **PART** |
| 111 | 1.5 | UFO portal | `portal_10_front_001.png` | ToE:2 EA:2 CS:2 | sprite 3 | **HAVE** |
| 113 | 1.5 | Stepped ground hill 1 (ToE) | `d_brick_01_001.png` | ToE:32 EA:4 CS:2 | - | **MISSING** |
| 114 | 1.5 | Stepped ground hill 2 (ToE) | `d_brick_02_001.png` | ToE:19 EA:6 CS:28 | - | **MISSING** |
| 115 | 1.5 | Stepped ground hill 3 (ToE) | `d_brick_03_001.png` | ToE:23 EA:8 | - | **MISSING** |
| 116 | 1.5 | Brick block (ToE set) | `square_f_01_001.png` | ToE:16 CS:9 | block set B bricks (ToE, Clutterfunk, Electroman, Clubstep) | **HAVE** |
| 117 | 1.5 | Brick block, edge (ToE set) | `square_f_02_001.png` | ToE:272 EA:94 CS:445 | block set B | **HAVE** |
| 118 | 1.5 | Brick block, corner (ToE set) | `square_f_03_001.png` | ToE:53 CS:121 | block set B | **HAVE** |
| 119 | 1.5 | Brick block outline dot (ToE set) | `blockOutline_06_001.png` | ToE:9 CS:57 | block set B | **HAVE** |
| 120 | 1.5 | Brick fill (ToE set) | `square_f_05_001.png` | ToE:394 EA:518 CS:458 | block set B | **HAVE** |
| 121 | 1.5 | Brick block, sides (ToE set) | `square_f_06_001.png` | ToE:44 CS:90 | block set B | **HAVE** |
| 122 | 1.5 | Brick block, side (ToE set) | `square_f_07_001.png` | ToE:156 EA:41 CS:174 | block set B | **HAVE** |
| 123 | 1.5 | Thorn deco 1 (ToE) | `d_thorn_01_001.png` | ToE:33 | - | **MISSING** |
| 124 | 1.5 | Thorn deco 2 | `d_thorn_02_001.png` | ToE:16 | - | **MISSING** |
| 125 | 1.5 | Thorn deco 3 | `d_thorn_03_001.png` | ToE:8 | - | **MISSING** |
| 126 | 1.5 | Thorn deco 4 | `d_thorn_04_001.png` | ToE:18 | - | **MISSING** |
| 127 | 1.5 | Thorn deco 5 | `d_thorn_05_001.png` | ToE:3 | - | **MISSING** |
| 128 | 1.5 | Thorn deco 6 | `d_thorn_06_001.png` | ToE:10 | - | **MISSING** |
| 129 | 1.5 | Cloud 3 | `d_cloud_03_001.png` | ToE:20 EA:14 CS:10 | CLOUD_* (approx) | **PART** |
| 130 | 1.5 | Cloud 4 | `d_cloud_04_001.png` | ToE:11 EA:11 CS:8 | CLOUD_* (approx) | **PART** |
| 131 | 1.5 | Cloud 5 (small) | `d_cloud_05_001.png` | EA:11 CS:5 | CLOUD_* (approx) | **PART** |
| 132 | 1.5 | Arrow deco | `d_arrow_01_001.png` | ToE:7 EA:14 CS:14 | sprite 54 / 51 | **HAVE** |
| 133 | 1.5 | ! deco | `d_exmark_01_001.png` | ToE:9 | sprite 53 | **HAVE** |
| 135 | 1.5 | Bush ground spikes (ToE) | `pit_b_01_001.png` | ToE:121 | spike set C pit spikes (ToE, Demon Park, The Lightning Road) | **HAVE** |
| 136 | 1.5 | ? deco | `d_qmark_01_001.png` | ToE:1 | sprite 52 | **HAVE** |
| 137 | 1.5 | Gear ring, half | `d_wheel_01_001.png` | ToE:66 EA:66 CS:50 | - | **MISSING** |
| 138 | 1.5 | Gear ring, big | `d_wheel_02_001.png` | ToE:136 EA:48 CS:43 | - | **MISSING** |
| 139 | 1.5 | Gear ring, small | `d_wheel_03_001.png` | ToE:53 EA:3 CS:1 | - | **MISSING** |
| 140 | 1.5 | Pink pad | `bump_03_001.png` | CLG:1 ToE:10 EA:3 CS:7 | sprite 37 / 4 | **HAVE** |
| 141 | 1.5 | Pink orb | `ring_03_001.png` | TM:2 ToE:10 EA:4 CS:22 | sprite 6 | **HAVE** |
| 142 | 1.6 | Secret coin | `secretCoin_01_001.png` | SM:3 BoT:3 PG:3 DO:3 BaB:3 CLG:3 Jmp:3 TM:3 Cyc:3 xS:3 CF:3 ToE:3 EA:3 CS:3 | sprites 7, 26, 27 | **HAVE** |
| 143 | 1.6 | Breakable brick | `brick_02_001.png` | EA:32 | - | **ART** |
| 144 | 1.6 | Fading spike | `invis_spike_01_001.png` | CS:13 | - | **ART** |
| 145 | 1.6 | Fading small spike | `invis_spike_03_001.png` | CS:37 | - | **ART** |
| 146 | 1.6 | Fading block | `invis_square_01_001.png` | CS:21 | - | **ART** |
| 150 | 1.6 | Pulsing X | `d_cross_01_001.png` | EA:1 CS:3 | sprite 55 | **HAVE** |
| 151 | 1.6 | Spiky art deco 1 | `d_spikeart_01_001.png` | CS:58 | - | **MISSING** |
| 152 | 1.6 | Spiky art deco 2 | `d_spikeart_02_001.png` | CS:144 | - | **MISSING** |
| 153 | 1.6 | Spiky art deco 3 | `d_spikeart_03_001.png` | CS:95 | - | **MISSING** |
| 154 | 1.6 | Spiked wheel deco 1 | `d_spikewheel_01_001.png` | EA:37 CS:104 | - | **MISSING** |
| 155 | 1.6 | Spiked wheel deco 2 | `d_spikewheel_02_001.png` | EA:1 | - | **MISSING** |
| 157 | 1.6 | Wave hill 1 | `d_wave_01_001.png` | EA:750 CS:574 | - | **ART** |
| 158 | 1.6 | Wave hill 2 | `d_wave_02_001.png` | EA:4 CS:2 | - | **ART** |
| 159 | 1.6 | Wave hill 3 | `d_wave_03_001.png` | EA:3 CS:1 | - | **ART** |
| 161 | 1.6 | 1.6 outline line (rails) | `lightsquare_01_02_001.png` | EA:351 CS:249 | - | **ART** |
| 162 | 1.6 | 1.6 outline corner | `blockOutline_03_001.png` | EA:117 CS:174 | - | **ART** |
| 163 | 1.6 | 1.6 outline dot | `blockOutline_06_001.png` | EA:36 CS:106 | - | **ART** |
| 164 | 1.6 | 1.6 striped fill | `square_g_05_001.png` | EA:37 CS:55 | - | **ART** |
| 165 | 1.6 | 1.6 outline U | `blockOutline_04_001.png` | EA:7 CS:15 | - | **ART** |
| 166 | 1.6 | 1.6 outline side | `blockOutline_05_001.png` | EA:23 CS:11 | - | **ART** |
| 167 | 1.6 | 1.6 outline dot 2 | `blockOutline_06_001.png` | EA:2 CS:7 | - | **ART** |
| 169 | 1.6 | 1.6 outline square | `blockOutline_01_001.png` | CS:4 | - | **ART** |
| 170 | 1.6 | 1.6 block | `square_h_01_001.png` | EA:276 CS:4 | - | **ART** |
| 171 | 1.6 | 1.6 block, edge | `square_h_02_001.png` | EA:51 CS:2 | - | **ART** |
| 173 | 1.6 | 1.6 block, corner | `square_h_04_001.png` | EA:137 | - | **ART** |
| 174 | 1.6 | 1.6 block, corner 2 | `square_h_05_001.png` | EA:15 | - | **ART** |
| 175 | 1.6 | 1.6 block, fill | `square_h_06_001.png` | EA:20 | - | **ART** |
| 176 | 1.6 | 1.6 block, small | `square_h_07_001.png` | EA:25 | - | **ART** |
| 177 | 1.6 | Ice spike 1 | `iceSpike_01_001.png` | EA:82 CS:6 | - | **MISSING** |
| 178 | 1.6 | Ice spike 2 | `iceSpike_02_001.png` | EA:51 CS:338 | - | **MISSING** |
| 179 | 1.6 | Ice spike 3 | `iceSpike_03_001.png` | EA:10 CS:19 | - | **MISSING** |
| 180 | 1.6 | Cart wheel deco 1 | `d_cartwheel_01_001.png` | EA:37 CS:44 | - | **MISSING** |
| 181 | 1.6 | Cart wheel deco 2 | `d_cartwheel_02_001.png` | EA:46 CS:6 | - | **MISSING** |
| 182 | 1.6 | Cart wheel deco 3 | `d_cartwheel_03_001.png` | EA:6 CS:4 | - | **MISSING** |
| 183 | 1.6 | Black saw 1 | `blade_b_01_001.png` | CS:12 | - | **MISSING** |
| 184 | 1.6 | Black saw 2 | `blade_b_02_001.png` | CS:3 | - | **MISSING** |
| 185 | 1.6 | Black saw 3 | `blade_b_03_001.png` | CS:4 | - | **MISSING** |
| 186 | 1.6 | 1.6 saw, big | `blade_01_001.png` | EA:33 CS:54 | - | **ART** |
| 187 | 1.6 | 1.6 saw, medium | `blade_02_001.png` | EA:53 CS:4 | - | **ART** |
| 188 | 1.6 | 1.6 saw, star | `blade_03_001.png` | EA:6 | - | **ART** |
| 190 | 1.6 | Chain art deco | `d_art_02_001.png` | EA:72 CS:13 | - | **MISSING** |
| 191 | 1.6 | Black fake spike | `fakeSpike_01_001.png` | EA:8 CS:138 | FAKE_SPIKE_UP/TOP (197, 198) | **HAVE** |
| 192 | 1.6 | 1.6 block variant | `square_h_08_001.png` | EA:54 | - | **ART** |
| 193 | 1.6 | 1.6 dark fill | `square_g_11_001.png` | EA:130 CS:510 | - | **ART** |
| 194 | 1.6 | 1.6 block variant | `square_h_09_001.png` | EA:25 | - | **ART** |
| 197 | 1.6 | 1.6 block variant | `square_h_10_001.png` | EA:2 | - | **ART** |
| 198 | 1.6 | Black fake spike, small | `fakeSpike_02_001.png` | CS:4 | FAKE_SPIKE_HALF (203) | **HAVE** |
| 199 | 1.6 | Black fake spike, medium | `fakeSpike_03_001.png` | CS:139 | FAKE_SMALL_SPIKE_* (205-207) | **HAVE** |

Totals over the 171 IDs: ART 28, ENGINE 14, HAVE 81, MISSING 33, PART 15
