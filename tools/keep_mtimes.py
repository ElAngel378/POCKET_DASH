"""The build tools rewrite every generated file, and make then recompiles them all. Wrap a tool in
keep_unchanged(): a file that comes out with the same contents gets its old time back, so make only
compiles what really changed.
"""
import hashlib
import os
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATED = ["src", "include", "levels/level_data", "levels/chr_data"]


def snapshot():
    out = {}
    for d in GENERATED:
        for f in (ROOT / d).rglob("*"):
            if f.is_file() and f.suffix in (".c", ".h", ".s", ".bin", ".json"):
                st = f.stat()
                out[f] = (hashlib.md5(f.read_bytes()).digest(), st.st_atime_ns, st.st_mtime_ns)
    return out


@contextmanager
def keep_unchanged():
    before = snapshot()
    try:
        yield
    finally:
        for f, (digest, atime, mtime) in before.items():
            if f.exists() and f.stat().st_mtime_ns != mtime and hashlib.md5(f.read_bytes()).digest() == digest:
                os.utime(f, ns=(atime, mtime))
