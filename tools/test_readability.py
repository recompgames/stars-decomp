#!/usr/bin/env python3
"""Tests for tools/readability.py (metrics per DECOMP function and per segment)."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import readability as R  # noqa: E402

SRC = """\
/* Fixture. */
/* A game-level summary.
 * 1:0x0000 FUN_1000_0000, FAR_CDECL fn_01_0000 in a comment */
DECOMP(1, 0x0000, fx_a, FAR_CDECL)
uint16_t fx_a(uint16_t p)
{
  uint16_t a = drd16(0x10);          /* wr8(1, 2, 3) and FUN_1000_1111 in a comment */
  wr16(MEM_SEL_DGROUP, p, a);
  uint16_t local_4 = a;
  const char *s = "DAT_1234 0x77";
  return (uint16_t)(a << 0x4) + 0xFF + 0x1 + 0x0 + 0xFFFF;
}

/* No tag line here. */
DECOMP(1, 0x0010, fx_b, FAR_CDECL)
uint16_t fx_b(uint16_t p)
{
  return (uint16_t)(p & 0x1e) + LINN(0x20) + (uint16_t)LINN(p) + g_DAT_1110_0042;
}
"""


class ReadabilityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = Path(self.tmp.name) / "src"
        self.src.mkdir()
        (self.src / "seg01.c").write_text(SRC)

    def tearDown(self):
        self.tmp.cleanup()

    def test_readability_counts(self):
        fns = {f.name: f for f in R.metrics(self.src)}
        a = fns["fx_a"]
        # drd16 + wr16 are raw; the wr8 in the comment is not
        self.assertEqual(a.raw, 2)
        # local_4 in code; FUN_ in a comment and DAT_ in a string are ignored
        self.assertEqual(a.placeholders, 1)
        # 0x10 only: 0/1/0xFF/0xFFFF are exempt, 0x4 is a power of two used as a shift, 0x77 is in a string
        self.assertEqual(a.magic, 1)
        self.assertTrue(a.tagged)
        self.assertEqual((a.seg, a.off), (1, 0))

    def test_second_function(self):
        b = {f.name: f for f in R.metrics(self.src)}["fx_b"]
        self.assertEqual(b.raw, 1)            # LINN(0x20) numeric; LINN(p) is not
        self.assertEqual(b.placeholders, 1)   # g_DAT_...
        self.assertEqual(b.magic, 2)          # 0x1e, 0x20 (0x0042 is part of an identifier)
        self.assertFalse(b.tagged)

    def test_per_segment_table_and_csv(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(R.main(["--src", str(self.src)]), 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0].split(), ["seg", "functions", "raw", "placeholders", "magic", "untagged"])
        self.assertEqual(lines[1].split(), ["1", "2", "3", "2", "3", "1"])
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(R.main(["--src", str(self.src), "--per-function"]), 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0], "seg,off,name,raw,placeholders,magic,tagged")
        self.assertEqual(lines[1], "1,0x0000,fx_a,2,1,1,1")
        self.assertEqual(lines[2], "1,0x0010,fx_b,1,1,2,0")

    def one(self, text):
        (self.src / "seg01.c").write_text(text)
        return R.metrics(self.src)[0]

    def test_call_orig_address_is_a_magic_constant(self):
        f = self.one("DECOMP(1, 0x0000, fx_c, FAR_CDECL)\nvoid fx_c(void)\n{\n  CALL_ORIG(1, 0x0010, FAR_CDECL, 0);\n}\n")
        self.assertEqual(f.magic, 1)

    def test_shift_exemption_is_only_for_powers_of_two(self):
        f = self.one("DECOMP(1, 0x0000, fx_c, FAR_CDECL)\nvoid fx_c(void)\n{\n  x = a << 0x4; y = b >> 0x3; z = c << 0x2;\n}\n")
        self.assertEqual(f.magic, 1)   # only 0x3 (not a power of two); 0x4 and 0x2 are

    def test_power_of_two_not_used_as_shift_is_magic(self):
        f = self.one("DECOMP(1, 0x0000, fx_c, FAR_CDECL)\nvoid fx_c(void)\n{\n  x = a & 0x8;\n}\n")
        self.assertEqual(f.magic, 1)

    def test_placeholder_in_signature_is_counted(self):
        f = self.one("DECOMP(1, 0x0000, fn_01_0000, FAR_CDECL)\nvoid fn_01_0000(void)\n{\n}\n")
        self.assertEqual(f.placeholders, 1)

    def test_all_drd_dwr_helpers_are_raw(self):
        f = self.one("DECOMP(1, 0x0000, fx_c, FAR_CDECL)\nvoid fx_c(void)\n{\n"
                     "  drd_f64(p); dwr_f64(p, v); drd32(p); dwr8(p, 1); rd32(s, p); wr32(s, p, v);\n}\n")
        self.assertEqual(f.raw, 6)


if __name__ == "__main__":
    unittest.main()
