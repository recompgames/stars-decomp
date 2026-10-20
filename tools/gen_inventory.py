#!/usr/bin/env python3
"""Regenerate the numbers in docs/inventory.md that come from tools, not from reading.

usage: gen_inventory.py [--check] [--csv docs/functions.csv] [--facts re/facts.txt]
                        [--exe original/stars26i.exe] [--nedump build/nedump] [--doc docs/inventory.md]

The document keeps hand-written prose. Generated text lives between marker lines

    <!-- GEN:name -->
    ...
    <!-- /GEN:name -->

and only that text is rewritten. Blocks: summary, coverage, x87, csoverride, messages.
With --check nothing is written; exit 1 if a block is out of date (so a test can enforce it).

Inputs: docs/functions.csv (ExportInventory.java), re/facts.txt (InventoryFacts.java: COVER, X87, CSOVERRIDE and
MSG records), and the exe (raw byte scans). re/facts.txt is git-ignored; regenerate it with the Ghidra command in
docs/inventory.md. The message names below are written from memory of the Windows 3.1 headers; no header is
available on this machine, so the generated tables label every name as unchecked.
"""
import csv
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---- names from memory (NOT checked against a source) ----
WM_NAMES = {
    0x7: "WM_SETFOCUS", 0x10: "WM_CLOSE", 0x1D: "WM_FONTCHANGE", 0x1F: "WM_CANCELMODE", 0x20: "WM_SETCURSOR",
    0x30: "WM_SETFONT", 0x100: "WM_KEYDOWN", 0x102: "WM_CHAR", 0x111: "WM_COMMAND", 0x202: "WM_LBUTTONUP",
    0x205: "WM_RBUTTONUP",
}
# WM_USER (0x400) + n: control-class messages. Candidate names per class, from memory of Windows 3.1 windows.h.
USER_CANDIDATES = {
    0: "BM_GETCHECK / EM_GETSEL / CB_GETEDITSEL",
    1: "BM_SETCHECK / EM_SETSEL / CB_LIMITTEXT / LB_ADDSTRING",
    3: "BM_SETSTATE / EM_SETRECT / CB_ADDSTRING / LB_DELETESTRING",
    4: "BM_SETSTYLE / EM_SETRECTNP / CB_DELETESTRING",
    5: "CB_DIR / LB_RESETCONTENT (no EDIT message recalled at +5)",
    6: "EM_LINESCROLL / CB_GETCOUNT / LB_SETSEL",
    7: "CB_GETCURSEL / LB_SETCURSEL",
    8: "EM_GETMODIFY / CB_GETLBTEXT / LB_GETSEL",
    9: "EM_SETMODIFY / CB_GETLBTEXTLEN / LB_GETCURSEL",
    10: "EM_GETLINECOUNT / CB_INSERTSTRING / LB_GETTEXT",
    11: "EM_LINEINDEX / CB_RESETCONTENT / LB_GETTEXTLEN",
    14: "EM_GETTHUMB / CB_SETCURSEL / LB_DIR",
    21: "EM_LIMITTEXT / LB_SETHORIZONTALEXTENT",
    24: "EM_FMTLINES / LB_SETTOPINDEX",
}
UNCHECKED = "name from memory, not checked against a source"
X87_OPCODES = {  # two-byte opcodes for the byte scan (D9 xx)
    "F2XM1": 0xF0, "FYL2X": 0xF1, "FPTAN": 0xF2, "FPATAN": 0xF3, "FPREM": 0xF8, "FSQRT": 0xFA,
    "FRNDINT": 0xFC, "FSCALE": 0xFD,
}
X87_SPECIAL = ["FLDCW", "FSTCW", "FNSTCW", "F2XM1", "FYL2X", "FPTAN", "FPATAN", "FSCALE", "FRNDINT", "FSQRT", "FPREM"]


class GenError(Exception):
    pass


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def parse_facts(text):
    """{tag: [fields...]} where each fields list is the '|' split of the record without the tag."""
    out = defaultdict(list)
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.split("|")
        out[parts[0]].append(parts[1:])
    return out


def parse_segments(text):
    """{n: (file offset, length)} for code segments from `nedump --segments`."""
    return {int(m.group(1)): (int(m.group(2), 16), int(m.group(3)))
            for m in re.finditer(r"^\s*(\d+)\s+CODE\s+file=0x([0-9a-f]+)\s+length=\s*(\d+)", text, re.M)}


def raw_x87_scan(segment_bytes):
    """{mnemonic: {seg: count}} of D9 xx byte pairs in each code segment (data tables included)."""
    out = {m: {} for m in X87_OPCODES}
    for seg, b in segment_bytes.items():
        for m, op in X87_OPCODES.items():
            n = sum(1 for i in range(len(b) - 1) if b[i] == 0xD9 and b[i + 1] == op)
            if n:
                out[m][seg] = n
    return out


# ---- block renderers (pure functions of parsed inputs) ----
def seg_accounting(rows, cover, seg=34):
    """Bytes of `seg` in function runs, in RANGE rows and the rest (0x90/0x00 padding between them)."""
    size = next(int(c[1]) for c in cover if int(c[0]) == seg)
    fun = sum(int(r["size"]) for r in rows if int(r["seg"]) == seg and not r["name"].startswith("RANGE_"))
    rng = sum(int(r["size"]) for r in rows if int(r["seg"]) == seg and r["name"].startswith("RANGE_"))
    return size, fun, rng, size - fun - rng


def render_summary(rows, cover):
    funcs = [r for r in rows if not r["name"].startswith("RANGE_")]
    ranges = [r for r in rows if r["name"].startswith("RANGE_")]
    org = Counter(r["origin"] for r in funcs)
    ret = Counter(r["ret"] for r in funcs)
    fp = sum(1 for r in funcs if r["fp"] == "1")
    thunks = sum(1 for r in funcs if r["notes"].startswith("thunk:"))
    rbytes = sum(int(r["size"]) for r in ranges)
    return "\n".join([
        f"- Rows in `docs/functions.csv`: {len(rows)} = {len(funcs)} Ghidra functions + {len(ranges)} `RANGE_*` rows "
        f"({rbytes} bytes of segment 34 that no function run covers).",
        "- Segment 34 byte accounting: memory size {0}; in function runs {1}; in RANGE rows {2}; remaining (0x90/0x00 padding "
        "between rows) {3}.".format(*seg_accounting(rows, cover)),
        f"- Functions: origin game {org['game']}, crt {org['crt']}. ret: " +
        ", ".join(f"{k} {ret[k]}" for k in ("far", "near", "mixed", "none")) + f". fp=1: {fp}. Thunks among them: {thunks}.",
    ])


def render_coverage(rows, cover):
    """cover: [[seg, size, ins, data, undef]] from COVER records."""
    nfun = Counter(int(r["seg"]) for r in rows if not r["name"].startswith("RANGE_"))
    nrng = Counter(int(r["seg"]) for r in rows if r["name"].startswith("RANGE_"))
    lines = ["| seg | functions | range rows | memory size | instructions | defined data | undefined |",
             "|----:|----------:|-----------:|------------:|-------------:|-------------:|----------:|"]
    tot = [0, 0, 0, 0, 0, 0]
    for c in sorted(cover, key=lambda c: int(c[0])):
        seg, size, ins, data, undef = (int(x) for x in c)
        lines.append(f"| {seg} | {nfun[seg]} | {nrng[seg]} | {size} | {ins} | {data} | {undef} |")
        for i, v in enumerate((nfun[seg], nrng[seg], size, ins, data, undef)):
            tot[i] += v
    lines.append("| **all** | " + " | ".join(f"**{v}**" for v in tot) + " |")
    return "\n".join(lines)


def render_x87(x87, raw):
    """x87: [[mnemonic, seg, count]] (Ghidra); raw: {mnemonic: {seg: count}} (byte scan)."""
    by = defaultdict(lambda: defaultdict(int))
    for m, seg, n in x87:
        by[m][int(seg)] += int(n)
    lines = ["| instruction | Ghidra instructions (by segment) | raw `D9 xx` byte matches (by segment) |",
             "|-------------|----------------------------------|----------------------------------------|"]
    for m in X87_SPECIAL:
        g = ", ".join(f"seg {s} x{n}" for s, n in sorted(by[m].items())) or "none"
        r = raw.get(m, {})
        rr = ", ".join(f"seg {s} x{n}" for s, n in sorted(r.items())) if m in X87_OPCODES else "(no fixed two-byte form)"
        lines.append(f"| {m} | {g} | {rr or 'none'} |")
    total = sum(sum(d.values()) for d in by.values())
    segs = sorted({s for d in by.values() for s in d})
    lines.append("")
    lines.append(f"Ghidra decodes {total} x87 instructions (mnemonic starts with F) in {len(segs)} code segments; "
                 f"{len(by)} distinct mnemonics. A raw-byte match that Ghidra does not decode is inside a data table or "
                 f"an undecoded range, so it is not evidence of an instruction.")
    return "\n".join(lines)


def render_csoverride(cs):
    """cs: CSOVERRIDE records: [addr, text, classification] plus the ['count', N] record."""
    count = next(int(r[1]) for r in cs if r[0] == "count")
    recs = [r for r in cs if r[0] != "count"]
    writes = sum(1 for r in recs if len(r) > 2 and r[2].startswith("WRITE"))
    segs = Counter(r[0].split(":")[0] for r in recs)
    return (f"- Instructions with a `CS:` segment override: {count} (records: {len(recs)}), in {len(segs)} segments.\n"
            f"- Of those, instructions that write through `CS:`: **{writes}**.")


def msg_value(v):
    if v == "dynamic":
        return None
    return int(v, 16)


def site_key(addr):
    seg, off = addr.split(":")
    return int(seg), int(off, 16)


def msg_name(value):
    if value in WM_NAMES:
        return f"{WM_NAMES[value]} ({UNCHECKED})"
    if value >= 0x400:
        n = value - 0x400
        cand = USER_CANDIDATES.get(n)
        base = f"user-defined, WM_USER+{n}"
        if cand:
            return f"{base}; control-class candidates: {cand} ({UNCHECKED})"
        return f"{base}; no candidate recalled"
    return f"(no name recalled)"


def aggregate_messages(msg, api):
    """msg: MSG records. Returns (total sites, {value|None: [site addrs]})."""
    total, by = 0, defaultdict(list)
    for r in msg:
        if r[0] != api:
            continue
        if r[1] == "count":
            total = int(r[2])
            continue
        by[msg_value(r[3])].append(r[1])
    return total, by


def render_messages(msg):
    out = []
    for api in ("USER.SENDMESSAGE", "USER.SENDDLGITEMMESSAGE"):
        total, by = aggregate_messages(msg, api)
        got = sum(len(v) for v in by.values())
        out.append(f"#### {api} ({total} sites, {got} listed)\n")
        out.append("| Message | Sites | Name / candidates | Example sites |")
        out.append("|---------|------:|-------------------|---------------|")
        for v in sorted((k for k in by if k is not None)) + ([None] if None in by else []):
            sites = sorted(by[v], key=site_key)
            ex = ", ".join(sites[:4]) + (" ..." if len(sites) > 4 else "")
            if v is None:
                out.append(f"| dynamic | {len(sites)} | see the list of dynamic sites below | {ex} |")
            else:
                out.append(f"| `0x{v:x}` | {len(sites)} | {msg_name(v)} | {ex} |")
        out.append("")
    return "\n".join(out).rstrip()


RENDER = ("summary", "coverage", "x87", "csoverride", "messages")


def replace_blocks(doc, blocks):
    """Replace the text between GEN markers. Raises GenError if a marker pair is missing."""
    for name, body in blocks.items():
        pat = re.compile(rf"(<!-- GEN:{name} -->\n)(.*?)(\n<!-- /GEN:{name} -->)", re.S)
        if not pat.search(doc):
            raise GenError(f"marker pair for block '{name}' not found")
        doc = pat.sub(lambda m: m.group(1) + body + m.group(3), doc, count=1)
    return doc


def build_blocks(rows, facts, raw):
    return {
        "summary": render_summary(rows, facts["COVER"]),
        "coverage": render_coverage(rows, facts["COVER"]),
        "x87": render_x87(facts["X87"], raw),
        "csoverride": render_csoverride(facts["CSOVERRIDE"]),
        "messages": render_messages(facts["MSG"]),
    }


def main(argv):
    args = argv[1:]
    opt = {"--csv": ROOT / "docs/functions.csv", "--facts": ROOT / "re/facts.txt",
           "--exe": ROOT / "original/stars26i.exe", "--nedump": ROOT / "build/nedump", "--doc": ROOT / "docs/inventory.md"}
    check = False
    while args:
        a = args.pop(0)
        if a == "--check":
            check = True
        elif a in opt:
            opt[a] = Path(args.pop(0))
        else:
            print(__doc__, file=sys.stderr)
            return 2
    try:
        rows = read_csv(opt["--csv"])
        facts = parse_facts(opt["--facts"].read_text(encoding="utf-8"))
        for tag in ("COVER", "X87", "CSOVERRIDE", "MSG"):
            if not facts[tag]:
                raise GenError(f"{opt['--facts']} has no {tag} records; rerun the Ghidra command")
        segs = parse_segments(subprocess.check_output([str(opt["--nedump"]), "--segments", str(opt["--exe"])], text=True))
        data = opt["--exe"].read_bytes()
        raw = raw_x87_scan({n: data[off:off + ln] for n, (off, ln) in segs.items()})
        doc = opt["--doc"].read_text(encoding="utf-8")
        new = replace_blocks(doc, build_blocks(rows, facts, raw))
    except (GenError, OSError, subprocess.CalledProcessError) as e:
        print(f"gen_inventory: {e}", file=sys.stderr)
        return 2
    if new == doc:
        print("gen_inventory: generated blocks are up to date")
        return 0
    if check:
        print("gen_inventory: generated blocks in docs/inventory.md are out of date; run tools/gen_inventory.py", file=sys.stderr)
        return 1
    opt["--doc"].write_text(new, encoding="utf-8")
    print(f"gen_inventory: updated {opt['--doc']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
