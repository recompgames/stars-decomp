# stars-game

Decompile **Stars! 2.6i** (16-bit Windows 3.x, `original/stars26i.exe`) into C, recompile it to
WebAssembly, and run it in a browser with the Windows 3.11 look and feel preserved.

- Plan and tool list: [docs/PLAN.md](docs/PLAN.md)
- Work tracking: `bd ready` (beads, prefix `stars`)

## Folder layout

| Path | What it is | In git? |
|------|------------|---------|
| `original/` | The untouched Stars! 2.6i files. Read-only (`chmod 444`). Never edit, never commit. | no |
| `private/` | `code.txt`: registration serials. Never commit, never paste into issues or logs. | no |
| `docs/` | Plan, conventions, function inventory. | yes |
| `re/` | Generated RE working files (Ghidra project, dumps, frame captures). Safe to delete and regenerate. | no |

Directories created later by the tracked tasks (`runtime/`, `harness/`, `src/`, `tools/`, `ghidra/`,
`tests/`, `web/`) are described in [docs/PLAN.md](docs/PLAN.md#directory-layout).

## Original files (SHA-256)

Check these before trusting any analysis result: `shasum -a 256 original/*`

```
5770dade2db0e11918631d5c128c0bf7ab48e371f7b2267a4ff02531bb8e198e  stars26i.exe   the game (NE, 3 216 832 bytes)
9c5cb10fd5e5d201813fcf6f470c495f290b322ee21e1795418a8412393270d1  stars!.hlp     WinHelp 3.x help file
3387135a5075439b9238d6c486b047d4dba8d9a6d1dad6bb74050347c70616db  ctl3dv2.dll    MS 3D controls (used by hyprfind.dll, not by the game)
ebe6cafdddf9d95dc687fe42c9de4b6ef6971ea5ab6d1a89fe312f3132de9b70  hyprfind.dll   WinHelp full-text search (not used by the game)
dce03a552f45de42cc8f92dd4cf043d027ee185e88c29a02641f9046a3ce3280  wavemix.dll    MS wave mixer (not imported by the game)
```
