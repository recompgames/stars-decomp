/* Layout tests for the shared game structs in src/stars.h (stars-bfj.2). Public (docs/PLAN.md
 * decision 8).
 *
 * 1. Compile time: src/stars.h holds a _Static_assert on sizeof and on offsetof of every field. The
 *    asserts below add the sizes the original code itself uses: allocation sizes and strides found
 *    in the machine code (each with the seg:off that shows it), so a wrong sizeof fails the build.
 * 2. Run time: BLOCK_* macros against the block-type bytes the loader 14:0x0508 compares with;
 *    struct fields written through the C struct land at the byte offsets the original code uses;
 *    the ORIGINAL block readers are run in the emulator on synthetic blocks and their output is
 *    read back through the C structs: 14:0x0370 (Player), 14:0x0000 (ShipDesign), 14:0x1f0c
 *    (Planet), 14:0x235e (Fleet, version-5 layout), 14:0x271a (BattlePlan). Calling conventions
 *    come from the disassembly, not from Ghidra's guessed prototypes (14:0x0370 takes two NEAR
 *    offsets, not a far pointer).
 *
 * DIFF tests planned for the segment tasks (stars-bfj segment epics); none is written here because
 * the segment functions are not decompiled yet:
 *   GameHeader   15:0x1334 (writes it), 14:0x0508 (copies it), 4:0x1f40 (prints game_name)
 *   Player       14:0x0370 (reader), 14:0x32a0 (writer), 28:0x21ee / 28:0x2200 / 28:0x2230 / 28:0x2254
 *   Planet       14:0x1f0c (reader), 14:0x4926 (writer)
 *   Fleet        14:0x235e (reader), 14:0x4c86 (writer), 30:0x0fe8
 *   Waypoint     14:0x235e, 14:0x35b0
 *   ShipDesign   14:0x0000 (reader), 14:0x342a (writer)
 *   BattlePlan   14:0x271a (reader), 14:0x50d6 (writer)
 *   ProdQueue    26:0x29da
 *   ZipPreset    26:0x3428, 26:0x33e4
 *   MapObject    5:0x2840, 5:0x329a, 7:0x063e
 *   MessageHeader 6:0x752e, 6:0x7344
 * Mutation proof (stars-bfj.2): move one field by one byte in a scratch copy of src/stars.h; the
 * _Static_assert in the header (or a CHECK below) fails. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "harness/harness.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"

/* ---- 1. sizes and strides the original code uses -------------------------------------------- */
_Static_assert(sizeof(struct GameHeader) == 0x40, "15:0x1334 block(7, 0x40, DS:0x70)");
_Static_assert(sizeof(struct Player) == 0xC0, "14:0x0508 DS:0x5A64 + i*0xC0");
_Static_assert(sizeof(struct Planet) == 0x38, "14:0x0508 allocates count * 0x38");
_Static_assert(sizeof(struct Fleet) == 0x7C, "14:0x0508 allocates 0x7C per fleet");
_Static_assert(sizeof(struct ShipDesign) * 16 == 0x8D0, "14:0x0508 allocates 0x8D0 for 16 ship designs");
_Static_assert(sizeof(struct ShipDesign) * 10 == 0x582, "14:0x0508 allocates 0x582 for 10 starbase designs");
_Static_assert(sizeof(struct BattlePlan) * 16 == 0x240, "14:0x0508 allocates 0x240 for 16 battle plans");
_Static_assert(sizeof(struct MapObject) == 0x12, "14:0x0508 allocates count * 0x12; 14:0x35b0 writes 0x12");
_Static_assert(sizeof(struct Waypoint) == 0x12, "14:0x235e allocates (count + 1) * 0x12 behind a 4-byte header");
_Static_assert(sizeof(struct ZipPreset) == 0x28, "26:0x2d26 DS:0x2576 + n*0x28");
_Static_assert(sizeof(struct ProdItem) == 4, "14:0x0508 queue block: size >> 2 entries; 14:0x35b0 count << 2");
_Static_assert(sizeof(struct ProdQueueHdr) == 4, "14:0x0508 allocates (n + 2) 4-byte cells");
_Static_assert(sizeof(struct WaypointListHdr) == 4, "14:0x235e entries start at list + 4");
_Static_assert(sizeof(struct FileHeader) == 0x10, "14:0x2caa copies 4 dwords of the type-8 body");
_Static_assert(sizeof(struct MessageHeader) == 4, "6:0x752e: word 0 (id | argument bits), word 1");

/* ---- 3. oracle: the ORIGINAL player-block reader 14:0x0370 ------------------------------- */
static void fill(uint8_t *p, size_t n, uint8_t seed) {
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i * 7);
}

static void test_player_reader_oracle(void) {
  /* Block body at DS:0x4DB2 (where 14:0x305a puts it): a long record (byte 6 & 7 == 7). */
  uint8_t *blk = LINN(0x4DB2);
  memset(blk, 0, 0x100);
  fill(blk, 0x70, 0x11);
  blk[0] = 3;       /* player id */
  blk[1] = 2;       /* design count */
  blk[6] = 0x07;    /* long-record flag */
  blk[0x70] = 4;    /* relations length */
  blk[0x71] = 1; blk[0x72] = 2; blk[0x73] = 3; blk[0x74] = 4;
  size_t o = 0x75;
  blk[o++] = 0;     /* plain (not compressed) name follows */
  memcpy(blk + o, "Alpha", 6); o += 6;
  blk[o++] = 0;     /* plain plural name follows */
  memcpy(blk + o, "Alphas", 7);
  /* The plural is read only when DS:0x772 (game version word) & 0xFE0 >= 0x6E0. */
  uint16_t ver_save;
  memcpy(&ver_save, LINN(0x772), 2);
  uint16_t ver = 0x0A00;
  memcpy(LINN(0x772), &ver, 2);

  struct Player *pl = (struct Player *)LINN(DS_PLAYERS + 3 * sizeof(struct Player));
  memset(pl, 0xEE, sizeof *pl);
  /* 14:0x0370 is FAR CDECL (dest, src), both near offsets into DS (Ghidra shows a far dest; the
   * disassembly zeroes 0x30 dwords at ES:[bp+6] with ES = DS and reads the block at DS:[bp+8]). */
  uint16_t args[2] = {(uint16_t)(DS_PLAYERS + 3 * sizeof(struct Player)), 0x4DB2};
  harness_call(SEG_SEL(14), 0x0370, HCONV_CDECL, args, 2, NULL);

  CHECK_EQ(pl->player_id, 3);
  CHECK_EQ(pl->design_count, 2);
  CHECK_EQ(pl->record_flags, 0x07);
  CHECK_MEM(pl, blk, 0x08 - 0);             /* bytes 0..7 verbatim */
  CHECK_MEM((uint8_t *)pl + 0x08, blk + 0x08, 0x54 - 0x08); /* up to fld_54 verbatim */
  CHECK_EQ(pl->fld_54, (uint16_t)((blk[0x54] | (blk[0x55] << 8)) & 7)); /* masked with 7 */
  CHECK_EQ(pl->relations[0], 1);
  CHECK_EQ(pl->relations[3], 4);
  CHECK_EQ(strcmp((char *)pl->name, "Alpha"), 0);
  CHECK_EQ(strcmp((char *)pl->plural_name, "Alphas"), 0);
  memcpy(LINN(0x772), &ver_save, 2);
}

static void test_design_reader_oracle(void) {
  /* The ORIGINAL design-block reader 14:0x0000 (FAR CDECL: far src block, far dest design array;
   * disassembly: SI = [bp+6], ES = [bp+8] for the block, dest base at [bp+0xA]/[bp+0xC]). Dest is
   * DS:0x417A, the 16-design static table that 14:0x0508 points DS:0xBE[player] at (0x4A4A -
   * 0x417A = 16 * 0x8D). Version byte 7, design slot 5. */
  uint8_t *blk = LINN(0x4DB2);
  memset(blk, 0, 0x100);
  blk[0] = 7;                 /* record version */
  blk[1] = (uint8_t)(5 << 2); /* slot 5 (bits 2-6) */
  blk[2] = 3;                 /* hull */
  blk[3] = 0x2A;
  blk[4] = 0x34; blk[5] = 0x12;
  blk[6] = 2;                 /* slot count */
  blk[7] = 0x78; blk[8] = 0x56;
  blk[9] = 0xDD; blk[10] = 0xCC; blk[11] = 0xBB; blk[12] = 0xAA;
  blk[13] = 0x44; blk[14] = 0x33; blk[15] = 0x22; blk[16] = 0x11;
  const uint8_t slots[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  memcpy(blk + 17, slots, 8);
  blk[25] = 0;                /* plain name follows */
  memcpy(blk + 26, "Zed", 4);
  uint16_t cur_save;
  memcpy(&cur_save, LINN(0x14C), 2);
  uint16_t cur = 0xFFFF;      /* no current player: skips the cost recomputation (14:0x0000 tail) */
  memcpy(LINN(0x14C), &cur, 2);

  uint16_t args[4] = {0x4DB2, MEM_SEL_DGROUP, DS_DEFAULT_SHIP_DESIGNS, MEM_SEL_DGROUP};
  memset(LINN(DS_DEFAULT_SHIP_DESIGNS), 0, 16 * sizeof(struct ShipDesign));
  harness_call(SEG_SEL(14), 0x0000, HCONV_CDECL, args, 4, NULL);

  struct ShipDesign *d = (struct ShipDesign *)LINN(DS_DEFAULT_SHIP_DESIGNS + 5 * sizeof(struct ShipDesign));
  CHECK_EQ(d->hull, 3);
  CHECK_EQ(d->strength, 0x1234);
  CHECK_EQ(d->slot_count, 2);
  CHECK_EQ(d->version, 7);
  CHECK_EQ(d->flags, 0x14);
  CHECK_EQ(d->fld_7d, 0x5678);
  CHECK_EQ(d->fld_7f, 0xAABBCCDDu);
  CHECK_EQ(d->fld_83, 0x11223344u);
  CHECK_EQ(d->fld_32 & 3, 0x2A & 3);
  CHECK_MEM(&d->slots[0], slots, 8);
  CHECK_EQ(strcmp((char *)d->name, "Zed"), 0);
  memcpy(LINN(0x14C), &cur_save, 2);
}

static void test_planet_reader_oracle(void) {
  /* The ORIGINAL planet-block reader 14:0x1f0c, FAR CDECL (player, far planet, p3, p4); with p4 = 0
   * it zeroes the planet first and with p3 != 0 it takes the "refresh from file" branch. The block
   * is a type-14 body at DS:0x4DB2 (header word at DS:0x2910) of detail level 4. */
  uint8_t *blk = LINN(0x4DB2);
  memset(blk, 0, 0x40);
  blk[0] = 0x23; blk[1] = 0x29;       /* id 0x123 | owner 5 << 11 */
  blk[2] = 0x04; blk[3] = 0x20;       /* detail level 4, bit 13: "four wide values" present */
  blk[4] = 0x05;                      /* three 2-bit codes: 1, 1, 0 */
  blk[5] = 0x11; blk[6] = 0x22;       /* planet +6, +7 (+8 stays 0) */
  blk[7] = 0x33; blk[8] = 0x44; blk[9] = 0x55;   /* planet +9..+0xB */
  blk[10] = 0x10; blk[11] = 0x20; blk[12] = 0x30; /* planet +0xC..+0xE, copied to +0xF..+0x11 */
  blk[13] = 0xEF; blk[14] = 0xBE;     /* planet +0x12 */
  blk[15] = 0x39;                     /* codes 1, 2, 3, 0 */
  blk[16] = 0x07;                     /* planet +0x1C = 7 */
  blk[17] = 0x02; blk[18] = 0x01;     /* planet +0x20 = 0x0102 */
  blk[19] = 0x0D; blk[20] = 0x0C; blk[21] = 0x0B; blk[22] = 0x0A; /* planet +0x24 */
  blk[23] = 0x77; blk[24] = 0x07;     /* planet +0x32 (file mode) */
  uint16_t hdr = BLOCK_MAKE(BLK_PLANET, 25);
  memcpy(LINN(0x2910), &hdr, 2);

  /* Planet buffer: DS:0x6600..0x6637, bytes of DGROUP (size 0x6688) with no referenced global. */
  uint16_t args[5] = {0, 0x6600, MEM_SEL_DGROUP, 1, 0};
  struct Planet *pl = (struct Planet *)LINN(0x6600);
  memset(pl, 0xEE, sizeof *pl);
  uint32_t ok = harness_call(SEG_SEL(14), 0x1F0C, HCONV_CDECL, args, 5, NULL);

  CHECK_EQ(ok & 0xFFFF, 1);
  CHECK_EQ(pl->id, 0x123);
  CHECK_EQ(pl->owner, 5);
  CHECK_EQ(pl->info_level, 4);
  CHECK_EQ(pl->fld_06[0], 0x11);
  CHECK_EQ(pl->fld_06[1], 0x22);
  CHECK_EQ(pl->fld_06[2], 0);
  CHECK_EQ(pl->fld_09[0], 0x33);
  CHECK_EQ(pl->fld_09[2], 0x55);
  CHECK_EQ(pl->env_current[0], 0x10);
  CHECK_EQ(pl->env_current[2], 0x30);
  CHECK_EQ(pl->env_original[0], 0x10);
  CHECK_EQ(pl->env_original[2], 0x30);
  CHECK_EQ(pl->fld_12, 0xBEEF);
  CHECK_EQ(pl->fld_1c[0], 7);
  CHECK_EQ(pl->fld_1c[1], 0x0102);
  CHECK_EQ(pl->fld_1c[2], 0x0A0B0C0Du);
  CHECK_EQ(pl->fld_1c[3], 0);
  CHECK_EQ(pl->turn, 0x0777);
}

static void test_fleet_reader_oracle(void) {
  /* The ORIGINAL fleet-block reader 14:0x235e, FAR CDECL (far fleet record). A version-5 block
   * (the old layout: no following blocks to read) with byte-sized ship counts (flags bit 3). The
   * epilogue reads the next block header through 14:0x305a; DS:0x968 (in-memory source) points it
   * at two zero bytes (an end block). */
  uint8_t *blk = LINN(0x4DB2);
  memset(blk, 0, 0x40);
  blk[0] = 0x33; blk[1] = 0x00;       /* id */
  blk[2] = 0x02; blk[3] = 0x00;       /* owner */
  blk[4] = 5;                         /* version */
  blk[5] = 0x08;                      /* byte-sized counts */
  blk[6] = 0xFF; blk[7] = 0xFF;       /* in deep space */
  blk[8] = 0x11; blk[9] = 0x22;       /* x */
  blk[10] = 0x33; blk[11] = 0x44;     /* y */
  blk[12] = 0x05; blk[13] = 0x00;     /* ship-count mask: designs 0 and 2 */
  blk[14] = 0x07; blk[15] = 0x09;     /* ship_count[0] = 7, ship_count[2] = 9 */
  blk[16] = 0x39; blk[17] = 0x00;     /* cargo codes: 1 (byte), 2 (word), 3 (dword), 0, 0 */
  blk[18] = 0x55;
  blk[19] = 0x34; blk[20] = 0x12;
  blk[21] = 0xD4; blk[22] = 0xC3; blk[23] = 0xB2; blk[24] = 0xA1;
  blk[25] = 0x04; blk[26] = 0x03; blk[27] = 0x02; blk[28] = 0x01; /* old layout: dword -> +0x74 */
  blk[29] = 0x0D; blk[30] = 0x0C; blk[31] = 0x0B; blk[32] = 0x0A; /* then dword -> +0x2C */
  uint32_t save968;
  memcpy(&save968, LINN(0x968), 4);
  uint32_t src = MK_FP(MEM_SEL_DGROUP, 0x6640);
  memset(LINN(0x6640), 0, 8);
  memcpy(LINN(0x968), &src, 4);

  struct Fleet *fl = (struct Fleet *)LINN(0x6600);
  memset(fl, 0xEE, sizeof *fl);
  uint16_t args[2] = {0x6600, MEM_SEL_DGROUP};
  uint32_t ok = harness_call(SEG_SEL(14), 0x235E, HCONV_CDECL, args, 2, NULL);

  CHECK_EQ(ok & 0xFFFF, 1);
  CHECK_EQ(fl->id, 0x33);
  CHECK_EQ(fl->owner, 2);
  CHECK_EQ(fl->version, 5);
  CHECK_EQ(fl->flags, 0x08);
  CHECK_EQ(fl->planet_id, 0xFFFF);
  CHECK_EQ(fl->x, 0x2211);
  CHECK_EQ(fl->y, 0x4433);
  CHECK_EQ(fl->ship_count[0], 7);
  CHECK_EQ(fl->ship_count[1], 0);
  CHECK_EQ(fl->ship_count[2], 9);
  CHECK_EQ(fl->cargo[0], 0x55);
  CHECK_EQ(fl->cargo[1], 0x1234);
  CHECK_EQ(fl->cargo[2], 0xA1B2C3D4u);
  CHECK_EQ(fl->cargo[3], 0);
  CHECK_EQ(fl->fld_74, 0x01020304u);
  CHECK_EQ((uint32_t)fl->damage[0] | ((uint32_t)fl->damage[1] << 16), 0x0A0B0C0Du);
  CHECK_EQ(fl->waypoints, 0u);
  memcpy(LINN(0x968), &save968, 4);
}

static void test_battle_plan_reader_oracle(void) {
  /* The ORIGINAL battle-plan block reader 14:0x271a, FAR CDECL (far src block, far dest plan,
   * plan index): the 4-byte header is copied, the high nibble of byte 0 is replaced by the index,
   * and a plain (flag byte 0) name at +4 is copied to +4. */
  uint8_t *blk = LINN(0x4DB2);
  memset(blk, 0, 0x40);
  blk[0] = 0xAB; blk[1] = 0x00; blk[2] = 0x12; blk[3] = 0x05;
  blk[4] = 0;                          /* plain name follows */
  memcpy(blk + 5, "Bomber", 7);
  struct BattlePlan *bp = (struct BattlePlan *)LINN(0x6600);
  memset(bp, 0xEE, sizeof *bp);
  uint16_t args[5] = {0x4DB2, MEM_SEL_DGROUP, 0x6600, MEM_SEL_DGROUP, 3};
  harness_call(SEG_SEL(14), 0x271A, HCONV_CDECL, args, 5, NULL);
  CHECK_EQ(bp->fld_00, 0x3B);
  CHECK_EQ(bp->flags, 0x00);
  CHECK_EQ(bp->fld_02, 0x12);
  CHECK_EQ(bp->fld_03, 0x05);
  CHECK_EQ(strcmp((char *)bp->name, "Bomber"), 0);
}

int main(void) {
  /* ---- 2. block header and the loader's type bytes ---- */
  CHECK_EQ(BLOCK_SIZE(0x1C40), 0x40);
  CHECK_EQ(BLOCK_TYPE(0x1C40), BLK_GAME_HEADER);
  CHECK_EQ(BLOCK_MAKE(BLK_GAME_HEADER, 0x40), 0x1C40);
  CHECK_EQ(BLOCK_MAKE(BLK_END, 2), 2);
  /* High byte & 0xFC is what 14:0x0508 compares: 0x18 player, 0x1C game, 0x20 file header, 0x38
   * planet, 0x40/0x4C/0x50/0x54 fleet/waypoints/name, 0x68 design, 0x70 queue, 0x78 plan, 0x90,
   * 0xAC map object. */
  CHECK_EQ((BLOCK_MAKE(BLK_PLAYER, 0) >> 8) & 0xFC, 0x18);
  CHECK_EQ((BLOCK_MAKE(BLK_GAME_HEADER, 0) >> 8) & 0xFC, 0x1C);
  CHECK_EQ((BLOCK_MAKE(BLK_FILE_HEADER, 0) >> 8) & 0xFC, 0x20);
  CHECK_EQ((BLOCK_MAKE(BLK_PLANET, 0) >> 8) & 0xFC, 0x38);
  CHECK_EQ((BLOCK_MAKE(BLK_FLEET, 0) >> 8) & 0xFC, 0x40);
  CHECK_EQ((BLOCK_MAKE(BLK_WAYPOINT_19, 0) >> 8) & 0xFC, 0x4C);
  CHECK_EQ((BLOCK_MAKE(BLK_WAYPOINT_20, 0) >> 8) & 0xFC, 0x50);
  CHECK_EQ((BLOCK_MAKE(BLK_FLEET_NAME, 0) >> 8) & 0xFC, 0x54);
  CHECK_EQ((BLOCK_MAKE(BLK_DESIGN, 0) >> 8) & 0xFC, 0x68);
  CHECK_EQ((BLOCK_MAKE(BLK_PROD_QUEUE, 0) >> 8) & 0xFC, 0x70);
  CHECK_EQ((BLOCK_MAKE(BLK_BATTLE_PLAN, 0) >> 8) & 0xFC, 0x78);
  CHECK_EQ((BLOCK_MAKE(BLK_36, 0) >> 8) & 0xFC, 0x90);
  CHECK_EQ((BLOCK_MAKE(BLK_MAP_OBJECT, 0) >> 8) & 0xFC, 0xAC);
  CHECK_EQ((BLOCK_MAKE(BLK_45, 0) >> 8) & 0xFC, 0xB4);
  CHECK_EQ(BLOCK_SIZE(0xFFFF), 0x3FF);
  CHECK_EQ(BLOCK_TYPE(0xFFFF), 0x3F);

  /* ---- fields written through the struct land at the original's byte offsets ---- */
  {
    struct Planet pl;
    uint8_t raw[sizeof pl];
    memset(&pl, 0, sizeof pl);
    pl.queue = MK_FP(0x1234, 0x5678);
    memcpy(raw, &pl, sizeof raw);
    CHECK_EQ(raw[0x34], 0x78); /* offset word at +0x34 (14:0x0508: planet word 0x1A) */
    CHECK_EQ(raw[0x35], 0x56);
    CHECK_EQ(raw[0x36], 0x34); /* selector word at +0x36 (planet word 0x1B) */
    CHECK_EQ(raw[0x37], 0x12);
    pl.turn = 0xBEEF;
    memcpy(raw, &pl, sizeof raw);
    CHECK_EQ(raw[0x32] | (raw[0x33] << 8), 0xBEEF);
  }
  {
    struct Fleet fl;
    uint8_t raw[sizeof fl];
    memset(&fl, 0, sizeof fl);
    fl.waypoints = MK_FP(0xABCD, 0x0123);
    fl.x = 0x1111;
    fl.y = 0x2222;
    fl.ship_count[15] = 0x4242;
    fl.fuel = 0x0A0B0C0Du;
    memcpy(raw, &fl, sizeof raw);
    CHECK_EQ(raw[0x64] | (raw[0x65] << 8), 0x0123); /* 14:0x235e: word 0x19 of dwords = +0x64 */
    CHECK_EQ(raw[0x66] | (raw[0x67] << 8), 0xABCD);
    CHECK_EQ(raw[0x08] | (raw[0x09] << 8), 0x1111);
    CHECK_EQ(raw[0x0A] | (raw[0x0B] << 8), 0x2222);
    CHECK_EQ(raw[0x2A] | (raw[0x2B] << 8), 0x4242); /* last of 16 words starting at +0x0C */
    CHECK_EQ(raw[0x5C], 0x0D);                      /* last of five dwords starting at +0x4C */
    CHECK_EQ(raw[0x5F], 0x0A);
  }
  {
    struct ShipDesign d;
    uint8_t raw[sizeof d];
    memset(&d, 0, sizeof d);
    d.slots[15].count = 9;
    d.slot_count = 16;
    memcpy(raw, &d, sizeof raw);
    CHECK_EQ(raw[0x3A + 15 * 4 + 3], 9); /* 16 slots of 4 bytes at +0x3A end at +0x7A */
    CHECK_EQ(raw[0x7A], 16);
  }
  {
    /* ScoreRecord (type-45 block): 14:0x0508 allocates player_count * 0x18 at DS:0x177A; 14:0x35b0 writes 0x18 */
    struct ScoreRecord sc;
    uint8_t raw[sizeof sc];
    CHECK_EQ(sizeof(struct ScoreRecord), 0x18);
    memset(&sc, 0, sizeof sc);
    sc.id_flags = 0x0023;
    sc.rank = 0x0504;
    sc.score = 0x44332211u;
    sc.resources = 0x88776655u;
    sc.planets = 0xAABB;
    sc.starbases = 0xCCDD;
    sc.ships[2] = 0x1234;
    sc.tech = 0xBEEF;
    memcpy(raw, &sc, sizeof raw);
    CHECK_EQ(raw[0x00] & SCORE_ID_MASK, 3);       /* 14:0x0508: record index = word & 0x1f */
    CHECK_EQ(raw[0x00] & SCORE_VALID, SCORE_VALID); /* 32:0x158e: bit 0x20 of byte 0 */
    CHECK_EQ(raw[0x02], 0x04);                    /* 32:0x158e column 8 reads word +2 */
    CHECK_EQ(raw[0x04], 0x11);                    /* column 7: dword +4 */
    CHECK_EQ(raw[0x08], 0x55);                    /* column 6: dword +8 */
    CHECK_EQ(raw[0x0C], 0xBB);                    /* column 0: word +0xC */
    CHECK_EQ(raw[0x0E], 0xDD);                    /* column 1: word +0xE */
    CHECK_EQ(raw[0x14], 0x34);                    /* columns 2-4: words +0x10, +0x12, +0x14 */
    CHECK_EQ(raw[0x16], 0xEF);                    /* column 5: word +0x16 */
  }
  {
    /* Fields named from 16:0x0962 / 16:0x13f8 / 16:0x1e02 / 26:0x29da / 23:0x3b78 */
    CHECK_EQ(offsetof(struct Fleet, damage), 0x2C);   /* 16:0x0962 */
    CHECK_EQ(offsetof(struct Fleet, cargo) + sizeof(((struct Fleet *)0)->cargo), 0x5C); /* four cargo dwords at +0x4c, 16:0x1e02 */
    CHECK_EQ(offsetof(struct Fleet, fuel), 0x5C);     /* 16:0x13f8 */
    CHECK_EQ(offsetof(struct ShipDesign, mass), 0x28);     /* 16:0x1e02 */
    CHECK_EQ(offsetof(struct ShipDesign, strength), 0x38); /* 16:0x0962 */
    CHECK_EQ(offsetof(struct Planet, route_target), 0x30); /* 16:0x13f8 */
    CHECK_EQ(offsetof(struct Planet, env_current), 0x0C);
    CHECK_EQ(offsetof(struct Planet, env_original), 0x0F);
    CHECK_EQ(offsetof(struct Player, cost_adjust_pct), 0x38); /* 26:0x29da DS:0x5A9C */
    CHECK_EQ(offsetof(struct Player, fld_0a), 0x0A);          /* 23:0x3b78 DS:0x5A6E */
    struct Fleet fl;
    memset(&fl, 0, sizeof fl);
    fl.damage[0] = (uint16_t)((9u << FLEET_DAMAGE_SHIFT) | 40);
    CHECK_EQ(fl.damage[0] & FLEET_DAMAGE_PCT_MASK, 40);
    CHECK_EQ(fl.damage[0] >> FLEET_DAMAGE_SHIFT, 9);
    CHECK_EQ((0x0403 & PRODITEM_QTY_MASK), 3);
    CHECK_EQ((0x0002 & PRODITEM_W1_KIND_MASK), PRODITEM_W1_KIND_SPECIAL);
  }
  {
    /* 5-byte turn-time message record (6:0x7410 build_message_record): byte 0 = player (low nibble) | value
     * length (high nibble); word +1 = id (bits 0-8) | one word-flag per value (bits 9-15); word +3 = subject */
    struct MessageRecord5 r;
    uint8_t raw[sizeof r];
    CHECK_EQ(sizeof(struct MessageRecord5), 5);
    memset(&r, 0, sizeof r);
    r.player_len = (uint8_t)((6u << MSG_LEN_SHIFT) | 9u);
    r.id_arg_sizes = (uint16_t)(0x0123 | (5u << MSG_WORD_FLAG_SHIFT));
    r.subject = 0xBEEF;
    memcpy(raw, &r, sizeof raw);
    CHECK_EQ(raw[0] & MSG_PLAYER_MASK, 9);
    CHECK_EQ(raw[0] >> MSG_LEN_SHIFT, 6);
    CHECK_EQ(raw[1] | (raw[2] << 8), 0x0123 | (5 << 9)); /* word at +1 */
    CHECK_EQ(raw[3] | (raw[4] << 8), 0xBEEF);            /* subject word at +3 */
    CHECK_EQ(offsetof(struct MessageHeader, subject), 2); /* 6:0x752e word +2 is the subject */
    CHECK_EQ(MSG_ID_MASK, 0x1FF);
  }
  {
    /* BattleToken 0x1d bytes: 14:0x27d4 steps tokens by byte(+3 of the record) * 0x1d; 30:0x5234 */
    CHECK_EQ(sizeof(struct BattleToken), 0x1D);
    CHECK_EQ(offsetof(struct BattleToken, owner), 0x02);
    CHECK_EQ(offsetof(struct BattleToken, kind), 0x03);
    CHECK_EQ(offsetof(struct BattleToken, design_slot), 0x04);
    CHECK_EQ(offsetof(struct BattleToken, ship_count), 0x13);
    CHECK_EQ(offsetof(struct BattleToken, damage), 0x15);
    CHECK_EQ(offsetof(struct BattleToken, flags), 0x1B);
  }
  {
    /* MapObject.seen_by_mask: 14:0x35b0 tests (word at +0xE) & (1 << player) for minefields */
    CHECK_EQ(offsetof(struct MapObject, seen_by_mask), 0x0E);
    /* mineral packet speed: 5:0x04e8 shows ((byte at +7) & 0x3c) >> 2) + 4 */
    CHECK_EQ(MAPOBJ_PACKET_WARP((uint8_t)0x14), 9);
    CHECK_EQ(PLAYER_ATTR_RACE, 14);
    CHECK_EQ(sizeof(((struct Player *)0)->attrs), PLAYER_ATTR_COUNT);
    /* ProdItem fields (26:0x29da) */
    struct ProdItem pi = {.w0 = (uint16_t)((3u << 10) | 25u), .w1 = 0x0002};
    CHECK_EQ(prod_item_qty(&pi), 25);
    CHECK_EQ(prod_item_class(&pi), 1);
    CHECK_EQ(prod_item_index(&pi), 3);
    pi.w1 = 0x0001;
    CHECK_EQ(prod_item_index(&pi), 3 | 64); /* w1 bit 0 is the high bit of the 7-bit item index */
  }
  {
    struct MessageHeader m;
    memset(&m, 0, sizeof m);
    m.id_arg_sizes = (uint16_t)(0x1FF | (3u << 9));
    CHECK_EQ(m.id_arg_sizes & 0x1FF, 0x1FF);
    CHECK_EQ(m.id_arg_sizes >> 9, 3);
  }

  {
    struct MapObject mo;
    uint8_t raw[sizeof mo];
    CHECK_EQ(sizeof(struct MapObject), 0x12); /* 14:0x0508 allocates count * 0x12; 5:0x0474 steps by 0x12 */
    memset(&mo, 0, sizeof mo);
    mo.id_kind = 0x2A71;
    mo.xy = 0x44332211u;
    memcpy(raw, &mo, sizeof raw);
    CHECK_EQ(raw[0x00], 0x71); /* id_lo is the low byte of word 0 */
    CHECK_EQ(raw[0x01], 0x2A); /* owner_kind_bits is the high byte: 5:0x0474 reads the byte at +1 */
    CHECK_EQ(raw[0x02], 0x11); /* xy dword at +2: 5:0x36a8 */
    CHECK_EQ(raw[0x05], 0x44);
    CHECK_EQ(map_object_owner(mo.owner_kind_bits), 5);    /* (0x2A & 0x1E) >> 1 */
    CHECK_EQ(map_object_kind(mo.owner_kind_bits), 0x20);  /* 0x2A & 0xE0 */
    mo.owner_kind_bits = 0x1E;
    CHECK_EQ(map_object_owner(mo.owner_kind_bits), 15);
    CHECK_EQ(map_object_kind(mo.owner_kind_bits), MAPOBJ_KIND_MINEFIELD);
  }

  /* ---- 3. oracle ---- */
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  test_player_reader_oracle();
  test_design_reader_oracle();
  test_planet_reader_oracle();
  test_fleet_reader_oracle();
  test_battle_plan_reader_oracle();
  harness_shutdown();
  return TEST_DONE();
}
