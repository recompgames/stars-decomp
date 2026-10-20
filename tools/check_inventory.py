#!/usr/bin/env python3
"""Validate docs/functions.csv against the NE image.

usage: check_inventory.py [--nedump build/nedump] [--exe original/stars26i.exe] docs/functions.csv

Checks: every code segment has a function; every NE export appears under its name at its entry
address; a function starts at the entry point; status/origin/ret/fp values are legal; no
duplicates; no overlap inside a segment; off + size stays inside the segment; every callers/callees
entry names an existing row (referential integrity); functions listed in MIN_SIZES are at least that
big (guards the NOP-padding size fix). Exit 0 = pass.
Segment lengths and exports come from `nedump` (so the CSV is checked against the real file).
"""
import csv
import re
import subprocess
import sys
from pathlib import Path

HEADER = ["seg", "off", "name", "size", "ret", "callers", "callees", "apis", "fp", "origin", "status", "notes"]
STATUS = {"todo", "decompiled", "tested", "unused"}
ORIGIN = {"game", "crt"}
RET = {"near", "far", "mixed", "none"}  # "none": no RET instruction (never returns, or tail-jumps out)
ENTRY = (34, 0x001A)
# Known lower bounds for `size` of specific rows, checked only when the row exists.
# PLANETWNDPROC at 9:0x0000 runs to 9:0x07df; a size of 67 means the NOP-padding gap was not bridged.
MIN_SIZES = {(9, 0x0000): 0x7E0}
REF = re.compile(r"\d+:0x[0-9a-f]{4,}")


class InventoryError(Exception):
    pass


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.reader(f)
        head = next(rd, None)
        if head != HEADER:
            raise InventoryError(f"{path}: header is {head}, expected {HEADER}")
        rows = []
        for n, r in enumerate(rd, start=2):
            if len(r) != len(HEADER):
                raise InventoryError(f"{path}:{n}: {len(r)} columns, expected {len(HEADER)}")
            rows.append(dict(zip(HEADER, r)))
        return rows


def parse_segments(text):
    """Code segment sizes from `nedump --segments`: {number: max(file length, min_alloc)}.

    The loader zero-fills a segment up to min_alloc, and an instruction may end in that fill (segment 32:
    length 30065, min_alloc 30066, ends in RETF 0x0a whose last byte is the fill), so the memory size is
    the bound for off + size.
    """
    out = {}
    for m in re.finditer(r"^\s*(\d+)\s+CODE\s+.*?length=\s*(\d+)(?:\s+min_alloc=\s*(\d+))?", text, re.M):
        out[int(m.group(1))] = max(int(m.group(2)), int(m.group(3) or 0))
    return out


def parse_exports(text):
    """[(name, seg, off)] from `nedump --exports`."""
    return [(m.group(4), int(m.group(2)), int(m.group(3), 16))
            for m in re.finditer(r"^\s*(\d+)\s+(\d+):0x([0-9a-fA-F]+)\s+(\S+)\s*$", text, re.M)]


def check(rows, segments, exports, entry=ENTRY, min_sizes=MIN_SIZES):
    """Return a list of error strings (empty = pass)."""
    errs = []
    addr = {}
    parsed = []
    for i, r in enumerate(rows, start=2):
        where = f"row {i} ({r.get('name')})"
        try:
            seg = int(r["seg"])
            if not re.fullmatch(r"0x[0-9a-f]{4,}", r["off"]):
                raise ValueError("off must be lower-case hex like 0x00a0")
            off = int(r["off"], 16)
            size = int(r["size"])
        except ValueError as e:
            errs.append(f"{where}: bad numeric field: {e}")
            continue
        if seg not in segments:
            errs.append(f"{where}: segment {seg} is not a code segment")
        if size <= 0:
            errs.append(f"{where}: size {size} must be positive")
        if r["status"] not in STATUS:
            errs.append(f"{where}: status '{r['status']}' not in {sorted(STATUS)}")
        if r["origin"] not in ORIGIN:
            errs.append(f"{where}: origin '{r['origin']}' not in {sorted(ORIGIN)}")
        if r["ret"] not in RET:
            errs.append(f"{where}: ret '{r['ret']}' not in {sorted(RET)}")
        if r["fp"] not in ("0", "1"):
            errs.append(f"{where}: fp '{r['fp']}' must be 0 or 1")
        if (seg, off) in addr:
            errs.append(f"{where}: duplicate address {seg}:0x{off:04x}")
        addr[(seg, off)] = r["name"]
        if seg in segments and off + size > segments[seg]:
            errs.append(f"{where}: {seg}:0x{off:04x}+{size} extends beyond segment length {segments[seg]}")
        parsed.append((seg, off, size, r["name"]))
    for seg in sorted(segments):
        if not any(p[0] == seg for p in parsed):
            errs.append(f"segment {seg} has no function")
    for name, seg, off in exports:
        got = addr.get((seg, off))
        if got != name:
            errs.append(f"export {name} expected at {seg}:0x{off:04x}, found {got!r}")
    if entry not in addr:
        errs.append(f"no function at entry point {entry[0]}:0x{entry[1]:04x}")
    for (seg, off), want in sorted(min_sizes.items()):
        for p in parsed:
            if (p[0], p[1]) == (seg, off) and p[2] < want:
                errs.append(f"{p[3]} {seg}:0x{off:04x} has size {p[2]}, expected at least {want} (NOP-padding gap not bridged?)")
    known = {f"{seg}:0x{off:04x}" for seg, off in addr}
    for i, r in enumerate(rows, start=2):
        for col in ("callers", "callees"):
            for ref in r[col].split():
                if not REF.fullmatch(ref):
                    errs.append(f"row {i} ({r['name']}): malformed {col} entry '{ref}'")
                elif ref not in known:
                    errs.append(f"row {i} ({r['name']}): {col} entry {ref} has no row")
    prev = None
    for seg, off, size, name in sorted(parsed):
        if prev and prev[0] == seg and prev[1] + prev[2] > off:
            errs.append(f"overlap in segment {seg}: {prev[3]} (0x{prev[1]:04x}+{prev[2]}) and {name} (0x{off:04x})")
        prev = (seg, off, size, name)
    return errs


def main(argv):
    args = argv[1:]
    nedump, exe, csv_path = "build/nedump", "original/stars26i.exe", None
    while args:
        a = args.pop(0)
        if a == "--nedump":
            nedump = args.pop(0)
        elif a == "--exe":
            exe = args.pop(0)
        else:
            csv_path = a
    if not csv_path:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        rows = read_rows(csv_path)
        segments = parse_segments(subprocess.check_output([nedump, "--segments", exe], text=True))
        exports = parse_exports(subprocess.check_output([nedump, "--exports", exe], text=True))
    except (InventoryError, OSError, subprocess.CalledProcessError) as e:
        print(f"check_inventory: {e}", file=sys.stderr)
        return 2
    errs = check(rows, segments, exports)
    for e in errs:
        print("ERROR:", e)
    print(f"{len(rows)} functions, {len(segments)} code segments, {len(exports)} exports, {len(errs)} errors")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
