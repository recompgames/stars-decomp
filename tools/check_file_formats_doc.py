#!/usr/bin/env python3
"""Check docs/FILE_FORMATS.md against src/stars.h: struct sizes and block type numbers.

Doc syntax checked: "`struct Name` (0xNN bytes)" and table rows "| N | BLK_NAME |".
Usage: check_file_formats_doc.py [doc] [header]; exit 1 and print each mismatch."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def problems(doc_text, hdr_text):
    sizes = dict(re.findall(r"STATIC_SIZE\(struct (\w+), (0x[0-9A-Fa-f]+)\)", hdr_text))
    blks = dict(re.findall(r"\b(BLK_\w+)\s*=\s*(\d+)", hdr_text))
    out = []
    doc_sizes = re.findall(r"`struct (\w+)` \((0x[0-9a-fA-F]+) bytes\)", doc_text)
    doc_blks = re.findall(r"^\|\s*(\d+)\s*\|\s*(BLK_\w+)\s*\|", doc_text, re.M)
    if not doc_sizes or not doc_blks:
        out.append("doc quotes no struct sizes or no block-type rows")
    for name, size in doc_sizes:
        if name not in sizes or int(sizes[name], 16) != int(size, 16):
            out.append(f"struct {name}: doc {size}, stars.h {sizes.get(name)}")
    for num, name in doc_blks:
        if blks.get(name) != num:
            out.append(f"{name}: doc {num}, stars.h {blks.get(name)}")
    return out


if __name__ == "__main__":
    doc = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs/FILE_FORMATS.md"
    hdr = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "src/stars.h"
    errs = problems(doc.read_text(), hdr.read_text())
    print("\n".join(errs) or "ok")
    sys.exit(1 if errs else 0)
