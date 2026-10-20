/* Shared declarations for the decompiled game code (docs/CONVENTIONS.md rules a, c, d).
 * Public (docs/PLAN.md decision 8). Game structs (stars-bfj.2) are added below, each packed
 * with _Static_assert on sizeof and on offsetof of every field. */
#ifndef STARS_SRC_STARS_H
#define STARS_SRC_STARS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/frame.h"
#include "runtime/include/win16.h"
#include "runtime/mem.h"
#include "src/globals.h"

/* By-name Win16 API calls are logged to the harness API log (harness/byname.h, stars-4a6). */
#include "harness/byname.h"

/* NEARPTR: a 16-bit offset into DGROUP (FARPTR, selector:offset, is in win16.h). */
typedef uint16_t NEARPTR;

/* Selector of NE segment n (code or data): n << 3. DGROUP is MEM_SEL_DGROUP (0x0118). */
#define SEG_SEL(n) MEM_SEL_SEG(n)

/* Packed struct and layout checks: structs are defined between #pragma pack(push, 1) and
 * #pragma pack(pop) (the lint rejects 'struct PACKED Name'); follow every definition with
 * STATIC_SIZE / STATIC_OFFSET. */
#define PACKED __attribute__((packed))
#define STATIC_SIZE(type, n) _Static_assert(sizeof(type) == (n), #type " size")
#define STATIC_OFFSET(type, field, n) _Static_assert(offsetof(type, field) == (n), #type "." #field " offset")

/* Huge-pointer stepping (the original's __AHINCR code): add n bytes to a far pointer; every 64 KB
 * carried into the offset moves the selector on by MEM_AHINCR (8). The offset wraps in 16 bits. */
static inline uint32_t HUGE_ADD(uint32_t fp, uint32_t n) {
  uint32_t o = (uint32_t)(uint16_t)fp + n;
  uint16_t sel = (uint16_t)((fp >> 16) + (o >> 16) * MEM_AHINCR);
  return MK_FP(sel, (uint16_t)o);
}

/* Readable data access (docs/CONVENTIONS.md rule k). FAR_REC gives a transient host pointer to the
 * record of type `type` at seg:off (the offset wraps in 16 bits like the original's register);
 * NEAR_AT is the DGROUP variable at a near offset, read or written at the width of `type` (use the
 * *_ua types of src/globals.h where the offset can be odd). Neither pointer may outlive its statement
 * or loop iteration (rule c). */
#define FAR_REC(type, seg, off) ((type *)LIN(MK_FP((seg), (uint16_t)(off))))
#define NEAR_AT(type, off) (*(type *)LINN((uint16_t)(off)))

/* ---- Game data structures (stars-bfj.2) ----------------------------------------------------------
 * Recovered from the original machine code via re/decomp (Ghidra pseudo-C), never from guesses.
 * Each field's comment names the seg:off (NE segment : offset) of a function that reads or writes
 * it. A field called fld_XX is known to exist with that width at that offset, but its meaning is not
 * proven; uint8_t unknown_XX[n] is padding with no access found. "Fleet and planet block" layouts
 * match the in-memory record because the loaders copy block bytes verbatim (14:0x235e, 14:0x0370).
 * All structs are packed with #pragma pack(push, 1) (tools/lint_src.py does not accept the
 * 'struct PACKED Name' form), little-endian, as in the 16-bit original. */

/* Where the tables live in DGROUP (selector MEM_SEL_DGROUP) and which functions index them. */
enum {
  DS_GAME_HEADER = 0x0070,     /* GameHeader, 0x40 bytes: 14:0x0508, 15:0x1334 */
  DS_PLANET_PTR = 0x00B6,      /* far pointer to Planet[DS:0x4D96], stride 0x38: 14:0x0508 */
  DS_FLEET_PTRS = 0x00BA,      /* far pointer to FARPTR[DS:0x553A], each -> Fleet: 14:0x0508 */
  DS_SHIP_DESIGNS = 0x00BE,    /* FARPTR[16 players] -> ShipDesign[16]: 14:0x0508 */
  DS_DEFAULT_SHIP_DESIGNS = 0x417A, /* static ShipDesign[16] (0x4A4A - 0x417A = 0x8D0): 14:0x0508 points DS:0xBE[current player] here */
  DS_SB_DESIGNS = 0x010C,      /* FARPTR[16 players] -> ShipDesign[10]: 14:0x0508 */
  DS_MESSAGE_BUF = 0x0AAC,     /* far pointer to message bytes; used length DS:0xAB0, count DS:0xAB4: 6:0x752e (the per-id value-count table is NOT in DGROUP: it is bytes of code segment 6 at cs:0x0E74 + id, src/seg06.c nvals()) */
  DS_MAP_OBJECTS = 0x188E,     /* far pointer to MapObject[DS:0x1892], capacity DS:0x1894: 14:0x0508 */
  DS_ZIP_PRESETS = 0x2576,     /* ZipPreset[4]: 26:0x2d26 */
  DS_PLANET_XY = 0x31BA,       /* {u16 x, u16 y}[GameHeader.planet_count]: 14:0x0508 */
  DS_PLAYER_BATTLE_PLANS_COUNT = 0x4A4A, /* uint8_t[16]: 14:0x0508, 30:0x049a */
  DS_PLANET_COUNT = 0x4D96,    /* 14:0x0508 */
  DS_FLEET_COUNT = 0x553A,     /* 14:0x0508 */
  DS_SCORES = 0x177A,          /* far pointer to ScoreRecord[GameHeader.player_count] (0x18 each): 14:0x0508 allocates player_count * 0x18, 14:0x35b0 writes type 45, 23:0x3b78 fills, 32:0x158e reads */
  DS_PLAYERS = 0x5A64,         /* Player[GameHeader.player_count], stride 0xC0: 14:0x0508 */
  DS_BATTLE_PLAN_PTRS = 0x59FA, /* FARPTR[16 players] -> BattlePlan[16]: 14:0x0508, 30:0x049a */
  DS_HEAP_HEADS = 0x0D74,      /* FARPTR[12] -> first HeapSeg of heap n (0 = none): 12:0x0000, 0x0098, 0x01fc, 0x0232, 0x0372 */
  DS_HEAP_MIN = 0x0DA4         /* uint16_t[12], minimum segment size of heap n (initial data): 12:0x0000, 0x0098 */
};

/* BlockHeader word: size is the low 10 bits, type the high 6. */
#define BLOCK_SIZE(w) ((uint16_t)((w) & 0x3ff))
#define BLOCK_TYPE(w) ((uint16_t)((uint16_t)(w) >> 10))
#define BLOCK_MAKE(type, size) ((uint16_t)(((uint16_t)(type) << 10) | ((size) & 0x3ff)))

/* Block types (BlockHeader.type) seen in the loader 14:0x0508 and its writers. */
enum {
  BLK_END = 0,          /* 14:0x35b0 and 15:0x1334 end a file with block(0, 2) */
  BLK_PLAYER = 6,       /* 14:0x0508 (header byte 0x18), 14:0x32a0 writes it: Player */
  BLK_GAME_HEADER = 7,  /* 15:0x1334 block(7, 0x40, DS:0x70): GameHeader */
  BLK_FILE_HEADER = 8,  /* 14:0x579a / 14:0x305a special-case it: FileHeader */
  BLK_MESSAGE = 12,     /* 6:0x8308 writes message buffer chunks with type 12 */
  BLK_PLANET_13 = 13,   /* FULL planet form: the same body as type 14 plus the 8 improvement bytes (Planet +0x14), the starbase dword (+0x2C) and route_target (+0x30). Decided from re/decomp/seg14/4926_FUN_1068_4926.c:139-163 (those fields are written only under "param_2 != 0xe" and the block is emitted as type 0xd) and re/decomp/seg14/1f0c_FUN_1068_1f0c.c:165 (the reader skips them when the block type is 14); 14:0x35b0 emits it for a planet whose kind byte is 7 */
  BLK_PLANET = 14,      /* REDUCED planet form (14:0x0508 header byte 0x38): ids, owner, info_level, the 3+3+3 environment/mineral bytes, +0x12 and the four surface amounts, but not the fields named under BLK_PLANET_13; 4926 lines 36 and 177 (the block type 0xe is also the only one whose info_level is clamped to 3 or 4) */
  BLK_FLEET = 16,       /* 14:0x4c86 (layout version 7); 14:0x235e requires it when version >= 7 */
  BLK_FLEET_OLD = 17,   /* 14:0x4c86 (older layout) */
  BLK_WAYPOINT_19 = 19, /* 14:0x235e accepts 19 and 20 as waypoint blocks: Waypoint */
  BLK_WAYPOINT_20 = 20,
  BLK_FLEET_NAME = 21,  /* 14:0x4fa2 writes it, 14:0x235e reads it (header byte 0x54) */
  BLK_DESIGN = 26,      /* 14:0x342a, reader 14:0x0000 (header byte 0x68): ShipDesign */
  BLK_PROD_QUEUE = 28,  /* 14:0x35b0 (count << 2 bytes), 14:0x0508 (header byte 0x70) */
  BLK_BATTLE_PLAN = 30, /* 14:0x50d6, reader 14:0x271a (header byte 0x78): BattlePlan */
  BLK_36 = 36,          /* 14:0x35b0 writes 4 bytes from DS:0x31C; 14:0x0508 (header byte 0x90) reads them */
  BLK_MESSAGE_OUT = 40, /* 6:0x8308, 9:0x7d1e write type 40 */
  BLK_MAP_OBJECT = 43,  /* 14:0x35b0 (0x12-byte records, preceded by a 2-byte count block), 14:0x0508 (0xAC): MapObject */
  BLK_45 = 45           /* 14:0x35b0 writes 0x18-byte ScoreRecord entries from DS:0x177A; 14:0x0508 (header byte 0xB4) reads them */
};

/* Game-file name patterns (strings in DGROUP, formatted with _wsprintf). Offsets are DS offsets; the
 * format string is what stars26i.exe contains there. */
enum {
  DSSTR_X1 = 0x0372,      /* "%s.x1"  used at 4:0x0528 and 10:0x0000 */
  DSSTR_M_N = 0x03D3,     /* "%s.m%d" used at 4:0x506c (player n+1 turn file) */
  DSSTR_XSUF = 0x0786,    /* "%s.x%s" used at 14:0x0508 */
  DSSTR_X_N = 0x0994,     /* "%s.x%d" (referenced as data only; no direct use found) */
  DSSTR_HST = 0x09BE,     /* "%s.hst" used at 14:0x35b0 when the player index is 0xFFFF (host file) */
  DSSTR_M_N2 = 0x09C5,    /* "%s.m%d" used at 14:0x35b0 for player n+1 */
  DSSTR_XY = 0x09E4       /* "%s.xy"  used at 15:0x1334 (universe/map file) */
};


#pragma pack(push, 1)

/* A far pointer in DGROUP as its two words, offset first (the order pass 1 read them). */
struct FarPtrWords {
  uint16_t off;                      /* +0x00 (2) */
  uint16_t seg;                      /* +0x02 (2) */
};
STATIC_SIZE(struct FarPtrWords, 0x4);
STATIC_OFFSET(struct FarPtrWords, off, 0x0);
STATIC_OFFSET(struct FarPtrWords, seg, 0x2);

/* BlockHeader: see docs/FILE_FORMATS.md section 2. */
/* BlockHeader (2 bytes)
Every record in a game file starts with this 16-bit word: size = word & 0x3ff, type = word >> 10
(14:0x305a reads it into DS:0x2910 then the body into DS:0x4db2; 14:0x579a builds it as
(type << 10) | size and writes header and body). Block bodies other than the file header and the
end block are scrambled with a keystream seeded from the file header (14:0x305a calls 8:0x8ae6 on a
non-empty body; 14:0x579a calls it before writing). Block type numbers seen in the loader (14:0x0508)
and writers are the BLK_* constants below. */
struct BlockHeader {
  uint16_t word;                     /* +0x00 (2): 14:0x305a (DS:0x2910), 14:0x579a (writer) */
};
STATIC_SIZE(struct BlockHeader, 0x2);
STATIC_OFFSET(struct BlockHeader, word, 0x0);

/* FileHeader: see docs/FILE_FORMATS.md section 3. */
/* FileHeader (16 bytes)
Body of the type-8 block (BLK_FILE_HEADER), read at DS:0x4db2 by 14:0x305a / 14:0x2caa, which
checks it against the open game; it also seeds the keystream (14:0x305a -> 8:0x89aa). */
struct FileHeader {
  uint8_t unknown_00[4];             /* +0x00 (4): no access found in re/decomp; kept as padding */
  uint32_t game_id;                  /* +0x04 (4): 14:0x2caa: compared with GameHeader.game_id (DS:0x70) */
  uint16_t version;                  /* +0x08 (2): 14:0x2caa: (this & 0xfe0) range-checked; stored in DS:0x772 */
  uint16_t turn;                     /* +0x0A (2): 14:0x2caa: compared with GameHeader.turn (DS:0x82) */
  uint16_t player_salt;              /* +0x0C (2): 14:0x2caa: low 5 bits = player index; 14:0x305a seeds 8:0x89aa with this word */
  uint16_t flags;                    /* +0x0E (2): 14:0x2caa: bits 0x100/0x200/0x400 tested; 14:0x305a bit 4 of the high byte seeds 8:0x89aa */
};
STATIC_SIZE(struct FileHeader, 0x10);
STATIC_OFFSET(struct FileHeader, unknown_00, 0x0);
STATIC_OFFSET(struct FileHeader, game_id, 0x4);
STATIC_OFFSET(struct FileHeader, version, 0x8);
STATIC_OFFSET(struct FileHeader, turn, 0xA);
STATIC_OFFSET(struct FileHeader, player_salt, 0xC);
STATIC_OFFSET(struct FileHeader, flags, 0xE);

/* GameHeader: see docs/FILE_FORMATS.md section 4. */
/* GameHeader (64 bytes)
The type-7 block (BLK_GAME_HEADER): 0x40 bytes, resident at DGROUP 0x0070 (14:0x0508 copies 16
dwords from DS:0x4db2 to DS:0x70; 15:0x1334 writes it back with block(7, 0x40, DS:0x70)). */
struct GameHeader {
  uint32_t game_id;                  /* +0x00 (4): 14:0x2caa compares it with FileHeader.game_id; 14:0x2992 clears it */
  uint16_t universe_size;            /* +0x04 (2): 14:0x0508: (this+1)*400 -> DS:0xB0; 15:0x4efe, 15:0x73a8 */
  uint16_t fld_06;                   /* +0x06 (2): meaning not proven: only these accesses found: 15:0x1334, 15:0x6ab2 (new game setup) */
  uint16_t player_count;             /* +0x08 (2): 14:0x0508: loop bound over the 0xC0-byte player table */
  uint16_t planet_count;             /* +0x0A (2): 14:0x0508: loop bound filling the planet x/y tables (DS:0x31BA) */
  uint16_t fld_0c;                   /* +0x0C (2): meaning not proven: only these accesses found: 15:0x1334 (multiplied into a count), 15:0x6ab2 writes 1 or 2 */
  uint16_t fld_0e;                   /* +0x0E (2): meaning not proven: only these accesses found: 14:0x0508 and 14:0x2992 clear it; 9:0x641c */
  uint8_t flags_80;                  /* +0x10 (1): 14:0x0508/14:0x35b0/14:0x2caa: bit tests (0x04, 0x08); 14:0x2caa rewrites bits 1-3 */
  uint8_t flags_81;                  /* +0x11 (1): 14:0x2caa, 15:0x1334 */
  uint16_t turn;                     /* +0x12 (2): 14:0x0508: loaded from the file header; 14:0x1f0c/14:0x235e stamp records with it; 14:0x2992 clears */
  uint8_t fld_84[9];                 /* +0x14 (9): meaning not proven: only these accesses found: 15:0x6ab2 writes DS:0x84..0x8C one byte at a time; 15:0x47f0 writes 0x8B/0x8C */
  uint8_t unknown_1D[3];             /* +0x1D (3): no access found in re/decomp; kept as padding */
  uint8_t game_name[32];             /* +0x20 (32): 4:0x1f40 prints it as a NUL-terminated string from DS:0x90; 14:0x2992 sets byte 0 to 0 (extent = rest of the 0x40-byte block) */
};
STATIC_SIZE(struct GameHeader, 0x40);
STATIC_OFFSET(struct GameHeader, game_id, 0x0);
STATIC_OFFSET(struct GameHeader, universe_size, 0x4);
STATIC_OFFSET(struct GameHeader, fld_06, 0x6);
STATIC_OFFSET(struct GameHeader, player_count, 0x8);
STATIC_OFFSET(struct GameHeader, planet_count, 0xA);
STATIC_OFFSET(struct GameHeader, fld_0c, 0xC);
STATIC_OFFSET(struct GameHeader, fld_0e, 0xE);
STATIC_OFFSET(struct GameHeader, flags_80, 0x10);
STATIC_OFFSET(struct GameHeader, flags_81, 0x11);
STATIC_OFFSET(struct GameHeader, turn, 0x12);
STATIC_OFFSET(struct GameHeader, fld_84, 0x14);
STATIC_OFFSET(struct GameHeader, unknown_1D, 0x1D);
STATIC_OFFSET(struct GameHeader, game_name, 0x20);

/* Player: see docs/FILE_FORMATS.md section 5. */
/* Player (192 bytes)
Player record, table at DGROUP 0x5A64, stride 0xC0 (14:0x0508 indexes DS:0x5A64 + i*0xC0 and
zeroes player_count*0xC0 bytes; 14:0x0370 reads the type-6 block into it). The first 0x70 bytes are the
block body copied verbatim when (record_flags & 7) == 7 (14:0x0370), else only the first 8. */
struct Player {
  uint8_t player_id;                 /* +0x00 (1): 14:0x0508: block byte 0 selects the table slot; 15:0x1334 */
  uint8_t design_count;              /* +0x01 (1): 14:0x0508: number of type-26 design blocks read for this player; 14:0x38fe */
  uint16_t planet_count;             /* +0x02 (2): 14:0x0508: zeroed after load, then +1 per planet whose owner is this player; 17:0x0462 */
  uint16_t fleet_and_sb_counts;      /* +0x04 (2): 14:0x0508: (this & 0xfff) = fleet count summed into DS:0x553A; (this >> 12) = starbase design count; 14:0x444e */
  uint8_t record_flags;              /* +0x06 (1): 14:0x0370: (this & 7) == 7 selects the long record */
  uint8_t status_flags;              /* +0x07 (1): 14:0x0508/14:0x35b0: bit 0 and bit 1 tested; 18:0x098a */
  uint8_t unknown_08[2];             /* +0x08 (2): no access found in re/decomp; kept as padding */
  uint16_t fld_0a;                   /* +0x0A (2): meaning not proven: only these accesses found: 23:0x3b78 copies this word (DS:0x5A6E + i*0xC0) into ScoreRecord.rank for every player */
  uint32_t fld_0c;                   /* +0x0C (4): meaning not proven: only these accesses found: 14:0x0508: DS:0x31C = this (of the current player) */
  uint8_t unknown_10[40];            /* +0x10 (40): no access found in re/decomp; kept as padding */
  int8_t cost_adjust_pct;            /* +0x38 (1): 26:0x29da: signed percent byte (DS:0x5A9C + i*0xC0); pct * cost / 100 is subtracted from each production item cost */
  uint8_t unknown_39[5];             /* +0x39 (5): no access found in re/decomp; kept as padding */
  int8_t attrs[15];                  /* +0x3E (15): PLAYER_ATTR_COUNT entries; callers in re/decomp pass indices 0-8, 10 and 14 only, never above 14 (the clamp tables DS:0x300/0x310 are 16 bytes apart); 28:0x21ee reads attrs[i]; 28:0x2200 writes it clamped to DS:0x300/0x310 tables; index 14 used at 14:0x1f0c and 14:0x35b0 (race wizard values) */
  uint8_t unknown_4D[1];             /* +0x4D (1): no access found in re/decomp; kept as padding */
  uint32_t trait_bits;               /* +0x4E (4): 28:0x2230 tests bit n in the words at +0x4E/+0x50; 28:0x2254 sets/clears (race wizard) */
  uint8_t unknown_52[2];             /* +0x52 (2): no access found in re/decomp; kept as padding */
  uint16_t fld_54;                   /* +0x54 (2): meaning not proven: only these accesses found: 14:0x0370 masks it with 7; 14:0x0508 tests bit 0 of its low byte (DS:0x5AB8); 9:0x78a0 */
  uint8_t unknown_56[26];            /* +0x56 (26): no access found in re/decomp; kept as padding */
  uint8_t relations[16];             /* +0x70 (16): 14:0x0370 copies block[0x70] bytes from block+0x71 to +0x70 (extent up to the name at +0x80) */
  uint8_t name[32];                  /* +0x80 (32): 14:0x0370 (plain or compressed string, <= 0x20 bytes, via 8:0xa666) */
  uint8_t plural_name[32];           /* +0xA0 (32): 14:0x0370 (same decoding) */
};
STATIC_SIZE(struct Player, 0xC0);
STATIC_OFFSET(struct Player, player_id, 0x0);
STATIC_OFFSET(struct Player, design_count, 0x1);
STATIC_OFFSET(struct Player, planet_count, 0x2);
STATIC_OFFSET(struct Player, fleet_and_sb_counts, 0x4);
STATIC_OFFSET(struct Player, record_flags, 0x6);
STATIC_OFFSET(struct Player, status_flags, 0x7);
STATIC_OFFSET(struct Player, unknown_08, 0x8);
STATIC_OFFSET(struct Player, fld_0a, 0xA);
STATIC_OFFSET(struct Player, fld_0c, 0xC);
STATIC_OFFSET(struct Player, unknown_10, 0x10);
STATIC_OFFSET(struct Player, cost_adjust_pct, 0x38);
STATIC_OFFSET(struct Player, unknown_39, 0x39);
STATIC_OFFSET(struct Player, attrs, 0x3E);
STATIC_OFFSET(struct Player, unknown_4D, 0x4D);
STATIC_OFFSET(struct Player, trait_bits, 0x4E);
STATIC_OFFSET(struct Player, unknown_52, 0x52);
STATIC_OFFSET(struct Player, fld_54, 0x54);
STATIC_OFFSET(struct Player, unknown_56, 0x56);
STATIC_OFFSET(struct Player, relations, 0x70);
STATIC_OFFSET(struct Player, name, 0x80);
STATIC_OFFSET(struct Player, plural_name, 0xA0);

/* Planet: see docs/FILE_FORMATS.md section 5. */
/* Planet (56 bytes)
Planet record: far array DS:0xB6/0xB8, stride 0x38 (14:0x0508 allocates count*0x38; 14:0x1f0c reads
block types 13/14 into it, 14:0x4926 writes them). Count in DS:0x4D96. */
struct Planet {
  uint16_t id;                       /* +0x00 (2): 14:0x1f0c: low 11 bits of the block word; 14:0x0508 sorts/merges by it */
  uint16_t owner;                    /* +0x02 (2): 14:0x1f0c, 14:0x0508: 0xFFFF = unowned; indexes the player table */
  uint8_t info_level;                /* +0x04 (1): 14:0x1f0c compares it with the block detail field (4db4 & 0x7f) */
  uint8_t flags;                     /* +0x05 (1): 14:0x1f0c, 14:0x0508: bit 1 = has a starbase, bit 3 = updated this turn */
  uint8_t fld_06[3];                 /* +0x06 (3): meaning not proven: only these accesses found: 14:0x1f0c reads 3 optional bytes into +6..+8; 14:0x4926 writes them */
  uint8_t fld_09[3];                 /* +0x09 (3): meaning not proven: only these accesses found: 14:0x1f0c: 3 bytes; 14:0x4926 */
  uint8_t env_current[3];            /* +0x0C (3): 14:0x1f0c: 3 bytes (each <= 100) read first and copied to env_original; 14:0x4926 (the three values are the planet environment; per-axis order not proven) */
  uint8_t env_original[3];           /* +0x0F (3): 14:0x1f0c: copy of env_current unless the block carries its own (word flag 0x400); 14:0x4926 sets flag 0x400 when it differs from env_current */
  uint16_t fld_12;                   /* +0x12 (2): meaning not proven: only these accesses found: 14:0x1f0c: word read when the block has it; 14:0x4926 */
  uint8_t fld_14[8];                 /* +0x14 (8): meaning not proven: only these accesses found: 14:0x1f0c: 8 bytes copied from the block via 34:0x0c98; 14:0x4926 writes words +0x14..+0x1B */
  uint32_t fld_1c[4];                /* +0x1C (16): meaning not proven: only these accesses found: 14:0x1f0c: four 0/1/2/4-byte values widened to dwords; 14:0x4926 */
  uint32_t starbase;                 /* +0x2C (4): 14:0x1f0c, 14:0x4926: present when flags bit 1; low nibble of the low byte = starbase design index (14:0x0508) */
  uint16_t route_target;             /* +0x30 (2): (this & 0x3ff) = id + 1 of the planet this planet routes to, 0 = none: 16:0x13f8 (minus 1 gives the planet id); 14:0x1f0c, 14:0x4926 (written when (this & 0x3ff) != 0) */
  uint16_t turn;                     /* +0x32 (2): 14:0x1f0c: = GameHeader.turn when the planet record is refreshed */
  FARPTR queue;                      /* +0x34 (4): 14:0x0508: far pointer to the ProdQueueHdr loaded from a type-28 block (offset at +0x34, selector at +0x36) */
};
STATIC_SIZE(struct Planet, 0x38);
STATIC_OFFSET(struct Planet, id, 0x0);
STATIC_OFFSET(struct Planet, owner, 0x2);
STATIC_OFFSET(struct Planet, info_level, 0x4);
STATIC_OFFSET(struct Planet, flags, 0x5);
STATIC_OFFSET(struct Planet, fld_06, 0x6);
STATIC_OFFSET(struct Planet, fld_09, 0x9);
STATIC_OFFSET(struct Planet, env_current, 0xC);
STATIC_OFFSET(struct Planet, env_original, 0xF);
STATIC_OFFSET(struct Planet, fld_12, 0x12);
STATIC_OFFSET(struct Planet, fld_14, 0x14);
STATIC_OFFSET(struct Planet, fld_1c, 0x1C);
STATIC_OFFSET(struct Planet, starbase, 0x2C);
STATIC_OFFSET(struct Planet, route_target, 0x30);
STATIC_OFFSET(struct Planet, turn, 0x32);
STATIC_OFFSET(struct Planet, queue, 0x34);

/* Waypoint: see docs/FILE_FORMATS.md section 5. */
/* Waypoint (18 bytes)
Waypoint, 0x12 bytes (14:0x235e reads type 19/20 blocks into them; 14:0x35b0 appends one by copying
the target's x,y dword). A fleet's list is a WaypointListHdr followed by waypoint_count + 1 of these
(14:0x235e allocates (waypoint_count+1)*0x12 after a 4-byte header and zeroes it). */
struct Waypoint {
  uint16_t x;                        /* +0x00 (2): 14:0x35b0: dword copied from Fleet/Planet +8 (x,y) */
  uint16_t y;                        /* +0x02 (2): 14:0x35b0: same dword copy */
  uint16_t target_id;                /* +0x04 (2): 14:0x35b0: = target Fleet word 0 */
  uint16_t flags;                    /* +0x06 (2): 14:0x35b0: nibble masks 0xF, 0xF0 and 0x0F00 are rewritten on this word; 14:0x235e clears bit 5 of the high byte */
  uint16_t fld_08;                   /* +0x08 (2): meaning not proven: only these accesses found: 14:0x35b0: copied together with +0xA as one dword */
  uint16_t fld_0a;                   /* +0x0A (2): meaning not proven: only these accesses found: 14:0x35b0: (this + 1) * 0x32 used as a distance */
  uint8_t unknown_0C[6];             /* +0x0C (6): no access found in re/decomp; kept as padding */
};
STATIC_SIZE(struct Waypoint, 0x12);
STATIC_OFFSET(struct Waypoint, x, 0x0);
STATIC_OFFSET(struct Waypoint, y, 0x2);
STATIC_OFFSET(struct Waypoint, target_id, 0x4);
STATIC_OFFSET(struct Waypoint, flags, 0x6);
STATIC_OFFSET(struct Waypoint, fld_08, 0x8);
STATIC_OFFSET(struct Waypoint, fld_0a, 0xA);
STATIC_OFFSET(struct Waypoint, unknown_0C, 0xC);

/* WaypointListHdr (4 bytes)
Header of a fleet's waypoint list (Fleet.waypoints): 4 bytes, then Waypoint[count + 1]. */
struct WaypointListHdr {
  uint8_t unknown_00[3];             /* +0x00 (3): no access found in re/decomp; kept as padding */
  uint8_t count;                     /* +0x03 (1): 14:0x235e stores the waypoint count at +3; 9:0x651e */
};
STATIC_SIZE(struct WaypointListHdr, 0x4);
STATIC_OFFSET(struct WaypointListHdr, unknown_00, 0x0);
STATIC_OFFSET(struct WaypointListHdr, count, 0x3);

/* Fleet: see docs/FILE_FORMATS.md section 5. */
/* Fleet (124 bytes)
Fleet record: far pointer array DS:0xBA/0xBC (4 bytes per fleet), count in DS:0x553A; each fleet is a
0x7C-byte allocation (14:0x0508 allocates 0x7C per fleet and calls 14:0x235e; 14:0x4c86 writes it).
Bytes 0..0xB are the type-16/17 block header copied verbatim (14:0x235e). */
struct Fleet {
  uint16_t id;                       /* +0x00 (2): 14:0x35b0 passes it as the fleet id in message arguments; 14:0x4c86 */
  uint16_t owner;                    /* +0x02 (2): 14:0x0508: indexes the player table; 30:0x0fe8 compares it with the current player */
  uint8_t version;                   /* +0x04 (1): 14:0x235e: > 3 reads cargo, < 7 old layout, 7 current; 14:0x4c86 */
  uint8_t flags;                     /* +0x05 (1): 14:0x235e: bit 2 = empty, bit 3 = byte-sized counts; 14:0x35b0 bits 0, 1, 2 and 7 */
  uint16_t planet_id;                /* +0x06 (2): 14:0x235e: 0xFFFF = in deep space, else checked against the planet tables (DS:0x31BA) */
  union {
    struct {
      uint16_t x;                    /* +0x08 (2): 14:0x35b0: distance computation; 14:0x235e compares it with DS:0x31BA */
      uint16_t y;                    /* +0x0A (2): 14:0x35b0: distance computation; 14:0x235e */
    };
    uint32_t xy;                     /* +0x08 (4): 5:0x36a8 compares the dword (x, y) with the selected place DS:0x4B7C */
  };
  uint16_t ship_count[16];           /* +0x0C (32): 14:0x235e/14:0x4c86: one word per design slot (mask-compressed on disk) */
  uint16_t damage[16];               /* +0x2C (32): per design slot, 16:0x0962: low 7 bits (FLEET_DAMAGE_PCT_MASK) = percent of the slot's ships that are damaged, bits 7+ (FLEET_DAMAGE_SHIFT) = damage per damaged ship (scaled by the design strength /500); 14:0x235e (version 7: mask-compressed words), 14:0x4c86; version < 7 reads a dword here */
  uint32_t cargo[4];                 /* +0x4C (16): 14:0x235e/14:0x4c86: four 0/1/2/4-byte values; 16:0x1e02 sums them as the fleet cargo, 16:0x13f8 tests all four for zero; kind labels not proven */
  uint32_t fuel;                     /* +0x5C (4): the fifth value of the 14:0x235e/14:0x4c86 run; 16:0x13f8 compares it with the jump cost (the fifth cargo dword); 19:0x0000 tests it */
  uint8_t battle_plan;               /* +0x60 (1): 30:0x0fe8, 14:0x4c86: index into the owner battle-plan table */
  uint8_t unknown_61[1];             /* +0x61 (1): no access found in re/decomp; kept as padding */
  uint16_t waypoint_count;           /* +0x62 (2): 14:0x235e, 14:0x4c86 (byte at +0x62) */
  FARPTR waypoints;                  /* +0x64 (4): 14:0x235e: far pointer (offset +0x64, selector +0x66) to a WaypointListHdr */
  uint8_t unknown_68[12];            /* +0x68 (12): no access found in re/decomp; kept as padding */
  uint32_t fld_74;                   /* +0x74 (4): meaning not proven: only these accesses found: 14:0x235e/14:0x4c86 (old version-< 7 layout) */
  FARPTR name;                       /* +0x78 (4): 14:0x235e allocates the name string; 14:0x4c86 passes it to 14:0x4fa2 */
};
STATIC_SIZE(struct Fleet, 0x7C);
STATIC_OFFSET(struct Fleet, id, 0x0);
STATIC_OFFSET(struct Fleet, owner, 0x2);
STATIC_OFFSET(struct Fleet, version, 0x4);
STATIC_OFFSET(struct Fleet, flags, 0x5);
STATIC_OFFSET(struct Fleet, planet_id, 0x6);
STATIC_OFFSET(struct Fleet, x, 0x8);
STATIC_OFFSET(struct Fleet, y, 0xA);
STATIC_OFFSET(struct Fleet, xy, 0x8);
STATIC_OFFSET(struct Fleet, ship_count, 0xC);
STATIC_OFFSET(struct Fleet, damage, 0x2C);
STATIC_OFFSET(struct Fleet, cargo, 0x4C);
STATIC_OFFSET(struct Fleet, fuel, 0x5C);
STATIC_OFFSET(struct Fleet, battle_plan, 0x60);
STATIC_OFFSET(struct Fleet, unknown_61, 0x61);
STATIC_OFFSET(struct Fleet, waypoint_count, 0x62);
STATIC_OFFSET(struct Fleet, waypoints, 0x64);
STATIC_OFFSET(struct Fleet, unknown_68, 0x68);
STATIC_OFFSET(struct Fleet, fld_74, 0x74);
STATIC_OFFSET(struct Fleet, name, 0x78);

/* DesignSlot (4 bytes)
One component slot of a design (16 per design). */
struct DesignSlot {
  uint16_t category;                 /* +0x00 (2): 14:0x0000: used as a bit mask against a table entry */
  uint8_t item;                      /* +0x02 (1): 14:0x0000: low byte of the word at slot+2 */
  uint8_t count;                     /* +0x03 (1): 14:0x0000: zeroed when invalid; multiplied into the cost sum */
};
STATIC_SIZE(struct DesignSlot, 0x4);
STATIC_OFFSET(struct DesignSlot, category, 0x0);
STATIC_OFFSET(struct DesignSlot, item, 0x2);
STATIC_OFFSET(struct DesignSlot, count, 0x3);

/* ShipDesign: see docs/FILE_FORMATS.md section 5. */
/* ShipDesign (141 bytes)
Ship or starbase design, 0x8D bytes: per player a far pointer table DS:0xBE (16 ship designs =
0x8D0 bytes) and DS:0x10C (10 starbase designs = 0x582 bytes) (14:0x0508). 14:0x0000 reads a type-26
block into one, 14:0x342a writes it. */
struct ShipDesign {
  uint16_t hull;                     /* +0x00 (2): 14:0x0000: compared as a word with block byte 2; 14:0x342a writes the low byte */
  uint8_t unknown_02[6];             /* +0x02 (6): no access found in re/decomp; kept as padding */
  uint8_t name[32];                  /* +0x08 (32): 14:0x0000/14:0x342a: string copied to/from +8 (<= 0x20 bytes) */
  uint16_t mass;                     /* +0x28 (2): 16:0x1e02 multiplies the ship count by it, 16:0x13f8 picks the heaviest design by it, 16:0x0962; 14:0x0000 (old layout), 14:0x342a */
  uint8_t unknown_2A[8];             /* +0x2A (8): no access found in re/decomp; kept as padding */
  uint16_t fld_32;                   /* +0x32 (2): meaning not proven: only these accesses found: 14:0x0000: compared with block byte 3; 14:0x342a */
  uint8_t unknown_34[4];             /* +0x34 (4): no access found in re/decomp; kept as padding */
  uint16_t strength;                 /* +0x38 (2): 16:0x0962 reads it as the design strength (damage is scaled by it); 14:0x0000 (version 7), 14:0x342a */
  struct DesignSlot slots[16];       /* +0x3A (64): 14:0x0000: block bytes copied here ((count << 2) bytes); 14:0x342a */
  uint8_t slot_count;                /* +0x7A (1): 14:0x0000, 14:0x342a */
  uint8_t version;                   /* +0x7B (1): 14:0x0000: (this < 7) selects the old layout; 14:0x342a */
  uint8_t flags;                     /* +0x7C (1): 14:0x0508 sets bit 1 on every slot at load (free); 14:0x0000 tests bits 1 and 7 */
  uint16_t fld_7d;                   /* +0x7D (2): meaning not proven: only these accesses found: 14:0x0000 and 14:0x342a (version 7 only) */
  uint32_t fld_7f;                   /* +0x7F (4): meaning not proven: only these accesses found: 14:0x0000 and 14:0x342a (version 7 only) */
  uint32_t fld_83;                   /* +0x83 (4): meaning not proven: only these accesses found: 14:0x0000 and 14:0x342a (version 7 only) */
  uint8_t unknown_87[4];             /* +0x87 (4): no access found in re/decomp; kept as padding */
  uint16_t fld_8b;                   /* +0x8B (2): meaning not proven: only these accesses found: 14:0x0508 zeroes it for every design slot */
};
STATIC_SIZE(struct ShipDesign, 0x8D);
STATIC_OFFSET(struct ShipDesign, hull, 0x0);
STATIC_OFFSET(struct ShipDesign, unknown_02, 0x2);
STATIC_OFFSET(struct ShipDesign, name, 0x8);
STATIC_OFFSET(struct ShipDesign, mass, 0x28);
STATIC_OFFSET(struct ShipDesign, unknown_2A, 0x2A);
STATIC_OFFSET(struct ShipDesign, fld_32, 0x32);
STATIC_OFFSET(struct ShipDesign, unknown_34, 0x34);
STATIC_OFFSET(struct ShipDesign, strength, 0x38);
STATIC_OFFSET(struct ShipDesign, slots, 0x3A);
STATIC_OFFSET(struct ShipDesign, slot_count, 0x7A);
STATIC_OFFSET(struct ShipDesign, version, 0x7B);
STATIC_OFFSET(struct ShipDesign, flags, 0x7C);
STATIC_OFFSET(struct ShipDesign, fld_7d, 0x7D);
STATIC_OFFSET(struct ShipDesign, fld_7f, 0x7F);
STATIC_OFFSET(struct ShipDesign, fld_83, 0x83);
STATIC_OFFSET(struct ShipDesign, unknown_87, 0x87);
STATIC_OFFSET(struct ShipDesign, fld_8b, 0x8B);

/* BattlePlan: see docs/FILE_FORMATS.md section 5. */
/* BattlePlan (36 bytes)
Battle plan, 0x24 bytes: per player a far pointer in DS:0x59FA (16 plans = 0x240 bytes, 14:0x0508),
plan count in DS:0x4A4A[player]. 14:0x271a reads a type-30 block, 14:0x50d6 writes it,
30:0x049a (BATTLEPLANSDLG) edits a copy in DS:0x53B8. */
struct BattlePlan {
  uint8_t fld_00;                    /* +0x00 (1): meaning not proven: only these accesses found: 14:0x271a: high nibble = plan index; 30:0x049a */
  uint8_t flags;                     /* +0x01 (1): 14:0x50d6: bit 6 = default name (name not written); 30:0x049a bits 6,7 */
  uint8_t fld_02;                    /* +0x02 (1): meaning not proven: only these accesses found: 30:0x049a: two 4-bit fields (dialog combo boxes) */
  uint8_t fld_03;                    /* +0x03 (1): meaning not proven: only these accesses found: 30:0x049a: (this & 0x1f) shown in a dialog list */
  uint8_t name[32];                  /* +0x04 (32): 14:0x271a copies the string to +4; 14:0x50d6 writes it */
};
STATIC_SIZE(struct BattlePlan, 0x24);
STATIC_OFFSET(struct BattlePlan, fld_00, 0x0);
STATIC_OFFSET(struct BattlePlan, flags, 0x1);
STATIC_OFFSET(struct BattlePlan, fld_02, 0x2);
STATIC_OFFSET(struct BattlePlan, fld_03, 0x3);
STATIC_OFFSET(struct BattlePlan, name, 0x4);

/* ProdQueueHdr (4 bytes)
Production queue header (Planet.queue): then count ProdItem entries (4 bytes each). 14:0x0508 loads a
type-28 block (size/4 entries) behind a 4-byte header; 14:0x35b0 writes count << 2 bytes. */
struct ProdQueueHdr {
  uint8_t unknown_00[2];             /* +0x00 (2): no access found in re/decomp; kept as padding */
  uint8_t capacity;                  /* +0x02 (1): 14:0x0508: reallocated when (this <= entries in the new block) */
  uint8_t count;                     /* +0x03 (1): 14:0x0508, 14:0x35b0, 26:0x29da */
};
STATIC_SIZE(struct ProdQueueHdr, 0x4);
STATIC_OFFSET(struct ProdQueueHdr, unknown_00, 0x0);
STATIC_OFFSET(struct ProdQueueHdr, capacity, 0x2);
STATIC_OFFSET(struct ProdQueueHdr, count, 0x3);

/* ProdItem: see docs/FILE_FORMATS.md section 5. */
/* ProdItem (4 bytes)
One production queue entry (4 bytes). Observed masks (26:0x29da): word0 & 0x3ff = quantity,
word0 & 0xfc00 and (word1 & 0xe) == 2 select the item kind (14:0x0508 end: kind check 0x1bff). */
struct ProdItem {
  uint16_t w0;                       /* +0x00 (2): 26:0x29da, 14:0x0508 */
  uint16_t w1;                       /* +0x02 (2): 26:0x29da, 14:0x0508 */
};
STATIC_SIZE(struct ProdItem, 0x4);
STATIC_OFFSET(struct ProdItem, w0, 0x0);
STATIC_OFFSET(struct ProdItem, w1, 0x2);
/* ProdItem bit fields (26:0x29da): w0 & PRODITEM_QTY_MASK = quantity (an empty entry is skipped);
 * (w1 & PRODITEM_W1_KIND_MASK) == PRODITEM_W1_KIND_SPECIAL selects the item class that 26:0x29da
 * handles apart (its sub-kind is (w0w1 & 0x1fc) >> 2 there). Other bits: see the placeholders in the
 * report, not decoded. */
enum {
  PRODITEM_QTY_MASK = 0x03FF,
  PRODITEM_W1_KIND_MASK = 0x000E,
  PRODITEM_W1_KIND_SPECIAL = 0x0002,
  PRODITEM_ITEM_SHIFT = 10,        /* item index = (w0 >> 10) | ((w1 & 1) << 6): 26:0x29da (CONCAT11(w1 low, w0 high) & 0x1fc) >> 2 */
  PRODITEM_ITEM_DWORD_MASK = 0x1FC00, /* the same bits of the dword w1:w0; 26:0x29da compares them with 0xC00 (index 3) */
  PRODITEM_CLASS_SHIFT = 1         /* class = (w1 & 0xE) >> 1 */
};
static inline uint16_t prod_item_qty(const struct ProdItem *it) { return (uint16_t)(it->w0 & PRODITEM_QTY_MASK); }
static inline uint16_t prod_item_class(const struct ProdItem *it) {
  return (uint16_t)((it->w1 & PRODITEM_W1_KIND_MASK) >> PRODITEM_CLASS_SHIFT);
}
static inline uint16_t prod_item_index(const struct ProdItem *it) {
  return (uint16_t)((it->w0 >> PRODITEM_ITEM_SHIFT) | ((it->w1 & 1) << 6));
}

/* ZipPreset (40 bytes)
Saved ZIP production preset, 4 of them at DGROUP 0x2576, stride 0x28 (26:0x2d26 ZIPPRODDLG indexes
DS:0x2576 + n*0x28; 4 radio buttons 0x431..0x434). */
struct ZipPreset {
  uint8_t name[13];                  /* +0x00 (13): 26:0x2d26: NUL-terminated string at +0, terminated before the flag at +0xD */
  uint8_t in_use;                    /* +0x0D (1): 26:0x2d26 (DS:0x2583 + n*0x28), 26:0x33e4, 26:0x3428 */
  uint8_t kind;                      /* +0x0E (1): 26:0x2d26: this + 0x4B5 is a string id (DS:0x2584 + n*0x28) */
  uint8_t count;                     /* +0x0F (1): 26:0x3428: number of items */
  uint16_t items[12];                /* +0x10 (24): 26:0x3428: (item & 0x3f) + 0x7E is a string id; extent = rest of the 0x28 stride */
};
STATIC_SIZE(struct ZipPreset, 0x28);
STATIC_OFFSET(struct ZipPreset, name, 0x0);
STATIC_OFFSET(struct ZipPreset, in_use, 0xD);
STATIC_OFFSET(struct ZipPreset, kind, 0xE);
STATIC_OFFSET(struct ZipPreset, count, 0xF);
STATIC_OFFSET(struct ZipPreset, items, 0x10);

/* ScoreRecord (24 bytes)
Per-player record of the type-45 block (BLK_45), far array DS:0x177A, one entry per player, stride 0x18
(14:0x0508 allocates player_count * 0x18, stores the block at index id & 0x1f; 14:0x35b0 writes 0x18
bytes per player; 23:0x3b78 fills it each turn; 32:0x158e and 32:0x0fa8 show it, one column per field
index 0..8). Names follow the column order and are inferred from it, not read from strings. */
struct ScoreRecord {
  uint16_t id_flags;                 /* +0x00 (2): 14:0x0508: (this & 0x1f) = player index; 0x20 = record valid (32:0x158e, 23:0x3b78 sets it); 0x0040 and byte-1 bits 0x01/0x04/0x08/0x10/0x40/0x80 are flags set by 23:0x3b78 */
  uint16_t rank;                     /* +0x02 (2): 32:0x158e column 8; 23:0x3b78 copies Player.fld_0a here; 14:0x0508 reads it as a turn when byte 1 has 0x80 */
  uint32_t score;                    /* +0x04 (4): 32:0x158e column 7 (dword) */
  uint32_t resources;                /* +0x08 (4): 32:0x158e column 6 (dword) */
  uint16_t planets;                  /* +0x0C (2): 32:0x158e column 0 */
  uint16_t starbases;                /* +0x0E (2): 32:0x158e column 1 */
  uint16_t ships[3];                 /* +0x10 (6): 32:0x158e columns 2-4: low 13 bits mantissa, bits 13-15 times 2 = left shift applied to it */
  uint16_t tech;                     /* +0x16 (2): 32:0x158e column 5 */
};
STATIC_SIZE(struct ScoreRecord, 0x18);
STATIC_OFFSET(struct ScoreRecord, id_flags, 0x0);
STATIC_OFFSET(struct ScoreRecord, rank, 0x2);
STATIC_OFFSET(struct ScoreRecord, score, 0x4);
STATIC_OFFSET(struct ScoreRecord, resources, 0x8);
STATIC_OFFSET(struct ScoreRecord, planets, 0xC);
STATIC_OFFSET(struct ScoreRecord, starbases, 0xE);
STATIC_OFFSET(struct ScoreRecord, ships, 0x10);
STATIC_OFFSET(struct ScoreRecord, tech, 0x16);

/* MapObject: see docs/FILE_FORMATS.md section 5. */
/* MapObject (18 bytes)
18-byte map object (minefield window data of 5:0x0000 MINEWNDPROC; type-43 blocks, BLK_MAP_OBJECT).
Far array DS:0x188E/0x1890, count DS:0x1892, capacity DS:0x1894. 14:0x0508 copies the block and sets +0x10 to the
current turn; 14:0x35b0 writes blocks of 0x12 bytes. Bits 5-7 of byte +1 select a kind (0x20 is handled as a
mineral-packet-like object, 14:0x0508 end). */
struct MapObject {
  union {
    uint16_t id_kind;                /* +0x00 (2): 14:0x0508: array is kept sorted by this word; 5:0x0474 compares word 0 with its key; kind = (byte at +1) & 0xE0 (5:0x2840, 7:0x063e) */
    struct {
      uint8_t id_lo;                 /* +0x00 (1): low byte of id_kind (5:0x0474 and the diff scenarios write it) */
      uint8_t owner_kind_bits;       /* +0x01 (1): 5:0x0474: bits 1-4 (MAPOBJ_OWNER_MASK) = owner player compared with DS:0x14C, bits 5-7 (MAPOBJ_KIND_MASK) = kind; 5:0x2840 */
    };
  };
  uint32_t xy;                       /* +0x02 (4): 5:0x36a8: dword compared with the selected place DS:0x4B7C (a fleet's x,y dword is at +8) */
  uint16_t fld_06;                   /* +0x06 (2): meaning not proven: only these accesses found: 14:0x0508: (this & 0x3ff) is a planet id when kind == 0x20; byte +7 & 0x3c tested */
  uint8_t unknown_08[4];             /* +0x08 (4): no access found in re/decomp; kept as padding */
  uint8_t fld_0c;                    /* +0x0C (1): meaning not proven: only these accesses found: 5:0x2ab4: + 0x4DE used as a string id */
  uint8_t fld_0d;                    /* +0x0D (1): meaning not proven: only these accesses found: 5:0x0000 stores a SendMessage result here; 5:0x329a tests it */
  uint16_t seen_by_mask;             /* +0x0E (2): 14:0x35b0 (minefields, kind 0): the object is written to a player's file when this word has bit (1 << player) set */
  uint16_t turn;                     /* +0x10 (2): 14:0x0508: = GameHeader.turn (DS:0x82) after copying the block */
};
STATIC_SIZE(struct MapObject, 0x12);
STATIC_OFFSET(struct MapObject, id_kind, 0x0);
STATIC_OFFSET(struct MapObject, id_lo, 0x0);
STATIC_OFFSET(struct MapObject, owner_kind_bits, 0x1);
STATIC_OFFSET(struct MapObject, xy, 0x2);
STATIC_OFFSET(struct MapObject, fld_06, 0x6);
STATIC_OFFSET(struct MapObject, unknown_08, 0x8);
STATIC_OFFSET(struct MapObject, fld_0c, 0xC);
STATIC_OFFSET(struct MapObject, fld_0d, 0xD);
STATIC_OFFSET(struct MapObject, seen_by_mask, 0xE);
STATIC_OFFSET(struct MapObject, turn, 0x10);

/* MessageHeader (4 bytes)
Message record header in the message buffer (far pointer DS:0xAAC, used bytes DS:0xAB0 (< 0xFFC9),
count DS:0xAB4). 6:0x752e builds it: word 0 = message id (bits 0-8) | one bit per argument (bit 9+i set =
argument i is a word, clear = a byte); then word 1, then the arguments. Argument count per message id is in DS:0xE74 + id. */
struct MessageHeader {
  uint16_t id_arg_sizes;             /* +0x00 (2): 6:0x752e: (id & 0x1ff) | argument-size bits << 9 */
  uint16_t subject;                  /* +0x02 (2): 6:0x752e: the subject word (src/seg06.c: word +2 = subject), then the values; always stored as a word */
};
STATIC_SIZE(struct MessageHeader, 0x4);
STATIC_OFFSET(struct MessageHeader, id_arg_sizes, 0x0);
STATIC_OFFSET(struct MessageHeader, subject, 0x2);

/* MessageRecord5 (5 bytes)
Turn-time message record, built by 6:0x7410 (build_message_record), appended by 6:0x7344 / 6:0x739e and
written to the turn file by 6:0x8308 without byte 0. The values (count = signed byte cs:0x0E74 + id, one
byte each, or one word each when its flag bit is set) follow the header; the byte length of the values is
in the high nibble of player_len. */
struct MessageRecord5 {
  uint8_t player_len;                /* +0x00 (1): 6:0x7410: low nibble = player (MSG_PLAYER_MASK), high nibble = length of the values (MSG_LEN_SHIFT) */
  uint16_t id_arg_sizes;             /* +0x01 (2): 6:0x7410: bits 0-8 = message id (MSG_ID_MASK), bit 9 + k set = value k is a word (MSG_WORD_FLAG_SHIFT) */
  uint16_t subject;                  /* +0x03 (2): 6:0x7410: the subject word */
};
STATIC_SIZE(struct MessageRecord5, 0x5);
STATIC_OFFSET(struct MessageRecord5, player_len, 0x0);
STATIC_OFFSET(struct MessageRecord5, id_arg_sizes, 0x1);
STATIC_OFFSET(struct MessageRecord5, subject, 0x3);
enum {
  MSG_ID_MASK = 0x1FF,
  MSG_WORD_FLAG_SHIFT = 9,
  MSG_PLAYER_MASK = 0x0F,
  MSG_LEN_SHIFT = 4
};
/* Argument count of message id n: signed byte at cs:0x0E74 + n of code segment 6 (ids 0..0x180). */
#define MSG_ARG_COUNT_CS6_OFFSET 0x0E74

/* BattleToken (29 bytes)
One ship stack (token) of a battle record (types 31/39, copied verbatim into the heap at DS:0xDBC by
14:0x0508; 14:0x27d4 walks them: record byte +3 = token count, tokens from record +0x0E, stride 0x1D).
30:0x5234 (the repair/damage routine of the battle) reads and updates one. Fleet-or-planet id in word 0
(kind 1 = a planet's starbase: 30:0x5234 looks it up with 14:0x01e4, else the fleet with 14:0x14a8). */
struct BattleToken {
  uint16_t object_id;                /* +0x00 (2): 30:0x5234: passed to the fleet/planet lookup */
  uint8_t owner;                     /* +0x02 (1): 30:0x5234: player; (owner * 16 + design_slot) * 2 indexes the table at DS:0x5506 */
  uint8_t kind;                      /* +0x03 (1): 30:0x5234: 1 = starbase (planet), else fleet; 14:0x27d4 reads it as the token count of the record */
  uint8_t design_slot;               /* +0x04 (1): 30:0x5234: design index within the owner */
  uint8_t unknown_05[12];            /* +0x05 (12): no access found in re/decomp; kept as padding */
  uint16_t fld_11;                   /* +0x11 (2): meaning not proven: only these accesses found: 30:0x5234: nonzero = a pending quantity multiplied by ship_count */
  uint16_t ship_count;               /* +0x13 (2): 30:0x5234: ships of the stack, reduced as they die */
  uint16_t damage;                   /* +0x15 (2): 30:0x5234: same encoding as Fleet.damage (percent in the low 7 bits, damage << 7) */
  uint8_t unknown_17[1];             /* +0x17 (1): no access found in re/decomp; kept as padding */
  uint8_t fld_18;                    /* +0x18 (1): meaning not proven: only these accesses found: 30:0x5234: low nibble == 1 is cleared (moves the state in +0x1B) */
  uint8_t unknown_19[2];             /* +0x19 (2): no access found in re/decomp; kept as padding */
  uint16_t flags;                    /* +0x1B (2): 30:0x5234: bit 0 and bits 8-9 rewritten (mask 0xFCFF | 0xE0); role not proven */
};
STATIC_SIZE(struct BattleToken, 0x1D);
STATIC_OFFSET(struct BattleToken, object_id, 0x0);
STATIC_OFFSET(struct BattleToken, owner, 0x2);
STATIC_OFFSET(struct BattleToken, kind, 0x3);
STATIC_OFFSET(struct BattleToken, design_slot, 0x4);
STATIC_OFFSET(struct BattleToken, unknown_05, 0x5);
STATIC_OFFSET(struct BattleToken, fld_11, 0x11);
STATIC_OFFSET(struct BattleToken, ship_count, 0x13);
STATIC_OFFSET(struct BattleToken, damage, 0x15);
STATIC_OFFSET(struct BattleToken, unknown_17, 0x17);
STATIC_OFFSET(struct BattleToken, fld_18, 0x18);
STATIC_OFFSET(struct BattleToken, unknown_19, 0x19);
STATIC_OFFSET(struct BattleToken, flags, 0x1B);

/* HeapSeg (0x10 bytes): header at offset 0 of each global-memory segment that backs one of the 12
 * game heaps (DS_HEAP_HEADS[heap] is a linked list of them). Recovered from 12:0x0000 (creates it),
 * 12:0x0098 (grows it), 12:0x0232 (allocates), 12:0x04d0 (frees). Allocations inside the segment
 * start at offset 0x10; each has a word before its data: size (even) | 1 when free. */
struct HeapSeg {
  uint16_t free_total;               /* +0x00 (2): 12:0x0000 = seg_size - 0x10; 12:0x0232 subtracts allocations, 12:0x04d0 adds back */
  uint16_t seg_size;                 /* +0x02 (2): 12:0x0000: byte size of the segment (GlobalAlloc size); 12:0x0098 += growth */
  uint16_t tail_free;                /* +0x04 (2): 12:0x0000 = seg_size - 0x10: free bytes after tail_off */
  uint16_t tail_off;                 /* +0x06 (2): 12:0x0000 = 0x10: offset where the never-used tail starts */
  uint32_t next;                     /* +0x08 (4): 12:0x0000: far pointer to the next HeapSeg of this heap */
  uint16_t handle;                   /* +0x0C (2): 12:0x0000: GlobalAlloc handle of the segment */
  uint8_t heap;                      /* +0x0E (1): 12:0x0000: heap number 0..11 */
  uint8_t unknown_0F;                /* +0x0F (1): no access found in re/decomp; kept as padding */
};
STATIC_SIZE(struct HeapSeg, 0x10);
STATIC_OFFSET(struct HeapSeg, free_total, 0x0);
STATIC_OFFSET(struct HeapSeg, seg_size, 0x2);
STATIC_OFFSET(struct HeapSeg, tail_free, 0x4);
STATIC_OFFSET(struct HeapSeg, tail_off, 0x6);
STATIC_OFFSET(struct HeapSeg, next, 0x8);
STATIC_OFFSET(struct HeapSeg, handle, 0xC);
STATIC_OFFSET(struct HeapSeg, heap, 0xE);
STATIC_OFFSET(struct HeapSeg, unknown_0F, 0xF);

#pragma pack(pop)

/* Player.attrs indices: 28:0x21ee(player, 14) is the race value (14:0x1f0c, 14:0x35b0, src/seg02.c). */
enum {
  PLAYER_ATTR_COUNT = 15,
  PLAYER_ATTR_RACE = 14
};

/* Fleet.damage[i] fields (16:0x0962). */
enum {
  FLEET_DAMAGE_PCT_MASK = 0x7F,
  FLEET_DAMAGE_SHIFT = 7
};

/* ScoreRecord.id_flags (14:0x0508, 32:0x158e). */
enum {
  SCORE_ID_MASK = 0x1F,
  SCORE_VALID = 0x20
};

/* MapObject.owner_kind_bits: bits 1-4 hold the owning player, bits 5-7 the kind of object. Kind 0 is
 * the minefield (5:0x04e8 takes a square root of its radius; the segment-5 window is MINEWNDPROC);
 * kind 0x20 is handled as a mineral-packet-like object (14:0x0508 end). */
enum {
  MAPOBJ_OWNER_MASK = 0x1E,
  MAPOBJ_OWNER_SHIFT = 1,
  MAPOBJ_KIND_MASK = 0xE0,
  MAPOBJ_KIND_MINEFIELD = 0x00,
  MAPOBJ_PACKET_WARP_MASK = 0x3C, /* kind 0x20 (packet): bits 2-5 of the byte at +7, shown as ((b & 0x3c) >> 2) + 4: 5:0x04e8 (re/decomp/seg05/04e8_FUN_1020_04e8.c:1140) */
  MAPOBJ_PACKET_WARP_BASE = 4
};
#define MAPOBJ_PACKET_WARP(b7) ((uint16_t)((((b7) & MAPOBJ_PACKET_WARP_MASK) >> 2) + MAPOBJ_PACKET_WARP_BASE))
/* They take the byte, so a caller reads MapObject.owner_kind_bits once, as pass 1 did. */
static inline uint16_t map_object_owner(uint8_t owner_kind_bits) {
  return (uint16_t)((owner_kind_bits & MAPOBJ_OWNER_MASK) >> MAPOBJ_OWNER_SHIFT);
}
static inline uint8_t map_object_kind(uint8_t owner_kind_bits) {
  return (uint8_t)(owner_kind_bits & MAPOBJ_KIND_MASK);
}

/* ---- Shared vocabulary for pass 2 (stars-decomp-1tp.5) ----------------------------------------------
 * Named constants for masks, kinds, sentinels, table sizes and Win16 resource / command ids. Each value
 * is exactly the number pass 1 spells in src/seg*.c; pass 2 replaces the raw number with the name.
 * Every group is pinned by _Static_assert: a changed value fails the build (this is the regression
 * check), and a switch with one case per id fails on a duplicate. Win16 message numbers (WM_COMMAND
 * 0x0111 and the rest) are NOT redefined here: they come from runtime/ctl.h and runtime/user.h. */

/* Sentinels: 0xFFFF means "none" in these 16-bit words. */
enum {
  /* g_current_player (DS:0x14C) is 0xFFFF when no player is loaded (host / no game): src/seg02.c:137
   * (NOPLAYER), src/seg04.c:1128, 1736, 2552; src/seg06.c:1300. */
  NO_PLAYER = 0xFFFF,
  /* g_selected_planet / g_selected_fleet / g_selected_map_object (DS:0x4B84 / 0x4B86 / 0x4B8A) are 0xFFFF
   * when nothing of that kind is selected: src/seg05.c:295, 447, 452, 459 (local NO_SELECTION). */
  SELECTION_NONE = 0xFFFF,
  /* Planet.owner uses 0xFFFF for "unowned": struct Planet.owner comment (14:0x1f0c, 14:0x0508);
   * src/seg16.c:1040 treats an unowned planet apart. Only Planet.owner is evidenced; Fleet.owner is not. */
  OWNER_NONE = 0xFFFF,
  /* Fleet.planet_id is 0xFFFF for a fleet in deep space: struct Fleet.planet_id comment (14:0x235e). */
  FLEET_IN_DEEP_SPACE = 0xFFFF,
  /* 14:0x35b0 picks the host-file name (DSSTR_HST) when the player index is 0xFFFF: DSSTR_HST comment. */
  PLAYER_INDEX_HOST = 0xFFFF,
  /* A message record whose id field is all ones (0x1FF) is deleted: delete_messages 6:0x80fa
   * (src/seg06.c:526-537, ORs 0x1FF into word +1); src/seg06.c:648 and 672 skip such records when gathering. */
  MSG_ID_DELETED = 0x01FF
};

/* Table sizes (counts), each checked against a byte extent recovered for the table. */
enum {
  MAX_PLAYERS = 16,               /* DS_SHIP_DESIGNS, DS_SB_DESIGNS, DS_BATTLE_PLAN_PTRS are FARPTR[16 players]: 14:0x0508 */
  SHIP_DESIGNS_PER_PLAYER = 16,   /* ShipDesign[16] = 0x8D0 bytes: 14:0x0508 (DS_DEFAULT_SHIP_DESIGNS comment) */
  STARBASE_DESIGNS_PER_PLAYER = 10, /* ShipDesign[10] = 0x582 bytes: 14:0x0508 (struct ShipDesign comment) */
  BATTLE_PLANS_PER_PLAYER = 16,   /* BattlePlan[16] = 0x240 bytes: 14:0x0508, 30:0x049a (struct BattlePlan comment) */
  FLEET_DESIGN_SLOTS = 16,        /* Fleet.ship_count[16] and Fleet.damage[16], one per design slot (struct Fleet) */
  GAME_HEAP_COUNT = 12,           /* DS_HEAP_HEADS is FARPTR[12], DS_HEAP_MIN uint16_t[12]: 12:0x0000, 0x0098 */
  HEAPSEG_FIRST_ALLOC_OFFSET = 0x10, /* allocations in a HeapSeg start at offset 0x10 (sizeof(struct HeapSeg)): 12:0x0000 */
  UNIVERSE_SIZE_UNIT = 400        /* universe width = (GameHeader.universe_size + 1) * 400: 14:0x0508 (struct GameHeader) */
};
_Static_assert(SHIP_DESIGNS_PER_PLAYER * sizeof(struct ShipDesign) == 0x8D0, "ShipDesign[16] extent");
_Static_assert(STARBASE_DESIGNS_PER_PLAYER * sizeof(struct ShipDesign) == 0x582, "ShipDesign[10] extent");
_Static_assert(BATTLE_PLANS_PER_PLAYER * sizeof(struct BattlePlan) == 0x240, "BattlePlan[16] extent");
_Static_assert(HEAPSEG_FIRST_ALLOC_OFFSET == sizeof(struct HeapSeg), "HeapSeg header size");
_Static_assert(sizeof(((struct Fleet *)0)->ship_count) / sizeof(uint16_t) == FLEET_DESIGN_SLOTS, "Fleet.ship_count slots");
_Static_assert(sizeof(((struct Fleet *)0)->damage) / sizeof(uint16_t) == FLEET_DESIGN_SLOTS, "Fleet.damage slots");

/* BlockHeader word fields (see BLOCK_SIZE / BLOCK_TYPE above): size = low 10 bits, type = high 6.
 * 14:0x305a reads, 14:0x579a builds it. */
enum {
  BLOCK_SIZE_MASK = 0x03FF,
  BLOCK_TYPE_SHIFT = 10
};
_Static_assert(BLOCK_SIZE(0xFFFF) == BLOCK_SIZE_MASK && BLOCK_TYPE(0xFC00) == 0x3F, "BlockHeader field split");

/* Bit fields of records. Each comment names the record field and the function that proves it; a name
 * is given only where a function was read that tests or builds exactly that mask. */
enum {
  /* Planet.id: low 11 bits of the block word (14:0x1f0c, struct Planet.id comment). */
  PLANET_ID_MASK = 0x07FF,
  /* Planet.flags: bit 1 = has a starbase, bit 3 = updated this turn (14:0x1f0c, 14:0x0508). */
  PLANET_FLAG_HAS_STARBASE = 0x02,
  PLANET_FLAG_UPDATED = 0x08,
  /* Planet.starbase: low nibble of the low byte = starbase design index (14:0x0508). */
  PLANET_STARBASE_DESIGN_MASK = 0x0F,
  /* Planet.route_target: (word & 0x3FF) = id + 1 of the planet it routes to, 0 = none: 16:0x13f8,
   * src/seg16.c:1267 and 1313 (pass 1 reads word +0x30 & 0x3ff). */
  PLANET_ROUTE_TARGET_MASK = 0x03FF,
  /* Player.fleet_and_sb_counts: low 12 bits = fleet count, high 4 bits = starbase design count (14:0x0508). */
  PLAYER_FLEET_COUNT_MASK = 0x0FFF,
  PLAYER_SB_COUNT_SHIFT = 12,
  /* Player.record_flags: (flags & 7) == 7 selects the long (0x70-byte) record (14:0x0370). */
  PLAYER_RECORD_LONG_MASK = 0x07,
  /* FileHeader.player_salt: low 5 bits = player index (14:0x2caa). */
  FILEHDR_PLAYER_MASK = 0x1F,
  /* Fleet.flags: bit 2 = empty, bit 3 = byte-sized counts (14:0x235e). */
  FLEET_FLAG_EMPTY = 0x04,
  FLEET_FLAG_BYTE_COUNTS = 0x08,
  /* ShipDesign.flags: bit 1 = slot is free (14:0x0508 sets it on every slot at load, 14:0x0000 tests it). */
  DESIGN_FLAG_FREE = 0x02,
  /* BattlePlan.flags: bit 6 = default name, name not written (14:0x50d6). */
  BATTLE_PLAN_FLAG_DEFAULT_NAME = 0x40,
  /* ScoreRecord.ships[i]: low 13 bits mantissa, bits 13-15 times 2 = left shift (32:0x158e). Weak
   * evidence: src/seg33.c:161 and 1024 also use & 0x1fff, but on other words (an id_kind xor, a local), so
   * they do not prove this field; the proof is the 32:0x158e decoding in the struct ScoreRecord comment. */
  SCORE_SHIPS_MANTISSA_MASK = 0x1FFF,
  SCORE_SHIPS_EXP_SHIFT = 13,
  /* MapObject kind (owner_kind_bits & MAPOBJ_KIND_MASK) 0x20 = mineral-packet-like object (14:0x0508 end;
   * MAPOBJ_PACKET_WARP above is read from it in 5:0x04e8). */
  MAPOBJ_KIND_PACKET = 0x20
};
_Static_assert((MAPOBJ_KIND_PACKET & MAPOBJ_KIND_MASK) == MAPOBJ_KIND_PACKET, "packet kind inside the kind mask");
_Static_assert(((0xFFFF >> PLAYER_SB_COUNT_SHIFT) & PLAYER_FLEET_COUNT_MASK) == 0x000F &&
               (PLAYER_FLEET_COUNT_MASK | (0xF << PLAYER_SB_COUNT_SHIFT)) == 0xFFFF,
               "Player.fleet_and_sb_counts split covers the word");
_Static_assert((SCORE_SHIPS_MANTISSA_MASK | (7 << SCORE_SHIPS_EXP_SHIFT)) == 0xFFFF, "ScoreRecord.ships split covers the word");

/* Pin every constant above to the number pass 1 spells (the regression check: change a value and the build fails). */
_Static_assert(NO_PLAYER == 0xFFFF && SELECTION_NONE == 0xFFFF && OWNER_NONE == 0xFFFF && FLEET_IN_DEEP_SPACE == 0xFFFF &&
               PLAYER_INDEX_HOST == 0xFFFF && MSG_ID_DELETED == 0x1FF && MSG_ID_DELETED == MSG_ID_MASK, "sentinels");
_Static_assert(MAX_PLAYERS == 16 && SHIP_DESIGNS_PER_PLAYER == 16 && STARBASE_DESIGNS_PER_PLAYER == 10 &&
               BATTLE_PLANS_PER_PLAYER == 16 && FLEET_DESIGN_SLOTS == 16 && GAME_HEAP_COUNT == 12 &&
               HEAPSEG_FIRST_ALLOC_OFFSET == 0x10 && UNIVERSE_SIZE_UNIT == 400, "table sizes");
_Static_assert(BLOCK_SIZE_MASK == 0x3FF && BLOCK_TYPE_SHIFT == 10, "BlockHeader fields");
_Static_assert(PLANET_ID_MASK == 0x7FF && PLANET_FLAG_HAS_STARBASE == 2 && PLANET_FLAG_UPDATED == 8 &&
               PLANET_STARBASE_DESIGN_MASK == 0xF && PLANET_ROUTE_TARGET_MASK == 0x3FF && PLAYER_FLEET_COUNT_MASK == 0xFFF &&
               PLAYER_SB_COUNT_SHIFT == 12 && PLAYER_RECORD_LONG_MASK == 7 && FILEHDR_PLAYER_MASK == 0x1F &&
               FLEET_FLAG_EMPTY == 4 && FLEET_FLAG_BYTE_COUNTS == 8 && DESIGN_FLAG_FREE == 2 &&
               BATTLE_PLAN_FLAG_DEFAULT_NAME == 0x40 && SCORE_SHIPS_MANTISSA_MASK == 0x1FFF && SCORE_SHIPS_EXP_SHIFT == 13 &&
               MAPOBJ_KIND_PACKET == 0x20, "bit fields");

/* Window command ids (WM_COMMAND wParam) of the main window. The menu ids come from the STARSMENU
 * resource (build/nedump --dump-resources, docs/TOOLING.md section 5; the decimal ids are the ones the
 * dump prints). 4:0x1f40 frame_on_command (src/seg04.c:2415-2535, the comment above it lists them) dispatches on them. */
enum {
  IDM_FILE_NEW = 0x0ED8,          /* "&New..." */
  IDM_FILE_RACE_WIZARD = 0x0081,  /* "Custom &Race Wizard..." */
  IDM_FILE_OPEN = 0x0ED9,         /* "&Open..." (src/seg04.c:2427 opens a game file, 4:0x3b66) */
  IDM_FILE_CLOSE = 0x0071,        /* "&Close" */
  IDM_FILE_SAVE = 0x0EDA,         /* "&Save" */
  IDM_FILE_SAVE_SUBMIT = 0x0EDB,  /* "Save &And Submit" */
  IDM_FILE_PRINT_MAP = 0x00D5,    /* "&Print Map" (src/seg04.c:2432 the print dialog) */
  IDM_FILE_EXIT = 0x0EE2,         /* "E&xit" */
  IDM_VIEW_TOOLBAR = 0x00B3,      /* "&Toolbar" (src/seg04.c:2428 the toolbar toggle) */
  IDM_VIEW_FIND = 0x1068,         /* "&Find..." */
  IDM_VIEW_ZOOM_25 = 0x0F3D,      /* "25%" ... "400%": nine consecutive ids 0xF3D..0xF45 (src/seg04.c:2431 lists 0xf3d to 0xf45) */
  IDM_VIEW_ZOOM_38 = 0x0F3E,
  IDM_VIEW_ZOOM_50 = 0x0F3F,
  IDM_VIEW_ZOOM_75 = 0x0F40,
  IDM_VIEW_ZOOM_100 = 0x0F41,
  IDM_VIEW_ZOOM_125 = 0x0F42,
  IDM_VIEW_ZOOM_150 = 0x0F43,
  IDM_VIEW_ZOOM_200 = 0x0F44,
  IDM_VIEW_ZOOM_400 = 0x0F45,
  IDM_VIEW_LAYOUT_LARGE = 0x0082, /* "&Large Screen" (the layout commands 0x82 to 0x84, src/seg04.c:2428) */
  IDM_VIEW_LAYOUT_MEDIUM = 0x0083,
  IDM_VIEW_LAYOUT_SMALL = 0x0084,
  IDM_VIEW_PLAYER_COLORS = 0x098D, /* "Player &Colors" */
  IDM_VIEW_RACE = 0x009C,         /* "&Race..." */
  IDM_VIEW_GAME_PARAMS = 0x009E,  /* "&Game Parameters..." */
  IDM_TURN_WAIT = 0x006A,         /* "&Wait for New" (src/seg04.c:2426: the host mode, 4:0x33df) */
  IDM_TURN_GENERATE = 0x0069,     /* "&Generate" (src/seg04.c:2425: starts the game / turn files) */
  IDM_CMD_SHIP_DESIGN = 0x007D,   /* "&Ship Design..." */
  IDM_CMD_RESEARCH = 0x007E,      /* "&Research..." */
  IDM_CMD_BATTLE_PLANS = 0x07DC,  /* "&Battle Plans..." */
  IDM_CMD_RELATIONS = 0x07DE,     /* "&Player Relations..." */
  IDM_CMD_CHANGE_PASSWORD = 0x010E, /* "&Change Password..." (src/seg04.c:2430, branch L2d6c) */
  IDM_REPORT_PLANETS = 0x08FD,    /* "&Planets..." */
  IDM_REPORT_FLEETS = 0x08FF,     /* "&Fleets..." */
  IDM_REPORT_ENEMY_FLEETS = 0x0900, /* "&Enemy Fleets..." */
  IDM_REPORT_BATTLES = 0x0901,    /* "&Battles..." */
  IDM_REPORT_SCORE = 0x005F,      /* "&Score..." (src/seg04.c:2423 lists ids 0x5f, 0x60 together with dialog 0x66, IDD_SCORE) */
  IDM_DUMP_UNIVERSE = 0x0055,     /* "&Universe Definition" (src/seg04.c:2422: ids 0x53 to 0x55) */
  IDM_DUMP_PLANET = 0x0054,       /* "&Planet Information" */
  IDM_DUMP_FLEET = 0x0053,        /* "&Fleet Information" */
  IDM_HELP_INTRO = 0x09C2,        /* "&Introduction" */
  IDM_HELP_PLAYERS_GUIDE = 0x0101, /* "&Player's Guide" (src/seg04.c:2428 WinHelp) */
  IDM_HELP_TECH_BROWSER = 0x0100, /* "Technology &Browser" (src/seg04.c:2428) */
  IDM_HELP_TUTORIAL = 0x09C5,     /* "&Tutorial" */
  IDM_HELP_ABOUT = 0x0063,        /* "&About Stars!..." (src/seg04.c:2423: dialog IDD_ABOUT, 3:0x0ca0) */
  /* Commands with no menu item (accelerators, buttons, internal posts), named from src/seg04.c:2415-2434. */
  CMD_GAME_LOADED = 0x0FA1,       /* posted/sent to the main window DS:0x2806 after a game is loaded: 4:0x3b66 (src/seg04.c:1215,
                                   * 1334, 1927, 2131), 15:0x38E7; the handler L3030 (src/seg04.c:3010) sets bit 0 of DS:0x794,
                                   * runs 4:0x197a and 4:0x5d98, then invalidates and updates window DS:0x150 */
  IDM_TURNS_10 = 0x5208,          /* count down 10 / 100 / 1000 turns in DS:0x378 (src/seg04.c:2425) */
  IDM_TURNS_100 = 0x5209,
  IDM_TURNS_1000 = 0x520A,
  IDM_RECENT_FILE_FIRST = 0x3A98, /* 0x3A98..0x3AFB: DS:0x4C84 = id - 0x3A98, a recent-file slot (src/seg04.c:2421, 2441) */
  IDM_RECENT_FILE_LAST = 0x3AFB
};
/* One case per id: a duplicate value is a compile error ("duplicate case value"), the unique_values
 * check. The ids are also pinned to the decimal numbers that nedump prints for STARSMENU. */
static inline uint16_t stars_menu_ids_distinct(uint16_t id) {
  switch (id) {
    case IDM_FILE_NEW: case IDM_FILE_RACE_WIZARD: case IDM_FILE_OPEN: case IDM_FILE_CLOSE:
    case IDM_FILE_SAVE: case IDM_FILE_SAVE_SUBMIT: case IDM_FILE_PRINT_MAP: case IDM_FILE_EXIT:
    case IDM_VIEW_TOOLBAR: case IDM_VIEW_FIND:
    case IDM_VIEW_ZOOM_25: case IDM_VIEW_ZOOM_38: case IDM_VIEW_ZOOM_50: case IDM_VIEW_ZOOM_75:
    case IDM_VIEW_ZOOM_100: case IDM_VIEW_ZOOM_125: case IDM_VIEW_ZOOM_150: case IDM_VIEW_ZOOM_200:
    case IDM_VIEW_ZOOM_400:
    case IDM_VIEW_LAYOUT_LARGE: case IDM_VIEW_LAYOUT_MEDIUM: case IDM_VIEW_LAYOUT_SMALL:
    case IDM_VIEW_PLAYER_COLORS: case IDM_VIEW_RACE: case IDM_VIEW_GAME_PARAMS:
    case IDM_TURN_WAIT: case IDM_TURN_GENERATE:
    case IDM_CMD_SHIP_DESIGN: case IDM_CMD_RESEARCH: case IDM_CMD_BATTLE_PLANS: case IDM_CMD_RELATIONS:
    case IDM_CMD_CHANGE_PASSWORD:
    case IDM_REPORT_PLANETS: case IDM_REPORT_FLEETS: case IDM_REPORT_ENEMY_FLEETS: case IDM_REPORT_BATTLES:
    case IDM_REPORT_SCORE: case IDM_DUMP_UNIVERSE: case IDM_DUMP_PLANET: case IDM_DUMP_FLEET:
    case IDM_HELP_INTRO: case IDM_HELP_PLAYERS_GUIDE: case IDM_HELP_TECH_BROWSER: case IDM_HELP_TUTORIAL:
    case IDM_HELP_ABOUT: case CMD_GAME_LOADED: case IDM_TURNS_10: case IDM_TURNS_100: case IDM_TURNS_1000:
    case IDM_RECENT_FILE_FIRST:
      return 1;
    default:
      return 0;
  }
}
_Static_assert(IDM_FILE_NEW == 3800 && IDM_FILE_RACE_WIZARD == 129 && IDM_FILE_OPEN == 3801 && IDM_FILE_CLOSE == 113 &&
               IDM_FILE_SAVE == 3802 && IDM_FILE_SAVE_SUBMIT == 3803 && IDM_FILE_PRINT_MAP == 213 && IDM_FILE_EXIT == 3810,
               "STARSMENU File ids");
_Static_assert(IDM_VIEW_TOOLBAR == 179 && IDM_VIEW_FIND == 4200 && IDM_VIEW_ZOOM_25 == 3901 && IDM_VIEW_ZOOM_38 == 3902 &&
               IDM_VIEW_ZOOM_50 == 3903 && IDM_VIEW_ZOOM_75 == 3904 && IDM_VIEW_ZOOM_100 == 3905 && IDM_VIEW_ZOOM_125 == 3906 &&
               IDM_VIEW_ZOOM_150 == 3907 && IDM_VIEW_ZOOM_200 == 3908 && IDM_VIEW_ZOOM_400 == 3909 &&
               IDM_VIEW_LAYOUT_LARGE == 130 && IDM_VIEW_LAYOUT_MEDIUM == 131 && IDM_VIEW_LAYOUT_SMALL == 132 &&
               IDM_VIEW_PLAYER_COLORS == 2445 && IDM_VIEW_RACE == 156 && IDM_VIEW_GAME_PARAMS == 158,
               "STARSMENU View ids");
_Static_assert(IDM_TURN_WAIT == 106 && IDM_TURN_GENERATE == 105 && IDM_CMD_SHIP_DESIGN == 125 && IDM_CMD_RESEARCH == 126 &&
               IDM_CMD_BATTLE_PLANS == 2012 && IDM_CMD_RELATIONS == 2014 && IDM_CMD_CHANGE_PASSWORD == 270,
               "STARSMENU Turn and Commands ids");
_Static_assert(IDM_REPORT_PLANETS == 2301 && IDM_REPORT_FLEETS == 2303 && IDM_REPORT_ENEMY_FLEETS == 2304 &&
               IDM_REPORT_BATTLES == 2305 && IDM_REPORT_SCORE == 95 && IDM_DUMP_UNIVERSE == 85 && IDM_DUMP_PLANET == 84 &&
               IDM_DUMP_FLEET == 83, "STARSMENU Report ids");
_Static_assert(IDM_HELP_INTRO == 2498 && IDM_HELP_PLAYERS_GUIDE == 257 && IDM_HELP_TECH_BROWSER == 256 &&
               IDM_HELP_TUTORIAL == 2501 && IDM_HELP_ABOUT == 99, "STARSMENU Help ids");
_Static_assert(CMD_GAME_LOADED == 4001 && IDM_TURNS_10 == 21000 && IDM_TURNS_1000 == 21002 &&
               IDM_TURNS_100 == 21001 && IDM_RECENT_FILE_FIRST == 15000 && IDM_RECENT_FILE_LAST == 15099, "non-menu command ids");

/* Dialog template ids (the resource id passed to DialogBox / CreateDialog); the caption is what the
 * dump (build/nedump --dump-resources) prints for that template. */
enum {
  IDD_ABOUT = 0x005A,             /* dialog 90 (About, 3:0x0ca0): src/seg04.c:2423 */
  IDD_SERIAL = 0x0056,            /* dialog 86 "Register Serial Number": src/seg04.c:2157, src/seg06.c:2340 */
  IDD_ORDERING = 0x0061,          /* dialog 97 "Ordering Information": src/seg03.c:623 */
  IDD_SCORE = 0x0066,             /* dialog 102, caption "Victory Conditions" (32:0x0a66); src/seg04.c:2423 only pairs it with commands 0x5f/0x60,
                                   * so the name SCORE is a guess from the Score menu item, not proven */
  IDD_HOST_MODE = 0x0073,         /* dialog 115 "Stars! Host Mode": src/seg04.c:1058 */
  IDD_RESEARCH = 0x007F,          /* dialog 127 "Research": src/seg04.c:2753 */
  IDD_TECH_BROWSER = 0x0080,      /* dialog 128 "Technology Browser" (modeless, CreateDialog): src/seg04.c:2778 */
  IDD_CHANGE_PASSWORD = 0x008D,   /* dialog 141 "Change Password": src/seg04.c:3793 */
  IDD_PRINT_MAP = 0x00D6,         /* dialog 214 "Print Map": src/seg04.c:3082 */
  IDD_AUTO_GENERATE = 0x0402,     /* dialog 1026 "Auto Generate Options": src/seg04.c:3804 */
  IDD_RENAME_BATTLE_PLAN = 0x07E3 /* dialog 2019 "Rename Battle Plan": src/seg16.c:228 */
};
static inline uint16_t stars_dialog_ids_distinct(uint16_t id) {
  switch (id) {
    case IDD_ABOUT: case IDD_SERIAL: case IDD_ORDERING: case IDD_SCORE: case IDD_HOST_MODE: case IDD_RESEARCH:
    case IDD_TECH_BROWSER: case IDD_CHANGE_PASSWORD: case IDD_PRINT_MAP: case IDD_AUTO_GENERATE:
    case IDD_RENAME_BATTLE_PLAN:
      return 1;
    default:
      return 0;
  }
}
_Static_assert(IDD_ABOUT == 90 && IDD_SERIAL == 86 && IDD_ORDERING == 97 && IDD_SCORE == 102 && IDD_HOST_MODE == 115 &&
               IDD_RESEARCH == 127 && IDD_TECH_BROWSER == 128 && IDD_CHANGE_PASSWORD == 141 && IDD_PRINT_MAP == 214 &&
               IDD_AUTO_GENERATE == 1026 && IDD_RENAME_BATTLE_PLAN == 2019, "dialog resource ids");

/* Leaf functions that other segments call by their C name (docs/CONVENTIONS.md rule e: a callee that
 * mirrors no frame is a plain C call). */
uint16_t toolbar_item_is_on(uint16_t item); /* 13:0x0902 (fn_13_0902): is toolbar item `item` on (src/seg13.c) */
uint16_t fleet_has_colony_hull(uint32_t fleet); /* 16:0x16f6 (fn_16_16f6): does the fleet hold a ship of hull 14 or 15 (the colony ships) (src/seg16.c) */
uint16_t fleet_has_scout_frigate_destroyer(uint32_t fleet); /* 16:0x175c (fn_16_175c): does the fleet hold a ship of hull 4, 5 or 6 (scout, frigate, destroyer) (src/seg16.c) */

#endif /* STARS_SRC_STARS_H */
