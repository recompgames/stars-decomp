# Code-segment bytes that no inventoried function covers

Result of task stars-decomp-1tp.1. `tools/segment_gaps.py` lists the bytes of each code segment
that no row of `docs/functions.csv` covers (a row covers `[off, off + size)`). Every range of 16 bytes
or more is listed below, in the form `seg:0xSTART-0xEND` (END inclusive), and
`python3 tools/segment_gaps.py --check docs/code-segment-data.md` exits 0 only while that stays true.
Ranges under 16 bytes are 0x90 or 0x00 alignment padding.

The text gives structure only: addresses, lengths, shape and readers, never the bytes.

## Finding

- No gap is code that Ghidra missed. Every byte Ghidra decoded as an instruction inside a gap already
  belongs to a Ghidra function body. The inventory `size` column is the run that starts at the entry
  point (docs/TOOLING.md section 4), so a function whose body continues past a `JMP CS:[BX+table]`
  switch is undercounted: the jump table ends the run and the case code that follows is a detached
  fragment of the same function. Part B lists these functions.
- The remaining gaps are data stored in the code segment (part A) and the jump tables and padding
  that sit between those fragments (part B columns "jump table" and "padding").
- Premise check: every segment total in the task text matches `segment_gaps.py` on the current
  inventory (for example segment 2: 21191 uncovered of 22838 bytes).
- Consequence for later tasks: a function listed here with a small `size` is much larger than its
  row says. The pass-1 task of its segment should size the work from the end of the gap listed in part B.

## A. Data tables (no instruction decoded, no function owns them)

Readers are the functions that reference the range (Ghidra data references, or an operand with a
`CS:` override that falls inside the range) in a static scan; none means the scan found no reader.
All names are provisional until the data-recovery tasks name them.

| Range | Length | Shape | Readers |
|-------|-------:|-------|---------|
| 1:0x0890-0x08a3 | 20 | byte table, 10-step ascending ramp (20 entries) | none found by static scan |
| 2:0x000f-0x507b | 20589 | tables of named records (strings inside); sub-tables below | see sub-table list |
| 4:0x02b1-0x02c9 | 25 | byte table, small values, used by the serial encode/decode pair | 4:0x1af2, 4:0x1c78 |
| 6:0x0e74-0x6a67 | 23540 | byte tables, high entropy, starts with a run of values 1 to 7 (text-compression style tables, provisional) | 6:0x7410, 6:0x752e, 6:0x7668, 6:0x8460, 6:0x8864 |
| 7:0x001b-0x0091 | 119 | table of 32-bit values (about 29 entries, first two equal) | 7:0x336a, 7:0x3cd4 |
| 8:0x0038-0x8653 | 34332 | byte tables starting with a frequency-ordered character string, then high-entropy data (text-compression style, provisional) | 8:0x8672, 8:0x86cc, 8:0x89aa, 8:0x8b56, 8:0x8df4 |
| 11:0x09e6-0x0a17 | 50 | table of 16-bit values, 3 leading flag words then 22 ascending thresholds (50 bytes; sits inside 11:0x0000-0x0a17) | 11:0x0b46 |
| 13:0x050f-0x053d | 47 | table of ascending 16-bit values (23 entries) | 13:0x053e, 13:0x09ae, 13:0x0844, 13:0x0000 |
| 15:0x004f-0x1333 | 4837 | sparse table (83 percent zero bytes) of small records | 15:0x1334, 15:0x69a2 |
| 17:0x0205-0x0461 | 605 | byte lookup tables, values 0 to 0x10 (605 bytes; follows the end of the function at 17:0x0000) | 17:0x1730, 17:0x38b0 |
| 18:0x01d7-0x0365 | 399 | table of 16-bit values (399 bytes) | 18:0x0366, 18:0x4912, 18:0x4cf4 |
| 19:0x02c5-0x039f | 219 | table of 16-bit values (219 bytes) | 19:0x12f0 |
| 20:0x1c61-0x1d9f | 319 | byte table (319 bytes) | 20:0x1da0 |
| 21:0x109e-0x123d | 416 | table, 16-bit values (416 bytes) | 21:0x2c74 |
| 22:0x0e6e-0x0e91 | 36 | table of 16-bit values, ascending (18 entries) | 22:0x30b6 |
| 25:0x008d-0x0119 | 141 | table of 16-bit values, ascending (141 bytes) | 25:0x01f6, 25:0x2718, 25:0x4054 |
| 27:0x062c-0x06b9 | 142 | mixed table of 16-bit values, 51 percent zero bytes (142 bytes) | 27:0x1546, 27:0x1604 |
| 28:0x02c4-0x0335 | 114 | table of 16-bit values (114 bytes) | 28:0x1d60, 28:0x2116, 28:0x2200, 28:0x2d30, 28:0x3b74 |
| 30:0x0306-0x038d | 136 | packed nibble bitmap-like table (136 bytes) | 30:0x2cb4 |

### Segment 31 data (two ranges and a misdecoded fragment)

| Range | Length | Shape | Readers |
|-------|-------:|-------|---------|
| 31:0x01ae-0x346a | 12989 | byte tables; starts with a short flag area, then a frequency-ordered character string and high-entropy data (text-compression style, provisional) | 31:0x6524, 31:0x9baa |
| 31:0x3478-0x5c1d | 10150 | continuation of the same tables | 31:0x6524, 31:0x9baa |

The 13 bytes 31:0x346b-0x3477 lie between the two ranges. Ghidra decodes them as instructions of
the function at 31:0x663a (it reaches them from a computed jump at 31:0x687d), but they are table
data: the entries of that jump table are worth re-checking in the pass-1 task of segment 31.
The whole range 31:0x01ae-0x5c1d (23152 bytes) is one gap in `segment_gaps.py`.

### Segment 2 tables inside 2:0x000f-0x507b

2:0x000f-0x507b (20589 bytes) holds tables of records that each begin with a name string. The accessors
in segment 2 return `cs:(index * size + base)` for these tables:

| Base | Record size | Accessor | Note |
|------|------------:|----------|------|
| 2:0x000f | 0x38 | none (sequence of names seen at a 0x38 stride) | 17 records, name at record + 8 |
| 2:0x05c3 | 0x8f | 2:0x0000 | name at record + 8 |
| 2:0x14ac | 0x8f | 2:0x507c (index below 0x20; from 0x20 up it calls 2:0x0000 with index - 0x20) | |
| 2:0x369a | 0x36 | 2:0x50ca | |
| 2:0x4594 | 0x38 | 2:0x50b0 | |
| 2:0x4b9c | 0x4e | 2:0x50a2 | |

Record counts and exact table ends are not yet recovered; sizes 0x8d and 0x8f both appear between
names near 2:0x0424 to 2:0x0bf0. The segment-2 data-recovery task settles them.

## B. Code inside Ghidra function bodies (detached by jump tables)

"Inventory size" is the `size` column of the owning function's row, which stops at the first jump
table; the real function runs at least to the end of its gap. Jump-table bytes are 16-bit offsets of the switch cases; padding is 0x90 bytes.

| Gap | Length | Owning function (inventory size) | Code | Jump table | Padding | Other data |
|-----|-------:|----------------------------------|-----:|-----------:|--------:|-----------:|
| 2:0x55eb-0x5841 | 599 | 2:0x50f8 (1267) | 561 | 35 | 3 | 0 |
| 3:0x00fa-0x03df | 742 | 3:0x0000 (250) | 625 | 112 | 5 | 0 |
| 9:0x653f-0x789f | 4961 | 9:0x651e (33) | 4839 | 93 | 29 | 0 |
| 11:0x0336-0x0a17 | 1762 | 11:0x0000 (822) | 1677 | 12 | 23 | 50 (data) |
| 11:0x0a3d-0x0aaf | 115 | 11:0x0a18 (37) | 91 | 19 | 5 | 0 |
| 11:0x0ad5-0x0b45 | 113 | 11:0x0ab0 (37) | 88 | 19 | 6 | 0 |
| 11:0x2088-0x3413 | 5004 | 11:0x0b46 (5442) | 4958 | 12 | 34 | 0 |
| 15:0x211c-0x38fd | 6114 | 15:0x1334 (3560) | 6064 | 18 | 32 | 0 |
| 15:0x6a2f-0x6ab1 | 131 | 15:0x6a04 (43) | 104 | 21 | 6 | 0 |
| 17:0x0129-0x0461 | 825 | 17:0x0000 (297) | 201 | 17 | 2 | 605 (data) |
| 21:0x1bcd-0x1d75 | 425 | 21:0x1aec (225) | 402 | 17 | 6 | 0 |
| 22:0x4312-0x5ff5 | 7396 | 22:0x3ec6 (1100) | 7319 | 37 | 40 | 0 |
| 23:0x0804-0x0e67 | 1636 | 23:0x0756 (174) | 1614 | 14 | 8 | 0 |
| 23:0x14c1-0x1bc5 | 1797 | 23:0x0e68 (1625) | 1728 | 57 | 12 | 0 |
| 24:0x0131-0x091d | 2029 | 24:0x00ca (103) | 1990 | 29 | 10 | 0 |
| 24:0x0968-0x0e23 | 1212 | 24:0x091e (74) | 1175 | 28 | 9 | 0 |
| 24:0x24c3-0x25a1 | 223 | 24:0x23ba (265) | 202 | 19 | 2 | 0 |
| 24:0x2660-0x2737 | 216 | 24:0x25a2 (190) | 192 | 20 | 4 | 0 |
| 26:0x2650-0x29d9 | 906 | 26:0x21e4 (1132) | 840 | 56 | 10 | 0 |
| 27:0x29ff-0x492d | 7983 | 27:0x1e06 (3065) | 7814 | 118 | 51 | 0 |
| 30:0x2955-0x2c13 | 703 | 30:0x27c4 (401) | 675 | 25 | 3 | 0 |
| 30:0x3901-0x3965 | 101 | 30:0x38ea (23) | 84 | 15 | 2 | 0 |
| 30:0x3bf0-0x3c79 | 138 | 30:0x3bdc (20) | 123 | 12 | 3 | 0 |
| 31:0x664f-0x9a83 | 13365 | 31:0x663a (21) | 13094 | 87 | 184 | 0 |
| 31:0xa877-0xab71 | 763 | 31:0xa748 (303) | 725 | 35 | 3 | 0 |
| 32:0x0304-0x0653 | 848 | 32:0x0000 (772) | 804 | 33 | 11 | 0 |
| 32:0x1866-0x1a0d | 424 | 32:0x158e (728) | 402 | 18 | 4 | 0 |
| 32:0x223f-0x22b9 | 123 | 32:0x2228 (23) | 99 | 17 | 7 | 0 |
| 32:0x2329-0x24e1 | 441 | 32:0x22ba (111) | 377 | 55 | 9 | 0 |
| 32:0x2568-0x3579 | 4114 | 32:0x24e2 (134) | 3967 | 109 | 38 | 0 |
| 32:0x363f-0x36d5 | 151 | 32:0x35f0 (79) | 136 | 13 | 2 | 0 |
| 32:0x3902-0x3bb1 | 688 | 32:0x38b4 (78) | 645 | 35 | 8 | 0 |
| 32:0x3e3c-0x4e69 | 4142 | 32:0x3dc6 (118) | 3985 | 109 | 48 | 0 |
| 32:0x549e-0x5953 | 1206 | 32:0x5418 (134) | 1170 | 26 | 10 | 0 |

The 11:0x09e6 data (50 bytes, in the gap 11:0x0336-0x0a17) and the 17:0x0205 data (605 bytes, in the
gap 17:0x0129-0x0461) are the "other data" entries; the jump-table column excludes them. Ranges
11:0x09e6-0x0a17 and 17:0x0205-0x0461 are also listed in part A.
