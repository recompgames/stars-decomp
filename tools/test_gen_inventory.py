"""Tests for tools/gen_inventory.py (generated numbers in docs/inventory.md)."""
import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import gen_inventory as gi  # noqa: E402

HEADER = "seg,off,name,size,ret,callers,callees,apis,fp,origin,status,notes"


def rows(*lines):
    import csv
    import io
    return list(csv.DictReader(io.StringIO(HEADER + "\n" + "\n".join(lines) + "\n")))


FACTS = """\
COVER|1|100|60|10|30
COVER|34|200|120|0|80
X87|FLDCW|34|3
X87|FADD|1|2
X87|FADD|34|1
CSOVERRIDE|1:0x0010|JMP word ptr CS:[BX + 0x20]|read-only
CSOVERRIDE|34:0x0020|MOV word ptr CS:[0x5],AX|WRITE to code segment
CSOVERRIDE|count|2
MSG|USER.SENDMESSAGE|count|5
MSG|USER.SENDMESSAGE|10:0x0100|F|0x403|PUSH
MSG|USER.SENDMESSAGE|2:0x0300|F|0x403|PUSH
MSG|USER.SENDMESSAGE|2:0x0010|F|0x30|PUSH
MSG|USER.SENDMESSAGE|3:0x0001|F|dynamic|PUSH
MSG|USER.SENDMESSAGE|4:0x0001|F|0x50|PUSH
MSG|USER.SENDDLGITEMMESSAGE|count|1
MSG|USER.SENDDLGITEMMESSAGE|8:0x0001|F|0x415|PUSH
"""


class Pieces(unittest.TestCase):
    def test_parse_facts_groups_by_tag(self):
        f = gi.parse_facts(FACTS)
        self.assertEqual(len(f["MSG"]), 8)
        self.assertEqual(f["COVER"][1], ["34", "200", "120", "0", "80"])

    def test_message_aggregation_counts_and_orders_sites_numerically(self):
        total, by = gi.aggregate_messages(gi.parse_facts(FACTS)["MSG"], "USER.SENDMESSAGE")
        self.assertEqual(total, 5)
        self.assertEqual(sorted(by[0x403], key=gi.site_key), ["2:0x0300", "10:0x0100"])
        self.assertEqual(len(by[None]), 1)

    def test_messages_table_labels_every_name_unchecked(self):
        text = gi.render_messages(gi.parse_facts(FACTS)["MSG"])
        self.assertIn("| `0x30` | 1 | WM_SETFONT (name from memory, not checked against a source)", text)
        self.assertIn("BM_SETSTATE", text)       # candidate added in review for +3
        self.assertIn("| `0x50` | 1 | (no name recalled)", text)
        self.assertIn("| dynamic | 1 |", text)
        # example sites sorted by segment then offset, not by string
        self.assertIn("2:0x0300, 10:0x0100", text)
        for line in text.splitlines():
            if line.startswith("| `0x") and "no name recalled" not in line:
                self.assertIn(gi.UNCHECKED, line)

    def test_new_candidates_present(self):
        self.assertIn("BM_SETSTATE", gi.USER_CANDIDATES[3])
        self.assertIn("BM_SETSTYLE", gi.USER_CANDIDATES[4])
        self.assertIn("EM_GETTHUMB", gi.USER_CANDIDATES[14])

    def test_coverage_table_sums(self):
        r = rows("1,0x0000,A,10,far,,,,0,game,todo,", "34,0x0000,B,5,near,,,,0,crt,todo,",
                 "34,0x0010,RANGE_34_0010,7,none,,,,0,crt,todo,")
        text = gi.render_coverage(r, gi.parse_facts(FACTS)["COVER"])
        self.assertIn("| 1 | 1 | 0 | 100 | 60 | 10 | 30 |", text)
        self.assertIn("| 34 | 1 | 1 | 200 | 120 | 0 | 80 |", text)
        self.assertIn("| **all** | **2** | **1** | **300** | **180** | **10** | **110** |", text)

    def test_summary_accounts_for_segment_34_bytes(self):
        r = rows("34,0x0000,B,100,near,,,,0,crt,todo,", "34,0x0070,RANGE_34_0070,60,none,,,,1,crt,todo,")
        text = gi.render_summary(r, gi.parse_facts(FACTS)["COVER"])
        self.assertIn("in function runs 100; in RANGE rows 60; remaining (0x90/0x00 padding between rows) 40", text)
        self.assertIn("1 Ghidra functions + 1 `RANGE_*` rows", text)

    def test_x87_table(self):
        f = gi.parse_facts(FACTS)
        text = gi.render_x87(f["X87"], {"FPTAN": {31: 2, 34: 1}})
        self.assertIn("| FLDCW | seg 34 x3 |", text)
        self.assertIn("| FPTAN | none | seg 31 x2, seg 34 x1 |", text)
        self.assertIn("Ghidra decodes 6 x87 instructions", text)

    def test_raw_x87_scan(self):
        got = gi.raw_x87_scan({7: bytes([0, 0xD9, 0xF2, 0xD9, 0xF2, 0xD9, 0xFA]), 8: bytes([1, 2, 3])})
        self.assertEqual(got["FPTAN"], {7: 2})
        self.assertEqual(got["FSQRT"], {7: 1})
        self.assertEqual(got["FSCALE"], {})

    def test_cs_override_counts_writes(self):
        text = gi.render_csoverride(gi.parse_facts(FACTS)["CSOVERRIDE"])
        self.assertIn("override: 2 (records: 2), in 2 segments", text)
        self.assertIn("**1**", text)


class Blocks(unittest.TestCase):
    DOC = "intro\n<!-- GEN:a -->\nold\n<!-- /GEN:a -->\nprose\n<!-- GEN:b -->\nold b\n<!-- /GEN:b -->\ntail\n"

    def test_replace_only_marked_text_and_is_idempotent(self):
        once = gi.replace_blocks(self.DOC, {"a": "NEW A", "b": "NEW B"})
        self.assertEqual(once, "intro\n<!-- GEN:a -->\nNEW A\n<!-- /GEN:a -->\nprose\n<!-- GEN:b -->\nNEW B\n<!-- /GEN:b -->\ntail\n")
        self.assertEqual(gi.replace_blocks(once, {"a": "NEW A", "b": "NEW B"}), once)

    def test_missing_marker_is_an_error(self):
        with self.assertRaises(gi.GenError):
            gi.replace_blocks("no markers here", {"a": "x"})

    def test_replacement_text_with_backslashes_is_literal(self):
        out = gi.replace_blocks(self.DOC, {"a": r"C:\new \1"})
        self.assertIn(r"C:\new \1", out)


class Real(unittest.TestCase):
    @unittest.skipUnless(all((ROOT / p).exists() for p in ("docs/functions.csv", "re/facts.txt", "build/nedump", "original/stars26i.exe")),
                         "needs docs/functions.csv, re/facts.txt (Ghidra run), build/nedump and the exe")
    def test_generated_blocks_in_inventory_are_current(self):
        r = subprocess.run([sys.executable, str(TOOLS / "gen_inventory.py"), "--check"], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
