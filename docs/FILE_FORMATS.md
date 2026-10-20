# Game file formats

Structure of the files Stars! 2.6i reads and writes, recovered from the decompiled load and save
code. Locations are `seg:off` (NE segment : offset, as in `docs/functions.csv`). This file describes
structure only: it contains no bytes from any game file. Checked by `tools/check_file_formats_doc.py`
(struct sizes and block-type numbers must equal `src/stars.h`).

Status: statements were read from the Ghidra pseudo-C of the named function, except where marked
inferred or unconfirmed (the file name extensions of the text exports, the magic string). Things not
proven are in "Open questions" at the end.

## 1. File kinds

`14:0x52bc` builds the file name from the base name in DS:0x5764 (extension stripped), a kind number
and a player index n (0-based). Format strings are in DGROUP: `"%s."` (DS:0x9cc), `"%c%d"` (DS:0x9d7).

| Kind | Name built | Content | Writer | Reader |
|------|------------|---------|--------|--------|
| 0 (or 5) | `base.xy` | Universe: game header and planet positions | 15:0x1334 | 14:0x0508 (checks it), 15:0x1334 |
| 1 | `base.x<n+1>` | Player orders and message log (turn "x" file) | 9:0x7d1e | 9:0x78a0, 9:0x7c26 |
| 2 | `base.hst` | Host file: whole game state | 14:0x35b0 (param 0xFFFF) | 14:0x0508 |
| 3 | `base.m<n+1>` | Player turn file: that player's view of the game | 14:0x35b0 | 14:0x0508, 4:0x3ecc (peek) |
| 4 | `base.h<n+1>` | Player history file: remembered planets and designs | 9:0x80da | 14:0x0508 |

Kind 5 is the same name as kind 0; 28:0x39d8 uses it to write an empty file (end block only) to a
user-chosen path after a Save dialog. Other name strings: `"%s.x1"` (DS:0x372, used at 4:0x0528 to
open player 1's orders file), `"%s.m%d"` (DS:0x9c5, used in 14:0x35b0), `"%s.hst"` (DS:0x9be).

## 2. Common layer: blocks

Every file is a sequence of blocks. Each block is a `struct BlockHeader` (0x2 bytes): a little-endian
16-bit word, size = word & 0x3ff (body length in bytes), type = word >> 10. The body follows
immediately. A file ends with an end block of type 0 (14:0x35b0 and 15:0x1334 write it as
type 0, size 2 in .hst/.m/.xy; 9:0x7d1e and 9:0x80da write type 0, size 0).

- Reader of one block: 14:0x305a. It reads the 2-byte header into DS:0x2910, the body into DS:0x4db2
  (if size is not 0), via the raw reader 14:0x31da.
- Writer of one block: 14:0x579a(type, size, far ptr). It copies the body to DS:0x4db2, then the
  header and body go out through the raw writer 14:0x5836.
- Raw I/O: open 14:0x3100 (OpenFile; retries up to 4 seconds when sharing-locked; on failure it
  raises an error through 34:0x0825), close 14:0x31c2, read 14:0x31da, write 14:0x5836. The current
  handle is DS:0x9e2. If the far pointer DS:0x968 is non-null, 14:0x31da reads from that memory
  instead of the file (used for game data held in a resource, see 9:0x78a0 and 9:0x7eea).
- Header-then-open: 14:0x2caa(kind, player, mode) builds the name (14:0x52bc), opens it (14:0x3100)
  and validates the file header block (section 3). 14:0x538c(kind, player, name) opens a file for
  writing (open flags 0x1012) and writes a fresh file header with 14:0x53f8.

### Scrambling (encryption)

Bodies of every block except type 0 and type 8 are XORed with a keystream (not a checksum; there is
no checksum in these files).

- Cipher: two linear congruential generators, state in DS:0x24e8 and DS:0x24ec, stepped by 8:0x8a1e.
  Multipliers 0x9c4e (modulus 0x7fffffab) and 0x9ef4 (modulus 0x7fffff07). Output is
  (low 32 bits of the first) minus (low 32 bits of the second).
- Seeding: 8:0x89aa, called with fields of the file header block (14:0x305a when it reads type 8,
  14:0x579a when it writes type 8). It indexes a small constant table at DS:0x8554 with the turn
  field, and discards a number of outputs that depends on the game id, the player index and one flag
  bit (see Open questions for the exact argument mapping).
- Applying: 8:0x8ae6(buffer, size) XORs 4 bytes at a time, then the 1 to 3 trailing bytes with the low
  bytes of one more output. Same call decrypts (14:0x305a) and encrypts (14:0x579a).
- The stream is continuous across blocks of a file: it is seeded once by the file header and then
  advanced by each body in order. Planet records in .xy are not scrambled (section 4).

## 3. File header block (type 8)

It is the first block of every file. `struct FileHeader` (0x10 bytes) is the body of the type-8
block; it is not scrambled.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| +0x00 | 4 | magic | written by 14:0x53f8 as a 4-character string copied from DS:0x9dc (unconfirmed: the string at that address is "J3J3" in the exe, but the copy source is elided in the pseudo-C; open question 1); 14:0x2caa does not test it |
| +0x04 | 4 | game_id | 14:0x53f8 writes GameHeader.game_id (DS:0x70); 14:0x2caa requires equality unless DS:0x70 is 0 (then any game is accepted) |
| +0x08 | 2 | version | 14:0x53f8 writes the constant 0x2a2a; 14:0x2caa requires high nibble 0x2 and (version & 0xfe0) strictly between 0x61f and 0xa40; stored in DS:0x772 |
| +0x0A | 2 | turn | 14:0x53f8 writes GameHeader.turn (DS:0x82); 14:0x2caa compares |
| +0x0C | 2 | player_salt | low 5 bits = player index (14:0x53f8 stores its player argument & 0x1f); upper 11 bits = 8:0x8730(2000) + GetTickCount, times 0x20 |
| +0x0E | 2 | flags | bits 0x100, 0x200 and 0x400 are tested by 14:0x2caa; bit 0x1000 (bit 4 of the high byte) feeds the cipher seed (8:0x89aa); bit 0x800 is set by 14:0x53f8 under a DS:0x792 condition |

Header validation order (14:0x2caa): open, read header, require type 8, compare game_id, player,
turn, flags per the requested mode. On mismatch it shows an error through 14:0x2c60 and closes.
14:0x54ea edits an existing file in place: for param 1, 2 or 4 it sets or clears one flag bit in the
header's flags word and rewrites the header block; for param 8 it finds a player block and rewrites it; 14:0x2f40 and 14:0x2f88 are read-only header probes built on 14:0x2caa.

## 4. .xy (universe file)

Writer: 15:0x1334 (new game). Opens with 14:0x538c(kind 0). Reader/validator: 14:0x0508 (first act,
via 14:0x2caa(0, 0xFFFF)) and 15:0x1334.

Block order:

1. type 8 `struct FileHeader` (written by 14:0x53f8).
2. type 7 `struct GameHeader` (0x40 bytes, scrambled): copy of DS:0x70. 15:0x1334 writes it with
   14:0x579a(7, 0x40, DS:0x70); 14:0x0508 copies it back from DS:0x4db2 to DS:0x70 (16 dwords) and
   derives DS:0xB0 = (universe_size + 1) * 400 and DS:0xB2 = DS:0xB0 + 2000.
3. GameHeader.planet_count raw 4-byte planet records, written with 14:0x5836 directly and read with
   14:0x31da directly: NOT in a block and NOT scrambled. Per record, as a little-endian dword:
   bits 0-9 = x minus the previous planet's x (first planet counts from 1000), bits 10-21 = y,
   bits 22-31 = name index (stored in DS:0x29e8[i]). 14:0x0508 rebuilds the absolute x/y table
   DS:0x31ba and range-checks the values against the universe size.
4. End block, type 0, size 2: body is the player count (DS:0x78). 14:0x0508 requires type 0 here.

## 5. Block types

Block types with a name in `src/stars.h` (`BLK_*`). Sizes are body sizes.

| Type | Name | Body | Written by | Read by |
|------|------|------|------------|---------|
| 0 | BLK_END | player count (.xy), turn DS:0x82 (.hst/.m), or empty (.x/.h) | 15:0x1334, 14:0x35b0, 9:0x7d1e, 9:0x80da | 14:0x0508 |
| 6 | BLK_PLAYER | `struct Player`, 0xC0 in memory; 14:0x32a0 writes the file form | 14:0x32a0 | 14:0x0370 (from 14:0x0508, 4:0x3ecc) |
| 7 | BLK_GAME_HEADER | `struct GameHeader` | 15:0x1334 | 14:0x0508 |
| 8 | BLK_FILE_HEADER | `struct FileHeader` | 14:0x53f8 | 14:0x305a, 14:0x2caa |
| 12 | BLK_MESSAGE | message buffer chunks (`struct MessageHeader` records: id and per-value word flags, subject word, values; the value count of an id is the signed byte at cs:0x0E74 + id of code segment 6) | 6:0x8308 | 14:0x27d4 (fix-up of a loaded chunk) |
| 13 | BLK_PLANET_13 | `struct Planet`, full form: type 14 body plus the improvement bytes (+0x14), the starbase dword (+0x2C) and the route target (+0x30) | 14:0x4926 | 14:0x1f0c |
| 14 | BLK_PLANET | `struct Planet`, 0x38, reduced form (none of the full-form fields) | 14:0x4926 | 14:0x1f0c |
| 16 | BLK_FLEET | `struct Fleet` (layout version 7) | 14:0x4c86 | 14:0x235e |
| 17 | BLK_FLEET_OLD | `struct Fleet` (older layout) | 14:0x4c86 | 14:0x235e |
| 19 | BLK_WAYPOINT_19 | `struct Waypoint`, 0x12 | 14:0x444e | 14:0x235e |
| 20 | BLK_WAYPOINT_20 | `struct Waypoint`, 0x12 | 14:0x444e | 14:0x235e |
| 21 | BLK_FLEET_NAME | fleet name | 14:0x4fa2 | 14:0x235e |
| 26 | BLK_DESIGN | `struct ShipDesign` | 14:0x342a | 14:0x0000 |
| 28 | BLK_PROD_QUEUE | `struct ProdItem` array, count << 2 bytes | 14:0x35b0 | 14:0x0508 |
| 30 | BLK_BATTLE_PLAN | `struct BattlePlan`, 0x24 | 14:0x50d6 | 14:0x271a |
| 36 | BLK_36 | 4 bytes from DS:0x31c | 14:0x35b0 | 14:0x0508 |
| 40 | BLK_MESSAGE_OUT | message record | 6:0x8308, 9:0x7d1e | 9:0x78a0 |
| 43 | BLK_MAP_OBJECT | `struct MapObject`, 0x12; preceded by a 2-byte count block | 14:0x35b0 | 14:0x0508 |
| 45 | BLK_45 | `struct ScoreRecord`, 0x18 bytes, one per player, from DS:0x177a (record index = low 5 bits of word 0) | 14:0x35b0, 9:0x80da | 14:0x0508 |

Block types seen in the code that `src/stars.h` has no name for:

| Type | Where | What the code does with it |
|------|-------|----------------------------|
| 9 | 9:0x7d1e writes (size 0x11), 9:0x78a0 reads | orders-file header: orders byte count (DS:0x970), DS:0x872, 11 bytes from DS:0x564c (2 dwords, a word, a byte); these account for 15 of the 0x11 bytes, the other 2 are not set by visible code (local stack field); 9:0x78a0 also copies 0x10 bytes per player into DS:0x980 |
| 31, 39 | 14:0x0508 reads (types 0x7c and 0x9c at byte-high) | bodies copied verbatim into a heap at DS:0xdbc; 39 appears to continue the previous 31; 14:0x27d4 walks a record as a token count at +3, then that many `struct BattleToken` (0x1D bytes each) from +0x0E (the battle ship stacks, read by 30:0x5234) |
| 32 | 9:0x80da writes (size 4), 14:0x0508 reads | .h file first block: planet count, then a 12-bit count from Player word DS:0x5a68; reader sizes the planet array from the sum, capped at 1000 |
| 33 | 9:0x80da writes DS:0xab2 bytes from DS:0x5508 | .h file: opaque blob, reader copies it to DS:0x5508 if it fits in DS:0xab2 |
| 15 | 9:0x80da writes (via 14:0x4926) | planet variant for kind byte below 3; read path not traced |
| 41 | 9:0x80da writes | not read in the traced loader |

## 6. .hst and .m (host and player turn files)

Writer: 14:0x35b0(base, player, flag). Player 0xFFFF writes .hst (host: every player), else .m<n+1>
(only that player's data). It first runs 14:0x5872 (rebuilds derived tables), opens with
14:0x538c(kind 2 or 3), and, when the flag argument is set and 14:0x441e finds an existing file with a valid header, it
reopens that file in place (14:0x54ea) instead of creating it. Block order written:

1. type 8 file header (14:0x53f8).
2. type 6 player blocks (14:0x32a0), one per player (all for .hst; the requesting player plus
   visible others for .m).
3. type 36 (4 bytes) for .hst when DS:0x31c is non-zero.
4. messages for the file's owner (6:0x8308, types 12 and 40).
5. planets (14:0x4926): type 13 when the planet's kind byte (record byte +4) is 7, followed by a
   type-28 production queue block when its queue pointer is set; kind 2 is written as kind 3, type 14;
   all other owned planets type 14.
6. fleets (14:0x4c86 and 14:0x4fa2 for the name; 14:0x444e writes waypoints), designs (14:0x342a, type 26),
   type 45 records, a 2-byte count block of type 43 then 0x12-byte map objects, battle plans (14:0x50d6).
7. end block (type 0, size 2).

Reader: 14:0x0508(base, ext). `ext` is "hst" (host) or "m<n>" (digits, minus 1 = player; any other first character
selects the .m path). It validates the .xy file first (section 4), clears the
player table, then loads the requested file: 14:0x2caa(kind 2 or 3), then loops 14:0x305a over the
blocks, dispatching to 14:0x0370 (player), 14:0x1f0c (planets), 14:0x235e (fleets and waypoints),
14:0x0000 (designs), 14:0x271a (battle plans), 14:0x27d4 (message chunks). The outer loop repeats while
8:0xb1ec reports further turn data in the same file, (see Open questions).
14:0x2992 resets the loaded state before a load. 4:0x3ecc peeks a file: header, one player block,
end block, without loading. 4:0x39f8 probes each player's .x file in turn with 14:0x2caa(kind 1)
and loads 14:0x0508 first when the game is not loaded.

## 7. .x<n> (orders file)

Written by 9:0x7d1e(base, player) via 14:0x538c(kind 1). Order after the file header:
type 9 header (size 0x11), then the orders buffer at DS:0x96c (length DS:0x970) re-emitted block by
block (each record in the buffer is its own 2-byte header plus body), then type 40 records from the
linked list at DS:0xac0 (size from the record plus 0xC), then end block (type 0, size 0).
Reader: 9:0x78a0(path): opens with 14:0x3100 (flags 0x4020) or, for the bundled data, reads from a
locked resource through DS:0x968, validates the file header against the loaded game (game id and
turn), reads the type 9 header, rebuilds the orders buffer in DS:0x96c (copying header and body of
each block), then the type 40 list into DS:0xac0. 9:0x7c26 re-reads only the lengths to size the
buffer. Used when the player submits turns and when the host loads x files before generating.

## 8. .h<n> (player history file)

Written by 9:0x80da(player) via 14:0x538c(kind 4): type 32 (4 bytes), then planet blocks via
14:0x4926 with the history flag set (type 14, or 15 for planets not currently owned), type 33, player blocks (14:0x32a0), designs (14:0x342a), type 45
(0x18-byte records), type 41, end block. Read by 14:0x0508 when a player file is
loaded: 14:0x2caa(kind 4), type 32, planet blocks through 14:0x1f0c, type 33, player blocks (type 6)
through 14:0x0370, designs (type 26) through 14:0x0000, then type 45 records.

## 9. Other files the code opens (not game files)

| Function | What |
|----------|------|
| 8:0x8c92 | copies a file (read 14:0x31da, write _lwrite) to a second path with 'm' in the extension; used by 22:0x0000 (generate turn) to back up .m files |
| 3:0x03e0, 15:0x39d4 | read a whole text file (up to 16000 bytes) into memory; text, not blocks |
| 32:0x5954, 32:0x5ada, 32:0x67a8 | write text report exports (.map, .pla, .fle strings in DGROUP) with 14:0x3100 flag 0x1012 and 14:0x5836 |
| 9:0x7eea | extracts game data from a locked resource (via DS:0x968) into a file with 14:0x5836, then ends at the next type-8 block |
| 8:0xad28, 8:0xadc4 | resource loading (AccessResource, _lread) for the program's own resources, not game files |

## 10. Struct link table

`struct BlockHeader` (0x2 bytes), `struct FileHeader` (0x10 bytes), `struct GameHeader`
(0x40 bytes), `struct Player` (0xC0 bytes), `struct Planet` (0x38 bytes), `struct Waypoint`
(0x12 bytes), `struct Fleet` (0x7C bytes), `struct ShipDesign` (0x8D bytes), `struct BattlePlan`
(0x24 bytes), `struct ProdItem` (0x4 bytes), `struct MapObject` (0x12 bytes), `struct ScoreRecord` (0x18 bytes), `struct MessageRecord5` (0x5 bytes), `struct BattleToken` (0x1D bytes). Each of these structs in
`src/stars.h` carries a line "see docs/FILE_FORMATS.md section N": BlockHeader section 2,
FileHeader section 3, GameHeader section 4, all others section 5 (block-type table).

## 11. Open questions

1. FileHeader magic: 14:0x53f8 copies a 4-byte string with 34:0x0352 (strncpy); the source operand is
   elided in the pseudo-C. Confirm in disassembly that it is DS:0x9dc. Study 14:0x53f8.
2. Cipher seed mapping: Ghidra shows five arguments to 8:0x89aa and elides a sixth (param_6). Which
   header field supplies the discard count and which the table indices is not verified. Study 8:0x89aa
   with the call sites in 14:0x305a and 14:0x579a.
3. FileHeader.flags bit meanings (0x100, 0x200, 0x400, 0x800, 0x1000): only tested, not named. Study
   14:0x2caa and 14:0x54ea.
4. Types 31, 39, 33, 41 and 12 vs 40: content and purpose. Study 14:0x0508 (around the DS:0xdbc heap),
   9:0x80da and 6:0x8308.
5. Type 9 body: field meaning past the first two words (11 bytes from DS:0x564c, plus 2 of the 0x11 body bytes not set by visible code). Study 9:0x7d1e and 9:0x78a0.
6. The outer loop of 14:0x0508 that repeats per turn: what 8:0xb1ec returns and how a file holds
   more than one turn. Study 14:0x0508 and 8:0xb1ec.
7. Which extension each of 32:0x5954, 32:0x5ada, 32:0x67a8 writes (.map/.pla/.fle) is inferred, not read from the format strings.
8. Block type 15 for historical planets and the "variant" Planet in types 13/14 (14:0x4926 flag argument). Type 15 is written by 9:0x80da for planets whose kind byte is below 3.
