#!/usr/bin/env python3
"""Tests for tools/gen_registry.py (fixtures are built in a temp dir)."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_registry as g  # noqa: E402

CSV = "seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes\n" \
      "1,0x0000,FUN_a,10,far,,,,0,game,todo,\n" \
      "1,0x0010,FUN_b,10,far,,,,0,game,todo,\n" \
      "2,0x0020,FUN_c,10,near,,,,0,game,todo,\n" \
      "3,0x0030,FUN_d,10,far,,,,0,game,todo,\n"


class GenRegistryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "src").mkdir()
        (self.root / "f.csv").write_text(CSV)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text, name="a.c"):
        (self.root / "src" / name).write_text(text)

    def gen(self):
        return g.generate(self.root / "src", self.root / "f.csv")

    def test_parses_decomp_lines(self):
        self.write("DECOMP(1, 0x0000, add2, FAR_PASCAL)\n"
                   "int16_t add2(int16_t a, int16_t b) { return a + b; }\n"
                   "DECOMP(2, 0x0020, cd, NEAR_CDECL)\n"
                   "uint32_t cd(uint16_t x, uint32_t y)\n{ return y; }\n")
        out = self.gen()
        self.assertIn('{1, 0x0000, "add2", FAR_PASCAL, dt_ad_1_0000, 1}', out)
        self.assertIn('{2, 0x0020, "cd", NEAR_CDECL, dt_ad_2_0020, 2}', out)

    def test_pascal_offsets(self):
        self.write("DECOMP(1, 0x0000, add2, FAR_PASCAL)\n"
                   "int16_t add2(int16_t a, int16_t b) { return a + b; }\n")
        out = self.gen()
        # far return address = 4 bytes; b is pushed last so it is lowest
        self.assertIn("(int16_t)DT_ARG16(ss, sp, 6), (int16_t)DT_ARG16(ss, sp, 4)", out)

    def test_cdecl_offsets_and_dword(self):
        self.write("DECOMP(2, 0x0020, cd, NEAR_CDECL)\n"
                   "void cd(uint16_t x, uint32_t y, uint8_t z) { }\n")
        out = self.gen()
        self.assertIn("(uint16_t)DT_ARG16(ss, sp, 2), (uint32_t)DT_ARG32(ss, sp, 4), "
                      "(uint8_t)DT_ARG16(ss, sp, 8)", out)

    def test_register(self):
        self.write("DECOMP(3, 0x0030, reg, REGISTER)\nvoid reg(Regs16 *r) { }\n")
        self.assertIn('"reg", REGISTER, dt_ad_3_0030, 0', self.gen())

    def test_duplicate_is_error(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\nvoid f1(void) {}\n"
                   "DECOMP(1, 0x0000, f2, FAR_PASCAL)\nvoid f2(void) {}\n")
        with self.assertRaisesRegex(g.GenError, "duplicate DECOMP"):
            self.gen()

    def test_duplicate_across_files_is_error(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\nvoid f1(void) {}\n", "a.c")
        self.write("DECOMP(1, 0x0000, f2, FAR_PASCAL)\nvoid f2(void) {}\n", "b.c")
        with self.assertRaisesRegex(g.GenError, "duplicate DECOMP"):
            self.gen()

    def test_unknown_function_is_error(self):
        self.write("DECOMP(1, 0x0999, f1, FAR_PASCAL)\nvoid f1(void) {}\n")
        with self.assertRaisesRegex(g.GenError, "not in"):
            self.gen()

    def test_bad_conv_and_pointer_are_errors(self):
        self.write("DECOMP(1, 0x0000, f1, STDCALL)\nvoid f1(void) {}\n")
        with self.assertRaisesRegex(g.GenError, "calling convention"):
            self.gen()
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\nvoid f1(char *p) {}\n")
        with self.assertRaisesRegex(g.GenError, "pointer"):
            self.gen()

    def test_empty_src_gives_empty_table(self):
        out = self.gen()
        self.assertIn("dt_registry_count = 0", out)

    def test_main_writes_file_and_reports_error(self):
        self.write("DECOMP(1, 0x0000, add2, FAR_PASCAL)\nint16_t add2(int16_t a, int16_t b) { return a; }\n")
        out = self.root / "out" / "registry.c"
        rc = g.main(["--src", str(self.root / "src"), "--csv", str(self.root / "f.csv"), "-o", str(out)])
        self.assertEqual(rc, 0)
        self.assertIn("extern int16_t add2(int16_t a, int16_t b);", out.read_text())
        self.write("DECOMP(1, 0x0001, add2, FAR_PASCAL)\nint16_t add2(int16_t a, int16_t b) { return a; }\n")
        self.assertEqual(g.main(["--src", str(self.root / "src"), "--csv", str(self.root / "f.csv"),
                                 "-o", str(out)]), 1)

    # ---- D(a): the generated REGISTER adapter must compile ----

    @unittest.skipUnless(shutil.which("clang"), "clang not installed")
    def test_generated_registry_compiles(self):
        self.write("DECOMP(1, 0x0000, add2, FAR_PASCAL)\n"
                   "int16_t add2(int16_t a, int16_t b) { return a + b; }\n"
                   "DECOMP(1, 0x0010, cd, NEAR_CDECL)\n"
                   "uint32_t cd(uint16_t x, uint32_t y) { return y; }\n"
                   "DECOMP(3, 0x0030, reg, REGISTER)\n"
                   "void reg(Regs16 *r) { r->ax = r->bx; }\n"
                   "DECOMP(2, 0x0020, regv, REGISTER)\n"
                   "uint32_t regv(Regs16 *regs) { return regs->ax; }\n")
        out = self.root / "registry.c"
        out.write_text(self.gen())
        self.assertIn("reg(regs);", out.read_text())
        r = subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                            "-I", str(ROOT), str(out)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    # ---- F: type mapping ----

    def test_ambiguous_types_are_rejected(self):
        for t in ("int", "short", "long", "char", "unsigned", "unsigned int", "signed", "signed char",
                  "unsigned short", "unsigned long", "long long"):
            with self.subTest(param=t):
                self.write(f"DECOMP(1, 0x0000, f1, FAR_PASCAL)\nvoid f1({t} a) {{}}\n")
                with self.assertRaisesRegex(g.GenError, "fixed-width"):
                    self.gen()
            with self.subTest(ret=t):
                self.write(f"DECOMP(1, 0x0000, f1, FAR_PASCAL)\n{t} f1(uint16_t a) {{ return a; }}\n")
                with self.assertRaisesRegex(g.GenError, "fixed-width"):
                    self.gen()

    def test_fixed_width_types_are_accepted(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\n"
                   "bool f1(int8_t a, uint8_t b, int16_t c, uint16_t d, int32_t e, uint32_t f, bool g) "
                   "{ return a; }\n")
        self.assertIn("dt_ad_1_0000", self.gen())

    def test_static_is_rejected(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\nstatic uint16_t f1(uint16_t a) { return a; }\n")
        with self.assertRaisesRegex(g.GenError, "static"):
            self.gen()

    def test_inline_is_rejected(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\ninline uint16_t f1(uint16_t a) { return a; }\n")
        with self.assertRaisesRegex(g.GenError, "inline"):
            self.gen()

    def test_extern_is_dropped_from_the_prototype(self):
        self.write("DECOMP(1, 0x0000, f1, FAR_PASCAL)\nextern uint16_t f1(uint16_t a) { return a; }\n")
        self.assertIn("extern uint16_t f1(uint16_t a);", self.gen())
        self.assertNotIn("extern extern", self.gen())

    @unittest.skipUnless(shutil.which("clang"), "clang not installed")
    def test_signed_arguments_round_trip(self):
        """An int16_t -1 pushed as 0xFFFF arrives as -1, an int32_t -1 as -1, an int8_t 0xFF as -1."""
        self.write("DECOMP(1, 0x0000, widen, FAR_PASCAL)\n"
                   "int32_t widen(int16_t a) { return a; }\n"
                   "DECOMP(1, 0x0010, mix, FAR_CDECL)\n"
                   "int16_t mix(int8_t a, int32_t b, int16_t c) { return (int16_t)(a < 0 && b < 0 && c < 0 ? -7 : 7); }\n"
                   "DECOMP(2, 0x0020, ret16, FAR_PASCAL)\n"
                   "int16_t ret16(int16_t a) { return a; }\n")
        reg = self.root / "registry.c"
        reg.write_text(self.gen())
        # the functions live in the same translation unit as the registry
        fixture = (self.root / "src" / "a.c").read_text().replace("DECOMP(", "// DECOMP(")
        drv = self.root / "driver.c"
        drv.write_text(r'''
#include <stdio.h>
#include "harness/difftest.h"
''' + fixture + r'''
#include "registry.c"
static void put(uint16_t sel, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(sel, off)), &v, 2); }
int main(void) {
  Regs16 r;
  uint16_t sel;
  mem_init(0);
  sel = GlobalAlloc(GPTR, 256);
  memset(&r, 0, sizeof r);
  /* FAR PASCAL widen(int16_t a): a at sp+4 */
  put(sel, 0x14, 0xFFFF);
  printf("%08x\n", (unsigned)dt_ad_1_0000(sel, 0x10, &r));
  /* FAR CDECL mix(int8_t a, int32_t b, int16_t c): a at +4, b at +6 (low, high), c at +10 */
  put(sel, 0x34, 0x00FF); put(sel, 0x36, 0xFFFF); put(sel, 0x38, 0xFFFF); put(sel, 0x3A, 0xFFFF);
  printf("%04x\n", (unsigned)dt_ad_1_0010(sel, 0x30, &r) & 0xFFFF);
  put(sel, 0x54, 0x8000);
  printf("%08x\n", (unsigned)dt_ad_2_0020(sel, 0x50, &r));
  return 0;
}
''')
        exe = self.root / "driver"
        cc = subprocess.run(["clang", "-std=c11", "-Wall", "-Wno-unused-function", "-I", str(ROOT), "-I", str(self.root),
                             str(drv), str(ROOT / "runtime/mem.c"), str(ROOT / "runtime/frame.c"), "-o", str(exe)],
                            capture_output=True, text=True)
        self.assertEqual(cc.returncode, 0, cc.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.split(), ["ffffffff", "fff9", "00008000"])


if __name__ == "__main__":
    unittest.main()
