/* Diff tests for decompiled segment 2 (src/seg02.c). Public (docs/PLAN.md decision 8).
 * Worked example of docs/CONVENTIONS.md: seg 2 off 0x0000 (FUN_1008_0000, 15 bytes). It is one of
 * four same-shape functions in segment 2 (2:0x0000, 0x50a2, 0x50b0, 0x50ca), chosen because it
 * exercises an argument and a DX:AX return; the literal smallest leaf is a bare RETF at 4:0x4db8,
 * which exercises neither.
 *
 * Segment 2 holds the tech-requirement tables (records of 0x8f, 0x4e, 0x38, 0x36, ... bytes inside the
 * code segment, reached through the small accessor functions 2:0x0000, 0x507c, 0x50a2, 0x50b0,
 * 0x50be, 0x50c4, 0x50ca) and the check "may the current player use item (kind, index)":
 *   2:0x50d8  set (kind, index) of a request record and run the check
 *   2:0x50f8  the check itself: finds the item's record, applies the player's trait rules
 *   2:0x5842  run the check for kind 0x8000, from index 8 down until it answers 1
 *   2:0x587a  compare the item's six requirement bytes with the player's six tech levels
 * Every scenario builds its state by writing DGROUP directly (the request record, the player rows
 * of 0xc0 bytes at DS:0x5a64 and the current player word DS:0x14c); DIFF then snapshots it and compares
 * the original with the C version (return registers, all memory outside the callee's own frame, the
 * ordered API call log; this segment calls no Win16 API).
 *
 * Run as ORIGINAL code in both runs (no double): 28:0x21ee (a signed player byte), 28:0x2230 (a
 * bit of the player's trait dword) and 27:0x4af6 (a bit of the player's mask word). They are small
 * and read only the player row, so each scenario steers them through the row bytes. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

/* SEG02_ONLY=tag[,tag] runs only the named parts (acc 587a 50f8 50d8 5842): a mutation check of
 * one function does not need the whole run. Unset: everything. */
static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }

/* The request record every function takes: DS:REQ = kind word, +2 = index word (low byte used),
 * +4/+6 = the table pointer the check stores. */
#define REQ 0x3000
#define ROW(k) ((uint16_t)(0x5a64 + 0xc0 * (k))) /* the player row of player k (0xc0 bytes) */

/* The player rows -1 .. 3 (row -1 is what DS:0x14c = 0xffff addresses) filled with random bytes,
 * then the fields the callees read steered:
 *   +0x4c  signed byte read by 28:0x21ee(row, 0xe): the "race" value the rules compare with
 *   +0x4e  dword of trait bits read by 28:0x2230(row, n)
 *   +0x52  word mask read by 27:0x4af6
 *   +0x1a..+0x1f  six signed tech levels, +0x39 low nibble the focus field, +0x19 the byte before
 * The pattern argument picks all-zero, all-one or random for the trait dword and the mask. */
static void world(int8_t di, unsigned trait_pat, unsigned mask_pat) {
  for (int k = -1; k <= 3; k++) {
    uint8_t *row = LINN(ROW(k));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    row[0x4c] = (uint8_t)di;
    uint32_t t = trait_pat == 0 ? 0 : trait_pat == 1 ? 0xffffffffu : rnd();
    memcpy(row + 0x4e, &t, 4);
    uint16_t m = mask_pat == 0 ? 0 : mask_pat == 1 ? 0xffff : (uint16_t)rnd();
    memcpy(row + 0x52, &m, 2);
  }
}

static void req(uint16_t kind, uint16_t idx_word) {
  DS_W(REQ) = kind;
  DS_W(REQ + 2) = idx_word;
  DS_W(REQ + 4) = (uint16_t)rnd();
  DS_W(REQ + 6) = (uint16_t)rnd();
}

/* kind, number of valid indexes (the check answers 0 and stores nothing from this index up) */
static const struct { uint16_t kind, n; } kinds[] = {
  {0x1, 0x10}, {0x2, 0x10}, {0x4, 0x0a}, {0x8, 0x0c}, {0x10, 0x18}, {0x20, 0x0c}, {0x40, 0x0f}, {0x80, 0x08},
  {0x100, 0x0a}, {0x200, 0x10}, {0x400, 0x05}, {0x800, 0x11}, {0x1000, 0x0b}, {0x2000, 0x14}, {0x4000, 0x20},
  {0x8000, 0x0f},
};
#define NKINDS (sizeof kinds / sizeof kinds[0])
/* Words that are not a kind: zero, a kind with a second bit, the gaps of the dispatch. */
static const uint16_t bad_kinds[] = {0x0000, 0x0003, 0x0011, 0x0021, 0x0030, 0x0041, 0x0081, 0x0101, 0x0300,
                                     0x0401, 0x0801, 0x1001, 0x2001, 0x4001, 0x8001, 0xc000, 0xffff, 0x7fff};
static const uint16_t c14s[] = {0xffff, 0, 1, 3};

/* ---- 2:0x50a2, 0x50b0, 0x50ca: table accessors; 2:0x50be, 0x50c4: constants ---- */

static void t_accessors(void) {
  static const uint16_t idx[] = {0, 1, 2, 5, 0x0f, 0x10, 0x1f, 0x20, 0x7fff, 0x8000, 0xffff, 0xfffe, 0x0100};
  for (unsigned i = 0; i < sizeof idx / 2; i++) {
    DIFF(2, 0x50a2, FAR_CDECL, idx[i]);
    DIFF(2, 0x50b0, FAR_CDECL, idx[i]);
    DIFF(2, 0x50ca, FAR_CDECL, idx[i]);
    DIFF(2, 0x507c, FAR_CDECL, idx[i]);
  }
  /* 2:0x507c around its signed threshold 0x20: below it the 0x8f-byte record at 0x14ac, from 0x20 up
   * 2:0x0000(index - 0x20); negative indexes are below the threshold (JL). */
  for (uint16_t v = 0x1c; v < 0x26; v++) DIFF(2, 0x507c, FAR_CDECL, v);
  for (uint16_t v = 0xfff0; v != 0; v++) DIFF(2, 0x507c, FAR_CDECL, v);
  DIFF(2, 0x50be, FAR_CDECL);
  DIFF(2, 0x50c4, FAR_CDECL);
  DIFF_FUZZ(2, 0x50a2, FAR_CDECL, 1, 300, 0x5EED50A2u);
  DIFF_FUZZ(2, 0x50b0, FAR_CDECL, 1, 300, 0x5EED50B0u);
  DIFF_FUZZ(2, 0x50ca, FAR_CDECL, 1, 300, 0x5EED50CAu);
  DIFF_FUZZ(2, 0x507c, FAR_CDECL, 1, 300, 0x5EED507Cu);
}

/* ---- 2:0x587a: six requirement bytes against six tech levels ---- */

static void t_587a(void) {
  static const int8_t vals[] = {-128, -127, -2, -1, 0, 1, 2, 126, 127};
  uint16_t blk = 0x3100; /* the item's bytes (DGROUP), and a second copy in a far block */
  uint16_t h = GlobalAlloc(0x42, 64);
  CHECK(h != 0);
  uint32_t far_blk = GlobalLock(h);
  CHECK(far_blk != 0);
  for (unsigned it = 0; it < 2500; it++) {
    g_rng = 0x587a0001u + 0x9e3779b9u * (it + 1);
    world(0, 2, 2);
    uint16_t c14 = c14s[rn(4)];
    DS_W(0x14c) = c14;
    uint8_t *cur = LINN((uint16_t)(0x5a7e + 0xc0 * c14));
    uint8_t *nib = LINN((uint16_t)(0x5a9d + 0xc0 * c14));
    uint8_t *prev = LINN((uint16_t)(0x5a7d + 0xc0 * c14));
    uint8_t item[8];
    for (unsigned i = 0; i < 8; i++) item[i] = (uint8_t)vals[rn(sizeof vals)];
    for (unsigned i = 0; i < 6; i++) {
      cur[i] = (uint8_t)vals[rn(sizeof vals)];
      /* make "current below item" and "item = current + 1" common: derive some item bytes from it */
      switch (rn(5)) {
        case 0: item[i] = cur[i]; break;
        case 1: item[i] = (uint8_t)(cur[i] + 1); break;
        case 2: item[i] = (uint8_t)(cur[i] + 2); break;
        case 3: item[i] = (uint8_t)(cur[i] - 1); break;
        default: break;
      }
    }
    if (rn(3) == 0) *prev = (uint8_t)vals[rn(sizeof vals)];
    *nib = (uint8_t)((*nib & 0xf0) | (rn(4) == 0 ? 6 + rn(10) : rn(6)));
    if (rn(2)) {
      memcpy(LINN(blk), item, 8);
      DIFF(2, 0x587a, FAR_CDECL, blk, MEM_SEL_DGROUP);
    } else {
      memcpy(LIN(far_blk), item, 8);
      DIFF(2, 0x587a, FAR_CDECL, LO(far_blk), HI(far_blk));
    }
  }
  /* the offset of the item bytes high in the segment: the index arithmetic of the exit that returns
   * the difference (item[idx - 1]) still has to wrap like a 16-bit register */
  for (unsigned it = 0; it < 200; it++) {
    g_rng = 0x587a1001u + 0x9e3779b9u * (it + 1);
    world(0, 2, 2);
    DS_W(0x14c) = 0;
    uint8_t *cur = LINN(0x5a7e);
    for (unsigned i = 0; i < 6; i++) cur[i] = (uint8_t)(rn(4) - 2);
    uint8_t *item = LINN(blk);
    for (unsigned i = 0; i < 8; i++) item[i] = (uint8_t)(rn(8) - 4);
    DS_B(0x5a9d) = (uint8_t)rn(6);
    DIFF(2, 0x587a, FAR_CDECL, blk, MEM_SEL_DGROUP);
  }
}

/* ---- 2:0x50f8: may the player use item (kind, index)? ---- */

static void run_50f8(uint16_t kind, uint16_t idxw, uint16_t c14) {
  req(kind, idxw);
  DS_W(0x14c) = c14;
  DIFF(2, 0x50f8, FAR_CDECL, REQ);
}

static void t_50f8(void) {
  static const int8_t dis[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -128, 127, 10};
  /* random world for every (kind, index): the player rows, the race value, the traits and the mask random */
  for (unsigned k = 0; k < NKINDS; k++) {
    for (unsigned idx = 0; idx < (unsigned)kinds[k].n + 2; idx++) {
      for (unsigned c = 0; c < sizeof c14s / 2; c++) {
        for (unsigned n = 0; n < 3; n++) {
          g_rng = 0x50f80001u + 0x9e3779b9u * (k * 4096 + idx * 64 + c * 8 + n + 1);
          world(dis[rn(sizeof dis)], 2, 2);
          uint16_t hi = (uint16_t)(rnd() & 0xff00); /* only the low byte of the index word counts */
          run_50f8(kinds[k].kind, (uint16_t)(hi | idx), c14s[c]);
        }
      }
    }
  }
  /* the race value 0..9 against random worlds whose trait dword and mask word are mostly all-zero or
   * all-one, so every comparison of the race value and every trait / mask test is taken both ways */
  for (unsigned k = 0; k < NKINDS; k++) {
    for (unsigned idx = 0; idx < kinds[k].n; idx++) {
      for (int8_t di = 0; di <= 9; di++) {
        for (unsigned v = 0; v < 2; v++) {
          g_rng = 0x50f80002u + 0x9e3779b9u * (k * 4096 + idx * 128 + (unsigned)di * 4 + v + 1);
          unsigned tp = rn(5) > 2 ? 2 : rn(2), mp = rn(5) > 2 ? 2 : rn(2);
          if (v == 0) tp = (unsigned)(idx + di) & 1, mp = (unsigned)(idx + di) >> 1 & 1;
          world(di, tp, mp);
          run_50f8(kinds[k].kind, (uint16_t)idx, 0);
        }
      }
    }
  }
  /* indexes above the table: the check answers 0 whatever the world is; byte values high in the byte */
  for (unsigned k = 0; k < NKINDS; k++) {
    static const uint16_t big[] = {0x80, 0xff, 0x7f, 0x40};
    for (unsigned b = 0; b < 4; b++) {
      g_rng = 0x50f80003u + k * 16 + b;
      world(2, 2, 2);
      run_50f8(kinds[k].kind, big[b], 0);
    }
  }
  /* words that are not a kind */
  for (unsigned b = 0; b < sizeof bad_kinds / 2; b++) {
    for (unsigned idx = 0; idx < 4; idx++) {
      g_rng = 0x50f80004u + b * 16 + idx;
      world(2, 2, 2);
      run_50f8(bad_kinds[b], (uint16_t)idx, 0);
    }
  }
}

/* ---- 2:0x50d8: set the kind and index of a request, then run the check ---- */

static void t_50d8(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x50d80001u + 0x9e3779b9u * (it + 1);
    world((int8_t)rn(11) - 1, 2, 2);
    DS_W(0x14c) = c14s[rn(4)];
    uint16_t kind = rn(8) == 0 ? bad_kinds[rn(sizeof bad_kinds / 2)] : kinds[rn(NKINDS)].kind;
    uint16_t idx = (uint16_t)rn(0x24);
    /* the old contents of the request are random, so the clearing of byte +3 and the replacement of
     * byte +2 can be seen; the argument word has random high bits (the argument is a byte) */
    req((uint16_t)rnd(), (uint16_t)rnd());
    DIFF(2, 0x50d8, FAR_CDECL, REQ, kind, (uint16_t)((rnd() & 0xff00) | idx));
  }
}

/* ---- 2:0x5842: kind 0x8000, from index 8 down to the first item that answers 1 ---- */

static void t_5842(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x58420001u + 0x9e3779b9u * (it + 1);
    world((int8_t)rn(10), 2, 2);
    uint16_t c14 = c14s[rn(4)];
    DS_W(0x14c) = c14;
    /* tech levels of four kinds: all 127 (index 8 is open at once unless a rule forbids it), small
     * ones (the first open index is somewhere in the middle), a few negative ones (none open) and a
     * wide mix, so that the early exit is taken at different indexes and the loop also runs to 0 */
    uint8_t *cur = LINN((uint16_t)(0x5a7e + 0xc0 * c14));
    for (unsigned i = 0; i < 6; i++) {
      switch (it % 4) {
        case 0: cur[i] = 127; break;
        case 1: cur[i] = (uint8_t)rn(30); break;
        case 2: cur[i] = (uint8_t)((int)rn(12) - 4); break;
        default: cur[i] = (uint8_t)((int)rn(40) - 8); break;
      }
    }
    DS_B((uint16_t)(0x5a9d + 0xc0 * c14)) = (uint8_t)rn(16);
    req((uint16_t)rnd(), (uint16_t)rnd());
    DIFF(2, 0x5842, FAR_CDECL, REQ);
  }
}

int main(void) {
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  /* 2:0x0000, FAR_CDECL, one int16 argument: index * 0x8f + 0x5c3, returned as the far pointer
   * cs:offset (DX = CS). Boundaries: 0, 1, the largest positive and most negative index (the
   * product wraps in 16 bits), and -1. */
  DIFF(2, 0x0000, FAR_CDECL, 0);
  DIFF(2, 0x0000, FAR_CDECL, 1);
  DIFF(2, 0x0000, FAR_CDECL, 7);
  DIFF(2, 0x0000, FAR_CDECL, 0xFFFF);
  DIFF(2, 0x0000, FAR_CDECL, 0x7FFF);
  DIFF(2, 0x0000, FAR_CDECL, 0x8000);
  DIFF_FUZZ(2, 0x0000, FAR_CDECL, 1, 2000, 0x5EED0002u);
  g_only = getenv("SEG02_ONLY");
  if (want("acc")) t_accessors();
  if (want("587a")) t_587a();
  if (want("50f8")) t_50f8();
  if (want("50d8")) t_50d8();
  if (want("5842")) t_5842();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
