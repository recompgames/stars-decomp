# Tooling: rebuild the local analysis outputs

Everything under `re/`, `docs/functions.csv`, `docs/inventory.md` and `tests/golden/` is derived from
the original game and is git-ignored (docs/PLAN.md decision 8). This page says how to regenerate
it from your own licensed copy. Run every command from the repository root.

## 1. Prerequisites

| Tool | Used for | Install |
|------|----------|---------|
| clang, make | Runtime, harness, tests | Xcode command line tools |
| Python 3 (stdlib only) | `tools/*.py` generators, lint, progress | system or Homebrew |
| Unicorn, Capstone | Run and disassemble the original x86 code in the harness | `brew install unicorn capstone` |
| OpenJDK 21 | Runs Ghidra and `ghidra/*.java` | `brew install openjdk@21` |
| Ghidra 12.1 | Disassembler and decompiler; `analyzeHeadless` must be on PATH | https://ghidra-sre.org |

`make doctor` checks all of them and fails if one is missing. The Makefile finds Unicorn and Capstone
through `brew --prefix`; on Linux override `UNICORN_PREFIX` and `CAPSTONE_PREFIX`.

## 2. Original files

Put the Stars! 2.6i distribution in `original/` (git-ignored, read-only). Check it first:

```
shasum -a 256 original/stars26i.exe   # must be 5770dade2db0e11918631d5c128c0bf7ab48e371f7b2267a4ff02531bb8e198e
```

Registration serials go in `private/` and never in a script, log or commit. `run_original` can type a
secret from an environment variable (`type $NAME`).

## 3. Build and test

```
make            # runtime, harness, tests, build/nedump, build/run_original
make test       # lint + every C test + the Python tool tests (needs original/stars26i.exe)
python3 tools/progress.py [--seg N]
```

`make` also generates `runtime/win16_api.inc` from the vendored Wine spec files
(`tools/gen_imports.py`; provenance in `tools/winespec/SOURCE.md`) and `build/registry.c`
(`tools/gen_registry.py`).

## 4. Ghidra project and function inventory

Ghidra loads the file with the "New Executable (NE)" loader as `x86:LE:16:Protected Mode`. Addresses in
our files are NE numbering (`seg:off`, seg 1-35), not Ghidra's: NE segment n is Ghidra segment
`0x1000 + 8*(n-1)`.

First import (about 75 s):

```
mkdir -p re/ghidra
analyzeHeadless re/ghidra stars -import original/stars26i.exe -overwrite -scriptPath ghidra \
  -postScript Label.java -postScript ExportInventory.java \
  -postScript ExportDecomp.java -postScript InventoryFacts.java
```

Later runs on the existing project (the manual `origin`, `status` and `notes` columns of
`docs/functions.csv` are kept; a vanished row that has manual values aborts the export):

```
analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis -scriptPath ghidra \
  -postScript Label.java -postScript ExportInventory.java
```

| Script | Writes | Notes |
|--------|--------|-------|
| `ghidra/Label.java` | names in the Ghidra project | Makes every NE export and the entry point a named function. Idempotent. |
| `ghidra/ExportInventory.java` | `docs/functions.csv` | One row per function: seg, off, name, size, callers, callees, apis, fp, origin, status, notes. |
| `ghidra/ExportDecomp.java` | `re/decomp/segNN/<off>_<name>.c` | Ghidra pseudo-C, reference only: the diff tests are the arbiter. |
| `ghidra/InventoryFacts.java` | `re/facts.txt` | Static facts (DOS3Call sites, SendMessage ids, window classes, ...). |
| `ghidra/ExportGlobals.java` | `re/globals.csv` | Labelled DGROUP data items (segment 35). |

Then validate and regenerate:

```
make build/nedump
python3 tools/check_inventory.py docs/functions.csv     # inventory against the real NE image
python3 tools/gen_inventory.py                          # rewrites the GEN blocks in docs/inventory.md
python3 tools/gen_inventory.py --check                  # exit 1 if a GEN block is stale
```

`docs/inventory.md` mixes hand-written prose with generated blocks between `<!-- GEN:name -->` and
`<!-- /GEN:name -->`; never edit inside those markers.

### Globals

`src/globals.h` is generated, never hand-edited. To add or change a global, label it in Ghidra with its
type first (Ghidra GUI, or the GhidraMCP plugin from a Claude session), then:

```
analyzeHeadless re/ghidra stars -process stars26i.exe -noanalysis -scriptPath ghidra \
  -postScript ExportGlobals.java
python3 tools/gen_globals.py          # re/globals.csv -> src/globals.h
```

See docs/CONVENTIONS.md for the rules on 16/32-bit globals.

## 5. Resources and screen captures

```
build/nedump --segments --relocs --imports --exports --resources original/stars26i.exe
build/nedump --dump-resources re/resources original/stars26i.exe   # .bin raw, .bmp bitmaps, .txt dialogs/menus
```

`build/run_original` runs the unmodified game on the Win16 runtime under Unicorn, driven by a script
(`tests/scripts/*.txt`; the script language is documented at the top of `harness/run_original.c`):

```
build/run_original --strict --script tests/scripts/boot.txt --dump re/frames
build/run_original --list-imports
```

`--dump DIR` writes 24-bit `.bmp` screenshots. Exit codes: 0 ok, 1 fault or bad usage, 2 timeout, 3
unimplemented API in `--strict`. `runtime/tests/test_visual_chrome.c` writes `re/frames/NAME.ppm` for
review. Captures show real screens: look at each one before sharing it, and never commit them.

## 6. Writing a decompiled function and its test

1. Pick a row in `docs/functions.csv` (`python3 tools/progress.py` shows what is left; `bd ready` shows
   tracked work). Read `re/decomp/segNN/<off>_<name>.c` for a starting point.
2. Write it in `src/segNN.c` following docs/CONVENTIONS.md. Each function is preceded by
   `DECOMP(seg, 0xOFF, name, CONV)` (CONV: FAR_PASCAL, FAR_CDECL, NEAR_PASCAL, NEAR_CDECL, REGISTER) and
   uses fixed-width types only. Functions whose locals have their address taken mirror the original
   stack frame with `FRAME_ENTER` / `FRAME_LEAVE` (`runtime/frame.h`).
3. Add scenarios to `tests/scenarios/test_segNN.c`. `DIFF(seg, off, conv, args...)` runs the original
   function and the C one from the same memory snapshot and compares return registers, all memory
   outside the callee's own frame, and the ordered Win16 API call log. A worked example is
   `tests/scenarios/test_seg02.c`.
4. `make test` (this also runs `tools/lint_src.py`), then set the function's `status` in
   `docs/functions.csv`.

## 7. What each generated file depends on

| Output | Generator | Inputs |
|--------|-----------|--------|
| `runtime/win16_api.inc` | `tools/gen_imports.py` | `tools/winespec/*.spec` |
| `build/registry.c` | `tools/gen_registry.py` | `src/*.c`, `docs/functions.csv` |
| `src/globals.h` | `tools/gen_globals.py` | `re/globals.csv` |
| `docs/functions.csv` | `ghidra/ExportInventory.java` | Ghidra project |
| `docs/inventory.md` GEN blocks | `tools/gen_inventory.py` | `docs/functions.csv`, `re/facts.txt`, the exe, `build/nedump` |
| `re/decomp/`, `re/facts.txt`, `re/globals.csv` | `ghidra/*.java` | Ghidra project |
