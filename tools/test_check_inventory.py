"""Tests for tools/check_inventory.py (docs/functions.csv validator).

Fixtures in tools/fixtures/ use two tiny segments: segment 1 (length 100) and segment 2 (length 100),
export WNDPROC at 1:0x0000 and entry point at 2:0x0000.
"""
import os
import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import check_inventory as ci  # noqa: E402

FIX = TOOLS / "fixtures"
SEGS = {1: 100, 2: 100}
EXPORTS = [("WNDPROC", 1, 0x0000)]
ENTRY = (2, 0x0000)


def run(name, **kw):
    args = dict(segments=SEGS, exports=EXPORTS, entry=ENTRY)
    args.update(kw)
    return ci.check(ci.read_rows(FIX / name), **args)


class Fixtures(unittest.TestCase):
    def test_good_fixture_has_no_errors(self):
        self.assertEqual(run("good.csv"), [])

    def test_overlap_is_an_error(self):
        errs = run("overlap.csv")
        self.assertTrue(any("overlap" in e for e in errs), errs)

    def test_missing_export_is_an_error(self):
        errs = run("missing_export.csv")
        self.assertTrue(any("export WNDPROC" in e for e in errs), errs)

    def test_bad_status_is_an_error(self):
        errs = run("bad_status.csv")
        self.assertTrue(any("status" in e and "finished" in e for e in errs), errs)

    def test_segment_without_function_is_an_error(self):
        errs = run("good.csv", segments={1: 100, 2: 100, 3: 100})
        self.assertTrue(any("segment 3" in e for e in errs), errs)

    def test_function_past_segment_end_is_an_error(self):
        errs = run("good.csv", segments={1: 40, 2: 100})
        self.assertTrue(any("beyond" in e for e in errs), errs)

    def test_missing_entry_function_is_an_error(self):
        errs = run("good.csv", entry=(2, 0x001A))
        self.assertTrue(any("entry" in e for e in errs), errs)

    def test_bad_origin_is_an_error(self):
        rows = ci.read_rows(FIX / "good.csv")
        rows[0]["origin"] = "dos"
        self.assertTrue(any("origin" in e for e in ci.check(rows, SEGS, EXPORTS, ENTRY)))

    def test_duplicate_address_is_an_error(self):
        rows = ci.read_rows(FIX / "good.csv")
        rows.append(dict(rows[0]))
        self.assertTrue(any("duplicate" in e for e in ci.check(rows, SEGS, EXPORTS, ENTRY)))

    def test_bad_header_is_an_error(self):
        bad = FIX / "bad_header.csv"
        self.assertRaises(ci.InventoryError, ci.read_rows, bad)

    def test_dangling_callee_is_an_error(self):
        errs = run("dangling_ref.csv")
        self.assertTrue(any("callees entry 1:0x0050 has no row" in e for e in errs), errs)

    def test_dangling_caller_is_an_error(self):
        rows = ci.read_rows(FIX / "good.csv")
        rows[0]["callers"] = "2:0x0099"
        self.assertTrue(any("callers entry 2:0x0099 has no row" in e for e in ci.check(rows, SEGS, EXPORTS, ENTRY)))

    def test_malformed_reference_is_an_error(self):
        rows = ci.read_rows(FIX / "good.csv")
        rows[0]["callees"] = "helper"
        self.assertTrue(any("malformed callees" in e for e in ci.check(rows, SEGS, EXPORTS, ENTRY)))

    def test_valid_references_pass(self):
        self.assertEqual(run("good.csv"), [])  # good.csv has 1:0x0030 as callee and 1:0x0000 as caller

    def test_min_size_guard_catches_unbridged_padding_gap(self):
        # PLANETWNDPROC-like row whose size covers only the first fragment (the old bug: 67 instead of 0x7e0).
        rows = ci.read_rows(FIX / "good.csv")
        rows[0]["size"] = "32"
        errs = ci.check(rows, SEGS, EXPORTS, ENTRY, min_sizes={(1, 0): 0x40})
        self.assertTrue(any("expected at least 64" in e for e in errs), errs)
        self.assertEqual(ci.check(rows, SEGS, EXPORTS, ENTRY, min_sizes={(1, 0): 0x20}), [])

    def test_quoted_newline_in_notes_is_read(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "n.csv"
            p.write_text(",".join(ci.HEADER) + '\n1,0x0000,WNDPROC,32,far,,,,0,game,todo,"line1\nline2, ""q"""\n', encoding="utf-8")
            rows = ci.read_rows(p)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["notes"], 'line1\nline2, "q"')

    def test_nedump_parsers(self):
        segs = ci.parse_segments("segments:\n    1 CODE  file=0x001140 length=  7938 min_alloc=  7938\n"
                                 "   35 DATA  file=0x054e40 length= 26248 min_alloc= 26248\n")
        self.assertEqual(segs, {1: 7938})
        # min_alloc larger than the file length (segment 32 of stars26i.exe) is the memory size
        segs = ci.parse_segments("    32 CODE  file=0x1 length= 30065 min_alloc= 30066 flags=0x1d50\n")
        self.assertEqual(segs, {32: 30066})
        exps = ci.parse_exports("entries:\n      4  27:0x0000 RESEARCHDLG\n      9   9:0xb96a X\n")
        self.assertEqual(exps, [("RESEARCHDLG", 27, 0), ("X", 9, 0xB96A)])


# Recovered entry points in segment 34: (wrong, late entry) -> correct entry. The MS C far prologues are
# 8C D8 90 45 55 8B EC / 1E 58 90 45 55 8B EC / 45 55 8B EC; the entry is the first byte, not the PUSH BP.
ENTRY_FIXES = {0x01C4: 0x01C0, 0x01D5: 0x01D1, 0x0896: 0x0892, 0x08BA: 0x08B6, 0x094A: 0x0946, 0x15DE: 0x15DA,
               0x1C7E: 0x1C7A, 0x263A: 0x2636, 0x1DB5: 0x1DB4, 0x2D61: 0x2D60, 0x2DCF: 0x2DCE}


class RealInventory(unittest.TestCase):
    """Runs the CLI on docs/functions.csv when it, nedump and the exe exist (all are git-ignored or built)."""

    @unittest.skipUnless((ROOT / "docs/functions.csv").exists() and (ROOT / "build/nedump").exists()
                         and (ROOT / "original/stars26i.exe").exists(), "inventory/nedump/exe not present")
    def test_real_inventory_passes(self):
        self._cli()

    def _cli(self):
        r = subprocess.run([sys.executable, str(TOOLS / "check_inventory.py"), str(ROOT / "docs/functions.csv")],
                           cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def rows(self):
        path = Path(os.environ.get("INVENTORY_CSV", ROOT / "docs/functions.csv"))
        return {(int(r["seg"]), int(r["off"], 16)): r for r in ci.read_rows(path)}

    @unittest.skipUnless((ROOT / "docs/functions.csv").exists(), "inventory not present")
    def test_planetwndproc_size_covers_whole_body(self):
        r = self.rows()[(9, 0x0000)]
        self.assertEqual(r["name"], "PLANETWNDPROC")
        self.assertGreaterEqual(int(r["size"]), 0x7E0, "body runs to 9:0x07df; padding gaps must be bridged")

    @unittest.skipUnless((ROOT / "docs/functions.csv").exists(), "inventory not present")
    def test_seg34_prologue_entries_are_exact(self):
        rows = self.rows()
        for wrong, right in ENTRY_FIXES.items():
            self.assertIn((34, right), rows, f"34:0x{right:04x} missing")
            self.assertNotIn((34, wrong), rows, f"34:0x{wrong:04x} is the late (wrong) entry")

    @unittest.skipUnless((ROOT / "docs/functions.csv").exists(), "inventory not present")
    def test_2d46_is_one_function(self):
        rows = self.rows()
        self.assertIn((34, 0x2D46), rows)
        self.assertGreater(int(rows[(34, 0x2D46)]["size"]), 1)
        self.assertNotIn((34, 0x2D47), rows)

    @unittest.skipUnless((ROOT / "docs/functions.csv").exists(), "inventory not present")
    def test_thunk_037a_has_a_row(self):
        rows = self.rows()
        self.assertIn((34, 0x037A), rows)
        self.assertIn("34:0x11ae", rows[(34, 0x037A)]["notes"])


if __name__ == "__main__":
    unittest.main()
