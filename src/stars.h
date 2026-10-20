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

/* Native builds log by-name Win16 API calls to the harness API log (harness/byname.h, stars-4a6);
 * the wasm build calls the runtime directly. */
#ifndef __EMSCRIPTEN__
#include "harness/byname.h"
#endif

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
  DS_MESSAGE_BUF = 0x0AAC,     /* far pointer to message bytes; used length DS:0xAB0, count DS:0xAB4: 6:0x752e */
  DS_MAP_OBJECTS = 0x188E,     /* far pointer to MapObject[DS:0x1892], capacity DS:0x1894: 14:0x0508 */
  DS_ZIP_PRESETS = 0x2576,     /* ZipPreset[4]: 26:0x2d26 */
  DS_PLANET_XY = 0x31BA,       /* {u16 x, u16 y}[GameHeader.planet_count]: 14:0x0508 */
  DS_PLAYER_BATTLE_PLANS_COUNT = 0x4A4A, /* uint8_t[16]: 14:0x0508, 30:0x049a */
  DS_PLANET_COUNT = 0x4D96,    /* 14:0x0508 */
  DS_FLEET_COUNT = 0x553A,     /* 14:0x0508 */
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
  BLK_PLANET_13 = 13,   /* 14:0x4926 writes one of 13/14 per planet */
  BLK_PLANET = 14,      /* 14:0x0508 (header byte 0x38), 14:0x4926: Planet */
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
  BLK_45 = 45           /* 14:0x35b0 writes 0x18-byte records from DS:0x177A; not decoded here */
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

/* GameHeader (64 bytes)
The type-7 block (BLK_GAME_HEADER): 0x40 bytes, resident at DGROUP 0x0070 (14:0x0508 copies 16
dwords from DS:0x4db2 to DS:0x70; 15:0x1334 writes it back with block(7, 0x40, DS:0x70)). */
struct GameHeader {
  uint32_t game_id;                  /* +0x00 (4): 14:0x2caa compares it with FileHeader.game_id; 14:0x2992 clears it */
  uint16_t universe_size;            /* +0x04 (2): 14:0x0508: (this+1)*400 -> DS:0xB0; 15:0x4efe, 15:0x73a8 */
  uint16_t fld_06;                   /* +0x06 (2): 15:0x1334, 15:0x6ab2 (new game setup); role not proven */
  uint16_t player_count;             /* +0x08 (2): 14:0x0508: loop bound over the 0xC0-byte player table */
  uint16_t planet_count;             /* +0x0A (2): 14:0x0508: loop bound filling the planet x/y tables (DS:0x31BA) */
  uint16_t fld_0c;                   /* +0x0C (2): 15:0x1334 (multiplied into a count), 15:0x6ab2 writes 1 or 2; role not proven */
  uint16_t fld_0e;                   /* +0x0E (2): 14:0x0508 and 14:0x2992 clear it; 9:0x641c */
  uint8_t flags_80;                  /* +0x10 (1): 14:0x0508/14:0x35b0/14:0x2caa: bit tests (0x04, 0x08); 14:0x2caa rewrites bits 1-3 */
  uint8_t flags_81;                  /* +0x11 (1): 14:0x2caa, 15:0x1334 */
  uint16_t turn;                     /* +0x12 (2): 14:0x0508: loaded from the file header; 14:0x1f0c/14:0x235e stamp records with it; 14:0x2992 clears */
  uint8_t fld_84[9];                 /* +0x14 (9): 15:0x6ab2 writes DS:0x84..0x8C one byte at a time; 15:0x47f0 writes 0x8B/0x8C; role not proven */
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
  uint8_t unknown_08[4];             /* +0x08 (4): no access found in re/decomp; kept as padding */
  uint32_t fld_0c;                   /* +0x0C (4): 14:0x0508: DS:0x31C = this (of the current player) */
  uint8_t unknown_10[40];            /* +0x10 (40): no access found in re/decomp; kept as padding */
  int8_t fld_38;                     /* +0x38 (1): 26:0x29da: read as a signed percentage byte at DS:0x5A9C + i*0xC0 */
  uint8_t unknown_39[5];             /* +0x39 (5): no access found in re/decomp; kept as padding */
  int8_t attrs[15];                  /* +0x3E (15): 28:0x21ee reads attrs[i]; 28:0x2200 writes it clamped to DS:0x300/0x310 tables; index 14 used at 14:0x1f0c and 14:0x35b0 (race wizard values) */
  uint8_t unknown_4D[1];             /* +0x4D (1): no access found in re/decomp; kept as padding */
  uint32_t trait_bits;               /* +0x4E (4): 28:0x2230 tests bit n in the words at +0x4E/+0x50; 28:0x2254 sets/clears (race wizard) */
  uint8_t unknown_52[2];             /* +0x52 (2): no access found in re/decomp; kept as padding */
  uint16_t fld_54;                   /* +0x54 (2): 14:0x0370 masks it with 7; 14:0x0508 tests bit 0 of its low byte (DS:0x5AB8); 9:0x78a0 */
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
STATIC_OFFSET(struct Player, fld_0c, 0xC);
STATIC_OFFSET(struct Player, unknown_10, 0x10);
STATIC_OFFSET(struct Player, fld_38, 0x38);
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

/* Planet (56 bytes)
Planet record: far array DS:0xB6/0xB8, stride 0x38 (14:0x0508 allocates count*0x38; 14:0x1f0c reads
block types 13/14 into it, 14:0x4926 writes them). Count in DS:0x4D96. */
struct Planet {
  uint16_t id;                       /* +0x00 (2): 14:0x1f0c: low 11 bits of the block word; 14:0x0508 sorts/merges by it */
  uint16_t owner;                    /* +0x02 (2): 14:0x1f0c, 14:0x0508: 0xFFFF = unowned; indexes the player table */
  uint8_t info_level;                /* +0x04 (1): 14:0x1f0c compares it with the block detail field (4db4 & 0x7f) */
  uint8_t flags;                     /* +0x05 (1): 14:0x1f0c, 14:0x0508: bit 1 = has a starbase, bit 3 = updated this turn */
  uint8_t fld_06[3];                 /* +0x06 (3): 14:0x1f0c reads 3 optional bytes into +6..+8; 14:0x4926 writes them */
  uint8_t fld_09[3];                 /* +0x09 (3): 14:0x1f0c: 3 bytes; 14:0x4926 */
  uint8_t fld_0c[3];                 /* +0x0C (3): 14:0x1f0c: 3 bytes (<= 100); 14:0x4926 */
  uint8_t fld_0f[3];                 /* +0x0F (3): 14:0x1f0c: 3 bytes (<= 100), copy of +0xC unless the block carries it; 14:0x4926 */
  uint16_t fld_12;                   /* +0x12 (2): 14:0x1f0c: word read when the block has it; 14:0x4926 */
  uint8_t fld_14[8];                 /* +0x14 (8): 14:0x1f0c: 8 bytes copied from the block via 34:0x0c98; 14:0x4926 writes words +0x14..+0x1B */
  uint32_t fld_1c[4];                /* +0x1C (16): 14:0x1f0c: four 0/1/2/4-byte values widened to dwords; 14:0x4926 */
  uint32_t starbase;                 /* +0x2C (4): 14:0x1f0c, 14:0x4926: present when flags bit 1; low nibble of the low byte = starbase design index (14:0x0508) */
  uint16_t fld_30;                   /* +0x30 (2): 14:0x1f0c, 14:0x4926 (written when (this & 0x3ff) != 0) */
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
STATIC_OFFSET(struct Planet, fld_0c, 0xC);
STATIC_OFFSET(struct Planet, fld_0f, 0xF);
STATIC_OFFSET(struct Planet, fld_12, 0x12);
STATIC_OFFSET(struct Planet, fld_14, 0x14);
STATIC_OFFSET(struct Planet, fld_1c, 0x1C);
STATIC_OFFSET(struct Planet, starbase, 0x2C);
STATIC_OFFSET(struct Planet, fld_30, 0x30);
STATIC_OFFSET(struct Planet, turn, 0x32);
STATIC_OFFSET(struct Planet, queue, 0x34);

/* Waypoint (18 bytes)
Waypoint, 0x12 bytes (14:0x235e reads type 19/20 blocks into them; 14:0x35b0 appends one by copying
the target's x,y dword). A fleet's list is a WaypointListHdr followed by waypoint_count + 1 of these
(14:0x235e allocates (waypoint_count+1)*0x12 after a 4-byte header and zeroes it). */
struct Waypoint {
  uint16_t x;                        /* +0x00 (2): 14:0x35b0: dword copied from Fleet/Planet +8 (x,y) */
  uint16_t y;                        /* +0x02 (2): 14:0x35b0: same dword copy */
  uint16_t target_id;                /* +0x04 (2): 14:0x35b0: = target Fleet word 0 */
  uint16_t flags;                    /* +0x06 (2): 14:0x35b0: nibble masks 0xF, 0xF0 and 0x0F00 are rewritten on this word; 14:0x235e clears bit 5 of the high byte */
  uint16_t fld_08;                   /* +0x08 (2): 14:0x35b0: copied together with +0xA as one dword */
  uint16_t fld_0a;                   /* +0x0A (2): 14:0x35b0: (this + 1) * 0x32 used as a distance */
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
  uint16_t x;                        /* +0x08 (2): 14:0x35b0: distance computation; 14:0x235e compares it with DS:0x31BA */
  uint16_t y;                        /* +0x0A (2): 14:0x35b0: distance computation; 14:0x235e */
  uint16_t ship_count[16];           /* +0x0C (32): 14:0x235e/14:0x4c86: one word per design slot (mask-compressed on disk) */
  uint16_t fld_2c[16];               /* +0x2C (32): 14:0x235e (version 7: mask-compressed words), 14:0x4c86; version < 7 reads a dword here */
  uint32_t cargo[5];                 /* +0x4C (20): 14:0x235e/14:0x4c86: five 0/1/2/4-byte values (role inferred, not proven) */
  uint8_t battle_plan;               /* +0x60 (1): 30:0x0fe8, 14:0x4c86: index into the owner battle-plan table */
  uint8_t unknown_61[1];             /* +0x61 (1): no access found in re/decomp; kept as padding */
  uint16_t waypoint_count;           /* +0x62 (2): 14:0x235e, 14:0x4c86 (byte at +0x62) */
  FARPTR waypoints;                  /* +0x64 (4): 14:0x235e: far pointer (offset +0x64, selector +0x66) to a WaypointListHdr */
  uint8_t unknown_68[12];            /* +0x68 (12): no access found in re/decomp; kept as padding */
  uint32_t fld_74;                   /* +0x74 (4): 14:0x235e/14:0x4c86 (old version-< 7 layout) */
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
STATIC_OFFSET(struct Fleet, ship_count, 0xC);
STATIC_OFFSET(struct Fleet, fld_2c, 0x2C);
STATIC_OFFSET(struct Fleet, cargo, 0x4C);
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

/* ShipDesign (141 bytes)
Ship or starbase design, 0x8D bytes: per player a far pointer table DS:0xBE (16 ship designs =
0x8D0 bytes) and DS:0x10C (10 starbase designs = 0x582 bytes) (14:0x0508). 14:0x0000 reads a type-26
block into one, 14:0x342a writes it. */
struct ShipDesign {
  uint16_t hull;                     /* +0x00 (2): 14:0x0000: compared as a word with block byte 2; 14:0x342a writes the low byte */
  uint8_t unknown_02[6];             /* +0x02 (6): no access found in re/decomp; kept as padding */
  uint8_t name[32];                  /* +0x08 (32): 14:0x0000/14:0x342a: string copied to/from +8 (<= 0x20 bytes) */
  uint16_t fld_28;                   /* +0x28 (2): 14:0x0000 (old layout), 14:0x342a */
  uint8_t unknown_2A[8];             /* +0x2A (8): no access found in re/decomp; kept as padding */
  uint16_t fld_32;                   /* +0x32 (2): 14:0x0000: compared with block byte 3; 14:0x342a */
  uint8_t unknown_34[4];             /* +0x34 (4): no access found in re/decomp; kept as padding */
  uint16_t fld_38;                   /* +0x38 (2): 14:0x0000 (version 7), 14:0x342a */
  struct DesignSlot slots[16];       /* +0x3A (64): 14:0x0000: block bytes copied here ((count << 2) bytes); 14:0x342a */
  uint8_t slot_count;                /* +0x7A (1): 14:0x0000, 14:0x342a */
  uint8_t version;                   /* +0x7B (1): 14:0x0000: (this < 7) selects the old layout; 14:0x342a */
  uint8_t flags;                     /* +0x7C (1): 14:0x0508 sets bit 1 on every slot at load (free); 14:0x0000 tests bits 1 and 7 */
  uint16_t fld_7d;                   /* +0x7D (2): 14:0x0000 and 14:0x342a (version 7 only) */
  uint32_t fld_7f;                   /* +0x7F (4): 14:0x0000 and 14:0x342a (version 7 only) */
  uint32_t fld_83;                   /* +0x83 (4): 14:0x0000 and 14:0x342a (version 7 only) */
  uint8_t unknown_87[4];             /* +0x87 (4): no access found in re/decomp; kept as padding */
  uint16_t fld_8b;                   /* +0x8B (2): 14:0x0508 zeroes it for every design slot */
};
STATIC_SIZE(struct ShipDesign, 0x8D);
STATIC_OFFSET(struct ShipDesign, hull, 0x0);
STATIC_OFFSET(struct ShipDesign, unknown_02, 0x2);
STATIC_OFFSET(struct ShipDesign, name, 0x8);
STATIC_OFFSET(struct ShipDesign, fld_28, 0x28);
STATIC_OFFSET(struct ShipDesign, unknown_2A, 0x2A);
STATIC_OFFSET(struct ShipDesign, fld_32, 0x32);
STATIC_OFFSET(struct ShipDesign, unknown_34, 0x34);
STATIC_OFFSET(struct ShipDesign, fld_38, 0x38);
STATIC_OFFSET(struct ShipDesign, slots, 0x3A);
STATIC_OFFSET(struct ShipDesign, slot_count, 0x7A);
STATIC_OFFSET(struct ShipDesign, version, 0x7B);
STATIC_OFFSET(struct ShipDesign, flags, 0x7C);
STATIC_OFFSET(struct ShipDesign, fld_7d, 0x7D);
STATIC_OFFSET(struct ShipDesign, fld_7f, 0x7F);
STATIC_OFFSET(struct ShipDesign, fld_83, 0x83);
STATIC_OFFSET(struct ShipDesign, unknown_87, 0x87);
STATIC_OFFSET(struct ShipDesign, fld_8b, 0x8B);

/* BattlePlan (36 bytes)
Battle plan, 0x24 bytes: per player a far pointer in DS:0x59FA (16 plans = 0x240 bytes, 14:0x0508),
plan count in DS:0x4A4A[player]. 14:0x271a reads a type-30 block, 14:0x50d6 writes it,
30:0x049a (BATTLEPLANSDLG) edits a copy in DS:0x53B8. */
struct BattlePlan {
  uint8_t fld_00;                    /* +0x00 (1): 14:0x271a: high nibble = plan index; 30:0x049a */
  uint8_t flags;                     /* +0x01 (1): 14:0x50d6: bit 6 = default name (name not written); 30:0x049a bits 6,7 */
  uint8_t fld_02;                    /* +0x02 (1): 30:0x049a: two 4-bit fields (dialog combo boxes) */
  uint8_t fld_03;                    /* +0x03 (1): 30:0x049a: (this & 0x1f) shown in a dialog list */
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

/* MapObject (18 bytes)
18-byte map object (minefield window data of 5:0x0000 MINEWNDPROC; type-43 blocks, BLK_MAP_OBJECT).
Far array DS:0x188E/0x1890, count DS:0x1892, capacity DS:0x1894. 14:0x0508 copies the block and sets +0x10 to the
current turn; 14:0x35b0 writes blocks of 0x12 bytes. Bits 5-7 of byte +1 select a kind (0x20 is handled as a
mineral-packet-like object, 14:0x0508 end). */
struct MapObject {
  uint16_t id_kind;                  /* +0x00 (2): 14:0x0508: array is kept sorted by this word; 5:0x0000 reads word 0; kind = (byte at +1) & 0xE0 (5:0x2840, 7:0x063e) */
  uint8_t unknown_02[4];             /* +0x02 (4): no access found in re/decomp; kept as padding */
  uint16_t fld_06;                   /* +0x06 (2): 14:0x0508: (this & 0x3ff) is a planet id when kind == 0x20; byte +7 & 0x3c tested */
  uint8_t unknown_08[4];             /* +0x08 (4): no access found in re/decomp; kept as padding */
  uint8_t fld_0c;                    /* +0x0C (1): 5:0x2ab4: + 0x4DE used as a string id */
  uint8_t fld_0d;                    /* +0x0D (1): 5:0x0000 stores a SendMessage result here; 5:0x329a tests it */
  uint8_t unknown_0E[2];             /* +0x0E (2): no access found in re/decomp; kept as padding */
  uint16_t turn;                     /* +0x10 (2): 14:0x0508: = GameHeader.turn (DS:0x82) after copying the block */
};
STATIC_SIZE(struct MapObject, 0x12);
STATIC_OFFSET(struct MapObject, id_kind, 0x0);
STATIC_OFFSET(struct MapObject, unknown_02, 0x2);
STATIC_OFFSET(struct MapObject, fld_06, 0x6);
STATIC_OFFSET(struct MapObject, unknown_08, 0x8);
STATIC_OFFSET(struct MapObject, fld_0c, 0xC);
STATIC_OFFSET(struct MapObject, fld_0d, 0xD);
STATIC_OFFSET(struct MapObject, unknown_0E, 0xE);
STATIC_OFFSET(struct MapObject, turn, 0x10);

/* MessageHeader (4 bytes)
Message record header in the message buffer (far pointer DS:0xAAC, used bytes DS:0xAB0 (< 0xFFC9),
count DS:0xAB4). 6:0x752e builds it: word 0 = message id (bits 0-8) | one bit per argument (bit 9+i set =
argument i is a word, clear = a byte); then word 1, then the arguments. Argument count per message id is in DS:0xE74 + id. */
struct MessageHeader {
  uint16_t id_arg_sizes;             /* +0x00 (2): 6:0x752e: (id & 0x1ff) | argument-size bits << 9 */
  uint16_t arg0;                     /* +0x02 (2): 6:0x752e: first argument word, always stored as a word */
};
STATIC_SIZE(struct MessageHeader, 0x4);
STATIC_OFFSET(struct MessageHeader, id_arg_sizes, 0x0);
STATIC_OFFSET(struct MessageHeader, arg0, 0x2);

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

/* Leaf functions that other segments call by their C name (docs/CONVENTIONS.md rule e: a callee that
 * mirrors no frame is a plain C call). */
uint16_t toolbar_item_is_on(uint16_t item); /* 13:0x0902 (fn_13_0902): is toolbar item `item` on (src/seg13.c) */
uint16_t fleet_has_colony_hull(uint32_t fleet); /* 16:0x16f6 (fn_16_16f6): does the fleet hold a ship of hull 14 or 15 (the colony ships) (src/seg16.c) */
uint16_t fleet_has_scout_frigate_destroyer(uint32_t fleet); /* 16:0x175c (fn_16_175c): does the fleet hold a ship of hull 4, 5 or 6 (scout, frigate, destroyer) (src/seg16.c) */

#endif /* STARS_SRC_STARS_H */
