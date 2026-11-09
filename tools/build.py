"""The easy way to build Pocket Dash: build.bat / build.sh run this.

    build.bat                build the game (bin/POCKETDASH.gb)
    build.bat mylevel        build it and start the level straight away in your emulator
    build.bat --clean        start from nothing

It finds or downloads GBDK into .tools/, converts the levels when anything changed, compiles only
the files that need it (by content, so a stale file can't sneak into the ROM), links, and explains
what went wrong in plain words. The full output of a failed step goes to build_error.log.
The makefile still works, and does the same compile: this reads its flags.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / ".tools"
TEMP = ROOT / "temp"
BIN = ROOT / "bin"
STATE = TEMP / "build_state.json"
LOG = ROOT / "build_error.log"
GBDK_VERSION = "4.5.0"
GBDK_URL = "https://github.com/gbdk-2020/gbdk-2020/releases/download/%s/%%s" % GBDK_VERSION
INCLUDE_DIRS = ["include", "src/graphics", "src", "."]


def say(text=""):
    print(text, flush=True)


class Failed(Exception):
    """A problem to show the user, already in plain words"""


# ------------------------------------------------------------------ errors in plain words

EXPLAIN = [
    (r"(\w+): no metatile id left \(cell (\d+), (\d+)\)",
     lambda m: "Level '%s' uses too many different combinations of objects (a level can have about 200).\n"
               "It ran out around column %s. Try using the same blocks more often, or stacking fewer "
               "different objects on the same spot." % (m.group(1), m.group(2))),
    (r"layer (.+), cell (\d+), (\d+): tile (\d+) is not an object",
     lambda m: "Layer '%s' has a tile at column %s, row %s that isn't one of the game's objects.\n"
               "Only use the 'gd_objects' tileset on tile layers, and the 'sprites' tileset on the SP layer."
               % (m.group(1), m.group(2), m.group(3))),
    (r"layer (.+): offset (-?\d+), (-?\d+) is not a multiple of 8",
     lambda m: "Layer '%s' has an offset of %s, %s. Layer offsets must be multiples of 8 px." % m.groups()),
    (r"Error: (.+): unknown Fade '(.+)' at x (\d+)",
     lambda m: "A colour trigger at x %s has the Fade value '%s', which doesn't exist.\n"
               "Use one of: 0.3 s, instant, 0.5 s, 1 s, 2 s, 4 s, 10 s." % (m.group(3), m.group(2))),
    (r"Error: (.+) does not use objects/sprites\.tsx",
     lambda m: "%s doesn't use the sprites tileset. Start a level with new_level instead of an empty map." % m.group(1)),
    (r"xml\.etree\.ElementTree\.ParseError|not well-formed",
     lambda m: "A level file is damaged and can't be read. Save it again in Tiled, or restore it from git."),
    (r"Level '(\w+)': there's no song called '(.+?)'",
     lambda m: "Level '%s' uses the song '%s', which doesn't exist. Pick one from the Song property in Tiled "
               "(Map > Map Properties)." % m.groups()),
    (r"Level file name '(.+?)' can't end",
     lambda m: "The file name '%s' can't end in _ and a number. Rename it in levels/chr_data/tmx, like my_level2." % m.group(1)),
    (r"bank 0 \S+ ends at",
     lambda m: "There are too many levels in one ROM (each one takes a little of a small shared space).\n"
               "Remove a level from levels/chr_data/tmx to make room."),
    (r"ROM bank overflow:\s*(.*)",
     lambda m: "The game no longer fits in the ROM (%s).\nThat comes from the code or a very big level: "
               "ask on the Discord and paste build_error.log." % m.group(1)),
    (r"no free bank|consecutive free ROM banks",
     lambda m: "The ROM is full, there's no room left for this many levels (long levels take the most).\n"
               "Remove a level from levels/chr_data/tmx, or make yours shorter."),
    (r"PIL|Pillow",
     lambda m: "Pillow (an image library for Python) is missing. Run build.bat or build.sh again to install it."),
]


def explain(output):
    for pattern, message in EXPLAIN:
        m = re.search(pattern, output)
        if m:
            return message(m)
    return None


def fail_with(step, output):
    LOG.write_text(output, encoding="utf-8", errors="replace")
    known = explain(output)
    if known:
        raise Failed("%s\n\n(%s failed. Full details: build_error.log)" % (known, step))
    tail = "\n".join(l for l in output.splitlines()[-12:] if l.strip())
    raise Failed("%s failed. The last lines were:\n\n%s\n\nIf that doesn't help, paste build_error.log on the Discord." % (step, tail))


# ------------------------------------------------------------------ GBDK

def gbdk_asset():
    system, machine = platform.system(), platform.machine().lower()
    if system == "Windows":
        return "gbdk-win64.zip"
    if system == "Darwin":
        return "gbdk-macos-arm64.tar.gz" if machine in ("arm64", "aarch64") else "gbdk-macos.tar.gz"
    return "gbdk-linux-arm64.tar.gz" if machine in ("arm64", "aarch64") else "gbdk-linux64.tar.gz"


def lcc_in(folder):
    return Path(folder) / "bin" / ("lcc.exe" if os.name == "nt" else "lcc")


def without_spaces(path):
    """GBDK's compiler can't run from a path with a space: Windows has a short name for most"""
    if " " not in str(path):
        return path
    if os.name == "nt":
        import ctypes
        buf = ctypes.create_unicode_buffer(1024)
        if ctypes.windll.kernel32.GetShortPathNameW(str(path), buf, 1024) and " " not in buf.value:
            return Path(buf.value)
    raise Failed("The Game Boy compiler doesn't work in a folder with a space in its name:\n  %s\n\n"
                 "Move the project somewhere without spaces, like C:\\GBDASH, and run this again." % path.parent.parent)


def find_gbdk():
    choices = [os.environ.get("GBDK"), TOOLS / "gbdk", "C:/gbdk", "/opt/gbdk"]
    for c in choices:
        if c and lcc_in(c).exists():
            return Path(c)
    name = gbdk_asset()
    say("Downloading the Game Boy compiler (GBDK %s). This only happens once..." % GBDK_VERSION)
    TOOLS.mkdir(exist_ok=True)
    archive = TOOLS / name
    try:
        urllib.request.urlretrieve(GBDK_URL % name, archive)
    except OSError as e:
        raise Failed("Couldn't download GBDK (%s). Check your internet connection and try again." % e)
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            z.extractall(TOOLS)
    else:
        with tarfile.open(archive) as t:
            t.extractall(TOOLS)
    archive.unlink()
    if not lcc_in(TOOLS / "gbdk").exists():
        raise Failed("GBDK downloaded, but lcc wasn't where it should be. Delete the .tools folder and try again.")
    if os.name != "nt":
        for f in (TOOLS / "gbdk" / "bin").iterdir():
            f.chmod(f.stat().st_mode | 0o111)
    return TOOLS / "gbdk"


# ------------------------------------------------------------------ what to compile

def hash_bytes(*parts):
    h = hashlib.sha1()
    for p in parts:
        h.update(p)
    return h.hexdigest()


class Deps:
    """What a source file really depends on: everything it #includes and INCBINs"""
    INCLUDE = re.compile(rb'^\s*#\s*include\s+"([^"]+)"', re.M)
    ASM_INCLUDE = re.compile(rb'^\s*\.include\s+"([^"]+)"', re.M)
    INCBIN = re.compile(rb'(?:INCBIN\s*\(\s*\w+\s*,\s*|\.incbin\s+)"([^"]+)"')

    def __init__(self):
        self.memo = {}

    def resolve(self, name, here):
        for base in [here] + [ROOT / d for d in INCLUDE_DIRS]:
            candidate = base / name
            if candidate.is_file():
                return candidate.resolve()
        return None

    def of(self, path, seen):
        """Adds path and all it pulls in to seen"""
        path = path.resolve()
        if path in seen:
            return
        seen.add(path)
        data = path.read_bytes()
        for pattern in (self.INCLUDE, self.ASM_INCLUDE, self.INCBIN):
            for m in pattern.finditer(data):
                found = self.resolve(m.group(1).decode(), path.parent)
                if found:
                    self.of(found, seen)

    def key(self, path, flags):
        seen = set()
        self.of(path, seen)
        parts = [flags.encode()]
        for f in sorted(seen):
            if f not in self.memo:
                self.memo[f] = hash_bytes(f.read_bytes()).encode()
            parts.append(self.memo[f])
        return hash_bytes(*parts)


def makefile_flags():
    text = (ROOT / "makefile").read_text()
    flags = re.search(r"^LCCFLAGS = (.*)$", text, re.M).group(1)
    flags = flags.replace("$(PROF_FLAGS)", "").replace("$(INCDIR)", "include").split()
    libs = re.search(r"^LIBS = (.*)$", text, re.M).group(1).replace("$(LIBDIR)", "lib").split()
    return flags, libs


def sources():
    src = ROOT / "src"
    files = sorted(src.glob("*.c"), key=lambda p: p.name)
    for d in sorted(p for p in src.iterdir() if p.is_dir()):
        files += sorted(d.glob("*.c"), key=lambda p: p.name)
    for d in sorted(p for p in src.iterdir() if p.is_dir()):
        files += sorted(d.glob("*.s"), key=lambda p: p.name)
    return files


def rel(path):
    return path.relative_to(ROOT).as_posix()


def compile_all(lcc, flags, jobs):
    state = json.loads(STATE.read_text()) if STATE.exists() else {}
    deps = Deps()
    todo, objects = [], []
    version = str(lcc)
    for src in sources():
        obj = TEMP / (src.stem + ".o")
        objects.append(obj)
        key = deps.key(src, version + " ".join(flags))
        if state.get(src.name) != key or not obj.exists():
            todo.append((src, obj, key))
    if not todo:
        say("Code: nothing to compile.")
        return objects, False

    say("Compiling %d file%s (the first time takes a few minutes)..." % (len(todo), "" if len(todo) == 1 else "s"))

    def one(item):
        src, obj, key = item
        r = subprocess.run([str(lcc)] + flags + ["-c", "-o", rel(obj), rel(src)], cwd=ROOT, capture_output=True, text=True, errors="replace")
        return item, r

    done = 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for (src, obj, key), r in pool.map(one, todo):
            if r.returncode:
                STATE.write_text(json.dumps(state))
                fail_with("Compiling %s" % src.name, r.stdout + r.stderr)
            state[src.name] = key
            done += 1
            if done % 25 == 0 or done == len(todo):
                say("  %d / %d" % (done, len(todo)))
    STATE.write_text(json.dumps(state))
    return objects, True


# ------------------------------------------------------------------ levels

def level_inputs():
    """Everything the level conversion reads"""
    gfx = ROOT / "levels" / "chr_data"
    files = sorted((ROOT / "tools").glob("*.py")) + sorted((gfx / "tmx").glob("*.tmx"))
    files += sorted((gfx / "objects" / "art").glob("*.png")) + sorted((gfx / "game_art").glob("*.png"))
    files += sorted((ROOT / "music" / "Furnace").glob("*.fur")) + sorted((ROOT / "music" / "Legacy").glob("*.uge"))
    return hash_bytes(*[f.name.encode() + f.read_bytes() for f in files])


def convert_levels(force):
    TEMP.mkdir(exist_ok=True)
    stamp = TEMP / "levels_converted.txt"
    current = level_inputs()
    if not force and stamp.exists() and stamp.read_text() == current:
        say("Levels: no changes.")
        return
    say("Converting levels (a changed song can take a few minutes)...")
    r = subprocess.run([sys.executable, "tools/build_levels.py"], cwd=ROOT, capture_output=True, text=True, errors="replace")
    if r.returncode:
        fail_with("Converting the levels", r.stdout + r.stderr)
    warnings = sorted(set(re.sub(r"cell \d+, \d+: ", "", l.strip()) for l in r.stdout.splitlines() if "warning" in l.lower()))
    if warnings:
        say("  (%d kinds of warning, details in build_warnings.log)" % len(warnings))
        (ROOT / "build_warnings.log").write_text(r.stdout, encoding="utf-8", errors="replace")
    stamp.write_text(level_inputs())


def link(lcc, flags, libs, objects):
    BIN.mkdir(exist_ok=True)
    rom = BIN / "POCKETDASH.gb"
    if rom.exists():
        rom.unlink()
    say("Linking...")
    r = subprocess.run([str(lcc)] + flags + ["-o", rel(rom)] + [rel(o) for o in objects] + libs, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if r.returncode or not rom.exists():
        fail_with("Linking", r.stdout + r.stderr)
    c = subprocess.run([sys.executable, "tools/check_banks.py", "bin/POCKETDASH.map"], cwd=ROOT, capture_output=True, text=True)
    if c.returncode:
        rom.unlink()
        fail_with("Checking the ROM", c.stdout + c.stderr)
    return rom


# ------------------------------------------------------------------ test a level

def level_index(name):
    sys.path.insert(0, str(ROOT / "tools"))
    import build_levels
    names = [p.stem.lower() for p in build_levels.level_files()]
    if name.lower() not in names:
        raise Failed("There's no level called '%s'. The levels are:\n  %s" % (name, ", ".join(names)))
    return names.index(name.lower())


def test_rom(rom, name):
    """A copy of the ROM that starts straight in the level (main.c boot_level)"""
    noi = (BIN / "POCKETDASH.noi").read_text(errors="replace")
    m = re.search(r"DEF _boot_level (0x[0-9A-Fa-f]+)", noi)
    if m is None or int(m.group(1), 16) >= 0x4000:
        raise Failed("Couldn't find where to set the starting level in the ROM. Try build --clean.")
    data = bytearray(rom.read_bytes())
    data[int(m.group(1), 16)] = level_index(name)
    total = (sum(data) - data[0x14E] - data[0x14F]) & 0xFFFF     # the ROM's header checksum
    data[0x14E], data[0x14F] = total >> 8, total & 0xFF
    out = BIN / "POCKETDASH_test.gb"
    out.write_bytes(data)
    return out


def launch(rom):
    """Opens the ROM with whatever program the computer uses for .gb files"""
    try:
        if os.name == "nt":
            os.startfile(str(rom))
        elif platform.system() == "Darwin":
            subprocess.run(["open", str(rom)], check=True)
        else:
            subprocess.run(["xdg-open", str(rom)], check=True)
        say("Opening it in your emulator...")
    except (OSError, subprocess.CalledProcessError):
        say("I couldn't open it automatically. Open this file in a Game Boy emulator:\n  %s" % rom)


def main():
    parser = argparse.ArgumentParser(description="Build Pocket Dash")
    parser.add_argument("level", nargs="?", help="start this level after building")
    parser.add_argument("--clean", action="store_true", help="throw away temp files first")
    parser.add_argument("--no-run", action="store_true", help="with a level: make the test ROM, don't open it")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    args = parser.parse_args()
    if LOG.exists():
        LOG.unlink()
    try:
        if args.clean and TEMP.exists():
            shutil.rmtree(TEMP)
        TEMP.mkdir(exist_ok=True)
        gbdk = find_gbdk()
        convert_levels(args.clean)
        flags, libs = makefile_flags()
        lcc = without_spaces(lcc_in(gbdk))
        objects, compiled = compile_all(lcc, flags, args.jobs)
        rom = BIN / "POCKETDASH.gb"
        if compiled or not rom.exists():
            rom = link(lcc, flags, libs, objects)
        say()
        say("Done: %s" % rom.relative_to(ROOT))
        if args.level:
            test = test_rom(rom, args.level)
            say("Test ROM, starts in '%s': %s" % (args.level, test.relative_to(ROOT)))
            if not args.no_run:
                launch(test)
    except Failed as e:
        say()
        say("Build failed.")
        say()
        say(str(e))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
