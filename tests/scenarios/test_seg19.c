/* Diff tests for decompiled segment 19 (src/seg19.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 19 is the code of the computer players (the "AI" turn):
 *   19:0x0000  clear the order flags of the computer player's fleets and queue their new orders
 *   19:0x03a0  does a fleet hold enough of ship designs 9 to 12 (sums of its counts at +0x1e..+0x24)
 *   19:0x0400  the computer player's main turn: refresh the scores, walk the planets and the fleets,
 *              queue orders, then walk the fleets once more to handle the leftovers
 *   19:0x12f0  queue the messages of the techs the computer player has reached
 *   19:0x1594  the older, shorter variant of 19:0x0400 (scores, one fleet walk, leftovers)
 *   19:0x212e  a bare RETF
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered API call log, which here holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of
 * the callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See stub_handler for what a double does:
 * it returns the next queued value (a queue per callee, kept in the stub block in the arena, so it
 * is rewound by the snapshot between the two runs), optionally writes 16 queued bytes to the buffer
 * a pointer argument names (the out-buffers of 18:0x3558 and 18:0x3600), and hashes, at every call,
 * the state the call could observe: the fleet record the call names, the info array, the cursor
 * struct and the DGROUP words 19:0x0400 writes between calls. The hashes are compared as arena
 * memory, so a call made before or after a write it should follow (or precede) shows as a difference.
 * Run as ORIGINAL code in both runs (no double): 8:0x8730 (the random generator), 7:0x01e4 (find a
 * planet by id), 7:0x3b60 (copy a design's cost words), 28:0x21ee and 28:0x41d4 (player attributes),
 * and 19:0x03a0 and 19:0x12f0 (decompiled here, called by 19:0x0400 / 19:0x1594), and 16:0x175c and
 * 16:0x16f6 (decompiled in src/seg16.c, called by 19:0x0000: the ORIGINAL code in one run, the C functions in the
 * other; they read the design table DS:0xBE + 4 * owner of the fleet's owner, see designs_for). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/kernel_thunks.h"
#include "runtime/kernel.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL19 MEM_SEL_SEG(19)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }
static uint8_t rb_(uint32_t fp, uint16_t off) { return *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))); }
static uint16_t rw(uint32_t fp, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 2);
  return v;
}
static void ww(uint32_t fp, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 2); }
static void wd(uint32_t fp, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 4); }
static void wb(uint32_t fp, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))) = v; }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }
/* true with probability num/den */
static bool chance(uint32_t num, uint32_t den) { return rn(den) < num; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}

/* ---- the test doubles ---- */

#define STUB_BLKSZ 0x8000
#define STUB_DEF 0x10     /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200   /* queue length of id (word) */
#define STUB_QIDX 0x280   /* queue index of id (word) */
#define STUB_Q 0x400      /* queue of id: 32 entries of 20 bytes (ax, dx, 16 poke bytes) */
#define STUB_QENT 20
#define STUB_QMAX 32
#define STUB_LOG0 0x6000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs;     /* argument words */
  int8_t farw;       /* argument word index of a far pointer to a fleet record whose 0x7c bytes are hashed, -1 none */
  int8_t poke;       /* argument word index of a near pointer (SS) the queued 16 bytes are written to, -1 none */
  int8_t ssw;        /* argument word index of a near pointer (SS) whose ssn bytes are hashed, -1 none */
  uint8_t ssn;
  uint8_t pokelen;   /* bytes a queued poke writes */
} StubCfg;

enum {
  S_28FC, S_36AA, S_5D3C, S_5BAE, S_462A, S_474C, S_2A74, S_0100, S_2732, S_4DE8, S_0810, S_3558, S_3600,
  S_2DB2, S_2E90, S_153E, S_3BB4, S_3C64, S_39F6, S_2664, S_19C4, S_4510, S_098A, S_0864, S_08FE, S_0DEE,
  S_0750, S_2E4E, S_0630, S_0000, S_59BA, S_1E2A, S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    [S_28FC] = {"18:28fc", 18, 0x28fc, 5, -1, -1, -1, 0, 0},
    [S_36AA] = {"18:36aa", 18, 0x36aa, 1, -1, -1, -1, 0, 0},
    [S_5D3C] = {"18:5d3c", 18, 0x5d3c, 5, -1, 3, -1, 0, 2},
    [S_5BAE] = {"18:5bae", 18, 0x5bae, 1, -1, -1, -1, 0, 0},
    [S_462A] = {"9:462a", 9, 0x462a, 3, 0, -1, -1, 0, 0},
    [S_474C] = {"9:474c", 9, 0x474c, 3, 0, -1, -1, 0, 0},
    [S_2A74] = {"9:2a74", 9, 0x2a74, 2, -1, -1, -1, 0, 0},
    /* Rule j2 justification: 26:0x0100 and 26:0x0810 are decompiled (src/seg26.c, diffed in tests/scenarios/test_seg26.c) but stays a double here. These
     * scenarios build random worlds for the computer players' turn; the real function reads the planet copy at DS:0x4c08, its
     * production queue and the player tables, which no scenario here sets up consistently. Measured with the double removed:
     * the ORIGINAL run faults or the heap blocks the function allocates differ between the runs. Blind spot: only the call
     * (arguments and order against the other calls) is checked here, not what the function does. */
    [S_0100] = {"26:0100", 26, 0x0100, 1, -1, -1, -1, 0, 0},
    [S_2732] = {"18:2732", 18, 0x2732, 4, -1, -1, -1, 0, 0},
    [S_4DE8] = {"9:4de8", 9, 0x4de8, 3, 0, -1, -1, 0, 0},
    [S_0810] = {"26:0810", 26, 0x0810, 1, -1, -1, -1, 0, 0},
    [S_3558] = {"18:3558", 18, 0x3558, 3, 0, 2, -1, 0, 16},
    [S_3600] = {"18:3600", 18, 0x3600, 3, 0, 2, -1, 0, 16},
    [S_2DB2] = {"18:2db2", 18, 0x2db2, 2, 0, -1, -1, 0, 0},
    [S_2E90] = {"18:2e90", 18, 0x2e90, 2, 0, -1, -1, 0, 0},
    [S_153E] = {"7:153e", 7, 0x153e, 2, -1, -1, -1, 0, 0},
    [S_3BB4] = {"18:3bb4", 18, 0x3bb4, 3, 0, -1, -1, 0, 0},
    [S_3C64] = {"18:3c64", 18, 0x3c64, 8, 0, -1, -1, 0, 0},
    [S_39F6] = {"18:39f6", 18, 0x39f6, 4, 0, -1, -1, 0, 0},
    [S_2664] = {"18:2664", 18, 0x2664, 4, 0, -1, 2, 18, 0},
    [S_19C4] = {"18:19c4", 18, 0x19c4, 4, 0, -1, -1, 0, 0},
    [S_4510] = {"18:4510", 18, 0x4510, 3, 0, -1, -1, 0, 0},
    [S_098A] = {"18:098a", 18, 0x098a, 3, 0, 2, -1, 0, 4},
    [S_0864] = {"18:0864", 18, 0x0864, 3, 0, -1, -1, 0, 0},
    [S_08FE] = {"18:08fe", 18, 0x08fe, 4, 0, -1, -1, 0, 0},
    [S_0DEE] = {"18:0dee", 18, 0x0dee, 3, 0, 2, -1, 0, 4},
    [S_0750] = {"18:0750", 18, 0x0750, 6, -1, -1, -1, 0, 0},
    [S_2E4E] = {"18:2e4e", 18, 0x2e4e, 2, 0, -1, -1, 0, 0},
    [S_0630] = {"18:0630", 18, 0x0630, 2, -1, -1, 0, 0x8d, 0},
    [S_0000] = {"18:0000", 18, 0x0000, 4, -1, -1, -1, 0, 0},
    [S_59BA] = {"18:59ba", 18, 0x59ba, 5, -1, -1, -1, 0, 0},
    [S_1E2A] = {"21:1e2a", 21, 0x1e2a, 0, -1, -1, -1, 0, 0},
};

/* The state the doubles hash at every call, besides the fleet record they name. */
static uint32_t g_w_info;   /* far pointer to the info array (DS:0x1600), nfleet * 16 bytes */
static uint16_t g_w_ninfo;
static uint32_t g_w_cur;    /* far pointer to the cursor struct (DS:0x4bf0), 0x20 bytes */

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  uint32_t h = 2166136261u;
  if (cfg->farw >= 0) {
    uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
    if (fp != 0 && mem_check(fp, 0x7c)) h = fnv(h, LIN(fp), 0x7c);
  }
  if (cfg->ssw >= 0 && cfg->ssn) {
    uint16_t p = hcall_word(c, (unsigned)cfg->ssw);
    h = fnv(h, LIN(MK_FP(c->regs->ss, p)), cfg->ssn);
  }
  if (g_w_info) h = fnv(h, LIN(g_w_info), (size_t)g_w_ninfo * 16);
  if (g_w_cur) h = fnv(h, LIN(g_w_cur), 0x20);
  h = fnv(h, LINN(0x255c), 2);
  h = fnv(h, LINN(0x4bee), 2);
  h = fnv(h, LINN(0x5340), 4);
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
  if (qidx < qlen) {
    const uint8_t *e = blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * STUB_QENT + (size_t)qidx * STUB_QENT;
    c->ret = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
    if (cfg->poke >= 0) {
      uint16_t p = hcall_word(c, (unsigned)cfg->poke);
      if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), e + 4, cfg->pokelen);
    }
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
}
static void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
static void stub_clear(unsigned id) {
  uint8_t *blk = LIN(g_stub_blk);
  memset(blk + STUB_QLEN + 2 * id, 0, 2);
  memset(blk + STUB_QIDX + 2 * id, 0, 2);
  stub_def(id, 0, 0);
}
static void stubs_clear_all(void) {
  for (unsigned i = 0; i < S_NSTUB; i++) stub_clear(i);
  uint16_t at = STUB_LOG0;
  memcpy(LIN(g_stub_blk), &at, 2);
}
/* Queues one return (dx:ax) and, for a poking double, 16 bytes written to the out-buffer. */
static void stub_push(unsigned id, uint16_t ax, uint16_t dx, const void *poke) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t n;
  memcpy(&n, blk + STUB_QLEN + 2 * id, 2);
  CHECK(n < STUB_QMAX);
  if (n >= STUB_QMAX) return;
  uint8_t *e = blk + STUB_Q + (size_t)id * STUB_QMAX * STUB_QENT + (size_t)n * STUB_QENT;
  e[0] = (uint8_t)ax;
  e[1] = (uint8_t)(ax >> 8);
  e[2] = (uint8_t)dx;
  e[3] = (uint8_t)(dx >> 8);
  if (poke) memcpy(e + 4, poke, 16);
  else memset(e + 4, 0, 16);
  n++;
  memcpy(blk + STUB_QLEN + 2 * id, &n, 2);
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
  }
  stubs_clear_all();
}


/* ---- the scenario world: fleets, planets and the tables that point at them ---- */

#define NFL_MAX 8
static uint32_t g_ptrs;       /* DS:0xba: the fleet pointer array */
static uint32_t g_fblk[3];    /* blocks that hold the fleet records */
static uint32_t g_info;       /* DS:0x1600: 16 bytes per fleet id */
static uint32_t g_cur;        /* DS:0x4bf0: the cursor struct */
static uint32_t g_fleet[NFL_MAX];

static void set_far(uint16_t off, uint32_t fp) { ds_w32(off, fp); }


/* Random bytes into a block. */
static void fill_rand(uint32_t fp, uint16_t n) {
  uint8_t *p = LIN(fp);
  for (uint16_t i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

/* The design table of player `me` (DS:0xBE + 4 * me): 16 designs of 0x8d bytes whose hull byte (+0) is small,
 * read by 16:0x175c (hulls 4 to 6) and 16:0x16f6 (hulls 14 and 15). */
static uint32_t g_dsg;
static void designs_for(uint16_t me) {
  if (!g_dsg) g_dsg = gblock(0x8d0);
  fill_rand(g_dsg, 0x8d0);
  for (unsigned i = 0; i < 16; i++) wb(g_dsg, (uint16_t)(0x8d * i), (uint8_t)rn(chance(1, 2) ? 16 : 40));
  ds_w32((uint16_t)(0xbe + 4 * me), g_dsg);
}

/* Lays out nfl fleets: records spread over the three blocks at varied offsets, pointer array, count. */
static void world_fleets(unsigned nfl) {
  CHECK(nfl <= NFL_MAX);
  for (unsigned i = 0; i < NFL_MAX; i++) {
    g_fleet[i] = MK_FP(HI(g_fblk[i % 3]), (uint16_t)(0x10 + 0x80 * (i / 3)));
    memset(LIN(g_fleet[i]), 0, 0x7c);
    memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * i))), &g_fleet[i], 4);
  }
  set_far(0x00ba, g_ptrs);
  DS_W(0x553a) = (uint16_t)nfl;
  set_far(0x1600, g_info);
  set_far(0x4bf0, g_cur);
  g_w_info = g_info;
  g_w_ninfo = 64;
  g_w_cur = g_cur;
  fill_rand(g_info, 64 * 16);
  for (unsigned i = 0; i < 64; i++) {
    wb(g_info, (uint16_t)(16 * i + 3), (uint8_t)(chance(1, 2) ? 0 : rn(3)));
    wb(g_info, (uint16_t)(16 * i + 1), (uint8_t)(rn(8)));
  }
  fill_rand(g_cur, 0x40);
  ww(g_cur, 8, (uint16_t)rn(13));
}

/* ---- 19:0x212e: a bare RETF ---- */

static void t_212e(void) {
  DIFF(19, 0x212e, FAR_CDECL);
  DIFF(19, 0x212e, FAR_CDECL, 0x1234);
}

/* ---- 19:0x03a0: does the fleet hold enough of the designs 9 to 12 ---- */

static uint32_t g_fl;   /* a scratch fleet record block */
static void t_03a0(void) {
  static const uint16_t w[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff, 0x4000, 0x00fe, 0x0100, 0xff00};
  static const int16_t modes[] = {-32768, 1, 2, 3, 32767};
  static const uint8_t thr[] = {0, 1, 5, 0x80, 0xff};
  uint32_t fl = g_fl;
  for (unsigned t = 0; t < sizeof thr; t++) {
    DS_B(0x5340) = thr[t];
    for (unsigned k = 0; k < 30; k++) {
      uint16_t a = w[rn(sizeof w / 2)], b = w[rn(sizeof w / 2)], c = w[rn(sizeof w / 2)], d = w[rn(sizeof w / 2)];
      if (k < 8) a = (uint16_t)(k & 1), b = (uint16_t)((k >> 1) & 1), c = (uint16_t)((k >> 2) & 1), d = 0;
      ww(fl, 0x1e, a);
      ww(fl, 0x20, b);
      ww(fl, 0x22, c);
      ww(fl, 0x24, d);
      for (unsigned m = 0; m < sizeof modes / 2; m++)
        DIFF(19, 0x03a0, FAR_CDECL, LO(fl), HI(fl), (uint16_t)modes[m]);
    }
  }
}

/* ---- 19:0x12f0: queue the messages of the techs the computer player has reached ---- */

static void t_12f0(void) {
  static const uint16_t flagw[] = {0x442a, 0x44b7, 0x49ac, 0x4283, 0x41f6, 0x4544, 0x4310, 0x439d, 0x46eb};
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x12f00000u + it * 2654435761u;
    if (!g_rng) g_rng = 1;
    DS_W(0x14c) = (uint16_t)rn(16);
    for (unsigned i = 0; i < sizeof flagw / 2; i++) DS_B(flagw[i]) = (uint8_t)(rnd() & (it % 7 == 0 ? 0xff : 0xfe)) | (uint8_t)(chance(3, 4) ? 2 : 0);
    ds_w32(0x428a, chance(1, 2) ? 0 : rnd());
    ds_w32(0x41fd, chance(1, 2) ? 0 : rnd());
    uint8_t *pl = (uint8_t *)LINN((uint16_t)(0x5a64 + 0xc0 * DS_W(0x14c)));
    for (unsigned k = 0x1b; k < 0x20; k++) pl[k] = (uint8_t)(it % 11 == 0 ? 0x7f : chance(1, 12) ? 0xf0 + rn(16) : rn(20));
    ds_w32(0x151e, rnd());
    ds_w32(0x1522, rnd());
    stubs_clear_all();
    for (unsigned k = 0; k < 16; k++) stub_push(S_0000, (uint16_t)(chance(1, 3) ? 1 : 0), 0, NULL);
    DIFF(19, 0x12f0, FAR_CDECL);
  }
}


/* ---- 19:0x0000: queue the orders of the computer player's fleets ---- */

static void t_0000(void) {
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x00000000u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned nfl = it % 7;
    world_fleets(nfl);
    uint16_t me = (uint16_t)(3 + rn(2));
    DS_W(0x14c) = me;
    designs_for(me);
    DS_W(0x82) = (uint16_t)(chance(1, 2) ? 0x10 : 0x14 + rn(3));
    DS_W(0x255c) = 0x1234;
    DS_W(0x4c0a) = chance(2, 3) ? me : (uint16_t)(me + 1);
    ds_w32(0x4c30, chance(1, 3) ? 0x32 : chance(1, 2) ? 0x31 : (uint32_t)(int32_t)-5 + rn(300));
    DS_W(0x4b92) = chance(1, 2) ? 0xffff : 5;
    DS_B(0x4c0d) = (uint8_t)(chance(1, 2) ? 2 : rn(2));
    for (unsigned i = 0; i < nfl; i++) {
      uint32_t f = g_fleet[i];
      fill_rand(f, 0x7c);
      ww(f, 0, (uint16_t)(chance(1, 8) ? 20 : rn(16)));
      ww(f, 2, chance(3, 4) ? me : (uint16_t)(me + 1));
      ww(f, 6, chance(1, 2) ? 0xffff : (uint16_t)rn(30));
      ww(f, 0x62, (uint16_t)(chance(3, 4) ? rn(2) : 0xffff - rn(3) + 3 * rn(2)));
      wd(f, 0x5c, chance(1, 3) ? 0 : chance(1, 2) ? 1 : rnd());
    }
    if (nfl && chance(1, 8)) memset(LIN(g_fleet[nfl - 1]), 0, 4); /* ends the walk at a null entry */
    if (nfl > 1 && chance(1, 10)) memset(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * (nfl - 1)))), 0, 4);
    for (unsigned k = 0; k < 8; k++) {
      uint8_t out[16] = {0};
      uint32_t outp = chance(1, 3) ? 0 : g_fleet[rn(NFL_MAX)];
      memcpy(out, &outp, 4);
      stub_push(S_0DEE, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, out);
      {
      uint8_t pk[16] = {0};
      uint32_t pp = chance(1, 2) ? 0 : g_fleet[rn(NFL_MAX)];
      memcpy(pk, &pp, 4);
      stub_push(S_098A, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, pk);
    }
      stub_push(S_4510, (uint16_t)rn(2), 0, NULL);
      stub_push(S_28FC, (uint16_t)rnd(), 0, NULL);
    }
    DIFF(19, 0x0000, FAR_CDECL, (uint16_t)rnd());
  }
}

/* ---- the scenario world for the two big functions ---- */

#define NPL_MAX 8
static uint32_t g_pln;    /* the planet table block */
static uint32_t g_lst;    /* DS:0xcd2: the list struct */
static uint32_t g_tab;    /* DS:0x1604: the table struct */
static uint32_t g_pl[NPL_MAX];
static uint16_t g_plid[NPL_MAX];

/* The stack slot BP+off of the function under test, for a FAR function with nargs argument words and an
 * ENTER frame: the harness starts the stack at harness_stack_top(). */
static void seed_stack32(unsigned nargs, int off, uint32_t v) {
  uint16_t bp = (uint16_t)(harness_stack_top() - 2 * nargs - 4 - 2);
  memcpy(LINN((uint16_t)(bp + off)), &v, 4);
}

static void world_planets(unsigned npl, uint16_t me, bool sparse) {
  CHECK(npl <= NPL_MAX);
  for (unsigned i = 0; i < NPL_MAX; i++) {
    g_pl[i] = MK_FP(HI(g_pln), (uint16_t)(0x10 + 0x38 * i));
    memset(LIN(g_pl[i]), 0, 0x38);
  }
  set_far(0x00b6, MK_FP(HI(g_pln), 0x10));
  DS_W(0x4d96) = (uint16_t)npl;
  DS_W(0x007a) = (uint16_t)npl;
  uint16_t id = 0;
  for (unsigned i = 0; i < npl; i++) {
    uint32_t pf = g_pl[i];
    fill_rand(pf, 0x38);
    id = (uint16_t)(id + 1 + (sparse ? rn(6) : 0));
    g_plid[i] = sparse ? id : (uint16_t)i;
    ww(pf, 0, g_plid[i]);
    ww(pf, 2, chance(1, 3) ? 0xffff : chance(2, 3) ? me : (uint16_t)(me + 1 + rn(2)));
    wb(pf, 4, (uint8_t)(chance(1, 3) ? rn(3) : 3 + rn(4)));
    wb(pf, 5, (uint8_t)(rnd() & (chance(1, 2) ? 0xff : 0xfd)));
    wb(pf, 9, (uint8_t)(chance(1, 3) ? 0x30 + rn(0x20) : 0x40 + rn(8)));
    wb(pf, 10, (uint8_t)(chance(1, 3) ? rn(0x50) : rn(0x100)));
    wb(pf, 11, (uint8_t)rn(0x100));
    wd(pf, 0x28, chance(1, 3) ? (uint32_t)(0x3e8 + rn(3) - 1) : chance(1, 3) ? 0x5dc + rn(3) - 1 : rnd());
  }
  if (sparse && npl) {
    uint16_t m = (uint16_t)(g_plid[npl - 1] + 1 + rn(2));
    if (chance(1, 2)) {
      uint16_t q = chance(1, 2) ? 12 : 24;
      m = (uint16_t)(((m + q - 1) / q) * q + (chance(1, 3) ? q : 0)); /* a multiple of the divisors 19:0x0400 uses */
    }
    DS_W(0x007a) = m;
  }
}

/* A list struct at DS:0xcd2: a count byte at +3 and entries of two words from +4. */
static void world_list(void) {
  uint32_t l = g_lst;
  uint8_t *p = LIN(l);
  memset(p, 0, 0x40);
  set_far(0x0cd2, l);
  uint8_t n = (uint8_t)(chance(1, 5) ? 0 : 1 + rn(4));
  p[3] = n;
  for (unsigned k = 0; k < n; k++) {
    uint16_t w2 = chance(1, 2) ? 4 : (uint16_t)rn(16);       /* (w2 & 0xe) == 4 selects */
    static const uint32_t pick[] = {0x4000, 0x3c00, 0x4400, 0x4000, 0x0000, 0x1fc00, 0x10000, 0x7c00};
    uint32_t d = chance(1, 2) ? pick[rn(8)] : (0x4000u + rn(0x40000));
    d = (d & ~0x3ffu) | (chance(1, 2) ? 0 : rn(0x400));
    wd(l, (uint16_t)(4 + 4 * k), (d & 0xffffu) | ((uint32_t)w2 << 16));
  }
}

/* The table struct at DS:0x1604: count at +2, entries of 0x14 bytes from +4 (id, n, n fleet ids). */
static void world_table(unsigned npl) {
  uint32_t t = g_tab;
  uint8_t *p = LIN(t);
  memset(p, 0, 0x100);
  set_far(0x1604, t);
  uint16_t n = (uint16_t)(chance(1, 4) ? 0 : 1 + rn(3));
  ww(t, 2, n);
  for (unsigned k = 0; k < n; k++) {
    uint16_t base = (uint16_t)(4 + 0x14 * k);
    ww(t, base, (npl && chance(3, 4)) ? g_plid[rn(npl)] : (uint16_t)0xffff); /* the id handed to 7:0x01e4 */
    uint16_t m = (uint16_t)(chance(1, 4) ? 0 : 1 + rn(4));
    ww(t, (uint16_t)(base + 2), m);
    for (unsigned j = 0; j < m; j++) ww(t, (uint16_t)(base + 4 + 2 * j), (uint16_t)(chance(2, 3) ? rn(16) : 0xffff));
  }
}

static void world_ds(uint16_t me) {
  {
    uint8_t *xy = LINN(0x31ba);
    for (unsigned i = 0; i < 0x100; i++) xy[i] = (uint8_t)rnd();
    for (unsigned p = 0; p < 16; p++) {
      uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
      for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
      row[0x4c] = (uint8_t)(chance(1, 3) ? 8 : rn(16));
    }
  }
  DS_W(0x14c) = me;
  designs_for(me);
  {
    static const uint16_t edge[] = {0, 1, 9, 0xa, 0xb, 0x13, 0x14, 0x15, 0x1d, 0x1e, 0x1f, 0x31, 0x32, 0x33, 0x3b, 0x3c, 0x3d,
                                    0x63, 0x64, 0x65, 0x72, 0x73, 0x74, 0x77, 0x78, 0x79, 0x81, 0x82, 0x83, 0xc7, 0xc8, 0xc9, 0x424, 0x425};
    DS_W(0x82) = chance(1, 3) ? edge[rn(sizeof edge / 2)] : (uint16_t)(chance(1, 6) ? 0 : chance(1, 9) ? 1000 + rn(60000) : chance(1, 3) ? rn(0x20) : 0x14 + rn(0x100));
  }
  DS_B(0x80) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
  DS_W(0x255c) = 0x1234;
  DS_W(0x4bee) = 0;
  DS_W(0x4c0a) = chance(2, 3) ? me : (uint16_t)(me + 1);
  ds_w32(0x4c30, chance(1, 3) ? 0x32 : chance(1, 2) ? 0xc8 : (uint32_t)(int32_t)-5 + rn(400));
  DS_W(0x4b92) = chance(1, 2) ? 0xffff : 5;
  DS_B(0x4c0d) = (uint8_t)(chance(1, 2) ? 2 : rn(2));
  DS_B(0x4243) = (uint8_t)rn(4);
  DS_B(0x41b6) = (uint8_t)(chance(1, 2) ? 1 : chance(1, 2) ? 0xa : rn(0x20));
  ds_w32(0x428a, chance(1, 3) ? 0 : chance(1, 2) ? rn(4) : rnd());
  DS_B(0x4544) = (uint8_t)(chance(1, 2) ? 2 : 0);
  ds_w32(0x151e, rnd());
  ds_w32(0x1522, rnd());
  set_far(0x00ba, g_ptrs);
}

/* Random fleet records. The player's fleets get small counts so that the checks of 19:0x03a0 and the
 * order records vary. */
static void world_rand_fleets(unsigned nfl, uint16_t me, unsigned npl) {
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    fill_rand(f, 0x7c);
    ww(f, 0, (uint16_t)(chance(1, 8) ? 20 : rn(16)));
    ww(f, 2, chance(3, 5) ? me : chance(1, 5) ? 0xffff : (uint16_t)(me + 1 + rn(3)));
    ww(f, 6, (chance(1, 3) || !npl) ? 0xffff : g_plid[rn(npl)]);
    ww(f, 8, rn(2));
    ww(f, 0xa, rn(2));
    ww(f, 0xe, chance(1, 3) ? 0 : (uint16_t)rn(5));
    ww(f, 0x1a, chance(1, 2) ? 0 : (uint16_t)rn(5));
    ww(f, 0x1c, chance(1, 2) ? 0 : (uint16_t)rn(5));
    ww(f, 0x62, (uint16_t)(chance(1, 5) ? 0xffff : rn(4)));
    ww(f, 0x26, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x28, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0xc, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x10, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x12, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x1e, (uint16_t)rn(4));
    ww(f, 0x20, (uint16_t)rn(4));
    wd(f, 0x58, chance(1, 3) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-1 : rnd() & 0xffff);
    wd(f, 0x5c, chance(1, 3) ? 0 : chance(1, 2) ? 1 + rn(3) : rnd());
    wd(f, 0x28, chance(1, 3) ? 0x3e8 + rn(3) - 1 : chance(1, 3) ? 0x5dc + rn(3) - 1 : rnd());
    ww(f, 0x18, (uint16_t)(chance(1, 3) ? 0 : chance(1, 3) ? 0xa + rn(8) : rn(0x14)));
    wb(f, 5, (uint8_t)(rnd()));
    if (chance(2, 5)) {
      /* an idle fleet: no orders pending */
      ww(f, 0xe, 0);
      ww(f, 0x1a, 0);
      ww(f, 0x1c, 0);
      ww(f, 0x62, (uint16_t)rn(2));
      if (chance(1, 2)) ww(f, 0x26, 0), ww(f, 0x28, 0);
    }
    if (chance(1, 3) && (int16_t)rw(f, 0x62) < 1) ww(f, 0x64, 0), ww(f, 0x66, 0);
    else {
      /* the waypoint list: a record of 0x20 bytes somewhere in a fleet block */
      uint32_t w = MK_FP(HI(g_fblk[rn(3)]), (uint16_t)(0x300 + 0x20 * rn(4)));
      fill_rand(w, 0x20);
      wb(w, 0x1d, (uint8_t)(chance(1, 2) ? 1 : rn(16)));
      wb(w, 0xa, (uint8_t)(chance(1, 2) ? 0 : rn(256)));
      ww(w, 0x1a, npl ? g_plid[rn(npl)] : (uint16_t)0);
      wd(f, 0x64, w);
    }
    wd(f, 0x68, 0);
  }
  if (nfl && chance(1, 10)) memset(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * (nfl - 1)))), 0, 4);
}

/* Overwrites the AX of the idx-th queued return of a double. */
static void stub_set_ax(unsigned id, unsigned idx, uint16_t ax) {
  uint8_t *e = (uint8_t *)LIN(g_stub_blk) + STUB_Q + (size_t)id * STUB_QMAX * STUB_QENT + (size_t)idx * STUB_QENT;
  e[0] = (uint8_t)ax;
  e[1] = (uint8_t)(ax >> 8);
}

/* Random returns for the doubles: queues of 24 for each. */
static void stubs_random(void) {
  uint8_t poke[16];
  for (unsigned k = 0; k < 24; k++) {
    stub_push(S_28FC, (uint16_t)rnd(), 0, NULL);
    stub_push(S_462A, (uint16_t)(chance(1, 3) ? 0 : chance(1, 3) ? 0xffff : 1), 0, NULL);
    stub_push(S_474C, (uint16_t)(chance(1, 3) ? 0 : chance(1, 2) ? 0xffff : 1), 0, NULL);
    stub_push(S_2E90, (uint16_t)rn(2), 0, NULL);
    stub_push(S_2DB2, (uint16_t)rn(2), 0, NULL);
    stub_push(S_2E4E, (uint16_t)rn(2), 0, NULL);
    stub_push(S_4510, (uint16_t)rn(2), 0, NULL);
    {
      uint8_t pk[16] = {0};
      uint32_t pp = chance(1, 2) ? 0 : g_fleet[rn(NFL_MAX)];
      memcpy(pk, &pp, 4);
      stub_push(S_098A, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, pk);
    }
    memset(poke, 0, sizeof poke);
    uint32_t outp = chance(1, 3) ? 0 : g_fleet[rn(NFL_MAX)];
    memcpy(poke, &outp, 4);
    stub_push(S_0DEE, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, poke);
    {
      uint32_t fp = chance(2, 3) ? g_fleet[rn(NFL_MAX)] : 0;
      stub_push(S_39F6, LO(fp), HI(fp), NULL);
    }
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(2000);
      memcpy(poke + 4 * j, &v, 4);
    }
    stub_push(S_3558, 0, 0, poke);
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(2000);
      memcpy(poke + 4 * j, &v, 4);
    }
    stub_push(S_3600, 0, 0, poke);
    memset(poke, 0, sizeof poke);
    uint16_t o = chance(1, 3) ? 0xffff : (uint16_t)rn(16);
    memcpy(poke, &o, 2);
    stub_push(S_5D3C, (uint16_t)(chance(1, 2) ? rn(3000) : rn(10)), 0, poke);
  }
}

/* A scenario aimed at the order record 19:0x1594 builds for another player's planet: the first own fleet
 * has no pending order, 18:0x2e90 says it can act, the planet it sits at belongs to another player and
 * the slot byte +3 is set. */
static void focus_1594(uint16_t me, unsigned npl, unsigned nfl) {
  if (!npl || !nfl) return;
  uint32_t f = g_fleet[0];
  ww(f, 2, me);
  ww(f, 6, g_plid[0]);
  ww(f, 0x1a, 0);
  ww(f, 0x1c, 0);
  ww(f, 0x62, 0);
  ww(f, 0xe, 0);
  wd(f, 0x58, 1 + rn(9));
  ww(g_pl[0], 2, (uint16_t)(me + 1));
  wb(g_info, (uint16_t)(16 * g_plid[0] + 3), 1);
  stub_set_ax(S_2E90, 0, 1);
  if (chance(2, 3)) ww(g_cur, 8, g_plid[0]);
}

/* The other idle-fleet path of 19:0x1594: a fleet with a pending value at +0xe, a valid planet, and the
 * cursor threshold DS:0x4c30 on either side of 0x32. */
static void focus_1594b(uint16_t me, unsigned npl, unsigned nfl) {
  if (!npl || !nfl) return;
  uint32_t f = g_fleet[0];
  ww(f, 2, me);
  ww(f, 6, g_plid[0]);
  ww(f, 0x1a, 0);
  ww(f, 0x1c, 0);
  ww(f, 0x62, (uint16_t)rn(2));
  ww(f, 0xe, (uint16_t)(1 + rn(3)));
  DS_W(0x4c0a) = me;
  static const uint32_t v[] = {0x31, 0x32, 0x33};
  ds_w32(0x4c30, v[rn(3)]);
  wd(f, 0x58, chance(1, 2) ? 0 : 1);
  DS_B(0x4243) = (uint8_t)rn(4);
}

/* ---- 19:0x1594: the older, shorter turn of a computer player ---- */

static void t_1594(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x15940001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned nfl = it % 7;
    unsigned npl = 1 + (it / 7) % 6;
    uint16_t me = (uint16_t)(3 + rn(2));
    world_fleets(nfl);
    world_planets(npl, me, chance(1, 3));
    world_ds(me);
    world_list();
    world_table(npl);
    world_rand_fleets(nfl, me, npl);
    if (chance(1, 2)) ww(g_cur, 8, g_plid[rn(npl)]);
    stubs_random();
    if (it % 4 == 0) focus_1594(me, npl, nfl);
    if (it % 4 == 2) focus_1594b(me, npl, nfl);
    seed_stack32(1, -0x18, g_fleet[rn(NFL_MAX)]);
    DIFF(19, 0x1594, FAR_CDECL, (uint16_t)rnd());
  }
}


/* A scenario aimed at the deepest path of the planet walk of 19:0x0400: an own planet with a big
 * dword, a fleet of the player at that planet with a pending order (+0x18 between 0xa and 0x14), many
 * ships (19:0x03a0 and the sums at +0x10 / +0x12 pass), the list entry found. */
static void focus_0400(uint16_t me, unsigned npl, unsigned nfl) {
  if (!npl || !nfl) return;
  uint32_t pf = g_pl[0];
  ww(pf, 2, me);
  wb(pf, 5, (uint8_t)(rb_(pf, 5) | 2));
  wd(pf, 0x28, 0x5dc + rn(5));
  DS_B(0x4544) = 0;
  uint32_t f = g_fleet[0];
  ww(f, 2, me);
  ww(f, 6, g_plid[0]);
  ww(f, 0x18, (uint16_t)(0xa + rn(0x0b)));
  ww(f, 0x10, (uint16_t)(2 + rn(20)));
  ww(f, 0x12, (uint16_t)(2 + rn(20)));
  ww(f, 0x1e, (uint16_t)(2 + rn(20)));
  ww(f, 0x20, (uint16_t)(2 + rn(20)));
  ww(f, 0x22, (uint16_t)(2 + rn(20)));
  ww(f, 0x24, (uint16_t)(2 + rn(20)));
  DS_B(0x5340) = (uint8_t)rn(6);
  DS_B(0x5342) = (uint8_t)rn(6);
  /* the list needs an entry the walk accepts: (w2 & 0xe) == 4 and the masked dword below 0x4000 */
  if (chance(1, 2)) {
    wb(g_lst, 3, 1);
    wd(g_lst, 4, 0x00040000u | (rn(0x3c) << 10));
  } else {
    wb(g_lst, 3, 0); /* no entry: the walk goes on to the planet-count checks */
  }
  static const uint16_t turns[] = {0xa, 0xb, 0xc, 0x14, 0x15};
  DS_W(0x82) = turns[rn(5)];
  ds_w32(0x428a, 0);
  if (npl >= 2) {
    ww(g_pl[1], 2, 0xffff);
    wb(g_pl[1], 4, (uint8_t)(3 + rn(3)));
    stub_set_ax(S_462A, 0, 1);
  }
}

/* ---- 19:0x0400: the main turn of a computer player ---- */

static void world_players(uint16_t me) {
  uint8_t *pl = LINN((uint16_t)(0x5a64 + 0xc0 * me));
  uint16_t pc = (uint16_t)(chance(1, 4) ? rn(8) : rn(120));
  memcpy(pl + 2, &pc, 2);
  for (unsigned k = 0x1b; k < 0x20; k++) pl[k] = (uint8_t)(chance(1, 10) ? 0xf0 + rn(8) : 3 + rn(8));
  uint16_t t12 = (uint16_t)((int16_t)DS_W(0x007a) / 12 + 8), t24 = (uint16_t)((int16_t)DS_W(0x007a) / 0x18 + 4);
  for (unsigned k = 0; k < 16; k++) {
    uint16_t base = (uint16_t)(0x417a + 0x8d * k);
    uint16_t lo = (uint16_t)((chance(1, 2) ? t12 : t24) + rn(3) - 1), hi = (uint16_t)(chance(4, 5) ? 0 : rn(2));
    if (!(k == 1 && chance(2, 3))) { /* DS:0x428a is record 1's dword: world_ds sets it for 19:0x0400's own test */
      memcpy(LINN((uint16_t)(base + 0x83)), &lo, 2);
      memcpy(LINN((uint16_t)(base + 0x85)), &hi, 2);
    }
    for (unsigned j = 0; j < 4; j++) {
      uint16_t w = (uint16_t)(chance(1, 3) ? 0 : chance(2, 3) ? rn(30) : rn(1500));
      memcpy(LINN((uint16_t)(base + 0x2a + 2 * j)), &w, 2);
    }
  }
  uint16_t h = (uint16_t)(chance(3, 4) ? 1 : rn(3));
  memcpy(LINN(0x4207), &h, 2);
}

static void t_0400(void) {
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x04000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned nfl = it % 8;
    unsigned npl = 1 + (it / 8) % 6;
    uint16_t me = (uint16_t)(3 + rn(2));
    world_fleets(nfl);
    world_planets(npl, me, chance(1, 3));
    if (it % 3 == 0) {
      uint16_t q = chance(1, 2) ? 12 : 24, last = (uint16_t)(g_plid[npl - 1] + 1);
      DS_W(0x007a) = (uint16_t)(((last + q - 1) / q) * q + (chance(1, 2) ? q : 0)); /* a multiple of the divisor */
    }
    world_ds(me);
    world_players(me);
    world_list();
    world_table(npl);
    world_rand_fleets(nfl, me, npl);
    if (chance(1, 2)) ww(g_cur, 8, g_plid[rn(npl)]);
    stubs_random();
    if (it % 3 == 0) focus_0400(me, npl, nfl);
    {
      /* the second 18:0x5d3c answer is compared with the planet-count limit that 19:0x0400 computes */
      int16_t cnt2 = (int16_t)rw(g_tab, 2) * 2;
      int16_t lim = (int16_t)((int16_t)DS_W((uint16_t)(0x5a66 + 0xc0 * me)) / 10);
      if (lim < cnt2) lim = cnt2;
      int16_t thr = (int16_t)((int16_t)(uint16_t)(lim * 10) / 7);
      stub_set_ax(S_5D3C, 1, (uint16_t)(chance(1, 2) ? thr + (int)rn(5) - 2 : lim + (int)rn(3) - 1));
    }
    seed_stack32(1, -0x24, g_fleet[rn(NFL_MAX)]);
    seed_stack32(1, -0x18, g_fleet[rn(NFL_MAX)]);
    DIFF(19, 0x0400, FAR_CDECL, (uint16_t)rnd());
  }
}

/* 21:0x1e2a is decompiled now (src/seg21.c), but here it stays a test double: the double logged by the stub
 * is the expectation. The C version of the caller reaches it through rt_call_far, which would run the C
 * function, so an adapter makes rt_call_far run the stubbed code instead (docs/CONVENTIONS.md rule j2:
 * the doubled callee is not under test here; it is diffed in tests/scenarios/test_seg21.c). */
static uint32_t double_21_1e2a(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss;
  (void)sp;
  (void)r;
  return rt_call_orig(21, 0x1e2a, FAR_CDECL, 0);
}

int main(void) {
  g_only = getenv("SEG19_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  stubs_init();
  {
    static const DtEntry e = {21, 0x1e2a, "double_21_1e2a", FAR_CDECL, double_21_1e2a, 0};
    difftest_register(&e);
  }
  g_fl = gblock(0x200);
  if (want("212e")) t_212e();
  g_ptrs = gblock(0x100);
  for (unsigned i = 0; i < 3; i++) g_fblk[i] = gblock(0x400);
  g_info = gblock(0x400);
  g_cur = gblock(0x100);
  g_pln = gblock(0x400);
  g_lst = gblock(0x100);
  g_tab = gblock(0x100);
  if (want("0000")) t_0000();
  if (want("1594")) t_1594();
  if (want("0400")) t_0400();
  if (want("03a0")) t_03a0();
  if (want("12f0")) t_12f0();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
