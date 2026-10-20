# stars-decomp

Decompile **Stars! 2.6i** (16-bit Windows 3.x, `original/stars26i.exe`) into C source that people can
read, and prove every function behaves exactly like the original machine code.

- Plan, decisions and phases: [docs/PLAN.md](docs/PLAN.md)
- Rules for decompiled code: [docs/CONVENTIONS.md](docs/CONVENTIONS.md)
- Rebuild `re/`, the inventory and the screen captures from your own copy: [docs/TOOLING.md](docs/TOOLING.md)
- Work tracking: `bd ready` (beads, prefix `stars-decomp`)

## Where to look

| Path | What it is | Read it when |
|------|------------|--------------|
| `src/` | **The decompiled game.** `segNN.c` holds the functions of original code segment NN; `stars.h` the game structs, `globals.h` the named globals. | You want to read the game. Start here. |
| `tests/scenarios/` | Differential tests, one `test_segNN.c` per segment: each runs the original function and the C function from the same memory and compares every byte. | You want the evidence that a function is right. |
| `docs/` | `PLAN.md` and `CONVENTIONS.md`. | You want the why. |
| `harness/` | Test machinery: runs the original x86 code in the Unicorn emulator next to the C code. | You are changing how tests run. |
| `runtime/` | Test machinery: the Win16 API (KERNEL, USER, GDI, ...) and the segmented memory model the original code and the C code both run on. | You are changing how tests run. |
| `tools/` | Generators, lint, progress report, `nedump` (NE file dumper), vendored Wine spec files. | You are changing the tooling. |
| `ghidra/` | Ghidra scripts that export the function inventory, globals and per-function pseudo-C. | You are regenerating Ghidra output. |
| `third_party/` | Vendored SoftFloat 3e (exact x87 80-bit arithmetic) and font licences. | Rarely. |

Local only, never committed: `original/` (the game files, read-only), `private/` (registration
serials), `re/` (Ghidra project and exports, regenerable), `build/`, `docs/functions.csv` and
`docs/inventory.md` (the function inventory).

## Build and test

```
make doctor   # check the required tools
make test     # build everything, run every test (needs original/stars26i.exe)
python3 tools/progress.py   # decompilation progress per segment
```

## Original files (SHA-256)

Check these before trusting any analysis result: `shasum -a 256 original/*`

```
5770dade2db0e11918631d5c128c0bf7ab48e371f7b2267a4ff02531bb8e198e  stars26i.exe   the game (NE, 3 216 832 bytes)
9c5cb10fd5e5d201813fcf6f470c495f290b322ee21e1795418a8412393270d1  stars!.hlp     WinHelp 3.x help file
3387135a5075439b9238d6c486b047d4dba8d9a6d1dad6bb74050347c70616db  ctl3dv2.dll    MS 3D controls (used by hyprfind.dll, not by the game)
ebe6cafdddf9d95dc687fe42c9de4b6ef6971ea5ab6d1a89fe312f3132de9b70  hyprfind.dll   WinHelp full-text search (not used by the game)
dce03a552f45de42cc8f92dd4cf043d027ee185e88c29a02641f9046a3ce3280  wavemix.dll    MS wave mixer (not imported by the game)
```
