# Stars! 2.6i decompilation: tools and plan

Goal: decompile `original/stars26i.exe` (Stars! 2.6i, Win16) into C source that a person can read,
understand and modify, and that still behaves exactly like the original. Every function is checked
against the original machine code, so readability work never changes behaviour.

A function is finished when both hold:
1. **Faithful:** its differential tests pass against the original code running under an x86 emulator.
2. **Readable:** it uses named globals, struct fields, enums and named flag constants instead of raw
   offsets and magic numbers, has meaningful names for itself, its parameters and its locals, and its
   comment explains what it does in game terms (see §3, decision 3, and the metrics in §5).

Work is tracked in beads (`bd ready`, prefix `stars-decomp`). This document is the shared reference
that the bd tasks point to.

## 1. What the binary is

Measured on 2026-10-15 from `original/stars26i.exe` (SHA-256 `5770dade…198e`, 3 216 832 bytes).

| Fact | Value |
|------|-------|
| Format | NE (16-bit Windows 3.x EXE). Module `STARS!`, description "Stars! Intergalactic Conquest Game", "Copyright © 1995-1996 Jeff Johnson && Jeff McBride" |
| Toolchain | Microsoft LINK 5.60 (NE header linker version). Microsoft C runtime markers present (`<<NMSG>>`, `R6000`…`R6021`). Exact compiler version not yet confirmed. |
| Segments | 35: segments 1–34 are code, segment 35 is DGROUP (26 248 bytes; heap 8 192, stack 16 384). 715 684 bytes of segment data in total. Alignment shift 6. |
| Entry point | CS:IP = segment 34, offset 0x001A. 53 entry-table ordinals, all exported by name (window and dialog procedures). |
| Imports | 197 functions: KERNEL 44, USER 107, GDI 41, COMMDLG 3, TOOLHELP 1, WIN87EM 1. All by ordinal. Full list in Appendix A. |
| Not imported | CTL3DV2, WAVEMIX, MMSYSTEM, `LoadLibrary`, `GetProcAddress`. The three DLLs in `original/` are not used by the game. `hyprfind.dll` is WinHelp's search DLL (it imports CTL3DV2). |
| Relocation records | 390 internal, 1 028 import-by-ordinal, 0 import-by-name, 810 OSFIXUP |
| x87 floating point | The 810 OSFIXUP sites already contain native x87 code (`9B D8..DF`, `9B 26/2E/36 …`, `90 9B`). Windows turns them into emulator interrupts only on machines without an FPU. Decompile them as native x87; loaders must ignore OSFIXUP records. All transcendental ops (FPTAN, FPATAN, F2XM1, FYL2X, FSCALE, FPREM, FRNDINT, FSQRT) and all FLDCW/FNSTCW are in segment 34 (C runtime). Game segments use basic FP ops only. |
| Resources | 38 BITMAP (2 376 000 bytes), 36 DIALOG, 1 MENU (`STARSMENU`), 2 ACCELERATOR (116, 1080), 10 ICON + 10 GROUP_ICON, 11 CURSOR + 11 GROUP_CURSOR, custom types 10000/10002/10004 (IDs 10001/10003/10005; 4 352 / 51 776 / 19 904 bytes; contents unknown) |
| Game files | Strings `%s.xy`, `%s.hst`, `%s.x1` are present. Any file the decompiled code writes must stay byte-compatible with the original game. |

## 2. Tools

| # | Tool | Role | Status on this Mac |
|---|------|------|--------------------|
| 1 | Ghidra 12.1 (`analyzeHeadless`, `ghidraRun`) | Disassembler and decompiler. Loads this file with the "New Executable (NE)" loader as `x86:LE:16:Protected Mode` (verified). Ships Win16 symbol files for all six imported modules (`Ghidra/Features/Base/data/symbols/win16/*.exports`). | installed |
| 2 | GhidraMCP | Interactive queries and renames from Claude sessions (needs the Ghidra GUI open with the program loaded). | installed |
| 3 | OpenJDK 21 | Runs Ghidra and our Java GhidraScripts. | installed |
| 4 | Unicorn 2.1.4, C library | Executes the original 16-bit code in the hybrid harness (the test oracle). Verified: real-mode x87 (`FNINIT` control word 0x037F, round-to-even), and 16-bit protected mode with GDT descriptors based above 1 MB, including selector `+8` stepping. | installed (Homebrew) |
| 5 | Capstone 5.0.9, C library | Disassembly in harness traces and failure reports. | installed |
| 6 | Apple clang, LLVM 20/21 | Native builds of the runtime, harness and decompiled code. | installed |
| 7 | Python 3 (stdlib only) | Code generators, lint and progress reports. | installed (3.14.8) |
| 8 | Wine 16-bit `.spec` files | Ordinal → name, calling convention and argument list for every import. The harness needs the argument bytes to pop for Pascal calls (e.g. `USER.1 MessageBox(word str str word)` pops 12). From wine-mirror/wine commit `5b6b85421c2a2161728b2a55e9f09271770a3eb0` (LGPL-2.1); provenance in `tools/winespec/SOURCE.md`. | vendored in `tools/winespec/` |
| 9 | Berkeley SoftFloat 3e (BSD-3) | Exact 80-bit x87 arithmetic, needed only if the C runtime runs the FPU at 64-bit mantissa precision. | vendor only if needed |

Tools in the repo:

| Tool | Role | Status |
|------|------|--------|
| `nedump` (`tools/nedump.c`, `runtime/ne.c`) | Prints NE headers, segments, relocations, imports and exports. Dumps resources (dialogs and menus as text, bitmaps as `.bmp`). | built |
| Ghidra scripts (`ghidra/*.java`) | Apply names and types, then export the function inventory, call graph and per-function decompiled C. | built |
| Hybrid harness (`harness/`, C + Unicorn) | Runs original code on the Win16 API stubs in `runtime/` and the same memory image, and diff-tests decompiled C against the original. | built |
| Generators (`tools/gen_*.py`) | Import table, globals, inventory and registry from the spec files, Ghidra exports and `src/`. | built |
| `tools/lint_src.py` | Checks `src/` against `docs/CONVENTIONS.md`. | built; readability checks to add (§5) |
| `tools/progress.py` | Per-segment progress from `docs/functions.csv`. | built; readability columns to add (§5) |

Not needed:
- IDA Pro / Hex-Rays: the Hex-Rays decompiler does not support 16-bit x86.
- A floating-point "unpatcher": the OSFIXUP sites are already native x87 (see §1).
- Microsoft Visual C++ 1.5x: needed only for a byte-matching recompile, which is not a goal.
- Reko: possible second-opinion decompiler. Its feature list mentions NE, but that is unverified on this binary. Evaluate it only if Ghidra output is blocked.

## 3. Approach

Each function goes through two passes. Both are diff-tested against the same oracle, so the second
pass is a behaviour-preserving refactor.

```
original/stars26i.exe ──► Ghidra (names, types, pseudo-C)
                                   │
                                   ▼
          pass 1: faithful C  (src/segNN.c, offsets allowed)  ──► diff test ◄── original x86 in Unicorn
                                   │                                  ▲
          data recovery: globals, structs, enums, file formats        │ same memory image,
                                   │                                  │ same Win16 API call log
                                   ▼                                  │
          pass 2: readable C  (named data, typed access, game comments) ──┘
```

Key decisions:

1. **Keep the original segmented memory model underneath.** Far pointers stay 32-bit `selector:offset`
   values. `LIN(fp)` turns one into a host pointer through an emulated descriptor table (GDT, ring 0,
   `__AHINCR` = 8, `__AHSHIFT` = 3). Near pointers stay 16-bit offsets into DGROUP, with the local heap
   inside DGROUP. Handles stay 16-bit. The reasons:
   - Struct layouts and memory images stay identical to the original on every host, including 64-bit
     native test builds, where a C pointer is 8 bytes and could not stand in for a 4-byte far pointer.
   - The harness maps the memory arena into Unicorn (`uc_mem_map_ptr`), with the descriptor table
     inside the arena. Original x86 code and decompiled C then run on one shared memory image, which
     is what makes per-function diff tests exact.
2. **Hybrid execution while decompiling.** In native test builds, a call to a function not decompiled
   yet falls through to Unicorn, which runs the original code. Segments can be decompiled in any order.
3. **Readable code hides the memory model.** Pass-2 code never spells a raw offset. It reaches data
   through named globals and typed accessors declared in `src/stars.h` and `src/globals.h`, which
   compile to the same reads and writes as pass 1. For example, pass 1's
   `(rd8(seg, bx + 1) & 0x1e) >> 1` becomes `map_object_owner(obj)`, and `drd16(0x4b84)` becomes
   `g_selected_planet`. The exact form of the accessors (inline functions, macros or packed structs
   read through `LIN`) is fixed in `docs/CONVENTIONS.md` by the first data-recovery task.
4. **Comments explain the game, not the machine.** A function's comment says what it does and why, in
   game terms. One tag line keeps the address, the Ghidra name and the calling convention (for example
   `5:0x0474 FUN_1020_0474, FAR CDECL`). Instruction-level notes (`ENTER`, `[BP+6]`) belong in pass 1
   only and are removed in pass 2 unless they explain a quirk the C must reproduce.
5. **Win16 API stubs only as far as the tests need.** The runtime implements the imported functions
   that decompiled code calls, so the harness can run them. It does not aim for pixel-exact rendering
   or a complete Windows 3.1 implementation. The API call log is part of each diff test.
6. **Floating point.** The x87 precision-control setting the C runtime uses decides the approach (see
   `docs/CONVENTIONS.md`, "Floating point"). With 53-bit precision, C `double` is exact for the basic
   ops the game segments use. With 64-bit precision, use SoftFloat 3e `extF80`. The diff tests settle
   it either way.

Owner decisions:

7. **No Windows 3.11 media.** There is no DOSBox-X reference environment, so tasks that depend on
   reference screenshots are out of scope. Golden game files come from the original running on the
   harness.
8. **Public repository, decompilation source included.** The GitHub repo is public. It holds the plan,
   tools, the Win16 API stubs, the harness, the decompiled source (`src/`) and diff-test scenarios. The
   original files and registration serials stay local and git-ignored (`original/`, `private/`), and so
   do the derived reverse-engineering outputs (`re/`, `docs/functions.csv`, `docs/inventory.md`). The
   counts that `tools/progress.py` prints may be published; the inventory itself is not.
9. **Golden master is turn generation only.** The whole-program check runs turn generation headless on
   game files. It does not need the full user interface to run under the harness, so the API stubs
   only have to cover what turn generation and the per-function tests reach.

## 4. Test-driven development

Every task follows red → green → refactor: the test is written first and seen to fail, then the code
makes it pass. There are three kinds of oracle, from cheapest to most expensive:

| Oracle | Checks | Where |
|--------|--------|-------|
| Static facts | Parsers and loaders reproduce the numbers in §1 exactly (segment count, import counts, 810 OSFIXUP records, resource IDs). | `runtime/tests/` |
| Differential (per function) | From one memory snapshot, run the original function in Unicorn and the decompiled C function. Return registers, every byte of memory outside the callee's own stack frame, and the ordered Win16 API call log must all match. Pure functions are also fuzzed with seeded random arguments. Red step: before the C function exists, the driver reports `MISSING` and fails. The same tests must stay green through pass 2. | `harness/`, `tests/scenarios/` |
| Golden master (turn generation) | Game files produced by the original (under the harness) and by the decompiled build are byte-identical for the same inputs and seeds. This is the safety net for pass-2 refactors that cross function boundaries. | `tests/golden/` |

Win16 API behaviour is specified from the Windows 3.1 SDK documentation, with Wine's 16-bit
implementation as a behavioural reference.

Conventions shared by all tasks:
- `make test` at the repo root builds every native test with clang and runs it. A non-zero exit
  means failure. C tests use a minimal `tests/test.h` (CHECK macros) with no framework.
- Test file names are `test_<topic>.c` next to the module (`runtime/tests/`, `harness/tests/`).
- No commits are made by agents. Changes are left in the working tree.

## 5. Phases, milestones and readability metrics

| Phase | Delivers | Milestone |
|-------|----------|-----------|
| 1. Foundation and static analysis | Build/test skeleton, NE loader + `nedump`, Ghidra project + function inventory | M1: every function in the inventory, imports/exports named (done) |
| 2. Memory model and hybrid harness | Selector memory model, Unicorn harness, diff-test driver, FP decision | M2: first original function executed; first decompiled function green (done) |
| 3. Data recovery | Named DGROUP globals, struct layouts with evidence, enums and bit flags, the `.xy`/`.hst`/`.x1` file formats, the typed-accessor convention (decision 3) | M3: every global and struct that a decompiled function touches has a name and a type |
| 4. Faithful decompilation | All 34 code segments diff-tested (pass 1) | M4: native build with zero emulated functions; turn generation byte-identical to the original |
| 5. Readable source | Pass 2 for every function: names for functions, parameters, locals; typed data access; game-level comments; files renamed by subsystem once understood | M5: the readability metrics below at their targets |

Phase 3 runs ahead of the large segments in phase 4, because every segment reads the same globals and
structs. Phases 4 and 5 then run in parallel per segment.

Readability metrics, reported by `tools/progress.py` and enforced by `tools/lint_src.py` for functions
marked readable:

| Metric | Target for a readable function |
|--------|--------------------------------|
| Raw-offset accesses (`rd*`/`wr*`/`drd*`/`dwr*` with a numeric offset) | 0 |
| Placeholder names left (`fn_NN_XXXX`, `FUN_…`, `DAT_…`, `local_…`, `param_…`) | 0 |
| Magic constants without a name (masks, record sizes, sentinel values) | 0, except values that are plainly arithmetic |
| Comment | one game-level summary plus the tag line (decision 4) |

`docs/functions.csv` gains a `readable` status after `tested`, so progress shows how far pass 2 has
come per segment.

## 6. Risks

| Risk | Mitigation |
|------|------------|
| x87 80-bit precision differs from C `double` | Decide from the measured control word; SoftFloat `extF80` if needed. Diff tests catch divergence. |
| Transcendental results (C runtime only) differ between Unicorn, real FPUs and libm | Game segments don't use them directly. Inventory which CRT math functions the game calls and diff-test them. |
| Pass-2 refactors change behaviour | Per-function diff tests stay green through pass 2; the golden turn-generation test covers changes across functions. |
| Wrong guesses about what data means give misleading names | Each global, struct field and enum records its evidence (the code that reads or writes it) next to its declaration. Rename freely when evidence improves. |
| Copyright: original files and decompiled source | `original/` and `private/` stay git-ignored. Publishing `src/` is an owner decision (decision 8). |
| Ghidra 16-bit output is noisy (`CONCAT22` segment:offset pairs) | Conventions doc with translation rules; diff tests are the arbiter, not the pseudo-C. |

## Directory layout

```
original/      untouched game files (read-only, git-ignored)
private/       registration serials (git-ignored)
docs/          PLAN.md, CONVENTIONS.md, functions.csv (inventory + status, git-ignored)
tools/         generators, lint, progress (Python stdlib), nedump, vendored Wine spec files (tools/winespec/)
ghidra/        Java GhidraScripts (the Ghidra project itself lives in re/ghidra/)
runtime/       Win16 API stubs in C; runtime/tests/
harness/       native-only Unicorn harness and diff-test driver; harness/tests/
src/           decompiled game: segNN.c per code segment (renamed by subsystem in phase 5),
               stars.h (structs, enums, accessors), globals.h (named DGROUP globals), registry
tests/         test.h, scenarios/ (diff tests), golden/ (game files, git-ignored)
third_party/   vendored libraries, only if a task's decision requires one (e.g. SoftFloat 3e)
re/            generated, git-ignored: Ghidra project, dumps
build/         build output, git-ignored
```

## Appendix A: imported Win16 functions (197)

Resolved with the Wine spec files listed in §2 (tool #8).

- **KERNEL (44):** FatalExit, GetVersion, LocalAlloc, LocalReAlloc, LocalFree, LocalSize, GlobalAlloc, GlobalReAlloc, GlobalFree, GlobalLock, GlobalUnlock, GlobalSize, LockSegment, UnlockSegment, Yield, WaitEvent, GetModuleFileName, MakeProcInstance, FreeProcInstance, FindResource, LoadResource, LockResource, FreeResource, AccessResource, SizeofResource, AllocResource, OpenFile, _lclose, _lread, _lwrite, lstrcpy, lstrcat, lstrlen, InitTask, DOS3Call, __AHSHIFT, __AHINCR, GetPrivateProfileInt, GetPrivateProfileString, WritePrivateProfileString, GetDOSEnvironment, GetDriveType, FatalAppExit, __WINFLAGS
- **USER (107):** MessageBox, InitApp, PostQuitMessage, ExitWindows, SetTimer, KillTimer, GetTickCount, GetCurrentTime, GetCursorPos, SetCapture, ReleaseCapture, SetFocus, GetFocus, ClientToScreen, ScreenToClient, WindowFromPoint, IsIconic, GetWindowRect, GetClientRect, EnableWindow, GetWindowText, SetWindowText, BeginPaint, EndPaint, CreateWindow, ShowWindow, GetParent, IsWindowVisible, DestroyWindow, MoveWindow, RegisterClass, GetActiveWindow, ScrollWindow, SetScrollPos, GetScrollPos, SetScrollRange, GetDC, ReleaseDC, SetCursor, SetRect, CopyRect, PtInRect, OffsetRect, InflateRect, IntersectRect, FillRect, FrameRect, DrawIcon, DrawText, DialogBox, EndDialog, CreateDialog, GetDlgItem, SetDlgItemText, GetDlgItemText, CheckRadioButton, CheckDlgButton, IsDlgButtonChecked, SendDlgItemMessage, MessageBeep, FlashWindow, GetKeyState, DefWindowProc, GetMessage, PeekMessage, PostMessage, SendMessage, TranslateMessage, DispatchMessage, CallWindowProc, UpdateWindow, InvalidateRect, ValidateRect, GetWindowLong, SetWindowLong, DestroyMenu, CheckMenuItem, EnableMenuItem, GetMenu, GetSubMenu, DrawMenuBar, WinHelp, LoadCursor, LoadIcon, LoadBitmap, LoadAccelerators, TranslateAccelerator, GetSystemMetrics, GetSysColor, SetWindowPos, EqualRect, GetAsyncKeyState, MapWindowPoints, GetWindow, GetMenuItemCount, IsZoomed, SelectPalette, RealizePalette, GetWindowPlacement, InsertMenu, AppendMenu, DeleteMenu, CreatePopupMenu, TrackPopupMenu, _wsprintf, DestroyIcon, DestroyCursor
- **GDI (41):** SetBkColor, SetBkMode, SetROP2, SetTextColor, SetWindowOrg, LineTo, MoveTo, ExcludeClipRect, IntersectClipRect, Ellipse, Rectangle, PatBlt, SetPixel, TextOut, BitBlt, Escape, SelectClipRgn, SelectObject, CreateCompatibleBitmap, CreateCompatibleDC, CreateFontIndirect, CreatePatternBrush, CreatePen, CreateRectRgn, CreateSolidBrush, DeleteDC, DeleteObject, GetBkColor, GetDeviceCaps, GetObject, GetROP2, GetStockObject, GetTextExtent, GetTextMetrics, MulDiv, SetBrushOrg, UnrealizeObject, ExtTextOut, CreatePalette, StretchDIBits, GetDIBits
- **COMMDLG (3):** GetOpenFileName, GetSaveFileName, PrintDlg
- **TOOLHELP (1):** TimerCount
- **WIN87EM (1):** __fpMath
