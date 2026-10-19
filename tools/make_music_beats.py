"""Beat timelines of the songs, for the pulsing rods (GD's pulse rods: their balls swell to the
music).

    python tools/make_music_beats.py

Plays every song of src/music/*.c through on paper like hUGEDriver does (Fxx speed, Dxx
pattern break to row xx - 1 of the next order (D00: none), Bxx jump to order xx - 1 (B00: the
next order), after the last order back to order 0, where the game stops the level songs: main.c
step_music; the timeline loops when the song gets back to a row it already played) and lists its hits in driver ticks (hUGE_dosound calls,
counted by step_music in main.c) on the noise channel (drums): a strong hit on a beat (every 4th
row of a pattern) or with the song's kick (its lowest drum note in common use), a weak one
otherwise (hi-hats and the like). Where the drums are silent for a second or more, fake beats keep
the rods pulsing to the song's tempo (add_fake_beats; also used by tools/convert_furnace.py).

Writes src/music/music_beats.c (bank 65: the timelines and music_beats_start / music_beats_poll)
and include/music_beats.h.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MUSIC = ROOT / "src" / "music"
OUT_C = MUSIC / "music_beats.c"
OUT_H = ROOT / "include" / "music_beats.h"
BANK = 65
STRONG, WEAK, FILL, END, LOOP = 2, 1, 0, 0xFE, 0xFF
FAKE = 3                       # a fake beat (add_fake_beats): sp_draw.c pulses gentler
HUGE_HZ = 55                   # hUGE songs: timer dividers 176..187 (assets.c), 51..59 ticks a second
LOOPING_SONGS = {"practice"}   # not stopped at their end (main.c step_music: banks 1 and 212)


NOTES = ["C_", "Cs", "D_", "Ds", "E_", "F_", "Fs", "G_", "Gs", "A_", "As", "B_"]


def note_num(n):
    if n.isdigit():   # some songs write the note number
        return int(n)
    return NOTES.index(n[:2]) + 12 * (int(n[2]) - 3)


def parse(path):
    s = path.read_text()
    m = re.search(r"const hUGESong_t (\w+) = \{(\d+),", s)
    if not m:
        return None
    name, tempo = m.group(1), int(m.group(2))
    pats = {}
    for pm in re.finditer(r"static const unsigned char (\w+)\[\] = \{(.*?)\};", s, re.S):
        rows = re.findall(r"DN\((\w+),\s*(\d+),\s*0x([0-9A-Fa-f]+)\)", pm.group(2))
        if rows:
            pats[pm.group(1)] = [(None if n == "___" else note_num(n), int(e, 16)) for n, _, e in rows]
    orders = []
    for ch in range(1, 5):
        om = re.search(r"order%d\[\] = \{(.*?)\};" % ch, s, re.S)
        orders.append([p.strip() for p in om.group(1).split(",") if p.strip()])
    return name, tempo, pats, orders


def timeline(tempo, pats, orders, stops_at_end=True):
    """[(tick, strength)] of the hits, the tick the song loops back at and the tick it loops to
    (None: it ends). stops_at_end: the game stops the song when it wraps from its last order to
    order 0 (main.c step_music: every song but the menu and practice ones)."""
    n_orders = len(orders[0])
    drums = {}
    for p in orders[3]:
        for n, _ in pats[p]:
            if n is not None:
                drums[n] = drums.get(n, 0) + 1
    common = [n for n, k in drums.items() if k * 20 >= sum(drums.values())]
    kick = min(common) if common else None
    seen = {}
    hits = []
    o, r, t, speed = 0, 0, 0, tempo
    while True:
        if (o, r) in seen:
            return hits, t, seen[(o, r)]
        seen[(o, r)] = t
        cells = [pats[orders[ch][o]][r] for ch in range(4)]
        # hUGEDriver (fx_pos_jump, fx_pattern_break, tick_time): row_break and next_order are
        # stored + 1 (0 = none). Bxx: next_order = xx (B00: none), row_break 1 if still 0.
        # Dxx: row_break = xx (D00: none). At the end of the row: row_break - 1 is the row,
        # next_order - 1 the order (none: the next one); after the last order: order 0.
        row_break, next_order = 0, 0
        for _, e in cells:
            kind, param = e >> 8, e & 0xFF
            if kind == 0xF and param:
                speed = param
            elif kind == 0xB:
                if not row_break:
                    row_break = 1
                next_order = param
            elif kind == 0xD:
                row_break = param
        drum = cells[3][0]
        if drum is not None:
            hits.append((t, STRONG if (r % 4 == 0 or drum == kick) else WEAK))
        t += speed
        prev_o = o
        if row_break:
            r = row_break - 1
            o = next_order - 1 if next_order else (o + 1) % n_orders
        else:
            r += 1
            if r >= len(pats[orders[0][o]]):
                o, r = (o + 1) % n_orders, 0
        # main.c step_music stops a level song when it goes from its last order to order 0
        if stops_at_end and prev_o == n_orders - 1 and o == 0:
            return hits, t, None


def add_fake_beats(hits, end_t, hz):
    """hits plus FAKE beats where the drums are silent for a second (hz ticks) or more: from a
    second after the last hit up to half a step before the next one (or the song's end), twice a
    beat (on the beat and halfway). The beat: the most common gap between the strong hits around (16 on each side), doubled
    or halved into 0.3 .. 0.8 s; on the grid of the last strong hit before the silence (the first
    one after it for the silence at the start). Without 2 strong hits: no fake beats."""
    strong = [t for t, c in hits if c == STRONG]
    if len(strong) < 2:
        return list(hits)

    def beat_at(i):
        ds = [strong[j + 1] - strong[j] for j in range(max(0, i - 16), min(len(strong) - 1, i + 16))]
        ds = [d for d in ds if d > 0] or [strong[-1] - strong[0]]
        p = float(max(set(ds), key=lambda d: (ds.count(d), -d)))
        while p < 0.3 * hz:
            p *= 2
        while p > 0.8 * hz:
            p /= 2
        return p
    fakes = []
    real = sorted({t for t, _ in hits})
    for a, b, last in [(0, real[0], False)] + list(zip(real, real[1:], [False] * len(real))) + [(real[-1], end_t, True)]:
        if b - a < hz:
            continue
        before = [i for i, t in enumerate(strong) if t <= a]
        i = before[-1] if before else 0
        p = beat_at(i) / 2   # twice a beat
        k = int((a + hz - strong[i]) // p)   # the first step a second into the silence or later
        while True:
            t = strong[i] + k * p
            k += 1
            if t < a + hz:
                continue
            if (t >= b) if last else (t > b - p / 2):
                break
            fakes.append((int(round(t)), FAKE))
    return sorted(list(hits) + fakes)


def encode(hits, end_t, loop_t):
    """(delta, code) pairs: delta ticks after the previous entry (FILL entries bridge gaps over
    255), then the END or LOOP entry at end_t. A looping song has a FILL entry at the loop point:
    the loop part (index loop_i on) counts from it, the first time and after every LOOP."""
    out, prev = [], 0

    def emit(t, code):
        nonlocal prev
        d = t - prev
        while d > 255:
            out.append((255, FILL))
            d -= 255
        out.append((d, code))
        prev = t
    loop_i = None
    for t, c in hits:
        if loop_t is not None and loop_i is None and t >= loop_t:
            emit(loop_t, FILL)
            loop_i = len(out)
        emit(t, c)
    if loop_t is None:
        emit(end_t, END)
        return out, None
    if loop_i is None:
        emit(loop_t, FILL)
        loop_i = len(out)
    emit(end_t, LOOP)
    return out, loop_i


def main():
    songs = []
    for f in sorted(MUSIC.glob("*.c")):
        if f.name == OUT_C.name:
            continue
        p = parse(f)
        if p:
            songs.append(p)
    arrays, entries = [], []
    for name, tempo, pats, orders in songs:
        hits, end_t, loop_t = timeline(tempo, pats, orders, name not in LOOPING_SONGS)
        hits = add_fake_beats(hits, end_t, HUGE_HZ)
        data, loop_i = encode(hits, end_t, loop_t)
        strong = sum(1 for _, c in hits if c == STRONG)
        fake = sum(1 for _, c in hits if c == FAKE)
        print(f"{name}: {len(hits) - fake} hits ({strong} strong) + {fake} fake beats, {end_t} ticks"
              + (f", loops to tick {loop_t}" if loop_t is not None else ""))
        flat = ", ".join(f"{d}, {c}" for d, c in data)
        arrays.append(f"static const uint8_t beats_{name}[] = {{ {flat} }};")
        entries.append((name, loop_i))

    decls = "\n".join(f"extern const hUGESong_t {n};" for n, _ in entries)
    table = "\n".join(f"    {{ &{n}, beats_{n}, {'beats_%s + %d' % (n, 2 * li) if li is not None else '0'} }},"
                      for n, li in entries)
    OUT_C.write_text(f"""#pragma bank {BANK}
// Generated by tools/make_music_beats.py from the songs in src/music. Do not edit.

#include <gb/gb.h>
#include "hUGEDriver.h"
#include "music_beats.h"

{decls}

// (delta ticks, code): {WEAK} weak hit, {STRONG} strong hit, {FILL} none (a gap over 255 ticks),
// {END} the song ends, {LOOP} it loops (to the song's loop entry)
{chr(10).join(arrays)}

typedef struct {{ const hUGESong_t *song; const uint8_t *beats; const uint8_t *loop; }} SongBeats;
static const SongBeats song_beats[] = {{
{table}
}};

static const uint8_t *bp;        // the next entry, 0: none
static const uint8_t *loop_p;
static uint16_t next_t;          // its tick

void music_beats_start(const hUGESong_t *song) BANKED {{
    bp = 0;
    for (uint8_t i = 0; i < sizeof(song_beats) / sizeof(song_beats[0]); i++) {{
        if (song_beats[i].song == song) {{
            bp = song_beats[i].beats;
            loop_p = song_beats[i].loop;
            next_t = bp[0];
            return;
        }}
    }}
}}

uint8_t music_beats_poll(void) BANKED {{
    if (!bp) return 0;
    uint16_t t;
    do {{ t = music_ticks; }} while (t != music_ticks);   // set by the timer interrupt
    uint8_t hit = 0;
    // entries up to tick t: the song has played them (t counts the calls done)
    while ((int16_t)(t - next_t) > 0) {{
        uint8_t c = bp[1];
        if (c == {END}) {{ bp = 0; break; }}
        if (c == {LOOP}) bp = loop_p;
        else {{
            if (c == {FAKE} ? !hit : (c > hit || hit == {FAKE})) hit = c;   // a fake beat: only alone
            bp += 2;
        }}
        next_t += bp[0];
    }}
    return hit;
}}
""")
    OUT_H.write_text(f"""// Generated by tools/make_music_beats.py. Do not edit.
#ifndef MUSIC_BEATS_H
#define MUSIC_BEATS_H

#include <gb/gb.h>
#include <stdint.h>
#include "hUGEDriver.h"

#define MUSIC_HIT_WEAK {WEAK}
#define MUSIC_HIT_STRONG {STRONG}
#define MUSIC_HIT_FAKE {FAKE}     // the drums have been silent a second or more: a beat of the song's tempo

extern volatile uint16_t music_ticks;   // hUGE_dosound calls since the song started (main.c)

// The song that init_music_banked just started (no timeline: no hits)
void music_beats_start(const hUGESong_t *song) BANKED;
// Strongest hit the music played since the last call: 0 none, MUSIC_HIT_WEAK, MUSIC_HIT_STRONG,
// MUSIC_HIT_FAKE (none of the others)
uint8_t music_beats_poll(void) BANKED;

#endif
""")


if __name__ == "__main__":
    main()
