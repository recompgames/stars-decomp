"""Tests for tools/segment_gaps.py (uncovered code-segment ranges)."""
import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import check_inventory as ci  # noqa: E402
import segment_gaps as sg  # noqa: E402

FIX = TOOLS / "fixtures"


class Gaps(unittest.TestCase):
    def test_segment_gaps_fixture(self):
        rows = ci.read_rows(FIX / "gaps.csv")
        self.assertEqual(sg.gaps(rows, {1: 0x40}), [(1, 0x10, 0x1F), (1, 0x30, 0x3F)])

    def test_segment_gaps_overlap(self):
        rows = ci.read_rows(FIX / "gaps_overlap.csv")
        self.assertEqual(sg.gaps(rows, {1: 0x20}), [])

    def test_segment_without_rows_is_one_gap(self):
        self.assertEqual(sg.gaps([], {3: 0x10}), [(3, 0, 0xF)])

    def test_format(self):
        self.assertEqual(sg.fmt((2, 0x10, 0x1F)), "2:0x0010-0x001f 16")

    def test_real_segment_2_has_large_gap(self):
        csv, exe, nd = ROOT / "docs/functions.csv", ROOT / "original/stars26i.exe", ROOT / "build/nedump"
        if not (csv.exists() and exe.exists() and nd.exists()):
            self.skipTest("local inventory or original missing")
        segs = ci.parse_segments(subprocess.check_output([str(nd), "--segments", str(exe)], text=True))
        # Classified as data in docs/code-segment-data.md; the tool still reports it.
        total = sum(e - s + 1 for g, s, e in sg.gaps(ci.read_rows(csv), segs) if g == 2)
        self.assertGreaterEqual(total, 20000)


if __name__ == "__main__":
    unittest.main()
