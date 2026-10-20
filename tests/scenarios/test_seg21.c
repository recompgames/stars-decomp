/* Diff tests for decompiled segment 21 (src/seg21.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 21 is the older computer-player turn: 21:0x0000 is the turn itself, 21:0x123e, 0x1e8a and
 * 0x23d0 its big helpers, and the rest are filter callbacks and small helpers (see src/seg21.c).
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then snapshots
 * it and compares the original with the C version (return value, all memory outside the callee's own
 * frame, the ordered API call log, which here holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of
 * the callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See stub_handler for what a double does.
 * Run as ORIGINAL code in both runs (no double): 8:0x8730 (the random generator), 8:0xbc50 (squared
 * distance), 9:0x4f76 (the weapon power of a fleet), 7:0x01e4 (find a planet by id) and the other
 * data readers listed in docs/functions.csv notes. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/kernel_thunks.h"
#include "runtime/kernel.h"
#include "src/stars.h"
#include "src/crt34.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL21 MEM_SEL_SEG(21)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define UNUSED __attribute__((unused))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static UNUSED void ds_ww(uint16_t off, uint16_t v) { memcpy(LINN(off), &v, 2); }
static UNUSED void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }
static UNUSED uint32_t ds_r32(uint16_t off) {
  uint32_t v;
  memcpy(&v, LINN(off), 4);
  return v;
}
static UNUSED uint16_t rw(uint32_t fp, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 2);
  return v;
}
static UNUSED void ww(uint32_t fp, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 2); }
static UNUSED uint32_t rd32f(uint32_t fp, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 4);
  return v;
}
static UNUSED void wd(uint32_t fp, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 4); }
static UNUSED uint8_t rb(uint32_t fp, uint16_t off) { return *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))); }
static UNUSED void wb(uint32_t fp, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))) = v; }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static UNUSED uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static UNUSED uint32_t rn(uint32_t n) { return rnd() % n; }
/* true with probability num/den */
static UNUSED bool chance(uint32_t num, uint32_t den) { return rn(den) < num; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static UNUSED uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}
/* Random bytes into a block. */
static UNUSED void fill_rand(uint32_t fp, uint16_t n) {
  uint8_t *p = LIN(fp);
  for (uint16_t i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

/* ---- the test doubles (rule j2) ---- */

/* A double is a logged thunk written over the first 5 bytes of the callee in the code segment image
 * (JMP FAR to an import registered with harness_add_import), so the same bytes serve the ORIGINAL
 * run and the C version's CALL_ORIG. Each call is an entry of the API log (argument words as pushed
 * and the return value) and the handler appends a hash of the state the call could observe to a log
 * in the stub block: all of DGROUP below the stack, the registered watch blocks (the far blocks the
 * scenario built), the record the call's far-pointer argument names and the local buffer a near-pointer
 * argument names. The log is compared as arena memory, so a call made before or after a write it
 * should follow (or precede) shows as a difference. Returns come from a per-callee queue in the arena
 * (rewound with the snapshot between the two runs); a double can also write queued bytes to up to two
 * out-buffers named by near-pointer arguments. */
#define STUB_BLKSZ 0xE000
#define STUB_DEF 0x10      /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200    /* queue length of id (word) */
#define STUB_QIDX 0x280    /* queue index of id (word) */
#define STUB_Q 0x400       /* queue of id: STUB_QMAX entries of STUB_QENT bytes (ax, dx, 16 + 16 poke bytes) */
#define STUB_QENT 36
#define STUB_QMAX 32
#define STUB_LOG0 0xC000
static uint32_t g_stub_blk;
static unsigned g_ssn_2664 = 18; /* bytes of the 18-byte order record 18:0x2664 hashes (see t_0000) */

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs;      /* argument words */
  int8_t farw;        /* argument word index of a far pointer to a record whose farn bytes are hashed, -1 none */
  uint8_t farn;
  int8_t ssw;         /* argument word index of a near pointer (SS) whose ssn bytes are hashed, -1 none */
  uint8_t ssn;
  int8_t p1w;         /* argument word index of a near pointer (SS) the queued bytes 0..p1n are written to */
  uint8_t p1n;
  int8_t p2w;         /* the same for the queued bytes 16..16+p2n */
  uint8_t p2n;
} StubCfg;

enum {
  S_18_0000, S_18_0630, S_18_0750, S_18_0864, S_18_098A, S_18_1434, S_18_2664, S_18_2732, S_18_28FC,
  S_18_3558, S_18_3600, S_18_36AA, S_18_3854, S_18_3922, S_18_39F6, S_18_3AD8, S_18_3BB4, S_18_4510,
  S_18_457A, S_18_50A0, S_18_50C8, S_18_59BA, S_18_5BAE, S_18_5D3C, S_18_5E82, S_18_5FE6,
  S_26_0100, S_26_0810, S_26_21E4, S_9_2A74, S_7_153E, S_7_0358, S_7_294E, S_10_678C,
  S_9_4A62, S_9_4B0C, S_9_4C64, S_9_4B8E, S_9_474C, S_9_462A, S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    /* name,       seg off  nargs farw farn ssw ssn p1w p1n p2w p2n */
    [S_18_0000] = {"18:0000", 18, 0x0000, 4, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_0630] = {"18:0630", 18, 0x0630, 2, -1, 0, 0, 0x8d, -1, 0, -1, 0},
    [S_18_0750] = {"18:0750", 18, 0x0750, 6, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_0864] = {"18:0864", 18, 0x0864, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_098A] = {"18:098a", 18, 0x098a, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_1434] = {"18:1434", 18, 0x1434, 7, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_2664] = {"18:2664", 18, 0x2664, 4, 0, 0x80, 2, 18, -1, 0, -1, 0},
    [S_18_2732] = {"18:2732", 18, 0x2732, 4, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_28FC] = {"18:28fc", 18, 0x28fc, 5, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_3558] = {"18:3558", 18, 0x3558, 3, 0, 0x80, -1, 0, 2, 16, -1, 0},
    [S_18_3600] = {"18:3600", 18, 0x3600, 3, 0, 0x80, -1, 0, 2, 16, -1, 0},
    [S_18_36AA] = {"18:36aa", 18, 0x36aa, 1, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_3854] = {"18:3854", 18, 0x3854, 4, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_3922] = {"18:3922", 18, 0x3922, 4, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_39F6] = {"18:39f6", 18, 0x39f6, 4, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_3AD8] = {"18:3ad8", 18, 0x3ad8, 4, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_3BB4] = {"18:3bb4", 18, 0x3bb4, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_4510] = {"18:4510", 18, 0x4510, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_457A] = {"18:457a", 18, 0x457a, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_50A0] = {"18:50a0", 18, 0x50a0, 0, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_50C8] = {"18:50c8", 18, 0x50c8, 0, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_59BA] = {"18:59ba", 18, 0x59ba, 5, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_18_5BAE] = {"18:5bae", 18, 0x5bae, 1, -1, 0, 0, 16, -1, 0, -1, 0},
    [S_18_5D3C] = {"18:5d3c", 18, 0x5d3c, 5, -1, 0, -1, 0, 3, 2, 4, 16},
    [S_18_5E82] = {"18:5e82", 18, 0x5e82, 8, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_18_5FE6] = {"18:5fe6", 18, 0x5fe6, 2, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_26_0100] = {"26:0100", 26, 0x0100, 1, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_26_0810] = {"26:0810", 26, 0x0810, 1, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_26_21E4] = {"26:21e4", 26, 0x21e4, 7, 0, 0x80, -1, 0, 4, 16, -1, 0},
    [S_9_2A74] = {"9:2a74", 9, 0x2a74, 2, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_7_153E] = {"7:153e", 7, 0x153e, 2, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_7_0358] = {"7:0358", 7, 0x0358, 2, -1, 0, -1, 0, -1, 0, -1, 0},
    [S_7_294E] = {"7:294e", 7, 0x294e, 4, -1, 0, -1, 0, 3, 16, -1, 0},
    [S_10_678C] = {"10:678c", 10, 0x678c, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_4A62] = {"9:4a62", 9, 0x4a62, 4, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_4B0C] = {"9:4b0c", 9, 0x4b0c, 2, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_4C64] = {"9:4c64", 9, 0x4c64, 4, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_4B8E] = {"9:4b8e", 9, 0x4b8e, 2, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_474C] = {"9:474c", 9, 0x474c, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
    [S_9_462A] = {"9:462a", 9, 0x462a, 3, 0, 0x80, -1, 0, -1, 0, -1, 0},
};

#define NWATCH 12
static struct { uint32_t fp; uint16_t len; } g_watch[NWATCH];
static unsigned g_nwatch;
static UNUSED void watch_clear(void) { g_nwatch = 0; }
static UNUSED void watch_add(uint32_t fp, uint16_t len) {
  CHECK(g_nwatch < NWATCH);
  if (g_nwatch < NWATCH) {
    g_watch[g_nwatch].fp = fp;
    g_watch[g_nwatch].len = len;
    g_nwatch++;
  }
}

static uint64_t hmix(uint64_t h, const void *p, size_t n) {
  const uint8_t *b = p;
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, b + i, 8);
    h = (h ^ w) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
  }
  for (; i < n; i++) {
    h = (h ^ b[i]) * 0x100000001B3ull;
  }
  return h;
}
static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  uint64_t h = 1469598103934665603ull;
  h = hmix(h, LINN(0), harness_stack_limit());
  for (unsigned i = 0; i < g_nwatch; i++) {
    if (mem_check(g_watch[i].fp, g_watch[i].len)) h = hmix(h, LIN(g_watch[i].fp), g_watch[i].len);
  }
  if (cfg->farw >= 0) {
    uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
    if (fp != 0 && mem_check(fp, cfg->farn)) h = hmix(h, LIN(fp), cfg->farn);
  }
  if (cfg->ssw >= 0 && cfg->ssn) {
    uint16_t p = hcall_word(c, (unsigned)cfg->ssw);
    unsigned ssn = c->ordinal == S_18_2664 ? g_ssn_2664 : cfg->ssn;
    h = hmix(h, LIN(MK_FP(c->regs->ss, p)), ssn);
  }
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ || at < STUB_LOG0) at = STUB_LOG0;
  uint32_t h32 = (uint32_t)(h ^ (h >> 32));
  memcpy(blk + at, &h32, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
  if (qidx < qlen) {
    const uint8_t *e = blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * STUB_QENT + (size_t)qidx * STUB_QENT;
    c->ret = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
    if (cfg->p1w >= 0 && cfg->p1n) {
      uint16_t p = hcall_word(c, (unsigned)cfg->p1w);
      if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), e + 4, cfg->p1n);
    }
    if (cfg->p2w >= 0 && cfg->p2n) {
      uint16_t p = hcall_word(c, (unsigned)cfg->p2w);
      if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), e + 20, cfg->p2n);
    }
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
}
static UNUSED void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
static void stub_clear(unsigned id) {
  uint8_t *blk = LIN(g_stub_blk);
  memset(blk + STUB_QLEN + 2 * id, 0, 2);
  memset(blk + STUB_QIDX + 2 * id, 0, 2);
  stub_def(id, 0, 0);
}
static UNUSED void stubs_clear_all(void) {
  for (unsigned i = 0; i < S_NSTUB; i++) stub_clear(i);
  uint16_t at = STUB_LOG0;
  memcpy(LIN(g_stub_blk), &at, 2);
}
/* Queues one return (dx:ax) and the bytes written to the out-buffers (either may be NULL). */
static UNUSED void stub_push(unsigned id, uint16_t ax, uint16_t dx, const void *p1, const void *p2) {
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
  memset(e + 4, 0, 32);
  if (p1) memcpy(e + 4, p1, 16);
  if (p2) memcpy(e + 20, p2, 16);
  n++;
  memcpy(blk + STUB_QLEN + 2 * id, &n, 2);
}
static uint8_t g_stub_jmp[S_NSTUB][5];   /* the JMP FAR written over the callee */
static uint8_t g_stub_orig[S_NSTUB][5];  /* the bytes it replaces */
/* Switches the double of callee id on or off (the callee's own code is back when it is off): 9:0x474c and
 * 9:0x462a run as original code in some tests and are doubled in others. */
static UNUSED void stub_enable(unsigned id, bool on) {
  const StubCfg *cfg = &g_stubs[id];
  memcpy(LIN(MK_FP(MEM_SEL_SEG(cfg->seg), cfg->off)), on ? g_stub_jmp[id] : g_stub_orig[id], 5);
}
static UNUSED void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(g_stub_jmp[i], c, sizeof c);
    memcpy(g_stub_orig[i], LIN(MK_FP(MEM_SEL_SEG(cfg->seg), cfg->off)), sizeof c);
    if (i != S_9_474C && i != S_9_462A) stub_enable(i, true);
  }
  stubs_clear_all();
}

/* ---- the common scenario world ---- */

#define NPL 48                 /* planet ids 0..NPL-1 are valid in the DS tables */
static uint32_t g_tab;         /* DS:0x1604: the per-planet flag table block */
static uint32_t g_dsg[16];     /* per-player design blocks (DS:0x10c + 4 * player): 16 designs of 0x8d bytes */
static uint32_t g_recs;        /* a block of records the callbacks look at */
static uint32_t g_tab3;        /* the dword 21:0x29da and 21:0x123e multiply the pow result by */

/* The current player, the planet count, the flag table (two byte pairs per planet id) and the planet
 * coordinate table; the squared distance (8:0xbc50) of two planets then spans the 400 * 400 threshold. */
static void world_tables(uint16_t me, uint16_t nplan) {
  DS_W(0x014c) = me;
  DS_W(0x007a) = nplan;
  uint16_t off = (uint16_t)(chance(1, 2) ? 0 : 2 * rn(16));
  ds_w32(0x1604, MK_FP(HI(g_tab), off));
  fill_rand(MK_FP(HI(g_tab), 0), 0x400);
  for (unsigned i = 0; i < 0x400; i++) {
    if (chance(1, 3)) *((uint8_t *)LIN(MK_FP(HI(g_tab), (uint16_t)i))) = 0;
  }
  for (unsigned i = 0; i < NPL; i++) {
    int32_t x = (int32_t)rn(900) + 1000, y = (int32_t)rn(900) + 1000;
    ds_w32((uint16_t)(0x31ba + 4 * i), (uint32_t)(((uint32_t)(uint16_t)y << 16) | (uint16_t)x));
  }
  /* the third table 21:0x29da multiplies by: DS:0x1604 + 4 * nplan + 2 */
  g_tab3 = MK_FP(HI(g_tab), (uint16_t)(off + 4 * nplan + 2));
  wd(g_tab3, 0, (uint32_t)((int32_t)rn(5000) - 500));
}

/* Planet id `id` at (x, y). */
static UNUSED void set_xy(unsigned id, int32_t x, int32_t y) {
  ds_w32((uint16_t)(0x31ba + 4 * id), (uint32_t)(((uint32_t)(uint16_t)y << 16) | (uint16_t)x));
}

/* The design table of every player: byte +0x7b of a design is the hull class (7 = the ones the
 * weapon power counts for other players), +0x7a the slot count, slots of four bytes from +0x3a. */
static void world_designs(void) {
  for (unsigned p = 0; p < 16; p++) {
    if (!g_dsg[p]) g_dsg[p] = gblock(0x8d0);
    uint32_t d = g_dsg[p];
    fill_rand(d, 0x8d0);
    for (unsigned k = 0; k < 16; k++) {
      uint16_t o = (uint16_t)(0x8d * k);
      wb(d, (uint16_t)(o + 0x7b), (uint8_t)(chance(1, 2) ? 7 : rn(16)));
      wb(d, (uint16_t)(o + 0x7a), (uint8_t)(chance(1, 4) ? 0 : 1 + rn(15)));
      for (unsigned s = 0; s < 16; s++) {
        ww(d, (uint16_t)(o + 0x3a + 4 * s), (uint16_t)(chance(2, 3) ? 0x200 : rnd()));
        ww(d, (uint16_t)(o + 0x3c + 4 * s), (uint16_t)((chance(3, 4) ? 1 + rn(255) : 0) << 8 | (chance(1, 8) ? rn(0x20) : 5 + rn(12))));
      }
    }
    ds_w32((uint16_t)(0x10c + 4 * p), d);
  }
}

/* The player records at DS:0x5a64 + 0xc0 * p: the three habitability triples (ideal at +0x10, low at
 * +0x13, high at +0x16, signed bytes) 9:0x474c reads, kept valid (low < ideal < high) so that the
 * original code it runs does not divide by zero; the attribute 28:0x21ee reads (+0x4c). */
static UNUSED void world_players(void) {
  for (unsigned p = 0; p < 16; p++) {
    uint16_t rec = (uint16_t)(0x5a64 + 0xc0 * p);
    for (unsigned i = 0; i < 3; i++) {
      int c = (int)rn(60) - 30, r1 = 1 + (int)rn(40), r2 = 1 + (int)rn(40);
      if (chance(1, 12)) r2 = -1 - (int)rn(8); /* a negative high bound: the "never lives here" branch */
      DS_B((uint16_t)(rec + 0x10 + i)) = (uint8_t)(int8_t)c;
      DS_B((uint16_t)(rec + 0x13 + i)) = (uint8_t)(int8_t)(c - r1);
      DS_B((uint16_t)(rec + 0x16 + i)) = (uint8_t)(int8_t)(c + r2);
    }
    DS_B((uint16_t)(rec + 0x4c)) = (uint8_t)(chance(1, 8) ? 8 : rn(8));
  }
}

/* A record at slot k of the records block: planet or fleet header words (id, owner), byte +5 flags. */
static uint32_t rec_at(unsigned k) { return MK_FP(HI(g_recs), (uint16_t)(0x10 + 0x80 * k)); }

/* ---- 21:0x2bd8 and 21:0x2858: leaf filter callbacks ---- */

static void t_2bd8(void) {
  for (unsigned it = 0; it < 60; it++) {
    g_rng = 0x2bd80001u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    DS_W(0x014c) = me;
    uint32_t r = rec_at(it % 4);
    fill_rand(r, 0x40);
    static const uint16_t own[] = {0, 1, 2, 3, 0xffff, 0x7fff, 0x8000};
    uint16_t o = (it % 3 == 0) ? me : (it % 3 == 1) ? 0xffff : own[rn(7)];
    ww(r, 2, o);
    DIFF(21, 0x2bd8, FAR_CDECL, LO(rec_at(0)), HI(rec_at(0)), LO(r), HI(r));
  }
  /* the current player itself is 0xffff (a neutral viewer): owner 0xffff gives 0 as well */
  DS_W(0x014c) = 0xffff;
  ww(rec_at(1), 2, 0xffff);
  DIFF(21, 0x2bd8, FAR_CDECL, 0, 0, LO(rec_at(1)), HI(rec_at(1)));
  ww(rec_at(1), 2, 3);
  DIFF(21, 0x2bd8, FAR_CDECL, 0, 0, LO(rec_at(1)), HI(rec_at(1)));
}

static void t_2858(void) {
  static const uint32_t thr[] = {0, 0x897, 0x898, 0x899, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x1000};
  for (unsigned it = 0; it < 200; it++) {
    g_rng = 0x28580001u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    DS_W(0x014c) = me;
    uint32_t r = rec_at(it % 4);
    fill_rand(r, 0x40);
    ww(r, 2, chance(3, 4) ? me : (uint16_t)(me + 1));
    wd(r, 0x28, chance(3, 4) ? thr[rn(8)] : rnd());
    wb(r, 5, (uint8_t)(chance(1, 2) ? 2 | rn(256) : rn(256) & 0xfd));
    DIFF(21, 0x2858, FAR_CDECL, LO(rec_at(0)), HI(rec_at(0)), LO(r), HI(r));
  }
}

/* ---- 21:0x272c and 21:0x27ba: callbacks with a distance limit ---- */

static void rec_planetlike(uint32_t r, uint16_t me, uint16_t id) {
  fill_rand(r, 0x40);
  ww(r, 0, id);
  ww(r, 2, chance(4, 5) ? me : (uint16_t)(me + 1));
  static const uint32_t v[] = {0, 199, 200, 201, 998, 999, 1000, 1001, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x80, 0x10000};
  wd(r, 0x28, chance(3, 4) ? v[rn(13)] : rnd());
}

static void t_272c_27ba(uint16_t off) {
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x272c0001u + off * 7u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)rn(NPL / 2));
    uint32_t a = rec_at(0), b = rec_at(1 + it % 3);
    rec_planetlike(a, me, (uint16_t)rn(NPL));
    rec_planetlike(b, me, (uint16_t)rn(NPL));
    if (off == 0x272c) DIFF(21, 0x272c, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
    else DIFF(21, 0x27ba, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
  }
  /* The exact boundary of the squared distance: the code returns 1 up to 400 * 400 = 160000 (0x27100)
   * and 0 from 160001, so the planets are placed at dx, dy with dx * dx + dy * dy = 160000 and 160001
   * (and a few just below and above), along both axes and in both directions. Every other condition passes: b is owned by the current player,
   * its dword +0x28 is 0 and its flag byte (tab[2 * id], tab = DS:0x1604 + 2 * (DS:0x7a + 1)) is 0. */
  static const int32_t pts[][2] = {{400, 0}, {-400, 0}, {0, 400}, {0, -400}, {400, 1}, {-400, -1}, {1, 400}, {-1, -400},
                                   {399, 20}, {399, 21}, {400, -1}, {0, 399}, {399, 0}, {0, 401}, {401, 0}};
  for (unsigned k = 0; k < sizeof pts / sizeof pts[0]; k++) {
    g_rng = 0x272c1001u + off * 7u + 0x9e3779b9u * (k + 1);
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)(2 + rn(NPL / 2)));
    uint32_t a = rec_at(0), b = rec_at(1);
    rec_planetlike(a, me, 0);
    rec_planetlike(b, me, 1);
    ww(b, 2, me);
    wd(b, 0x28, 0);
    uint32_t t1 = ds_r32(0x1604);
    wb(MK_FP(HI(t1), (uint16_t)(LO(t1) + 2 * (DS_W(0x007a) + 1) + 2 * 1)), 0, 0);
    set_xy(0, 1000, 1000);
    set_xy(1, 1000 + pts[k][0], 1000 + pts[k][1]);
    if (off == 0x272c) DIFF(21, 0x272c, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
    else DIFF(21, 0x27ba, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
  }
}

/* ---- 21:0x2882: a fleet-vs-fleet filter with the weapon power and x87 ---- */

/* The ORIGINAL return AX of one run (difftest_run does not record into the summary). */
static UNUSED uint16_t orig_ax(uint16_t seg, uint16_t off, const uint16_t *args, int nargs) {
  DtResult r = difftest_run(seg, off, FAR_CDECL, args, nargs, NULL);
  return (uint16_t)r.ret;
}

static void t_2882(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x28820001u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)rn(NPL / 2));
    world_designs();
    uint32_t a = rec_at(0), b = rec_at(1);
    fill_rand(a, 0x40);
    fill_rand(b, 0x40);
    static const uint32_t cg[] = {0, 0x2bb, 0x2bc, 0x2bd, 0x5000, 0xffffffffu, 0x80000000u};
    for (unsigned k = 0; k < 3; k++) wd(a, (uint16_t)(0x1c + 4 * k), chance(3, 4) ? cg[rn(7)] : rnd());
    ww(a, 0, (uint16_t)rn(NPL));
    ww(a, 2, (uint16_t)rn(16));
    wb(a, 0x2c, (uint8_t)rn(16));
    ww(b, 0, (uint16_t)rn(NPL));
    ww(b, 2, chance(7, 8) ? me : (uint16_t)rn(16));
    wb(b, 5, (uint8_t)(chance(7, 8) ? (rnd() | 2) : (rnd() & 0xfd)));
    wb(b, 0x2c, (uint8_t)rn(16));
    DIFF(21, 0x2882, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
  }
  /* The exact FICOM boundary t * t == distance (the filter fails only when t * t < distance, an
   * exact tie passes). With K = 1.0 the double t is the integer n * n, so with the planets on one row
   * the squared distance is d * d and the result is 1 exactly while n * n >= d. For scenarios that pass
   * every other condition, find with the ORIGINAL the largest d with result 1 (the one with n * n == d)
   * and test d - 1, d (the tie, expected 1) and d + 1 (expected 0), as t_29da does for its flip. */
  uint8_t k_saved[8];
  memcpy(k_saved, LINN(0x20bc), 8);
  const uint64_t k_one = 0x3ff0000000000000ull;
  memcpy(LINN(0x20bc), &k_one, 8);
  unsigned ties = 0;
  for (unsigned it = 0; it < 400 && ties < 12; it++) {
    g_rng = 0x28820101u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)(2 + rn(NPL / 2)));
    world_designs();
    uint32_t a = rec_at(0), b = rec_at(1);
    fill_rand(a, 0x40);
    fill_rand(b, 0x40);
    for (unsigned k = 0; k < 3; k++) wd(a, (uint16_t)(0x1c + 4 * k), 0x5000);
    ww(a, 0, 0);
    ww(a, 2, (uint16_t)rn(16));
    wb(a, 0x2c, (uint8_t)rn(16));
    ww(b, 0, 1);
    ww(b, 2, me);
    wb(b, 5, (uint8_t)(rnd() | 2));
    wb(b, 0x2c, (uint8_t)rn(16));
    set_xy(0, 0, 1000);
    const uint16_t args[] = {LO(a), HI(a), LO(b), HI(b)};
    set_xy(1, 0, 1000);
    if (orig_ax(21, 0x2882, args, 4) != 1) continue; /* a condition other than the distance fails */
    int32_t lo = 0, hi = 0x7fff;
    set_xy(1, hi, 1000);
    if (orig_ax(21, 0x2882, args, 4) != 0) continue; /* n * n is beyond the search range */
    while (lo + 1 < hi) { /* lo: result 1, hi: result 0 */
      int32_t mid = lo + (hi - lo) / 2;
      set_xy(1, mid, 1000);
      if (orig_ax(21, 0x2882, args, 4) == 1) lo = mid;
      else hi = mid;
    }
    for (int32_t d = lo - 1; d <= lo + 1; d++) {
      if (d < 0) continue;
      set_xy(1, d, 1000);
      DIFF(21, 0x2882, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
    }
    ties++;
  }
  memcpy(LINN(0x20bc), k_saved, 8);
  if (ties == 0) fprintf(stderr, "t_2882: no FICOM tie scenario was found\n");
}

/* ---- 21:0x29da: a fleet-vs-fleet filter with the weapon power, sqrt, pow and ftol ---- */

static void t_29da(void) {
  static const int32_t spread[] = {0, 2, 5, 12, 30, 60, 120};
  static const uint16_t lowbits[] = {0, 1, 224, 225, 226, 0xfff, 0x800, 100};
  for (unsigned it = 0; it < 1000; it++) {
    g_rng = 0x29da0001u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)rn(NPL / 2));
    world_designs();
    unsigned ida = rn(NPL), idb = chance(1, 8) ? ida : rn(NPL);
    int32_t s = spread[rn(7)];
    set_xy(ida, 1000, 1000);
    set_xy(idb, 1000 + (int32_t)rn((uint32_t)(2 * s + 1)) - s, 1000 + (int32_t)rn((uint32_t)(2 * s + 1)) - s);
    uint32_t a = rec_at(0), b = rec_at(1);
    fill_rand(a, 0x40);
    fill_rand(b, 0x40);
    ww(a, 0, (uint16_t)ida);
    ww(a, 2, (uint16_t)rn(16));
    wb(a, 0x2c, (uint8_t)rn(16));
    ww(b, 0, (uint16_t)idb);
    uint16_t bo = chance(1, 10) ? me : chance(1, 10) ? 0xffff : (uint16_t)rn(16);
    ww(b, 2, bo);
    wb(b, 5, (uint8_t)(chance(3, 4) ? (rnd() | 2) : (rnd() & 0xfd)));
    wb(b, 0x2c, (uint8_t)rn(16));
    ww(b, 0x12, (uint16_t)(chance(1, 8) ? (rnd() & 0xf000) : chance(1, 4) ? ((rnd() & 0xf000) | lowbits[rn(8)]) : chance(1, 2) ? ((rnd() & 0xf000) | rn(300)) : rnd()));
    /* the player record byte 28:0x21ee reads (record + 0x4c): 8 sometimes */
    for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5a64 + 0xc0 * p + 0x4c)) = (uint8_t)(chance(1, 8) ? 8 : rn(8));
    if (it % 3 == 0) {
      /* a scenario that reaches the pow stage: another owner, no flag bits, a hull class 7 design,
       * a player attribute other than 8, and the planets close to each other */
      bo = (uint16_t)((me + 1 + rn(7)) & 15);
      ww(b, 2, bo);
      wb(b, 5, (uint8_t)(rnd() | 2));
      uint32_t t1 = ds_r32(0x1604);
      wb(MK_FP(HI(t1), (uint16_t)(LO(t1) + 2)), (uint16_t)(2 * idb), 0);
      uint8_t sel = (uint8_t)(rnd() & 0xf);
      wb(b, 0x2c, sel);
      wb(g_dsg[bo], (uint16_t)(0x8d * (sel & 0xf) + 0x7b), 7);
      DS_B((uint16_t)(0x5a64 + 0xc0 * bo + 0x4c)) = (uint8_t)rn(8);
      set_xy(idb, 1000 + (int32_t)rn(25) - 12, 1000 + (int32_t)rn(25) - 12);
      set_xy(ida, 1000, 1000);
    }
    /* a multiplier that makes the result 1 about half of the time */
    wd(g_tab3, 0, (uint32_t)(int32_t)(chance(1, 3) ? rn(5000) : rn(1u << (4 + rn(18)))));
    const uint16_t args[] = {LO(a), HI(a), LO(b), HI(b)};
    DIFF(21, 0x29da, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
    if (it % 20 == 0) {
      /* the result is 1 once V = ftol(pow * N) reaches the other side X: find the smallest N that flips it
       * (V is monotone in N) and test the three multipliers around the flip */
      uint32_t lo = 0, hi = 1u << 26;
      wd(g_tab3, 0, hi);
      if (orig_ax(21, 0x29da, args, 4) != 1) continue;
      while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        wd(g_tab3, 0, mid);
        if (orig_ax(21, 0x29da, args, 4) == 1) hi = mid;
        else lo = mid + 1;
      }
      for (int32_t d = -1; d <= 1; d++) {
        wd(g_tab3, 0, (uint32_t)((int32_t)lo + d));
        DIFF(21, 0x29da, FAR_CDECL, LO(a), HI(a), LO(b), HI(b));
      }
    }
  }
}

/* ---- 21:0x1d76: does the fleet's cargo differ enough from its capacity ---- */

static void t_1d76(void) {
  static const int32_t dv[] = {-1, 0, 1, 0xa8, 0xa9, 0xaa, 0xab, 0x7fffffff, (int32_t)0x80000000, -0xaa, 300, 1000};
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x1d760001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint32_t fl = rec_at(it % 4);
    fill_rand(fl, 0x80);
    uint32_t A[4], B[4];
    for (unsigned k = 0; k < 4; k++) {
      B[k] = rnd();
      int32_t d = chance(3, 4) ? dv[rn(12)] : (int32_t)rnd();
      if (it % 5 == 0) d = (int32_t)rn(400) - 100;
      A[k] = B[k] + (uint32_t)d;
    }
    uint8_t pa[16], pb[16];
    memcpy(pa, A, 16);
    memcpy(pb, B, 16);
    stub_push(S_18_3558, (uint16_t)rnd(), 0, pa, NULL);
    stub_push(S_18_3600, (uint16_t)rnd(), 0, pb, NULL);
    stub_def(S_18_2732, (uint16_t)rnd(), 0);
    DIFF(21, 0x1d76, FAR_CDECL, LO(fl), HI(fl));
  }
}

/* ---- 21:0x2bf8: queue one order for a fleet that is strong enough ---- */

/* The ORIGINAL DX:AX of one run of any function (difftest_run does not record into the summary). */
static UNUSED uint32_t orig_ret(uint16_t seg, uint16_t off, const uint16_t *args, int nargs) {
  DtResult r = difftest_run(seg, off, FAR_CDECL, args, nargs, NULL);
  return r.ret;
}

static void t_2bf8(void) {
  /* 18:0x50a0 and 18:0x50c8 (40 and 37 bytes, no calls) only read the current player's design block
   * through DS:0x10c + 4 * DS:0x14c, so they run as ORIGINAL code here (no double); world_designs builds
   * the block. They stay doubled for 21:0x0000, whose test does not build the block. */
  stub_enable(S_18_50A0, false);
  stub_enable(S_18_50C8, false);
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x2bf80001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, (uint16_t)rn(NPL / 2));
    world_players();
    world_designs();
    uint32_t r = rec_at(it % 4);
    fill_rand(r, 0x80);
    wb(r, 5, (uint8_t)(chance(1, 3) ? rnd() | 2 : rnd() & 0xfd));
    static const uint32_t v[] = {0, 0x1f3, 0x1f4, 0x1f5, 0xffffffffu, 0x80000000u, 5000};
    wd(r, 0x28, chance(3, 4) ? v[rn(7)] : rnd());
    for (unsigned k = 0; k < 3; k++) wb(r, (uint16_t)(0x9 + k), (uint8_t)(chance(1, 2) ? 0xf + rn(3) - 1 : rn(256)));
    for (unsigned k = 0; k < 3; k++) wb(r, (uint16_t)(0xc + k), (uint8_t)(int8_t)((int)rn(100) - 50));
    DIFF(21, 0x2bf8, FAR_CDECL, LO(r), HI(r));
    if (it % 40 == 0) {
      /* the fleet's score (9:0x474c) is compared with 15: find the environment byte that gives 14, 15, 16 */
      wb(r, 5, (uint8_t)(rb(r, 5) & 0xfd));
      wd(r, 0x28, 0x1f4 + rn(3));
      for (int e = -60; e <= 60; e++) {
        wb(r, 0xc, (uint8_t)(int8_t)e);
        const uint16_t a474[] = {LO(r), HI(r), DS_W(0x014c)};
        int16_t sc = (int16_t)orig_ret(9, 0x474c, a474, 3);
        if (sc >= 14 && sc <= 16) DIFF(21, 0x2bf8, FAR_CDECL, LO(r), HI(r));
      }
    }
  }
  stub_enable(S_18_50A0, true);
  stub_enable(S_18_50C8, true);
}

/* ---- 21:0x30c2: pick one of three target planets for a fleet and queue its orders ---- */

/* The random generator state (8:0x8730 reads and writes the dwords at DS:0x151e and DS:0x1522). */
static UNUSED void rng_state(void) {
  ds_w32(0x151e, rnd() | 1);
  ds_w32(0x1522, rnd() | 1);
}
/* A model of 8:0x8730 (random(n)), so that a scenario can pick a generator state whose next draws are
 * the values it wants (the boundaries of the thresholds they are compared with). The model is checked
 * against the ORIGINAL code in main. */
static uint16_t rng_step(uint32_t *s1, uint32_t *s2, int16_t n) {
  int32_t b = (int32_t)*s1, c = (int32_t)*s2;
  int32_t q1 = b / -0xd1a4;
  int32_t la = (int32_t)((uint32_t)q1 * 0x7fffffabu + (uint32_t)b * 0x9c4eu);
  if (la < 0) la = (int32_t)((uint32_t)la + 0x7fffffabu);
  int32_t q2 = c / -0xce26;
  int32_t lb = (int32_t)((uint32_t)q2 * 0x7fffff07u + (uint32_t)c * 0x9ef4u);
  if (lb < 0) lb = (int32_t)((uint32_t)lb + 0x7fffff07u);
  int32_t le = (int32_t)((uint32_t)la - (uint32_t)lb);
  if (le < 1) le = (int32_t)((uint32_t)le + 0x7fffffaau);
  *s1 = (uint32_t)la;
  *s2 = (uint32_t)lb;
  if (n < 1) return 0;
  return (uint16_t)((uint32_t)le % (uint32_t)n);
}
/* Sets the generator state so that the next k draws random(n[i]) give v[i] (v[i] < 0: any value). */
static UNUSED void rng_find(const int16_t *n, const int *v, unsigned k) {
  for (unsigned tries = 0; tries < 20000000u; tries++) {
    uint32_t s1 = rnd() | 1, s2 = rnd() | 1, a = s1, b = s2;
    bool ok = true;
    for (unsigned i = 0; i < k && ok; i++) {
      uint16_t r = rng_step(&a, &b, n[i]);
      if (v[i] >= 0 && r != v[i]) ok = false;
    }
    if (ok) {
      ds_w32(0x151e, s1);
      ds_w32(0x1522, s2);
      return;
    }
  }
  CHECK(!"rng_find: no state");
}

static void t_30c2(void) {
  static const int16_t pick[] = {-1, 0, 1, 5, 2};
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x30c20001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    rng_state();
    DS_W(0x014c) = (uint16_t)rn(8);
    uint32_t fl = rec_at(it % 4);
    fill_rand(fl, 0x80);
    /* the readers 9:0x4a62 - 9:0x4b0c and 9:0x4c64 - 9:0x4b8e: differences around 100, some negative */
    static const int16_t rv[] = {0, 50, 99, 100, 101, 150, 300, -5};
    for (unsigned k = 0; k < 2; k++) {
      int16_t a = rv[rn(8)], b = chance(1, 2) ? (int16_t)(a - rv[rn(8)]) : (int16_t)rn(120);
      stub_push(S_9_4A62, (uint16_t)a, 0, NULL, NULL);
      stub_push(S_9_4B0C, (uint16_t)(chance(1, 3) ? a + 1 : b), 0, NULL, NULL);
      stub_push(S_9_4C64, (uint16_t)a, 0, NULL, NULL);
      stub_push(S_9_4B8E, (uint16_t)(chance(1, 3) ? a + 1 : b), 0, NULL, NULL);
    }
    uint16_t c = (uint16_t)(chance(1, 4) ? 0xffff : pick[rn(5)]);
    bool want_rng = chance(2, 3);
    uint16_t d = (uint16_t)(chance(1, 3) ? 0xffff : rn(40));
    uint16_t e = (uint16_t)(chance(1, 3) ? 0xffff : rn(40));
    /* the bytes 21:0x30c2 tests: +0x4544, +0x45d1, +0x465e, +0x46eb of the record 0x234 * c */
    if (c != 0xffff && (int16_t)c >= 0 && (int16_t)c < 8) {
      static const uint16_t offs[] = {0x4544, 0x45d1, 0x465e, 0x46eb};
      for (unsigned k = 0; k < 4; k++) DS_B((uint16_t)(offs[k] + 0x234 * c)) = (uint8_t)(chance(1, 2) ? 2 : rnd() & 0xfd);
    }
    if (want_rng) {
      /* draws: r1 against 0x32 / 0x19 (the planet ids), r2 against the threshold 0x3c / 0x5a, then the two
       * draws against 0x4b and 0x32 when their bytes are clear */
      static const int r1v[] = {0x18, 0x19, 0x1a, 0x32, 0x33, 0x34, 0x33, 99};
      static const int r2v[] = {0x3b, 0x3c, 0x3d, 0x59, 0x5a, 0x5b, 0, 99};
      static const int r3v[] = {0x4a, 0x4b, 0x4c, 0, 99};
      static const int r4v[] = {0x31, 0x32, 0x33, 0, 99};
      int16_t nn[4] = {100, 100, 100, 100};
      int vv[4] = {r1v[rn(8)], r2v[rn(8)], r3v[rn(5)], r4v[rn(5)]};
      unsigned k = 2;
      if (c != 0xffff && (int16_t)c >= 0 && (int16_t)c < 8) {
        if ((DS_B((uint16_t)(0x465e + 0x234 * c)) & 2) == 0) k = 3;
        if ((DS_B((uint16_t)(0x46eb + 0x234 * c)) & 2) == 0) {
          if (k == 2) vv[2] = vv[3];
          k++;
        }
      }
      if (k == 4) vv[3] = -1; /* three constrained draws are found quickly, four are not */
      rng_find(nn, vv, k);
    }
    DIFF(21, 0x30c2, FAR_CDECL, LO(fl), HI(fl), (uint16_t)rnd(), d, c, e);
  }
}

/* Focus scenarios for 21:0x30c2: the draws exactly at the thresholds (r1 against 0x32 and 0x19, r2 against
 * the 0x3c / 0x5a threshold, r3 against 0x4b, r4 against 0x32), with the byte bits chosen so that the draw
 * is made. */
static void t_30c2_focus(void) {
  static const int r1v[] = {0x18, 0x19, 0x1a, 0x31, 0x32, 0x33, 0x34};
  static const int r2v[] = {0x3b, 0x3c, 0x3d, 0x59, 0x5a, 0x5b};
  static const int r3v[] = {0x4a, 0x4b, 0x4c};
  static const int r4v[] = {0x31, 0x32, 0x33};
  unsigned n = 0;
  for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned i = 0; i < 7; i++)
      for (unsigned j = 0; j < 6; j++)
        for (unsigned k = 0; k < 3; k++) {
          g_rng = 0x30c20f00u + n++;
          stubs_clear_all();
          rng_state();
          DS_W(0x014c) = 1;
          uint32_t fl = rec_at(0);
          fill_rand(fl, 0x80);
          /* e1 and e2 both at least 100 (threshold 0x3c) in modes 0 and 1, else 0x5a */
          int16_t e = (int16_t)((mode & 1) ? 10 : 120);
          for (unsigned q = 0; q < 2; q++) {
            stub_push(S_9_4A62, (uint16_t)e, 0, NULL, NULL);
            stub_push(S_9_4B0C, 0, 0, NULL, NULL);
            stub_push(S_9_4C64, (uint16_t)e, 0, NULL, NULL);
            stub_push(S_9_4B8E, 0, 0, NULL, NULL);
          }
          /* the record 0 of the 0x234-byte table: bits of DS:0x4544, 0x45d1, 0x465e, 0x46eb */
          DS_B(0x4544) = 2;
          DS_B(0x45d1) = 2;
          DS_B(0x465e) = (uint8_t)((mode & 2) ? 0 : 2);
          DS_B(0x46eb) = (uint8_t)((mode & 2) ? 2 : 0);
          int16_t nn[3] = {100, 100, 100};
          int vv[3] = {r1v[i], r2v[j], (mode & 2) ? r3v[k] : r4v[k]};
          rng_find(nn, vv, 3);
          DIFF(21, 0x30c2, FAR_CDECL, LO(fl), HI(fl), 0, 3, 0, 4);
        }
}

/* ---- 21:0x1aec: pick a planet near a fleet by a direction code plus random jitter ---- */

#define NPLN 10
static uint32_t g_pln;         /* the planet table block (DS:0xb6): stride 0x38, id at +0, owner at +2 */
static uint16_t g_plbase;      /* offset of planet 0 in that block */
static uint32_t plan_at(unsigned i) { return MK_FP(HI(g_pln), (uint16_t)(g_plbase + 0x38 * i)); }

/* Planets 0..NPLN-1 in the planet table; 7:0x01e4 finds them by index because DS:0x7a == DS:0x4d96. */
static UNUSED void world_planets(uint16_t me) {
  uint16_t base = (uint16_t)(0x10 * rn(4));
  g_plbase = base;
  ds_w32(0xb6, MK_FP(HI(g_pln), base));
  DS_W(0x4d96) = NPLN;
  DS_W(0x007a) = NPLN;
  for (unsigned i = 0; i < NPLN; i++) {
    uint32_t pr = MK_FP(HI(g_pln), (uint16_t)(base + 0x38 * i));
    fill_rand(pr, 0x38);
    ww(pr, 0, (uint16_t)i);
    ww(pr, 2, chance(1, 3) ? me : chance(1, 4) ? 0xffff : (uint16_t)rn(16));
  }
}

static UNUSED uint16_t sq_isqrt(uint32_t v) {
  uint32_t r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return (uint16_t)r;
}

static void t_1aec(void) {
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x1aec0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    rng_state();
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, NPLN);
    world_designs();
    world_planets(me);
    DS_W(0x0074) = (uint16_t)(chance(1, 8) ? rn(0xffff) : rn(12));
    uint32_t f = rec_at(it % 4);
    fill_rand(f, 0x80);
    uint16_t fid = (uint16_t)rn(NPL);
    ww(f, 0, fid);
    ww(f, 2, (uint16_t)rn(16));
    wb(f, 5, (uint8_t)rnd());
    wb(f, 0x2c, (uint8_t)rn(16));
    /* the fleet's coordinates (DS:0x31ba[id]) near 1000 + 0..size, so that the jitter moves them across the edges */
    uint16_t size = (uint16_t)((DS_W(0x0074) + 1) * 400);
    if (it % 3 == 0) {
      /* edge scenario: a small box (or none), the first jitter R chosen and the coordinates built from
       * 0, S, R, S - R so that sums land exactly on the edges 0 and S */
      DS_W(0x0074) = (uint16_t)(chance(1, 3) ? 0xffff : rn(3));
      size = (uint16_t)((DS_W(0x0074) + 1) * 400);
      int16_t n1 = (int16_t)(size * 0.3), n2 = (int16_t)(size * 0.15);
      int16_t rv = 0;
      if (n1 > 0) {
        int16_t nn[1] = {n1};
        int vv[1] = {(int)rn((uint32_t)n1)};
        rng_find(nn, vv, 1);
        rv = (int16_t)(vv[0] - n2);
      } else {
        rng_state();
      }
      int16_t cands[8] = {0, (int16_t)size, rv, (int16_t)-rv, (int16_t)(size - rv), (int16_t)(size + rv), (int16_t)(size / 2), 1};
      set_xy(fid, 1000 + cands[rn(8)], 1000 + cands[rn(8)]);
    } else {
      set_xy(fid, 1000 + (int32_t)(chance(1, 4) ? 0 : chance(1, 2) ? (int16_t)size : (int32_t)rn(size ? size : 1)),
             1000 + (int32_t)(chance(1, 4) ? 0 : chance(1, 2) ? (int16_t)size : (int32_t)rn(size ? size : 1)));
    }
    uint16_t dir = (uint16_t)(chance(1, 12) ? 8 + rn(3) : chance(1, 20) ? 0xffff : rn(8));
    /* the stub for 7:0x294e: it returns 1 (found) with a planet id at +8 of the 16 byte answer */
    {
      uint8_t ans[16] = {0};
      uint16_t pid = (uint16_t)(chance(1, 8) ? (uint16_t)(NPLN + rn(3)) : chance(1, 8) ? 0xffff : rn(NPLN));
      memcpy(ans + 8, &pid, 2);
      stub_push(S_7_294E, (uint16_t)(chance(7, 8) ? 1 : 0), 0, ans, NULL);
      /* the planet's coordinates: near the fleet so that the squared distance meets the limit */
      if (pid < NPL) {
        const uint16_t a4f[] = {LO(f), HI(f), 0};
        uint16_t n = (uint16_t)orig_ret(9, 0x4f76, a4f, 3);
        uint16_t n2 = (uint16_t)((n + 3) * (n + 3));
        int32_t v = (int16_t)(uint16_t)(n2 * n2);
        int32_t target = v + (int32_t)rn(3) - 1;
        if (target < 0 || chance(1, 6)) target = (int32_t)rn(70000);
        int32_t dx = 0, dy = 0, best = 0x7fffffff;
        for (int32_t x = 0; x < 300; x++) {
          int32_t rem = target - x * x;
          if (rem < 0) break;
          int32_t y = sq_isqrt((uint32_t)rem);
          for (int32_t yy = y; yy <= y + 1; yy++) {
            int32_t d = x * x + yy * yy - target;
            if (d < 0) d = -d;
            if (d < best || (d == best && chance(1, 4))) {
              best = d;
              dx = x;
              dy = yy;
            }
          }
          if (best == 0 && chance(1, 8)) break;
        }
        uint32_t fx = ds_r32((uint16_t)(0x31ba + 4 * fid));
        set_xy(pid, (int32_t)(uint16_t)fx + (chance(1, 2) ? dx : -dx), (int32_t)(fx >> 16) + (chance(1, 2) ? dy : -dy));
      }
    }
    DIFF(21, 0x1aec, FAR_CDECL, LO(f), HI(f), dir);
  }
}

/* ---- 21:0x32aa: decide the next order of a fleet that has finished its waypoint ---- */

static uint32_t g_wp;          /* the waypoint block 21:0x32aa copies an 18-byte order from (rec + 0x64) + 0x16 */
static uint32_t g_info;        /* DS:0x1600: 16 bytes per planet id */

static void t_32aa(void) {
  for (unsigned it = 0; it < 700; it++) {
    g_rng = 0x32aa0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, NPLN);
    world_planets(me);
    ds_w32(0x1600, g_info);
    fill_rand(g_info, 16 * NPL);
    watch_clear();
    watch_add(g_pln, 0x400);
    watch_add(g_info, 16 * NPL);
    watch_add(g_recs, 0x400);
    watch_add(g_wp, 0x100);
    world_players();
    for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5a64 + 0xc0 * p + 0x7)) = (uint8_t)(rnd() & 0x1c);
    DS_W(0x0082) = (uint16_t)(rn(3) + 5);
    DS_B(0x0080) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
    uint32_t rec = rec_at(it % 4);
    fill_rand(rec, 0x80);
    static const uint8_t b0v[] = {29, 30, 31, 39, 40, 41, 15, 20};
    uint8_t b0 = b0v[rn(8)];
    DS_B(0x5340) = b0;
    DS_B(0x5341) = (uint8_t)rn(40);
    DS_B(0x5342) = (uint8_t)rn(40);
    DS_B(0x5343) = (uint8_t)rn(40);
    ww(rec, 0, (uint16_t)rn(NPL));
    ww(rec, 6, (uint16_t)(chance(1, 5) ? 0xffff : rn(NPLN)));
    ww(rec, 0x62, (uint16_t)(chance(1, 6) ? rn(2) : 2 + rn(3)));
    uint16_t wpoff = (uint16_t)(0x10 * rn(4));
    wd(rec, 0x64, MK_FP(HI(g_wp), wpoff));
    wd(rec, 8, (uint32_t)(1000 | (1000 << 16)));
    /* the planets' +0x32 word is compared with the turn DS:0x82 */
    for (unsigned i = 0; i < NPLN; i++) ww(plan_at(i), 0x32, (uint16_t)(chance(1, 2) ? DS_W(0x0082) : rn(10)));
    /* the order record copied from wp + 0x16: position, target id, kind in the low nibble of byte +7 */
    {
      uint8_t *o = LIN(MK_FP(HI(g_wp), (uint16_t)(wpoff + 0x16)));
      for (unsigned k = 0; k < 18; k++) o[k] = (uint8_t)rnd();
      /* squared distances around 62500 (250 * 250) and well inside / outside */
      static const int16_t px[] = {250, -250, 0, 0, 249, 251, 150, 150, -200, 10, 400, 0};
      static const int16_t py[] = {0, 0, 250, -250, 0, 0, 200, -200, 150, 20, 400, 0};
      unsigned pi = rn(12);
      uint32_t pos = (uint32_t)(uint16_t)(1000 + px[pi]) | ((uint32_t)(uint16_t)(1000 + py[pi]) << 16);
      memcpy(o, &pos, 4);
      uint16_t tid = (uint16_t)(chance(1, 5) ? 0xffff : rn(NPLN + 1));
      memcpy(o + 4, &tid, 2);
      o[7] = (uint8_t)((o[7] & 0xf0) | rn(4));
    }
    /* the cargo sums si and di around the thresholds they are compared with */
    for (unsigned k = 0; k < 8; k++) ww(rec, (uint16_t)(0x18 + 2 * k), 0);
    {
      int32_t b = b0;
      int32_t tsi;
      switch (rn(8)) {
        case 0: tsi = b; break;
        case 1: tsi = 2 * b; break;
        case 2: tsi = 3 * b; break;
        case 3: tsi = 4 * b; break;
        case 4: tsi = 0x3c; break;
        case 5: tsi = 0x78; break;
        case 6: tsi = DS_B(0x5341); break;
        default: tsi = (int32_t)rn(300); break;
      }
      tsi += (int32_t)rn(3) - 1;
      ww(rec, 0x18, (uint16_t)tsi);
      ww(rec, 0x1e, (uint16_t)((chance(1, 2) ? (int)DS_B(0x5342) : (int)DS_B(0x5343)) + (int)rn(3) - 1));
    }
    /* the answers of the searches: far pointers to a planet record (an id below NPL) or nothing */
    for (unsigned k = 0; k < 2; k++) {
      uint32_t a = chance(1, 3) ? 0 : plan_at(rn(NPLN)), b = chance(1, 3) ? 0 : plan_at(rn(NPLN)), c = chance(1, 3) ? 0 : rec_at(rn(4));
      stub_push(S_18_39F6, LO(a), HI(a), NULL, NULL);
      stub_push(S_18_3922, LO(b), HI(b), NULL, NULL);
      stub_push(S_18_3854, LO(c), HI(c), NULL, NULL);
    }
    {
      /* the three draws random(10) against 5 and 7 */
      static const int rv[] = {4, 5, 6, 7, 0, 9};
      int16_t nn[3] = {10, 10, 10};
      int vv[3] = {rv[rn(6)], rv[rn(6)], rv[rn(6)]};
      rng_find(nn, vv, 3);
    }
    DIFF(21, 0x32aa, FAR_CDECL, LO(rec), HI(rec));
  }
}

/* Focus scenarios for 21:0x32aa: the decision tree around the thresholds, enumerated: the target planet
 * owned by the current player (path A) or not (path B, owner 3 or 0xffff), the cargo sum si at the values
 * where its compares change (B0, 2 * B0, 3 * B0 and 4 * B0 for B0 = 30 and 40, 0x3c, 0x78, B1), di around
 * B2 and B3, the flag nibble 4 or 8 and draws of 4 to 7. */
static void t_32aa_focus(void) {
  static const int16_t sv[] = {19, 20, 21, 29, 30, 31, 39, 40, 41, 59, 60, 61, 79, 80, 81, 89, 90, 91, 119, 120, 121, 122, 159, 160, 161, 10};
  unsigned n = 0;
  for (unsigned path = 0; path < 2; path++)
    for (unsigned b0i = 0; b0i < 2; b0i++)
      for (unsigned i = 0; i < sizeof sv / 2; i++)
        for (unsigned dj = 0; dj < 4; dj++)
          for (unsigned dr = 0; dr < 4; dr++) {
            unsigned fl = (n % 16 == 15) ? 0 : 1; /* the flag nibble 4 only returns: tried every 16th time */
            g_rng = 0x32aaf000u + n++;
            stubs_clear_all();
            uint16_t me = 2;
            world_tables(me, NPLN);
            world_planets(me);
            ds_w32(0x1600, g_info);
            fill_rand(g_info, 16 * NPL);
            watch_clear();
            watch_add(g_pln, 0x400);
            watch_add(g_info, 16 * NPL);
            watch_add(g_recs, 0x400);
            watch_add(g_wp, 0x100);
            world_players();
            for (unsigned q = 0; q < 16; q++) DS_B((uint16_t)(0x5a64 + 0xc0 * q + 0x7)) = (uint8_t)(fl ? 8 : 4);
            DS_W(0x0082) = 7;
            DS_B(0x0080) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
            DS_B(0x5340) = (uint8_t)(b0i ? 40 : 30);
            DS_B(0x5341) = 20;
            DS_B(0x5342) = 5;
            DS_B(0x5343) = 7;
            uint32_t rec = rec_at(1);
            fill_rand(rec, 0x80);
            ww(rec, 0, 3);
            ww(rec, 6, 4);
            ww(rec, 0x62, 1);
            for (unsigned k = 0; k < 8; k++) ww(rec, (uint16_t)(0x18 + 2 * k), 0);
            ww(rec, 0x18, (uint16_t)sv[i]);
            static const int16_t dv[] = {4, 5, 6, 8};
            ww(rec, 0x1e, (uint16_t)(path == 0 ? dv[dj] : dv[dj] + 2));
            /* the target planet 4: owner me (path A), another player (B) or nobody (C) */
            ww(plan_at(4), 2, path == 0 ? me : (n & 1) ? 3 : 0xffff);
            wb(plan_at(4), 5, 0);
            ww(plan_at(4), 0x32, 7);
            for (unsigned k = 0; k < 2; k++) {
              uint32_t a = plan_at(1 + k), b = plan_at(5 + k), c = rec_at(2);
              stub_push(S_18_39F6, LO(a), HI(a), NULL, NULL);
              stub_push(S_18_3922, LO(b), HI(b), NULL, NULL);
              stub_push(S_18_3854, LO(c), HI(c), NULL, NULL);
            }
            static const int dp[4][3] = {{4, 6, 6}, {5, 6, 7}, {5, 7, 6}, {6, 7, 7}};
            int16_t nn[3] = {10, 10, 10};
            int vv[3] = {dp[dr][0], dp[dr][1], dp[dr][2]};
            rng_find(nn, vv, 3);
            DIFF(21, 0x32aa, FAR_CDECL, LO(rec), HI(rec));
          }
}

/* ---- 21:0x2c74: queue the messages of the techs the computer player has reached ---- */

static void t_2c74(void) {
  for (unsigned it = 0; it < 1200; it++) {
    g_rng = 0x2c740001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    rng_state();
    uint16_t me = (uint16_t)rn(8);
    DS_W(0x014c) = me;
    DS_W(0x0082) = (uint16_t)(chance(1, 8) ? rn(8) : chance(1, 6) ? 0x1e + rn(3) - 1 : chance(1, 6) ? 0x4b + rn(3) - 1 : rn(120));
    for (unsigned k = 0; k < 0x8d; k++) DS_B((uint16_t)(0x417a + 2 + k)) = (uint8_t)rnd();
    /* the flag bytes: bit 1 of 0x41f6, 0x442a, 0x44b7, 0x4310, 0x439d, byte +1 of the records */
    static const uint16_t flagb[] = {0x41f6, 0x442a, 0x44b7, 0x4310, 0x439d, 0x4544, 0x45d1, 0x4544 + 0x234, 0x49ac, 0x49ac + 0x8d};
    for (unsigned k = 0; k < sizeof flagb / 2; k++) DS_B(flagb[k]) = (uint8_t)(chance(1, 2) ? 2 : rnd() & 0xfd);
    DS_B(0x4543 + 1) = (uint8_t)(chance(2, 3) ? 2 : 0);
    DS_B(0x4543 + 0x234 + 1) = (uint8_t)(chance(2, 3) ? 2 : 0);
    DS_B(0x49ab + 1) = (uint8_t)(chance(2, 3) ? 2 : 0);
    DS_B(0x49ab + 0x8d + 1) = (uint8_t)(chance(2, 3) ? 2 : 0);
    DS_W(0x417a) = (uint16_t)(chance(1, 3) ? 5 : rn(10));
    ds_w32(0x41fd, chance(1, 2) ? 0 : rnd());
    for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5a64 + 0xc0 * p + 7)) = (uint8_t)(rnd() & 0x1c);
    /* the "turn" words the techs record: 0x442b, 0x4311, 0x4545, 0x49ad */
    static const uint16_t turnw[] = {0x442b, 0x4311, 0x4545, 0x4545 + 0x234, 0x49ad, 0x49ad + 0x8d};
    for (unsigned k = 0; k < sizeof turnw / 2; k++) ds_ww(turnw[k], (uint16_t)(DS_W(0x0082) + (uint16_t)(rn(60)) - 40));
    for (unsigned k = 0; k < 24; k++) stub_push(S_18_0000, (uint16_t)(chance(1, 3) ? 1 : 0), 0, NULL, NULL);
    DIFF(21, 0x2c74, FAR_CDECL);
  }
}

/* ---- 21:0x23d0: react to one fleet that has finished its journey ---- */

static uint32_t g_cur;         /* DS:0x4bf0: the cursor struct */

static void t_23d0(void) {
  static const uint32_t p28[] = {0, 0x7cf, 0x7d0, 0x7d1, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x10000, 0x1ffff};
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x23d00001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, NPLN);
    world_planets(me);
    world_players();
    fill_rand(g_cur, 0x40);
    ds_w32(0x4bf0, g_cur);
    watch_clear();
    watch_add(g_pln, 0x400);
    watch_add(g_cur, 0x40);
    watch_add(g_tab, 0x400);
    watch_add(g_recs, 0x400);
    uint32_t rec = rec_at(it % 4);
    fill_rand(rec, 0x80);
    unsigned pid = chance(1, 6) ? NPLN + rn(3) : rn(NPLN);  /* the planet 7:0x01e4 finds: none above NPLN */
    ww(rec, 0, (uint16_t)rn(NPL));
    ww(rec, 6, (uint16_t)(chance(1, 12) ? 0xffff : pid));
    wd(rec, 8, (uint32_t)(rn(60) + 1000) | ((uint32_t)(rn(60) + 1000) << 16));
    wd(rec, 0x58, chance(1, 2) ? (uint32_t)rn(3) - 1 : rnd());
    if (pid < NPLN) {
      uint32_t pr = plan_at(pid);
      ww(pr, 2, chance(1, 3) ? me : chance(1, 5) ? 0xffff : (uint16_t)rn(16));
      wb(pr, 5, (uint8_t)(chance(1, 3) ? rnd() | 2 : rnd() & 0xfd));
      wd(pr, 0x28, chance(2, 3) ? p28[rn(9)] : rnd());
      ww(g_cur, 8, chance(1, 2) ? (uint16_t)pid : (uint16_t)rn(16));
    }
    wb(g_cur, 0xb, (uint8_t)(chance(1, 2) ? 1 | (rnd() & 0xf0) : rnd()));
    /* the flag table the function updates: bytes at tab + 2 * planet id */
    uint32_t tabp = MK_FP(HI(g_tab), (uint16_t)(0x20 * rn(4)));
    for (unsigned k = 0; k < 2 * NPL + 4; k++) wb(tabp, (uint16_t)k, (uint8_t)(chance(1, 2) ? (rnd() & 0xfe) : rnd()));
    /* answers of the doubles */
    {
      uint8_t ans[16] = {0};
      uint16_t qid = (uint16_t)(chance(1, 6) ? NPLN + rn(3) : rn(NPLN));
      memcpy(ans + 8, &qid, 2);
      stub_push(S_7_294E, (uint16_t)(chance(3, 4) ? 1 : 0), 0, ans, NULL);
    }
    for (unsigned k = 0; k < 3; k++) {
      uint32_t a = chance(1, 3) ? 0 : plan_at(rn(NPLN));
      stub_push(S_18_3922, LO(a), HI(a), NULL, NULL);
    }
    stub_push(S_10_678C, (uint16_t)rnd(), 0, NULL, NULL);
    stub_push(S_18_2664, (uint16_t)rn(2), 0, NULL, NULL);
    stub_push(S_18_4510, (uint16_t)rn(2), 0, NULL, NULL);
    DIFF(21, 0x23d0, FAR_CDECL, LO(rec), HI(rec), LO(tabp), HI(tabp));
  }
}

/* ---- 21:0x1e8a: spend a fleet's cargo on the queued ships ---- */

static uint32_t g_lst;         /* DS:0xcd2: a list struct (count byte at +3, dword entries from +4) */
#define QBASE 0x6800           /* DS:0x5a62 points here: the queue entries (dwords) */

/* A list entry: type in bits 10-16, count in the low 10 bits, w2 low bits 001x. */
static uint32_t qentry(unsigned type, unsigned count) {
  uint32_t w2 = chance(1, 2) ? 2 : 0x12;
  return ((uint32_t)w2 << 16) | (uint16_t)((type << 10) | count);
}

static void put_q16(unsigned id, int32_t a, int32_t b, int32_t c, int32_t d) {
  int32_t v[4] = {a, b, c, d};
  stub_push(id, 0, 0, v, NULL);
}

/* The list struct at DS:0xcd2 and the queue at DS:0x5a62 that 21:0x1e8a reads. */
static void world_1e8a_lists(void) {
  /* the list at DS:0xcd2 */
  memset(LIN(g_lst), 0, 0x40);
  ds_w32(0xcd2, g_lst);
  {
    uint8_t n = (uint8_t)(chance(1, 5) ? 0 : 1 + rn(4));
    wb(g_lst, 3, n);
    for (unsigned k = 0; k < n; k++) {
      static const unsigned types[] = {8, 7, 8, 7, 0xb, 0xc, 5};
      wd(g_lst, (uint16_t)(4 + 4 * k), qentry(types[rn(7)], 1 + rn(40)));
    }
  }
  /* the queue at DS:0x5a62 (a near pointer) with the count at DS:0x6686 */
  DS_W(0x5a62) = QBASE;
  unsigned nq = chance(1, 8) ? 0 : 1 + rn(6);
  DS_W(0x6686) = (uint16_t)nq;
  for (unsigned k = 0; k < nq; k++) {
    static const unsigned types[] = {8, 7, 8, 7, 0xb, 0xc, 5, 0x1c};
    uint32_t e = qentry(types[rn(8)], rn(500));
    if (chance(1, 12)) e = rnd();
    ds_w32((uint16_t)(QBASE + 4 * k), e);
  }
}

/* The state 21:0x1e8a reads besides the fleet record, the same for every fleet it is called for. */
static void world_1e8a(void) {
  stubs_clear_all();
  uint16_t me = (uint16_t)rn(8);
  DS_W(0x014c) = me;
  DS_W(0x0082) = (uint16_t)(chance(1, 3) ? 0x64 + rn(3) - 1 : rn(300));
  DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 8 : 0);
  world_players();
  /* the player's flag byte (0x5a6b) and the six attribute bytes 0x5a7e to 0x5a83 */
  uint16_t prec = (uint16_t)(0x5a64 + 0xc0 * me);
  DS_B((uint16_t)(prec + 7)) = (uint8_t)(chance(1, 2) ? 0xa0 | (rnd() & 0x1f) : rnd());
  bool all1a = chance(1, 2);
  for (unsigned k = 0; k < 6; k++) DS_B((uint16_t)(prec + 0x1a + k)) = (uint8_t)(all1a ? 0x1a : chance(1, 8) ? rn(256) : 0x1a);
  world_1e8a_lists();
  watch_clear();
  watch_add(g_lst, 0x40);
  watch_add(g_recs, 0x400);
}

/* A fleet record for 21:0x1e8a: the record at rec + 0x34 (far pointer) holds a dword with the type bits
 * 0xc00 / 0x2c00 or nothing. */
static void rec_1e8a(uint32_t rec, unsigned it) {
  fill_rand(rec, 0x80);
  uint32_t aux = rec_at((it + 1) % 4);
  uint32_t tgt = chance(1, 4) ? 0 : MK_FP(HI(aux), (uint16_t)(LO(aux) + 0x40));
  wd(rec, 0x34, tgt);
  if (tgt) {
    uint32_t dw = chance(1, 3) ? (0x0c00u | (rnd() & 0x3ff)) : chance(1, 2) ? 0x2c00u : rnd();
    uint32_t w2 = chance(1, 2) ? 2u : rnd() & 0xf;
    wd(tgt, 4, (dw & 0xffffu) | (w2 << 16) | (chance(1, 4) ? 0x10000u : 0));
  }
}

/* The answers of the doubles for one call of 21:0x1e8a. */
static void queue_1e8a(void) {
  /* A and B (18:0x3558 and 18:0x3600) with D = A - B: positive in the first three kinds, the fourth non-negative */
  int32_t a[4], b[4];
  static const int32_t dd[] = {-5, 0, 1, 50, 500, 3000, 100000, 0x45, 0x46, 0xaa, 0x47, 0xa9, 0xab};
  for (unsigned k = 0; k < 4; k++) {
    b[k] = (int32_t)rn(1000);
    int32_t d = chance(2, 3) ? dd[rn(13)] : (int32_t)rn(20000);
    if (k == 3 && chance(9, 10) && d < 0) d = -d;
    if (k < 3 && chance(7, 8) && d <= 0) d = 1 + (int32_t)rn(3000);
    a[k] = b[k] + d;
  }
  stub_push(S_18_3558, 0, 0, a, NULL);
  stub_push(S_18_3600, 0, 0, b, NULL);
  /* 26:0x21e4 fills the cost vectors; the divisors are positive */
  for (unsigned k = 0; k < 6; k++) {
    int32_t c0 = 1 + (int32_t)rn(30), c1 = 1 + (int32_t)rn(30), c2 = 1 + (int32_t)rn(30), c3 = 1 + (int32_t)rn(600);
    put_q16(S_26_21E4, c0, c1, c2, c3);
  }
  /* the readers: differences around the queue count */
  for (unsigned k = 0; k < 4; k++) {
    static const int16_t rv[] = {0, 1, 2, 5, 10, 30, 100, -3};
    stub_push(S_9_4A62, (uint16_t)rv[rn(8)], 0, NULL, NULL);
    stub_push(S_9_4B0C, (uint16_t)rv[rn(8)], 0, NULL, NULL);
    stub_push(S_9_4C64, (uint16_t)rv[rn(8)], 0, NULL, NULL);
    stub_push(S_9_4B8E, (uint16_t)rv[rn(8)], 0, NULL, NULL);
  }
}

static void t_1e8a(void) {
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x1e8a0001u + 0x9e3779b9u * (it + 1);
    world_1e8a();
    uint32_t rec = rec_at(it % 4);
    rec_1e8a(rec, it);
    queue_1e8a();
    for (unsigned k = 0; k < 2; k++) {
      static const int16_t rv[] = {0, 1, 2, 5, 10, 30, 100, -3};
      stub_push(S_9_4A62, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4B0C, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4C64, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4B8E, (uint16_t)rv[rn(8)], 0, NULL, NULL);
    }
    DIFF(21, 0x1e8a, FAR_CDECL, LO(rec), HI(rec));
  }
}

/* ---- 21:0x1e2a: the final walk over the order list ---- */

static uint32_t g_fptr;        /* DS:0x1608: far pointers to the records, DS:0x4b62 of them */

static void t_1e2a(void) {
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x1e2a0001u + 0x9e3779b9u * (it + 1);
    world_1e8a();
    watch_add(g_fptr, 0x40);
    unsigned n = chance(1, 6) ? 0 : 1 + rn(3);
    DS_W(0x4b62) = (uint16_t)(chance(1, 10) ? 0 : n);
    ds_w32(0x1608, g_fptr);
    memset(LIN(g_fptr), 0, 0x40);
    for (unsigned k = 0; k < n; k++) {
      uint32_t rec = rec_at(k);
      rec_1e8a(rec, it + k);
      wd(g_fptr, (uint16_t)(4 * k), chance(1, 12) ? 0 : rec);
      queue_1e8a();
    }
    DIFF(21, 0x1e2a, FAR_CDECL);
  }
}

/* ---- 21:0x123e: the spend phase ---- */

/* The weapon powers 21:0x123e compares: np3 = 9:0x4f76(P) + 3 and nq = 9:0x4f76(Q) (+1 with the carry) when Q has
 * bit 1 in +5, else 0; the pow path needs them different (the callback 21:0x29da never hands it such a Q;
 * equal powers would divide by zero in the original). */
static bool powers_differ(uint32_t p, uint32_t q) {
  const uint16_t ap[] = {LO(p), HI(p), 0x6a00};
  uint16_t np3 = (uint16_t)(orig_ret(9, 0x4f76, ap, 3) + 3);
  uint16_t nq = 0;
  if (rb(q, 5) & 2) {
    const uint16_t aq[] = {LO(q), HI(q), 0x6a00};
    nq = (uint16_t)orig_ret(9, 0x4f76, aq, 3);
    uint16_t carry;
    memcpy(&carry, LINN(0x6a00), 2);
    if (carry != 0) nq++;
  }
  return np3 != nq;
}

static void t_123e(void) {
  static const uint32_t cg[] = {0, 5, 9, 10, 11, 999, 1000, 1001, 0x2bb, 0x2bc, 0x2bd, 0x500, 0x7fffffffu, 0xffffffffu, 0x80000000u};
  static const int32_t a3v[] = {-10, 0, 60, 68, 69, 70, 71, 72, 75, 100, 140, 150, 400, 2000};
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x123e0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    rng_state();
    uint16_t me = (uint16_t)rn(8);
    world_tables(me, NPLN);
    world_designs();
    world_planets(me);
    world_players();
    for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5a64 + 0xc0 * p + 7)) = (uint8_t)(chance(2, 3) ? 8 : chance(1, 2) ? 4 : rnd() & 0x1c);
    ds_ww(0x4c36, (uint16_t)rnd());
    ds_w32(0x4c08, rnd());
    unsigned n = 1 + rn(3);
    DS_W(0x4b62) = (uint16_t)n;
    ds_w32(0x1608, g_fptr);
    memset(LIN(g_fptr), 0, 0x40);
    ds_w32(0x1600, g_info);
    fill_rand(g_info, 16 * NPL);
    watch_clear();
    watch_add(g_recs, 0x400);
    watch_add(g_tab, 0x400);
    watch_add(g_fptr, 0x40);
    watch_add(g_pln, 0x400);
    watch_add(g_info, 16 * NPL);
    unsigned kq = 3;
    for (unsigned k = 0; k < n; k++) {
      uint32_t pr = rec_at(k);
      fill_rand(pr, 0x80);
      unsigned pid = rn(NPL);
      ww(pr, 0, (uint16_t)pid);
      ww(pr, 2, (uint16_t)rn(16));
      wb(pr, 5, (uint8_t)(chance(7, 8) ? rnd() | 2 : rnd() & 0xfd));
      static const uint8_t hull[] = {1, 3, 6, 8, 1, 3, 2, 0, 15};
      wb(pr, 0x2c, (uint8_t)(hull[rn(9)] | (rnd() & 0xf0)));
      for (unsigned c = 0; c < 3; c++) wd(pr, (uint16_t)(0x1c + 4 * c), chance(2, 3) ? cg[rn(15)] : rnd());
      wd(g_fptr, (uint16_t)(4 * k), pr);
      /* the flag byte T1 (bit 7: done, bit 4, low three bits) and the byte pair T2 */
      uint32_t t1 = ds_r32(0x1604);
      uint8_t f1 = (uint8_t)(chance(1, 3) ? 0x80 | (rnd() & 0x7f) : rnd() & 0x7f);
      wb(MK_FP(HI(t1), (uint16_t)(LO(t1) + 2)), (uint16_t)(2 * pid), f1);
      /* A and B (18:0x3558 and 18:0x3600) */
      int32_t av[4], bv[4];
      for (unsigned c = 0; c < 3; c++) {
        bv[c] = (int32_t)rn(300);
        av[c] = bv[c] + (chance(1, 2) ? (int32_t)rn(2000) : (int32_t)rn(300) - 100);
      }
      bv[3] = (int32_t)rn(100);
      av[3] = a3v[rn(14)];
      if (chance(1, 5)) {
        /* S = A0 + A1 + A2 - B - 0xd2 at the threshold 0x96 */
        bv[0] = bv[1] = bv[2] = 0;
        av[1] = (int32_t)rn(300);
        av[2] = (int32_t)rn(300);
        av[0] = 0xd2 + 0x95 + (int32_t)rn(3) - av[1] - av[2];
      }
      stub_push(S_18_3558, 0, 0, av, NULL);
      stub_push(S_18_3600, 0, 0, bv, NULL);
      /* the extra pair 21:0x1d76 reads */
      int32_t av2[4] = {(int32_t)rn(500), (int32_t)rn(500), (int32_t)rn(500), (int32_t)rn(500)};
      int32_t bv2[4] = {(int32_t)rn(300), (int32_t)rn(300), (int32_t)rn(300), (int32_t)rn(300)};
      stub_push(S_18_3558, 0, 0, av2, NULL);
      stub_push(S_18_3600, 0, 0, bv2, NULL);
      /* Q: the answer of the search 18:0x3922 (two calls per fleet at most) */
      uint32_t qr = rec_at(kq);
      fill_rand(qr, 0x80);
      ww(qr, 0, (uint16_t)rn(NPL));
      ww(qr, 2, (uint16_t)rn(16));
      wb(qr, 5, (uint8_t)(chance(3, 4) ? rnd() | 2 : rnd() & 0xfd));
      wb(qr, 0x2c, (uint8_t)(hull[rn(9)] | (rnd() & 0xf0)));
      for (unsigned c = 0; c < 3; c++) wd(qr, (uint16_t)(0x1c + 4 * c), chance(3, 4) ? cg[rn(15)] : rnd());
      ww(qr, 0x12, (uint16_t)(chance(1, 6) ? (rnd() & 0xf000) : rnd()));
      /* planets of P and Q close to each other (the sqrt distance), the 4f76 powers apart */
      set_xy(pid, 1000, 1000);
      set_xy(rw(qr, 0), 1000 + (int32_t)rn(41) - 20, 1000 + (int32_t)rn(41) - 20);
      set_xy(pid, 1000, 1000);
      for (unsigned tries = 0; tries < 20 && !powers_differ(pr, qr); tries++) wb(qr, 0x2c, (uint8_t)rnd());
      uint32_t qp = chance(1, 4) ? 0 : qr;
      stub_push(S_18_3922, LO(qp), HI(qp), NULL, NULL);
      stub_push(S_18_3922, LO(qp), HI(qp), NULL, NULL);
      /* the planet 21:0x1aec finds through 7:0x294e */
      uint8_t ans[16] = {0};
      uint16_t pidq = (uint16_t)(chance(1, 8) ? NPLN + rn(3) : rn(NPLN));
      memcpy(ans + 8, &pidq, 2);
      stub_push(S_7_294E, (uint16_t)(chance(7, 8) ? 1 : 0), 0, ans, NULL);
    }
    DIFF(21, 0x123e, FAR_CDECL);
  }
}

/* ---- 21:0x0000: the turn of the computer player ---- */

static uint32_t g_tab2;        /* the big table block 21:0x0000 clears (0x2000 bytes) */
static uint32_t g_fpa;         /* DS:0xba: the fleet pointer array */

/* Every design has the same weapon slots (four of power 10 that tie), so the weapon power 9:0x4f76
 * returns is the same for every fleet that has bit 1 in +5: 21:0x123e's pow then never sees equal
 * powers (np3 = power + 3 against power + 1) and never divides by zero. */
static void world_designs_uniform(void) {
  for (unsigned p = 0; p < 16; p++) {
    if (!g_dsg[p]) g_dsg[p] = gblock(0x8d0);
    uint32_t d = g_dsg[p];
    memset(LIN(d), 0, 0x8d0);
    for (unsigned k = 0; k < 16; k++) {
      uint16_t o = (uint16_t)(0x8d * k);
      wb(d, (uint16_t)(o + 0x7b), 7);
      wb(d, (uint16_t)(o + 0x7a), 4);
      for (unsigned sl = 0; sl < 4; sl++) {
        ww(d, (uint16_t)(o + 0x3a + 4 * sl), 0x200);
        ww(d, (uint16_t)(o + 0x3c + 4 * sl), 0x010c);
      }
    }
    ds_w32((uint16_t)(0x10c + 4 * p), d);
  }
}

/* A fleet record of the world of 21:0x0000. The cargo words (+0x14 to +0x26) are all non-positive in
 * "empty" fleets and have a positive one in "loaded" fleets; +0xc to +0x12 are small counts. */
static void fleet_0000(uint32_t f, uint16_t me, bool mine, unsigned id) {
  fill_rand(f, 0x80);
  ww(f, 0, (uint16_t)id);
  ww(f, 2, mine ? me : chance(1, 8) ? 0xffff : (uint16_t)(me + 1 + rn(7)));
  wb(f, 5, (uint8_t)(chance(7, 8) ? rnd() | 2 : rnd() & 0xfd));
  ww(f, 6, (uint16_t)(chance(1, 3) ? 0xffff : rn(NPLN)));
  static const int16_t small[] = {0, 0, 1, 2, 5, 9, 10, 11, 15, 16, 17, 30, -1};
  for (unsigned k = 0; k < 4; k++) ww(f, (uint16_t)(0xc + 2 * k), (uint16_t)(chance(1, 3) ? 0 : small[rn(13)]));
  unsigned mode = rn(3);
  for (unsigned k = 0; k < 10; k++) {
    int16_t v = 0;
    if (mode == 1) v = (int16_t)(chance(1, 3) ? 1 + rn(40) : chance(1, 2) ? 0 : -(int16_t)rn(4));
    else if (mode == 0) v = (int16_t)(chance(1, 2) ? 0 : -(int16_t)rn(40));
    ww(f, (uint16_t)(0x14 + 2 * k), (uint16_t)v);
  }
  static const uint32_t d28[] = {0, 0, 1, 2, 100, 1000, 5000, 6000, 10000, 20000, 50000, 0x7d0, 0x7d1, 0x7cf, 3000, 0x80000000u, 0xffffffffu, 30, 5499, 5500, 5501, 14999, 15000, 15001, 5500, 15000};
  wd(f, 0x28, chance(3, 4) ? d28[rn(26)] : rnd());
  wb(f, 0x2c, (uint8_t)(chance(1, 2) ? (uint8_t[]){1, 3, 6, 8}[rn(4)] : rnd()));
  static const uint32_t d58[] = {0, 1, 0xffffffffu, 0x80000000u, 77};
  wd(f, 0x58, chance(1, 2) ? d58[rn(5)] : rnd());
  wb(f, 0x60, (uint8_t)(chance(1, 3) ? 4 : rnd()));
  ww(f, 0x62, (uint16_t)(chance(1, 2) ? rn(2) : 2 + rn(3)));
  uint16_t wo = (uint16_t)(0x20 * rn(7));
  wd(f, 0x64, MK_FP(HI(g_wp), wo));
  /* the waypoint record: +8 and +0x1a planet ids, +0xa and +0xb and +0x1d with a kind nibble */
  uint32_t w = MK_FP(HI(g_wp), wo);
  ww(w, 8, (uint16_t)rn(NPL));
  ww(w, 0x1a, (uint16_t)rn(NPL));
  wb(w, 0xa, (uint8_t)(chance(1, 3) ? 6 : chance(1, 3) ? 0 : rnd()));
  wb(w, 0xb, (uint8_t)(chance(1, 2) ? 1 | (rnd() & 0xf0) : rnd()));
  wb(w, 0x1d, (uint8_t)(chance(1, 2) ? 1 | (rnd() & 0xf0) : rnd()));
  ww(f, 0x12, (uint16_t)(chance(1, 3) ? (rnd() & 0xf000) | rn(3) : rnd()));
  /* +0x34: a far pointer 21:0x1e8a dereferences, to a record whose dword +4 holds type bits */
  {
    uint32_t aux = MK_FP(HI(g_lst), 0x80);
    uint32_t tgt = chance(1, 3) ? 0 : aux;
    wd(f, 0x34, tgt);
    if (tgt) {
      uint32_t dw = chance(1, 3) ? (0x0c00u | (rnd() & 0x3ff)) : chance(1, 2) ? 0x2c00u : rnd();
      uint32_t w2 = chance(1, 2) ? 2u : rnd() & 0xf;
      wd(tgt, 4, (dw & 0xffffu) | (w2 << 16) | (chance(1, 4) ? 0x10000u : 0));
    }
  }
}

/* BP-0x34 of 21:0x0000 for a turn (the value it compares the cargo and +0x28 words with). */
static uint16_t bp34_of(uint16_t t) {
  uint16_t v = 1;
  if (t > 0x32) v = (uint16_t)((uint16_t)(t - 0x32) / 10 + 1);
  if (t > 0x64) v = (uint16_t)(v + (uint16_t)((uint16_t)(t - 0x64) / 10 * (t / 0x64)));
  return v;
}

static unsigned iters(unsigned n) {
  const char *e = getenv("SEG21_ITERS");
  return e ? (unsigned)atoi(e) : n;
}

static void t_0000(void) {
  for (unsigned it = 0; it < iters(1000); it++) {
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    rng_state();
    uint16_t me = (uint16_t)rn(8);
    static const uint16_t turns[] = {1, 5, 6, 0x14, 0x15, 0x1e, 0x1f, 0x28, 0x29, 0x32, 0x33, 0x3c, 0x4b, 0x50, 0x51, 0x64, 0x65,
                                     0x73, 0x74, 0x77, 0x78, 0x79, 0x82, 0x83, 0x96, 0xc7, 0xc8, 0xc9, 0x18f, 0x190, 0x191, 0x258, 0x44c, 0x5dc, 0x424, 0x438, 0x32};
    world_tables(me, NPLN);
    DS_W(0x0082) = (uint16_t)(chance(1, 4) ? rn(0x70) : chance(2, 3) ? turns[rn(37)] : rn(700));
    DS_B(0x0080) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
    DS_W(0x255c) = (uint16_t)(chance(1, 2) ? 0 : 0x1234);
    DS_W(0x15ba) = 0; /* 8:0xb90e has nothing to draw */
    world_designs_uniform();
    world_planets(me);
    for (unsigned i = 0; i < NPLN; i++) {
      uint32_t pr = plan_at(i);
      ww(pr, 0x26, (uint16_t)(chance(1, 2) ? 0 : rnd()));
      ww(pr, 0x24, (uint16_t)(chance(1, 3) ? 0x3e8 + rn(3) - 1 : chance(1, 3) ? 0xa + rn(3) - 1 : rnd()));
      ww(pr, 0x1c, (uint16_t)(chance(1, 3) ? 0x3e8 + rn(3) - 1 : rn(30)));
      ww(pr, 0x1e, 0);
      ww(pr, 0x20, (uint16_t)(chance(1, 3) ? 0x3e8 + rn(3) - 1 : rn(30)));
      ww(pr, 0x22, 0);
    }
    world_players();
    /* the big block 21:0x0000 may clear: its first word is 2 half of the time */
    {
      uint16_t off = (uint16_t)(chance(1, 2) ? 0 : 0x40);
      fill_rand(g_tab2, 0x2100);
      ds_w32(0x1604, MK_FP(HI(g_tab2), off));
      ww(MK_FP(HI(g_tab2), off), 0, (uint16_t)(chance(1, 2) ? 2 : rn(100) + 3));
    }
    /* the DS area of the tech/research records the loops read (0x417a to 0x4a50) */
    for (unsigned k = 0x4150; k < 0x4a60; k++) DS_B((uint16_t)k) = (uint8_t)(chance(1, 3) ? 2 : rnd());
    for (unsigned k = 0; k < 14; k++) {
      DS_B((uint16_t)(0x41f5 + 0x8d * k + 1)) = (uint8_t)(chance(1, 2) ? 2 : rnd() & 0xfd);
      if (chance(1, 2)) ds_w32((uint16_t)(0x41f5 + 0x8d * k + 8), 0);
    }
    DS_B(0x4544) = (uint8_t)(chance(2, 3) ? 2 : 0);
    DS_B(0x4544 + 0x234) = (uint8_t)(chance(2, 3) ? 2 : 0);
    {
      /* the record words +2 (0x4545 and 0x4779) against the turn minus the limit si (0x32, 0x46, 0x64, 0x12c) */
      uint16_t t = DS_W(0x0082), lim = (uint16_t)(t < 0x78 ? 0x32 : t < 0xc8 ? 0x46 : t < 0x190 ? 0x64 : 0x12c);
      for (unsigned k = 0; k < 2; k++)
        if (chance(1, 2)) ds_ww((uint16_t)(0x4545 + 0x234 * k), (uint16_t)(t - lim + (int)rn(3) - 1));
    }
    ds_w32(0x428a, rnd());
    ds_w32(0x41fd, chance(1, 2) ? 0 : rnd());
    DS_B(0x4310) = (uint8_t)(chance(1, 2) ? 2 : 0);
    DS_W(0x417a) = (uint16_t)(chance(1, 3) ? 5 : rn(10));
    for (unsigned q = 0; q < 16; q++) DS_B((uint16_t)(0x5a64 + 0xc0 * q + 7)) = (uint8_t)(chance(1, 2) ? 8 : rnd() & 0x1c);
    if (chance(1, 2)) {
      /* 28:0x41d4(me) = the signed byte player+0x19, doubled when player+0x4c is 0: make it 1 so that
       * the products with +0x28 can be placed on the thresholds 0x157c and 0x3a98 */
      DS_B((uint16_t)(0x5a64 + 0xc0 * me + 0x19)) = 1;
      DS_B((uint16_t)(0x5a64 + 0xc0 * me + 0x4c)) = (uint8_t)(1 + rn(7));
    }
    /* the cursor struct, the info array */
    fill_rand(g_cur, 0x40);
    ds_w32(0x4bf0, g_cur);
    ds_w32(0x1600, g_info);
    fill_rand(g_info, 16 * NPL);
    for (unsigned k = 0; k < NPL; k++) wb(g_info, (uint16_t)(16 * k + 0xa), (uint8_t)(chance(1, 3) ? 0 : rnd()));
    ds_w32(0x4c08, rnd());
    DS_W(0x4c0a) = (uint16_t)(chance(2, 3) ? me : rn(16));
    DS_B(0x4bec) = 0;
    DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 8 : 0);
    world_1e8a_lists();
    /* fleets: the pointer array DS:0xba (nf entries, the last one may be null) and the list DS:0x1608 */
    unsigned nf = rn(6);
    /* counters at and one above their thresholds 0x78, 0xfa, 0x37 and 0x28: many fleets that all point to one record */
    unsigned many = it == 100 ? 121 : it == 200 ? 251 : it == 300 ? 56 : it == 400 ? 41 : it == 600 ? 120 : it == 700 ? 250 : it == 800 ? 55 : it == 900 ? 40 : 0;
    if (many) nf = many;
    DS_W(0x553a) = (uint16_t)nf;
    ds_w32(0xba, g_fpa);
    memset(LIN(g_fpa), 0, 0x440);
    for (unsigned k = 0; k < (many ? 1 : nf); k++) {
      uint32_t f = rec_at(k);
      fleet_0000(f, me, many ? true : chance(2, 3), chance(4, 5) ? rn(NPLN) : rn(NPL));
      if (!many && chance(1, 4)) {
        /* compares against BP-0x34: the cargo word doubled, or the +0x28 word, close to it */
        uint16_t b34 = bp34_of(DS_W(0x0082));
        ww(f, 2, me);
        if (chance(1, 2)) {
          for (unsigned c = 0; c < 10; c++) ww(f, (uint16_t)(0x14 + 2 * c), 0);
          int32_t cw = ((int32_t)b34 + (int32_t)rn(4) - 1) / 2;
          ww(f, 0x14, (uint16_t)(cw < 1 ? 1 : cw));
          ww(f, 0x62, 1);
          ww(f, 0x28, 0);
          ww(f, 0x2a, 0);
        } else {
          wd(f, 0x28, (uint32_t)(2 * (int32_t)b34 + (int32_t)rn(4) - 1));
          ww(f, 6, (uint16_t)rn(NPLN));
          ds_ww(0x49ad, (uint16_t)(DS_W(0x0082) - 3));
          ds_ww(0x4a3a, (uint16_t)(DS_W(0x0082) - 4));
        }
      }
      if (!many && chance(1, 3)) {
        /* an idle owned fleet with a free cargo hold and a count of 7 to 20 in +0xc: the order search path */
        ww(f, 2, me);
        ww(f, 0xc, (uint16_t)(7 + rn(14)));
        ww(f, 0xe, 0);
        ww(f, 0x10, 0);
        ww(f, 0x12, 0);
        for (unsigned c = 0; c < 10; c++) ww(f, (uint16_t)(0x14 + 2 * c), 0);
        ww(f, 0x62, 1);
        ww(f, 0x28, 0);
        ww(f, 0x2a, 0);
        if (chance(1, 2)) DS_W(0x0082) = (uint16_t)(0x29 + rn(200));
      }
      if (many) {
        /* idle, loaded, owned fleets: with no target they count in BP-0x40 (130), with one in BP-0x3e (260) */
        ww(f, 6, many >= 250 ? 3 : 0xffff);
        ww(f, 0x62, 1);
        ww(f, 0xe, 0);
        ww(f, 0x10, 0);
        ww(f, 0x12, 0);
        DS_W(0x0082) = 0x50;
        ww(f, 0x28, 0);
        ww(f, 0x2a, 0);
        ww(f, 0xc, 3);
        for (unsigned c = 0; c < 10; c++) ww(f, (uint16_t)(0x14 + 2 * c), (uint16_t)(many >= 120 ? (c == 0 ? 5 : 0) : 0));
      }
      wd(g_fpa, (uint16_t)(4 * k), f);
    }
    for (unsigned k = 1; k < many; k++) wd(g_fpa, (uint16_t)(4 * k), rd32f(g_fpa, 0));
    if (nf && !many && chance(1, 10)) wd(g_fpa, (uint16_t)(4 * (nf - 1)), 0);
    bool lastfocus = many || it % 6 == 2; /* the list fleet takes the whole path of the building loop */
    unsigned nl = nf ? rn((many ? 5 : nf) + 1) : 0;
    if (lastfocus && nf && nl == 0) nl = 1;
    DS_W(0x4b62) = (uint16_t)nl;
    ds_w32(0x1608, g_fptr);
    memset(LIN(g_fptr), 0, 0x40);
    for (unsigned k = 0; k < nl; k++) wd(g_fptr, (uint16_t)(4 * k), rd32f(g_fpa, (uint16_t)(4 * k)));
    bool lf_ok = lastfocus && nl && rd32f(g_fptr, 0) != 0;
    if (lf_ok) {
      /* the first list fleet: bit 1 in +5, hull class 2, bit 3 in its tab1 byte, no bit 0 / no bits 1-2 in its tab2
       * byte, DS:0x428a below 0x28; its answers are queued first below */
      uint32_t lf = rd32f(g_fptr, 0);
      wb(lf, 5, (uint8_t)(rb(lf, 5) | 2));
      wb(lf, 0x2c, (uint8_t)((rb(lf, 0x2c) & 0xf0) | 2));
      uint32_t t1 = ds_r32(0x1604);
      uint16_t id2 = (uint16_t)(2 * rw(lf, 0));
      wb(MK_FP(HI(t1), (uint16_t)(LO(t1) + 2)), id2, (uint8_t)(rb(MK_FP(HI(t1), (uint16_t)(LO(t1) + 2)), id2) | 8));
      uint16_t o2 = (uint16_t)(LO(t1) + (uint16_t)((DS_W(0x007a) + 1) * 2) + id2);
      wb(MK_FP(HI(t1), o2), 0, (uint8_t)(rb(MK_FP(HI(t1), o2), 0) & 0x98));
      DS_B(0x4310) = (uint8_t)(DS_B(0x4310) & 0xfd);
      ds_w32(0x428a, rn(0x30));
      /* [bp-6] = +0x28 * 28:0x41d4(me): attr 1, +0x28 on the thresholds 0x157c and 0x3a98 */
      DS_B((uint16_t)(0x5a64 + 0xc0 * me + 0x19)) = 1;
      DS_B((uint16_t)(0x5a64 + 0xc0 * me + 0x4c)) = (uint8_t)(1 + rn(7));
      static const uint32_t th[] = {5499, 5500, 5501, 14999, 15000, 15001, 5501, 15001};
      if (!many && nf >= 2) {
        /* a second fleet that names this one as its target with a count of 9 to 17 in +0xc (the scan at 21:0x0e9f) */
        uint32_t f2 = rd32f(g_fpa, 4);
        if (f2 != 0 && f2 != lf) {
          static const uint16_t cc[] = {9, 10, 11, 16, 17};
          ww(f2, 6, rw(lf, 0));
          ww(f2, 0xc, cc[rn(5)]);
          ww(f2, 2, me);
          DS_W(0x417a) = 5;
        }
      }
      bool sumf = !many && chance(1, 4);
      if (sumf) {
        /* the sum of the idle fleets' +0xc words next to 10000 */
        ww(lf, 2, me);
        ww(lf, 0x28, 0);
        ww(lf, 0x2a, 0);
        ww(lf, 0xc, (uint16_t)(0x270f + rn(3)));
        for (unsigned k = 1; k < nf; k++) {
          uint32_t fk = rd32f(g_fpa, (uint16_t)(4 * k));
          if (fk != 0 && fk != lf) ww(fk, 0xc, 0);
        }
        DS_W(0x417a) = 5;
      }
      if (!many && !sumf) {
        wd(lf, 0x28, th[rn(8)]);
        if (chance(1, 2)) DS_W(0x0082) = (uint16_t)(0x5f + rn(6));
      }
    }
    watch_clear();
    watch_add(g_recs, 0x400);
    watch_add(g_pln, 0x400);
    watch_add(g_info, 16 * NPL);
    watch_add(g_cur, 0x40);
    watch_add(g_wp, 0x100);
    watch_add(g_fpa, 0x40);
    watch_add(g_fptr, 0x40);
    watch_add(g_tab2, 0x400);
    watch_add(g_lst, 0x100);
    /* the answers of the doubles, in the order of the calls (a queue per callee) */
    if (lf_ok) {
      /* the building loop's first fleet: 9:0x474c at least 10, 9:0x462a above it, 18:0x5fe6 yes, A - B non-negative */
      stub_push(S_9_474C, (uint16_t)(10 + rn(3)), 0, NULL, NULL);
      stub_push(S_9_462A, (uint16_t)(chance(1, 2) ? 30 : 12 + rn(3)), 0, NULL, NULL);
      stub_push(S_18_5FE6, 1, 0, NULL, NULL);
      int32_t a[4], b[4];
      static const int32_t dd[] = {0x46, 0x47, 0x48, 0x100, 0, 1};
      for (unsigned c = 0; c < 4; c++) {
        b[c] = (int32_t)rn(100);
        a[c] = b[c] + (c == 3 ? dd[rn(6)] : (int32_t)rn(50));
      }
      stub_push(S_18_3558, 0, 0, a, NULL);
      stub_push(S_18_3600, 0, 0, b, NULL);
      /* the three 18:0x5d3c answers of the start: the third (BP-0xc) below 0x32 */
      for (unsigned c = 0; c < 3; c++) {
        uint8_t w[16] = {0}, b16[16] = {0};
        uint16_t wi = (uint16_t)rn(16);
        memcpy(w, &wi, 2);
        stub_push(S_18_5D3C, (uint16_t)(c == 2 ? rn(0x34) : rnd()), 0, w, b16);
      }
      /* the 18:0x3922 answers: first the 21:0x27ba search (a record or none), then the 21:0x2bd8 search: a record
       * K whose distance to the fleet is on the limit 0x15f90 */
      {
        uint32_t lf2 = rd32f(g_fptr, 0);
        uint32_t k1 = rec_at(5), k2 = rec_at(6);
        ww(k1, 0, (uint16_t)(20 + rn(10)));
        ww(k2, 0, (uint16_t)(30 + rn(10)));
        unsigned fid = rw(lf2, 0);
        int32_t target = 0x15f90 + (int32_t)rn(3) - 1, bx_ = 0, by_ = 0;
        for (int32_t x = 0; x < 400; x++) {
          int32_t rem = target - x * x;
          if (rem < 0) break;
          int32_t y = (int32_t)sq_isqrt((uint32_t)rem);
          if (x * x + y * y == target) {
            bx_ = x;
            by_ = y;
            if (chance(1, 3)) break;
          }
        }
        set_xy(fid, 1000, 1000);
        set_xy(rw(k2, 0), 1000 + bx_, 1000 + by_);
        uint32_t first = chance(1, 2) ? 0 : k1;
        stub_push(S_18_3922, LO(first), HI(first), NULL, NULL);
        stub_push(S_18_3922, LO(k2), HI(k2), NULL, NULL);
      }
    }
    for (unsigned k = 0; k < 20; k++) {
      uint32_t r = chance(1, 3) ? 0 : chance(1, 2) ? plan_at(rn(NPLN)) : rec_at(rn(6));
      stub_push(S_18_3922, LO(r), HI(r), NULL, NULL);
      stub_push(S_18_39F6, LO(r), HI(r), NULL, NULL);
      stub_push(S_18_098A, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, NULL, NULL);
      stub_push(S_18_4510, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_2664, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_0864, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_3AD8, (uint16_t)(chance(1, 3) ? 0xffff : rn(NPLN)), 0, NULL, NULL);
      static const int16_t sc[] = {5, 9, 10, 11, 15, 20, 30};
      stub_push(S_9_474C, (uint16_t)sc[rn(7)], 0, NULL, NULL);
      stub_push(S_9_462A, (uint16_t)sc[rn(7)], 0, NULL, NULL);
      stub_push(S_18_5FE6, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_5E82, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_0000, (uint16_t)rn(2), 0, NULL, NULL);
      stub_push(S_18_50C8, (uint16_t)rnd(), 0, NULL, NULL);
      stub_push(S_18_28FC, (uint16_t)rnd(), 0, NULL, NULL);
      stub_push(S_10_678C, (uint16_t)rnd(), 0, NULL, NULL);
      uint8_t ans[16] = {0};
      uint16_t pid = (uint16_t)(chance(1, 8) ? NPLN + rn(3) : rn(NPLN));
      memcpy(ans + 8, &pid, 2);
      stub_push(S_7_294E, (uint16_t)(chance(7, 8) ? 1 : 0), 0, ans, NULL);
      uint8_t w[16] = {0};
      uint16_t wd5 = (uint16_t)rn(16);
      memcpy(w, &wd5, 2);
      uint8_t b16[16];
      unsigned bprof = rn(4);
      for (unsigned i = 0; i < 16; i++) b16[i] = (uint8_t)(bprof == 0 ? 1 : bprof == 1 ? 0 : chance(1, 3) ? 1 : 0);
      stub_push(S_18_5D3C, (uint16_t)rnd(), 0, w, b16);
    }
    for (unsigned k = 0; k < 30; k++) {
      int32_t a[4], b[4];
      static const int32_t dd[] = {-5, 0, 1, 50, 500, 3000, 100000, 0x45, 0x46, 0xaa, 0x47, 0xa9, 0xab};
      for (unsigned c = 0; c < 4; c++) {
        b[c] = (int32_t)rn(1000);
        int32_t d = chance(2, 3) ? dd[rn(13)] : (int32_t)rn(20000);
        if (d < 0 && chance(7, 8)) d = -d;
        a[c] = b[c] + d;
      }
      stub_push(S_18_3558, 0, 0, a, NULL);
      stub_push(S_18_3600, 0, 0, b, NULL);
    }
    for (unsigned k = 0; k < 30; k++) {
      put_q16(S_26_21E4, 1 + (int32_t)rn(30), 1 + (int32_t)rn(30), 1 + (int32_t)rn(30), 1 + (int32_t)rn(600));
    }
    for (unsigned k = 0; k < 10; k++) {
      static const int16_t rv[] = {0, 1, 2, 5, 10, 30, 100, -3};
      stub_push(S_9_4A62, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4B0C, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4C64, (uint16_t)rv[rn(8)], 0, NULL, NULL);
      stub_push(S_9_4B8E, (uint16_t)rv[rn(8)], 0, NULL, NULL);
    }
    DIFF(21, 0x0000, FAR_CDECL, (uint16_t)rnd());
  }
}

/* test_pow_34_0d84_native (stars-nf6): the model crt_pow_34_0d84 against the original 34:0x0d84 run in
 * the Unicorn, for the operands segment 21 gives it at src/seg21.c lines 368 (21:0x29da:
 * base DS:0x20cc or DS:0x20d4, exponent a distance over a strength) and 1919 (21:0x123e: base
 * DS:0x209c or DS:0x20a4 (read from DGROUP), exponent the ratio of two doubles), plus a zero exponent. The
 * harness runs the original's FYL2X and F2XM1 through fp80_fyl2x and fp80_f2xm1 (harness_hook_x87_transc,
 * owner decision 2026-10-20), the helpers the model uses, so the double result must be bit-exact;
 * DS:0x18FC (the stored result) and the byte DS:0x1C9A must equal the original's, and the control word
 * must be restored. This checks the model's FYL2X, FRNDINT, F2XM1, FSCALE sequence against the
 * original's, not the helpers' values (those are pinned by test_fp_vectors in harness/tests/test_fp.c). */
static void t_pow_34_0d84_native(void) {
  static const uint16_t base_off[4] = {0x20cc, 0x20d4, 0x209c, 0x20a4};
  /* far call 34:0x0d84 with ST(1) = base [es:si], ST(0) = exponent [es:si+8]; result to [es:si+16] */
  const uint16_t sel34 = MEM_SEL_SEG(34);
  const uint8_t code[] = {0x26, 0xDD, 0x04, 0x26, 0xDD, 0x44, 0x08, 0x9A, 0x84, 0x0D,
                          (uint8_t)sel34, (uint8_t)(sel34 >> 8), 0x26, 0xDD, 0x5C, 0x10, 0xCB};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  uint16_t data = GlobalAlloc(GPTR, 32);
  unsigned total = 0, exact = 0, bad = 0;
  CHECK(sel != 0 && data != 0);
  g_rng = 0x0D84u;
  for (unsigned i = 0; i < 4000; i++) {
    double base, expo;
    memcpy(&base, LINN(base_off[i & 3]), 8);
    if (i < 8) expo = (double)(i & 1) * 0.5;            /* 0 and 0.5 */
    else if (i & 4) expo = (double)(rnd() % 3000) / (double)(1 + rnd() % 2000); /* distance / strength */
    else expo = (double)(rnd() % 100000) / (double)(1 + rnd() % 100000) * (double)(1 + rnd() % 8);
    if (expo > 200.0) expo = fmod(expo, 200.0); /* keep |t| below 2^14 (the overflow path is not modelled) */
    if (!(base > 0.5 && base < 2.0)) { /* the bases are constants such as 0.875, 0.75 and 8/7 */
      fprintf(stderr, "pow test: base at DS:%04x is %g\n", base_off[i & 3], base);
      CHECK(false);
      return;
    }
    memcpy(LIN((uint32_t)data << 16), &base, 8);
    memcpy((uint8_t *)LIN((uint32_t)data << 16) + 8, &expo, 8);
    memset((uint8_t *)LIN((uint32_t)data << 16) + 16, 0, 8);
    memset(LINN(0x18fc), 0, 8);
    DS_B(0x1c9a) = 0;
    uint16_t cw = (i & 1) ? 0x037f : FPU_CW_DEFAULT;
    Regs16 io;
    memset(&io, 0, sizeof io);
    io.es = data; io.ds = MEM_SEL_DGROUP;
    harness_set_fpcw(cw);
    harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
    CHECK_EQ(harness_last_error(), 0);
    uint64_t want, wds, got, gds;
    uint8_t wflag = DS_B(0x1c9a);
    memcpy(&want, (uint8_t *)LIN((uint32_t)data << 16) + 16, 8);
    memcpy(&wds, LINN(0x18fc), 8);
    memset(LINN(0x18fc), 0, 8);
    DS_B(0x1c9a) = 0;
    fp80_set_cw(cw);
    double g = fp80_to_f64(crt_pow_34_0d84(fp80_from_f64(base), fp80_from_f64(expo)));
    CHECK_EQ(fp80_get_cw(), cw);
    memcpy(&got, &g, 8);
    memcpy(&gds, LINN(0x18fc), 8);
    CHECK_EQ(wflag, 1);
    CHECK_EQ(DS_B(0x1c9a), 1);
    CHECK(want == wds); /* the original stores the result it returns */
    CHECK(got == gds);
    total++;
    if (want == got) exact++;
    else if (bad++ == 0)
      fprintf(stderr, "pow mismatch base %g expo %.17g: original %016llx model %016llx\n", base, expo,
              (unsigned long long)want, (unsigned long long)got);
  }
  fp80_set_cw(FPU_CW_DEFAULT);
  CHECK_EQ(bad, 0);
  CHECK_EQ(exact, total);
  printf("pow 34:0x0d84: %u operands from seg 21, %u bit-exact with the original\n", total, exact);
}

int main(void) {
  g_only = getenv("SEG21_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  stubs_init();
  g_tab = gblock(0x400);
  g_recs = gblock(0x400);
  g_pln = gblock(0x400);
  g_cur = gblock(0x100);
  g_lst = gblock(0x100);
  g_tab2 = gblock(0x2100);
  g_fpa = gblock(0x440);
  g_fptr = gblock(0x100);
  g_wp = gblock(0x100);
  g_info = gblock(0x400);
  {
    /* the model of 8:0x8730 against the original */
    for (unsigned i = 0; i < 40; i++) {
      g_rng = 0x87300000u + i;
      uint32_t s1 = rnd() | 1, s2 = rnd() | 1;
      ds_w32(0x151e, s1);
      ds_w32(0x1522, s2);
      const uint16_t a[] = {100};
      uint16_t got = (uint16_t)orig_ret(8, 0x8730, a, 1);
      CHECK_EQ(got, rng_step(&s1, &s2, 100));
    }
  }
  if (want("pow")) t_pow_34_0d84_native();
  if (want("2bd8")) t_2bd8();
  if (want("2858")) t_2858();
  if (want("272c")) t_272c_27ba(0x272c);
  if (want("27ba")) t_272c_27ba(0x27ba);
  if (want("2882")) t_2882();
  if (want("29da")) t_29da();
  if (want("1d76")) t_1d76();
  if (want("2bf8")) t_2bf8();
  if (want("1aec")) t_1aec();
  if (want("0000")) {
    g_ssn_2664 = 8;
    stub_enable(S_9_474C, true);
    stub_enable(S_9_462A, true);
    t_0000();
    stub_enable(S_9_474C, false);
    stub_enable(S_9_462A, false);
    g_ssn_2664 = 18;
  }
  if (want("123e")) t_123e();
  if (want("1e8a")) t_1e8a();
  if (want("1e2a")) t_1e2a();
  if (want("23d0")) t_23d0();
  if (want("2c74")) t_2c74();
  if (want("32aa")) {
    t_32aa();
    t_32aa_focus();
  }
  if (want("30c2")) {
    t_30c2();
    t_30c2_focus();
  }
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
