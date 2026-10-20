#!/usr/bin/env python3
"""List the code-segment bytes that no row of docs/functions.csv covers.

usage: segment_gaps.py [--nedump build/nedump] [--exe original/stars26i.exe] [--csv docs/functions.csv]
                       [--check docs/code-segment-data.md]

Prints one line per uncovered range: seg:0xSTART-0xEND BYTES (END inclusive). A row covers
[off, off+size); overlapping rows are merged. Segment lengths come from `nedump --segments`
(max of file length and min_alloc, as in check_inventory.py).
With --check, exit 1 if a range of 16+ bytes is not inside a range listed in the given markdown file
(lines of the form `seg:0xSTART-0xEND`); otherwise exit 0.
"""
import re
import subprocess
import sys

import check_inventory as ci

MIN_DOCUMENTED = 16


def gaps(rows, segments):
    """[(seg, start, end_inclusive)] of bytes in each code segment that no row covers."""
    cover = {s: [] for s in segments}
    for r in rows:
        seg = int(r["seg"])
        if seg in cover:
            off = int(r["off"], 16)
            cover[seg].append((off, off + int(r["size"])))
    out = []
    for seg in sorted(segments):
        pos = 0
        for start, end in sorted(cover[seg]):
            if start > pos:
                out.append((seg, pos, start - 1))
            pos = max(pos, end)
        if pos < segments[seg]:
            out.append((seg, pos, segments[seg] - 1))
    return out


def fmt(g):
    seg, start, end = g
    return f"{seg}:0x{start:04x}-0x{end:04x} {end - start + 1}"


def documented(text):
    """[(seg, start, end_inclusive)] ranges written as seg:0xSTART-0xEND in the markdown text."""
    return [(int(m[1]), int(m[2], 16), int(m[3], 16))
            for m in re.finditer(r"\b(\d+):0x([0-9a-fA-F]+)-0x([0-9a-fA-F]+)\b", text)]


def undocumented(found, docs):
    return [g for g in found
            if g[2] - g[1] + 1 >= MIN_DOCUMENTED
            and not any(d[0] == g[0] and d[1] <= g[1] and g[2] <= d[2] for d in docs)]


def main(argv):
    args = argv[1:]
    nedump, exe, csv_path, check = "build/nedump", "original/stars26i.exe", "docs/functions.csv", None
    while args:
        a = args.pop(0)
        if a == "--nedump":
            nedump = args.pop(0)
        elif a == "--exe":
            exe = args.pop(0)
        elif a == "--csv":
            csv_path = args.pop(0)
        elif a == "--check":
            check = args.pop(0)
        else:
            print(__doc__, file=sys.stderr)
            return 2
    try:
        rows = ci.read_rows(csv_path)
        segments = ci.parse_segments(subprocess.check_output([nedump, "--segments", exe], text=True))
        docs = documented(open(check, encoding="utf-8").read()) if check else []
    except (ci.InventoryError, OSError, subprocess.CalledProcessError) as e:
        print(f"segment_gaps: {e}", file=sys.stderr)
        return 2
    found = gaps(rows, segments)
    for g in found:
        print(fmt(g))
    if check:
        bad = undocumented(found, docs)
        for g in bad:
            print("UNDOCUMENTED:", fmt(g))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
