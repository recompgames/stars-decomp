#!/usr/bin/env python3
"""Generate build/registry.c, the table of decompiled C functions keyed by original (seg, off).

usage: gen_registry.py [--src DIR] [--csv FILE] [-o OUT]
       defaults: src/  docs/functions.csv  build/registry.c

Each decompiled function in src/*.c is preceded by a line
    DECOMP(seg, 0xOFF, name, CONV)        CONV: FAR_PASCAL FAR_CDECL NEAR_PASCAL NEAR_CDECL REGISTER
and is then defined as an ordinary (non-static, non-inline) C function whose parameters and return
type are the fixed-width int8_t/uint8_t/int16_t/uint16_t/int32_t/uint32_t/bool (far pointers are
uint32_t). Plain char/short/int/long/unsigned are rejected: they have no fixed size or signedness. The generator parses that definition to emit an adapter that reads the
arguments from the emulated stack exactly where the original would find them: PASCAL pushes the
first argument first (it ends up highest), CDECL pushes the last argument first (the first ends up
lowest); the return address takes 4 bytes for FAR and 2 for NEAR. A REGISTER function must be
"void/uint32_t name(Regs16 *r)" and receives the register file.

Errors (exit 1): a duplicate (seg, off) or name; a DECOMP whose (seg, off) is not in the csv; an
unknown CONV; a parameter or return type it cannot map; a DECOMP with no following definition.
"""
import csv
import re
import sys
from pathlib import Path

CONVS = ("FAR_PASCAL", "FAR_CDECL", "NEAR_PASCAL", "NEAR_CDECL", "REGISTER")
DECOMP_RE = re.compile(
    r"^\s*DECOMP\(\s*(\d+)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)\s*;?\s*$")
# Only fixed-width types: a Win16 int is 16 bits and long is 32, but the host's are not, and plain
# char/int/short/long/unsigned are read with the wrong width or signedness (-1 would arrive as 65535).
WORD_TYPES = {"int8_t", "uint8_t", "int16_t", "uint16_t", "bool", "_Bool"}
DWORD_TYPES = {"int32_t", "uint32_t"}
QUALS = {"const", "volatile"}      # kept: they do not change the size
DROP = {"extern", "register"}      # harmless on a definition, dropped from the generated prototype
REJECT = {"static": "DECOMP functions must not be static (the generated registry declares them extern)",
          "inline": "DECOMP functions must not be inline (the generated registry needs an external definition)"}


class GenError(Exception):
    pass


def type_words(tokens, where):
    """Size in 16-bit words (1 or 2) of a type given as tokens; 0 for void."""
    for t in tokens:
        if t in REJECT:
            raise GenError(f"{where}: {REJECT[t]}")
    toks = [t for t in tokens if t not in QUALS and t not in DROP]
    if "*" in toks:
        raise GenError(f"{where}: pointer types are not supported; use uint32_t for far pointers")
    if toks == ["void"]:
        return 0
    if len(toks) == 1 and toks[0] in DWORD_TYPES:
        return 2
    if len(toks) == 1 and toks[0] in WORD_TYPES:
        return 1
    raise GenError(f"{where}: unsupported type '{' '.join(toks)}': use a fixed-width type "
                   "(int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t or bool); plain "
                   "char/short/int/long/unsigned have no fixed size or signedness")


def tokenize(text):
    return re.findall(r"[A-Za-z_]\w*|\*", text)


def parse_decl(lines, start, name, where):
    """Parse the definition of `name` starting at lines[start]. Returns (ret_tokens, [param_text])."""
    text = ""
    for i in range(start, min(start + 8, len(lines))):
        text += " " + lines[i]
        if ")" in text:
            break
    m = re.search(r"([^;{}()]*?)\b" + re.escape(name) + r"\s*\(([^)]*)\)", text)
    if not m:
        raise GenError(f"{where}: no definition of '{name}' follows the DECOMP line")
    params = [p.strip() for p in m.group(2).split(",")] if m.group(2).strip() else []
    if params == ["void"]:
        params = []
    return tokenize(m.group(1)), params


def scan_source(path):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    found = []
    for i, line in enumerate(lines):
        if "DECOMP(" not in line:
            continue
        m = DECOMP_RE.match(line)
        if not m:
            if line.lstrip().startswith("DECOMP("):
                raise GenError(f"{path}:{i + 1}: malformed DECOMP line")
            continue
        where = f"{path}:{i + 1}"
        seg, off, name, conv = int(m.group(1)), int(m.group(2), 0), m.group(3), m.group(4)
        if conv not in CONVS:
            raise GenError(f"{where}: unknown calling convention '{conv}'")
        ret, params = parse_decl(lines, i + 1, name, where)
        found.append((seg, off, name, conv, ret, params, where))
    return found


def read_csv(path):
    known = set()
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.reader(f)
        next(rd, None)
        for row in rd:
            if len(row) >= 2:
                known.add((int(row[0]), int(row[1], 0)))
    return known


def proto_type(ret):
    return " ".join(t for t in ret if t not in DROP)


def adapter(entry):
    seg, off, name, conv, ret, params, where = entry
    aname = f"dt_ad_{seg}_{off:04x}"
    if conv == "REGISTER":
        if len(params) > 1 or (params and not re.fullmatch(r"Regs16\s*\*\s*\w*", params[0])):
            raise GenError(f"{where}: a REGISTER function must take (Regs16 *r)")
        # The adapter's register-file parameter is called "regs" (see generate()).
        body = [f"  (void)ss; (void)sp;", f"  {name}(regs);", "  return 0;"] if type_words(ret, where) == 0 else \
               [f"  (void)ss; (void)sp;", f"  return (uint32_t){name}(regs);"]
        proto = f"extern {proto_type(ret)} {name}({params[0] if params else 'void'});"
        return proto, aname, body, 0
    sizes, casts = [], []
    for p in params:
        toks = tokenize(p)
        if len(toks) < 2:
            raise GenError(f"{where}: parameter '{p}' needs a type and a name")
        ttoks = toks[:-1]
        sizes.append(type_words(ttoks, where))
        if sizes[-1] == 0:
            raise GenError(f"{where}: void parameter")
        casts.append(" ".join(ttoks))
    retsz = 4 if conv.startswith("FAR") else 2
    offs = []
    for i in range(len(params)):
        if conv.endswith("PASCAL"):
            offs.append(retsz + 2 * sum(sizes[i + 1:]))
        else:
            offs.append(retsz + 2 * sum(sizes[:i]))
    args = ", ".join(
        f"({casts[i]})DT_ARG{16 * sizes[i]}(ss, sp, {offs[i]})" for i in range(len(params)))
    rw = type_words(ret, where)
    call = f"{name}({args})"
    body = ["  (void)regs;"]
    if rw == 0:
        body += [f"  {call};", "  return 0;"]
    elif rw == 1:
        body += [f"  return (uint32_t)(uint16_t){call};"]
    else:
        body += [f"  return (uint32_t){call};"]
    proto = f"extern {proto_type(ret)} {name}({', '.join(params) if params else 'void'});"
    return proto, aname, body, rw


def generate(src_dir, csv_path):
    entries = []
    src_dir = Path(src_dir)
    for p in sorted(src_dir.glob("*.c")) if src_dir.is_dir() else []:
        entries += scan_source(p)
    seen, names = {}, {}
    for e in entries:
        key = (e[0], e[1])
        if key in seen:
            raise GenError(f"{e[6]}: duplicate DECOMP ({e[0]}, 0x{e[1]:04x}), first at {seen[key]}")
        if e[2] in names:
            raise GenError(f"{e[6]}: duplicate function name '{e[2]}', first at {names[e[2]]}")
        seen[key], names[e[2]] = e[6], e[6]
    if entries:
        if not Path(csv_path).exists():
            raise GenError(f"{csv_path} not found (needed to validate DECOMP lines)")
        known = read_csv(csv_path)
        for e in entries:
            if (e[0], e[1]) not in known:
                raise GenError(f"{e[6]}: DECOMP ({e[0]}, 0x{e[1]:04x}) is not in {csv_path}")
    out = ["/* Generated by tools/gen_registry.py. Do not edit. */",
           '#include "harness/difftest.h"', ""]
    rows = []
    for e in sorted(entries, key=lambda e: (e[0], e[1])):
        proto, aname, body, rw = adapter(e)
        out.append(proto)
        out.append(f"static uint32_t {aname}(uint16_t ss, uint16_t sp, Regs16 *regs) {{")
        out.append("  (void)ss; (void)sp; (void)regs;")
        out += [b for b in body if "(void)" not in b]
        out += ["}", ""]
        rows.append(f'  {{{e[0]}, 0x{e[1]:04x}, "{e[2]}", {e[3]}, {aname}, {rw}}},')
    out.append("const DtEntry dt_registry[] = {")
    out += rows
    out.append("  {0, 0, 0, 0, 0, 0}")
    out.append("};")
    out.append(f"const unsigned dt_registry_count = {len(rows)};")
    return "\n".join(out) + "\n"


def main(argv):
    src, csv_path, out = "src", "docs/functions.csv", "build/registry.c"
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--src":
            src = args.pop(0)
        elif a == "--csv":
            csv_path = args.pop(0)
        elif a == "-o":
            out = args.pop(0)
        else:
            print(__doc__, file=sys.stderr)
            return 2
    try:
        text = generate(src, csv_path)
    except GenError as e:
        print(f"gen_registry: error: {e}", file=sys.stderr)
        return 1
    outp = Path(out)
    outp.parent.mkdir(parents=True, exist_ok=True)
    tmp = outp.with_name(outp.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(outp)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
