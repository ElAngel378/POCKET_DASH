"""Fails the build when a ROM bank holds more than 16 KB (bank 0: its code up to _GSFINAL).

    python tools/check_banks.py bin/POCKETDASH.map

The linker does not stop a bank's code/data from running past 0x7FFF: the rest is placed at
0x8000 and up, which is VRAM on the Game Boy, so a call into it jumps into tile data (this
happened to bank 14, mt_renderer.c + famidash_bg.c: the level end crashed).
"""
import re
import sys


def main():
    text = open(sys.argv[1], encoding="latin1").read()
    bad = []
    for name, start, size in re.findall(r"^(_CODE_\d+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})", text, re.M):
        if int(size, 16) > 0x4000:
            bad.append("%s: %d bytes (%d over 16384)" % (name, int(size, 16), int(size, 16) - 0x4000))
    # bank 0: everything up to _GSFINAL (the boot code runs through _GSINIT into it) below 0x4000
    for name, start, size in re.findall(r"^(_HOME|_INITIALIZER|_GSINIT|_GSFINAL)\s+([0-9A-F]{8})\s+([0-9A-F]{8})", text, re.M):
        end = int(start, 16) + int(size, 16)
        if end > 0x4000:
            bad.append("bank 0 %s ends at 0x%04X (%d over 16384)" % (name, end, end - 0x4000))
    if bad:
        print("ROM bank overflow:\n  " + "\n  ".join(bad))
        sys.exit(1)


if __name__ == "__main__":
    main()
