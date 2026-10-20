#!/usr/bin/env python3
"""The wasm build has no emulator: CALL_ORIG and rt_call_far (runtime/frame.h) must abort with a
clear message there. Compiled natively with -D__EMSCRIPTEN__ to select the wasm branch."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless(shutil.which("clang"), "clang not installed")
class FrameWasmTest(unittest.TestCase):
    def run_snippet(self, body):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            src.write_text('#include "runtime/frame.h"\nint main(void) {\n' + body + "\n  return 0;\n}\n")
            exe = Path(tmp) / "t"
            cc = subprocess.run(
                ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-D__EMSCRIPTEN__", "-I", str(ROOT), str(src),
                 str(ROOT / "runtime/frame.c"), str(ROOT / "runtime/mem.c"), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(cc.returncode, 0, cc.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_rt_call_far_aborts_with_a_message(self):
        r = self.run_snippet("uint16_t a[1] = {1};\n  rt_call_far(0x00030010u, FAR_PASCAL, a, 1);")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("rt_call_far", r.stderr)
        self.assertIn("wasm", r.stderr)

    def test_call_orig_aborts_with_a_message(self):
        r = self.run_snippet("CALL_ORIG(3, 0x10, FAR_PASCAL, 0);")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not decompiled: 3:0010", r.stderr)


if __name__ == "__main__":
    unittest.main()
