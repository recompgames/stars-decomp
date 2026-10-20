/* Diff tests for decompiled segment 26 (src/seg26.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 26 is the planet production screens:
 *   26:0x0000 run the production queue dialog of the selected planet
 *   26:0x0100 copy the planet's production queue and list the items that can be added to it
 *   26:0x0810 commit or discard the edited production queue
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered API call log, which here holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. A double returns the next value queued for it
 * (a queue per callee kept in the arena, so a snapshot restore rewinds it) and hashes, at every call,
 * the state the call could observe (see stub_handler), so a call made before or after a write it
 * should follow shows as a difference in the hash log, which DIFF compares as arena memory. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
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
static uint32_t ds_r32(uint16_t off) {
  uint32_t v;
  memcpy(&v, LINN(off), 4);
  return v;
}

static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }
static bool chance(uint32_t num, uint32_t den) { return rn(den) < num; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}


/* Windows. The class procedure answers every message with 1 (MOV AX,1; XOR DX,DX; RETF 10). */
static uint32_t fstr(const char *s) {
  uint32_t p = gblock((uint32_t)strlen(s) + 1);
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}
static void register_test_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG26TEST");
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG26TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
  CHECK(h != 0);
  return h;
}
static uint16_t mkctl(const char *cls, uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr(cls), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
  CHECK(h != 0);
  return h;
}

/* ---- the test doubles ---- */

#define STUB_BLKSZ 0x8000
#define STUB_DEF 0x10   /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200 /* queue length of id (word) */
#define STUB_QIDX 0x280 /* queue index of id (word) */
#define STUB_Q 0x400    /* queue of id: 32 entries of 4 bytes (ax, dx) */
#define STUB_QMAX 32
#define STUB_LOG0 0x4000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs; /* argument words */
  uint8_t poke;  /* 1 + the argument word index of a near pointer (SS) to 8 bytes (four cost words) the double fills from the queued value, 0 none */
} StubCfg;
enum {
  S_7_0358, S_9_431C, S_9_07E0, S_31_61B0, S_8_8D94, S_9_49EA, S_9_4BEC, S_9_4D0E, S_9_4F76, S_9_51BC, S_7_1CCE, S_9_4DE8, S_23_0756, S_7_46FC, S_7_3B60, S_10_7A92, S_9_9398, S_9_2112, S_24_0E24, S_24_091E, S_9_3F16, S_9_3F3C, S_8_96E6, S_8_9958, S_8_97A2, S_9_2ED8, S_9_3082, S_34_0800, S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    [S_7_0358] = {"7:0358", 7, 0x0358, 2, 0},   [S_9_431C] = {"9:431c", 9, 0x431c, 5, 0},
    [S_9_07E0] = {"9:07e0", 9, 0x07e0, 2, 0},   [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, 0},
    [S_8_8D94] = {"8:8d94", 8, 0x8d94, 2, 0},   [S_9_49EA] = {"9:49ea", 9, 0x49ea, 3, 0},
    [S_9_4BEC] = {"9:4bec", 9, 0x4bec, 3, 0},   [S_9_4D0E] = {"9:4d0e", 9, 0x4d0e, 3, 0},
    [S_9_4F76] = {"9:4f76", 9, 0x4f76, 3, 0},   [S_9_51BC] = {"9:51bc", 9, 0x51bc, 2, 0},
    [S_7_1CCE] = {"7:1cce", 7, 0x1cce, 1, 0},
    [S_9_4DE8] = {"9:4de8", 9, 0x4de8, 3, 0},     [S_23_0756] = {"23:0756", 23, 0x0756, 9, 0},
    [S_7_46FC] = {"7:46fc", 7, 0x46fc, 3, 0},
    [S_24_0E24] = {"24:0e24", 24, 0x0e24, 8, 0}, [S_24_091E] = {"24:091e", 24, 0x091e, 3, 0},
    [S_8_96E6] = {"8:96e6", 8, 0x96e6, 3, 0}, [S_8_9958] = {"8:9958", 8, 0x9958, 3, 0}, [S_8_97A2] = {"8:97a2", 8, 0x97a2, 3, 0},
    [S_9_2ED8] = {"9:2ed8", 9, 0x2ed8, 2, 0}, [S_9_3082] = {"9:3082", 9, 0x3082, 2, 0},
    [S_34_0800] = {"34:0800", 34, 0x0800, 1, 0},
    [S_9_3F16] = {"9:3f16", 9, 0x3f16, 3, 0}, [S_9_3F3C] = {"9:3f3c", 9, 0x3f3c, 3, 0},
    [S_9_9398] = {"8:9398", 8, 0x9398, 6, 0}, [S_9_2112] = {"9:2112", 9, 0x2112, 7, 0},
    [S_7_3B60] = {"7:3b60", 7, 0x3b60, 4, 4}, [S_10_7A92] = {"10:7a92", 10, 0x7a92, 3, 3},
};

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
/* The DGROUP state the doubles hash at every call: the planet copy and its queue pointers, the lists. */
static void stub_handler(HCall *c) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  uint32_t h = 2166136261u;
  h = fnv(h, LINN(0x4c08), 0x38);
  h = fnv(h, LINN(0x0cd2), 8);
  h = fnv(h, LINN(0x5a62), 2);
  h = fnv(h, LINN(0x6686), 2);
  h = fnv(h, LINN(0x0790), 2);
  h = fnv(h, LINN(0x53f1), 1);
  h = fnv(h, LINN(0x5866), 0x40);
  uint32_t lst = ds_r32(0x0cd2);
  if (lst != 0 && mem_check(lst, 4)) {
    unsigned n = ((uint8_t *)LIN(lst))[3];
    if (mem_check(lst, (uint32_t)(4 + 4 * n))) h = fnv(h, LIN(lst), 4 + 4 * n);
  }
  uint16_t buf = DS_W(0x5a62);
  if (buf) h = fnv(h, LINN(buf), 0x100);
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
  if (qidx < qlen) {
    memcpy(&c->ret, blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * 4 + (size_t)qidx * 4, 4);
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
  if (c->ordinal == S_9_2112) memcpy(LINN(0x5866), "desc", 5); /* the text 9:0x2112 builds */
  if (g_stubs[c->ordinal].poke > 0) {
    uint16_t p = hcall_word(c, (unsigned)(g_stubs[c->ordinal].poke - 1));
    uint16_t v[4] = {(uint16_t)c->ret, (uint16_t)(c->ret >> 16), (uint16_t)(c->ret + (c->ret >> 16)), (uint16_t)(c->ret ^ (c->ret >> 16))};
    if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), v, 8);
  }
  if (c->ordinal == S_23_0756) { /* the status word goes to the near pointer in argument word 7; the DX of the queued value is the status */
    uint16_t p = hcall_word(c, 7);
    uint16_t st = (uint16_t)(c->ret >> 16);
    if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), &st, 2);
    c->ret &= 0xffff;
  }
}
static void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
static void stubs_clear_all(void) {
  uint8_t *blk = LIN(g_stub_blk);
  for (unsigned id = 0; id < S_NSTUB; id++) {
    memset(blk + STUB_QLEN + 2 * id, 0, 2);
    memset(blk + STUB_QIDX + 2 * id, 0, 2);
    stub_def(id, 0, 0);
  }
  uint16_t at = STUB_LOG0;
  memcpy(blk, &at, 2);
}
static void stub_push(unsigned id, uint16_t ax, uint16_t dx) {
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

/* ---- the scenario world: the selected planet, its queue, the player tables ---- */

static uint32_t g_designs;   /* the starbase design array of the current player (DS:0x10c + 4 * player) */
static uint32_t g_ships;     /* the ship design array of the current player (DS:0xbe + 4 * player) */

/* A queue block from the game's own typed-array allocator (12:0x0568, heap 0, run as original code): header
 * (capacity at +2, count at +3), then n entries of random bits. */
static uint32_t make_queue_cap(unsigned n, unsigned extra) {
  const uint16_t w[] = {4, (uint16_t)((n ? n : 1) + extra), 0};
  uint32_t q = harness_call(MEM_SEL_SEG(12), 0x0568, HCONV_CDECL, w, 3, NULL);
  CHECK(q != 0);
  uint8_t *p = LIN(q);
  p[3] = (uint8_t)n;
  for (unsigned k = 0; k < n; k++) {
    uint16_t qty = (uint16_t)(chance(1, 4) ? 0x3ff : rn(chance(1, 2) ? 6 : 0x3ff));
    uint16_t cls = chance(1, 2) ? 1 : 2;
    uint16_t item = (uint16_t)(chance(1, 2) ? rn(8) : rn(32));
    if (cls == 1 && chance(1, 3)) item = (uint16_t)(0xb + rn(2));
    uint16_t w0 = (uint16_t)(qty | ((item & 0x3f) << 10));
    uint16_t w1 = (uint16_t)(((item >> 6) & 1) | (cls << 1) | (chance(1, 4) ? (rn(16) << 4) : 0));
    memcpy(p + 4 + 4 * k, &w0, 2);
    memcpy(p + 6 + 4 * k, &w1, 2);
  }
  return q;
}
static uint32_t make_queue(unsigned n) { return make_queue_cap(n, 0); }

/* Part slots of a design record: category word at +0x3a, part byte +0x3c, count byte +0x3d, 4 bytes each,
 * the slot count at +0x7a. */
static void design_slots(uint8_t *r) {
  static const uint16_t cats[] = {0x0001, 0x0002, 0x0200, 0x0200, 0x4000, 0x0010};
  r[0x7a] = (uint8_t)rn(7);
  for (unsigned k = 0; k < 8; k++) {
    uint16_t cat = cats[rn(6)];
    memcpy(r + 0x3a + 4 * k, &cat, 2);
    r[0x3c + 4 * k] = (uint8_t)rn(chance(1, 2) ? 3 : 8);
    r[0x3d + 4 * k] = (uint8_t)(chance(1, 4) ? 0 : 1 + rn(4));
  }
}

/* The player tables: attrs of the current player (28:0x21ee), the starbase design array. */
static void world_player(uint16_t me, bool sb) {
  DS_W(0x14c) = me;
  uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * me));
  for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
  row[0x3e + 14] = (uint8_t)(chance(1, 3) ? 8 : chance(1, 3) ? 3 : rn(10));
  /* the starbase designs: 10 records of 0x8d bytes */
  uint8_t *d = LIN(g_designs);
  for (unsigned i = 0; i < 10 * 0x8d; i++) d[i] = (uint8_t)rnd();
  for (unsigned i = 0; i < 10; i++) {
    uint8_t *r = d + 0x8d * i;
    uint16_t hull = (uint16_t)(chance(1, 2) ? 0x20 + rn(8) : rn(32));
    memcpy(r, &hull, 2);
    snprintf((char *)r + 8, 0x20, "Starbase %u", i);
    design_slots(r);
    if (chance(1, 3)) r[0x7c] &= 0x7d;
    else if (chance(1, 3)) r[0x7c] |= 0x82;
  }
  ds_w32((uint16_t)(0x10c + 4 * me), g_designs);
  /* the default designs of DS:0x417a: 16 records of 0x8d bytes */
  uint8_t *dd = LINN(0x417a);
  for (unsigned i = 0; i < 16 * 0x8d; i++) dd[i] = (uint8_t)rnd();
  for (unsigned i = 0; i < 16; i++) {
    uint8_t *r = dd + 0x8d * i;
    uint16_t mass = (uint16_t)(chance(1, 3) ? rn(3) : rn(0x400));
    memcpy(r + 0x28, &mass, 2);
    snprintf((char *)r + 8, 0x20, "Design %u", i);
    design_slots(r);
    if (chance(2, 3)) r[0x7c] &= 0x7d;
  }
  /* the ship designs: 16 records */
  uint8_t *sh = LIN(g_ships);
  for (unsigned i = 0; i < 16 * 0x8d; i++) sh[i] = (uint8_t)rnd();
  for (unsigned i = 0; i < 16; i++) {
    uint8_t *r = sh + 0x8d * i;
    uint16_t hull = (uint16_t)(chance(1, 2) ? rn(3) : rn(20));
    memcpy(r, &hull, 2);
    snprintf((char *)r + 8, 0x20, "Ship %u", i);
    if (chance(1, 3)) r[0x7c] &= 0x7d;
    design_slots(r);
  }
  ds_w32((uint16_t)(0xbe + 4 * me), g_ships);
  (void)sb;
}

/* The selected planet copy (struct Planet at DS:0x4c08). */
static void world_planet(uint16_t me, bool with_queue, bool starbase) {
  uint8_t *pl = LINN(0x4c08);
  for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
  uint16_t id = (uint16_t)rn(40);
  memcpy(pl, &id, 2);
  memcpy(pl + 2, &me, 2);
  pl[5] = (uint8_t)((pl[5] & ~2) | (starbase ? 2 : 0));
  pl[0x2c] = (uint8_t)((pl[0x2c] & 0xf0) | rn(10));
  uint32_t q = with_queue ? make_queue(rn(8)) : 0;
  ds_w32(0x4c3c, q);
}

static void world_ui(void) {
  DS_W(0x0790) = (uint16_t)rnd();
  DS_W(0x4c22) = (uint16_t)rnd();
  DS_B(0x791) = (uint8_t)(chance(1, 2) ? 8 : rn(16));
  DS_B(0x53f1) = (uint8_t)rnd();
  DS_W(0x15fe) = (uint16_t)(chance(1, 2) ? 0 : 1);
  DS_W(0x4b74) = (uint16_t)(chance(1, 2) ? 1 : rn(4));
  DS_W(0x2806) = 0;
  DS_W(0x54f4) = 0;
  DS_W(0x6e) = 0x1234;
  DS_W(0x0cd6) = 0;
  DS_W(0x0cd8) = 0;
  DS_W(0x6686) = (uint16_t)rnd();
}

/* ---- 26:0x0810: commit or discard the edited production queue ---- */

static void t_0810(void) {
  for (unsigned it = 0; it < 200; it++) {
    g_rng = 0x08100001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)(chance(1, 3) ? 0 : rn(4));
    world_ui();
    world_player(me, false);
    world_planet(me, chance(3, 4), chance(1, 2));
    uint32_t e = chance(1, 5) ? 0 : make_queue(chance(1, 3) ? 0 : 1 + rn(5));
    ds_w32(0x0cd2, e);
    DIFF(26, 0x0810, FAR_CDECL, (uint16_t)(chance(1, 2) ? 0 : rn(3) ? 1 : 0xffff));
  }
}


/* ---- 26:0x0100: open the production queue of the selected planet ---- */

#define NAME_BUF_T 0x5866
#define ITEMS_BUF 0x6300 /* a 0x100-byte scratch area inside DGROUP (compared by DIFF, unlike the stack area behind the data) */
static struct ProdItem *pq_at_t(uint16_t base, unsigned i) { return LINN((uint16_t)(base + 4 * i)); }
static void stubs_for_open(void) {
  for (unsigned k = 0; k < 6; k++) {
    stub_push(S_9_4F76, (uint16_t)(chance(1, 3) ? 0 : chance(1, 5) ? 0xffff : 1 + rn(3)), 0);
    stub_push(S_9_4BEC, (uint16_t)(chance(1, 4) ? rn(20) : rn(3000)), 0);
    stub_push(S_9_49EA, (uint16_t)(chance(1, 4) ? rn(20) : rn(3000)), 0);
    stub_push(S_9_4D0E, (uint16_t)(chance(1, 4) ? rn(20) : rn(1500)), 0);
    stub_push(S_9_51BC, (uint16_t)(chance(1, 3) ? 0 : chance(1, 5) ? 0xffff : rn(1300)), 0);
  }
}
static void t_0100(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x01000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    world_planet(me, chance(3, 4), chance(2, 3));
    DS_B(0x4c34) = (uint8_t)((DS_B(0x4c34) & 0xf0) | rn(10));
    if (chance(1, 2)) ds_w32(0x4c20, (ds_r32(0x4c20) | 0x1f000));
    if (chance(1, 5)) ds_w32(0x4c20, (ds_r32(0x4c20) & ~0x1f000u));
    DS_W(0x1e + 0x4c00) = (uint16_t)rnd();
    DS_W(0x5a62) = ITEMS_BUF;
    memset(LINN(ITEMS_BUF), 0xa5, 0x100);
    stubs_for_open();
    /* the 0x4c1e..0x4c22 fields hold the current counts the available quantities are cut by */
    if (chance(1, 3)) DS_W(0x4c1e) = (uint16_t)(rn(60) << 4);
    if (chance(1, 3)) DS_W(0x4c1c) = (uint16_t)(rn(60) << 8);
    uint16_t arg = (it % 3 == 0) ? 0 : (it % 3 == 1 ? ITEMS_BUF : 0x6380);
    DIFF(26, 0x0100, FAR_CDECL, arg);
  }
}

/* ---- 26:0x0000: run the production queue dialog ---- */

static void t_0000(void) {
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    world_planet(me, chance(3, 4), chance(2, 3));
    DS_W(0x5a62) = ITEMS_BUF;
    stubs_for_open();
    DS_W(0x0cd6) = (uint16_t)(chance(1, 3) ? 0x1234 : 0);
    ds_w32(0x0cd2, 0);
    /* instance 0: DialogBox fails with -1, which the procedure takes as "accepted" */
    if (it % 5 == 4) {
      /* 34:0x0800 answers non-zero: the dialog was left through a throw to its catch point (frees the edited list,
       * ends the dialog window, shows the message 0x20/0x10) */
      ds_w32(0x0cd2, chance(1, 3) ? 0 : make_queue(rn(4)));
      stub_push(S_34_0800, 1, 0);
      DIFF(26, 0x0000, FAR_CDECL, (uint16_t)(it % 3 == 0 ? 1 : 0));
      continue;
    }
    DIFF(26, 0x0000, FAR_CDECL, (uint16_t)(it % 3 == 0 ? 1 : (it % 3 == 1 ? 0 : 0xffff)));
  }
}

/* ---- 26:0x2058: the name of a production item ---- */

static void random_item(struct ProdItem *e, uint16_t cls) {
  uint16_t item = (uint16_t)(chance(1, 3) ? rn(0x20) : chance(1, 2) ? rn(0x80) : (0x10 + rn(12)));
  if (cls == 2) item = (uint16_t)(item % 0x1a); /* the starbase design array holds 10 designs */
  else item = (uint16_t)(item % 0x1c);            /* the items that have a name */
  uint16_t qty = (uint16_t)(chance(1, 4) ? 0 : chance(1, 3) ? 0x3ff : rn(0x3ff));
  e->w0 = (uint16_t)(qty | ((item & 0x3f) << 10));
  e->w1 = (uint16_t)(((item >> 6) & 1) | (cls << 1) | (chance(1, 5) ? (rn(0x1000) << 4) : 0));
}
static void t_2058(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x20580001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_player(me, true);
    world_planet(me, false, chance(1, 2));
    struct ProdItem *e = LINN(0x6400);
    random_item(e, chance(1, 2) ? 2 : (chance(1, 2) ? 1 : rn(8)));
    if (it % 5 == 0) random_item(e, 2);
    memset(LINN(NAME_BUF_T), 0x55, 0x40);
    DIFF(26, 0x2058, FAR_CDECL, 0x6400, MEM_SEL_DGROUP);
  }
}

/* ---- 26:0x1f42: fill the list box of available items ---- */

static void t_1f42(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t lb = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 5, 200, 100, dlg, 0x417);
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x1f420001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_player(me, true);
    world_planet(me, false, chance(1, 2));
    unsigned n = it % 12;
    memset(LINN(NAME_BUF_T), 0, 0x40); /* a short name: the original copies it over its own counters when it is long */
    memcpy(LINN(NAME_BUF_T), "Name", 5);
    DS_W(0x5a62) = ITEMS_BUF;
    DS_W(0x6686) = (uint16_t)(it % 40 == 39 ? 0xffff : n);
    for (unsigned k = 0; k < n; k++) random_item(pq_at_t(ITEMS_BUF, k), chance(1, 2) ? 2 : 1);
    if (DIFF(26, 0x1f42, FAR_CDECL, (it % 25 == 24) ? 0 : lb) != DT_PASS) fprintf(stderr, "  1f42 iteration %u: n=%u count=%x\n", it, n, DS_W(0x6686));
  }
  DestroyWindow(dlg);
}

/* ---- 26:0x33e4: enable the buttons of the ZIP production dialog ---- */

static void t_33e4(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  mkwin(WS_CHILD | WS_VISIBLE, 5, 5, 80, 20, dlg, 0x817);
  mkwin(WS_CHILD | WS_VISIBLE, 5, 30, 80, 20, dlg, 0x41b);
  for (unsigned p = 0; p < 4; p++) {
    for (unsigned flag = 0; flag < 2; flag++) {
      DS_B((uint16_t)(0x2583 + 0x28 * p)) = (uint8_t)(flag ? 1 + p : 0);
      DIFF(26, 0x33e4, FAR_CDECL, dlg, p);
    }
  }
  DS_B(0x2583 + 0x28 * 3) = 7;
  DIFF(26, 0x33e4, FAR_CDECL, dlg, 0xffff);
  DIFF(26, 0x33e4, FAR_CDECL, 0, 1);
  DestroyWindow(dlg);
}

/* ---- 26:0x1a40: fill the controls of the production queue dialog ---- */

static void t_1a40(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 5, 200, 100, dlg, 0x416);
  mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 110, 200, 100, dlg, 0x417);
  mkwin(WS_CHILD | WS_VISIBLE, 5, 215, 80, 20, dlg, 0x8b);
  memcpy(LINN(0x6480), "Planet Test", 12);
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x1a400001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    stub_def(S_7_1CCE, 0x6480, 0);
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    world_planet(me, false, chance(1, 2));
    DS_W(0x4c22) = (uint16_t)rnd();
    unsigned n = it % 9;
    DS_W(0x5a62) = ITEMS_BUF;
    DS_W(0x6686) = (uint16_t)n;
    for (unsigned k = 0; k < n; k++) random_item(pq_at_t(ITEMS_BUF, k), chance(1, 2) ? 2 : 1);
    ds_w32(0x0cd2, make_queue(it % 7 == 6 ? 0 : 1 + rn(7)));
    DIFF(26, 0x1a40, FAR_CDECL, dlg);
  }
  DestroyWindow(dlg);
}

/* ---- 26:0x3428: show a ZIP production preset in the item list ---- */

static void t_3428(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 5, 200, 100, dlg, 0x417);
  DS_W(0x0d08) = 7;
  DS_W(0x267a) = 14;
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x34280001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned p = it % 4;
    uint16_t base = (uint16_t)(0x2576 + 0x28 * p);
    for (unsigned i = 0; i < 0x28; i++) DS_B((uint16_t)(base + i)) = (uint8_t)rnd();
    DS_B((uint16_t)(base + 0xd)) = (uint8_t)(chance(1, 5) ? 0 : 1);
    DS_B((uint16_t)(base + 0xf)) = (uint8_t)(chance(1, 6) ? 0 : 1 + rn(12));
    for (unsigned k = 0; k < 12; k++) {
      uint16_t w = (uint16_t)(chance(1, 3) ? (1 + 3 * rn(2)) : rn(0x20));
      uint16_t qty = (uint16_t)(chance(1, 3) ? 1 : rn(5));
      DS_W((uint16_t)(base + 0x10 + 2 * k)) = (uint16_t)(w | (chance(1, 4) ? 0x40 : 0) | ((qty * 0x40) & 0xffc0 & (chance(1, 2) ? 0xffc0 : 0)));
    }
    DIFF(26, 0x3428, FAR_CDECL, dlg, base);
  }
  DestroyWindow(dlg);
}

/* ---- 26:0x29da: simulate the production queue of a planet ---- */

static void t_29da(void) {
  uint32_t pl_blk = gblock(0x100);
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x29da0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    DS_W(0x553a) = 0;
    uint8_t *pl = LIN(pl_blk);
    for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
    memcpy(pl + 2, &me, 2);
    pl[5] &= 0xfb; /* the planet has output */
    for (unsigned i = 0; i < 3; i++) pl[0xc + i] = (uint8_t)(1 + rn(99));
    for (unsigned i = 0; i < 3; i++) pl[0x9 + i] = (uint8_t)(10 + rn(100));
    if (chance(1, 2)) pl[0x1a] |= 0x80; else pl[0x1a] &= 0x7f;
    unsigned n = 1 + it % 6;
    uint32_t q = make_queue(n);
    memcpy(pl + 0x34, &q, 4);
    uint32_t other = chance(1, 2) ? 0 : make_queue(1 + rn(5));
    unsigned qn = other ? ((uint8_t *)LIN(other))[3] : n;
    int16_t target = (int16_t)(chance(1, 5) ? -1 - (int)rn(2) : (int)rn(qn + 1) - (chance(1, 8) ? 1 : 0));
    DS_W(0x6500) = 0x5555;
    DS_W(0x6502) = 0x5555;
    for (unsigned k = 0; k < 24; k++) {
      stub_push(S_9_4DE8, (uint16_t)(chance(1, 5) ? 0 : rn(600)), 0);
      stub_push(S_23_0756, (uint16_t)(chance(1, 3) ? 0 : chance(1, 5) ? 0xffff : rn(40)), (uint16_t)(chance(1, 3) ? rn(3) : chance(1, 3) ? rn(7) : 3 + rn(2)));
    }
    DIFF(26, 0x29da, FAR_CDECL, LO(pl_blk), HI(pl_blk), LO(other), HI(other), (uint16_t)target, 0x6500, 0x6502);
  }
}

/* ---- 26:0x21e4: the cost of a production item ---- */

static void t_21e4(void) {
  uint32_t pl_blk = gblock(0x100), it_blk = gblock(0x40);
  for (unsigned it = 0; it < 1200; it++) {
    g_rng = 0x21e40001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    uint8_t *pl = LIN(pl_blk);
    for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
    memcpy(pl + 2, &me, 2);
    pl[5] = (uint8_t)((pl[5] & ~2) | (chance(1, 2) ? 2 : 0));
    pl[0x2c] = (uint8_t)((pl[0x2c] & 0xf0) | rn(10));
    struct ProdItem *e = LIN(it_blk);
    unsigned kind = it % 4;
    if (kind == 0 || kind == 1) {
      random_item(e, 2);
      if (kind == 1) {
        /* same hull as the planet's starbase design, so that the slot comparison runs */
        uint8_t *own = (uint8_t *)LIN(g_designs);
        uint16_t hullw;
        memcpy(&hullw, own + 0x8d * (pl[0x2c] & 0xf), 2);
        memcpy(own + 0x8d * (e->w1 & 1 ? 0 : (prod_item_index(e) >= 0x10 ? prod_item_index(e) - 0x10 : 0)), &hullw, 2);
      }
    } else {
      random_item(e, 1);
      e->w0 = (uint16_t)((e->w0 & 0x3ff) | (rn(0x1d) << 10));
      e->w1 = (uint16_t)((e->w1 & ~0xf) | (1 << 1));
    }
    DS_W(0x6500) = 0x5555;
    for (unsigned k = 0; k < 24; k++) {
      stub_push(S_7_3B60, (uint16_t)rn(300), (uint16_t)(chance(1, 3) ? rn(300) : 0));
      stub_push(S_10_7A92, (uint16_t)rn(120), (uint16_t)(chance(1, 3) ? rn(120) : 0));
    }
    if (getenv("TRACE")) fprintf(stderr, "21e4 it %u kind %u w0 %04x w1 %04x\n", it, kind, e->w0, e->w1);
    DIFF(26, 0x21e4, FAR_CDECL, LO(pl_blk), HI(pl_blk), LO(it_blk), HI(it_blk), 0x6500, me, (uint16_t)(it % 3 == 0 ? 1 : 0));
  }
}

/* ---- 26:0x1b5a: paint the cost panel of the production dialog ---- */

static uint16_t g_font;
/* The state 26:0x1b5a paints from: fonts, colours, the available items, the edited list and the list selections. */
static void paint_state(uint16_t l416, uint16_t l417) {
  uint16_t me = (uint16_t)rn(4);
  world_ui();
  world_player(me, true);
  world_planet(me, false, chance(1, 2));
  DS_W(0x292e) = g_font;
  DS_W(0x2930) = g_font;
  ds_w32(0x253a, 0x00C0C0C0);
  DS_W(0x267a) = 14;
  DS_W(0x14c) = me;
  DS_W(0x4b6e) = (uint16_t)rn(20);
  DS_W(0x288a) = (uint16_t)rnd();
  DS_W(0x5a5c) = (uint16_t)GetStockObject(chance(1, 2) ? 0 : 4);
  ds_w32(0x4ac0, 0x00123456);
  unsigned n = 1 + rn(9);
  DS_W(0x5a62) = ITEMS_BUF;
  DS_W(0x6686) = (uint16_t)n;
  for (unsigned k = 0; k < n; k++) random_item(pq_at_t(ITEMS_BUF, k), chance(1, 2) ? 2 : 1);
  unsigned qn = 1 + rn(6);
  ds_w32(0x0cd2, make_queue(qn));
  SendMessage(l416, 0x407, (uint16_t)(chance(1, 6) ? 0xffff : rn(n + 1)), 0);
  SendMessage(l417, 0x407, (uint16_t)(chance(1, 6) ? 0xffff : rn(qn + 1)), 0);
  for (unsigned k = 0; k < 24; k++) {
    stub_push(S_7_3B60, (uint16_t)rn(300), (uint16_t)(chance(1, 3) ? rn(300) : 0));
    stub_push(S_10_7A92, (uint16_t)rn(120), (uint16_t)(chance(1, 3) ? rn(120) : 0));
  }
}
static void t_1b5a(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0);
  uint16_t l416 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 5, 200, 100, dlg, 0x416);
  uint16_t l417 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 5, 110, 200, 100, dlg, 0x417);
  for (unsigned i = 0; i < 12; i++) {
    SendMessage(l416, 0x401, 0, fstr("item"));
    SendMessage(l417, 0x401, 0, fstr("queued"));
  }
  for (unsigned it = 0; it < 160; it++) {
    g_rng = 0x1b5a0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    paint_state(l416, l417);
    uint16_t dc = (it % 2) ? GetDC(dlg) : 0;
    DIFF(26, 0x1b5a, FAR_CDECL, dlg, dc);
    if (dc) ReleaseDC(dlg, dc);
  }
  DestroyWindow(dlg);
}

static void zip_presets(void) {
  for (unsigned i = 0; i < 4; i++) {
    uint16_t base = (uint16_t)(0x2576 + 0x28 * i);
    memset(LINN(base), 0, 0x28);
    char nm[16];
    snprintf(nm, sizeof nm, "Pre&set %u", i);
    if (chance(3, 4)) memcpy(LINN(base), nm, strlen(nm) + 1);
    DS_B((uint16_t)(base + 0xd)) = (uint8_t)(chance(3, 4) ? 1 : 0);
    DS_B((uint16_t)(base + 0xe)) = (uint8_t)rn(2);
    DS_B((uint16_t)(base + 0xf)) = (uint8_t)rn(13);
    for (unsigned k = 0; k < 12; k++) DS_W((uint16_t)(base + 0x10 + 2 * k)) = (uint16_t)(rn(7) | (rn(20) << 6));
  }
}

/* ---- 26:0x08e2: the dialog procedure of the production queue dialog ---- */

static void t_08e2(void) {
  /* a dialog with the controls the procedure lays out */
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 640, 480, 0, 0);
  static const uint16_t btn_ids[] = {0x42e, 0x42f, 1, 2, 0x418, 0x419, 0x439, 0x43a, 0x42d, 0x76};
  for (unsigned i = 0; i < 10; i++) mkwin(WS_CHILD | WS_VISIBLE, 5, 5 + 20 * (int)i, 60, 18, dlg, btn_ids[i]);
  uint16_t chk = mkwin(WS_CHILD | WS_VISIBLE, 5, 300, 80, 20, dlg, 0x8b);
  uint16_t l416 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 100, 5, 200, 100, dlg, 0x416);
  uint16_t l417 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 100, 110, 200, 100, dlg, 0x417);
  uint32_t ditem = gblock(0x40);
  for (unsigned it = 0; it < 260; it++) {
    g_rng = 0x08e20001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    paint_state(l416, l417);
    zip_presets();
    DS_W(0x54f4) = (uint16_t)(chance(1, 3) ? MEM_SEL_DGROUP : 0); /* a real instance: the ZIP production dialog runs (nothing answers it) */
    DS_W(0x4ac8) = 0;
    DS_W(0x0010) = GetStockObject(0);
    if (chance(1, 3)) DS_B(0x791) = (uint8_t)(0x40 | (chance(1, 2) ? 8 : 0)); /* the large dialog */
    DS_W(0x0cdc) = (uint16_t)(chance(1, 2) ? 0xffff : 0);
    {
      unsigned used = 0;
      for (unsigned i = 0; i < 4; i++) used += DS_B((uint16_t)(0x2583 + 0x28 * i)) != 0;
      for (unsigned k = 0; k < 12; k++) stub_push(S_24_0E24, (uint16_t)(chance(1, 3) ? used + 1 : chance(1, 4) ? 0xffff : rn(used + 1)), 0);
    }
    DS_W(0x4c78) = (uint16_t)rn(3) * 100;
    DS_W(0x4c7a) = 0;
    DS_W(0x4c7c) = 100 + (uint16_t)rn(1000);
    DS_W(0x4c7e) = 1000;
    uint16_t hdc = GetDC(dlg);
    uint16_t pt = (uint16_t)rn(500), pu = (uint16_t)rn(100);
    if (getenv("TRACE")) fprintf(stderr, "08e2 it %u case %u\n", it, it % 10);
    switch (it % 10) {
    case 0: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x000f, 0, 0, 0); break;
    case 1: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0014, hdc, 0, 0); break;
    case 2: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0019, hdc, chance(1, 2) ? 6 : 3, chance(1, 2) ? chk : l416); break;
    case 3: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0020, 0, 0, 0); break;
    case 4: {
      struct { uint16_t a, b, id, action, state; } *d = LIN(ditem);
      d->id = (uint16_t)(chance(1, 3) ? 0xffff : rn(5));
      d->action = (uint16_t)rn(6);
      DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x002b, 0, HI(ditem), LO(ditem));
      DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x002c, 0, HI(ditem), LO(ditem));
      break;
    }
    case 5: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0110, 0, 0, 0); break;
    case 6: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0201, 0, pu, pt); break;
    case 7:
    case 8: DIFF(26, 0x08e2, FAR_PASCAL, dlg, 0x0204, 0, pu, pt); break;
    default: DIFF(26, 0x08e2, FAR_PASCAL, dlg, chance(1, 2) ? 0x0111 : 0x0000, 0x7777, 0, 0); break;
    }
    ReleaseDC(dlg, hdc);
  }
  DestroyWindow(dlg);
}

/* ---- 26:0x2d26 ZIPPRODDLG: the ZIP production dialog ---- */

static void t_2d26(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0);
  uint16_t rb[4];
  for (unsigned i = 0; i < 4; i++) rb[i] = mkwin(WS_CHILD | WS_VISIBLE, 5, 5 + 25 * (int)i, 120, 20, dlg, (uint16_t)(0x431 + i));
  mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 150, 5, 200, 100, dlg, 0x417);
  mkwin(WS_CHILD | WS_VISIBLE, 5, 130, 60, 20, dlg, 0x817);
  mkwin(WS_CHILD | WS_VISIBLE, 70, 130, 60, 20, dlg, 0x41b);
  uint16_t hdc = GetDC(dlg);
  for (unsigned it = 0; it < 360; it++) {
    g_rng = 0x2d260001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)rn(4);
    world_ui();
    world_player(me, true);
    world_planet(me, false, false);
    zip_presets();
    DS_W(0x51b2) = (uint16_t)rn(4);
    DS_W(0x2930) = g_font;
    ds_w32(0x253a, 0x00C0C0C0);
    DS_W(0x267a) = 14;
    DS_W(0x0d08) = 5;
    DS_W(0x0010) = GetStockObject(0);
    DS_W(0x2806) = 0;
    DS_W(0x54f4) = (uint16_t)(chance(1, 2) ? 0 : MEM_SEL_DGROUP);
    DS_B(0x794) = (uint8_t)rnd();
    ds_w32(0x0cd2, make_queue(rn(8)));
    memcpy(LINN(0x5866), "newname", 8);
    switch (it % 12) {
    case 0: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x000f, 0, 0, 0); break;
    case 1: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0014, hdc, 0, 0); break;
    case 2: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0019, hdc, 0, chance(1, 2) ? rb[rn(4)] : 0x1234); break;
    case 3: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0110, 0, 0, 0); break;
    case 4: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, (uint16_t)(0x431 + rn(4)), 0, 0); break;
    case 5: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, (uint16_t)(1 + rn(2)), 0, 0); break;
    case 6:
    case 7: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, 0x816, 0, 0); break;
    case 8: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, 0x41b, 0, 0); break;
    case 9: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, 0x817, 0, 0); break;
    case 10: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0111, chance(1, 2) ? 0x76 : 0x1234, chance(1, 3) ? 5 : 0, 0); break;
    default: DIFF(26, 0x2d26, FAR_PASCAL, dlg, 0x0000, 0, 0, 0); break;
    }
  }
  ReleaseDC(dlg, hdc);
  DestroyWindow(dlg);
}

/* ---- 26:0x0e74: the commands of the production queue dialog ---- */

static void t_0e74(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 640, 480, 0, 0);
  uint16_t l416 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 100, 5, 200, 100, dlg, 0x416);
  uint16_t l417 = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE, 100, 110, 200, 100, dlg, 0x417);
  uint16_t chk = mkwin(WS_CHILD | WS_VISIBLE, 5, 300, 80, 20, dlg, 0x8b);
  for (unsigned i = 0; i < 12; i++) {
    SendMessage(l416, 0x401, 0, fstr("item"));
    SendMessage(l417, 0x401, 0, fstr("queued"));
  }
  static const uint16_t ids[] = {1, 2, 0x76, 0x8b, 0x416, 0x417, 0x418, 0x419, 0x42d, 0x816, 0x42e, 0x42f, 0x439, 0x43a, 0x1234, 0, 0x418, 0x419, 0x439, 0x43a, 0x418, 0x419};
  for (unsigned it = 0; it < 1400; it++) {
    g_rng = 0x0e740001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    stub_def(S_7_1CCE, 0x6480, 0);
    memcpy(LINN(0x6480), "Planet Test", 12);
    paint_state(l416, l417);
    zip_presets();
    DS_W(0x54f4) = 0;
    DS_W(0x2806) = 0;
    stubs_for_open();
    for (unsigned k = 0; k < 8; k++) {
      stub_push(S_9_2ED8, (uint16_t)rnd(), 0);
      stub_push(S_9_3082, (uint16_t)rn(40), 0);
    }
    /* a queue with room for more entries half of the time, so that the resize path (capacity == count) runs too */
    unsigned qn = rn(chance(1, 8) ? 40 : 7);
    ds_w32(0x0cd2, make_queue_cap(qn, chance(1, 2) ? 3 : 0));
    unsigned n = DS_W(0x6686);
    SendMessage(l416, 0x407, (uint16_t)(chance(1, 8) ? 0xffff : rn(n + 1)), 0);
    SendMessage(l417, 0x407, (uint16_t)(chance(1, 8) ? 0xffff : rn(qn + 2)), 0);
    bool ctrl = chance(1, 3), shift = chance(1, 3);
    rt_input_key(VK_CONTROL, 0x1d, ctrl, 0);
    rt_input_key(VK_SHIFT, 0x2a, shift, 0);
    uint16_t id = ids[it % (sizeof ids / 2)];
    uint16_t ctl = (id == 0x816) ? (uint16_t)rn(4) : (id == 0x8b ? chk : 0);
    uint16_t code = (id == 0x416 || id == 0x417) ? (uint16_t)(it % 3) : 0;
    if (id == 0x8b) SendMessage(chk, 0x401, (uint16_t)rn(2), 0);
    if (getenv("TRACE")) fprintf(stderr, "0e74 it %u id %x\n", it, id);
    DIFF(26, 0x0e74, FAR_CDECL, dlg, id, ctl, code);
    rt_input_key(VK_CONTROL, 0x1d, 0, 0);
    rt_input_key(VK_SHIFT, 0x2a, 0, 0);
  }
  DestroyWindow(dlg);
}

int main(void) {
  g_only = getenv("SEG26_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  register_test_class();
  stubs_init();
  g_font = GetStockObject(16);
  g_designs = gblock(0x800);
  g_ships = gblock(0x900);
  if (want("0810")) t_0810();
  if (want("0100")) t_0100();
  if (want("0000")) t_0000();
  if (want("2058")) t_2058();
  if (want("1f42")) t_1f42();
  if (want("33e4")) t_33e4();
  if (want("1a40")) t_1a40();
  if (want("3428")) t_3428();
  if (want("29da")) t_29da();
  if (want("21e4")) t_21e4();
  if (want("1b5a")) t_1b5a();
  if (want("08e2")) t_08e2();
  if (want("2d26")) t_2d26();
  if (want("0e74")) t_0e74();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
