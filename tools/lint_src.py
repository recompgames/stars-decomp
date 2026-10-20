#!/usr/bin/env python3
"""Lint the local decompiled sources against docs/CONVENTIONS.md.

usage: lint_src.py [--src DIR] [--csv FILE] [--scenarios DIR]
       defaults: src  docs/functions.csv  tests/scenarios
Prints "file:line: message" per problem and exits 1 if there is any, else 0. With no src files
and no csv there is nothing to check and it exits 0 (docs/functions.csv is local-only).

Checks:
 1. banned constructs in src/*.c and src/*.h (comments and string literals are ignored; #define
    bodies are scanned too):
    int short long unsigned signed char (use the fixed-width types), uintptr_t/intptr_t/ptrdiff_t,
    malloc/calloc/realloc/free, mutable file-scope variables and non-const static locals (rule a).
    Only a const that qualifies the declarator itself exempts a variable ('const T *p' is mutable). Host pointers stored in
    arena memory cannot be found statically; the diff tests catch those.
 2. every DECOMP(seg, 0xOFF, ...) has a row in the csv, the row's status is not todo, and a
    segNN.c file only holds DECOMP lines for segment NN; every csv row with status decompiled, tested or
    readable has a DECOMP (rules f, i).
 3. every csv row with status tested or readable has a DIFF(seg, 0xOFF, ...) in tests/scenarios/*.c.
 4. no CALL_ORIG(seg, 0xOFF, ...) targets a function that is already decompiled (a DECOMP line or
    csv status decompiled/tested).
 5. csv statuses are todo/decompiled/tested/readable/unused, and unused rows carry evidence in notes.
 6. frames (rule d): a DECOMP function whose body calls another DECOMP function, CALL_ORIG,
    rt_call_far or a Win16 API (the names in runtime/win16_api.inc next to the src dir) must contain
    FRAME_ENTER and a FRAME_LEAVE before every return. Exempt with a comment
    /* lint: leaf-frame-ok <reason> */ between the previous function and the end of this one.
 7. calls (rule e): a DECOMP function that contains FRAME_ENTER is never called by name from
    another DECOMP function: go through rt_call_far.
 8. readability (rules i, k; docs/PLAN.md section 5): a function whose csv status is readable has a
    DECOMP, a DIFF, and in its code zero raw rd*/wr*/drd*/dwr*/LINN accesses, zero placeholder names,
    zero unnamed hex constants and the tag line seg:0xOFF in its comment (tools/readability.py).
"""
import csv
import re
import sys
from pathlib import Path

STATUSES = {"todo", "decompiled", "tested", "readable", "unused"}
HOSTPTR = "host pointers do not belong in game code"
NOALLOC = "no host allocation: game memory comes from the emulated heap"
BANNED_WORDS = {
    "int": "Ghidra's int/uint are 16-bit: use int16_t/uint16_t (int32_t only for a 32-bit original)", "short": "use int16_t", "long": "use int32_t",
    "unsigned": "use uint16_t/uint32_t", "signed": "use int8_t/int16_t/int32_t",
    "int64_t": "no 64-bit type: use 32-bit pieces (hi, lo)", "uint64_t": "no 64-bit type: use 32-bit pieces (hi, lo)",
    "char": "use int8_t/uint8_t", "uintptr_t": HOSTPTR, "intptr_t": HOSTPTR, "ptrdiff_t": HOSTPTR,
    "malloc": NOALLOC, "calloc": NOALLOC, "realloc": NOALLOC, "free": NOALLOC,
}
WORD_RE = re.compile(r"\b[A-Za-z_]\w*\b")
NUM = r"(0[xX][0-9a-fA-F]+|\d+)"
DECOMP_RE = re.compile(r"^\s*DECOMP\(\s*(\d+)\s*,\s*" + NUM + r"\s*,\s*(\w+)\s*,")
CALL_ORIG_RE = re.compile(r"\bCALL_ORIG\(\s*(\d+)\s*,\s*" + NUM + r"\s*,")
DIFF_RE = re.compile(r"\bDIFF\(\s*(\d+)\s*,\s*" + NUM + r"\s*,")
W16API_RE = re.compile(r'W16API\(\s*\w+\s*,\s*\d+\s*,\s*"(\w+)"')
EXEMPT_RE = re.compile(r"/\*\s*lint:\s*leaf-frame-ok\s+\S.*?\*/", re.S)
SEGFILE_RE = re.compile(r"^seg(\d\d)\.c$")


def strip_code(text):
    """Blank out comments and string/char literals, keeping newlines so line numbers survive."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * max(j - i - 2, 0) + (c if j - i >= 2 else ""))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def code_lines(code):
    """(line number, text) of non-preprocessor lines; backslash continuations are skipped."""
    cont = False
    for ln, line in enumerate(code.splitlines(), start=1):
        was_cont = cont
        cont = line.rstrip().endswith("\\")
        if was_cont or line.lstrip().startswith("#") or cont:
            continue
        yield ln, line


def define_lines(code):
    """(line number, text) of #define lines and their backslash continuations, '#define' dropped."""
    cont = False
    for ln, line in enumerate(code.splitlines(), start=1):
        m = re.match(r"\s*#\s*define\b(.*)", line)
        if m or cont:
            yield ln, (m.group(1) if m else line)
            cont = line.rstrip().endswith("\\")
        else:
            cont = False


def check_banned(path, code, errs):
    for ln, line in list(code_lines(code)) + list(define_lines(code)):
        for w in WORD_RE.findall(line):
            if w in BANNED_WORDS:
                errs.append(f"{path}:{ln}: banned '{w}': {BANNED_WORDS[w]}")


def declarator_is_const(text):
    """True if a const qualifies the declared object itself: with a '*' in the declarator it must
    come after the last '*' (uint8_t *const p), otherwise anywhere ('const uint8_t *p' is a mutable
    pointer to const data)."""
    decl = text.split("=", 1)[0]
    star = decl.rfind("*")
    return bool(re.search(r"\bconst\b", decl[star + 1:] if star >= 0 else decl))


def is_prototype(text):
    """name(params); with no initializer. 'T (*name)(params);' is a function pointer variable."""
    if "=" in text or not re.search(r"\)\s*;$", text):
        return False
    return not re.match(r"[^()]*\(\s*\*", text)


def toplevel_ok(text):
    if re.match(r"^(typedef|extern|_Static_assert|static_assert)\b", text):
        return True
    if is_prototype(text) or declarator_is_const(text):
        return True
    return bool(re.match(r"^(struct|union|enum)\s*\w*\s*(\{\})?\s*;$", text))


def check_mutable_globals(path, code, errs):
    """Flag file-scope variable definitions that are not const, extern, typedef or prototypes, and
    'static' non-const variables inside function bodies."""
    depth, stmt, first = 0, "", 0
    body, bfirst = "", 0
    for ln, line in code_lines(code):
        for ch in line + " ":
            if depth > 0 and ch in ";{}":
                text = " ".join(body.split())
                if ch == ";" and re.match(r"static\b", text) and not declarator_is_const(text):
                    errs.append(f"{path}:{bfirst}: static local '{text[:50]}': state lives in DGROUP "
                                "(src/globals.h); tables must be const")
                body = ""
            elif depth > 0:
                if not body.strip() and not ch.isspace():
                    bfirst = ln
                body += ch
            if ch == "{":
                if depth == 0:
                    stmt += "{"
                depth += 1
            elif ch == "}":
                depth = max(depth - 1, 0)
                if depth == 0:
                    stmt += "}"
                    if "(" in stmt and not stmt.rstrip().endswith(";"):   # a function body ended
                        # a following ';' would belong to an initializer, so only drop real bodies
                        if re.search(r"\)\s*\{\}$", " ".join(stmt.split())):
                            stmt, first = "", 0
            elif depth == 0:
                if not stmt.strip() and not ch.isspace():
                    first = ln
                stmt += ch
                if ch == ";":
                    text = " ".join(stmt.split())
                    stmt = ""
                    if not toplevel_ok(text):
                        errs.append(f"{path}:{first}: mutable global '{text[:50]}': game globals live "
                                    "in DGROUP (src/globals.h); tables must be const")


def api_names(src):
    """Win16 API names from runtime/win16_api.inc beside the src directory (none if it is absent)."""
    inc = Path(src).parent / "runtime" / "win16_api.inc"
    if not inc.is_file():
        return set()
    return set(W16API_RE.findall(inc.read_text(encoding="utf-8", errors="replace")))


def function_bodies(text, code):
    """[(name, DECOMP line, body line offset, body code, exempt)] for each DECOMP function. The body
    runs from the first '{' after the DECOMP line to its matching '}' in the stripped code; the
    exemption comment is searched in the raw text since the previous function's end."""
    out, prev_end = [], 0
    for m in re.finditer(r"^[ \t]*DECOMP\(\s*\d+\s*,\s*" + NUM + r"\s*,\s*(\w+)\s*,[^\n]*", code, re.M):
        brace = code.find("{", m.end())
        if brace < 0:
            continue
        depth, i = 0, brace
        while i < len(code):
            depth += (code[i] == "{") - (code[i] == "}")
            i += 1
            if depth == 0:
                break
        line = code.count("\n", 0, m.start()) + 1
        body_line = code.count("\n", 0, brace) + 1
        out.append((m.group(2), line, body_line, code[brace:i],
                    bool(EXEMPT_RE.search(text[prev_end:i]))))
        prev_end = i
    return out


def check_frames(files, texts, codes, apis, errs):
    funcs = {p: function_bodies(texts[p], codes[p]) for p in files}
    names = {f[0] for fs in funcs.values() for f in fs}
    framed = {f[0] for fs in funcs.values() for f in fs if "FRAME_ENTER" in f[3]}
    for p, fs in funcs.items():
        for name, line, body_line, body, exempt in fs:
            idents = {m.group(1) for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body)}
            for callee in sorted((idents & framed) - {name}):
                errs.append(f"{p}:{line}: {name} calls {callee} by name, but {callee} mirrors a frame: "
                            "call it through rt_call_far (rule (e))")
            calls = idents & ({"CALL_ORIG", "rt_call_far"} | apis | (names - {name}))
            if not calls or exempt:
                continue
            why = f"calls {sorted(calls)[0]}"
            if "FRAME_ENTER" not in body:
                errs.append(f"{p}:{line}: {name} {why} but has no FRAME_ENTER: mirror the frame "
                            "(rule (d)) or add /* lint: leaf-frame-ok <reason> */")
                continue
            last = 0
            rets = [m.start() for m in re.finditer(r"\breturn\b", body)]
            for pos in rets or [len(body)]:
                if "FRAME_LEAVE" not in body[last:pos]:
                    at = body_line + body.count("\n", 0, pos)
                    errs.append(f"{p}:{at}: {name} returns without FRAME_LEAVE() (rule (d))"
                                if rets else f"{p}:{line}: {name} never calls FRAME_LEAVE() (rule (d))")
                last = pos


def check_readable(files, texts, rows, csv_path, errs):
    """Check 8: functions marked readable in the csv meet the readability targets."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import readability   # imported here: readability itself imports this module
    for p in files:
        if not SEGFILE_RE.match(p.name):
            continue
        for f in readability.file_metrics(texts[p]):
            row = rows.get((f.seg, f.off))
            if not row or (row[0].get("status") or "").strip() != "readable":
                continue
            bad = [msg for n, msg in ((f.raw, f"{f.raw} raw rd/wr/drd/dwr/LINN access(es)"),
                                      (f.placeholders, f"{f.placeholders} placeholder name(s)"),
                                      (f.magic, f"{f.magic} unnamed magic constant(s)"),
                                      (not f.tagged, f"no tag line {f.seg}:0x{f.off:04x} in its comment"))
                   if n]
            if bad:
                errs.append(f"{p}: readability: {f.name} ({f.seg}:0x{f.off:04x}) is readable in {csv_path} "
                            "but has " + ", ".join(bad) + " (rules (i), (k))")


def read_csv(path):
    rows = {}
    with open(path, newline="", encoding="utf-8") as f:
        for n, r in enumerate(csv.DictReader(f), start=2):
            try:
                rows[(int(r["seg"]), int(r["off"], 0))] = (r, n)
            except (KeyError, ValueError, TypeError):
                continue
    return rows


def lint(src, csv_path, scenarios):
    src, csv_path, scenarios = Path(src), Path(csv_path), Path(scenarios)
    errs = []
    files = sorted(list(src.glob("*.c")) + list(src.glob("*.h"))) if src.is_dir() else []
    texts = {p: p.read_text(encoding="utf-8", errors="replace") for p in files}
    codes = {p: strip_code(t) for p, t in texts.items()}
    decomps = {}                                    # (seg, off) -> "file:line"
    for p in files:
        check_banned(p, codes[p], errs)
        check_mutable_globals(p, codes[p], errs)
        m_file = SEGFILE_RE.match(p.name)
        for ln, line in enumerate(codes[p].splitlines(), start=1):
            m = DECOMP_RE.match(line)
            if not m:
                continue
            key, where = (int(m.group(1)), int(m.group(2), 0)), f"{p}:{ln}"
            if key in decomps:
                errs.append(f"{where}: duplicate DECOMP {key[0]}:0x{key[1]:04x} (first at {decomps[key]})")
            decomps[key] = where
            if m_file and int(m_file.group(1)) != key[0]:
                errs.append(f"{where}: DECOMP for segment {key[0]} in {p.name}, which holds segment "
                            f"{int(m_file.group(1))} only")
    rows = {}
    if csv_path.exists():
        rows = read_csv(csv_path)
    elif decomps:
        errs.append(f"{csv_path}: functions.csv not found (needed to validate DECOMP lines)")
    for (seg, off), where in decomps.items():
        if rows and (seg, off) not in rows:
            errs.append(f"{where}: DECOMP {seg}:0x{off:04x} is not in {csv_path}")
        elif rows and rows[(seg, off)][0].get("status", "").strip() == "todo":
            errs.append(f"{where}: DECOMP {seg}:0x{off:04x} but its status is todo in {csv_path} "
                        "(set decompiled or tested)")
    diffs = set()
    if scenarios.is_dir():
        for p in sorted(scenarios.glob("*.c")):
            code = strip_code(p.read_text(encoding="utf-8", errors="replace"))
            diffs |= {(int(m.group(1)), int(m.group(2), 0)) for m in DIFF_RE.finditer(code)}
    done = set(decomps)
    for key, (r, _n) in rows.items():
        if (r.get("status") or "").strip() in ("decompiled", "tested", "readable"):
            done.add(key)
    for (seg, off), (r, n) in sorted(rows.items()):
        st, name, loc = (r.get("status") or "").strip(), r.get("name", ""), f"{csv_path}:{n}"
        if st not in STATUSES:
            errs.append(f"{loc}: {name} has unknown status '{st}' (todo/decompiled/tested/readable/unused)")
        if st in ("decompiled", "tested", "readable") and (seg, off) not in decomps:
            errs.append(f"{loc}: {name} ({seg}:0x{off:04x}) is {st} but there is no DECOMP line in {src}")
        if st in ("tested", "readable") and (seg, off) not in diffs:
            errs.append(f"{loc}: {name} ({seg}:0x{off:04x}) is {st} but has no DIFF in {scenarios}")
        if st == "unused" and not (r.get("notes") or "").strip():
            errs.append(f"{loc}: {name} is unused but notes has no evidence (no callers, not an entry "
                        "point, not referenced from data)")
    for p in files:
        for ln, line in enumerate(codes[p].splitlines(), start=1):
            for m in CALL_ORIG_RE.finditer(line):
                key = (int(m.group(1)), int(m.group(2), 0))
                if key in done:
                    errs.append(f"{p}:{ln}: CALL_ORIG {key[0]}:0x{key[1]:04x} targets a function that is "
                                "already decompiled: call it by its C name")
    check_frames(files, texts, codes, api_names(src), errs)
    check_readable(files, texts, rows, csv_path, errs)
    return errs


def main(argv):
    src, csv_path, scn = "src", "docs/functions.csv", "tests/scenarios"
    args = list(argv)
    try:
        while args:
            a = args.pop(0)
            if a == "--src":
                src = args.pop(0)
            elif a == "--csv":
                csv_path = args.pop(0)
            elif a == "--scenarios":
                scn = args.pop(0)
            else:
                print(__doc__, file=sys.stderr)
                return 2
    except IndexError:
        print(__doc__, file=sys.stderr)
        return 2
    errs = lint(src, csv_path, scn)
    for e in errs:
        print(e)
    if errs:
        print(f"lint_src: {len(errs)} problem(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
