#!/usr/bin/env python3
"""Tests for tools/lint_src.py. Fixture: tools/fixtures/lint_good (clean); bad cases are copies with
one mutation each, so every test shows the lint can fail."""
import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import lint_src as L  # noqa: E402

GOOD = HERE / "fixtures" / "lint_good"


class LintTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "w"
        shutil.copytree(GOOD, self.root)
        self.src = self.root / "src"
        self.scn = self.root / "scenarios"
        self.csv = self.root / "functions.csv"

    def tearDown(self):
        self.tmp.cleanup()

    def lint(self):
        return L.lint(self.src, self.csv, self.scn)

    def edit(self, path, old, new):
        t = path.read_text()
        self.assertIn(old, t)
        path.write_text(t.replace(old, new, 1))

    def errors(self):
        return "\n".join(self.lint())

    def test_clean_fixture_passes(self):
        self.assertEqual(self.lint(), [])

    def test_main_exit_codes(self):
        args = ["--src", str(self.src), "--csv", str(self.csv), "--scenarios", str(self.scn)]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(L.main(args), 0)
            self.edit(self.src / "seg01.c", "uint16_t x =", "int x =")
            self.assertEqual(L.main(args), 1)

    def test_plain_int_names_file_and_line(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\n\nint x;\n")
        errs = self.lint()
        self.assertTrue(any(e.startswith(f"{self.src / 'seg01.c'}:3:") and "int" in e for e in errs), errs)

    def test_each_banned_construct(self):
        cases = {"short s;": "short", "long l;": "long", "unsigned u;": "unsigned",
                 "signed s;": "signed", "char c;": "char", "long double d;": "long",
                 "void f(void) { void *p = malloc(4); free(p); }": "malloc",
                 "uintptr_t p;": "uintptr_t",
                 "uint16_t g_counter;": "mutable global",
                 "static uint16_t cache[4];": "mutable global"}
        for code, word in cases.items():
            with self.subTest(code=code):
                (self.src / "seg01.c").write_text("#include <stdint.h>\n" + code + "\n")
                self.assertIn(word, self.errors())

    def test_lint_int64(self):
        # rule (a): the 64-bit types are not game types; use 32-bit pieces
        for ty in ("int64_t", "uint64_t"):
            with self.subTest(ty=ty):
                (self.src / "seg01.c").write_text("#include <stdint.h>\nvoid f(void) { %s v = 0; (void)v; }\n" % ty)
                self.assertIn(ty, self.errors())
        (self.src / "seg01.c").write_text("#include <stdint.h>\nvoid f(void) { int64_t v = 0; (void)v; }\n")
        args = ["--src", str(self.src), "--csv", str(self.csv), "--scenarios", str(self.scn)]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(L.main(args), 1)

    def test_mutable_globals_that_used_to_slip_through(self):
        # each of these used to pass: a "(" or a const anywhere exempted the statement
        cases = ["static uint16_t g = (1);",
                 "static void (*fp)(void);",
                 "uint16_t a[sizeof(uint32_t)];",
                 "const uint8_t *p;",
                 "static const uint8_t *tab[2];",
                 "static uint16_t (*tbl[2])(void) = {0, 0};"]
        for code in cases:
            with self.subTest(code=code):
                (self.src / "seg01.c").write_text("#include <stdint.h>\n" + code + "\n")
                self.assertIn("mutable global", self.errors())

    def test_const_declarators_and_prototypes_stay_ok(self):
        ok = ("#include <stdint.h>\n"
              "const uint8_t *const p = 0;\n"
              "static const uint8_t tab[2] = {1, 2};\n"
              "static uint16_t (*const fp)(void) = 0;\n"
              "extern uint16_t (*efp)(void);\n"
              "uint16_t proto(uint16_t (*cb)(void), uint8_t b);\n"
              "STATIC_SIZE(S, 2);\n")
        (self.src / "seg01.c").write_text(ok)
        self.assertEqual([e for e in self.lint() if "mutable" in e], [])

    def test_define_bodies_are_scanned(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\n#define BAD(x) ((int)(x))\n"
                                          "#define LONG2(x) \\\n  ((long)(x))\n")
        e = self.errors()
        self.assertIn("seg01.c:2: banned 'int'", e)
        self.assertIn("seg01.c:4: banned 'long'", e)

    def test_define_names_and_includes_are_not_banned_words(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\n#define COUNT 3\n"
                                          "#define MK(a) ((uint16_t)(a))\n")
        self.assertEqual([e for e in self.lint() if "banned" in e], [])

    def test_static_local_is_flagged(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\nuint16_t f(void)\n{\n"
                                          "  static uint16_t n;\n  return n++;\n}\n")
        e = self.errors()
        self.assertIn("seg01.c:4:", e)
        self.assertIn("static local", e)

    def test_static_const_local_table_is_ok(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\nuint16_t f(uint16_t i)\n{\n"
                                          "  static const uint16_t t[2] = {1, 2};\n  return t[i & 1];\n}\n")
        self.assertEqual([e for e in self.lint() if "static" in e or "mutable" in e], [])

    def test_int_message_says_ghidra_int_is_16_bit(self):
        (self.src / "seg01.c").write_text("#include <stdint.h>\nint x;\n")
        self.assertIn("Ghidra's int/uint are 16-bit", self.errors())

    def test_things_that_are_not_banned(self):
        ok = ("#include <stdint.h>\n"
              "typedef struct { uint16_t a; } S;\n"
              "struct T { int16_t b; };\n"
              "extern const uint16_t k;\n"
              "static const uint8_t tab[2] = {1, 2};\n"
              "_Static_assert(sizeof(S) == 2, \"x\");\n"
              "uint16_t prototype(uint16_t a);\n"
              "uint16_t f(uint16_t a) { uint16_t point = a; uint32_t longer = point; return (uint16_t)longer; }\n"
              "#define COUNT 3\n")
        (self.src / "seg01.c").write_text(ok)
        self.assertEqual([e for e in self.lint() if "banned" in e or "mutable" in e], [])

    def test_comments_and_strings_do_not_trigger(self):
        (self.src / "seg01.c").write_text('#include <stdint.h>\n/* int long\n short */ // unsigned\n'
                                          'static const uint8_t m[] = "int long";\n')
        self.assertEqual([e for e in self.lint() if "banned" in e or "mutable" in e], [])

    def test_headers_are_linted_too(self):
        (self.src / "stars.h").write_text("typedef long BAD;\n")
        self.assertIn("stars.h:1", self.errors())

    def test_decomp_without_csv_row(self):
        self.edit(self.src / "seg01.c", "DECOMP(1, 0x0000, fx_add", "DECOMP(1, 0x0999, fx_add")
        e = self.errors()
        self.assertIn("0x0999", e)
        self.assertIn("not in", e)

    def test_csv_decompiled_row_without_decomp(self):
        self.edit(self.csv, "fx_todo,10,far,,,,0,game,todo", "fx_todo,10,far,,,,0,game,decompiled")
        e = self.errors()
        self.assertIn("fx_todo", e)
        self.assertIn("no DECOMP", e)

    def test_decomp_on_todo_row(self):
        self.edit(self.csv, "fx_add,10,far,,,,0,game,tested", "fx_add,10,far,,,,0,game,todo")
        self.assertIn("status is todo", self.errors())

    def test_decomp_in_wrong_segment_file(self):
        shutil.move(self.src / "seg01.c", self.src / "seg02.c")
        self.assertIn("seg02.c", self.errors())
        self.assertIn("segment 1", self.errors())

    def test_tested_row_without_diff(self):
        (self.scn / "test_seg01.c").write_text("int main(void) { DIFF_FUZZ(1, 0x0000, FAR_CDECL, 1, 10, 1); }\n")
        e = self.errors()
        self.assertIn("fx_add", e)
        self.assertIn("no DIFF", e)

    def test_tested_row_with_diff_for_other_function(self):
        self.edit(self.scn / "test_seg01.c", "DIFF(1, 0x0000", "DIFF(1, 0x0010")
        self.assertIn("no DIFF", self.errors())

    def test_missing_scenarios_dir_means_no_diff(self):
        shutil.rmtree(self.scn)
        self.assertIn("no DIFF", self.errors())

    def test_call_orig_to_decompiled_function(self):
        self.edit(self.src / "seg01.c", "CALL_ORIG(1, 0x0010", "CALL_ORIG(1, 0x0000")
        e = self.errors()
        self.assertIn("CALL_ORIG", e)
        self.assertIn("already decompiled", e)

    def test_call_orig_to_decompiled_by_csv_status_only(self):
        self.edit(self.csv, "fx_todo,10,far,,,,0,game,todo", "fx_todo,10,far,,,,0,game,decompiled")
        self.assertIn("already decompiled", self.errors())

    FRAMED = ("#include <stdint.h>\n#include \"runtime/frame.h\"\n"
              "DECOMP(1, 0x0000, fx_add, FAR_CDECL)\nint16_t fx_add(int16_t a)\n{\n%s}\n"
              "DECOMP(1, 0x0010, fx_todo, FAR_CDECL)\nint16_t fx_todo(int16_t a)\n{\n%s}\n")

    def frames(self, add, todo):
        """seg01.c with two functions; fx_todo becomes decompiled so the csv checks stay quiet."""
        self.csv.write_text(self.csv.read_text().replace("fx_todo,10,far,,,,0,game,todo",
                                                         "fx_todo,10,far,,,,0,game,decompiled"))
        (self.src / "seg01.c").write_text(self.FRAMED % (add, todo))

    LEAF = "  return a;\n"
    CALLER = "  FRAME_ENTER(2);\n  int16_t r = (int16_t)GlobalAlloc(0, 4);\n  FRAME_LEAVE();\n  return r;\n"

    def test_lint_frame_missing(self):
        self.frames("  int16_t r = (int16_t)GlobalAlloc(0, 4);\n  return r;\n", self.LEAF)
        e = self.errors()
        self.assertIn("seg01.c:3:", e)
        self.assertIn("rule (d)", e)
        self.assertIn("fx_add", e)

    def test_lint_frame_missing_for_call_orig_rt_call_far_and_decomp_callee(self):
        for call in ("(void)CALL_ORIG(1, 0x0030, FAR_CDECL, 0);", "(void)rt_call_far(0, FAR_CDECL, 0, 0);",
                     "(void)fx_todo(a);"):
            with self.subTest(call=call):
                self.frames("  " + call + "\n  return a;\n", self.LEAF)
                self.assertIn("rule (d)", self.errors())

    def test_lint_frame_leaf_needs_no_frame(self):
        self.frames(self.LEAF, "  uint16_t x = 1;\n  return a + x;\n")
        self.assertEqual(self.lint(), [])

    def test_lint_frame_exempt(self):
        self.frames("  /* lint: leaf-frame-ok callee takes no stack address */\n"
                    "  int16_t r = (int16_t)GlobalAlloc(0, 4);\n  return r;\n", self.LEAF)
        self.assertEqual(self.lint(), [])

    def test_lint_frame_exempt_needs_a_reason(self):
        self.frames("  /* lint: leaf-frame-ok */\n  int16_t r = (int16_t)GlobalAlloc(0, 4);\n  return r;\n",
                    self.LEAF)
        self.assertIn("rule (d)", self.errors())

    def test_lint_frame_return_without_leave(self):
        self.frames("  FRAME_ENTER(2);\n  (void)GlobalAlloc(0, 4);\n  if (a) return 1;\n  FRAME_LEAVE();\n"
                    "  return 2;\n", self.LEAF)
        e = self.errors()
        self.assertIn("seg01.c:8:", e)
        self.assertIn("FRAME_LEAVE", e)
        self.assertIn("rule (d)", e)

    def test_lint_frame_leave_before_every_return_passes(self):
        self.frames("  FRAME_ENTER(2);\n  (void)GlobalAlloc(0, 4);\n"
                    "  if (a) { FRAME_LEAVE(); return 1; }\n  FRAME_LEAVE();\n  return 2;\n", self.LEAF)
        self.assertEqual(self.lint(), [])

    def test_lint_frame_in_comment_or_string_does_not_count(self):
        self.frames("  /* FRAME_ENTER(2); */\n  (void)GlobalAlloc(0, 4);\n  return a;\n", self.LEAF)
        self.assertIn("rule (d)", self.errors())

    def test_lint_bare_call(self):
        self.frames("  FRAME_ENTER(0);\n  int16_t r = fx_todo(a);\n  FRAME_LEAVE();\n  return r;\n",
                    "  FRAME_ENTER(0);\n  FRAME_LEAVE();\n  return a;\n")
        e = self.errors()
        self.assertIn("rule (e)", e)
        self.assertIn("fx_todo", e)
        self.assertIn("rt_call_far", e)

    def test_lint_call_by_name_to_leaf_is_fine_for_rule_e(self):
        self.frames("  FRAME_ENTER(0);\n  int16_t r = fx_todo(a);\n  FRAME_LEAVE();\n  return r;\n", self.LEAF)
        self.assertEqual(self.lint(), [])

    def test_lint_bare_call_across_files(self):
        self.frames(self.LEAF, "  FRAME_ENTER(0);\n  FRAME_LEAVE();\n  return a;\n")
        (self.src / "seg02.c").write_text(
            "#include <stdint.h>\nDECOMP(2, 0x0000, fx_two, FAR_CDECL)\nint16_t fx_two(int16_t a)\n{\n"
            "  FRAME_ENTER(0);\n  int16_t r = fx_todo(a);\n  FRAME_LEAVE();\n  return r;\n}\n")
        self.assertIn("rule (e)", "\n".join(e for e in self.lint() if "seg02.c" in e))

    TAG = "/* Adds. 1:0x0000 fx_add, FAR_CDECL */\n"

    def make_readable(self, body_extra="", tag=True):
        self.edit(self.csv, "fx_add,10,far,,,,0,game,tested", "fx_add,10,far,,,,0,game,readable")
        s = self.src / "seg01.c"
        self.edit(s, "CALL_ORIG(1, 0x0010", "CALL_ORIG(1, 16")   # a hex callee address would count as magic
        if tag:
            self.edit(s, "DECOMP(1, 0x0000", self.TAG + "DECOMP(1, 0x0000")
        if body_extra:
            self.edit(s, "  FRAME_LEAVE();", body_extra + "  FRAME_LEAVE();")

    def test_readable_clean_function_passes(self):
        self.make_readable()
        self.assertEqual(self.lint(), [])

    def test_readable_lint(self):
        self.make_readable("  uint16_t y = drd16(0x10);\n  (void)y;\n")
        e = self.errors()
        self.assertIn("readability", e)
        self.assertIn("fx_add", e)
        self.assertIn("raw", e)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(L.main(["--src", str(self.src), "--csv", str(self.csv),
                                     "--scenarios", str(self.scn)]), 1)

    def test_readable_needs_placeholder_free_magic_free_and_tag(self):
        for extra, tag, word in (("  uint16_t local_4 = 0;\n  (void)local_4;\n", True, "placeholder"),
                                 ("  uint16_t y = x & 0x1e;\n  (void)y;\n", True, "magic"),
                                 ("", False, "tag line")):
            with self.subTest(word=word):
                self.tearDown()
                self.setUp()
                self.make_readable(extra, tag)
                self.assertIn(word, self.errors())

    def test_readable_function_with_placeholder_name_fails(self):
        self.make_readable()
        s = self.src / "seg01.c"
        s.write_text(s.read_text().replace("fx_add", "fn_01_0000"))
        self.edit(self.csv, "fx_add", "fn_01_0000")
        self.assertIn("placeholder", self.errors())

    def test_readable_needs_diff_and_decomp(self):
        self.make_readable()
        self.edit(self.scn / "test_seg01.c", "DIFF(1, 0x0000", "DIFF(1, 0x0010")
        self.assertIn("no DIFF", self.errors())

    def test_non_readable_functions_are_not_held_to_readability(self):
        self.edit(self.src / "seg01.c", "  FRAME_LEAVE();", "  uint16_t y = drd16(0x10);\n  (void)y;\n  FRAME_LEAVE();")
        self.assertEqual(self.lint(), [])

    def test_unused_needs_evidence(self):
        self.edit(self.csv, "game,unused,no callers; not exported; no data refs", "game,unused,")
        self.assertIn("unused", self.errors())

    def test_unknown_status(self):
        self.edit(self.csv, "game,todo,", "game,done,")
        self.assertIn("status", self.errors())

    def test_empty_workspace_is_clean(self):
        shutil.rmtree(self.src)
        shutil.rmtree(self.scn)
        self.csv.unlink()
        self.assertEqual(self.lint(), [])

    def test_decomp_without_csv_is_error(self):
        self.csv.unlink()
        self.assertIn("functions.csv", self.errors())


if __name__ == "__main__":
    unittest.main()
