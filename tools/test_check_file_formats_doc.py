"""Tests for tools/check_file_formats_doc.py."""
import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import check_file_formats_doc as c  # noqa: E402

HDR = "STATIC_SIZE(struct FileHeader, 0x10);\nBLK_PLAYER = 6,\n"


class Check(unittest.TestCase):
    def test_match(self):
        doc = "`struct FileHeader` (0x10 bytes)\n| 6 | BLK_PLAYER | x |\n"
        self.assertEqual(c.problems(doc, HDR), [])

    def test_size_mismatch(self):
        doc = "`struct FileHeader` (0x12 bytes)\n| 6 | BLK_PLAYER | x |\n"
        self.assertEqual(len(c.problems(doc, HDR)), 1)

    def test_type_mismatch(self):
        doc = "`struct FileHeader` (0x10 bytes)\n| 7 | BLK_PLAYER | x |\n"
        self.assertEqual(len(c.problems(doc, HDR)), 1)

    def test_real_doc(self):
        r = subprocess.run([sys.executable, str(TOOLS / "check_file_formats_doc.py")],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)


if __name__ == "__main__":
    unittest.main()
