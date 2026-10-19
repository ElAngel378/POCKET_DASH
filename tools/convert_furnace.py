"""Furnace songs for the game: music/Furnace/*.fur -> src/furnace/ (player + songs), include/

    python tools/convert_furnace.py      (tools/build_levels.py also runs it)

Drop a .fur in music/Furnace and it is in the game:
  - named like a level (the TMX file: "BaseAfterBase.fur", "DryOut.fur" for dryout.tmx; case,
    spaces, punctuation and trailing digits don't matter), it is that level's song; with the
    LEGACY setting (or without a .fur) the level plays its hUGETracker song (music/Legacy)
  - MenuLoop.fur is the menu theme (the hUGETracker one with LEGACY)
  - fur2gb's tools/uge2fur.py converts a music/Legacy .uge to a .fur to edit in Furnace
  - any other song only appears in the music test (named by its Furnace song name, or file name)
Every song is in the music test, after the hUGETracker ones. Songs that aren't Game Boy songs, or
that fail to convert, are skipped with a warning. A song that doesn't repeat exactly within the
traced loops (virtual tempo, uneven grooves) plays its intro and first pass exactly, then jumps
back to the loop start (fur2gb's --approx-loop).

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
Converted songs are cached in .cache/furnace (by file contents, fur2gb and Furnace version), so
only new or changed songs are traced again. Without fur2gb or its Furnace build the generated
files are left as they are.
"""
import hashlib
import os
import pickle
import re
import shutil
import sys
from pathlib import Path

import make_music_beats   # add_fake_beats (tools/ is on the path: run as a script)

ROOT = Path(__file__).resolve().parent.parent
FUR2GB = Path(os.environ.get("FUR2GB_DIR", ROOT.parent / "fur2gb"))

SRC = ROOT / "music" / "Furnace"
OUT = ROOT / "src" / "furnace"
HDR = ROOT / "include"
CACHE = ROOT / ".cache" / "furnace"
FUR_CODE_BANK = 25            # fur_play etc., the song table, the beat timelines
FIRST_SONG_BANK = 130         # free banks 130..199 (romusage)
LAST_SONG_BANK = 199
# The player's state goes to HRAM (WRAM is full: the stack needs every byte left), so it is cut
# down: call depth 1 (songs grow ~25%) and at most 4 chunks of 16 KB per song.
FRAMES = 1
CHUNKS = 4
CHUNK_SIZE = 16300            # leave room for GBDK's BANKREF stubs in each bank

LEVEL_ALIASES = {"ultimate": "ultiatedestruction", "ultimatedestruction": "ultiatedestruction",
                 "toe": "theoryofeverything"}
MENU_NAMES = {"menuloop", "menu", "menutheme"}
# Order of the songs (their ids, so the music test list): Geometry Dash's own order, then the Pocket
# Dash ones; songs not listed come last in file order
SONG_ORDER = ["menuloop", "stereomadness", "backontrack", "polargeist", "dryout", "baseafterbase",
              "cantletgo", "jumper", "timemachine", "cycles", "xstep", "clutterfunk", "theoryofeverything",
              "electromanadventures", "electrodynamix", "toe2", "deadlocked",
              "ultiatedestruction", "schemingweasel", "stayinsideme"]
NAME_WIDTH = 18               # music test name field
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


fur2gb = None   # imported by convert() (the fur2gb repo may be missing)


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def level_of(stem, levels):
    """Index of the level a .fur file name belongs to, or None"""
    keys = {norm(lv): i for i, lv in enumerate(levels)}
    n = norm(stem)
    for cand in (n, re.sub(r"\d+$", "", n), n.replace("wip", "")):
        cand = LEVEL_ALIASES.get(cand, cand)
        if cand in keys:
            return keys[cand]
    return None


def screen_name(name):
    """Upper case in the music test font, shortened to NAME_WIDTH (vowels go from the end of the
    longest word first: THEORY OF EVERYTHING -> THEORY OF EVRYTHNG)"""
    name = re.sub(r"[^A-Z0-9 %/:\-]", "", name.upper().replace("_", " "))
    words = name.split()
    while len(" ".join(words)) > NAME_WIDTH:
        for i in sorted(range(len(words)), key=lambda i: -len(words[i])):
            w = words[i]
            j = max((k for k in range(1, len(w)) if w[k] in "AEIOU"), default=None)
            if j is not None:
                words[i] = w[:j] + w[j + 1:]
                break
        else:
            break
    return " ".join(words)[:NAME_WIDTH].rstrip()


def module_name(fur):
    """The song name stored in the module (Furnace's text export), or ''"""
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory(prefix="pd_fur_") as tmp:
        txt = Path(tmp) / "song.txt"
        subprocess.run([str(fur2gb.furtrace.DEFAULT_FURNACE), "-noreport", "-loglevel", "error",
                        "-txtout", str(txt), str(Path(fur).resolve())], capture_output=True)
        if not txt.exists():
            return ""
        for line in txt.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("- name:"):
                return line[7:].strip()
    return ""


def tool_hash():
    """What a converted song depends on besides the .fur: fur2gb, its Furnace, our settings"""
    h = hashlib.sha256(repr((FRAMES, CHUNKS, CHUNK_SIZE, 1)).encode())
    for f in sorted((FUR2GB / "tools").glob("*.py")) + sorted((FUR2GB / "driver").glob("*")):
        h.update(f.name.encode() + f.read_bytes())
    st = fur2gb.furtrace.DEFAULT_FURNACE.stat()
    h.update(f"{st.st_size} {st.st_mtime_ns}".encode())
    return h.hexdigest()[:16]


def convert_song(fur, tools):
    """The converted song, cached: dict(song, beats, name, approx), or dict(skip=reason)"""
    cache = CACHE / f"{hashlib.sha256(fur.read_bytes()).hexdigest()[:24]}_{tools}.pkl"
    if cache.exists():
        try:
            return pickle.loads(cache.read_bytes())
        except Exception:
            pass
    print(f"  {fur.name}: new or changed, converting (tracing it in Furnace can take a few minutes)...",
          flush=True)
    for approx in (False, True):
        try:
            trace, song = fur2gb.compile_song(fur, 4, approx_loop=approx)
        except SystemExit as e:
            msg = str(e)
            if not approx and "--approx-loop" in msg:
                continue   # doesn't repeat exactly: first pass exact, then jump back
            if "not a Game Boy song" in msg or "Game Boy Advance" in msg:
                res = dict(skip=msg)
                break
            return dict(skip=msg)   # not cached: tried again next time
        res = dict(song=song, beats=beats(trace, song), name=module_name(fur), approx=approx)
        break
    CACHE.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(pickle.dumps(res))
    return res


def convert(levels=None):
    """Convert music/Furnace for these levels (TMX stems in game order; default: the levels
    tools/build_levels.py finds)"""
    global fur2gb
    import build_levels
    if levels is None:
        levels = [p.stem.lower() for p in build_levels.level_files()]
    if not (FUR2GB / "tools" / "fur2gb.py").exists():
        print(f"  fur2gb not found at {FUR2GB} (set FUR2GB_DIR): keeping the existing Furnace songs")
        return
    sys.path.insert(0, str(FUR2GB / "tools"))
    import fur2gb as f2g
    fur2gb = f2g
    exe = fur2gb.furtrace.DEFAULT_FURNACE
    if not exe.exists():
        print(f"  Furnace build not found at {exe} (set FUR2GB_FURNACE): keeping the existing Furnace songs")
        return
    fur2gb.CHUNK_SIZE = CHUNK_SIZE
    fur2gb.MAX_DEPTH = FRAMES
    fur2gb.MAX_CHUNKS = CHUNKS
    tools = tool_hash()

    src = next((d for d in (ROOT / "music").glob("*") if d.is_dir() and d.name.lower() == "furnace"), SRC)
    furs = sorted(src.glob("*.fur"), key=lambda p: p.name.lower()) if src.exists() else []

    menu, by_level, extras = None, {}, []
    for fur in furs:
        conv = convert_song(fur, tools)
        if "skip" in conv:
            print(f"  ! {fur.name} skipped: {conv['skip']}")
            continue
        lvl = level_of(fur.stem, levels)
        if norm(fur.stem) in MENU_NAMES and not menu:
            menu = (fur, conv)
        elif lvl is not None and lvl not in by_level:
            by_level[lvl] = (fur, conv)
        else:
            if lvl is not None:
                print(f"  ! {fur.name}: {by_level[lvl][0].name} is already {levels[lvl]}'s song, "
                      f"this one is only in the music test")
            extras.append((fur, conv))

    # song order (= ids, music test order): menu theme, level songs in level order, the others
    used = {"menuloop"}

    def ident_for(base):
        base = norm(base) or "song"
        if base[0].isdigit():
            base = "s" + base
        n, i = base, 2
        while n in used:
            n, i = f"{base}{i}", i + 1
        used.add(n)
        return n

    songs = []   # (ident, name, level index or None, use, fur, conv)
    if menu:
        songs.append(("menuloop", "MENU THEME", None, "menu theme") + menu)
    for lvl in sorted(by_level):
        stem = levels[lvl]
        use = f"level {lvl + 1}"
        songs.append((ident_for(stem), screen_name(build_levels.level_title(stem)), lvl, use) + by_level[lvl])
    for fur, conv in extras:
        # the file name, or the song name in the module when the file name is its initials
        # (ToE.fur: Theory of Everything)
        initials = "".join(w[0] for w in re.findall(r"[a-z0-9]+", conv["name"].lower()))
        name = conv["name"] if initials and initials == norm(fur.stem) else fur.stem
        songs.append((ident_for(fur.stem), screen_name(name), None, "music test only", fur, conv))

    songs.sort(key=lambda sg: SONG_ORDER.index(sg[0]) if sg[0] in SONG_ORDER else len(SONG_ORDER))   # stable

    copy_player()
    for f in OUT.glob("fs_*.c"):
        f.unlink()
    banks = Banks()
    table, beat_arrays = [], []
    for ident, name, level, use, fur, conv in songs:
        song = conv["song"]
        sym = f"fs_{ident}"
        files = fur2gb.emit_c(OUT, sym, song, bank_for=banks)
        (OUT / f"{sym}.h").unlink()
        bank = int(files[-2].read_text().split("\n")[1].split()[-1])
        hits, end, loop = conv["beats"]
        # the rods keep pulsing to the tempo where the drums are silent (make_music_beats.py)
        data, loop_ofs = timeline(make_music_beats.add_fake_beats(hits, end, song["hz"]), end, loop)
        beat_arrays.append((ident, data, loop_ofs))
        table.append((ident, sym, bank))
        print(f"  {fur.name} -> {name}: {use}; {song['bytes'] + song['wave_bytes']} B"
              + (", approximate loop (intro and first pass exact)" if conv["approx"] else ""))
    write_sources(songs, table, beat_arrays, levels, banks)
    used_banks = sorted(b for b, f in banks.free.items() if f < 16384)
    print(f"  {len(songs)} Furnace song(s) in banks {used_banks[0]}..{used_banks[-1]}" if used_banks
          else "  no Furnace songs")


def write_sources(songs, table, beat_arrays, levels, banks):
    # song table (bank FUR_CODE_BANK); a dummy entry when there are no songs (C has no empty
    # arrays)
    c = [f"#pragma bank {FUR_CODE_BANK}",
         "// Generated by tools/convert_furnace.py from music/Furnace. Do not edit.",
         "#include <gb/gb.h>", '#include "furplayer.h"', '#include "furnace_songs.h"', ""]
    for ident, sym, bank in table:
        c += [f"BANKREF_EXTERN({sym})", f"extern const fur_song_t {sym};"]
    c += ["", "const FurSong fur_songs[] = {"]
    c += [f"    {{ &{sym}, BANK({sym}) }}," for ident, sym, bank in table] or ["    { 0, 0 },"]
    c += ["};", ""]
    (OUT / "furnace_songs.c").write_text("\n".join(c))

    # the beat timelines, in song banks (they don't fit bank FUR_CODE_BANK): grouped into banks,
    # each group with its own start / poll functions; fur_beats_* (bank FUR_CODE_BANK) dispatch
    for f in OUT.glob("furnace_beats*.c"):
        f.unlink()
    groups, cur, cur_size = [], [], 0
    for i, (ident, data, loop_ofs) in enumerate(beat_arrays):
        n = len(data) + 4
        if cur and cur_size + n > CHUNK_SIZE - 600:
            groups.append(cur)
            cur, cur_size = [], 0
        cur.append(i)
        cur_size += n
    if cur:
        groups.append(cur)
    where = {}
    for g, members in enumerate(groups):
        size = sum(len(beat_arrays[i][1]) + 4 for i in members) + 400
        c = [f"#pragma bank {banks(size)}",
             "// Generated by tools/convert_furnace.py from music/Furnace. Do not edit.",
             "#include <gb/gb.h>", '#include "furnace_songs.h"', "",
             "// (delta ticks, code): 1 weak hit, 2 strong hit, 0 none (a gap over 255 ticks or the",
             "// loop start), 254 the song ends, 255 it loops (to the loop entry)"]
        for k, i in enumerate(members):
            ident, data, loop_ofs = beat_arrays[i]
            where[i] = (g, k)
            c.append(f"static const uint8_t fbeats_{ident}[] = {{")
            for o in range(0, len(data), 24):
                c.append("    " + ", ".join(str(b) for b in data[o:o + 24]) + ",")
            c.append("};")
        c += ["", "static const uint8_t * const fbeats[][2] = {"]
        for i in members:
            ident, data, loop_ofs = beat_arrays[i]
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
              f"void fur_beats_start_{g}(uint8_t k) BANKED {{",
              "    bp = fbeats[k][0];",
              "    loop_p = fbeats[k][1];",
              "    next_t = bp[0];",
              "}",
              "",
              "// Same as music_beats_poll (music_beats.c) for the Furnace song playing",
              f"uint8_t fur_beats_poll_{g}(void) BANKED {{",
              "    if (!bp) return 0;",
              "    uint16_t t;",
              "    do { t = music_ticks; } while (t != music_ticks);   // set by the timer interrupt",
              "    uint8_t hit = 0;",
              "    while ((int16_t)(t - next_t) > 0) {",
              "        uint8_t c = bp[1];",
              "        if (c == 254) { bp = 0; break; }",
              "        if (c == 255) bp = loop_p;",
              "        else {",
              "            if (c == 3 ? !hit : (c > hit || hit == 3)) hit = c;   // 3: a fake beat, only alone",
              "            bp += 2;",
              "        }",
              "        next_t += bp[0];",
              "    }",
              "    return hit;",
              "}", ""]
        (OUT / f"furnace_beats_{g}.c").write_text("\n".join(c))

    # dispatch (bank FUR_CODE_BANK)
    c = ["", "// beat timelines: group (bank file furnace_beats_<group>.c) and index of each song"]
    for g in range(len(groups)):
        c += [f"void fur_beats_start_{g}(uint8_t k) BANKED;", f"uint8_t fur_beats_poll_{g}(void) BANKED;"]
    gi = [where[i] for i in range(len(beat_arrays))] or [(0, 0)]
    c += ["static const uint8_t fbeat_group[] = { " + ", ".join(str(x[0]) for x in gi) + " };",
          "static const uint8_t fbeat_index[] = { " + ", ".join(str(x[1]) for x in gi) + " };",
          "extern const uint8_t *fur_bp;",
          "extern uint8_t fur_bgroup;   // music_select.c: the group of the song playing, 0xFF none",
          "",
          "void fur_beats_start(uint8_t id) BANKED {",
          "    fur_bp = 0;",
          "    fur_bgroup = 0xFF;",
          "    if (id >= FUR_NUM_SONGS) return;",
          "    fur_bgroup = fbeat_group[id];",
          "    switch (fur_bgroup) {"]
    c += [f"        case {g}: fur_beats_start_{g}(fbeat_index[id]); break;" for g in range(len(groups))]
    c += ["    }", "}", "",
          "uint8_t fur_beats_poll(void) BANKED {",
          "    switch (fur_bgroup) {"]
    c += [f"        case {g}: return fur_beats_poll_{g}();" for g in range(len(groups))]
    c += ["    }", "    return 0;", "}", ""]
    with open(OUT / "furnace_songs.c", "a") as f:
        f.write("\n".join(c))

    lv = ["FUR_NONE"] * len(levels)
    for ident, name, level, use, fur, conv in songs:
        if level is not None:
            lv[level] = f"FUR_{ident.upper()}"
    # levels that play another level's song (tools/build_levels.py "song")
    import build_levels
    for i, stem in enumerate(levels):
        of = build_levels.KNOWN_LEVELS.get(stem, {}).get("song")
        if of in levels:
            lv[i] = lv[levels.index(of)]
    h = ["// Generated by tools/convert_furnace.py from music/Furnace. Do not edit.",
         "#ifndef FURNACE_SONGS_H", "#define FURNACE_SONGS_H", "", "#include <gb/gb.h>",
         '#include "furplayer.h"', "", f"#define FUR_CODE_BANK {FUR_CODE_BANK}",
         f"#define FUR_NUM_SONGS {len(songs)}", "#define FUR_NONE 0xFF"]
    h += [f"#define FUR_{s[0].upper()} {i}" for i, s in enumerate(songs)]
    if not any(s[0] == "menuloop" for s in songs):
        h.append("#define FUR_MENULOOP FUR_NONE   // no MenuLoop.fur: always the hUGE menu theme")
    # The tick where the song first wraps (end of its last pattern; the stream goes on for one more
    # pass after it, to the end of the loop): a level song stops there, like the hUGETracker ones.
    # 0 = loops (the menu theme) or ends by itself
    ends = [0 if (s[0] == "menuloop" or s[5]["beats"][2] is None) else s[5]["beats"][2] for s in songs]
    h += ["", "// ticks after which a song stops (0: it loops, or ends by itself)",
          "#define FUR_SONG_ENDS { " + (", ".join(str(e) for e in ends) or "0") + " }"]
    h += ["", "// music test names",
          "#define FUR_SONG_NAMES { " + (", ".join(f'"{s[1]}"' for s in songs) or '""') + " }",
          "// Furnace song of each level (FUR_NONE: the hUGETracker song)",
          "#define FUR_LEVEL_SONGS { " + (", ".join(lv) or "FUR_NONE") + " }", "",
          "typedef struct { const fur_song_t *song; uint8_t bank; } FurSong;",
          "extern const FurSong fur_songs[];",
          "void fur_beats_start(uint8_t id) BANKED;   // FUR_NONE: no timeline",
          "uint8_t fur_beats_poll(void) BANKED;", "", "#endif", ""]
    (HDR / "furnace_songs.h").write_text("\n".join(h))


if __name__ == "__main__":
    convert()


