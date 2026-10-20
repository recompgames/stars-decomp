/* Diff tests for decompiled segment 6 (src/seg06.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 6 is the message window of the game (MESSAGEWNDPROC), the message store behind it and the message
 * dialog MSGDLG (see the comment at the top of src/seg06.c for the record layouts).
 * Every scenario builds its state by writing DGROUP and a few far blocks directly (and windows with the
 * runtime); DIFF then snapshots it and compares the original with the C version (return registers, all
 * memory outside the callee's own frame, the ordered Win16 API call log, which here also holds the test
 * doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's CALL_ORIG;
 * the functions under test and the decompiled callees are never doubled. See the notes of each function in
 * docs/functions.csv for which memory a watch hashes and which it does not.
 *
 * SEG06_ONLY=tag[,tag] runs only the named parts: a mutation check of one function does not need the whole
 * run. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/commdlg_thunks.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/user.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}
static uint32_t fstr(const char *s) {
  uint32_t p = gblock((uint32_t)strlen(s) + 1);
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}

/* A far copy of s in one reused scratch block (k selects one of four slots of 0x200 bytes): the global heap
 * is finite, so the loops do not allocate. */
static uint32_t sstr(unsigned k, const char *s) {
  static uint32_t blk;
  if (!blk) blk = gblock(0x800);
  uint32_t p = blk + 0x200 * (k & 3);
  size_t n = strlen(s);
  if (n > 0x1ff) n = 0x1ff;
  memcpy(LIN(p), s, n);
  ((uint8_t *)LIN(p))[n] = 0;
  return p;
}
static uint32_t sblk(void) {
  static uint32_t blk;
  if (!blk) blk = gblock(0x40);
  return blk;
}

/* ---- test doubles: logged thunks (rule j2) ---- */

/* A double replaces the first 5 bytes of a callee in the code segment image with JMP FAR to an import thunk
 * of the module "STUBS" (CDECL: the caller pops the arguments), so the same bytes serve the ORIGINAL run and
 * the C version's CALL_ORIG. Every call is an entry of the API log (module, id, the argument words as
 * pushed, the return value), in order with the real API calls and the other doubles. A watched double
 * (watch >= 0: the argument word index of a byte count, followed by a far pointer) also appends an FNV-1a
 * hash of those bytes, taken at call time, to a log in the arena, which DIFF compares as arena memory. The
 * return value is the next value queued for the id (stub_push), else the default (stub_def). Scripted
 * effects (stub_eff_ds) write DGROUP at a given call number, the same in both runs. */
#define STUB_BLKSZ 0x8000
#define STUB_DEF 0x10   /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200 /* queue length of id (word) */
#define STUB_QIDX 0x280 /* queue index of id (word) */
#define STUB_NCALL 0x300 /* call counter of id (word) */
#define STUB_WLOGN 0x380 /* number of watch hashes logged (word) */
#define STUB_Q 0x400    /* queue of id: 64 entries of 4 bytes (ax, dx) */
#define STUB_QMAX 64
#define STUB_WLOG 0x4000 /* watch hashes: 4 bytes each, up to 0x1000 */
#define STUB_WMAX 0x1000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs; /* argument words */
  int8_t watch;  /* argument word index of a byte count followed by a far pointer, -1 none */
} StubCfg;
enum { S_14_51CC, S_14_579A, S_14_305A, S_9_3E72, S_7_1C00, S_7_1902, S_7_1CCE, S_7_1A70, S_7_25CE,
       S_7_14A8, S_9_2ED8, S_10_26B2, S_26_0000, S_29_0000, S_31_61B0, S_7_063E, S_7_01A0, S_11_62F2, S_11_5142,
       S_8_8D94, S_8_9958, S_8_9318, S_NSTUB };
static const StubCfg g_stubs[S_NSTUB] = {
    [S_14_51CC] = {"14:51cc", 14, 0x51cc, 4, -1},
    [S_14_579A] = {"14:579a", 14, 0x579a, 4, 1},
    [S_14_305A] = {"14:305a", 14, 0x305a, 0, -1},
    [S_9_3E72] = {"9:3e72", 9, 0x3e72, 2, -1},
    [S_7_1C00] = {"7:1c00", 7, 0x1c00, 1, -1},
    [S_7_1902] = {"7:1902", 7, 0x1902, 1, -1},
    [S_7_1CCE] = {"7:1cce", 7, 0x1cce, 1, -1},
    [S_7_1A70] = {"7:1a70", 7, 0x1a70, 1, -1},
    [S_7_25CE] = {"7:25ce", 7, 0x25ce, 4, -1},
    [S_7_14A8] = {"7:14a8", 7, 0x14a8, 1, -1},
    [S_9_2ED8] = {"9:2ed8", 9, 0x2ed8, 2, -1},
    [S_10_26B2] = {"10:26b2", 10, 0x26b2, 2, -1},
    [S_26_0000] = {"26:0000", 26, 0x0000, 1, -1},
    [S_29_0000] = {"29:0000", 29, 0x0000, 1, -1},
    [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, -1},
    [S_7_063E] = {"7:063e", 7, 0x063e, 1, -1},
    [S_7_01A0] = {"7:01a0", 7, 0x01a0, 1, -1},
    [S_11_62F2] = {"11:62f2", 11, 0x62f2, 2, -1},
    [S_11_5142] = {"11:5142", 11, 0x5142, 3, -1},
    [S_8_8D94] = {"8:8d94", 8, 0x8d94, 2, -1},
    [S_8_9958] = {"8:9958", 8, 0x9958, 3, -1},
    [S_8_9318] = {"8:9318", 8, 0x9318, 4, -1},
};
static uint8_t g_stub_orig[S_NSTUB][5];

typedef struct StubEff {
  int call;
  uint16_t a;
  uint16_t len;
  uint8_t data[0x400];
} StubEff;
#define MAXEFF 24
static StubEff g_eff[S_NSTUB][MAXEFF];
static unsigned g_neff[S_NSTUB];

static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  if (qidx < qlen) {
    memcpy(&c->ret, blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * 4 + (size_t)qidx * 4, 4);
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
  if (cfg->watch >= 0) {
    uint16_t n = hcall_word(c, (unsigned)cfg->watch);
    uint32_t p = (uint32_t)hcall_word(c, (unsigned)cfg->watch + 1) | ((uint32_t)hcall_word(c, (unsigned)cfg->watch + 2) << 16);
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < n; i++) {
      h ^= *(uint8_t *)LIN(p + i);
      h *= 16777619u;
    }
    uint16_t k;
    memcpy(&k, blk + STUB_WLOGN, 2);
    if (k < STUB_WMAX) {
      memcpy(blk + STUB_WLOG + 4 * (size_t)k, &h, 4);
      k++;
      memcpy(blk + STUB_WLOGN, &k, 2);
    }
  }
  uint16_t ncall;
  memcpy(&ncall, blk + STUB_NCALL + 2 * c->ordinal, 2);
  for (unsigned k = 0; k < g_neff[c->ordinal]; k++) {
    const StubEff *e = &g_eff[c->ordinal][k];
    if (e->call != -1 && e->call != (int)ncall) continue;
    memcpy(LINN(e->a), e->data, e->len);
  }
  ncall++;
  memcpy(blk + STUB_NCALL + 2 * c->ordinal, &ncall, 2);
}
static void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
__attribute__((unused)) static void stub_push(unsigned id, uint16_t ax, uint16_t dx) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t n;
  memcpy(&n, blk + STUB_QLEN + 2 * id, 2);
  CHECK(n < STUB_QMAX);
  if (n >= STUB_QMAX) return;
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy(blk + STUB_Q + (size_t)id * STUB_QMAX * 4 + (size_t)n * 4, &v, 4);
  n++;
  memcpy(blk + STUB_QLEN + 2 * id, &n, 2);
}
static void stubs_clear_all(void) {
  harness_log_clear();
  uint8_t *blk = LIN(g_stub_blk);
  for (unsigned id = 0; id < S_NSTUB; id++) {
    memset(blk + STUB_QLEN + 2 * id, 0, 2);
    memset(blk + STUB_QIDX + 2 * id, 0, 2);
    memset(blk + STUB_NCALL + 2 * id, 0, 2);
    stub_def(id, 0, 0);
    g_neff[id] = 0;
  }
  memset(blk + STUB_WLOGN, 0, 2);
}
__attribute__((unused)) static void stub_eff_ds(unsigned id, int call, uint16_t off, const void *data, uint16_t len) {
  CHECK(g_neff[id] < MAXEFF && len <= sizeof g_eff[0][0].data);
  StubEff *e = &g_eff[id][g_neff[id]++];
  e->call = call;
  e->a = off;
  e->len = len;
  memcpy(e->data, data, len);
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(g_stub_orig[i], LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), sizeof c);
    memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
  }
  stubs_clear_all();
}
/* Takes a double away again / puts it back (the drawing helpers 8:0x9958 and 8:0x9318 are doubles only in
 * the scenario that needs them). */
static void stub_uninstall(unsigned id) {
  const StubCfg *cfg = &g_stubs[id];
  memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), g_stub_orig[id], 5);
}
static void stub_install(unsigned id) {
  const StubCfg *cfg = &g_stubs[id];
  uint32_t th = harness_add_import("STUBS", (uint16_t)id, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
  uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
  memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
}

/* ---- the message store ---- */

/* The message buffer: a 64 KB block; DS:0xaac/0xaae point into it (at offset g_boff), DS:0xab0 is the used
 * length, DS:0xab4 the message count. */
static uint32_t g_buf;
static uint16_t g_boff;
#define SCR 0x3000 /* a scratch area in DGROUP the scenarios write */
static int16_t nvals_of(uint16_t id) { return (int16_t)(int8_t)*(uint8_t *)LIN(MK_FP(SEG_SEL(6), (uint16_t)(0x0e74 + id))); }
/* A message id: mostly valid ones (0 .. 0x180), with the special ones of the functions. */
static uint16_t rid(void) {
  static const uint16_t sp[] = {7, 0x23, 0x40, 0x8f, 0x1ff, 0, 0x180, 0x3e, 0x3f, 0xaf, 0xb4, 0x35, 0x2b, 0x91};
  return rn(4) == 0 ? sp[rn(sizeof sp / 2)] : (uint16_t)rn(0x181);
}
static void buf_reset(void) {
  g_boff = (uint16_t)(rn(4) == 0 ? 0x10 * rn(16) : 0);
  memset(LIN(g_buf), 0xcc, 0x10000);
  DS_W(0x0aac) = (uint16_t)(LO(g_buf) + g_boff);
  DS_W(0x0aae) = HI(g_buf);
  DS_W(0x0ab0) = 0;
  DS_W(0x0ab4) = 0;
}
static uint8_t *buf_end(void) { return (uint8_t *)LIN(g_buf) + g_boff + DS_W(0x0ab0); }
/* Appends a 4-byte-header record (the player's own file layout). */
static void put4(uint16_t id, uint16_t subject) {
  uint8_t *p = buf_end();
  uint16_t fl = (uint16_t)(rnd() & 0x7f);
  uint16_t w0 = (uint16_t)((id & 0x1ff) | (fl << 9));
  memcpy(p, &w0, 2);
  memcpy(p + 2, &subject, 2);
  unsigned n = 4;
  int16_t c = nvals_of(id & 0x1ff);
  for (int16_t k = 0; k < c; k++) {
    if (k < 7 && (fl >> k & 1)) {
      uint16_t v = (uint16_t)rnd();
      memcpy(p + n, &v, 2);
      n += 2;
    } else {
      p[n++] = (uint8_t)rnd();
    }
  }
  DS_W(0x0ab0) = (uint16_t)(DS_W(0x0ab0) + n);
  DS_W(0x0ab4) = (uint16_t)(DS_W(0x0ab4) + 1);
}
/* Appends a 5-byte-header record (the turn layout): player in the low nibble of byte 0, the length of
 * the values in the high nibble. */
static void put5(uint16_t player, uint16_t id, uint16_t subject, const uint16_t *vals, unsigned nv) {
  uint8_t *p = buf_end();
  uint16_t fl = 0;
  unsigned n = 5;
  int16_t c = nvals_of(id & 0x1ff);
  for (int16_t k = 0; k < c && k < 7; k++) {
    uint16_t v = (unsigned)k < nv ? vals[k] : (uint16_t)rnd();
    if (v >> 8) {
      fl = (uint16_t)(fl | (1u << k));
      memcpy(p + n, &v, 2);
      n += 2;
    } else {
      p[n++] = (uint8_t)v;
    }
  }
  p[0] = (uint8_t)(((n - 5) << 4) | (player & 0xf));
  uint16_t w = (uint16_t)((id & 0x1ff) | (fl << 9));
  memcpy(p + 1, &w, 2);
  memcpy(p + 3, &subject, 2);
  DS_W(0x0ab0) = (uint16_t)(DS_W(0x0ab0) + n);
  DS_W(0x0ab4) = (uint16_t)(DS_W(0x0ab4) + 1);
}
static void rand_bits(uint16_t off, unsigned n, unsigned dens) {
  for (unsigned i = 0; i < n; i++) {
    uint8_t b = 0;
    for (unsigned k = 0; k < 8; k++)
      if (rn(8) < dens) b = (uint8_t)(b | (1u << k));
    DS_B((uint16_t)(off + i)) = b;
  }
}
/* A buffer of n 4-byte-header records with random filter / occurred bits. */
static void world4(unsigned n) {
  buf_reset();
  for (unsigned i = 0; i < n; i++) put4(rid(), (uint16_t)rnd());
  rand_bits(0x5508, 49, rn(9));
  rand_bits(0x534e, 49, rn(9));
}

/* ---- 6:0x7668 / 0x7646: read message idx ---- */
static void t_7668(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x76680001u + 0x9e3779b9u * (it + 1);
    unsigned n = rn(5) == 0 ? 0 : 1 + rn(12);
    world4(n);
    if (rn(6) == 0) DS_W(0x0ab4) = (uint16_t)(n + 1 + rn(3)); /* the buffer ends before the count */
    if (rn(10) == 0) DS_W(0x0ab0) = (uint16_t)(DS_W(0x0ab0) - 1 - rn(3));
    memset(&DS_B(SCR), 0xee, 0x40);
    int16_t idx = (int16_t)rn(n + 4) - 2;
    if (rn(20) == 0) idx = (int16_t)(rn(2) ? 0x7fff : 0x8000);
    DIFF(6, 0x7668, FAR_CDECL, (uint16_t)idx, SCR);
    DIFF(6, 0x7646, FAR_CDECL, (uint16_t)idx);
  }
}

/* ---- 6:0x6ef4 / 0x6f66: the next / previous message ---- */
static void t_6ef4(void) {
  for (unsigned it = 0; it < 1200; it++) {
    g_rng = 0x6ef40001u + 0x9e3779b9u * (it + 1);
    unsigned n = rn(6) == 0 ? 0 : 1 + rn(10);
    world4(n);
    DS_W(0x0aba) = rn(3) == 0 ? (uint16_t)(1 + rn(3)) : 0;
    DS_W(0x0ac4) = (uint16_t)(rn(3) == 0 ? 0 : rn(4));
    int16_t cur = (int16_t)rn(n + 6) - 1;
    if (rn(15) == 0) cur = (int16_t)(rn(2) ? 0x7fff : 0x8000);
    DS_W(0x0ab6) = (uint16_t)cur;
    static const uint16_t flags[] = {0, 1, 0, 1, 2};
    uint16_t f = flags[rn(5)];
    DIFF(6, 0x6ef4, FAR_CDECL, f);
    DIFF(6, 0x6f66, FAR_CDECL, f);
  }
}

/* ---- 6:0x7294: the hit test of the message window ---- */
static void t_7294(void) {
  for (unsigned it = 0; it < 2000; it++) {
    g_rng = 0x72940001u + 0x9e3779b9u * (it + 1);
    int16_t l = (int16_t)rn(100), t = (int16_t)rn(50);
    int16_t r = (int16_t)(l + 50 + rn(300)), b = (int16_t)(t + 10 + rn(80));
    DS_W(0x4b64) = (uint16_t)l;
    DS_W(0x4b66) = (uint16_t)t;
    DS_W(0x4b68) = (uint16_t)r;
    DS_W(0x4b6a) = (uint16_t)b;
    unsigned n = rn(6);
    DS_W(0x0ab4) = (uint16_t)n;
    DS_W(0x0ab6) = (uint16_t)((int16_t)rn(n + 3) - 1);
    DS_B(0x0791) = (uint8_t)rnd();
    DS_B(0x0080) = (uint8_t)rnd();
    rand_bits(0x5508, 49, rn(3));
    rand_bits(0x534e, 49, rn(3));
    if (rn(3) == 0) {
      memset(&DS_B(0x5508), 0, 49);
      if (rn(2)) DS_B((uint16_t)(0x5508 + rn(49))) = 0xff;
    }
    /* points at the borders: the text area (left + height), the filter area (right - height), the
     * button (right - height - 0x18) */
    int16_t h = (int16_t)(b - t);
    const int16_t xs[] = {(int16_t)(l + h), (int16_t)(r - h), (int16_t)(r - h - 0x18), l, (int16_t)(r - 1), r};
    int16_t x = (int16_t)(xs[rn(6)] + (int16_t)rn(3) - 1);
    int16_t y = (int16_t)(t + (int16_t)rn((uint32_t)h + 2) - 1);
    if (rn(10) == 0) x = (int16_t)rnd();
    DIFF(6, 0x7294, FAR_CDECL, (uint16_t)x, (uint16_t)y);
  }
}

/* ---- 6:0x7410 / 0x7344 / 0x7328 / 0x739e: new messages (5-byte header) ---- */
static void players_rows(void) {
  for (unsigned k = 0; k < 16; k++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * k));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    /* the word +6 (DS:0x5a6a): bit 9 "no messages", bits 13-15 all set lift it */
    uint16_t w = (uint16_t)rnd();
    switch (rn(4)) {
      case 0: w = (uint16_t)(w & ~0x0200u); break;
      case 1: w = (uint16_t)((w | 0x0200u) & ~0xe000u); break;
      case 2: w = (uint16_t)(w | 0xe200u); break;
      default: break;
    }
    memcpy(row + 6, &w, 2);
  }
}
static uint16_t rval(void) {
  switch (rn(4)) {
    case 0: return (uint16_t)rn(0x100);
    case 1: return (uint16_t)rnd();
    case 2: return (uint16_t)(0x100 * (1 + rn(255)));
    default: return (uint16_t)rn(0x300);
  }
}
static void msg_world(void) {
  buf_reset();
  players_rows();
  unsigned n = rn(6);
  for (unsigned i = 0; i < n; i++) {
    uint16_t v[7];
    for (unsigned k = 0; k < 7; k++) v[k] = rval();
    put5((uint16_t)rn(16), rid(), (uint16_t)rnd(), v, 7);
  }
  if (rn(8) == 0) DS_W(0x0ab0) = (uint16_t)(0xffb4 - 2 + rn(5)); /* around "nearly full" */
}
static void t_7410(void) {
  for (unsigned it = 0; it < 1600; it++) {
    g_rng = 0x74100001u + 0x9e3779b9u * (it + 1);
    msg_world();
    for (unsigned k = 0; k < 0x30; k++) DS_B((uint16_t)(SCR + k)) = (uint8_t)rnd();
    uint16_t pl = rn(10) == 0 ? 0xffff : (uint16_t)rn(16);
    uint16_t id = rid();
    if (id == 0x1ff) id = (uint16_t)rn(0x181);
    uint16_t v[7];
    for (unsigned k = 0; k < 7; k++) v[k] = rval();
    uint16_t sub = (uint16_t)rnd();
    switch (it % 4) {
      case 0:
        DIFF(6, 0x7410, FAR_CDECL, SCR, pl, id, sub, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        break;
      case 1:
        DIFF(6, 0x7344, FAR_CDECL, pl, id, sub, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        break;
      case 2:
        DIFF(6, 0x739e, FAR_CDECL, pl, id, sub, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
        break;
      default:
        DIFF(6, 0x7328, FAR_CDECL, pl, id, sub, v[0], v[1]);
        break;
    }
  }
  /* the four ids a muted player still gets, against neighbours */
  static const uint16_t ids[] = {7, 0x23, 0x40, 0x8f, 6, 8, 0x22, 0x24, 0x3f, 0x41, 0x8e, 0x90};
  for (unsigned i = 0; i < sizeof ids / 2; i++) {
    g_rng = 0x74100101u + i;
    msg_world();
    DS_W(0x5a6a + 0xc0 * 3) = 0x0200;
    DS_W(0x0ab0) = 0x100;
    DIFF(6, 0x7344, FAR_CDECL, 3, ids[i], 0x55, 1, 2, 3, 0x400, 5, 6, 7);
    DIFF(6, 0x7410, FAR_CDECL, SCR, 3, ids[i], 0x55, 1, 2, 3, 0x400, 5, 6, 7);
  }
  /* the full-buffer limit: DS:0xab0 + 0x14 against 0xffc8 */
  for (uint16_t u = 0xffb2; u != 0xffba; u++) {
    msg_world();
    DS_W(0x5a6a) = 0;
    DS_W(0x0ab0) = u;
    DIFF(6, 0x7410, FAR_CDECL, SCR, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    DIFF(6, 0x7344, FAR_CDECL, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
  }
}

/* ---- 6:0x752e: a message of the player's own file (4-byte header) ---- */
static void t_752e(void) {
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x752e0001u + 0x9e3779b9u * (it + 1);
    world4(rn(8));
    DS_W(0x0aba) = (uint16_t)rn(2);
    DS_W(0x0ac4) = (uint16_t)rn(3);
    DS_W(0x0ab6) = (uint16_t)rnd();
    if (rn(8) == 0) DS_W(0x0ab0) = (uint16_t)(0xffb4 - 2 + rn(5));
    uint16_t id = rid();
    if (id == 0x1ff) id = (uint16_t)rn(0x181);
    uint16_t v[7];
    for (unsigned k = 0; k < 7; k++) v[k] = rval();
    uint16_t front = rn(3) == 0 ? 0 : (uint16_t)(rn(2) ? 1 : rnd());
    DIFF(6, 0x752e, FAR_CDECL, front, id, (uint16_t)rnd(), v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
  }
}

/* ---- 6:0x80fa / 0x8154 / 0x81b4: find, delete and resolve the planets of messages ---- */
/* Planets (DS:0xb6 far pointer, 0x38 bytes each, id word +0, owner +2; count DS:0x4d96, DS:0x7a): read by
 * 7:0x01e4 (planet by id), which runs as ORIGINAL code in both runs. */
#define MAXP 8
static uint32_t g_pl;
static void planets(void) {
  uint8_t *pl = LIN(g_pl);
  uint16_t id = (uint16_t)rn(3);
  unsigned np = 1 + rn(MAXP);
  for (unsigned i = 0; i < np; i++) {
    uint16_t ow = (uint16_t)rn(4);
    memcpy(pl + 0x38 * i, &id, 2);
    memcpy(pl + 0x38 * i + 2, &ow, 2);
    id = (uint16_t)(id + 1 + rn(3));
  }
  ds_w32(0x00b6, g_pl);
  DS_W(0x4d96) = (uint16_t)np;
  DS_W(0x007a) = (uint16_t)np;
}
static void world5(void) {
  buf_reset();
  unsigned n = rn(10);
  for (unsigned i = 0; i < n; i++) {
    uint16_t v[7];
    for (unsigned k = 0; k < 7; k++) v[k] = rn(2) ? (uint16_t)rn(20) : rval();
    static const uint16_t ids[] = {7, 0x23, 0x40, 0x8f, 0x1ff, 0x41, 0x90, 0xa0, 0x113, 0x116};
    uint16_t id = rn(2) ? ids[rn(10)] : rid();
    put5((uint16_t)rn(4), id, rn(2) ? (uint16_t)rn(20) : (uint16_t)rnd(), v, 7);
  }
}
static void t_80fa(void) {
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x80fa0001u + 0x9e3779b9u * (it + 1);
    world5();
    /* ask for a record that exists (often) */
    uint16_t pl = (uint16_t)rn(4), id = rid(), sub = (uint16_t)rn(20);
    if (DS_W(0x0ab0) && rn(3)) {
      uint8_t *p = (uint8_t *)LIN(g_buf) + g_boff;
      pl = p[0] & 0xf;
      id = (uint16_t)((p[1] | p[2] << 8) & 0x1ff);
      sub = (uint16_t)(p[3] | p[4] << 8);
      if (rn(4) == 0) pl = (uint16_t)(pl + 16);
    }
    if (rn(10) == 0) DS_W(0x0ab0) = 0;
    DIFF(6, 0x80fa, FAR_CDECL, pl, id, sub);
    DIFF(6, 0x8154, FAR_CDECL, pl, id, sub);
  }
}
static void t_81b4(void) {
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x81b40001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    planets();
    world5();
    if (rn(10) == 0) DS_W(0x0ab0) = 0;
    DIFF(6, 0x81b4, FAR_CDECL, (uint16_t)rn(5));
  }
}

/* ---- 6:0x829a / 0x8308: the comment list and the turn file ---- */
/* The comment list DS:0xac0: nodes of a far block, +0 next, +4 player, +6 a word (0 or player + 1), +8, +0xa a
 * signed text length, +0xc the text. */
static uint32_t g_nodes;
static void comments(void) {
  unsigned n = rn(6);
  uint32_t next = 0;
  for (unsigned i = n; i-- > 0;) {
    uint8_t *q = (uint8_t *)LIN(g_nodes) + 0x80 * i;
    for (unsigned k = 0; k < 0x80; k++) q[k] = (uint8_t)rnd();
    memcpy(q, &next, 4);
    uint16_t pl = (uint16_t)rn(4);
    memcpy(q + 4, &pl, 2);
    uint16_t w6 = rn(3) == 0 ? 0 : (uint16_t)rn(6);
    memcpy(q + 6, &w6, 2);
    int16_t len = (int16_t)rn(0x60);
    if (rn(3) == 0) len = (int16_t)-len;
    memcpy(q + 0xa, &len, 2);
    next = g_nodes + 0x80 * i;
  }
  ds_w32(0x0ac0, next);
}
static void t_829a(void) {
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x829a0001u + 0x9e3779b9u * (it + 1);
    players_rows();
    comments();
    DIFF(6, 0x829a, FAR_CDECL, rn(8) == 0 ? 0xffff : (uint16_t)rn(5));
  }
}
static void t_8308(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x83080001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    comments();
    world5();
    if (it % 5 == 0) {
      /* many records of one player: the 1024-byte buffer is written in parts */
      buf_reset();
      unsigned n = 60 + rn(200);
      for (unsigned i = 0; i < n; i++) {
        uint16_t v[7];
        for (unsigned k = 0; k < 7; k++) v[k] = rval();
        put5(rn(4) ? 1 : 2, rn(10) == 0 ? 0x1ff : rid(), (uint16_t)rnd(), v, 7);
      }
    }
    DIFF(6, 0x8308, FAR_CDECL, rn(8) == 0 ? 0xffff : (uint16_t)rn(4));
  }
}

/* ---- 6:0x841c: empty the store ---- */
static void t_841c(void) {
  for (unsigned it = 0; it < 4; it++) {
    for (unsigned k = 0; k < 0x30; k++) DS_W((uint16_t)(0x0aa8 + 2 * k)) = (uint16_t)rnd();
    rand_bits(0x534e, 52, 4);
    rand_bits(0x5508, 52, 4);
    DIFF(6, 0x841c, FAR_CDECL);
  }
}

/* ---- 6:0x8864: unpack the format string of a message id ---- */
static void t_8864(void) {
  for (uint16_t id = 0; id < 0x1c0; id++) {
    memset(&DS_B(0x5276), 0xee, 0x100);
    DS_B(0x0bb4) = 0xff;
    DIFF(6, 0x8864, FAR_CDECL, id);
  }
  static const uint8_t cache[] = {0, 1, 0x3f, 0x40, 0x7f, 0x80, 0xff};
  for (unsigned i = 0; i < sizeof cache; i++) {
    DS_B(0x0bb4) = cache[i];
    DIFF(6, 0x8864, FAR_CDECL, (uint16_t)(int16_t)(int8_t)cache[i]);
    DIFF(6, 0x8864, FAR_CDECL, cache[i]);
  }
}

/* ---- 6:0x8960: a filter bit and the ids that go with it ---- */
static void t_8960(void) {
  static const uint16_t ons[] = {0, 1, 2, 0xffff};
  for (int id = -12; id < 0x200; id++) {
    for (unsigned o = 0; o < 4; o++) {
      g_rng = 0x89600001u + (uint32_t)(id + 100) * 8 + o;
      rand_bits(0x5500, 0x48, 4);
      DIFF(6, 0x8960, FAR_CDECL, (uint16_t)id, ons[o]);
    }
  }
}


/* ---- 6:0x8460: load the messages and comments of the turn file ---- */
/* Double: 14:0x305a (read the next block) does nothing real; its scripted effects put the next block's header
 * word (DS:0x2910) and data (DS:0x4db2) into DGROUP at each call. 34:0x0800 (setjmp), 34:0x0c98 (copy),
 * 12:0x0232 (heap allocation, decompiled; heap 8 starts empty) and 6:0x6ef4 run for real. */
#define HEAD(h) (*(uint32_t *)LINN((uint16_t)(0x0D74 + 4 * (h))))
/* Frees the segments of heap 8 made by earlier scenarios (outside any DIFF) and empties the heap. */
static void heap8_reset(void) {
  uint32_t hs = HEAD(8);
  for (unsigned guard = 0; hs != 0 && guard < 64; guard++) {
    const struct HeapSeg *h = LIN(hs);
    uint32_t next = h->next;
    uint16_t hd = h->handle;
    (void)GlobalUnlock(hd);
    (void)GlobalFree(hd);
    hs = next;
  }
  HEAD(8) = 0;
  harness_log_clear();
}
static void chain_abc(void) {
  unsigned n = rn(4);
  uint32_t next = 0;
  for (unsigned i = n; i-- > 0;) {
    uint8_t *q = (uint8_t *)LIN(g_nodes) + 0x80 * i;
    for (unsigned k = 0; k < 0x80; k++) q[k] = (uint8_t)rnd();
    memcpy(q, &next, 4);
    next = g_nodes + 0x80 * i;
  }
  ds_w32(0x0abc, next);
}
static void t_8460(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x84600001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world4(rn(5));
    if (rn(8) == 0) DS_W(0x0ab0) = (uint16_t)(0xff00 + rn(0xc0));
    DS_W(0x0aba) = (uint16_t)rn(2);
    DS_W(0x0ac4) = (uint16_t)rn(3);
    DS_W(0x006e) = (uint16_t)rnd();
    heap8_reset();
    chain_abc();
    /* the blocks: type 12 (messages), then type 40 (comments), then another type */
    unsigned n12 = rn(4), n40 = rn(4), nb = n12 + n40 + 1;
    uint16_t hdr[12];
    static uint8_t data[12][0x400];
    for (unsigned b = 0; b < nb; b++) {
      unsigned type = b < n12 ? 12 : b < n12 + n40 ? 40 : (rn(2) ? 0 : 13);
      unsigned len = 0;
      if (type == 12) {
        /* records of the 4-byte layout */
        uint16_t sv = DS_W(0x0ab0), sc = DS_W(0x0ab4);
        uint8_t *at = buf_end();
        unsigned nr = rn(6);
        for (unsigned r = 0; r < nr; r++) put4(rid(), (uint16_t)rnd());
        len = (unsigned)(buf_end() - at);
        memcpy(data[b], at, len);
        DS_W(0x0ab0) = sv;
        DS_W(0x0ab4) = sc;
        if (rn(10) == 0) len = 0;
      } else if (type == 40) {
        len = 0xc + rn(0x40);
        for (unsigned k = 0; k < len; k++) data[b][k] = (uint8_t)rnd();
      } else {
        len = rn(0x20);
      }
      hdr[b] = (uint16_t)(type << 10 | len);
    }
    DS_W(0x2910) = hdr[0];
    memcpy(&DS_B(0x4db2), data[0], hdr[0] & 0x3ff);
    for (unsigned b = 1; b < nb; b++) {
      stub_eff_ds(S_14_305A, (int)b - 1, 0x2910, &hdr[b], 2);
      stub_eff_ds(S_14_305A, (int)b - 1, 0x4db2, data[b], (uint16_t)(hdr[b] & 0x3ff));
    }
    DIFF(6, 0x8460, FAR_CDECL);
  }
}

/* ---- 6:0x8640: store the edited comment ---- */
/* The comment window DS:0x59e6 is a runtime EDIT control, DS:0x53e0 a runtime COMBOBOX (its selection is the
 * comment's subject word), the text buffer DS:0x53ea a far block of 0x800 bytes. The nodes of the list
 * DS:0xac0 are heap-8 blocks made with the ORIGINAL 12:0x0232 before the DIFF. 8:0xa57e (the packer),
 * 9:0x641c, 34:0x0c98 and the heap functions 12:0x0232 / 0x03ec / 0x04d0 (decompiled) run for real. */
static uint16_t g_edit, g_combo, g_cwin;
static uint32_t g_text;
static uint32_t o_alloc8(uint16_t size) {
  uint16_t a[] = {size, 8};
  return harness_call(SEG_SEL(12), 0x0232, HCONV_CDECL, a, 2, NULL);
}
static void comment_windows(void) {
  if (g_cwin) return;
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG06TEST");
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
  g_cwin = CreateWindow(fstr("SEG06TEST"), fstr("w"), WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0, 0x1234, 0);
  CHECK(g_cwin != 0);
  g_edit = CreateWindow(fstr("EDIT"), fstr(""), WS_CHILD | WS_VISIBLE, 0, 0, 300, 40, g_cwin, 5, 0x1234, 0);
  CHECK(g_edit != 0);
  g_combo = CreateWindow(fstr("COMBOBOX"), fstr(""), WS_CHILD | WS_VISIBLE | 3, 0, 50, 200, 100, g_cwin, 6, 0x1234, 0);
  CHECK(g_combo != 0);
  for (unsigned i = 0; i < 5; i++) (void)SendMessage(g_combo, 0x0403, 0, fstr("item"));
  g_text = gblock(0x800);
}
static void t_8640(void) {
  comment_windows();
  static const char *texts[] = {"", "a", "Hello", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                "The quick brown fox jumps over the lazy dog", "ab", "abc abc abc abc abc abc"};
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x86400001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    heap8_reset();
    ds_w32(0x53ea, g_text);
    memset(LIN(g_text), 0xdd, 0x800);
    DS_W(0x59e6) = g_edit;
    DS_W(0x53e0) = g_combo;
    DS_W(0x014c) = (uint16_t)rn(4);
    DS_W(0x0ab6) = (uint16_t)rnd();
    unsigned sel = rn(5);
    (void)SendMessage(g_combo, 0x040e, (uint16_t)sel, 0);
    const char *t = texts[rn(7)];
    static char rt[0x80];
    if (rn(3) == 0) {
      /* high-entropy text: the packer may refuse it (the text is then stored as it is) */
      unsigned len = 1 + rn(0x70);
      for (unsigned k = 0; k < len; k++) rt[k] = (char)(0x21 + rn(0x5e));
      rt[len] = 0;
      t = rt;
    }
    /* the list: heap-8 nodes; some of them hold the same text as the edit control */
    unsigned n = rn(5);
    uint32_t nodes[5];
    for (unsigned i = 0; i < n; i++) {
      const char *nt = rn(3) == 0 ? t : texts[rn(7)];
      uint16_t len = (uint16_t)strlen(nt);
      nodes[i] = o_alloc8((uint16_t)(len + 0xc));
      CHECK(nodes[i] != 0);
      uint8_t *q = LIN(nodes[i]);
      for (unsigned k = 0; k < 0xc; k++) q[k] = (uint8_t)rnd();
      uint16_t w6 = rn(2) ? (uint16_t)sel : (uint16_t)rn(5);
      memcpy(q + 6, &w6, 2);
      uint16_t l = rn(4) ? (uint16_t)~len : len;
      memcpy(q + 0xa, &l, 2);
      memcpy(q + 0xc, nt, len);
    }
    for (unsigned i = 0; i < n; i++) {
      uint32_t nx = i + 1 < n ? nodes[i + 1] : 0;
      memcpy(LIN(nodes[i]), &nx, 4);
    }
    ds_w32(0x0ac0, n ? nodes[0] : 0);
    DS_W(0x0ac6) = (uint16_t)n;
    DS_W(0x0ab8) = (uint16_t)rn(n + 1);
    SetWindowText(g_edit, sstr(0, t));
    harness_log_clear();
    static const uint16_t dirs[] = {0, 1, 0xffff, 0x03e8, 2};
    DIFF(6, 0x8640, FAR_CDECL, dirs[rn(5)]);
  }
}


/* ---- 6:0x7778 / 0x80c8 / 0x80e0 / 0x774a: the message text ---- */
/* Doubles (logged thunks; they return a near pointer to a fixed DS string, "Name1" .. "Name6", from
 * name_doubles()): 9:0x3e72, 7:0x1c00, 7:0x1902, 7:0x1cce, 7:0x1a70, 7:0x25ce (the names of planets, fleets,
 * things). Run for real: 7:0x0b84 (the player names; the player rows get short names), 8:0x8dc0 / 0x8df4
 * (string resources), 2:0x50f8 (decompiled; item names of the tech tables), 6:0x8864 and 6:0x7668 (decompiled
 * here, original code through rt_call_far), _wsprintf. The designs of the 'z' conversion are far blocks of
 * 16 records of 0x8d bytes per player (DS:0xbe + 4p and DS:0x10c + 4p). */
#define NAMES (SCR + 0x500)
static void name_doubles(void) {
  static const unsigned ids[] = {S_9_3E72, S_7_1C00, S_7_1902, S_7_1CCE, S_7_1A70, S_7_25CE};
  for (unsigned i = 0; i < 6; i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "Name%u", i + 1);
    strcpy((char *)&DS_B((uint16_t)(NAMES + 0x10 * i)), nm);
    stub_def(ids[i], (uint16_t)(NAMES + 0x10 * i), 0);
  }
}
static void sane_names(void) {
  for (unsigned k = 0; k < 16; k++) {
    for (unsigned f = 0x80; f <= 0xa0; f += 0x20) {
      uint8_t *nm = LINN((uint16_t)(0x5a64 + 0xc0 * k + f));
      unsigned len = rn(4) == 0 ? 0 : rn(16);
      memset(nm, 0, 0x20);
      for (unsigned i = 0; i < len; i++) nm[i] = (uint8_t)(rn(5) == 0 ? ' ' : 'a' + rn(26));
      if (rn(3) == 0) nm[len] = (uint8_t)(rn(2) ? 's' : 'e'), nm[len + 1] = 0;
    }
  }
}
static uint32_t g_designs[8];
static void designs(void) {
  for (unsigned b = 0; b < 8; b++) {
    if (!g_designs[b]) g_designs[b] = gblock(0x8d * 16 + 0x10);
    uint8_t *q = LIN(g_designs[b]);
    for (unsigned r = 0; r < 16; r++) {
      unsigned len = rn(12);
      for (unsigned k = 0; k < len; k++) q[0x8d * r + 8 + k] = (uint8_t)('A' + rn(26));
      q[0x8d * r + 8 + len] = 0;
    }
    /* 0xbe + 4p: slots 0..15; 0x10c + 4p: slots 16..31 (the pointer minus 16 records) */
    if (b < 4)
      ds_w32((uint16_t)(0x00be + 4 * b), g_designs[b]);
    else
      ds_w32((uint16_t)(0x010c + 4 * (b - 4)), g_designs[b]);
  }
}
static const uint16_t g_kinds[][2] = {{0x1, 0x10}, {0x2, 0x10}, {0x4, 0x0a}, {0x8, 0x0c}, {0x10, 0x18}, {0x20, 0x0c},
                                      {0x40, 0x0f}, {0x80, 0x08}, {0x100, 0x0a}, {0x200, 0x10}, {0x400, 0x05},
                                      {0x800, 0x11}, {0x1000, 0x0b}, {0x2000, 0x14}, {0x4000, 0x20}, {0x8000, 0x0f}};
static void fmt_world(void) {
  stubs_clear_all();
  name_doubles();
  players_rows();
  sane_names();
  designs();
  DS_W(0x0078) = (uint16_t)(1 + rn(16));
  DS_W(0x014c) = rn(5) == 0 ? 0xffff : (uint16_t)rn(4);
  memset(&DS_B(0x553e), 0xee, 0x200);
  strcpy((char *)&DS_B(0x5764), rn(4) ? "GAME" : "LONGERNAME");
  strcpy((char *)&DS_B(0x5866), rn(2) ? "comment text" : "");
}
/* Argument words for the conversions of format f (a host copy), in the order 6:0x7778 reads them; cur is
 * DS:0x14c (with 0xffff '\f' and '\t' take no word). A conversion that takes no word ('\Z' with 0) is only
 * generated last, so that the next one does not read its word as well. */
static unsigned args_for(const char *f, uint16_t *a, unsigned max, uint16_t cur) {
  char cv[256];
  unsigned nc = 0;
  for (const char *c = f; *c && nc < sizeof cv; c++) {
    if (*c != '\\') continue;
    c++;
    if (!*c) break;
    cv[nc++] = *c;
  }
  unsigned n = 0;
  bool small = false; /* the word after a 'V' is also its plural count (an index of DS:0xb66) */
#define PUT(v) do { if (n < max) a[n] = small ? (uint16_t)rn(6) : (uint16_t)(v); small = false; n++; } while (0)
  for (unsigned i = 0; i < nc; i++) {
    switch (cv[i]) {
      case 'E': case 'F': case 'g': case 'p': case 's': case 'X': case 'I': case 'i': case 'u': PUT(rn(4) ? rn(40) : rnd()); break;
      case 'o': PUT(rn(3) ? rn(40) : rnd()); break;
      case 'G': PUT(rn(6)); break;
      case 'j': PUT(rn(20)); break;
      case 'L': case 'l': PUT(rnd() & 0xff); break;
      case 'O': PUT(rnd()); break;
      case 'S': PUT(rn(16)); break;
      case 'M': case 'e': PUT(rn(3)); break;
      case 'm': PUT(rn(6)); break;
      case 'P': { static const int16_t pv[] = {999, 1000, 1001, -999, -1000, 0, 99, 100, 5, -5, 32767, -32768, 1099};
                  PUT(rn(2) ? (uint16_t)pv[rn(13)] : rnd()); break; }
      case 'U': case 'v': PUT(rnd()); PUT(rn(2) ? 0 : rnd()); break;
      case 'V': PUT(rnd()); PUT(rn(2) ? 0 : rnd()); if (n < max) a[n] = (uint16_t)rn(6); small = true; break;
      case 'Z': { static const uint16_t zv[] = {1, 2, 4, 0x8000, 3, 5, 0x15, 0xffff, 0x8001, 0x10, 0};
                  uint16_t v = zv[rn(i + 1 == nc ? 11 : 10)];
                  if (v == 0) { if (n < max) a[n] = 0; } else PUT(v);
                  break; }
      case 'k': { unsigned k = small ? rn(3) : rn(16); /* after 'V' the kind is also the count: 1, 2 or 4 */
                  small = false;
                  PUT(g_kinds[k][0]); PUT((rnd() & 0xff00) | rn(g_kinds[k][1])); break; }
      case 'n': { unsigned m = rn(4); if (m == 0) { PUT(0xfffe); PUT(rn(40)); } else if (m == 1) { PUT(0xffff); PUT(rn(2) ? rn(40) : 0x8000 | rn(40)); }
                  else { PUT(rn(40)); PUT(rn(40)); } break; }
      case 'z': PUT((rn(4) << 5) | rn(32)); break;
      case 'r': PUT(rnd()); break;
      case 'f': case 't': if (cur != 0xffff) PUT(rnd()); break;
      default: break;
    }
  }
#undef PUT
  return n;
}
static uint32_t g_args;
static void t_7778(void) {
  /* the inventory's size (73) stops at the jump table: measure the whole body to 0x7ed0 */
  difftest_set_extent(6, 0x7778, 0x7ed0 - 0x7778, "format_message");
  static const char *convs = "EFGILMOPSUVXZefghijklmnoprstuvwyz";
  static const char *lits[] = {"", "a", "The ", " of ", "%", "x\\", "\\\\", "\\Q", "\\a", "\\D", "\\7", "\\\x80", "\\~", "\\{"};
  for (unsigned it = 0; it < 3000; it++) {
    g_rng = 0x77780001u + 0x9e3779b9u * (it + 1);
    fmt_world();
    char f[160] = "";
    unsigned nt = 1 + rn(6);
    for (unsigned t = 0; t < nt; t++) {
      if (rn(3) == 0) {
        strcat(f, lits[rn(14)]);
      } else {
        char c[3] = {'\\', convs[rn((uint32_t)strlen(convs))], 0};
        strcat(f, c);
        if (rn(2)) strcat(f, " ");
      }
    }
    if (strlen(f) && f[strlen(f) - 1] == '\\' && rn(2)) f[strlen(f) - 1] = 0;
    /* zeros after the format: a trailing backslash copies the NUL and reads one byte further (a format
     * that goes on there would run with arguments it was not given) */
    memset(&DS_B(SCR), 0, 0x200);
    strcpy((char *)&DS_B(SCR), f);
    uint16_t a[64];
    memset(a, 0, sizeof a);
    for (unsigned k = 0; k < 64; k++) a[k] = (uint16_t)rn(30);
    (void)args_for(f, a, 60, DS_W(0x014c));
    memcpy(LIN(g_args), a, sizeof a);
    if (getenv("SEG06_TRACE")) fprintf(stderr, "7778 it %u fmt \"%s\"\n", it, f);
    if (DIFF(6, 0x7778, FAR_CDECL, SCR, LO(g_args), HI(g_args)) != DT_PASS) fprintf(stderr, "  7778 fmt \"%s\"\n", f);
  }
}
/* The format of message id, from the ORIGINAL 6:0x8864 (setup, not under test). */
static void msg_format(uint16_t id, char *out, size_t n) {
  uint16_t a[1] = {id};
  DS_B(0x0bb4) = 0xff;
  uint16_t p = (uint16_t)harness_call(SEG_SEL(6), 0x8864, HCONV_CDECL, a, 1, NULL);
  snprintf(out, n, "%s", (char *)&DS_B(p));
}
static void t_80c8(void) {
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x80c80001u + 0x9e3779b9u * (it + 1);
    fmt_world();
    uint16_t id = (uint16_t)(it < 0x181 ? it : rn(0x181));
    char f[400];
    msg_format(id, f, sizeof f);
    uint16_t a[64];
    for (unsigned k = 0; k < 64; k++) a[k] = (uint16_t)rn(30);
    (void)args_for(f, a, 60, DS_W(0x014c));
    memcpy(LIN(g_args), a, sizeof a);
    memset(&DS_B(0x5276), 0xee, 0x100);
    DS_B(0x0bb4) = rn(10) == 0 ? (uint8_t)id : 0xff;
    if (DIFF(6, 0x80c8, FAR_CDECL, id, LO(g_args), HI(g_args)) != DT_PASS) fprintf(stderr, "  80c8 id %x \"%s\"\n", id, f);
  }
}
/* The strings of 8:0x8df4 (the original, setup): 0x80e0 formats them. */
static void t_80e0(void) {
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x80e00001u + 0x9e3779b9u * (it + 1);
    fmt_world();
    uint16_t id = (uint16_t)(it < 0x300 ? it : rn(0x300));
    uint16_t a1[1] = {id};
    uint16_t p = (uint16_t)harness_call(SEG_SEL(8), 0x8df4, HCONV_CDECL, a1, 1, NULL);
    char f[400];
    snprintf(f, sizeof f, "%s", (char *)&DS_B(p));
    uint16_t a[64];
    for (unsigned k = 0; k < 64; k++) a[k] = (uint16_t)rn(30);
    (void)args_for(f, a, 60, DS_W(0x014c));
    memcpy(LIN(g_args), a, sizeof a);
    if (DIFF(6, 0x80e0, FAR_CDECL, id, LO(g_args), HI(g_args)) != DT_PASS) fprintf(stderr, "  80e0 id %x \"%s\"\n", id, f);
  }
}
/* Appends a 4-byte-header record with the given values (a word value gets its flag bit). */
static void put4v(uint16_t id, uint16_t subject, const uint16_t *v) {
  uint8_t *p = buf_end();
  uint16_t fl = 0;
  unsigned n = 4;
  int16_t c = nvals_of(id & 0x1ff);
  for (int16_t k = 0; k < c; k++) {
    if (k < 7 && (v[k] >> 8)) {
      fl = (uint16_t)(fl | (1u << k));
      memcpy(p + n, &v[k], 2);
      n += 2;
    } else {
      p[n++] = (uint8_t)v[k];
    }
  }
  uint16_t w0 = (uint16_t)((id & 0x1ff) | (fl << 9));
  memcpy(p, &w0, 2);
  memcpy(p + 2, &subject, 2);
  DS_W(0x0ab0) = (uint16_t)(DS_W(0x0ab0) + n);
  DS_W(0x0ab4) = (uint16_t)(DS_W(0x0ab4) + 1);
}
static void t_774a(void) {
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x774a0001u + 0x9e3779b9u * (it + 1);
    fmt_world();
    buf_reset();
    unsigned n = rn(6);
    for (unsigned i = 0; i < n; i++) {
      uint16_t id = (uint16_t)rn(0x181);
      char f[400];
      msg_format(id, f, sizeof f);
      uint16_t a[64];
      for (unsigned k = 0; k < 64; k++) a[k] = (uint16_t)rn(30);
      (void)args_for(f, a, 60, DS_W(0x014c));
      put4v(id, (uint16_t)rnd(), a);
    }
    DS_B(0x0bb4) = 0xff;
    DIFF(6, 0x774a, FAR_CDECL, (uint16_t)((int16_t)rn(n + 3) - 1));
  }
}


/* ---- the message window: 6:0x6fc6, 6:0x6a68, MSGDLG and MESSAGEWNDPROC ---- */
/* The windows are real runtime windows: the message window g_mw and the main window g_main of a test class
 * (its procedure answers 1 to every message), the buttons (BUTTON), the combo box (COMBOBOX) and the two
 * edit controls (EDIT) of the runtime. ShowWindow, SetWindowPos, EnableWindow, SetWindowText, SendMessage,
 * the GDI calls (a memory DC and two bitmaps of the test) and DrawText run for real in both runs; so do
 * 8:0x9958 / 0x9318 / 0x9398 / 0x9482 / 0x96e6 (drawing and rectangle helpers), 8:0xa666 (the unpacker),
 * 8:0x8dc0 / 0x8df4, 7:0x0b84, 7:0x3ef4, 9:0x641c and the dialog MSGDLG behind DialogBox (original code,
 * answered by queued keys). Doubles (logged thunks, nothing real, the scenario's return values): 7:0x14a8 (a
 * fleet by id), 9:0x2ed8, 10:0x26b2 (select a planet / fleet), 26:0x0000, 29:0x0000, 31:0x61b0, 7:0x063e,
 * 7:0x01a0, 11:0x62f2, 11:0x5142, 8:0x8d94 (a message box), and those of the message text (see t_7778). */
static uint16_t g_mw, g_main, g_other, g_btn[4], g_cb, g_ed1, g_ed2, g_bmp1, g_bmp2, g_dlg, g_dlged;
static uint16_t mkwin(const char *cls, const char *title, uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr(cls), fstr(title), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
  CHECK(h != 0);
  return h;
}
static void input_reset(void) {
  uint32_t m = sblk();
  rt_input_key(VK_SHIFT, 0x2a, 0, 0);
  rt_input_key(VK_CONTROL, 0x1d, 0, 0);
  for (unsigned i = 0; i < 2000 && PeekMessage(m, 0, 0, 0, 1); i++) {
  }
  (void)GetAsyncKeyState(VK_SHIFT);
  (void)GetAsyncKeyState(VK_CONTROL);
  harness_log_clear();
}
static void key_tap(uint16_t vk, uint16_t scan) {
  rt_input_key(vk, scan, 1, 0);
  rt_input_key(vk, scan, 0, 0);
}
static void mw_windows(void) {
  if (g_mw) return;
  comment_windows(); /* registers SEG06TEST */
  g_main = mkwin("SEG06TEST", "main", WS_OVERLAPPED | WS_VISIBLE, 0, 0, 640, 480, 0, 0);
  g_mw = mkwin("SEG06TEST", "messages", WS_OVERLAPPED | WS_VISIBLE, 20, 20, 500, 200, 0, 0);
  g_other = mkwin("SEG06TEST", "other", WS_OVERLAPPED | WS_VISIBLE, 30, 300, 200, 100, 0, 0);
  for (unsigned k = 0; k < 4; k++) g_btn[k] = mkwin("BUTTON", "b", WS_CHILD | WS_VISIBLE, 400, 10 + 30 * (int)k, 40, 20, g_mw, (uint16_t)(10 + k));
  g_cb = mkwin("COMBOBOX", "", WS_CHILD | WS_VISIBLE | 3, 30, 50, 200, 100, g_mw, 20);
  for (unsigned i = 0; i < 6; i++) (void)SendMessage(g_cb, 0x0403, 0, fstr("item"));
  g_ed1 = mkwin("EDIT", "", WS_CHILD | WS_VISIBLE, 30, 80, 300, 40, g_mw, 21);
  g_ed2 = mkwin("EDIT", "", WS_CHILD | WS_VISIBLE, 30, 130, 300, 40, g_mw, 22);
  g_dlg = mkwin("SEG06TEST", "dialog", WS_OVERLAPPED | WS_VISIBLE, 100, 100, 300, 200, 0, 0);
  g_dlged = mkwin("EDIT", "", WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, g_dlg, 0x10c);
  uint16_t dc = GetDC(g_mw);
  g_bmp1 = CreateCompatibleBitmap(dc, 32, 0x60);
  g_bmp2 = CreateCompatibleBitmap(dc, 32, 0x60);
  CHECK(g_bmp1 != 0 && g_bmp2 != 0);
  ReleaseDC(g_mw, dc);
}
/* A packed text (the ORIGINAL 8:0xa57e, setup) at far p: returns the packed length, 0 when it does not pack. */
static uint16_t pack_text(const char *t, uint32_t p) {
  uint32_t src = sstr(1, t);
  DS_W(SCR + 0x7f0) = (uint16_t)strlen(t);
  uint16_t a[5] = {LO(src), HI(src), LO(p), HI(p), SCR + 0x7f0};
  uint16_t r = (uint16_t)harness_call(SEG_SEL(8), 0xa57e, HCONV_CDECL, a, 5, NULL);
  return r ? DS_W(SCR + 0x7f0) : 0;
}
static const char *const g_ctexts[] = {"", "Hello there", "Attack at dawn. Bring the cruisers.", "x", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
/* The comments of the chain DS:0xabc (heap-8 blocks of the original allocator): +4 the sender, +6 0 or the
 * receiver + 1, +8 a message index, +0xa the length (negative: raw text), +0xc the text. */
static void abc_comments(unsigned n) {
  uint32_t next = 0;
  for (unsigned i = 0; i < n; i++) {
    const char *t = g_ctexts[rn(5)];
    uint16_t len = (uint16_t)strlen(t);
    uint32_t q = o_alloc8((uint16_t)(len + 0x10 + 0x40));
    CHECK(q != 0);
    uint8_t *b = LIN(q);
    memcpy(b, &next, 4);
    uint16_t w4 = (uint16_t)rn(4), w6 = rn(3) ? 0 : (uint16_t)(1 + rn(4)), w8 = (uint16_t)rn(8);
    memcpy(b + 4, &w4, 2);
    memcpy(b + 6, &w6, 2);
    memcpy(b + 8, &w8, 2);
    uint16_t plen = rn(3) == 0 ? pack_text(t, q + 0xc) : 0;
    uint16_t l = plen && plen < len + 0x40 ? plen : (uint16_t)~len;
    if ((int16_t)l < 0) memcpy(b + 0xc, t, len + 1);
    memcpy(b + 0xa, &l, 2);
    next = q;
  }
  ds_w32(0x0abc, next);
}
/* The player's own comments, list DS:0xac0 (as in t_8640). */
static void ac0_comments(unsigned n) {
  uint32_t nodes[6];
  for (unsigned i = 0; i < n; i++) {
    const char *t = g_ctexts[rn(5)];
    uint16_t len = (uint16_t)strlen(t);
    nodes[i] = o_alloc8((uint16_t)(len + 0xc + 0x40));
    CHECK(nodes[i] != 0);
    uint8_t *q = LIN(nodes[i]);
    uint16_t w4 = (uint16_t)rn(4), w6 = (uint16_t)rn(5), w8 = (uint16_t)rn(8);
    memcpy(q + 4, &w4, 2);
    memcpy(q + 6, &w6, 2);
    memcpy(q + 8, &w8, 2);
    uint16_t plen = rn(3) == 0 ? pack_text(t, nodes[i] + 0xc) : 0;
    uint16_t l = plen && plen < len + 0x40 ? plen : (uint16_t)~len;
    if ((int16_t)l < 0) memcpy(q + 0xc, t, len + 1);
    memcpy(q + 0xa, &l, 2);
  }
  for (unsigned i = 0; i < n; i++) {
    uint32_t nx = i + 1 < n ? nodes[i + 1] : 0;
    memcpy(LIN(nodes[i]), &nx, 4);
  }
  ds_w32(0x0ac0, n ? nodes[0] : 0);
  DS_W(0x0ac6) = (uint16_t)n;
}
static void mw_state(void) {
  mw_windows();
  fmt_world();
  input_reset();
  heap8_reset();
  DS_W(0x0158) = g_mw;
  DS_W(0x2806) = g_main;
  DS_W(0x0150) = g_other;
  DS_W(0x5658) = g_other;
  DS_W(0x0312) = rn(3) ? 0 : g_other;
  for (unsigned k = 0; k < 4; k++) DS_W((uint16_t)(0x4d9e + 2 * k)) = g_btn[k];
  DS_W(0x53e0) = g_cb;
  DS_W(0x59e6) = g_ed1;
  DS_W(0x54fe) = g_ed2;
  DS_W(0x15fe) = rn(12) == 0 ? 1 : 0;
  DS_W(0x007e) = (uint16_t)rn(2);
  DS_W(0x0082) = (uint16_t)rn(40);
  DS_W(0x267a) = (uint16_t)(12 + rn(6));
  DS_W(0x2686) = (uint16_t)(12 + rn(6));
  DS_W(0x266c) = (uint16_t)rn(200);
  DS_W(0x2930) = GetStockObject(13);
  DS_W(0x0010) = GetStockObject(rn(2));
  DS_W(0x0014) = GetStockObject(4);
  DS_W(0x0016) = GetStockObject(1);
  DS_W(0x5a54) = g_bmp1;
  DS_W(0x2884) = g_bmp2;
  ds_w32(0x2870, 0x00102030);
  ds_w32(0x253a, 0x00c0c0c0);
  DS_W(0x5442) = LoadCursor(0, 0x00007f00u);
  DS_W(0x54f4) = MEM_SEL_DGROUP;
  ds_w32(0x53ea, g_text);
  memset(LIN(g_text), 0xdd, 0x800);
  DS_W(0x010a) = SCR + 0x7c0;
  strcpy((char *)&DS_B(SCR + 0x7c0), "STARS.HLP");
  DS_B(0x0791) = (uint8_t)rnd();
  DS_B(0x0080) = (uint8_t)rnd();
  DS_B(0x53f0) = (uint8_t)rnd();
  DS_W(0x0aba) = (uint16_t)(rn(3) == 0);
  /* the rectangles of WM_SIZE for a 500 x 200 window */
  int16_t w = (int16_t)(300 + rn(300)), h = (int16_t)(120 + rn(200)), fh = (int16_t)DS_W(0x267a);
  DS_W(0x2532) = 4;
  DS_W(0x2534) = (uint16_t)(fh * 2 + 3);
  DS_W(0x2536) = (uint16_t)(w - 0x34);
  DS_W(0x2538) = (uint16_t)(h - 4);
  if (rn(3) == 0) DS_W(0x2538) = (uint16_t)(DS_W(0x2534) + 8 + rn(16)); /* a low text area: long texts go to the edit control */
  DS_W(0x4b64) = 4;
  DS_W(0x4b66) = 4;
  DS_W(0x4b68) = (uint16_t)(w - 4);
  DS_W(0x4b6a) = (uint16_t)((fh - 2) * 2);
  strcpy((char *)&DS_B(0x4a66), rn(4) ? "Year 2410: Message 3 of 7 from somebody with a very long title line" : "");
  /* the messages: records of the 4-byte layout whose values suit their formats */
  buf_reset();
  unsigned n = rn(7);
  for (unsigned i = 0; i < n; i++) {
    /* the ids for which the "goto planet" of MESSAGEWNDPROC also opens the planet view (0x3e, 0x3f, 0xaf..0xb4) */
    static const uint16_t gotoids[] = {0x3e, 0x3f, 0xaf, 0xb0, 0xb2, 0xb4, 0xae, 0xb5, 0x3d, 0x40};
    uint16_t id = rn(4) == 0 ? gotoids[rn(10)] : (uint16_t)rn(0x181);
    char f[400];
    msg_format(id, f, sizeof f);
    uint16_t a[64];
    for (unsigned k = 0; k < 64; k++) a[k] = (uint16_t)rn(30);
    (void)args_for(f, a, 60, DS_W(0x014c));
    static const uint16_t subj[] = {0xffff, 0xfffe, 0xfffd, 0xfffc, 0xfffb, 0xfffa, 0xfff9, 0xc005, 0x4800, 0x4123, 0x0007, 0x8012};
    /* 6:0x6a68 reads the first value of a 0xfffa subject and the first two of a 0x4000 one (not 0x4800) from
     * the record 6:0x7668 wrote into its frame: a message with fewer values leaves stack residue there, which
     * differs between the runs inside a caller (see the notes), so such subjects go only with enough values */
    uint16_t sj = rn(3) ? subj[rn(12)] : (uint16_t)rnd();
    int16_t nv = nvals_of(id);
    if ((sj == 0xfffa && nv < 1) || ((sj & 0xc000) == 0x4000 && (sj & 0x3fff) != 0x800 && nv < 2) ||
        ((sj & 0xc000) == 0x4000 && nv < 2))
      sj = 0x0007;
    put4v(id, sj, a);
  }
  rand_bits(0x5508, 49, rn(5));
  rand_bits(0x534e, 49, rn(5));
  DS_W(0x0ac4) = (uint16_t)rn(4);
  abc_comments(DS_W(0x0ac4));
  ac0_comments(rn(4));
  DS_W(0x0ab8) = (uint16_t)rn(DS_W(0x0ac6) + 1);
  DS_W(0x0ac8) = (uint16_t)rn(4);
  int16_t cur = (int16_t)rn(n + DS_W(0x0ac4) + 2) - 1;
  if (cur >= (int16_t)(n + DS_W(0x0ac4))) cur = (int16_t)(n + DS_W(0x0ac4) - 1);
  DS_W(0x0ab6) = (uint16_t)cur;
  DS_W(0x415c) = (uint16_t)rn(13);
  DS_W(0x4160) = (uint16_t)rn(20);
  DS_W(0x4156) = (uint16_t)rn(20);
  DS_W(0x4158) = (uint16_t)rn(20);
  DS_W(0x4b74) = (uint16_t)rn(3);
  DS_W(0x4b78) = rn(2) ? DS_W(0x4160) : (uint16_t)rn(3);
  DS_W(0x014e) = (uint16_t)rn(2);
  DS_W(0x177e) = (uint16_t)rn(2);
  DS_W(0x1766) = rn(2) ? 0x16f0 : 0x164e;
  ds_w32(0x0872, rn(2) ? 0 : rnd());
  stub_def(S_7_14A8, rn(2) ? 0x1234 : 0, rn(2) ? 0x5678 : 0);
  stub_def(S_7_01A0, rn(3) ? (uint16_t)LO(g_nodes) : 0, rn(3) ? HI(g_nodes) : 0);
  memset(&DS_B(SCR), 0, 0x20);
}
static void t_6fc6(void) {
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x6fc60001u + 0x9e3779b9u * (it + 1);
    mw_state();
    uint16_t dc = GetDC(g_mw);
    DIFF(6, 0x6fc6, FAR_CDECL, dc, 0x4b64);
    ReleaseDC(g_mw, dc);
  }
}
static void t_6a68(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x6a680001u + 0x9e3779b9u * (it + 1);
    mw_state();
    DIFF(6, 0x6a68, FAR_CDECL, rn(15) ? g_mw : 0);
  }
}
static void t_7ed0(void) {
  mw_windows();
  static const char *texts[] = {"", "12", "abc", "0", "99999999", "-3", "1a"};
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x7ed00001u + 0x9e3779b9u * (it + 1);
    mw_state();
    DS_B(0x588e) = (uint8_t)rn(3);
    SetWindowText(g_dlged, sstr(0, texts[rn(7)]));
    harness_log_clear();
    uint16_t dc = GetDC(g_dlg);
    static const uint16_t msgs[] = {0x000f, 0x0014, 0x0019, 0x0110, 0x0111, 0x0111, 0x0111, 0x0000, 0x0010, 0x0015, 0x0018, 0x001a, 0x010f, 0x0112};
    uint16_t m = msgs[rn(14)];
    uint16_t wp = m == 0x0111 ? (uint16_t)(rn(3) ? 1 + rn(2) : rn(2) ? 0x76 : rn(200)) : m == 0x0014 || m == 0x0019 ? dc : (uint16_t)rnd();
    uint16_t lhi = m == 0x0019 ? (uint16_t)(rn(2) ? 6 : rn(8)) : (uint16_t)rnd();
    DIFF(6, 0x7ed0, FAR_PASCAL, rn(10) ? g_dlg : g_other, m, wp, lhi, (uint16_t)rnd());
    ReleaseDC(g_dlg, dc);
  }
}
#define MW(m, wp, lphi, lplo)                                                                                  \
  ({                                                                                                         \
    if (getenv("SEG06_TRACE"))                                                                               \
      fprintf(stderr, "MW msg %x wp %x lp %x:%x ab4 %u ab6 %d ac4 %u 791 %x 415c %u\n", (m), (wp), (lphi), (lplo), DS_W(0x0ab4), \
              (int16_t)DS_W(0x0ab6), DS_W(0x0ac4), DS_B(0x0791), DS_W(0x415c));                                \
    DIFF(6, 0x0000, FAR_PASCAL, g_mw, (m), (wp), (lphi), (lplo));                                            \
  })
static void mw_screen_point(int cx, int cy, int *sx, int *sy) {
  uint32_t pt = sblk() + 0x20;
  POINT16 *p = LIN(pt);
  p->x = (int16_t)cx;
  p->y = (int16_t)cy;
  ClientToScreen(g_mw, pt);
  *sx = p->x;
  *sy = p->y;
}
static void t_0000(void) {
  difftest_set_extent(6, 0x0000, 0x0e74, "MESSAGEWNDPROC");
  /* the plain messages */
  for (unsigned it = 0; it < 160; it++) {
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    mw_state();
    switch (it % 10) {
      case 0: MW(0x0001, 0, 0, 0); break;
      case 1: MW(0x0005, (uint16_t)rn(3), (uint16_t)(100 + rn(400)), (uint16_t)(200 + rn(500))); break;
      case 2: case 3: case 4:
        /* WM_PAINT only with a message shown outside the comment mode: otherwise 6:0x6fc6 restores the text
         * and background colours from two words of its frame that it never wrote, stack residue of the
         * callees before it (their saved SI / BP), which differs between the runs (see the notes) */
        if (DS_W(0x0ab4) == 0) goto plain;
        DS_B(0x0791) = (uint8_t)(DS_B(0x0791) & ~1u);
        DS_W(0x0ab6) = (uint16_t)rn(DS_W(0x0ab4));
        MW(0x000f, 0, 0, 0);
        break;
      case 5: { uint16_t dc = GetDC(g_mw); MW(0x0014, dc, 0, 0); ReleaseDC(g_mw, dc); break; }
      case 6: { uint16_t dc = GetDC(g_mw); MW(0x0019, dc, 6, rn(2) ? g_ed2 : g_ed1); ReleaseDC(g_mw, dc); break; }
      case 7: { uint32_t mm = sstr(2, ""); memset(LIN(mm), 0x5a, 0x20); MW(0x0024, 0, HI(mm), LO(mm)); break; }
      case 8: { static const uint16_t ms[] = {0x0000, 0x0002, 0x0003, 0x0006, 0x0010, 0x0015, 0x0018, 0x001a, 0x001f, 0x0021, 0x0023, 0x0025,
                                              0x0101, 0x0103, 0x0110, 0x0112, 0x0200, 0x0202, 0x0204, 0x0400};
                MW(ms[rn(20)], (uint16_t)rnd(), (uint16_t)rnd(), (uint16_t)rnd()); break; }
      default:
      plain:
        MW(0x0003, 0, 0, 0);
        break;
    }
  }
  /* WM_PAINT in the comment mode, past the messages and with none shown: 6:0x6fc6 then restores its colours
   * from two words of its frame it never wrote; with the drawing helpers 8:0x9958 / 8:0x9318 as doubles (their
   * frames would leave their saved registers there, which differ between the runs) those words are the
   * snapshot's in both runs */
  stub_install(S_8_9958);
  stub_install(S_8_9318);
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x00000051u + 0x9e3779b9u * (it + 1);
    mw_state();
    switch (it % 3) {
      case 0: DS_B(0x0791) = (uint8_t)(DS_B(0x0791) | 1); break;
      case 1: DS_B(0x0791) = (uint8_t)(DS_B(0x0791) & ~1u); DS_W(0x0ab6) = (uint16_t)(DS_W(0x0ab4) + rn(DS_W(0x0ac4) + 1)); break;
      default: DS_B(0x0791) = (uint8_t)(DS_B(0x0791) & ~1u); DS_W(0x0ab6) = 0xffff; break;
    }
    /* past the messages the comment DS:0xab6 - DS:0xab4 must exist (the walk would read through a null pointer) */
    if (it % 3 == 1 && DS_W(0x0ab6) >= DS_W(0x0ab4) + DS_W(0x0ac4)) {
      if (DS_W(0x0ac4) == 0) continue;
      DS_W(0x0ab6) = (uint16_t)(DS_W(0x0ab4) + rn(DS_W(0x0ac4)));
    }
    MW(0x000f, 0, 0, 0);
  }
  stub_uninstall(S_8_9958);
  stub_uninstall(S_8_9318);
  /* WM_SETCURSOR: the cursor over the regions of 6:0x7294 */
  for (unsigned it = 0; it < 60; it++) {
    g_rng = 0x00000101u + 0x9e3779b9u * (it + 1);
    mw_state();
    int16_t h = (int16_t)(DS_W(0x4b6a) - DS_W(0x4b66));
    const int xs[] = {10, (int)(int16_t)DS_W(0x4b68) - h - 2, (int)(int16_t)DS_W(0x4b68) - 5, 600};
    int sx, sy;
    mw_screen_point(xs[rn(4)], 6, &sx, &sy);
    rt_input_mouse((int16_t)sx, (int16_t)sy, 0, 0);
    MW(0x0020, g_mw, 0, 1);
  }
  /* WM_KEYDOWN, WM_CHAR */
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x00000201u + 0x9e3779b9u * (it + 1);
    mw_state();
    if (rn(3) == 0) rt_input_key(VK_SHIFT, 0x2a, 1, 0);
    if (rn(3) == 0) key_tap(VK_ESCAPE, 0x01);
    if (rn(2)) {
      static const uint16_t keys[] = {0x28, 0x26, 0x24, 0x23, 0x25, 0x0d};
      MW(0x0100, keys[rn(6)], 0, 1);
    } else {
      static const uint16_t chars[] = {0x0d, 0x2b, 0x2d, 0x2d, 0x61};
      MW(0x0102, chars[rn(5)], 0, 1);
    }
  }
  /* WM_COMMAND from the buttons; the goto actions */
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x00000301u + 0x9e3779b9u * (it + 1);
    mw_state();
    if (rn(3) == 0) rt_input_key(VK_SHIFT, 0x2a, 1, 0);
    if (DS_W(0x415c) == 9 && rn(2)) {
      /* the dialog MSGDLG: a number typed and Return, or Escape */
      static const char *nums[] = {"12", "abc", "7"};
      const char *t = nums[rn(3)];
      for (const char *q = t; *q; q++) key_tap((uint16_t)(*q >= 'a' ? *q - 0x20 : *q), 0x10);
      key_tap(VK_RETURN, 0x1c);
    }
    key_tap(VK_ESCAPE, 0x01);
    key_tap(VK_ESCAPE, 0x01);
    uint16_t ctl = rn(6) == 0 ? g_other : g_btn[rn(4)];
    if (rn(3) == 0) ctl = g_btn[1]; /* goto */
    if (rn(6) == 0) {
      /* one past the end (as after End): only for previous / next; a goto there walks the comment chain one node
       * too far and reads through a null pointer */
      DS_W(0x0ab6) = (uint16_t)(DS_W(0x0ab4) + DS_W(0x0ac4));
      ctl = g_btn[rn(2) * 2];
    }
    MW(0x0111, (uint16_t)rn(30), rn(8) == 0 ? (uint16_t)1 : 0, ctl);
  }
  /* WM_LBUTTONDOWN / WM_LBUTTONDBLCLK on the regions */
  for (unsigned it = 0; it < 200; it++) {
    g_rng = 0x00000401u + 0x9e3779b9u * (it + 1);
    mw_state();
    int16_t h = (int16_t)(DS_W(0x4b6a) - DS_W(0x4b66));
    const int16_t xs[] = {10, (int16_t)(DS_W(0x4b68) - h - 2), (int16_t)(DS_W(0x4b68) - 5), 600, (int16_t)(DS_W(0x4b68) - h - 0x18 + 1)};
    MW(rn(2) ? 0x0201 : 0x0203, 1, (uint16_t)(5 + rn(3)), (uint16_t)xs[rn(5)]);
  }
  input_reset();
}

/* The module itself: the dialog templates and bitmaps are resources of the exe (kernel_load). */
static void load_module(void) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}

int main(void) {
  g_only = getenv("SEG06_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  load_module();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  commdlg_register();
  toolhelp_win87em_register();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u));
  stubs_init();
  stub_uninstall(S_8_9958);
  stub_uninstall(S_8_9318);
  g_buf = gblock(0x10000);
  CHECK_EQ(LO(g_buf), 0);
  g_pl = gblock(0x38 * MAXP);
  g_nodes = gblock(0x80 * 8);
  g_args = gblock(0x100);
  if (want("7668")) t_7668();
  if (want("6ef4")) t_6ef4();
  if (want("7294")) t_7294();
  if (want("7410")) t_7410();
  if (want("752e")) t_752e();
  if (want("80fa")) t_80fa();
  if (want("81b4")) t_81b4();
  if (want("829a")) t_829a();
  if (want("8308")) t_8308();
  if (want("841c")) t_841c();
  if (want("8864")) t_8864();
  if (want("8960")) t_8960();
  if (want("8460")) t_8460();
  if (want("8640")) t_8640();
  if (want("7778")) t_7778();
  if (want("80c8")) t_80c8();
  if (want("80e0")) t_80e0();
  if (want("774a")) t_774a();
  if (want("6fc6")) t_6fc6();
  if (want("6a68")) t_6a68();
  if (want("7ed0")) t_7ed0();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
