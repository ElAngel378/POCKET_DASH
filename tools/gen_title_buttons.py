"""Title menu big buttons (Famidash icon / play / wrench) -> src/graphics/title_buttons.c

Three big buttons side by side need more than the 10 sprites per scanline the hardware draws, so
each button is split:

CGB: a BG "core" (8x8 cells fully inside the button: no sky pixel shows there, so a static tile
     with its own palette is fine, also filling the yellow / cyan holes) and a sprite "rim" (the
     rest, transparent where the moving parallax sky shows through). The menu draws the top of
     the screen with 8x8 sprites (a 16 tall sprite covers rows it has no pixels in) and switches to
     8x16 for the ground buttons mid-frame.
DMG: the sky scrolls with SCX, so the side buttons are BG tiles blended with the sky offline for
     every one of the 64 scroll positions (the sky period), copied in as the sky moves. PLAY stays
     the old 8x16 sprites (4 per line).

Side buttons (load_sides): Geometry Dash style 24x24 crosses with 8 px arms on the tile grid, so
the cross is all BG core and only the 4 cyan corner boxes are sprites; the yellow icon face and
tool come from Famidash's art (tools/title_buttons/fd_side_buttons.txt). (Famidash's own 26 px
shape, 10 px arms, needs 2 sprites per corner: 10-11 on a line.) The big buttons do not move when
selected (a 2 px bump breaks PLAY's core alignment: 11 on a line). On DMG the corner boxes show
the sky, like PLAY's.

  python tools/gen_title_buttons.py --solve   # search the CGB sprite/core split (seconds), writes
                                              # tools/title_buttons/layout.json
  python tools/gen_title_buttons.py           # layout.json -> C (checks the scanline budget)
"""
import json, os, re, sys, itertools
from functools import lru_cache

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TB = os.path.join(ROOT, 'tools', 'title_buttons')
OUT_C = os.path.join(ROOT, 'src', 'graphics', 'title_buttons.c')
OUT_H = os.path.join(ROOT, 'include', 'title_buttons.h')

# layout (screen pixels of the art grids; side grids have a 1 px empty border)
PLAY_X, PLAY_Y = 64, 52
ICON_X, WRENCH_X = 24, 112             # side buttons: 24x24 crosses on the 8 px tile grid
SIDE_Y = 56
CURSOR_Y = 43                          # cursor (8x8, pixels in rows 0..5) above the selected button
SPR_H = 8
OBJ_TILE_BASE = 32                     # CGB rim tiles (OBJ 0..31: play button, ground buttons, cursor)
CORE_TILE_BASE = 48                    # CGB core tiles, VRAM bank 1 (0..47: parallax sky)
CORE_BG_PALS = (2, 5, 6, 7)
DMG_BUF_TILE = 168                     # DMG side tiles: 2 buffers x 2 sides x 20 (shared area, OBJ unused)
DMG_SKY_TILE = 53
MAX_GROUP_OAM = 35                     # 40 - music/gear (4) - cursor (1)

# OBJ palettes a rim sprite may use (colours 1..3 of the palette); CGB OBJ palettes 3 and 4 are the
# ground buttons' and the cursor's, so these become 0, 1, 2, 5, 6, 7
OBJ_PALS = ('123', '13c', '23c', '13y', '23y', '3cy')
OBJ_PAL_NUMS = (0, 1, 2, 5, 6, 7)
def pal_fits(cols): return any(cols <= set(p) for p in OBJ_PALS)

COLORS = {'1': (138, 245, 30), '2': (30, 140, 20), '3': (0, 0, 0), 'c': (0, 240, 255), 'y': (255, 240, 0),
          'o': (220, 160, 0)}

# art
def fd_art(name):
    txt = open(os.path.join(TB, 'fd_side_buttons.txt')).read()
    block = txt.split('[%s]' % name)[1].strip().split('[')[0]
    return [l for l in block.splitlines() if l and not l.startswith('#')]

def cross24(inner):
    """24x24 cross with 8 px arms (outline included: every arm / centre cell is fully inside the
    button, so it is a static BG tile), lime top half / dark green bottom half, a cyan box with a
    black outline in each 8x8 corner cell (sprites), `inner` (rows of art, '.' = see through)
    centred on top"""
    a = {}
    def inside(x, y): return 0 <= x < 24 and 0 <= y < 24 and (8 <= x < 16 or 8 <= y < 16)
    for y in range(24):
        for x in range(24):
            if not inside(x, y): continue
            edge = not all(inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            a[(x, y)] = '3' if edge else ('1' if y < 12 else '2')
    for cx, cy in ((0, 0), (16, 0), (0, 16), (16, 16)):      # boxes hug the cross
        for y in range(8):
            for x in range(8):
                bx = x - 1 if cx == 0 else x
                by = y - 1 if cy == 0 else y
                if 0 <= bx < 7 and 0 <= by < 7:
                    a[(cx + x, cy + y)] = '3' if bx in (0, 6) or by in (0, 6) else 'c'
    h, w = len(inner), len(inner[0])
    ox, oy = (24 - w) // 2, (24 - h) // 2
    for y, r in enumerate(inner):
        for x, ch in enumerate(r):
            if ch != '.': a[(ox + x, oy + y)] = ch
    return [''.join(a.get((x, y), '.') for x in range(24)) for y in range(24)]

def load_sides():
    """Geometry Dash style side buttons (yellow icon face / tool on a cross with cyan corners), the
    face and tool taken from Famidash's art (tools/title_buttons/fd_side_buttons.txt)"""
    icon = fd_art('icon')
    # the face: inside the black square x7..20, y7..20; its pale pixels yellow, the rest black
    face = [''.join('y' if ch == '1' else '3' if ch in '23' else '.' for ch in r[7:21]) for r in icon[7:21]]
    # the tool: the pale / green pixels not connected to the button body (flood fill from the body)
    w = fd_art('wrench')
    H, W = len(w), len(w[0])
    seen = set(); st = [(14, 2), (3, 10), (25, 10), (3, 14), (25, 14), (14, 25)]
    while st:
        x, y = st.pop()
        if (x, y) in seen or not (0 <= x < W and 0 <= y < H) or w[y][x] not in '12': continue
        seen.add((x, y)); st += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    tool = {(x, y) for y, r in enumerate(w) for x, ch in enumerate(r)
            if ch in '12' and (x, y) not in seen and 6 <= x <= 23 and 6 <= y <= 21}
    out = {p: 'y' for p in tool}                 # yellow, with a fresh black outline around it
    for (x, y) in tool:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (x + dx, y + dy) not in tool: out[(x + dx, y + dy)] = '3'
    xs = [p[0] for p in out]; ys = [p[1] for p in out]
    tool = [''.join(out.get((x, y), '.') for x in range(min(xs), max(xs) + 1)) for y in range(min(ys), max(ys) + 1)]
    return cross24(face), cross24(tool)

def flood_out(g):
    H, W = len(g), len(g[0]); out = set()
    st = [(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)]
    while st:
        x, y = st.pop()
        if (x, y) in out or not (0 <= x < W and 0 <= y < H) or g[y][x] != '.': continue
        out.add((x, y)); st += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return out

def load_play():
    s = open(os.path.join(ROOT, 'src', 'graphics', 'playbutton.c')).read()
    b = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', s)][16:16 + 256]
    def p(c, x, y):
        d = b[(c * 4 + y // 8) * 16:][:16]; yy = y % 8
        return '.123'[((d[2 * yy] >> (7 - x)) & 1) | (((d[2 * yy + 1] >> (7 - x)) & 1) << 1)]
    g = [''.join(p(x // 8, x % 8, y) for x in range(32)) for y in range(32)]
    out = flood_out(g)
    # holes: the triangle (yellow on CGB) and the 4 corner boxes (cyan on CGB)
    return [''.join(('y' if 11 <= x <= 21 and 8 <= y <= 23 else 'c') if ch == '.' and (x, y) not in out else ch
                    for x, ch in enumerate(r)) for y, r in enumerate(g)]

ICON, WRENCH = load_sides()
PLAY = load_play()
GRIDS = {'play': PLAY, 'icon': ICON, 'wrench': WRENCH}
GX = {'play': PLAY_X, 'icon': ICON_X, 'wrench': WRENCH_X}
GY = {'play': PLAY_Y, 'icon': SIDE_Y, 'wrench': SIDE_Y}

def grid_px(name):
    g, x, y = GRIDS[name], GX[name], GY[name]
    return {(x + c, y + r): ch for r, row in enumerate(g) for c, ch in enumerate(row) if ch != '.'}

def core_cells(px):
    """[(cx, cy, colours drawn by the BG)] for the cells fully inside the button. A BG palette has
    4 colours: in a cell with 5, the pixels of the rarest one are left to the sprites."""
    xs = [p[0] for p in px]; ys = [p[1] for p in px]; out = []
    for cy in range(min(ys) // 8, max(ys) // 8 + 1):
        for cx in range(min(xs) // 8, max(xs) // 8 + 1):
            pts = [(cx * 8 + i, cy * 8 + j) for j in range(8) for i in range(8)]
            if not all(p in px for p in pts): continue
            n = {}
            for p in pts: n[px[p]] = n.get(px[p], 0) + 1
            cols = sorted(n, key=lambda c: -n[c])[:4]
            out.append((cx, cy, ''.join(sorted(cols))))
    return out

def rim_px(px):
    cov = {(cx * 8 + i, cy * 8 + j) for cx, cy, cols in core_cells(px) for j in range(8) for i in range(8)
           if px[(cx * 8 + i, cy * 8 + j)] in cols}
    return {p: c for p, c in px.items() if p not in cov}

# sprite solver
def strip_sprites(pix, W, maxcol=3, H=SPR_H):
    """Non-overlapping H tall sprites down one 8 px strip, <= maxcol colours each:
    [(y0, last owned row)] minimizing the sprite count, then the busy lines covered."""
    if not pix: return []
    rows = sorted({p[1] for p in pix}); bycol = {}
    for (x, y), c in pix.items(): bycol.setdefault(y, set()).add(c)
    @lru_cache(None)
    def best(r):
        nxt = [y for y in rows if y >= r]
        if not nxt: return (0, 0, ())
        f = nxt[0]; res = None
        for a in range(max(r, f - (H - 1)), f + 1):
            cols = set()
            for y in range(a, a + H): cols |= bycol.get(y, set())
            if not pal_fits(cols): continue
            sub = best(a + H)
            if sub[2] is None: continue
            cand = (sub[0] + 1, sub[1] + sum(W(l) for l in range(a, a + H)), ((a, a + H - 1),) + sub[2])
            if res is None or cand[:2] < res[:2]: res = cand
        if res is None:      # colour clash: a sprite owning fewer rows, overlapping the next one
            for a in range(max(r, f - (H - 1)), f + 1):
                cols = set(); end = a - 1
                for y in range(a, a + H):
                    if not pal_fits(cols | bycol.get(y, set())): break
                    cols |= bycol.get(y, set()); end = y
                if end < f: continue
                sub = best(end + 1)
                if sub[2] is None: continue
                cand = (sub[0] + 1, sub[1] + sum(W(l) for l in range(a, a + H)), ((a, end),) + sub[2])
                if res is None or cand[:2] < res[:2]: res = cand
        return res if res is not None else (99, 999, None)
    r = best(rows[0] - (H - 1))
    return None if r[0] >= 99 else list(r[2])

def assign(px, starts):
    strips = [{} for _ in starts]
    for p, c in px.items():
        for i, s in enumerate(starts):
            if s <= p[0] < s + 8: strips[i][p] = c; break
        else: return None
    return strips

def button_candidates(px, K):
    """Sprite layouts for one button: [(line profile, [(x, y, {pixel: colour})])], best K profiles."""
    xs = sorted({p[0] for p in px}); busy = {p[1] for p in px}
    W = lambda l: 1 if l in busy else 0
    memo = {}; sols = {}
    def dfs(starts):
        rest = [x for x in xs if not any(s <= x < s + 8 for s in starts)]
        if not rest:
            strips = assign(px, starts); spr = []; cnt = {}
            for st, sp in zip(starts, strips):
                k = tuple(sorted(sp.items()))
                if k not in memo: memo[k] = strip_sprites(sp, W)
                if memo[k] is None: return
                for a, e in memo[k]:
                    own = {p: c for p, c in sp.items() if a <= p[1] <= e}
                    spr.append((st, a, own))
                    for l in range(a, a + SPR_H): cnt[l] = cnt.get(l, 0) + 1
            key = (max(cnt.values()), len(spr), sum(cnt.values()))
            prof = tuple(sorted(cnt.items()))
            if prof not in sols or key < sols[prof][0]: sols[prof] = (key, spr)
            return
        if len(starts) >= 5: return
        for s in range(rest[0] - 7, rest[0] + 1): dfs(starts + [s])
    dfs([])
    return [(dict(p), spr) for p, (k, spr) in sorted(sols.items(), key=lambda kv: kv[1][0])[:K]]

def add(*ps):
    c = {}
    for p in ps:
        for l, v in p.items(): c[l] = c.get(l, 0) + v
    return c

CUR = {l: 1 for l in range(CURSOR_Y, CURSOR_Y + 6)}

def solve(K=15):
    C = {n: button_candidates(rim_px(grid_px(n)), K) for n in GRIDS}
    best = None
    for pu, iu, wu in itertools.product(C['play'], C['icon'], C['wrench']):
        m = max(add(pu[0], iu[0], wu[0], CUR).values())
        n = len(pu[1]) + len(iu[1]) + len(wu[1])
        if best is None or (m, n) < best[:2]: best = (m, n, {'play': pu, 'icon': iu, 'wrench': wu})
    print('solved: max sprites/line %d, button OAM %d' % best[:2])
    lay = {k: [[x, y, [[p[0], p[1], c] for p, c in sorted(own.items())]] for x, y, own in v[1]]
           for k, v in best[2].items()}
    json.dump(lay, open(os.path.join(TB, 'layout.json'), 'w'), indent=0)

# CGB output
def tile_bytes(pixels):
    """pixels: 8x8 rows of 0..3 -> 16 bytes (2bpp, plane 0 then plane 1 per row)"""
    out = []
    for row in pixels:
        lo = hi = 0
        for i, v in enumerate(row):
            lo |= (v & 1) << (7 - i); hi |= (v >> 1) << (7 - i)
        out += [lo, hi]
    return out

def cgb_data():
    lay = json.load(open(os.path.join(TB, 'layout.json')))
    obj_pals = list(OBJ_PALS)
    def obj_pal_for(cols):
        for i, p in enumerate(obj_pals):
            if set(cols) <= set(p): return i
        raise SystemExit('no OBJ palette holds colours %s' % ''.join(sorted(cols)))
    tiles = []; tindex = {}
    def tile_id(t):
        k = tuple(t)
        if k not in tindex: tindex[k] = len(tiles); tiles.append(t)
        return tindex[k]
    groups = {}; profiles = {}
    for key, spr in lay.items():
        px = grid_px(key); rim = rim_px(px)
        covered = {}
        ents = []; cnt = {}
        for x, y, own in spr:
            cols = {c for _, _, c in own}
            pi = obj_pal_for(cols); pal = obj_pals[pi]
            g = [[0] * 8 for _ in range(8)]
            for X, Y, c in own:
                assert rim.get((X, Y)) == c, 'layout does not match the art: run --solve'
                g[Y - y][X - x] = pal.index(c) + 1; covered[(X, Y)] = 1
            ents.append((x, y, OBJ_TILE_BASE + tile_id(tile_bytes(g)), OBJ_PAL_NUMS[pi]))
            for l in range(y, y + SPR_H): cnt[l] = cnt.get(l, 0) + 1
        missing = set(rim) - set(covered)
        if missing: raise SystemExit('%s: %d rim pixels without a sprite: run --solve' % (key, len(missing)))
        groups[key] = ents; profiles[key] = cnt
    if OBJ_TILE_BASE + len(tiles) > 128: raise SystemExit('too many rim tiles: %d' % len(tiles))
    # scanline / OAM budget (the cursor's 6 visible rows counted)
    c = add(profiles['play'], profiles['icon'], profiles['wrench'], CUR)
    m = max(c.values()); n = sum(len(v) for v in groups.values())
    if m > 10 or n > MAX_GROUP_OAM:
        raise SystemExit('budget: %d sprites on a line, %d OAM' % (m, n))
    print('  max %d sprites/line, %d OAM' % (m, n))
    # cores
    cpals = []
    cores = {}; ctiles = []; cindex = {}
    for key in lay:
        px = grid_px(key); ents = []
        for cx, cy, cols in core_cells(px):
            cols = set(cols)
            for i, p in enumerate(cpals):
                if cols <= set(p): break
            else:
                # grow a palette that still has room, else start a new one
                for i, p in enumerate(cpals):
                    if len(set(p) | cols) <= 4: cpals[i] = ''.join(sorted(set(p) | cols)); break
                else:
                    cpals.append(''.join(sorted(cols))); i = len(cpals) - 1
            ents.append((cx, cy, cols, i))
        cores[key] = ents
    if len(cpals) > len(CORE_BG_PALS): raise SystemExit('core needs %d BG palettes' % len(cpals))
    cpals = [(p + '3333')[:4] for p in cpals]
    for key, ents in cores.items():
        px = grid_px(key); out = []
        for cx, cy, cols, i in ents:
            # (a pixel of a 5th colour: under a sprite, any index)
            g = [[max(0, cpals[i].find(px[(cx * 8 + x, cy * 8 + y)])) for x in range(8)] for y in range(8)]
            k = tuple(tile_bytes(g))
            if k not in cindex: cindex[k] = len(ctiles); ctiles.append(list(k))
            out.append((cx, cy, CORE_TILE_BASE + cindex[k], 0x08 | CORE_BG_PALS[i]))
        cores[key] = out
    print('  CGB: %d rim tiles, %d core tiles, core palettes %s' % (len(tiles), len(ctiles), cpals))
    return groups, tiles, obj_pals, cores, ctiles, cpals

# DMG output
def sky_tiles():
    s = open(os.path.join(ROOT, 'src', 'graphics', 'bg_parallax_data_0.c')).read()
    s = s.split('// Phase 0')[1].split('}')[0]
    b = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', s)][:768]
    def px(t, x, y):
        d = b[t * 16:t * 16 + 16]
        return ((d[2 * y] >> (7 - x)) & 1) | (((d[2 * y + 1] >> (7 - x)) & 1) << 1)
    return px

def sky_ty0(ty): return (0, 16, 32)[(ty >> 1) % 3] + ((ty & 1) << 3)

DMG_COLS, DMG_ROWS = 5, 4

def dmg_rect(name):
    px = grid_px(name)
    art = {p: c for p, c in px.items()}
    xs = [p[0] for p in art]; ys = [p[1] for p in art]
    return min(xs), min(ys) // 8, art

def dmg_tiles(name, scx, with_sky):
    """DMG_COLS x DMG_ROWS map cells from map column (left + scx) >> 3: the button over the sky"""
    sky = sky_tiles()
    left, row0, art = dmg_rect(name)
    assert (max(p[1] for p in art)) // 8 < row0 + DMG_ROWS
    c0 = (left + scx) >> 3
    assert (max(p[0] for p in art) + scx) >> 3 < c0 + DMG_COLS
    out = []
    for r in range(DMG_ROWS):
        for c in range(DMG_COLS):
            g = [[0] * 8 for _ in range(8)]
            for j in range(8):
                for i in range(8):
                    mx, sy_ = (c0 + c) * 8 + i, (row0 + r) * 8 + j
                    a = art.get((mx - scx, sy_))
                    a = {'y': '1', 'o': '2'}.get(a, a)    # DMG: the yellow face in the button's greys
                    if a in ('1', '2', '3'): g[j][i] = int(a)
                    elif with_sky: g[j][i] = sky(sky_ty0(row0 + r) + ((c0 + c) & 7), i, j)
            out += tile_bytes(g)
    return out

# emit C
def carr(name, data, per=16, typ='uint8_t'):
    lines = [', '.join('0x%02X' % v for v in data[i:i + per]) for i in range(0, len(data), per)]
    return 'const %s %s[] = {\n    %s\n};\n' % (typ, name, ',\n    '.join(lines))

def emit():
    groups, tiles, obj_pals, cores, ctiles, cpals = cgb_data()
    keys = ['play', 'icon', 'wrench']
    c = ['// Generated by tools/gen_title_buttons.py: do not edit', '#pragma bank 70', '',
         '#include <gb/gb.h>', '#include <gb/cgb.h>', '#include <stdint.h>', '#include "title_buttons.h"', '',
         'BANKREF(title_buttons)', '']
    c.append(carr('tb_obj_tiles', sum(tiles, [])))
    c.append(carr('tb_core_tiles', sum(ctiles, [])))
    def rgb(col): r, g, b = COLORS[col]; return 'RGB8(%d, %d, %d)' % (r, g, b)
    c.append('const palette_color_t tb_obj_palettes[] = {\n' + ',\n'.join(
        '    RGB8(255, 255, 255), ' + ', '.join(rgb(x) for x in p) for p in obj_pals) + '\n};\n')
    c.append('const palette_color_t tb_core_palettes[] = {\n' + ',\n'.join(
        '    ' + ', '.join(rgb(x) for x in p) for p in cpals) + '\n};\n')
    # sprites: OAM y, x, tile, attr (CGB palette)
    for k in keys:
        c.append(carr('tb_spr_' + k, [v for x, y, t, p in groups[k] for v in (y + 16, x + 8, t, p)], per=4))
        c.append(carr('tb_core_' + k, [v for cx, cy, t, a in cores[k] for v in (cx, cy, t, a)], per=4))
    c.append('const tb_group_t tb_groups[3] = {\n' + ',\n'.join(
        '    { tb_spr_%s, %d, tb_core_%s, %d }' % (k, len(groups[k]), k, len(cores[k])) for k in keys) + '\n};\n')
    open(OUT_C, 'w', newline='\n').write('\n'.join(c))

    # DMG: 64 scroll positions of 20 tiles per side = 20 KB, as two halves of 32 (10 KB) in two banks
    bank = 71; names = []
    for name in ('icon', 'wrench'):
        if True:
            for half in (0, 1):
                n = 'tb_dmg_%s_%d' % (name, half)
                data = sum((dmg_tiles(name, scx, True) for scx in range(half * 32, half * 32 + 32)), [])
                body = ['// Generated by tools/gen_title_buttons.py: do not edit', '#pragma bank %d' % bank, '',
                        '#include <stdint.h>', '#include <gb/gb.h>', '', 'BANKREF(%s)' % n, '',
                        carr(n, data)]
                open(os.path.join(ROOT, 'src', 'graphics', '%s.c' % n), 'w', newline='\n').write('\n'.join(body))
                names.append(n); bank += 1
    plain = []
    for name in ('icon', 'wrench'):
        plain += dmg_tiles(name, 0, False)
    with open(OUT_C, 'a', newline='\n') as f:
        f.write('\n// DMG, "show background" off: the buttons on a blank sky, scroll 0\n')
        f.write(carr('tb_dmg_plain', plain))
        lefts = []; rows = []
        for name in ('icon', 'wrench'):
            l, r0, _ = dmg_rect(name); lefts.append(l); rows.append(r0)
        f.write(carr('tb_dmg_left', lefts, per=4))
        f.write(carr('tb_dmg_row0', rows, per=4))

    h = ['// Generated by tools/gen_title_buttons.py: do not edit', '#ifndef TITLE_BUTTONS_H', '#define TITLE_BUTTONS_H', '',
         '#include <gb/gb.h>', '#include <gb/cgb.h>', '#include <stdint.h>', '',
         '// Big title buttons. CGB: sprite rims (8x8, OAM y/x/tile/palette) + BG core cells (map x/y,',
         '// tile in VRAM bank 1, attribute) of play, icon, wrench.',
         'typedef struct { const uint8_t *spr; uint8_t nspr; const uint8_t *core; uint8_t ncore; } tb_group_t;',
         '#define TB_PLAY 0', '#define TB_ICON 1', '#define TB_WRENCH 2',
         '#define TB_OBJ_TILE_BASE %d' % OBJ_TILE_BASE, '#define TB_OBJ_TILE_COUNT %d' % len(tiles),
         '#define TB_CORE_TILE_BASE %d' % CORE_TILE_BASE, '#define TB_CORE_TILE_COUNT %d' % len(ctiles),
         '#define TB_OBJ_PAL_COUNT %d' % len(obj_pals),
         '#define TB_CORE_PAL_COUNT %d' % len(cpals),
         '#define TB_CURSOR_Y %d   // screen y' % CURSOR_Y,
         '#define TB_PLAY_CURSOR_X %d' % (PLAY_X + 12),
         '#define TB_ICON_CURSOR_X %d' % (ICON_X + 8), '#define TB_WRENCH_CURSOR_X %d' % (WRENCH_X + 8),
         '', '// DMG side buttons: %d x %d map cells per side, blended with the sky for each SCX 0..63' % (DMG_COLS, DMG_ROWS),
         '#define TB_DMG_COLS %d' % DMG_COLS, '#define TB_DMG_ROWS %d' % DMG_ROWS,
         '#define TB_DMG_TILES (TB_DMG_COLS * TB_DMG_ROWS)', '#define TB_DMG_BUF_TILE %d' % DMG_BUF_TILE,
         '#define TB_DMG_SKY_TILE %d' % DMG_SKY_TILE,
         '', 'BANKREF_EXTERN(title_buttons)',
         'extern const uint8_t tb_obj_tiles[];', 'extern const uint8_t tb_core_tiles[];',
         'extern const palette_color_t tb_obj_palettes[];', 'extern const palette_color_t tb_core_palettes[];',
         'extern const tb_group_t tb_groups[3];',
         'extern const uint8_t tb_dmg_plain[];   // [side][TB_DMG_TILES * 16]',
         'extern const uint8_t tb_dmg_left[2];   // screen x of the art, [side]',
         'extern const uint8_t tb_dmg_row0[2];   // first map row, [side]', '']
    for n in names:
        h += ['BANKREF_EXTERN(%s)' % n, 'extern const uint8_t %s[];' % n]
    h += ['', '#endif', '']
    open(OUT_H, 'w', newline='\n').write('\n'.join(h))
    print('wrote', OUT_C, OUT_H, 'and', len(names), 'DMG banks')

if __name__ == '__main__':
    if '--solve' in sys.argv: solve()
    emit()
