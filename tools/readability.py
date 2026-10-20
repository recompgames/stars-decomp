#!/usr/bin/env python3
"""Readability metrics per decompiled function (docs/PLAN.md section 5, docs/CONVENTIONS.md rule k).

usage: readability.py [--src DIR] [--per-function]       default src: src
Default output is a table with one row per segment; --per-function prints CSV
(seg,off,name,raw,placeholders,magic,tagged) with one row per DECOMP function. Always exits 0.

Metrics, counted in code only (comments and string/char literals are stripped first), over the
function from the end of its DECOMP line (so the signature is included) to the closing '}':
  raw           calls of rd8/16/32, wr8/16/32, any drd*/dwr* helper (drd8, dwr32, drd_f64, ...), plus LINN( with a numeric argument
  placeholders  fn_NN_xxxx, FUN_*, DAT_*, g_DAT_*, local_*, param_*, unknown_*, fld_* identifiers
  magic         hex literals other than 0, 1, 0xFF, 0xFFFF; a power of two used as a shift count is exempt
  tagged        1 if the comment before the DECOMP line holds the tag line seg:0xOFF (1:0x00ab)
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lint_src import strip_code, NUM  # noqa: E402

RAW_RE = re.compile(r"\b(?:rd|wr)(?:8|16|32)\s*\(|\b(?:drd|dwr)\w*\s*\(|\bLINN\s*\(\s*(?:0[xX][0-9a-fA-F]+|\d+)\b")
PLACEHOLDER_RE = re.compile(r"\bfn_\d\d_[0-9a-f]{4}\b|\b(?:g_)?(?:DAT_|FUN_)\w*|\b(?:local_|param_|unknown_|fld_)\w*")
HEX_RE = re.compile(r"(?<![\w.])0[xX][0-9a-fA-F]+(?![\w.])")
HEX_OK = {0, 1, 0xFF, 0xFFFF}
DECOMP_AT = re.compile(r"^[ \t]*DECOMP\(\s*(\d+)\s*,\s*" + NUM + r"\s*,\s*(\w+)\s*,[^\n]*", re.M)
SEGFILE_RE = re.compile(r"^seg\d\d\.c$")


class Func:
    def __init__(self, seg, off, name, raw, placeholders, magic, tagged):
        self.seg, self.off, self.name = seg, off, name
        self.raw, self.placeholders, self.magic, self.tagged = raw, placeholders, magic, tagged


def count_magic(body):
    n = 0
    for m in HEX_RE.finditer(body):
        v = int(m.group(), 16)
        if v in HEX_OK:
            continue
        before = body[:m.start()].rstrip()
        if (before.endswith("<<") or before.endswith(">>")) and v & (v - 1) == 0:
            continue                     # a power of two used as a shift count
        n += 1
    return n


def file_metrics(text):
    code = strip_code(text)
    out = []
    for m in DECOMP_AT.finditer(code):
        seg, off, name = int(m.group(1)), int(m.group(2), 0), m.group(3)
        brace = code.find("{", m.end())
        if brace < 0:
            continue
        depth, i = 0, brace
        while i < len(code):
            depth += (code[i] == "{") - (code[i] == "}")
            i += 1
            if depth == 0:
                break
        body = code[m.end():i]
        # the comment block just before the DECOMP line (raw text between the previous '*/' and it)
        head = text[:m.start()].rstrip()
        comment = head[head.rfind("/*"):] if head.endswith("*/") else ""
        tag = re.compile(r"\b%d:0[xX]0*%x\b" % (seg, off), re.I)
        out.append(Func(seg, off, name, len(RAW_RE.findall(body)), len(PLACEHOLDER_RE.findall(body)),
                        count_magic(body), bool(tag.search(comment))))
    return out


def metrics(src):
    """[Func] for every DECOMP function in src/seg*.c, sorted by (seg, off)."""
    funcs = []
    for p in sorted(Path(src).glob("seg*.c")):
        if SEGFILE_RE.match(p.name):
            funcs += file_metrics(p.read_text(encoding="utf-8", errors="replace"))
    return sorted(funcs, key=lambda f: (f.seg, f.off))


def segment_table(funcs):
    """Lines of the per-segment table."""
    rows = {}
    for f in funcs:
        r = rows.setdefault(f.seg, [0, 0, 0, 0, 0])
        r[0] += 1
        r[1] += f.raw
        r[2] += f.placeholders
        r[3] += f.magic
        r[4] += 0 if f.tagged else 1
    lines = [f"{'seg':>4} {'functions':>9} {'raw':>6} {'placeholders':>12} {'magic':>6} {'untagged':>8}"]
    tot = [0] * 5
    for seg in sorted(rows):
        r = rows[seg]
        tot = [a + b for a, b in zip(tot, r)]
        lines.append(f"{seg:>4} {r[0]:>9} {r[1]:>6} {r[2]:>12} {r[3]:>6} {r[4]:>8}")
    lines.append(f"{'all':>4} {tot[0]:>9} {tot[1]:>6} {tot[2]:>12} {tot[3]:>6} {tot[4]:>8}")
    return lines


def main(argv):
    src, per_fn = "src", False
    args = list(argv)
    try:
        while args:
            a = args.pop(0)
            if a == "--src":
                src = args.pop(0)
            elif a == "--per-function":
                per_fn = True
            else:
                print(__doc__, file=sys.stderr)
                return 0
    except IndexError:
        print(__doc__, file=sys.stderr)
        return 0
    funcs = metrics(src)
    if per_fn:
        print("seg,off,name,raw,placeholders,magic,tagged")
        for f in funcs:
            print(f"{f.seg},0x{f.off:04x},{f.name},{f.raw},{f.placeholders},{f.magic},{int(f.tagged)}")
    else:
        print("\n".join(segment_table(funcs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
