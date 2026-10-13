"""Converts a Famidash song (FamiStudio ca65 export) to a hUGEDriver song C file.

    python tools/famidash_song_to_huge.py MUSIC_FILE SONG_INDEX C_NAME OUT.c [--rows N] [--bank B]

Channel mapping (NES 2A03 -> Game Boy):
    pulse 1  -> duty 1          pulse 2 -> duty 2
    DPCM     -> wave            (Famidash puts bass lines on DPCM; "fdbass X" samples are
                                 converted to real notes from their sample note + rate)
    triangle -> wave (hUGE note = FamiStudio note - 25) where the DPCM bass is silent (plucks), otherwise dropped;
                triangle drums / DPCM hits -> noise where the noise channel is free
    noise    -> noise

Tempo: FamiTracker tempo. hUGE ticks per row = FamiTracker speed (Fxx on speed changes),
tick rate = 60.0988 Hz * step/256, which gives the timer divider (TMA) to use with the
4096 Hz timer (printed; put it in tools/build_levels.py KNOWN_LEVELS).

Instruments: one hUGE instrument per (Famidash instrument, duty) pair. Famidash volume
envelopes become hardware envelopes (start volume + decay pace), the volume column becomes
Cxy, note stops/releases become E00, slides and no-attack notes 3xx, arpeggio envelopes 0xy.
"""
import argparse
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from famistudio_decode import decode  # noqa: E402

NOTE_NAMES = ["C_", "Cs", "D_", "Ds", "E_", "F_", "Fs", "G_", "Gs", "A_", "As", "B_"]
DPCM_RATE_HZ = [4181.71, 4709.93, 5264.04, 5593.04, 6257.95, 7046.35, 7919.35, 8363.42,
                9419.86, 11186.1, 12604.0, 13982.6, 16884.6, 21306.8, 24858.0, 33143.9]
LETTER = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def hnote(h):
    if h is None:
        return "___"
    return "%s%d" % (NOTE_NAMES[h % 12], h // 12 + 3)


def fit(h, lo=0, hi=71):
    while h < lo:
        h += 12
    while h > hi:
        h -= 12
    return h


class Env:
    def __init__(self, song, label):
        self.vals, self.release, self.loop = song.envelopes[label] if label else ([0], 0, None)


def vol_envelope_byte(vals, scale=15):
    """Famidash volume envelope (per 60Hz frame) -> (start volume, GB envelope nibble)."""
    if not vals:
        return 0, 0
    v0 = vals[0] * scale // 15
    if v0 <= 0:
        return 0, 0
    # frames until the envelope has decayed to half / zero
    last = vals[-1]
    if last >= vals[0] or len(vals) < 3:
        return min(15, v0), 0          # sustained
    drop = vals[0] - last
    frames = len(vals)
    for i, v in enumerate(vals):
        if v <= last:
            frames = i + 1
            break
    per_step = frames / max(1, drop)   # frames per volume step
    pace = int(round(per_step / 0.9375))  # GB pace n = n/64 s per step
    pace = max(1, min(7, pace))
    return min(15, v0), pace           # direction 0 = decrease


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("music_file")
    ap.add_argument("song_index", type=int)
    ap.add_argument("c_name")
    ap.add_argument("out")
    ap.add_argument("--rows", type=int, default=0, help="song length (default: up to the first end opcode)")
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument("--bass-octave", type=int, default=0, help="extra octaves for the DPCM bass")
    args = ap.parse_args()

    song = decode(args.music_file, args.song_index)
    text = Path(args.music_file).read_text()
    inst_names = re.findall(r'^\s*\.word @env\d+,@env\d+,@env\d+,@env\d+ ; ([0-9a-f]{2}) : (.*)$', text, re.M)
    samples = re.findall(r'^\s*\.byte [^;]*; ([0-9a-f]{2}) (.*?) \(Pitch:(\d+)\)', text, re.M)

    n_rows = args.rows
    if not n_rows:
        ends = [next((i for i, r in enumerate(c) if r.get('end')), len(c)) for c in song.channels]
        n_rows = min(ends) + 1
    ch = [c[:n_rows] + [{}] * (n_rows - len(c[:n_rows])) for c in song.channels]

    # tempo
    tick_hz = 60.0988 * song.step_ntsc / 256.0
    tma = 256 - int(round(4096.0 / tick_hz))

    # per channel state walk -> GB rows
    out = [[None] * n_rows for _ in range(4)]   # (note, inst_key, effect) per row
    duty_insts, wave_insts, noise_insts = {}, {}, {}

    def inst_key(table, key, maker):
        if key not in table:
            table[key] = (len(table) + 1, maker())
        return table[key][0]

    def famidash_inst(i):
        return song.instruments[i] if i is not None and i < len(song.instruments) else None

    def pulse_channel(src, dst_idx):
        cur_inst, cur_duty, cur_vol, arp = None, None, 15, None
        last_note = None
        for r, row in enumerate(src):
            if 'inst' in row:
                cur_inst = row['inst']
            if 'duty' in row:
                cur_duty = row['duty'] & 3
            if 'vol' in row:
                cur_vol = row['vol']
            if 'arp_env' in row:
                arp = row['arp_env']
            fi = famidash_inst(cur_inst)
            duty = cur_duty
            if duty is None and fi:
                dvals = Env(song, fi[2]).vals
                duty = (dvals[0] & 3) if dvals else 2
            if duty is None:
                duty = 2
            note = row.get('note')
            eff = 0
            if note is None and not row.get('release'):
                if 'vol' in row and last_note is not None:
                    v0, pace = vol_envelope_byte(Env(song, fi[0]).vals if fi else [15], cur_vol)
                    eff = 0xC00 | (v0 << 4) | pace
                    out[dst_idx][r] = (None, 0, eff)
                continue
            if row.get('release') or note == 0:
                out[dst_idx][r] = (None, 0, 0xE00)
                last_note = None
                continue
            h = fit(note - 25)
            vals = Env(song, fi[0]).vals if fi else [15]
            v0, pace = vol_envelope_byte(vals, 15)
            key = (duty, v0, pace)
            gi = inst_key(duty_insts, key, lambda: (duty, v0, pace, inst_names[cur_inst][1] if cur_inst is not None and cur_inst < len(inst_names) else "?"))
            if row.get('no_attack') or 'slide' in row:
                eff = 0x3FF if row.get('no_attack') else (0x300 | max(1, min(0xFF, row['slide'][0] * 2)))
            elif cur_vol != 15:
                v, p2 = vol_envelope_byte(vals, cur_vol)
                eff = 0xC00 | (v << 4) | p2
            else:
                arp_label = arp if arp else (fi[1] if fi else None)
                if arp_label:
                    av = Env(song, arp_label).vals
                    offs = sorted(set(v for v in av[:16] if v != 0))
                    if offs and all(0 < o < 16 for o in offs):
                        x = offs[0]
                        y = offs[1] if len(offs) > 1 else 0
                        eff = (x << 4) | y
            out[dst_idx][r] = (h, gi, eff)
            last_note = h

    pulse_channel(ch[0], 0)
    pulse_channel(ch[1], 1)

    # wave: DPCM bass, triangle where the bass is silent
    dpcm_active = [False] * n_rows
    sounding = False
    drum_hits = {}
    for r, row in enumerate(ch[4]):
        note = row.get('note')
        if note is None:
            dpcm_active[r] = sounding
            continue
        if note == 0:
            sounding = False
            out[2][r] = (None, 0, 0xE00)
            continue
        idx = note - 12
        if not (0 <= idx < len(samples)):
            continue
        name, pitch = samples[idx][1], int(samples[idx][2])
        m = re.match(r'fdbass ([A-G]#?)', name)
        if m:
            midi = 60 + LETTER[m.group(1)] + 12 * math.log2(DPCM_RATE_HZ[pitch] / DPCM_RATE_HZ[15])
            h = fit(int(round(midi)) - 24 + 12 * args.bass_octave)
            gi = inst_key(wave_insts, 'bass', lambda: ('bass',))
            out[2][r] = (h, gi, 0)
            sounding = True
            dpcm_active[r] = True
        else:
            drum_hits[r] = name   # drum / voice sample -> noise if free
    tri_inst = None
    for r, row in enumerate(ch[2]):
        if 'inst' in row:
            tri_inst = row['inst']
        note = row.get('note')
        if note is None:
            continue
        name = inst_names[tri_inst][1] if tri_inst is not None and tri_inst < len(inst_names) else ""
        if "drum" in name:
            if note:
                drum_hits.setdefault(r, "tri kick")
            continue
        if dpcm_active[r] or out[2][r] is not None:
            continue
        if note == 0:
            out[2][r] = (None, 0, 0xE00)
            continue
        gi = inst_key(wave_insts, 'tri', lambda: ('tri',))
        out[2][r] = (fit(note - 25), gi, 0)   # NES triangle: pulse period table, one octave lower

    # noise
    n_inst, n_vol = None, 15
    for r, row in enumerate(ch[3]):
        if 'inst' in row:
            n_inst = row['inst']
        if 'vol' in row:
            n_vol = row['vol']
        note = row.get('note')
        if note is None:
            continue
        if note == 0 or row.get('release'):
            out[3][r] = (None, 0, 0xE00)
            continue
        fi = famidash_inst(n_inst)
        vals = Env(song, fi[0]).vals if fi else [15]
        v0, pace = vol_envelope_byte(vals, n_vol)
        v0 = min((4, 8, 12, 15), key=lambda q: abs(q - v0)) if v0 else 0
        gi = inst_key(noise_insts, (v0, pace), lambda: (v0, pace, inst_names[n_inst][1] if n_inst is not None and n_inst < len(inst_names) else "?"))
        out[3][r] = (fit(10 + (note & 15) * 3), gi, 0)
    for r, name in drum_hits.items():
        if out[3][r] is None:
            gi = inst_key(noise_insts, (15, 2), lambda: (15, 2, 'kick (' + name + ')'))
            out[3][r] = (fit(12), gi, 0)

    # speed changes (Fxx) on a channel without an effect that row
    speed0 = 6
    for r, row in enumerate(ch[1]) if any('speed' in x for x in ch[1]) else enumerate(ch[0]):
        if 'speed' not in row:
            continue
        if r == 0:
            speed0 = row['speed']
            continue
        for c in range(4):
            cell = out[c][r]
            if cell is None or cell[2] == 0:
                out[c][r] = (cell[0] if cell else None, cell[1] if cell else 0, 0xF00 | row['speed'])
                break
    for chn in range(4):
        for r in range(n_rows):
            if out[chn][r] is None:
                out[chn][r] = (None, 0, 0)

    for name, table in (("duty", duty_insts), ("wave", wave_insts), ("noise", noise_insts)):
        if len(table) > 15:
            print("WARNING: %d %s instruments (hUGE max 15), extra ones reuse instrument 15" % (len(table), name))

    # patterns (64 rows), deduplicated
    n_pat = (n_rows + 63) // 64
    patterns, orders = {}, [[] for _ in range(4)]
    for c in range(4):
        for p in range(n_pat):
            rows = []
            for r in range(p * 64, p * 64 + 64):
                if r < n_rows:
                    h, gi, eff = out[c][r]
                else:
                    h, gi, eff = None, 0, 0
                rows.append((hnote(h), min(gi, 15), eff))
            key = tuple(rows)
            if key not in patterns:
                patterns[key] = "P%d" % len(patterns)
            orders[c].append(patterns[key])

    L = []
    if args.bank:
        L.append("#pragma bank %d\n" % args.bank)
    L += ['// Converted from Famidash (%s, song %d) by tools/famidash_song_to_huge.py'
          % (Path(args.music_file).name, args.song_index),
          '// Timer divider (TMA) for this song: %d' % tma,
          '#include "hUGEDriver.h"', '#include <stddef.h>', '',
          'static const unsigned char order_cnt = %d;' % (n_pat * 2), '']
    for key, name in patterns.items():
        L.append("static const unsigned char %s[] = {" % name)
        for n, gi, eff in key:
            L.append("    DN(%s,%d,0x%03X)," % (n, gi, eff))
        L.append("};")
    for c in range(4):
        L.append("static const unsigned char* const order%d[] = {%s};" % (c + 1, ",".join(orders[c])))
    L.append("")
    L.append("static const hUGEDutyInstr_t duty_instruments[] = {")
    for key, (gi, (duty, v0, pace, name)) in sorted(duty_insts.items(), key=lambda kv: kv[1][0])[:15]:
        L.append("    {0,%d,%d,0,128}, // %d: %s duty %d" % (duty << 6, (v0 << 4) | pace, gi, name, duty))
    L.append("};")
    L.append("static const hUGEWaveInstr_t wave_instruments[] = {")
    for key, (gi, (kind,)) in sorted(wave_insts.items(), key=lambda kv: kv[1][0])[:15]:
        L.append("    {0,32,%d,0,128}, // %d: %s" % (0 if kind == 'tri' else 1, gi, "triangle" if kind == 'tri' else "DPCM bass"))
    L.append("};")
    L.append("static const hUGENoiseInstr_t noise_instruments[] = {")
    for key, (gi, (v0, pace, name)) in sorted(noise_insts.items(), key=lambda kv: kv[1][0])[:15]:
        L.append("    {%d,0,0,0,0}, // %d: %s" % ((v0 << 4) | pace, gi, name))
    L.append("};")
    L.append("")
    L.append("static const unsigned char waves[] = {")
    tri = [(i if i < 16 else 31 - i) for i in range(32)]            # NES triangle
    bass = [15 - i // 2 for i in range(32)]                           # saw: punchy DPCM-like bass
    for w in (tri, bass):
        packed = [(w[2 * i] << 4) | w[2 * i + 1] for i in range(16)]
        L.append("    " + ",".join(str(v) for v in packed) + ",")
    L.append("};")
    L.append("")
    if args.bank:
        L.append("const void __at(%d) __bank_%s;" % (args.bank, args.c_name))
    L.append("const hUGESong_t %s = {%d, &order_cnt, order1, order2, order3, order4, "
             "duty_instruments, wave_instruments, noise_instruments, NULL, waves};" % (args.c_name, speed0))
    Path(args.out).write_text("\n".join(L) + "\n", newline="\n")
    print("rows %d, patterns %d unique of %d, tick %.2f Hz -> TMA %d, speed %d, instruments duty %d wave %d noise %d"
          % (n_rows, len(patterns), n_pat * 4, tick_hz, tma, speed0, len(duty_insts), len(wave_insts), len(noise_insts)))


if __name__ == "__main__":
    main()
