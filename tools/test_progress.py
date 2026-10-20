#!/usr/bin/env python3
"""Tests for tools/progress.py (fixture: tools/fixtures/progress.csv)."""
import contextlib
import io
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import progress as p  # noqa: E402

FIX = str(HERE / "fixtures" / "progress.csv")


def run(args):
    buf, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(err):
        rc = p.main(args)
    return rc, buf.getvalue(), err.getvalue()


class ProgressTest(unittest.TestCase):
    def test_exact_counts_and_percentages(self):
        rc, out, _ = run(["--csv", FIX])
        self.assertEqual(rc, 0)
        self.assertEqual(out.splitlines(), [
            "seg 1: total 4, todo 2 (50.0%), decompiled 1 (25.0%), tested 1 (25.0%), unused 0 (0.0%)",
            "seg 2: total 3, todo 1 (33.3%), decompiled 0 (0.0%), tested 1 (33.3%), unused 1 (33.3%)",
            "total: total 7, todo 3 (42.9%), decompiled 1 (14.3%), tested 2 (28.6%), unused 1 (14.3%)",
        ])

    def test_seg_filter(self):
        rc, out, _ = run(["--csv", FIX, "--seg", "2"])
        self.assertEqual(rc, 0)
        self.assertEqual(out.splitlines(), [
            "seg 2: total 3, todo 1 (33.3%), decompiled 0 (0.0%), tested 1 (33.3%), unused 1 (33.3%)"])

    def test_seg_with_no_functions(self):
        rc, out, _ = run(["--csv", FIX, "--seg", "9"])
        self.assertEqual(rc, 0)
        self.assertEqual(out.splitlines(), [
            "seg 9: total 0, todo 0 (0.0%), decompiled 0 (0.0%), tested 0 (0.0%), unused 0 (0.0%)"])

    def test_always_exits_zero(self):
        rc, out, err = run(["--csv", "/nonexistent/functions.csv"])
        self.assertEqual(rc, 0)
        self.assertIn("cannot read", err)
        rc, _, err = run(["--bogus"])
        self.assertEqual(rc, 0)
        rc, _, err = run(["--seg", "x"])
        self.assertEqual(rc, 0)

    def test_unknown_status_is_reported_not_dropped(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "f.csv"
            f.write_text("seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes\n"
                         "1,0x0,a,1,far,,,,0,game,weird,\n")
            rc, out, _ = run(["--csv", str(f)])
        self.assertEqual(rc, 0)
        self.assertIn("weird 1 (100.0%)", out)

    def test_readable_status_is_counted_after_tested(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "f.csv"
            f.write_text("seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes\n"
                         "1,0x0,a,1,far,,,,0,game,readable,\n1,0x10,b,1,far,,,,0,game,tested,\n")
            rc, out, _ = run(["--csv", str(f)])
        self.assertEqual(out.splitlines()[0], "seg 1: total 2, todo 0 (0.0%), decompiled 0 (0.0%), "
                         "tested 1 (50.0%), readable 1 (50.0%), unused 0 (0.0%)")

    def test_readability_flag_appends_table(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "src"
            src.mkdir()
            (src / "seg01.c").write_text("DECOMP(1, 0x0000, f, FAR_CDECL)\nvoid f(void)\n{\n  drd8(1);\n}\n")
            rc, base, _ = run(["--csv", FIX])
            rc, out, _ = run(["--csv", FIX, "--readability", "--src", str(src)])
        self.assertEqual(rc, 0)
        self.assertTrue(out.startswith(base))
        rest = out[len(base):].splitlines()
        self.assertEqual(rest[0].split(), ["seg", "functions", "raw", "placeholders", "magic", "untagged"])
        self.assertEqual(rest[1].split(), ["1", "1", "1", "0", "0", "1"])


if __name__ == "__main__":
    unittest.main()
