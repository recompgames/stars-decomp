#!/usr/bin/env python3
"""Decompilation progress per segment from docs/functions.csv. Always exits 0.

usage: progress.py [--csv FILE] [--seg N] [--readability [--src DIR]]
       default csv: docs/functions.csv, default src: src
Prints one line per segment (or only segment N) and a total line:
  seg 2: total 3, todo 1 (33.3%), decompiled 0 (0.0%), tested 1 (33.3%), unused 1 (33.3%)
Statuses outside todo/decompiled/tested/unused (see docs/CONVENTIONS.md rule i) are appended as
"<status> N (P%)" rather than dropped; the readable status (after tested, rule i) is shown only on
lines that have one. --readability appends the per-segment readability table of tools/readability.py.
"""
import csv
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import readability  # noqa: E402

STATUSES = ("todo", "decompiled", "tested", "unused")


def line(label, counter):
    total = sum(counter.values())
    known = list(STATUSES)
    if counter.get("readable"):
        known.insert(known.index("tested") + 1, "readable")
    keys = known + sorted(k for k in counter if k not in known)
    parts = [f"{k} {counter.get(k, 0)} ({100.0 * counter.get(k, 0) / total if total else 0.0:.1f}%)"
             for k in keys]
    return f"{label}: total {total}, " + ", ".join(parts)


def main(argv):
    path, only, readable, src = "docs/functions.csv", None, False, "src"
    args = list(argv)
    try:
        while args:
            a = args.pop(0)
            if a == "--csv":
                path = args.pop(0)
            elif a == "--readability":
                readable = True
            elif a == "--src":
                src = args.pop(0)
            elif a == "--seg":
                only = int(args.pop(0))
            else:
                print(__doc__, file=sys.stderr)
                return 0
    except (IndexError, ValueError):
        print(__doc__, file=sys.stderr)
        return 0
    try:
        with open(path, newline="", encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
    except OSError as e:
        print(f"progress: cannot read {path}: {e}", file=sys.stderr)
        return 0
    per = {}
    for r in rows:
        try:
            seg = int(r["seg"])
        except (KeyError, ValueError, TypeError):
            continue
        per.setdefault(seg, Counter())[(r.get("status") or "").strip()] += 1
    if only is not None:
        print(line(f"seg {only}", per.get(only, Counter())))
        if readable:
            print("\n".join(readability.segment_table([f for f in readability.metrics(src) if f.seg == only])))
        return 0
    total = Counter()
    for seg in sorted(per):
        print(line(f"seg {seg}", per[seg]))
        total.update(per[seg])
    print(line("total", total))
    if readable:
        print("\n".join(readability.segment_table(readability.metrics(src))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
