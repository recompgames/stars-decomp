/* Diff tests for decompiled segment 20 (src/seg20.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 20 is the code of the type-5 computer player (started by 17:0x0000 when the player word
 * DS:0x5a6a + 0xc0 * player shifted right by 13 is 5):
 *   20:0x0000  the player's whole turn: refresh the strength limits, create designs, walk the planets
 *              and the fleets, queue production and orders
 *   20:0x1da0  queue the ship designs whose techs the player has reached
 *   20:0x2286  send a fleet to the best-scoring planet within range
 *   20:0x23ba  choose the planet a fleet should load cargo at or move to
 *   20:0x2998  give a fleet with waypoints its next order
 *   20:0x2cbc  sum up a fleet's ships and compare with the strength limit
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered API call log, which here holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of
 * the callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See stub_handler for what a double does: it
 * returns the next queued value (a queue per callee, kept in the stub block in the arena, so it is
 * rewound by the snapshot between the two runs), optionally writes queued bytes to the buffer a
 * pointer argument names, and hashes, at every call, the state the call could observe: the record the
 * call names, the stack record it is given, the info array, the planet table, the cursor struct and
 * a few DGROUP words. The hashes are compared as arena memory, so a call made before or after a
 * write it should follow (or precede) shows as a difference.
 * 26:0x0100 and 26:0x0810 (decompiled in src/seg26.c, called by 20:0x0000 through rt_call_far) stay doubles: the real ones
 * open the production dialog (GDI / USER calls, measured: the harness reports UNIMPLEMENTED GDI.SetBkColor and the run
 * faults) and are diffed in tests/scenarios/test_seg26.c.
 * Run as ORIGINAL code in both runs (no double): 8:0x8730 (the random generator), 8:0xbc50 (squared
 * distance), 7:0x01e4 (find a planet by id), 7:0x0358 and 7:0x3b60, 8:0xb90e, 28:0x41d4. 2:0x50d8
 * (decompiled in src/seg02.c) and 16:0x193e (src/seg16.c) are called by 20:0x1da0 / 20:0x0000: the
 * ORIGINAL code in one run, the C function in the other.
 *
 * Environment (debugging aids): SEG20_ONLY=tag[,tag] runs only the named parts (2cbc 2286 2998 23ba 1da0 0000);
 * SEG20_ITER=n runs only iteration n of the 20:0x0000 scenario; SEG20_TRACE=n prints the calls of the doubles
 * (and their state hashes) of iteration n.
 *
 * A DIFF leaves the runtime in ORIGINAL mode, so the callees that 20:0x0000 reaches through rt_call_far (2:0x50d8,
 * 16:0x193e, 20:0x1da0, 0x2286, 0x23ba, 0x2998) run the original code in both runs; they are diffed here on their
 * own. The return address under rt_call_far / CALL_ORIG is the harness thunk, not the original CS:IP (CONVENTIONS rule
 * d), so stack junk that holds an earlier return address differs between the two runs: the scenario of 20:0x0000
 * avoids the reads of such junk (see world_turn and g_rec_hash). */
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
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w16(uint16_t off, uint16_t v) { memcpy(LINN(off), &v, 2); }
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
/* Random bytes into a block. */
static void fill_rand(uint32_t fp, uint16_t n) {
  uint8_t *p = LIN(fp);
  for (uint16_t i = 0; i < n; i++) p[i] = (uint8_t)rnd();
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
static unsigned g_rec_hash = 18;
static bool g_trace;   /* SEG20_TRACE=<iteration>: print the doubles' calls of that iteration of the 20:0x0000 scenario */

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs;     /* argument words */
  int8_t farw;       /* argument word index of a far pointer to a record whose 0x7c bytes are hashed, -1 none */
  int8_t poke;       /* argument word index of a near pointer (SS) the queued 16 bytes are written to, -1 none */
  int8_t ssw;        /* argument word index of a near pointer (SS) whose ssn bytes are hashed, -1 none */
  uint8_t ssn;
  uint8_t pokelen;   /* bytes a queued poke writes */
} StubCfg;

enum {
  S_0000, S_0630, S_2664, S_2A74, S_457A, S_3BB4, S_39F6, S_3922, S_3854, S_292A, S_45AA, S_4DE8, S_0750, S_153E,
  S_28FC, S_36AA, S_5D3C, S_5BAE, S_4912, S_2BA6, S_2732, S_5FE6, S_3558, S_3600, S_5E82, S_3AD8, S_2DB2, S_2E90,
  S_4510, S_098A, S_0864, S_08FE, S_1434, S_59BA, S_4F76, S_0100, S_0810, S_1E2A, S_0358,
  S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    [S_0000] = {"18:0000", 18, 0x0000, 4, -1, -1, -1, 0, 0},
    [S_0630] = {"18:0630", 18, 0x0630, 2, -1, -1, 0, 0x8d, 0},
    [S_2664] = {"18:2664", 18, 0x2664, 4, 0, -1, 2, 18, 0},
    [S_2A74] = {"9:2a74", 9, 0x2a74, 2, -1, -1, -1, 0, 0},
    [S_457A] = {"18:457a", 18, 0x457a, 3, 0, -1, -1, 0, 0},
    [S_3BB4] = {"18:3bb4", 18, 0x3bb4, 3, 0, -1, -1, 0, 0},
    [S_39F6] = {"18:39f6", 18, 0x39f6, 4, 0, -1, -1, 0, 0},
    [S_3922] = {"18:3922", 18, 0x3922, 4, 0, -1, -1, 0, 0},
    [S_3854] = {"18:3854", 18, 0x3854, 4, 0, -1, -1, 0, 0},
    [S_292A] = {"10:292a", 10, 0x292a, 3, 0, -1, -1, 0, 0},
    [S_45AA] = {"9:45aa", 9, 0x45aa, 2, 0, -1, -1, 0, 0},
    [S_4DE8] = {"9:4de8", 9, 0x4de8, 3, 0, -1, -1, 0, 0},
    [S_0750] = {"18:0750", 18, 0x0750, 6, -1, -1, -1, 0, 0},
    [S_153E] = {"7:153e", 7, 0x153e, 2, -1, -1, -1, 0, 0},
    [S_28FC] = {"18:28fc", 18, 0x28fc, 5, -1, -1, -1, 0, 0},
    [S_36AA] = {"18:36aa", 18, 0x36aa, 1, -1, -1, -1, 0, 0},
    [S_5D3C] = {"18:5d3c", 18, 0x5d3c, 5, -1, 3, 4, 16, 2},
    [S_5BAE] = {"18:5bae", 18, 0x5bae, 1, -1, -1, 0, 10, 0},
    [S_4912] = {"18:4912", 18, 0x4912, 1, -1, 0, 0, 10, 10},
    [S_2BA6] = {"18:2ba6", 18, 0x2ba6, 1, -1, 0, -1, 0, 2},
    [S_2732] = {"18:2732", 18, 0x2732, 4, -1, -1, -1, 0, 0},
    [S_5FE6] = {"18:5fe6", 18, 0x5fe6, 2, 0, -1, -1, 0, 0},
    [S_3558] = {"18:3558", 18, 0x3558, 3, 0, 2, -1, 0, 16},
    [S_3600] = {"18:3600", 18, 0x3600, 3, 0, 2, -1, 0, 16},
    [S_5E82] = {"18:5e82", 18, 0x5e82, 8, 0, -1, -1, 0, 0},
    [S_3AD8] = {"18:3ad8", 18, 0x3ad8, 4, -1, -1, -1, 0, 0},
    [S_2DB2] = {"18:2db2", 18, 0x2db2, 2, 0, -1, -1, 0, 0},
    [S_2E90] = {"18:2e90", 18, 0x2e90, 2, 0, -1, -1, 0, 0},
    [S_4510] = {"18:4510", 18, 0x4510, 3, 0, -1, -1, 0, 0},
    [S_098A] = {"18:098a", 18, 0x098a, 3, 0, 2, -1, 0, 4},
    [S_0864] = {"18:0864", 18, 0x0864, 3, 0, -1, -1, 0, 0},
    [S_08FE] = {"18:08fe", 18, 0x08fe, 4, 0, -1, -1, 0, 0},
    [S_1434] = {"18:1434", 18, 0x1434, 7, 0, -1, -1, 0, 0},
    [S_59BA] = {"18:59ba", 18, 0x59ba, 5, -1, -1, 3, 16, 0},
    [S_4F76] = {"9:4f76", 9, 0x4f76, 3, 0, 2, -1, 0, 2},
    [S_0100] = {"26:0100", 26, 0x0100, 1, -1, -1, -1, 0, 0},
    [S_0810] = {"26:0810", 26, 0x0810, 1, -1, -1, -1, 0, 0},
    [S_1E2A] = {"21:1e2a", 21, 0x1e2a, 0, -1, -1, -1, 0, 0},
    [S_0358] = {"7:0358", 7, 0x0358, 2, -1, -1, -1, 0, 0},
};

/* A second out-buffer of a poking double: the argument word index of its near pointer (SS) and the number of queued
 * bytes (after the first pokelen) that are written to it. */
static const struct { uint8_t idx, len; } g_poke2[S_NSTUB] = {[S_5D3C] = {4, 14}};

/* The state the doubles hash at every call, besides the record they name. */
static uint32_t g_w_info;   /* far pointer to the info array (DS:0x1600), 64 * 16 bytes */
static uint32_t g_w_pln;    /* far pointer to the planet records (0x400 bytes) */
static uint32_t g_w_cur;    /* far pointer to the cursor struct (DS:0x4bf0), 0x20 bytes */
static uint32_t g_w_lst;    /* far pointer to the list struct (DS:0xcd2), 0x40 bytes */

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
  if (g_trace) {
    fprintf(stderr, "  call %s(", cfg->name);
    for (unsigned a = 0; a < cfg->nargs; a++) fprintf(stderr, "%s%04x", a ? "," : "", hcall_word(c, a));
    fprintf(stderr, ")\n");
  }
  if (cfg->farw >= 0) {
    uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
    if (fp != 0 && mem_check(fp, 0x7c)) h = fnv(h, LIN(fp), 0x7c);
  }
  if (cfg->ssw >= 0 && cfg->ssn) {
    uint16_t p = hcall_word(c, (unsigned)cfg->ssw);
    unsigned n = cfg->ssn;
    /* The order record given to 18:0x2664 by an emulated 20:0x2286 / 0x23ba / 0x2998 called from 20:0x0000 has
     * stack junk in the bytes the callee leaves unwritten (a return address of an earlier call, which differs
     * between the original and the C caller, CONVENTIONS rule d): only its first g_rec_hash bytes are hashed, and
     * of byte 7 (the flag byte, whose bits 7 to 5 are junk: (junk & 0xf1) | 0x11) only the bits 4 to 0. */
    const uint8_t *rec = LIN(MK_FP(c->regs->ss, p));
    if (c->ordinal == S_2664 && g_rec_hash < n) {
      n = g_rec_hash;
      h = fnv(h, rec, n);
      uint8_t b7 = (uint8_t)(rec[n] & 0x1f);
      h = fnv(h, &b7, 1);
    } else {
      h = fnv(h, rec, n);
    }
  }
  if (g_w_info) h = fnv(h, LIN(g_w_info), 64 * 16);
  if (g_w_pln) h = fnv(h, LIN(g_w_pln), 0x400);
  if (g_w_cur) h = fnv(h, LIN(g_w_cur), 0x20);
  if (g_w_lst) h = fnv(h, LIN(g_w_lst), 0x40);
  h = fnv(h, LINN(0x4bec), 2);
  h = fnv(h, LINN(0x4c36), 2);
  h = fnv(h, LINN(0x5392), 2);
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
    if (g_poke2[c->ordinal].len) {
      uint16_t p = hcall_word(c, g_poke2[c->ordinal].idx);
      if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), e + 4 + cfg->pokelen, g_poke2[c->ordinal].len);
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
/* Queues one return (dx:ax) and, for a poking double, bytes written to the out-buffer. */
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

#define NFL_MAX 140
#define NPL_MAX 8
static uint32_t g_fblk;       /* the block that holds the fleet records (0x80 bytes each from offset 0x10) */
static uint32_t g_wpl;        /* the block that holds the waypoint list records (0x40 bytes each) */
static uint32_t g_info;       /* DS:0x1600: 16 bytes per planet id */
static uint32_t g_pln;        /* the planet table block (DS:0xb6) */
static uint32_t g_plist;      /* DS:0x1608: the pointer list */
static uint32_t g_ptrs;       /* DS:0xba: the fleet pointer array */
static uint32_t g_fleet[NFL_MAX];
static uint32_t g_pl[NPL_MAX];

static void set_far(uint16_t off, uint32_t fp) { ds_w32(off, fp); }

/* Lays out npl planets (ids 0 .. npl - 1, 0x38 bytes each from offset 0x10 of the block), the pointer list
 * DS:0x1608 of them (DS:0x4b62 entries), the xy table DS:0x31ba and the info array. Fleet 0 sits at
 * (cx, cy); the planets' positions are random around it, so that some are within the squared distance
 * limits of the functions (0x1440, 0x9c40 and the steps 0x9c4, 0x2710, 0x57e4). */
static void world(unsigned npl, unsigned nfl) {
  CHECK(npl <= NPL_MAX && nfl <= NFL_MAX);
  for (unsigned i = 0; i < NPL_MAX; i++) {
    g_pl[i] = MK_FP(HI(g_pln), (uint16_t)(0x10 + 0x38 * i));
    memset(LIN(g_pl[i]), 0, 0x38);
  }
  for (unsigned i = 0; i < NFL_MAX; i++) {
    g_fleet[i] = MK_FP(HI(g_fblk), (uint16_t)(0x10 + 0x80 * i));
    memset(LIN(g_fleet[i]), 0, 0x7c);
  }
  set_far(0x00ba, g_ptrs);
  DS_W(0x553a) = (uint16_t)nfl;
  for (unsigned i = 0; i < NFL_MAX; i++) memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * i))), &g_fleet[i], 4);
  if (nfl > 1 && chance(1, 8)) memset(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * (nfl - 1)))), 0, 4);
  set_far(0x00b6, MK_FP(HI(g_pln), 0x10));
  DS_W(0x4d96) = (uint16_t)npl;
  DS_W(0x007a) = (uint16_t)npl;
  DS_W(0x4b62) = (uint16_t)npl;
  set_far(0x1608, g_plist);
  set_far(0x1600, g_info);
  g_w_info = g_info;
  g_w_pln = g_pln;
  fill_rand(g_info, 64 * 16);
  for (unsigned i = 0; i < 64; i++) wb(g_info, (uint16_t)(16 * i + 0xe), (uint8_t)(chance(1, 3) ? 1 : rn(2)));
  uint8_t *xy = LINN(0x31ba);
  for (unsigned i = 0; i < 0x100; i++) xy[i] = 0;
  for (unsigned i = 0; i < npl; i++) {
    uint32_t pf = g_pl[i];
    fill_rand(pf, 0x38);
    ww(pf, 0, (uint16_t)i);
    ww(pf, 2, chance(1, 3) ? 0xffff : (uint16_t)rn(3));
    wb(pf, 5, (uint8_t)(rnd() & (chance(1, 2) ? 0xff : 0xfd)));
    wb(pf, 9, (uint8_t)rn(chance(1, 3) ? 256 : 40));
    wb(pf, 10, (uint8_t)rn(chance(1, 3) ? 256 : 40));
    wb(pf, 11, (uint8_t)rn(chance(1, 3) ? 256 : 40));
    wd(pf, 0x1c, chance(1, 3) ? rn(0x9c4) : chance(1, 2) ? 0x9c4 + rn(3) - 1 : rn(5000));
    wd(pf, 0x20, chance(1, 3) ? rn(300) : rn(5000));
    wd(pf, 0x24, chance(1, 4) ? 1000 + rn(3) - 1 : rn(3000));
    wd(pf, 0x28, chance(1, 3) ? 0x3e8 + rn(3) - 1 : chance(1, 2) ? rn(5000) : rnd());
    int16_t px = (int16_t)(50 + (int)rn(200) - 100), py = (int16_t)(50 + (int)rn(200) - 100);
    uint32_t v = (uint32_t)(uint16_t)px | ((uint32_t)(uint16_t)py << 16);
    memcpy(xy + 4 * i, &v, 4);
    memcpy(LIN(MK_FP(HI(g_plist), (uint16_t)(4 * i))), &g_pl[i], 4);
  }
  memset(LIN(MK_FP(HI(g_plist), (uint16_t)(4 * npl))), 0, 4);
  if (npl > 1 && chance(1, 6)) memset(LIN(MK_FP(HI(g_plist), (uint16_t)(4 * (npl - 1)))), 0, 4);
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    fill_rand(f, 0x7c);
    ww(f, 0, (uint16_t)(i + 1));
    ww(f, 2, chance(3, 4) ? 3 : (uint16_t)rn(5));
    ww(f, 6, (chance(1, 3) || !npl) ? 0xffff : (uint16_t)rn(npl));
    int16_t fx = (int16_t)(50 + (int)rn(40) - 20), fy = (int16_t)(50 + (int)rn(40) - 20);
    wd(f, 8, (uint32_t)(uint16_t)fx | ((uint32_t)(uint16_t)fy << 16));
    for (unsigned k = 0; k < 6; k++) ww(f, (uint16_t)(0x10 + 2 * k), (uint16_t)(chance(1, 3) ? 0 : rn(chance(1, 3) ? 1500 : 6)));
    ww(f, 0x1c, (uint16_t)(chance(1, 3) ? 0 : rn(5)));
    ww(f, 0x1e, (uint16_t)(chance(1, 3) ? 0 : rn(5)));
    ww(f, 0x62, (uint16_t)(chance(1, 5) ? 0xffff : rn(4)));
    for (unsigned k = 0; k < 3; k++) wd(f, (uint16_t)(0x4c + 4 * k), chance(1, 3) ? 0 : rn(300));
    /* the waypoint list: a record of 0x40 bytes later in the fleet block */
    uint32_t w = MK_FP(HI(g_wpl), (uint16_t)(0x40 * i));
    fill_rand(w, 0x40);
    wb(w, 0x16 + 0x1d - 0x16 + 0, rb_(w, 0x1d)); /* keep the random kind nibble */
    wb(w, 0x1d, (uint8_t)((rb_(w, 0x1d) & 0xf0) | (chance(1, 3) ? 1 : chance(1, 2) ? 2 : rn(4))));
    ww(w, 0x1a, npl ? (uint16_t)rn(npl) : 0);
    int16_t wx = (int16_t)(50 + (int)rn(400) - 200), wy = (int16_t)(50 + (int)rn(400) - 200);
    wd(w, 0x16, (uint32_t)(uint16_t)wx | ((uint32_t)(uint16_t)wy << 16));
    wd(f, 0x64, w);
  }
  DS_W(0x014c) = 3;
  DS_W(0x0082) = (uint16_t)(chance(1, 3) ? rn(0x30) : chance(1, 2) ? 0x4f + rn(3) : rn(300));
  DS_B(0x0080) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
  DS_B(0x5340) = (uint8_t)(chance(1, 3) ? rn(6) : rn(80));
  DS_B(0x5341) = (uint8_t)rn(40);
  DS_B(0x5342) = (uint8_t)rn(8);
  DS_B(0x5343) = (uint8_t)rn(4);
  g_w_cur = 0;
  g_w_lst = 0;
}

/* Overwrites the AX of the idx-th queued return of a double. */
static void stub_set_ax(unsigned id, unsigned idx, uint16_t ax) {
  uint8_t *e = (uint8_t *)LIN(g_stub_blk) + STUB_Q + (size_t)id * STUB_QMAX * STUB_QENT + (size_t)idx * STUB_QENT;
  e[0] = (uint8_t)ax;
  e[1] = (uint8_t)(ax >> 8);
}

/* Random returns for the doubles that hand out planets or answers: queues of 16 for each. */
static void stubs_random(unsigned npl) {
  for (unsigned k = 0; k < 16; k++) {
    uint32_t p;
    p = (npl && chance(2, 3)) ? g_pl[rn(npl)] : 0;
    stub_push(S_39F6, LO(p), HI(p), NULL);
    p = (npl && chance(2, 3)) ? g_pl[rn(npl)] : 0;
    stub_push(S_3922, LO(p), HI(p), NULL);
    p = (npl && chance(2, 3)) ? g_pl[rn(npl)] : 0;
    stub_push(S_3854, LO(p), HI(p), NULL);
    stub_push(S_0000, (uint16_t)(chance(1, 3) ? 1 : 0), 0, NULL);
    stub_push(S_2664, (uint16_t)(chance(1, 3) ? 0 : rn(5)), 0, NULL);
    stub_push(S_292A, (uint16_t)(chance(1, 4) ? 0 : rn(500)), (uint16_t)(chance(1, 8) ? 0xffff : 0), NULL);
    stub_push(S_45AA, (uint16_t)(chance(1, 5) ? 0x19 + rn(3) - 1 : rn(60)), 0, NULL);
    stub_push(S_4DE8, (uint16_t)rn(40), 0, NULL);
  }
}

/* ---- 20:0x2cbc: does the fleet's strength reach the limit ---- */

static void t_2cbc(void) {
  static const uint16_t w[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff, 0x4000, 0x00fe, 0x0100};
  static const uint8_t thr[] = {0, 1, 5, 0x7f, 0x80, 0xff};
  world(1, 1);
  uint32_t fl = g_fleet[0];
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x2cbc0001u + 0x9e3779b9u * (it + 1);
    for (unsigned k = 0; k < 8; k++) ww(fl, (uint16_t)(0x10 + 2 * k), w[rn(sizeof w / 2)]);
    if (it % 3 == 0)
      for (unsigned k = 0; k < 8; k++) ww(fl, (uint16_t)(0x10 + 2 * k), (uint16_t)rn(4));
    DS_B(0x5340) = thr[it % sizeof thr];
    if (it % 5 == 0) DS_B(0x5340) = (uint8_t)rn(12);
    for (unsigned s = 0; s < 2; s++) {
      uint16_t rec = (uint16_t)(0x417a + 0x8d * (8 + s));
      ds_w16(rec, chance(2, 3) ? 9 : (uint16_t)rn(12));
      DS_B(rec + 0x7c) = (uint8_t)(chance(1, 2) ? 2 : 0) | (uint8_t)(rnd() & 0xfd);
    }
    uint16_t out = (it % 4 == 0) ? 0 : 0x6f00;
    DS_W(0x6f00) = 0x5555;
    DIFF(20, 0x2cbc, FAR_CDECL, LO(fl), HI(fl), out);
  }
}

/* ---- 20:0x2286: send a fleet to the best planet within range ---- */

static void t_2286(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x22860001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned npl = it % 7, nfl = 1;
    world(npl, nfl);
    stubs_random(npl);
    if (npl && it % 2 == 0) ww(g_fleet[0], 6, (uint16_t)rn(npl));
    if (it % 11 == 0 && npl) { /* fleet on top of the planets: every distance below the limit */
      wd(g_fleet[0], 8, 0x00320032);
      uint8_t *xy = LINN(0x31ba);
      for (unsigned i = 0; i < npl; i++) {
        uint32_t v = (uint32_t)(uint16_t)(50 + rn(40) - 20) | ((uint32_t)(uint16_t)(50 + rn(40) - 20) << 16);
        memcpy(xy + 4 * i, &v, 4);
      }
    }
    DIFF(20, 0x2286, FAR_CDECL, LO(g_fleet[0]), HI(g_fleet[0]));
  }
}

/* ---- 20:0x2998: give a fleet its next order ---- */

static void t_2998(void) {
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x29980001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned npl = 1 + it % 6, nfl = 1;
    world(npl, nfl);
    stubs_random(npl);
    uint32_t f = g_fleet[0];
    if (it % 2 == 0) ww(f, 6, (uint16_t)rn(npl));
    if (it % 9 == 0) ww(f, 6, 0xffff);
    if (it % 4 == 1) ww(f, 0x62, 1);
    ww(f, 0x1c, (uint16_t)(chance(1, 3) ? 0 : rn(5)));
    /* the planet the fleet sits at: owned by the player (3) or not */
    if (rw(f, 6) != 0xffff) ww(g_pl[rw(f, 6)], 2, (uint16_t)(chance(1, 2) ? 3 : chance(1, 2) ? 0xffff : rn(3)));
    if (it % 6 == 0) DS_B(0x5340) = (uint8_t)(1 + rn(40));
    DS_B(0x5a6b + 0xc0 * 3) = (uint8_t)((rnd() & 0xe3) | (chance(2, 3) ? 0x14 : (rn(8) << 2)));
    DS_B(0x5a64 + 0xc0 * 3 + 0x1b) = 0;
    /* the waypoint record seen by the first branch: its target planet's turn stamp */
    for (unsigned i = 0; i < npl; i++) ww(g_pl[i], 0x32, (uint16_t)(chance(1, 2) ? DS_W(0x82) : rn(300)));
    DIFF(20, 0x2998, FAR_CDECL, LO(f), HI(f));
  }
}

/* ---- 20:0x23ba: choose a target planet ---- */

static void t_23ba(void) {
  for (unsigned it = 0; it < 1200; it++) {
    g_rng = 0x23ba0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned npl = 1 + it % 7, nfl = 1;
    world(npl, nfl);
    stubs_random(npl);
    uint32_t f = g_fleet[0];
    if (it % 3 == 0) ww(f, 6, 0xffff);
    else ww(f, 6, (uint16_t)rn(npl));
    if (rw(f, 6) != 0xffff && it % 2 == 0) {
      uint32_t p = g_pl[rw(f, 6)];
      ww(p, 2, 3);
      wd(p, 0x28, 0x3e8 + rn(0x3000));
      stub_set_ax(S_45AA, 0, (uint16_t)(0x1a + rn(30)));
    }
    if (it % 4 == 0) { /* fleets close to the planets so that the distance steps are all reached */
      wd(f, 8, 0x00320032);
      uint8_t *xy = LINN(0x31ba);
      for (unsigned i = 0; i < npl; i++) {
        uint32_t v = (uint32_t)(uint16_t)(50 + rn(300) - 150) | ((uint32_t)(uint16_t)(50 + rn(300) - 150) << 16);
        memcpy(xy + 4 * i, &v, 4);
      }
    }
    for (unsigned i = 0; i < npl; i++) wd(g_pl[i], 0x1c, chance(1, 3) ? rn(100) : chance(1, 2) ? rn(2600) : 0x9c4 + rn(3) - 1);
    DS_W(0x0082) = (uint16_t)(chance(1, 2) ? rn(0xb0) : 0x50 + rn(3));
    DIFF(20, 0x23ba, FAR_CDECL, LO(f), HI(f));
  }
}


/* Boundaries of 20:0x2286: the squared distance limit 0x1440 (72 * 72 exactly) and the score comparison
 * 6 * current / 5 < best (current 8 * 5 = 40, so 48 is the equal case). Fleet at (0,0) on planet 0. */
static void t_2286_edges(void) {
  static const int dist[] = {70, 71, 72, 73, -72, -71};
  static const uint8_t b9[] = {5, 6, 7, 4};
  for (unsigned it = 0; it < 12; it++) {
    g_rng = 0x22860f00u + it;
    stubs_clear_all();
    world(2, 1);
    stubs_random(2);
    uint32_t f = g_fleet[0];
    ww(f, 6, 0);
    wd(f, 8, 0);
    wb(g_pl[0], 9, 5), wb(g_pl[0], 10, 0), wb(g_pl[0], 11, 0);
    uint8_t *xy = LINN(0x31ba);
    memset(xy, 0, 16);
    for (unsigned c = 0; c < sizeof dist / sizeof dist[0]; c++)
      for (unsigned k = 0; k < sizeof b9; k++) {
        uint32_t v = (uint32_t)(uint16_t)(int16_t)dist[c];
        memcpy(xy + 4, &v, 4);
        wb(g_pl[1], 9, b9[k]), wb(g_pl[1], 10, 0), wb(g_pl[1], 11, 0);
        DIFF(20, 0x2286, FAR_CDECL, LO(f), HI(f));
      }
  }
}

/* Boundaries of 20:0x23ba (fleet not at a planet): the distance steps 50, 100, 150 and the limit 200 (squared
 * 0x9c4, 0x2710, 0x57e4, 0x9c40) against 9:0x45aa ratings. Planet 0 is the candidate at the distance, planet 1
 * sits on the fleet with rating 10 (8 when the candidate is at the limit). */
static void t_23ba_edges(void) {
  static const struct { int d; uint16_t va; } cs[] = {{50, 11}, {51, 11}, {100, 13}, {101, 13}, {150, 15}, {151, 15},
                                                      {199, 15}, {200, 15}, {201, 15}, {0, 11}};
  for (unsigned it = 0; it < 6; it++) {
    for (unsigned c = 0; c < sizeof cs / sizeof cs[0]; c++) {
      g_rng = 0x23ba0f00u + 16 * it + c;
      stubs_clear_all();
      world(2, 1);
      stubs_random(2);
      uint32_t f = g_fleet[0];
      ww(f, 6, 0xffff);
      wd(f, 8, 0);
      uint8_t *xy = LINN(0x31ba);
      memset(xy, 0, 16);
      uint32_t v = (uint32_t)(uint16_t)(int16_t)cs[c].d;
      memcpy(xy, &v, 4);
      ww(g_pl[0], 0, 0);
      ww(g_pl[1], 0, 1);
      /* the planets' resource dwords are all above the thresholds of the first search, so that the search for
       * the best planet is the one that runs */
      for (unsigned i = 0; i < 2; i++)
        for (unsigned k = 0; k < 3; k++) wd(g_pl[i], (uint16_t)(0x1c + 4 * k), 3000);
      stub_clear(S_45AA);
      stub_push(S_45AA, cs[c].va, 0, NULL);
      stub_push(S_45AA, (uint16_t)(cs[c].d >= 199 ? 8 : 10), 0, NULL);
      stub_push(S_2664, 1, 0, NULL);
      DIFF(20, 0x23ba, FAR_CDECL, LO(f), HI(f));
    }
  }
}

/* Boundaries of 20:0x2998: the squared distance 0xf424 (250 * 250 exactly) to the waypoint (kind 2: a far one
 * proceeds, a near one stops), and the strength sum against the multiples of the limit DS:0x5340. */
static void t_2998_edges(void) {
  for (unsigned it = 0; it < 24; it++) {
    g_rng = 0x29980f00u + it;
    stubs_clear_all();
    world(3, 1);
    stubs_random(3);
    uint32_t f = g_fleet[0];
    uint32_t w = rn(2) ? MK_FP(HI(g_wpl), 0) : MK_FP(HI(g_wpl), 0x40);
    wd(f, 0x64, w);
    ww(f, 0x62, 2);
    wd(f, 8, 0);
    static const int dd[] = {249, 250, 251, -250, -251};
    for (unsigned c = 0; c < 5; c++) {
      wd(w, 0x16, (uint32_t)(uint16_t)(int16_t)dd[c]);
      wb(w, 0x1d, (uint8_t)((rb_(w, 0x1d) & 0xf0) | (it % 3 == 0 ? 1 : 2)));
      ww(w, 0x1a, 0);
      ww(g_pl[0], 0x32, DS_W(0x82));
      ww(g_pl[0], 2, 0xffff);
      DIFF(20, 0x2998, FAR_CDECL, LO(f), HI(f));
    }
  }
  /* the sum against T, 2T, 3T, 4T, 0x3c and 0x78 */
  static const unsigned T[] = {3, 7, 20};
  for (unsigned t = 0; t < 3; t++) {
    unsigned tt = T[t];
    unsigned sums[] = {tt - 1, tt, tt + 1, 2 * tt - 1, 2 * tt, 2 * tt + 1, 3 * tt - 1, 3 * tt, 3 * tt + 1, 4 * tt - 1, 4 * tt,
                       4 * tt + 1, 0x3b, 0x3c, 0x3d, 0x77, 0x78, 0x79};
    for (unsigned c = 0; c < sizeof sums / sizeof sums[0]; c++)
      for (unsigned own = 0; own < 2; own++)
        for (unsigned seed = 0; seed < 3; seed++) {
          g_rng = 0x29980e00u + 1000 * t + 40 * c + 4 * own + seed;
          stubs_clear_all();
          world(3, 1);
          stubs_random(3);
          uint32_t f = g_fleet[0];
          ww(f, 0x62, 0);
          for (unsigned k = 0; k < 6; k++) ww(f, (uint16_t)(0x10 + 2 * k), 0);
          ww(f, 0x10, (uint16_t)sums[c]);
          ww(f, 0x1c, (uint16_t)(own ? rn(3) : tt % 5)); /* si for the 5342 / 5343 compares */
          ww(f, 0x1e, 0);
          ww(f, 6, 1);
          ww(g_pl[1], 2, own ? 3 : 4);
          DS_B(0x5340) = (uint8_t)tt;
          DS_B(0x5341) = (uint8_t)(tt / 2);
          DS_B(0x5342) = (uint8_t)(1 + rn(3));
          DS_B(0x5343) = (uint8_t)(1 + rn(3));
          DS_B(0x5a6b + 0xc0 * 3) = 0x14;
          DIFF(20, 0x2998, FAR_CDECL, LO(f), HI(f));
        }
  }
}

/* ---- 20:0x1da0: queue the ship designs the player can build ---- */

static void t_1da0(void) {
  g_rec_hash = 18;
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x1da00001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world(1, 1);
    stubs_random(1);
    DS_W(0x014c) = (uint16_t)rn(16);
    DS_W(0x0082) = (uint16_t)(chance(1, 2) ? rn(0x50) : 0x13 + rn(0x40));
    /* the 0x8d-byte design records DS:0x417a + 0x8d * k: bit 1 of byte +0x7c and the stamp word at +0x7d */
    for (unsigned k = 0; k < 20; k++) {
      uint16_t rec = (uint16_t)(0x417a + 0x8d * k);
      DS_B(rec + 0x7c) = (uint8_t)((rnd() & 0xfd) | (chance(3, 4) ? 2 : 0));
      ds_w16((uint16_t)(rec + 0x7d), (uint16_t)(DS_W(0x82) - (chance(1, 3) ? 0x14 : rn(40))));
      ds_w32((uint16_t)(rec + 0x83), chance(1, 2) ? 0 : rnd());
      ds_w16(rec, (uint16_t)(chance(1, 8) ? 5 : rn(30)));
    }
    uint8_t *pl = (uint8_t *)LINN((uint16_t)(0x5a64 + 0xc0 * DS_W(0x14c)));
    for (unsigned k = 0x1a; k < 0x20; k++) pl[k] = (uint8_t)(it % 13 == 0 ? 0x7f : chance(1, 12) ? 0xf0 + rn(16) : rn(18));
    pl[7] = (uint8_t)((rnd() & 0xe3) | (rn(8) << 2));
    DS_B(0x4243) = (uint8_t)(chance(1, 3) ? 0xf : rn(16));
    ds_w32(0x151e, rnd());
    /* the request record 2:0x50d8 fills (BP-0x10) is read by 2:0x50f8: the player rows stay as set above */
    DIFF(20, 0x1da0, FAR_CDECL);
  }
}


/* ---- 20:0x0000: the whole turn of the type-5 player ---- */

static uint32_t g_cur;   /* DS:0x4bf0: the cursor struct */
static uint32_t g_lst;   /* DS:0xcd2: the list struct */
static uint32_t g_aux;   /* the blocks planet +0x34 points to */
static uint32_t g_dsg;   /* a ShipDesign[16] table every player's pointer DS:0xbe + 4 * player names (16:0x193e reads it) */

/* The world of the turn: planets and fleets as for the small functions, plus the cursor and list structs, the
 * design records DS:0x417a + 0x8d * k, the player row and the turn. `me` is the current player. */
static void world_turn(unsigned npl, unsigned nfl, uint16_t me) {
  world(npl, nfl);
  DS_W(0x14c) = me;
  set_far(0x4bf0, g_cur);
  fill_rand(g_cur, 0x40);
  g_w_cur = g_cur;
  set_far(0x0cd2, g_lst);
  memset(LIN(g_lst), 0, 0x40);
  g_w_lst = g_lst;
  {
    uint8_t n = (uint8_t)(chance(1, 4) ? 0 : 1 + rn(4));
    wb(g_lst, 3, n);
    for (unsigned k = 0; k < n; k++) {
      static const uint16_t kinds[] = {2, 4, 2, 4, 0, 6, 8, 0xe, 3};
      uint16_t hi = (uint16_t)(kinds[rn(9)] | (chance(1, 5) ? 1 : 0));
      static const uint16_t los[] = {0x0000, 0x3800, 0x3c00, 0x4000, 0x4400, 0x4800, 0x3400, 0x3000, 0x0400, 0x7c00};
      uint16_t lo = (uint16_t)(los[rn(10)] | (chance(1, 2) ? 0 : rn(0x400)));
      wd(g_lst, (uint16_t)(4 + 4 * k), (uint32_t)lo | ((uint32_t)hi << 16));
    }
  }
  /* the design records and the data behind them (DS:0x417a .. 0x4a50) */
  uint8_t *tab = LINN(0x417a);
  for (unsigned i = 0; i < 0x4a50 - 0x417a; i++) tab[i] = (uint8_t)rnd();
  for (unsigned k = 0; k < 16; k++) {
    uint16_t rec = (uint16_t)(0x417a + 0x8d * k);
    DS_B(rec + 0x7c) = (uint8_t)((rnd() & 0xfd) | (chance(2, 3) ? 2 : 0));
    ds_w16((uint16_t)(rec + 0x7d), (uint16_t)(DS_W(0x82) - (chance(1, 3) ? 0x14 + rn(3) - 1 : rn(60))));
    ds_w32((uint16_t)(rec + 0x83), chance(1, 2) ? 0 : (chance(1, 3) ? rn(0x2000) : rnd()));
    ds_w16(rec, (uint16_t)(chance(1, 8) ? (k == 0 ? 5 : 0x1d) : rn(0x20)));
  }
  DS_B(0x4243) = (uint8_t)(chance(1, 3) ? 0xf : rn(16));
  ds_w16(0x4555, (uint16_t)(chance(1, 3) ? 0xf : rn(16)));
  DS_B(0x4974) = (uint8_t)(chance(1, 3) ? 6 : rn(8));
  DS_B(0x4a01) = (uint8_t)(chance(1, 3) ? 6 : rn(8));
  DS_B(0x496c) = (uint8_t)(chance(1, 3) ? 0xf : rn(16));
  uint8_t *pl = (uint8_t *)LINN((uint16_t)(0x5a64 + 0xc0 * me));
  for (unsigned k = 0; k < 0xc0; k++) pl[k] = (uint8_t)rnd();
  pl[7] = (uint8_t)((rnd() & 0xe3) | (rn(8) << 2));
  {
    uint16_t v = (uint16_t)(chance(1, 3) ? rn(30) : rn(900));
    memcpy(pl + 2, &v, 2);
    v = (uint16_t)rn(2000);
    memcpy(pl + 8, &v, 2);
  }
  for (unsigned k = 0x1a; k < 0x20; k++) pl[k] = (uint8_t)(chance(1, 12) ? 0xf0 + rn(16) : rn(18));
  {
    static const uint16_t turns[] = {0, 3, 5, 9, 0xa, 0xb, 0x13, 0x14, 0x15, 0x19, 0x1a, 0x27, 0x28, 0x29, 0x4f, 0x50, 0x51, 0x73, 0x74, 0x77,
                                     0x78, 0x79, 0x81, 0x82, 0x83, 0x95, 0xc7, 0xc8, 0xc9, 0x100, 0x200};
    DS_W(0x82) = chance(2, 3) ? turns[rn(sizeof turns / 2)] : (uint16_t)rn(0x120);
  }
  for (unsigned i = 0; i < npl; i++) {
    uint32_t pf = g_pl[i];
    ww(pf, 0x12, (uint16_t)rnd());
    wb(pf, 0x2c, (uint8_t)(chance(1, 4) ? 0 : chance(1, 3) ? 1 : rn(16)));
    wd(pf, 0x28, chance(1, 4) ? 0xc8 + rn(3) - 1 : chance(1, 3) ? 0x2710 + rn(3) - 1 : chance(1, 2) ? rn(40000) : rnd());
    if (chance(1, 3)) {
      wd(pf, 0x34, 0);
    } else {
      uint32_t ap = MK_FP(HI(g_aux), (uint16_t)(0x10 * i));
      wb(ap, 3, (uint8_t)(chance(1, 3) ? 0x18 + rn(3) - 1 : rn(0x30)));
      wd(pf, 0x34, ap);
    }
    wb(pf, 5, (uint8_t)(rnd() | (chance(3, 4) ? 2 : 0)));
    wd(pf, 0x1c, chance(1, 3) ? rn(0x1e) : chance(1, 3) ? 0x1388 + rn(3) - 1 : rn(8000));
    wd(pf, 0x20, chance(1, 3) ? rn(0x1e) : chance(1, 3) ? 0x1388 + rn(3) - 1 : rn(8000));
    wd(pf, 0x24, chance(1, 3) ? rn(0x1e) : chance(1, 3) ? 0x7d0 + rn(3) - 1 : rn(8000));
  }
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    ww(f, 2, chance(3, 5) ? me : (uint16_t)(chance(1, 6) ? 0xffff : rn(5)));
    ww(f, 6, (chance(1, 3) || !npl) ? 0xffff : (uint16_t)rn(npl));
    for (unsigned k = 0; k < 16; k++) ww(f, (uint16_t)(0x0c + 2 * k), (uint16_t)(chance(2, 3) ? 0 : chance(1, 8) ? 0xffff : rn(chance(1, 3) ? 30 : 3)));
    ww(f, 0x62, chance(1, 7) ? 0xffff : (uint16_t)rn(4));
    wb(f, 0x60, (uint8_t)(chance(1, 3) ? 4 : rn(8)));
    wd(f, 0x58, chance(1, 3) ? 0 : chance(1, 4) ? (uint32_t)(int32_t)-1 : rn(100));
    ww(f, 0x28, (uint16_t)(chance(1, 3) ? 0 : rn(3)));
    ww(f, 0x2a, (uint16_t)(chance(1, 3) ? 0 : chance(1, 4) ? 0xffff : rn(3)));
    ww(f, 0xe, (uint16_t)(chance(1, 3) ? 0 : rn(3)));
    ww(f, 0x1a, (uint16_t)(chance(1, 2) ? 0 : rn(4)));
    /* 20:0x2998 reads a stack word that is junk when 18:0x2cbc answers 0 (and the junk is a return address
     * of an earlier call, which differs between the original and the C caller, CONVENTIONS rule d): a fleet
     * stronger than every limit DS:0x5340 (at most 50) makes 20:0x2cbc answer 1. The doubled word +0x16 does it
     * (the word at +0x10 stays random, so that the walk over the ship counts of designs 2 to 9 does not always stop
     * at the first); the style B fleets of t_0000 reach the same with design 9 alone. */
    ww(f, 0x16, (uint16_t)(30 + rn(70)));
    wd(f, 0x68, 0);
    uint32_t w = rnd() ? 0 : 0;
    (void)w;
    uint32_t wl = MK_FP(HI(g_wpl), (uint16_t)(0x40 * i));
    wb(wl, 0xa, (uint8_t)(chance(1, 2) ? 0 : chance(1, 3) ? 6 : chance(1, 3) ? 3 : rn(16)));
    static const uint8_t kn[] = {1, 2, 3, 6, 8, 0, 1, 8};
    wb(wl, 0x1d, (uint8_t)((rb_(wl, 0x1d) & 0xf0) | kn[rn(8)]));
    ww(wl, 0x1a, npl ? (uint16_t)rn(npl) : 0);
    ww(wl, 0x16, (uint16_t)(rb_(f, 8) + (int)rn(300) - 150));
    ww(wl, 0x18, (uint16_t)(rb_(f, 10) + (int)rn(300) - 150));
  }
  for (unsigned i = 0; i < 64; i++) {
    wb(g_info, (uint16_t)(16 * i + 3), (uint8_t)rn(3));
  }
  DS_W(0x7a) = (uint16_t)npl;
  if (!g_dsg) g_dsg = gblock(0x8d0 + 0x40);
  fill_rand(g_dsg, 0x8d0);
  for (unsigned k = 0; k < 16; k++) {
    wb(g_dsg, (uint16_t)(0x8d * k + 0x7a), (uint8_t)rn(17)); /* slot count */
    for (unsigned j = 0; j < 16; j++) { /* some slots are mining robots (category 0x80, item 0 to 6) */
      ww(g_dsg, (uint16_t)(0x8d * k + 0x3a + 4 * j), (uint16_t)(chance(1, 2) ? 0x80 : rnd()));
      wb(g_dsg, (uint16_t)(0x8d * k + 0x3c + 4 * j), (uint8_t)rn(chance(1, 4) ? 20 : 7));
    }
  }
  for (unsigned p = 0; p < 16; p++) ds_w32((uint16_t)(0xbe + 4 * p), g_dsg);
  /* the state of the game's random generator 8:0x8730 (two dwords, DS:0x151e and DS:0x1522): set here, not left over from
   * an earlier scenario. SEG20_SEED_HI=n replaces the second dword (for example 0: an unseeded upper half). */
  ds_w32(0x151e, rnd());
  ds_w32(0x1522, getenv("SEG20_SEED_HI") ? (uint32_t)strtoul(getenv("SEG20_SEED_HI"), NULL, 0) : rnd() * 3u + 7u);
}

/* Random answers for the doubles of the turn: queues of up to 24 for each. */
static void stubs_turn(unsigned npl, unsigned nfl) {
  uint8_t pk[16];
  for (unsigned k = 0; k < 24; k++) {
    stub_push(S_28FC, (uint16_t)rnd(), 0, NULL);
    stub_push(S_0630, 0, 0, NULL);
    for (unsigned j = 0; j < 4 && k < 8; j++) {
      memset(pk, 0, sizeof pk);
      uint16_t v = chance(1, 3) ? 0xffff : (uint16_t)rn(16);
      memcpy(pk, &v, 2);
      for (unsigned b = 2; b < 16; b++) pk[b] = (uint8_t)(chance(1, 2) ? 0 : 1 + rn(3));
      stub_push(S_5D3C, (uint16_t)(chance(1, 2) ? rn(3000) : rn(10)), 0, pk);
    }
    stub_push(S_5FE6, (uint16_t)rn(2), 0, NULL);
    {
      memset(pk, 0, sizeof pk);
      uint16_t v = (uint16_t)(chance(1, 4) ? 0x20 + rn(0x20) : chance(1, 3) ? 0x60 + rn(0x20) : rn(0x80));
      memcpy(pk, &v, 2);
      stub_push(S_2BA6, (uint16_t)rn(2), 0, pk);
      for (unsigned b = 0; b < 10; b++) pk[b] = (uint8_t)rnd();
      stub_push(S_4912, 0, 0, pk);
    }
    stub_push(S_2DB2, (uint16_t)rn(2), 0, NULL);
    stub_push(S_2E90, (uint16_t)rn(2), 0, NULL);
    stub_push(S_4510, (uint16_t)rn(2), 0, NULL);
    stub_push(S_5E82, (uint16_t)rn(2), 0, NULL);
    stub_push(S_2664, (uint16_t)rn(2), 0, NULL);
    stub_push(S_3AD8, (uint16_t)(chance(1, 3) ? 0xffff : npl ? rn(npl) : 0), 0, NULL);
    {
      memset(pk, 0, sizeof pk);
      uint32_t pp = (nfl && chance(1, 2)) ? g_fleet[rn(nfl)] : 0;
      memcpy(pk, &pp, 4);
      stub_push(S_098A, (uint16_t)(chance(1, 3) ? 0xffff : npl ? rn(npl) : 0), 0, pk);
    }
    {
      memset(pk, 0, sizeof pk);
      uint16_t v = (uint16_t)(chance(1, 4) ? rn(8) : 10 + rn(30));
      memcpy(pk, &v, 2);
      stub_push(S_4F76, (uint16_t)(chance(1, 3) ? 5 + rn(10) : 10 + rn(30)), 0, pk);
    }
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(2000);
      memcpy(pk + 4 * j, &v, 4);
    }
    stub_push(S_3558, 0, 0, pk);
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(1000);
      memcpy(pk + 4 * j, &v, 4);
    }
    stub_push(S_3600, 0, 0, pk);
  }
}

static void t_0000(void) {
  g_rec_hash = 7;
  const char *only = getenv("SEG20_ITER"), *trace = getenv("SEG20_TRACE");
  for (unsigned it = 0; it < 1500; it++) {
    if (only && (unsigned)atoi(only) != it) continue;
    g_trace = trace && (unsigned)atoi(trace) == it;
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned npl = 1 + it % 6, nfl = it % 9;
    uint16_t me = (uint16_t)(3 + rn(2));
    const bool big = it % 20 == 7;                 /* enough fleets for BP-0xa0 and BP-0xaa to pass 0x3a and 0x5a */
    const bool style_b = it % 4 == 3 || it % 40 == 27; /* fleets whose only ships are of design 9 (the bound 9 of the walk) */
    const bool two_fleets = it % 40 == 33;         /* two fleets of the player at planet 0 with ships of design 5: the first one counts */
    const bool sweep = it % 40 == 17;              /* exactly N fleets that raise the counter BP-0xa0 (N = 0x37 .. 0x3d) */
    if (big) nfl = 95 + rn(45);
    if (sweep) nfl = 0x37 + (it / 40) % 7;
    if (two_fleets) nfl = 2 + (it / 40) % 6;
    world_turn(npl, nfl, me);
    if (style_b) {
      /* 20:0x2cbc adds twice the design 9 count when the design record 9 has hull 9 and bit 1 of +0x7c clear */
      ds_w16(0x466f, 9);
      DS_B(0x46eb) = (uint8_t)(DS_B(0x46eb) & ~2);
    }
    for (unsigned i = 0; i < nfl; i++) {
      uint32_t f = g_fleet[i];
      if (big) {
        if (chance(19, 20)) ww(f, 2, me);
        ww(f, 0x62, (uint16_t)rn(2));
        ww(f, 0xe, 0);
        ww(f, 0x1a, 0);
        ww(f, 0x1c, 0);
      }
      if (style_b) {
        for (unsigned k = 0; k < 16; k++) ww(f, (uint16_t)(0x0c + 2 * k), 0);
        ww(f, 0x1e, (uint16_t)(30 + rn(30)));
      }
      if (sweep) {
        ww(f, 2, me);
        ww(f, 0x62, 1);
        ww(f, 0xc, 0);
        ww(f, 0x28, 1);
        ww(f, 0x2a, 0);
      }
    }
    bool big_share = false;
    if (it % 5 == 2) { /* planet 0 with large resource dwords (a share above 0x4e20 = 20000 for the cap), the others small */
      /* a share (resource / 5) just above 20100 = 201 * 100 (the quotient 18:0x2732 gets changes) up to 20480 for half of the
       * worlds, so that the cap test (> 0x4e20 = 20000) and its neighbours 0x4e90 and 0x5000 decide what is queued */
      for (unsigned k = 0; k < 3; k++)
        wd(g_pl[0], (uint16_t)(0x1c + 4 * k), (it / 5) % 2 ? 100500 + rn(1900) : 100000 + rn(200000));
      wd(g_pl[0], 0x28, 0x2710 + 1 + rn(40000));
      wb(g_pl[0], 5, (uint8_t)(rb_(g_pl[0], 5) | 2));
      wb(g_pl[0], 0x2c, 2);
      wd(g_pl[0], 0x34, 0);
      DS_W(0x82) = (uint16_t)(0x79 + rn(100));
      wb(g_lst, 3, 0); /* no entry in the production list */
      for (unsigned i = 1; i < npl; i++)
        for (unsigned k = 0; k < 3; k++) wd(g_pl[i], (uint16_t)(0x1c + 4 * k), rn(1000));
      for (unsigned i = 0; i < npl; i++) { /* every planet close to the others */
        uint32_t v = (uint32_t)(10 + rn(30)) | ((uint32_t)(10 + rn(30)) << 16);
        memcpy(LINN((uint16_t)(0x31ba + 4 * i)), &v, 4);
      }
      big_share = true;
    }
    if (it % 3 == 1) { /* a player who has every tech: 2:0x50d8 answers 1 */
      uint8_t *row = (uint8_t *)LINN((uint16_t)(0x5a64 + 0xc0 * me));
      for (unsigned k = 0x1a; k < 0x20; k++) row[k] = 0x7f;
    }
    stubs_turn(npl, nfl);
    if (big_share) { /* 9:0x4f76 rates every planet at 10 or more */
      stub_clear(S_4F76);
      for (unsigned k = 0; k < 24; k++) {
        uint8_t pk[16] = {0};
        pk[0] = (uint8_t)(10 + rn(20));
        stub_push(S_4F76, (uint16_t)(10 + rn(20)), 0, pk);
      }
    }
    if (two_fleets && npl >= 1 && nfl >= 2) { /* planet 0 builds; CHOICE_0E is 5 and both fleets hold ships of design 5 */
      wb(g_pl[0], 5, (uint8_t)(rb_(g_pl[0], 5) | 2));
      wd(g_pl[0], 0x28, 0x400);
      wb(g_pl[0], 0x2c, 2);
      wd(g_pl[0], 0x34, 0);
      for (unsigned k = 0; k < 3; k++) wb(g_pl[0], (uint16_t)(9 + k), (uint8_t)(40 + rn(60)));
      DS_W(0x82) = (uint16_t)(0x15 + rn(0x60));
      ds_w32((uint16_t)(0x417a + 0x8d * 5 + 0x83), 0);
      for (unsigned f = 0; f < 2; f++) {
        ww(g_fleet[f], 2, DS_W(0x14c));
        ww(g_fleet[f], 6, 0);
        ww(g_fleet[f], (uint16_t)(0x0c + 2 * 5), (uint16_t)(1 + rn(30)));
      }
    }
    if (sweep || two_fleets) { /* the answer of 18:0x5d3c that fills BP-0x9c must not be 0xffff */
      uint8_t *e = (uint8_t *)LIN(g_stub_blk) + STUB_Q + (size_t)S_5D3C * STUB_QMAX * STUB_QENT + 1 * STUB_QENT;
      uint16_t v = 5;
      memcpy(e + 4, &v, 2);
    }
    if (g_trace) {
      for (unsigned i = 0; i < npl; i++)
        fprintf(stderr, "planet %u at %04x: flags %02x v28 %08x k2c %02x turn %04x\n", i, LO(g_pl[i]), rb_(g_pl[i], 5), (unsigned)(rw(g_pl[i], 0x28) | ((uint32_t)rw(g_pl[i], 0x2a) << 16)), rb_(g_pl[i], 0x2c), DS_W(0x82));
    }
    if (DIFF(20, 0x0000, FAR_CDECL, (uint16_t)rnd()) != DT_PASS) fprintf(stderr, "  ^ iteration %u\n", it);
  }
}

/* 21:0x1e2a is decompiled (src/seg21.c), but here it stays a test double: the double logged by the stub is the
 * expectation. The C version of the caller reaches it through rt_call_far, which would run the C function, so an
 * adapter makes rt_call_far run the stubbed code instead (docs/CONVENTIONS.md rule j2: the doubled callee is not
 * under test here; it is diffed in tests/scenarios/test_seg21.c). */
static uint32_t double_21_1e2a(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss;
  (void)sp;
  (void)r;
  return rt_call_orig(21, 0x1e2a, FAR_CDECL, 0);
}

int main(void) {
  g_only = getenv("SEG20_ONLY");
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
  g_cur = gblock(0x100);
  g_lst = gblock(0x100);
  g_aux = gblock(0x400);
  g_fblk = gblock(0x80 * NFL_MAX + 0x40);
  g_wpl = gblock(0x40 * NFL_MAX + 0x40);
  g_info = gblock(0x400);
  g_pln = gblock(0x400);
  g_plist = gblock(0x100);
  g_ptrs = gblock(4 * NFL_MAX + 0x40);
  if (want("2cbc")) t_2cbc();
  if (want("2286")) t_2286();
  if (want("2998")) t_2998();
  if (want("23ba")) t_23ba();
  if (want("2286")) t_2286_edges();
  if (want("23ba")) t_23ba_edges();
  if (want("2998")) t_2998_edges();
  if (want("1da0")) t_1da0();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
