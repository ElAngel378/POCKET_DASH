"""Furnace songs for the game: music/Furnace/*.fur -> src/furnace/ (player + songs), include/

    python tools/convert_furnace.py

Uses the fur2gb repo (../fur2gb, or FUR2GB_DIR): its converter runs the reference Furnace on each
song and checks the result against Furnace tick by tick (fur2gb's player model), and its player
(driver/) is copied here:
  src/furnace/furplayer_core.c, furplayer_tick.s   bank 0 (the per-tick player, fur_tick)
  src/furnace/furplayer.c, include/furplayer.h     bank FUR_CODE_BANK (fur_play, fur_set_mute)
Writes:
  src/furnace/fs_<song>*.c   the song streams, in fixed banks from FIRST_SONG_BANK up
  src/furnace/furnace_songs.c (bank FUR_CODE_BANK) the song table and the songs' beat timelines
                             for the pulsing rods (tools/make_music_beats.py does the hUGE songs)
  include/furnace_songs.h    song ids, names (for the music test), level -> song map
"""
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FUR2GB = Path(os.environ.get("FUR2GB_DIR", ROOT.parent / "fur2gb"))
sys.path.insert(0, str(FUR2GB / "tools"))
import fur2gb  # noqa: E402

SRC = ROOT / "music" / "Furnace"
OUT = ROOT / "src" / "furnace"
HDR = ROOT / "include"
FUR_CODE_BANK = 25            # fur_play etc., the song table, the beat timelines
FIRST_SONG_BANK = 130         # free banks 130..199 (romusage)
LAST_SONG_BANK = 199
# The player's state goes to HRAM (WRAM is full: the stack needs every byte left), so it is cut
# down: call depth 1 (songs grow ~25%) and at most 4 chunks of 16 KB per song.
FRAMES = 1
CHUNKS = 4

# (file, id, music test name, level index that uses it or None, always used (else only with the
# FURNACE setting), approximate loop)
SONGS = [
    ("MenuLoop.fur", "menuloop", "MENU THEME", None, False, False),
    ("Stereo Madness.fur", "stereomadness", "STEREO MADNESS", 0, True, False),
    ("BackOnTrack.fur", "backontrack", "BACK ON TRACK", 1, True, False),
    ("Polargeist.fur", "polargeist", "POLARGEIST", 2, True, False),
    ("DryOut32.fur", "dryout", "DRY OUT", 3, False, False),
    ("Base After Base.fur", "baseafterbase", "BASE AFTER BASE", 4, False, False),
    ("Cycles.fur", "cycles", "CYCLES", 8, False, False),
    ("ToE.fur", "toe", "THEORY OF EVRYTHNG", None, False, False),
    ("Electrodynamix.fur", "electrodynamix", "ELECTRODYNAMIX", None, False, True),
    ("Deadlocked.fur", "deadlocked", "DEADLOCKED", None, False, False),
    ("Scheming weasel.fur", "schemingweasel", "SCHEMING WEASEL", None, False, True),
]
NUM_LEVELS = 12
STRONG, WEAK, FILL, END, LOOP = 2, 1, 0, 0xFE, 0xFF


class Banks:
    """First fit of the generated files into the free banks."""

    def __init__(self):
        self.free = {b: 16384 for b in range(FIRST_SONG_BANK, LAST_SONG_BANK + 1)}

    def __call__(self, size):
        size += 16  # GBDK's per-file bank padding
        for b in sorted(self.free):
            if self.free[b] >= size:
                self.free[b] -= size
                return b
        raise SystemExit("out of free banks for the Furnace songs")


def noise_hz(nr43):
    r = nr43 & 7
    return 524288 / (r if r else 0.5) / (2 << (nr43 >> 4))


def beats(trace, song):
    """(tick, code) hits: noise channel note-ons (NR44 bit 7), strong on a beat (every 4th row)
    or with the song's kick (its lowest common noise pitch), weak otherwise."""
    n = fur2gb.compared_ticks(trace, song["exact_ticks"])
    end = song["ticks"]  # ticks in the stream (one pass: the loop jumps back after it)
    hits, nr43 = [], 0
    for ti, t in enumerate(trace.ticks[:min(n, end)]):
        for o in t.ops:
            if o[0] != "W":
                continue
            if o[1] == 0x22:
                nr43 = o[2]
            elif o[1] == 0x23 and o[2] & 0x80:
                hits.append((ti, t.row, nr43))
    pitches = {}
    for _, _, p in hits:
        pitches[p] = pitches.get(p, 0) + 1
    common = [p for p, c in pitches.items() if c * 20 >= len(hits)]
    kick = min(common, key=noise_hz) if common else None
    out = []
    for ti, row, p in hits:
        code = STRONG if row % 4 == 0 or p == kick else WEAK
        if out and out[-1][0] == ti:
            out[-1] = (ti, max(out[-1][1], code))
        else:
            out.append((ti, code))
    return out, end, song["loop_tick"]


def timeline(hits, end, loop):
    """Bytes of (delta ticks, code) pairs as music_beats.c reads them, and the loop entry offset.
    A FILL entry sits exactly at the loop start so the loop's deltas count from there."""
    data, t, loop_ofs = [], 0, None
    events = list(hits)
    if loop is not None:
        events.append((loop, None))
        events.sort(key=lambda e: (e[0], e[1] is not None))
    for tick, code in events:
        d = tick - t
        while d > 255:
            data += [255, FILL]
            d -= 255
        if code is None:
            data += [d, FILL]
            loop_ofs = len(data)
        else:
            data += [d, code]
        t = tick
    d = end - t
    while d > 255:
        data += [255, FILL]
        d -= 255
    data += [d, LOOP if loop is not None else END]
    return data, loop_ofs


def copy_player():
    OUT.mkdir(parents=True, exist_ok=True)
    drv = FUR2GB / "driver"
    shutil.copy(drv / "furplayer_core.c", OUT / "furplayer_core.c")
    # the player's state goes to HRAM: WRAM is full (the stack needs every byte left)
    tick = (drv / "furplayer_tick.s").read_text()
    for a, b in (("\t.area\t_DATA\n_fur_ctx::", "\t.area\t_HRAM\n_fur_ctx::"),
                 ("\tFRAMES = 2\t", f"\tFRAMES = {FRAMES}\t"),
                 ("\tCHUNKS = 8\t", f"\tCHUNKS = {CHUNKS}\t")):
        assert tick.count(a) == 1, a
        tick = tick.replace(a, b)
    (OUT / "furplayer_tick.s").write_text(tick)
    (OUT / "furplayer.c").write_text(f"#pragma bank {FUR_CODE_BANK}\n// Copied from fur2gb by tools/convert_furnace.py\n"
                                     "#define FUR_NO_FRAME  // the timer interrupt ticks the player (main.c)\n"
                                     + (drv / "furplayer.c").read_text())
    h = (drv / "furplayer.h").read_text()
    for a, b in (("#ifndef FUR_API\n#define FUR_API\n#endif\n", "#define FUR_API BANKED  /* furplayer.c is banked here */\n"),
                 ("#define FUR_MAX_CHUNKS 8 ", f"#define FUR_MAX_CHUNKS {CHUNKS} "),
                 ("#define FUR_STACK_DEPTH 2 ", f"#define FUR_STACK_DEPTH {FRAMES} ")):
        assert h.count(a) == 1, a
        h = h.replace(a, b)
    (HDR / "furplayer.h").write_text("/* Copied from fur2gb by tools/convert_furnace.py */\n" + h)


def main():
    fur2gb.CHUNK_SIZE = 16300  # leave room for GBDK's BANKREF stubs in each bank
    fur2gb.MAX_DEPTH = FRAMES
    fur2gb.MAX_CHUNKS = CHUNKS
    copy_player()
    for f in OUT.glob("fs_*.c"):
        f.unlink()
    banks = Banks()
    table, beat_arrays = [], []
    for fname, ident, name, level, always, approx in SONGS:
        trace, song = fur2gb.compile_song(SRC / fname, 4, approx_loop=approx)
        sym = f"fs_{ident}"
        files = fur2gb.emit_c(OUT, sym, song, bank_for=banks)
        (OUT / f"{sym}.h").unlink()
        bank = int(files[-2].read_text().split("\n")[1].split()[-1])
        hits, end, loop = beats(trace, song)
        data, loop_ofs = timeline(hits, end, loop)
        beat_arrays.append((ident, data, loop_ofs))
        table.append((ident, sym, bank))
        print(f"{fname}: {song['bytes'] + song['wave_bytes']} B, {song['ticks']} ticks, "
              f"loop {song['loop_tick']}, {len(hits)} beat hits"
              + (", exact until the first loop" if song["exact_ticks"] else ""))

    # song table + beats (bank FUR_CODE_BANK)
    c = [f"#pragma bank {FUR_CODE_BANK}",
         "// Generated by tools/convert_furnace.py from music/Furnace. Do not edit.",
         "#include <gb/gb.h>", '#include "furplayer.h"', '#include "furnace_songs.h"', ""]
    for ident, sym, bank in table:
        c += [f"BANKREF_EXTERN({sym})", f"extern const fur_song_t {sym};"]
    c += ["", "const FurSong fur_songs[FUR_NUM_SONGS] = {"]
    for ident, sym, bank in table:
        c.append(f"    {{ &{sym}, BANK({sym}) }},")
    c += ["};", "", "// (delta ticks, code): 1 weak hit, 2 strong hit, 0 none (a gap over 255 ticks or the loop",
          "// start), 254 the song ends, 255 it loops (to the loop entry)"]
    for ident, data, loop_ofs in beat_arrays:
        c.append(f"static const uint8_t fbeats_{ident}[] = {{")
        for o in range(0, len(data), 24):
            c.append("    " + ", ".join(str(b) for b in data[o:o + 24]) + ",")
        c.append("};")
    c += ["", "static const uint8_t * const fbeats[FUR_NUM_SONGS][2] = {"]
    for ident, data, loop_ofs in beat_arrays:
        lp = f"fbeats_{ident} + {loop_ofs}" if loop_ofs is not None else "0"
        c.append(f"    {{ fbeats_{ident}, {lp} }},")
    c += ["};", "",
          "extern volatile uint16_t music_ticks;",
          "// in HRAM (src/furnace/music_hram.s): WRAM is full",
          "extern const uint8_t *fur_bp, *fur_loop_p;",
          "extern uint16_t fur_next_t;",
          "#define bp fur_bp",
          "#define loop_p fur_loop_p",
          "#define next_t fur_next_t",
          "",
          "void fur_beats_start(uint8_t id) BANKED {",
          "    if (id >= FUR_NUM_SONGS) { bp = 0; return; }",
          "    bp = fbeats[id][0];",
          "    loop_p = fbeats[id][1];",
          "    next_t = bp[0];",
          "}",
          "",
          "// Same as music_beats_poll (music_beats.c) for the Furnace song playing",
          "uint8_t fur_beats_poll(void) BANKED {",
          "    if (!bp) return 0;",
          "    uint16_t t;",
          "    do { t = music_ticks; } while (t != music_ticks);   // set by the timer interrupt",
          "    uint8_t hit = 0;",
          "    while ((int16_t)(t - next_t) > 0) {",
          "        uint8_t c = bp[1];",
          "        if (c == 254) { bp = 0; break; }",
          "        if (c == 255) bp = loop_p;",
          "        else {",
          "            if (c > hit) hit = c;",
          "            bp += 2;",
          "        }",
          "        next_t += bp[0];",
          "    }",
          "    return hit;",
          "}", ""]
    (OUT / "furnace_songs.c").write_text("\n".join(c))

    lv = ["FUR_NONE"] * NUM_LEVELS
    lv_always = ["0"] * NUM_LEVELS
    for i, (fname, ident, name, level, always, approx) in enumerate(SONGS):
        if level is not None:
            lv[level] = f"FUR_{ident.upper()}"
            lv_always[level] = "1" if always else "0"
    h = ["// Generated by tools/convert_furnace.py from music/Furnace. Do not edit.",
         "#ifndef FURNACE_SONGS_H", "#define FURNACE_SONGS_H", "", "#include <gb/gb.h>",
         '#include "furplayer.h"', "", f"#define FUR_CODE_BANK {FUR_CODE_BANK}",
         f"#define FUR_NUM_SONGS {len(SONGS)}", "#define FUR_NONE 0xFF"]
    for i, s in enumerate(SONGS):
        h.append(f"#define FUR_{s[1].upper()} {i}")
    h += ["", "// music test names", "#define FUR_SONG_NAMES { " + ", ".join(f'"{s[2]}"' for s in SONGS) + " }",
          "// Furnace song of each level (FUR_NONE: hUGE only), and whether it is used without the",
          "// FURNACE setting (these replaced hUGE ports of the same Furnace songs)",
          "#define FUR_LEVEL_SONGS { " + ", ".join(lv) + " }",
          "#define FUR_LEVEL_ALWAYS { " + ", ".join(lv_always) + " }", "",
          "typedef struct { const fur_song_t *song; uint8_t bank; } FurSong;",
          "extern const FurSong fur_songs[FUR_NUM_SONGS];",
          "void fur_beats_start(uint8_t id) BANKED;   // FUR_NONE: no timeline",
          "uint8_t fur_beats_poll(void) BANKED;", "", "#endif", ""]
    (HDR / "furnace_songs.h").write_text("\n".join(h))
    used = {b: 16384 - f for b, f in banks.free.items() if f < 16384}
    print(f"song banks: {', '.join(f'{b} ({u} B)' for b, u in sorted(used.items()))}")


if __name__ == "__main__":
    main()
