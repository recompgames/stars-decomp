/* Diff tests for decompiled segment 5 (src/seg05.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 5 is the information window of the selected thing (planet, fleet or map object) with its
 * panel, header text and order button:
 *   5:0x0000 MINEWNDPROC   its window procedure (create, paint, cursor, button command, clicks)
 *   5:0x0324               measure the labels and invalidate the list and label areas
 *   5:0x0474               count the map objects of the current player and find one by its first word
 *   5:0x04e8               paint the panel of the selected planet / fleet / map object
 *   5:0x2840               hit test: which zone of the panel is under a point
 *   5:0x2ab4               click handler: fills the descriptor DS:0x0bc0 that 24:0x091e shows (zones 1 to 14)
 *   5:0x329a               rebuild the header text and the order button
 *   5:0x344a               draw the glyph of a button (BitBlt)
 *   5:0x34fc               draw the small arrow marker (PatBlt)
 *   5:0x36a8               is more than one thing selected at the place
 *   5:0x3774               pop-up menu of the things at the selected place
 *   5:0x3a72               mineral output of a planet and of the fleets mining it
 * Every scenario builds its state by writing DGROUP and a few far blocks directly (or runs ORIGINAL
 * code for set-up); DIFF then snapshots it and compares the original with the C version (return
 * value, all memory outside the callee's own frame including the frame buffer, the ordered Win16
 * API call log, which here also holds the test doubles' calls). The runtime state (windows, GDI
 * objects) lives in the arena, so DIFF restores it between the two runs.
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of
 * the callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See the notes of each function in
 * docs/functions.csv for what each double hashes and what it does not.
 *
 * Environment (for focused runs): SEG05_ONLY=tag[,tag] runs the scenarios whose tag (hex offset) is
 * named, SEG05_ITERS=n sets the number of random worlds of the random scenarios, SEG05_LIMIT=n stops
 * the 2840 scenario after n DIFFs. */
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
#define SEL5 MEM_SEL_SEG(5)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* ---- test support: far memory, windows ---- */

static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}
__attribute__((unused)) static uint32_t fstr(const char *s) {
  uint32_t p = gblock((uint32_t)strlen(s) + 1);
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}

/* A scratch area in DGROUP that no function of segment 5 touches (the bottom of the stack area,
 * far below the stack top 0xa688). */
#define SCR 0x6700
/* Output slots for the functions that write through near pointers: the stack scratch area (from the
 * stack limit 0x6688 up) is not compared by DIFF, so an output there would be blind. DS:0x5e00 lies in
 * the data (the record of an unused player); both runs write it and the diff compares it. */
#define OUT 0x5e00

static uint32_t g_rng = 0x05a4c3e1u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}


/* ---- test doubles (docs/CONVENTIONS.md rule j2) ----
 * The first 5 bytes of the callee in the code segment image become JMP FAR to an import thunk of the
 * module "STUBS" (ordinal = stub id, CDECL: the caller pops the arguments), registered with
 * harness_add_import. The same image serves the ORIGINAL run and the C version's CALL_ORIG, so each
 * stubbed call is an entry of the harness API log (module, ordinal, the argument words as pushed, the
 * return value), compared in order with the real API calls and the other doubles. A pointer argument
 * is logged as the pointer only: the handler therefore also hashes, at every call, the memory its
 * arguments point to (see each handler) into a log in the stub block, which DIFF compares as arena
 * memory. The functions under test are never doubled. */
#define STUB_BLKSZ 0x4000
#define STUB_LOG0 0x100
static uint32_t g_stub_blk;
static void stub_hash_to_block(uint32_t h) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
}
static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
/* The return value (ax, dx) of stub id is kept at blk + 0x10 + 4 * id. */
static void stub_ret(unsigned id, uint16_t ax, uint16_t dx) {
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x10 + 4 * id))) = ax;
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x12 + 4 * id))) = dx;
}
static void stub_set_ret(HCall *c) {
  const uint8_t *blk = LIN(g_stub_blk);
  c->ret = (uint32_t)blk[0x12 + 4 * c->ordinal] | ((uint32_t)blk[0x13 + 4 * c->ordinal] << 8);
  c->ret = (c->ret << 16) | (uint32_t)blk[0x10 + 4 * c->ordinal] | ((uint32_t)blk[0x11 + 4 * c->ordinal] << 8);
}
static void stub_handler(HCall *c) { stub_set_ret(c); }
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  *(uint16_t *)LIN(g_stub_blk) = STUB_LOG0;
}
static void stub_install_h(uint16_t seg, uint16_t off, unsigned id, unsigned nargs, HApiFn fn) {
  char name[32];
  snprintf(name, sizeof name, "STUB_%u_%04x", seg, off);
  uint32_t th = harness_add_import("STUBS", (uint16_t)id, name, HK_VARARGS, (uint16_t)(2 * nargs), HR_RET32, fn);
  CHECK(th != 0);
  uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
  memcpy(LIN(MK_FP(SEG_SEL(seg), off)), c, sizeof c);
}
/* The doubles used by this file. */
enum { STUB_24_0E24, STUB_11_62F2, STUB_11_4EAA, STUB_9_2A74, STUB_24_091E, STUB_N };
static bool g_zap, g_zap2;
static uint16_t g_pick = 0xffff; /* what the pop-up 24:0x0e24 chooses: an index modulo the item count, 0xffff = cancel */

/* 24:0x0e24 (the pop-up menu of the targets), 8 argument words: hwnd, point, count, near pointer to the
 * count items of 4 bytes (SS), 0, selected index, 1. The handler hashes the items; the choice is
 * g_pick modulo the count (-1 for 0xffff or no items). */
static void h_e24(HCall *c) {
  uint16_t cnt = hcall_word(c, 3), arr = hcall_word(c, 4), strs = hcall_word(c, 5);
  uint16_t seg = FP_SEG(c->args);
  uint32_t h = 2166136261u;
  if (cnt > 100) cnt = 100;
  if (arr != 0) {
    h = fnv(h, LIN(MK_FP(seg, arr)), 4u * cnt);
  } else { /* a list of strings: the near pointers and the strings (up to 0x20 bytes, NUL included) */
    h = fnv(h, LIN(MK_FP(seg, strs)), 2u * cnt);
    for (uint16_t i = 0; i < cnt; i++) {
      uint16_t sp;
      memcpy(&sp, LIN(MK_FP(seg, (uint16_t)(strs + 2 * i))), 2);
      const uint8_t *t = LIN(MK_FP(seg, sp));
      size_t n = 0;
      while (n < 0x20 && t[n] != 0) n++;
      h = fnv(h, t, n + 1);
    }
  }
  stub_hash_to_block(h);
  if (g_zap) { /* the world changes while the menu is open: every map object gets another id */
    uint32_t mo;
    memcpy(&mo, LINN(0x188e), 4);
    for (uint16_t i = 0; i < *(uint16_t *)LINN(0x1892); i++) *(uint16_t *)LIN(MK_FP(HI(mo), (uint16_t)(LO(mo) + 0x12 * i))) = 0xBEEF;
  }
  if (g_zap2) *(uint16_t *)LINN(0x553a) = 0; /* the fleets are gone while the menu is open */
  c->ret = 0xffffu;
  if (g_pick != 0xffff && cnt != 0 && arr == 0) c->ret = g_pick % cnt;
  else if (g_pick != 0xffff && cnt != 0) { /* the menu offers no separator: skip them */
    for (uint16_t k = 0; k < cnt; k++) {
      uint16_t at = (uint16_t)((g_pick + k) % cnt);
      uint32_t it;
      memcpy(&it, LIN(MK_FP(seg, (uint16_t)(arr + 4 * at))), 4);
      if (it != 0xffffffffu) {
        c->ret = at;
        break;
      }
    }
  }
}
/* 11:0x62f2 (re-select from a saved copy of the selection), 2 argument words: near pointer to 16 bytes
 * (SS), 2. The handler hashes the 16 bytes. */
static void h_62f2(HCall *c) {
  uint16_t pp = hcall_word(c, 0);
  stub_hash_to_block(fnv(2166136261u, LIN(MK_FP(FP_SEG(c->args), pp)), 16));
  stub_set_ret(c);
}
/* 24:0x091e (starts the drag / info window for the descriptor at DS:0x0bc0..0x0bd5), 3 argument words:
 * window, point. The handler hashes the 22 bytes of the descriptor (the memory its far pointers point
 * to is not hashed). */
static void h_091e(HCall *c) {
  stub_hash_to_block(fnv(2166136261u, LINN(0x0bc0), 22));
  stub_set_ret(c);
}
static void doubles_init(void) {
  stubs_init();
  stub_install_h(24, 0x0e24, STUB_24_0E24, 8, h_e24);
  stub_install_h(11, 0x62f2, STUB_11_62F2, 2, h_62f2);
  stub_install_h(11, 0x4eaa, STUB_11_4EAA, 2, stub_handler);
  stub_install_h(9, 0x2a74, STUB_9_2A74, 2, stub_handler);
  stub_install_h(24, 0x091e, STUB_24_091E, 3, h_091e);
}

/* ---- the map objects (DS:0x188e far array, DS:0x1892 count) ---- */

#define MO_SIZE 0x12
static uint32_t g_mo;
static void mo_setup(uint16_t count, uint16_t off) {
  if (!g_mo) g_mo = gblock(0x10000 - 0x20);
  ds_w32(0x188e, MK_FP(HI(g_mo), off));
  DS_W(0x1892) = count;
}
static uint8_t *mo_at(unsigned i) { return (uint8_t *)LIN(g_mo) + i * MO_SIZE; }

/* ---- 5:0x0474 count map objects of the current kind and find one ---- */

static void t_0474(void) {
  mo_setup(0, 0);
  memset(LIN(g_mo), 0, 0x400);
  DS_W(0x014c) = 3;
  DIFF(5, 0x0474, FAR_CDECL, 0x1111, OUT, OUT + 2); /* no objects */
  mo_setup(1, 0);
  DIFF(5, 0x0474, FAR_CDECL, 0x1111, OUT, OUT + 2);
  /* objects: word 0 is the id, byte 1 holds the kind (bits 5-7) and the player (bits 1-4) */
  for (unsigned round = 0; round < 6; round++) {
    unsigned n = 1 + rnd() % 9;
    mo_setup((uint16_t)n, 0);
    for (unsigned i = 0; i < n; i++) {
      uint8_t *o = mo_at(i);
      memset(o, 0, MO_SIZE);
      uint16_t id = (uint16_t)(0x100 + (rnd() % 4));
      memcpy(o, &id, 2);
      o[1] = (uint8_t)(((rnd() % 5) << 1) | ((rnd() % 3 == 0) ? 0x20 : 0)); /* players 0..4, some with a kind */
    }
    DS_W(0x014c) = (uint16_t)(round % 4);
    for (uint16_t key = 0x0ff; key <= 0x104; key++) DIFF(5, 0x0474, FAR_CDECL, key, OUT, OUT + 2);
  }
  /* every entry of the current player counts; the match is the second of three */
  mo_setup(4, 0);
  for (unsigned i = 0; i < 4; i++) {
    uint8_t *o = mo_at(i);
    memset(o, 0, MO_SIZE);
    o[0] = (uint8_t)(0x70 + i);
    o[1] = (uint8_t)(i == 2 ? 0x20 : 0x06); /* player 3; entry 2 has a kind */
  }
  DS_W(0x014c) = 3;
  DIFF(5, 0x0474, FAR_CDECL, 0x0671, OUT, OUT + 2); /* word 0 holds the id and the player */
  DIFF(5, 0x0474, FAR_CDECL, 0x2072, OUT, OUT + 2);
  DIFF(5, 0x0474, FAR_CDECL, 0x0673, OUT, OUT + 2);
  DIFF(5, 0x0474, FAR_CDECL, 0x0670, OUT, OUT + 2);
  /* the match is the last one counted / the first / none */
  mo_setup(3, 0);
  for (unsigned i = 0; i < 3; i++) {
    uint8_t *o = mo_at(i);
    memset(o, 0, MO_SIZE);
    o[0] = (uint8_t)(0x10 + i);
    o[1] = 2; /* player 1 */
  }
  DS_W(0x014c) = 1;
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  DIFF(5, 0x0474, FAR_CDECL, 0x11, OUT, OUT + 2);
  DIFF(5, 0x0474, FAR_CDECL, 0x12, OUT, OUT + 2);
  DIFF(5, 0x0474, FAR_CDECL, 0x13, OUT, OUT + 2);
  DS_W(0x014c) = 2;
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  /* the count is multiplied by 0x12 in 16 bits: 0x1c73 * 0x12 = 0x20016, the end offset is 0x0016 */
  mo_setup(0x1c73, 0);
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  mo_setup(0x1c72, 0); /* 0x1c72 * 0x12 = 0x1ffe8: wraps to 0xffe8 */
  DS_W(0x014c) = 1;
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  /* base near the end of the 64 KB offset space: end <= base, no iteration */
  mo_setup(2, 0xffe0);
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  mo_setup(0x8000, 0); /* 0x8000 * 0x12 = 0x90000: end 0 <= base, no iteration */
  DIFF(5, 0x0474, FAR_CDECL, 0x10, OUT, OUT + 2);
  mo_setup(0, 0);
}

/* ---- the fleets (DS:0x00ba far array of far pointers, DS:0x553a count) ---- */

#define FL_SIZE 0x80
static uint32_t g_fl_arr, g_fl_blk;
/* Fleet i gets the dword d[i] at +8 (a null pointer when null_at == i). */
static void fl_setup(unsigned n, const uint32_t *d, int null_at) {
  if (!g_fl_arr) {
    g_fl_arr = gblock(0x400);
    g_fl_blk = gblock(FL_SIZE * 128);
  }
  for (unsigned i = 0; i < n; i++) {
    uint32_t fp = (int)i == null_at ? 0 : MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE));
    memcpy((uint8_t *)LIN(g_fl_arr) + 4 * i, &fp, 4);
    memset((uint8_t *)LIN(g_fl_blk) + i * FL_SIZE, 0, FL_SIZE);
    memcpy((uint8_t *)LIN(g_fl_blk) + i * FL_SIZE + 8, &d[i], 4);
  }
  /* the slots after the last fleet point to a spare record (the code can read the slot n) */
  memset((uint8_t *)LIN(g_fl_blk) + 127 * FL_SIZE, 0, FL_SIZE);
  for (unsigned i = n; i < n + 4 && i < 100; i++) {
    uint32_t spare = MK_FP(HI(g_fl_blk), (uint16_t)(127 * FL_SIZE));
    memcpy((uint8_t *)LIN(g_fl_arr) + 4 * i, &spare, 4);
  }
  ds_w32(0x00ba, g_fl_arr);
  DS_W(0x553a) = (uint16_t)n;
}

/* ---- 5:0x36a8 is more than one target selected ---- */

static void t_36a8(void) {
  static const uint16_t sels[] = {0xffff, 3};
  const uint32_t key = 0x00120034u;
  ds_w32(0x4b7c, key);
  for (unsigned a = 0; a < 2; a++)
    for (unsigned b = 0; b < 2; b++)
      for (unsigned c = 0; c < 2; c++)
        for (unsigned nf = 0; nf < 4; nf++)
          for (unsigned nm = 0; nm < 4; nm++) {
            DS_W(0x4b84) = sels[a];
            DS_W(0x4b86) = sels[b];
            DS_W(0x4b8a) = sels[c];
            /* nf fleets match (of 3), nm map objects match (of 3) */
            uint32_t d[3];
            for (unsigned i = 0; i < 3; i++) d[i] = i < nf ? key : 0x77u + i;
            fl_setup(3, d, -1);
            mo_setup(3, 0);
            for (unsigned i = 0; i < 3; i++) {
              memset(mo_at(i), 0, MO_SIZE);
              uint32_t v = (i < nm) ? key : 0x55u;
              memcpy(mo_at(i) + 2, &v, 4);
            }
            DIFF(5, 0x36a8, FAR_CDECL);
          }
  /* a null fleet pointer ends the fleet search; the search is bounded by the count */
  DS_W(0x4b84) = 0xffff;
  DS_W(0x4b86) = 3;
  DS_W(0x4b8a) = 0xffff;
  {
    uint32_t d[3] = {key, key, key};
    fl_setup(3, d, 1);
    DIFF(5, 0x36a8, FAR_CDECL);
    fl_setup(3, d, 0);
    DIFF(5, 0x36a8, FAR_CDECL);
    fl_setup(3, d, 2);
    DIFF(5, 0x36a8, FAR_CDECL);
    fl_setup(2, d, -1);
    DIFF(5, 0x36a8, FAR_CDECL);
    fl_setup(1, d, -1);
    DIFF(5, 0x36a8, FAR_CDECL);
    DS_W(0x553a) = 0;
    DIFF(5, 0x36a8, FAR_CDECL);
    DS_W(0x553a) = 0x8000; /* negative: no iteration */
    DIFF(5, 0x36a8, FAR_CDECL);
  }
  /* map objects: empty array and the end offset not above the base */
  DS_W(0x4b8a) = 3;
  mo_setup(0, 0);
  DIFF(5, 0x36a8, FAR_CDECL);
  mo_setup(2, 0xffe0);
  DIFF(5, 0x36a8, FAR_CDECL);
  mo_setup(1, 0);
  memcpy(mo_at(0) + 2, &key, 4);
  DIFF(5, 0x36a8, FAR_CDECL);
  /* the dwords differ in one half only */
  {
    uint32_t v = key ^ 0x10000u;
    memcpy(mo_at(0) + 2, &v, 4);
    DIFF(5, 0x36a8, FAR_CDECL);
    v = key ^ 1u;
    memcpy(mo_at(0) + 2, &v, 4);
    DIFF(5, 0x36a8, FAR_CDECL);
  }
  DS_W(0x4b84) = 0xffff;
  DS_W(0x4b86) = 0xffff;
  DS_W(0x4b8a) = 0xffff;
}

/* ---- drawing helpers: a screen DC, a bitmap and brushes ---- */

static void setrect(uint16_t at, int16_t l, int16_t t, int16_t r, int16_t b) {
  DS_W(at) = (uint16_t)l;
  DS_W(at + 2) = (uint16_t)t;
  DS_W(at + 4) = (uint16_t)r;
  DS_W(at + 6) = (uint16_t)b;
}
static uint16_t g_dc;
static void gdi_setup(void) {
  g_dc = gdi_create_screen_dc();
  CHECK(g_dc != 0);
  DS_W(0x4c74) = CreateCompatibleBitmap(g_dc, 0x40, 0x40);
  CHECK(DS_W(0x4c74) != 0);
  DS_W(0x0010) = GetStockObject(0); /* WHITE_BRUSH */
  DS_W(0x0014) = GetStockObject(4); /* BLACK_BRUSH */
  DS_W(0x0016) = GetStockObject(2); /* GRAY_BRUSH */
}

/* ---- 5:0x344a draw a button with a bitmap, 5:0x34fc draw a 3-D frame ---- */

static void t_344a(void) {
  static const int16_t rects[][4] = {{10, 20, 50, 40}, {10, 20, 51, 41}, {10, 20, 11, 21}, {50, 40, 10, 20},
                                     {51, 41, 10, 20}, {0, 0, 0, 0},     {-30, -20, -5, -9}, {0x7000, 0x7000, -0x7000, -0x7000},
                                     {-1, -1, 0, 0},   {0, 0, -1, -1},   {100, 7, 38, 90}};
  static const uint16_t states[] = {0, 1, 2, 0xffff, 0x8000};
  for (unsigned r = 0; r < sizeof rects / sizeof rects[0]; r++) {
    setrect(SCR, rects[r][0], rects[r][1], rects[r][2], rects[r][3]);
    for (unsigned k = 0; k < sizeof states / sizeof states[0]; k++) DIFF(5, 0x344a, FAR_CDECL, g_dc, SCR, states[k]);
  }
  DS_W(0x4c74) = 0; /* no bitmap: SelectObject fails the same way in both */
  DIFF(5, 0x344a, FAR_CDECL, g_dc, SCR, 1);
  DS_W(0x4c74) = CreateCompatibleBitmap(g_dc, 0x40, 0x40);
  DIFF(5, 0x344a, FAR_CDECL, 0, SCR, 0); /* hdc 0 */
}

static void t_34fc(void) {
  static const int16_t rects[][4] = {{10, 20, 50, 40}, {10, 20, 51, 41}, {10, 20, 12, 22}, {10, 20, 13, 23},
                                     {10, 20, 14, 24}, {50, 40, 10, 20}, {51, 41, 10, 20}, {0, 0, 0, 0},
                                     {-30, -20, -5, -9}, {10, 20, 11, 21}, {10, 20, 20, 19}, {0x7000, 0x7000, -0x7000, -0x7000},
                                     {10, 20, 60, 22}, {10, 20, 14, 60}, {5, 5, 9, 9}};
  uint16_t brushes[] = {GetStockObject(0), GetStockObject(4), 0};
  for (unsigned r = 0; r < sizeof rects / sizeof rects[0]; r++) {
    setrect(SCR, rects[r][0], rects[r][1], rects[r][2], rects[r][3]);
    for (unsigned k = 0; k < 3; k++) DIFF(5, 0x34fc, FAR_CDECL, g_dc, SCR, brushes[k]);
  }
  DIFF(5, 0x34fc, FAR_CDECL, 0, SCR, 0);
}

/* ---- windows. The test class uses the runtime's default window procedure ---- */

static void register_class(const char *name, uint32_t proc) {
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = proc;
  w->lpszClassName = fstr(name);
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent) {
  uint16_t h = CreateWindow(fstr("SEG05TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent,
                            0, 0x1234, 0);
  CHECK(h != 0);
  return h;
}

/* ---- the planets (DS:0x00b6 far array of 0x38-byte records, DS:0x4d96 and DS:0x007a count) ---- */

static uint32_t g_pl;
static void pl_setup(unsigned n) {
  if (!g_pl) g_pl = gblock(0x38 * 16);
  memset(LIN(g_pl), 0, 0x38 * n);
  for (unsigned i = 0; i < n; i++) *(uint16_t *)((uint8_t *)LIN(g_pl) + 0x38 * i) = (uint16_t)i;
  ds_w32(0x00b6, g_pl);
  DS_W(0x4d96) = (uint16_t)n;
  DS_W(0x007a) = (uint16_t)n;
}
static void pl_set(unsigned i, uint16_t owner, uint8_t flags) {
  uint8_t *r = (uint8_t *)LIN(g_pl) + 0x38 * i;
  memcpy(r + 2, &owner, 2);
  r[5] = flags;
}

/* ---- 5:0x2840 hit test ---- */

static unsigned g_diffs, g_limit = 0xffffffffu;
static void t_2840(void) {
  pl_setup(8);
  static const struct { uint16_t m80, m82; } modes[] = {{0, 0}, {1, 0}, {3, 0}, {5, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 0}, {2, 0}, {8, 0}};
  static const int W[] = {300, 211}, H[] = {200, 129};
  static const uint16_t hs[] = {0x10, 0x13};
  for (unsigned geo = 0; geo < 2; geo++) {
    uint16_t win = mkwin(0x90000000, 0, 0, W[geo], H[geo], 0);
    DS_W(0x267a) = hs[geo];
    int16_t h = (int16_t)hs[geo], top = (int16_t)(2 * h), right = W[geo], bottom = H[geo], left = 0;
    int16_t top2 = (int16_t)(top - 4);
    int16_t n = (int16_t)(((bottom - 4 * h - top2 - 2) / 6 + 1) & 0xfffe);
    int16_t di = (int16_t)(2 * h + top2);
    int16_t xt[] = {left + 9, left + 0x19, left + 0x39, left + 0x49, left + 0x5c, right - 4, right - 0x18, right * 3 / 5,
                    right - top, 0, 1};
    int16_t yt[] = {top - 5, 4, top2, di, di + 3 * n, (int16_t)(di + h / 2 + 3 * n + 1), (int16_t)(di + h / 2 + 3 * n + 1 + 3 * n),
                    top + 4, top + 9, top + 0x47, top + 0x49, top + 0x67, top + h + 7, di - h, di + n, di + 2 * n, 0, 1};
    int16_t xs[64], ys[96];
    unsigned nx = 0, ny = 0;
    for (unsigned i = 0; i < sizeof xt / sizeof xt[0]; i++)
      for (int d = -1; d <= 1; d++) xs[nx++] = (int16_t)(xt[i] + d);
    for (unsigned i = 0; i < sizeof yt / sizeof yt[0]; i++)
      for (int d = -1; d <= 1; d++) ys[ny++] = (int16_t)(yt[i] + d);
    xs[nx++] = -32768; xs[nx++] = 32767; ys[ny++] = -32768; ys[ny++] = 32767;
    for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++) {
      for (unsigned v = 0; v < 12; v++) {
        DS_W(0x4b80) = modes[m].m80;
        DS_W(0x4b82) = modes[m].m82;
        DS_W(0x4b84) = (v & 1) ? 2 : 0xffff;
        DS_W(0x014c) = 3;
        uint16_t owner = (v & 2) ? ((v & 4) ? 3 : 0xffff) : 5;
        pl_set(2, owner, (uint8_t)((v & 8) ? 2 : 0));
        /* 36a8: more than one selected when 4b84 and 4b86 are both set */
        DS_W(0x4b86) = (v % 3 == 0) ? 1 : 0xffff;
        DS_W(0x4b8a) = 1;
        mo_setup(3, 0);
        for (unsigned i = 0; i < 3; i++) memset(mo_at(i), 0, MO_SIZE);
        mo_at(1)[1] = (uint8_t)(v % 3 == 1 ? 0x40 : (v % 3 == 2 ? 0x20 : 0));
        /* random points drawn from the threshold neighbourhoods (10 per state; 60 for the planet-selected
         * variant of every mode of the first window) */
        unsigned np = (geo == 0 && v == 1) ? 60 : 10;
        for (unsigned k = 0; k < np; k++) {
          int16_t px = xs[rnd() % nx], py = ys[rnd() % ny];
          if (rnd() % 4 == 0) px = (int16_t)(rnd() % (unsigned)(W[geo] + 6) - 3);
          if (rnd() % 4 == 0) py = (int16_t)(rnd() % (unsigned)(H[geo] + 6) - 3);
          DIFF(5, 0x2840, FAR_CDECL, win, (uint16_t)px, (uint16_t)py);
          if (++g_diffs >= g_limit) return;
        }
      }
    }
  }
  DS_W(0x4b80) = 0;
}

/* ---- 5:0x0324 measure the labels and invalidate the window ---- */

static void t_0324(void) {
  static const char *const texts[] = {"", "a", "Mine field", "A much longer label text that is wide", "WWWWWWWWWWWW", "ii"};
  static const struct { int w, h; uint16_t fh; } geo[] = {{300, 200, 0x10}, {120, 90, 0x13}, {640, 480, 0x0c}, {40, 30, 0x02}, {500, 100, 0x01}};
  for (unsigned g = 0; g < sizeof geo / sizeof geo[0]; g++) {
    uint16_t win = mkwin(0x90000000, 0, 0, geo[g].w, geo[g].h, 0);
    DS_W(0x0154) = win;
    DS_W(0x267a) = geo[g].fh;
    DS_W(0x292e) = GetStockObject(13); /* SYSTEM_FONT */
    DS_W(0x2930) = GetStockObject(12); /* ANSI_VAR_FONT */
    for (unsigned t = 0; t < sizeof texts / sizeof texts[0]; t++) {
      memcpy(LINN(SCR), texts[t], strlen(texts[t]) + 1);
      DS_W(0x0490) = SCR;
      DIFF(5, 0x0324, FAR_CDECL);
    }
  }
  /* a bad window handle: GetClientRect / GetDC fail the same way */
  DS_W(0x0154) = 0;
  DIFF(5, 0x0324, FAR_CDECL);
}

/* ---- 5:0x329a rebuild the header name, the order button and the window layout ---- */

/* The players (DS:0x5a64, stride 0xc0): the byte at +0x4c is what 28:0x21ee reads. */
static void players_setup(uint8_t v) {
  for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5a64 + p * 0xc0 + 0x4c)) = (uint8_t)(v + p % 2);
}

static void t_329a(void) {
  uint16_t win = mkwin(0x90000000, 0, 0, 300, 200, 0);
  uint16_t btn = mkwin(0x90000000, 10, 10, 50, 20, 0);
  DS_W(0x573c) = btn;
  DS_W(0x267a) = 0x10;
  DS_W(0x014c) = 3;
  pl_setup(8);
  uint32_t d[3] = {1, 2, 3};
  fl_setup(3, d, -1);
  DS_W(0x553a) = 0; /* no fleet is found by id: the fleet name falls back to a fixed string */
  static const struct { uint16_t m80, m82; } modes[] = {{1, 0}, {2, 0}, {8, 0}, {4, 1}, {4, 2}, {4, 8}, {4, 0}, {0, 0}, {3, 0}, {10, 0}, {9, 0}};
  static const uint16_t idx[] = {0xffff, 1, 2};
  for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++)
    for (unsigned a = 0; a < 3; a++) {
      DS_W(0x4b80) = modes[m].m80;
      DS_W(0x4b82) = modes[m].m82;
      DS_W(0x4b84) = idx[a];
      DS_W(0x4b86) = idx[a];
      DS_W(0x4b8a) = idx[a];
      /* map objects: ids 7, 8, 9; kinds / players / +0xc / +0xd vary */
      for (unsigned var = 0; var < 6; var++) {
        mo_setup(3, 0);
        for (unsigned i = 0; i < 3; i++) {
          uint8_t *o = mo_at(i);
          memset(o, 0, MO_SIZE);
          o[0] = (uint8_t)(7 + i);
          o[1] = (uint8_t)((var == 1 && i == 1 ? 0x20 : 0) | (((var == 2 && i == 1) ? 4 : 3) << 1));
          o[0xc] = (uint8_t)((var == 3 && i == 1) ? 2 : 0);
          o[0xd] = (uint8_t)((var & 1) ? 1 : 0);
        }
        players_setup((uint8_t)(var == 4 ? 4 : 5));
        DIFF(5, 0x329a, FAR_CDECL, win);
      }
    }
  /* the header string is long: the copy runs over the word / byte split with odd and even lengths */
  DS_W(0x4b80) = 1;
  DS_W(0x4b84) = 0xffff;
  DIFF(5, 0x329a, FAR_CDECL, 0);
  DS_W(0x573c) = 0;
  DS_W(0x4b80) = 8;
  DS_W(0x4b8a) = 1;
  mo_setup(3, 0);
  DIFF(5, 0x329a, FAR_CDECL, win);
}

/* ---- 5:0x3a72 mineral output of a planet (and of the fleets that mine it) ---- */

static uint32_t rb(unsigned n) { return n ? rnd() % n : 0; }
#define CH(a, b) (rb(b) < (a))
static void wr8f(uint32_t p, unsigned off, uint8_t v) { *((uint8_t *)LIN(p) + off) = v; }
static void wr16f(uint32_t p, unsigned off, uint16_t v) { memcpy((uint8_t *)LIN(p) + off, &v, 2); }
static void wr32f(uint32_t p, unsigned off, uint32_t v) { memcpy((uint8_t *)LIN(p) + off, &v, 4); }

static uint32_t g_dtab5[4], g_wp5[8];
static void world5_alloc(void) {
  if (g_dtab5[0]) return;
  for (unsigned i = 0; i < 4; i++) g_dtab5[i] = gblock(0x8d * 16);
  for (unsigned i = 0; i < 8; i++) g_wp5[i] = gblock(0x40);
  for (unsigned i = 0; i < 4; i++) ds_w32((uint16_t)(0xbe + 4 * i), g_dtab5[i]);
}
static void t_3a72(void) {
  world5_alloc();
  uint32_t pl = gblock(0x40), outp = OUT;
  DS_W(0x014c) = 1;
  for (unsigned it = 0; it < 420; it++) {
    memset(LIN(pl), 0, 0x40);
    /* players 0..3: the type bytes read through 28:0x21ee at +0x42, +0x44 and +0x4c (8 = the special type) */
    for (unsigned q = 0; q < 4; q++) {
      DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x42)) = (uint8_t)(CH(1, 3) ? 0 : (int8_t)(rb(30) - 8));
      DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x44)) = (uint8_t)(rb(120));
      DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x4c)) = (uint8_t)(CH(1, 2) ? 8 : rb(12));
      /* the habitability ranges read by 9:0x474c: low < ideal < high per environment (or immune = -1) */
      for (unsigned k = 0; k < 3; k++) {
        unsigned lo = rb(30), mid = lo + 1 + rb(20), hi = mid + 1 + rb(20);
        if (CH(1, 6)) { lo = 0xff; mid = 0xff; hi = 0xff; }
        DS_B((uint16_t)(0x5a74 + q * 0xc0 + k)) = (uint8_t)mid;
        DS_B((uint16_t)(0x5a77 + q * 0xc0 + k)) = (uint8_t)lo;
        DS_B((uint16_t)(0x5a7a + q * 0xc0 + k)) = (uint8_t)hi;
      }
    }
    wr16f(pl, 0, (uint16_t)(1 + rb(3)));                       /* id */
    wr16f(pl, 2, CH(1, 12) ? 0xffff : (uint16_t)rb(4));        /* owner */
    wr8f(pl, 5, (uint8_t)(CH(1, 3) ? 4 : 0) | (uint8_t)rb(2));  /* flags: bit 2 = no output */
    for (unsigned k = 0; k < 3; k++) {
      static const uint8_t cnt[] = {0, 1, 2, 4, 5, 6, 0x18, 0x19, 0x1d, 0x1e, 0x1f, 0x63, 0x64, 0x65, 0xff, 10, 20, 50};
      wr8f(pl, 6 + k, CH(1, 4) ? 0 : (uint8_t)rb(256));      /* concentration */
      wr8f(pl, 9 + k, cnt[rb(sizeof cnt)]);                   /* the miners' count */
    }
    for (unsigned k = 0; k < 3; k++) wr8f(pl, 0xc + k, (uint8_t)rb(100));
    wr16f(pl, 0x14, (uint16_t)rnd());
    wr16f(pl, 0x16, (uint16_t)rb(16));
    for (unsigned k = 0; k < 3; k++) wr32f(pl, 0x1c + 4 * k, CH(1, 3) ? 0 : rnd() % 100000);
    wr32f(pl, 0x28, CH(1, 4) ? 0 : (CH(1, 3) ? 0xffffu : rnd() % 70000));
    /* the fleets: some sit at the planet, owned by it, on a mining task */
    unsigned nf = rb(5);
    uint32_t d3[6] = {0};
    fl_setup(nf, d3, -1);
    for (unsigned i = 0; i < nf; i++) {
      uint32_t fp = MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE));
      wr16f(fp, 0, (uint16_t)(0x30 + i));
      wr16f(fp, 2, CH(3, 4) ? *(uint16_t *)((uint8_t *)LIN(pl) + 2) : (uint16_t)rb(4));
      wr8f(fp, 5, (uint8_t)(CH(1, 5) ? 4 : 0));
      wr16f(fp, 6, CH(4, 5) ? *(uint16_t *)LIN(pl) : (uint16_t)rb(4));
      wr16f(fp, 0x62, (uint16_t)(CH(1, 6) ? 2 : rb(2)));
      wr32f(fp, 0x64, MK_FP(HI(g_wp5[i]), 0));
      memset(LIN(g_wp5[i]), 0, 0x40);
      wr8f(g_wp5[i], 0xa, (uint8_t)(CH(5, 6) ? 3 : rb(16)));
      for (unsigned k = 0; k < 16; k++) wr16f(fp, 0x0c + 2 * k, 0);
      wr16f(fp, 0x0c, (uint16_t)rb(4));
      /* the owner's design 0: one weapon slot */
      uint32_t dsg = g_dtab5[*(uint16_t *)((uint8_t *)LIN(fp) + 2) & 3];
      memset(LIN(dsg), 0, 0x8d);
      wr8f(dsg, 0x7a, 1);
      wr16f(dsg, 0x3a, 0x80);
      wr8f(dsg, 0x3c, (uint8_t)rb(7));
      wr8f(dsg, 0x3d, (uint8_t)(1 + rb(3)));
    }
    if (CH(1, 8)) {  /* a null fleet pointer ends the search */
      uint32_t null4 = 0;
      if (nf) memcpy(LIN(g_fl_arr), &null4, 4);
    }
    DS_W(0x790) = (uint16_t)(CH(1, 2) ? 2 : 0);
    uint32_t dd = CH(1, 2) ? 0xffffffffu : (CH(1, 6) ? (uint32_t)(int32_t)-(int32_t)rb(500) : rb(3000));
    uint16_t flag = (uint16_t)(CH(1, 2) ? 1 : 0);
    DIFF(5, 0x3a72, FAR_CDECL, LO(pl), HI(pl), outp, LO(dd), HI(dd), flag);
  }
  /* the depletion loop: explicit rate d, flag set. The produced amount is count * d / 100; the loop
   * takes whole miners (t = concentration * 0x30d4 / 256 / divisor each) while it can, then a part */
  for (unsigned ci = 0; ci < 6; ci++) {
    static const uint8_t counts[] = {2, 7, 30, 60, 100, 120};
    for (unsigned wi = 0; wi < 5; wi++) {
      static const uint8_t concs[] = {1, 2, 50, 100, 255};
      unsigned cnt = counts[ci], W = concs[wi];
      unsigned div = cnt > 0x64 ? 0x64 : cnt < 5 ? 10 : cnt < 0x19 ? 0x19 : cnt;
      int tt = (int)((W * 0x30d4 / 256) / div);
      for (int dv = -3; dv <= 3; dv++) {
        int target = tt + dv;
        if (target < 0) continue;
        uint32_t dd = (uint32_t)((target * 100 + (int)cnt - 1) / (int)cnt);
        memset(LIN(pl), 0, 0x40);
        wr16f(pl, 0, 1);
        wr16f(pl, 2, 1);
        for (unsigned k = 0; k < 3; k++) { wr8f(pl, 6 + k, (uint8_t)W); wr8f(pl, 9 + k, (uint8_t)cnt); }
        DS_W(0x790) = 0;
        DIFF(5, 0x3a72, FAR_CDECL, LO(pl), HI(pl), outp, LO(dd), HI(dd), 1);
        DS_W(0x790) = 2;
        DIFF(5, 0x3a72, FAR_CDECL, LO(pl), HI(pl), outp, LO(dd), HI(dd), 1);
      }
    }
  }
  DS_W(0x553a) = 0;
}

/* ---- 5:0x3774 build and show the pop-up menu of the objects at the selected place ---- */

static void t_3774(void) {
  uint16_t win = mkwin(0x90000000, 0, 0, 300, 200, 0);
  const uint32_t key = 0x00120034u;
  DS_W(0x014c) = 3;
  for (unsigned it = 0; it < 300; it++) {
    DS_W(0x4b80) = (uint16_t)(CH(1, 4) ? 1 : CH(1, 3) ? 2 : CH(1, 2) ? 8 : rb(6));
    DS_W(0x4b84) = CH(1, 3) ? 0xffff : (uint16_t)rb(8);
    DS_W(0x4b86) = CH(1, 3) ? 0xffff : (uint16_t)rb(4);
    DS_W(0x4b8a) = CH(1, 3) ? 0xffff : (uint16_t)rb(4);
    ds_w32(0x4b7c, key);
    /* planets for 7:0x01e4 */
    pl_setup(8);
    for (unsigned i = 0; i < 8; i++) pl_set(i, CH(1, 2) ? 3 : (uint16_t)rb(5), 0);
    /* fleets: ids 0x30 + i, some at the key place */
    unsigned nf = rb(8);
    if (DS_W(0x4b80) == 2) { /* the fleet DS:0x4b86 exists (the game never selects a missing one) */
      if (nf == 0) nf = 1;
      DS_W(0x4b86) = (uint16_t)rb(nf);
    }
    uint32_t d[8];
    for (unsigned i = 0; i < 8; i++) d[i] = CH(2, 3) ? key : 0x77u + i;
    int nullat = CH(1, 12) ? (int)rb(nf ? nf : 1) : -1;
    if (DS_W(0x4b80) == 2 && nullat >= 0 && nullat <= (int)DS_W(0x4b86)) nullat = -1; /* the selected fleet is not null */
    fl_setup(nf, d, nullat);
    for (unsigned i = 0; i < nf; i++) {
      uint32_t fp = MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE));
      wr16f(fp, 0, (uint16_t)(CH(1, 10) ? 0x8001 + i : 0x30 + i));
      wr16f(fp, 2, CH(1, 2) ? 3 : (uint16_t)rb(5));
    }
    /* map objects: ids 0x40 + i, some at the key place */
    unsigned nm = rb(6);
    mo_setup((uint16_t)nm, 0);
    for (unsigned i = 0; i < nm; i++) {
      uint8_t *o = mo_at(i);
      memset(o, 0, MO_SIZE);
      o[0] = (uint8_t)(0x40 + i);
      uint32_t v = CH(2, 3) ? key : 0x55u;
      memcpy(o + 2, &v, 4);
    }
    g_pick = CH(1, 6) ? 0xffff : (uint16_t)rb(100);
    g_zap = CH(1, 8);
    g_zap2 = CH(1, 10) && nullat < 0;
    stub_ret(STUB_9_2A74, 0, 0);
    DIFF(5, 0x3774, FAR_CDECL, win, 0x0123, 0x0456);
  }
  /* the fleet list is cut when the item array is full (92 items from the 2 first) */
  {
    uint32_t d[100];
    for (unsigned i = 0; i < 100; i++) d[i] = key;
    DS_W(0x4b84) = 3;
    DS_W(0x4b80) = 1;
    fl_setup(100, d, -1);
    mo_setup(0, 0);
    DS_W(0x553a) = 100;
    g_pick = 7;
    DIFF(5, 0x3774, FAR_CDECL, win, 1, 2);
    g_pick = 0xffff;
    DIFF(5, 0x3774, FAR_CDECL, win, 1, 2);
  }
  g_pick = 0xffff;
  g_zap = false;
  g_zap2 = false;
}

/* ---- a random world for the click handler 5:0x2ab4 ---- */

static uint16_t g_win2, g_win3, g_win4;
static uint32_t g_sb5[4];
static bool g_noneg_cur;
static void world_random(void) {
  world5_alloc();
  if (!g_win2) {
    g_win2 = mkwin(0x90000000, 0, 0, 300, 200, 0);
    g_win3 = mkwin(0x90000000, 5, 5, 40, 30, 0);
    g_win4 = mkwin(0x90000000, 9, 9, 40, 30, 0);
    for (unsigned i = 0; i < 4; i++) g_sb5[i] = gblock(0x8d * 10);
  }
  DS_W(0x0154) = g_win2;
  DS_W(0x0150) = g_win3;
  DS_W(0x573c) = g_win4;
  DS_W(0x267a) = 0x10;
  DS_W(0x014c) = (!g_noneg_cur && CH(1, 6)) ? 0xffff : (uint16_t)(g_noneg_cur ? rb(3) : 1); /* -1: no current player, 9:0x5220 and 2:0x50f8 take the planet's owner */
  DS_W(0x0180) = SCR + 0x80; /* the format "%d" of the value list */
  memcpy(LINN(SCR + 0x80), "%d", 3);
  /* players 0..3: type bytes read through 28:0x21ee (+0x42, +0x44, +0x4c), habitability ranges (+0x10..) */
  for (unsigned q = 0; q < 4; q++) {
    DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x42)) = (uint8_t)(CH(1, 3) ? 0 : (int8_t)(rb(30) - 8));
    DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x44)) = (uint8_t)(rb(120));
    DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x4c)) = (uint8_t)(CH(1, 2) ? 8 : rb(12));
    for (unsigned k = 0; k < 3; k++) {
      unsigned lo = rb(30), mid = lo + 1 + rb(20), hi = mid + 1 + rb(20);
      if (CH(1, 6)) { lo = 0xff; mid = 0xff; hi = 0xff; }
      DS_B((uint16_t)(0x5a74 + q * 0xc0 + k)) = (uint8_t)mid;
      DS_B((uint16_t)(0x5a77 + q * 0xc0 + k)) = (uint8_t)lo;
      DS_B((uint16_t)(0x5a7a + q * 0xc0 + k)) = (uint8_t)hi;
    }
    for (unsigned k = 0; k < 6; k++) DS_B((uint16_t)(0x5a7e + q * 0xc0 + k)) = (uint8_t)(CH(2, 3) ? 26 : rb(27)); /* the tech levels */
    ds_w32((uint16_t)(0x010c + 4 * q), g_sb5[q]); /* the star-base design tables */
    for (unsigned k = 0; k < 10; k++) *(uint8_t *)LIN(MK_FP(HI(g_sb5[q]), (uint16_t)(0x8d * k + 8))) = (uint8_t)('A' + q);
    /* ship designs of the owner: names at +8 */
    for (unsigned k = 0; k < 16; k++) {
      uint32_t dsg = MK_FP(HI(g_dtab5[q]), (uint16_t)(0x8d * k));
      memset(LIN(dsg), 0, 0x8d);
      char nm[24];
      snprintf(nm, sizeof nm, "Design %c%u%s", (char)('a' + q), k, k % 3 == 0 ? " &" : "");
      memcpy((uint8_t *)LIN(dsg) + 8, nm, strlen(nm) + 1);
      wr8f(dsg, 0x7a, 1);
      wr16f(dsg, 0x3a, 0x80);
      wr8f(dsg, 0x3c, (uint8_t)rb(7));
      wr8f(dsg, 0x3d, (uint8_t)(1 + rb(3)));
    }
  }
  /* planets: id = index, owner -1 / current / other, info level 0..5, minerals, population */
  pl_setup(8);
  for (unsigned i = 0; i < 8; i++) {
    uint8_t *r = (uint8_t *)LIN(g_pl) + 0x38 * i;
    pl_set(i, CH(1, 4) ? 0xffff : (CH(1, 2) ? 1 : (uint16_t)rb(4)), (uint8_t)(rb(256) & 0xfb));
    r[4] = (uint8_t)rb(6);
    for (unsigned k = 0; k < 3; k++) {
      r[6 + k] = CH(1, 4) ? 0 : (uint8_t)rb(256);
      static const uint8_t cnt[] = {0, 1, 2, 5, 0x19, 0x1d, 0x1e, 0x64, 0x65, 0xff, 10, 20, 50};
      r[9 + k] = cnt[rb(sizeof cnt)];
      r[0xc + k] = (uint8_t)rb(100);
      uint32_t m = CH(1, 3) ? 0 : (CH(1, 2) ? rb(200) : rb(100000));
      memcpy(r + 0x1c + 4 * k, &m, 4);
    }
    r[0x2c] = (uint8_t)rb(256);
    uint32_t pop = CH(1, 3) ? 0 : rb(70000);
    memcpy(r + 0x28, &pop, 4);
    *(uint16_t *)(r + 0x14) = (uint16_t)rnd();
    *(uint16_t *)(r + 0x16) = (uint16_t)rb(16);
  }
  /* selection */
  const uint32_t key = 0x00120034u;
  ds_w32(0x4b7c, key);
  DS_W(0x4b80) = (uint16_t)(CH(1, 4) ? 4 : CH(1, 3) ? 1 : CH(1, 2) ? 2 : CH(1, 2) ? 8 : rb(6));
  DS_W(0x4b82) = (uint16_t)rb(16);
  DS_W(0x4b84) = (uint16_t)rb(8); /* (the selections of the game are valid indexes) */
  DS_W(0x4b88) = (uint16_t)rb(0x100);
  DS_W(0x4b74) = (uint16_t)(CH(1, 3) ? 2 : rb(4));
  DS_W(0x055e) = (uint16_t)(CH(1, 2) ? 1 : rb(16));
  static const uint16_t vals[] = {100, 500, 1000, 2500, 5000, 7500, 10000, 20000, 30000, 7};
  DS_W(0x04c8) = vals[rb(10)];
  /* fleets: ids 0x30 + i, some at the key place, some mining at a planet */
  unsigned nf = 1 + rb(7);
  uint32_t d[8];
  for (unsigned i = 0; i < 8; i++) d[i] = CH(2, 3) ? key : 0x77u + i;
  fl_setup(nf, d, -1); /* (a null fleet pointer in the list would make the original read through selector 0) */
  for (unsigned i = 0; i < nf; i++) {
    uint32_t fp = MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE));
    wr16f(fp, 0, (uint16_t)(0x30 + i));
    wr16f(fp, 2, CH(3, 4) ? 1 : (uint16_t)rb(4));
    wr8f(fp, 4, CH(1, 2) ? 7 : (uint8_t)rb(9));
    wr8f(fp, 5, (uint8_t)(CH(1, 5) ? 4 : 0));
    wr16f(fp, 6, CH(2, 3) ? DS_W(0x4b84) : (uint16_t)rb(8));
    wr16f(fp, 0x62, (uint16_t)(CH(1, 6) ? 2 : rb(2)));
    wr32f(fp, 0x64, MK_FP(HI(g_wp5[i]), 0));
    memset(LIN(g_wp5[i]), 0, 0x40);
    wr8f(g_wp5[i], 0xa, (uint8_t)(CH(5, 6) ? 3 : rb(16)));
    for (unsigned k = 0; k < 16; k++) wr16f(fp, 0x0c + 2 * k, CH(1, 3) ? (uint16_t)(1 + rb(3)) : 0);
    if (CH(1, 3)) { /* a fleet of one design, or of none */
      for (unsigned k = 0; k < 16; k++) wr16f(fp, 0x0c + 2 * k, 0);
      if (CH(1, 2)) wr16f(fp, 0x0c + 2 * rb(16), 2);
    }
  }
  DS_W(0x4b86) = (uint16_t)rb(nf);
  /* map objects: ids 0x40 + i, kinds, fields */
  unsigned nm = 1 + rb(5);
  mo_setup((uint16_t)nm, 0);
  for (unsigned i = 0; i < nm; i++) {
    uint8_t *o = mo_at(i);
    memset(o, 0, MO_SIZE);
    o[0] = (uint8_t)(0x40 + i);
    o[1] = (uint8_t)((CH(1, 4) ? 0x20 : CH(1, 4) ? 0x40 : 0) | (rb(5) << 1));
    uint32_t v = CH(1, 2) ? key : 0x55u;
    memcpy(o + 2, &v, 4);
    o[0xc] = (uint8_t)rb(3);
  }
  DS_W(0x4b8a) = (uint16_t)rb(nm);
  if (DS_W(0x4b82) & 8) { /* (5:0x2ab4 zone 9 would loop forever when the map object of the place is missing) */
    uint32_t v = key;
    memcpy(mo_at(0) + 2, &v, 4);
  }
  /* the string-table bytes that 5:0x2ab4 zone 14 maps through DS:0x4de: item numbers 0..9 only */
  g_pick = CH(1, 5) ? 0xffff : (uint16_t)rb(100);
  g_zap = false;
  g_zap2 = false;
}

/* The zone code of 5:0x2840 for a point, by the ORIGINAL code (set-up, not under test). */
static uint16_t zone_of(uint16_t win, int16_t x, int16_t y) {
  const uint16_t a[] = {win, (uint16_t)x, (uint16_t)y};
  return (uint16_t)harness_call(SEL5, 0x2840, HCONV_CDECL, a, 3, NULL);
}

static unsigned g_zone_hits[16];
static void diff_zones(const bool *have, const uint32_t *pts);
static void t_2ab4(void) {
  unsigned iters = 160;
  if (getenv("SEG05_ITERS")) iters = (unsigned)atoi(getenv("SEG05_ITERS"));
  for (unsigned it = 0; it < iters; it++) {
    world_random();
    /* one point per zone, searched on a coarse grid of the window with the ORIGINAL hit test */
    uint32_t pts[16];
    bool have[16] = {false};
    for (unsigned k = 0; k < 90; k++) {
      int16_t x = (int16_t)(rb(310) - 5), y = (int16_t)(rb(210) - 5);
      uint16_t z = zone_of(g_win2, x, y);
      if (z < 16 && !have[z]) {
        have[z] = true;
        pts[z] = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
      }
    }
    diff_zones(have, pts);
  }
  /* zone 9 cycles the selection through the places by the mask in DS:0x4b82: every kind x mask, with and
   * without fleets / map objects at the place (the original loops forever when a mask with bit 8 finds no
   * map object, so object 0 always holds the key then) */
  static const uint16_t kinds[] = {1, 2, 8, 0};
  for (unsigned it = 0; it < 4 * 16 * 3; it++) {
    world_random();
    DS_W(0x4b80) = kinds[(it / 48) % 4];
    DS_W(0x4b82) = (uint16_t)((it / 3) % 16);
    unsigned variant = it % 3;
    const uint32_t key = 0x00120034u;
    for (unsigned i = 0; i < DS_W(0x553a); i++) {
      uint32_t v = (variant == 0) ? key : (variant == 1 && i == 0) ? key : 0x77u;
      wr32f(MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE)), 8, v);
    }
    mo_setup(3, 0);
    for (unsigned i = 0; i < 3; i++) {
      memset(mo_at(i), 0, MO_SIZE);
      uint32_t v = (i == 0 || (variant == 0 && i == 1)) ? key : 0x55u;
      memcpy(mo_at(i) + 2, &v, 4);
      mo_at(i)[1] = (uint8_t)(i << 1);
    }
    DS_W(0x4b8a) = (uint16_t)(it % 3);
    DS_W(0x4b86) = (uint16_t)(it % DS_W(0x553a));
    uint32_t pts[16];
    bool have[16] = {false};
    for (unsigned k = 0; k < 200 && !have[9]; k++) {
      int16_t x = (int16_t)(rb(310) - 5), y = (int16_t)(rb(210) - 5);
      if (zone_of(g_win2, x, y) == 9) {
        have[9] = true;
        pts[9] = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
      }
    }
    if (have[9]) {
      g_zone_hits[9]++;
      DIFF(5, 0x2ab4, FAR_CDECL, LO(pts[9]), HI(pts[9]), 0x201, 0);
    }
  }
}
static void diff_zones(const bool *have, const uint32_t *pts) {
  {
    for (unsigned z = 1; z < 15; z++) {
      if (!have[z]) continue;
      uint32_t pt = pts[z];
      uint16_t msgs[2] = {0x201, 0x204};
      for (unsigned m = 0; m < 2; m++) {
        if (m == 1 && !(z == 9 || z == 11 || CH(1, 4))) continue;
        g_zone_hits[z]++;
        DIFF(5, 0x2ab4, FAR_CDECL, LO(pt), HI(pt), msgs[m], 0);
      }
    }
  }
}


/* ---- 5:0x04e8 the information panel ---- */

static void info_setup(void) {
  /* fonts, colours, brushes the panel uses */
  DS_W(0x292e) = GetStockObject(13);
  DS_W(0x2930) = GetStockObject(12);
  DS_W(0x2938) = GetStockObject(16);
  ds_w32(0x2870, 0x00112233u);
  ds_w32(0x253a, 0x00c0c0c0u);
  for (unsigned i = 0; i < 6; i++) ds_w32((uint16_t)(0x418 + 4 * i), 0x00102030u * (i + 1));
  for (unsigned i = 0; i < 20; i++) DS_W((uint16_t)(0x5742 + 2 * i)) = GetStockObject((int)(i % 6 == 3 ? 5 : i % 5));
  DS_W(0x565a) = 0;
  DS_W(0x26ae) = 0;
  DS_W(0x5a54) = DS_W(0x4c74);
  DS_W(0x4174) = DS_W(0x4c74);
  DS_W(0x54f8) = GetStockObject(15);
  DS_W(0x0082) = 100;
  DS_B(0x0792) = (uint8_t)rb(256);
  /* the planet positions (x, y) sorted by x, read by 33:0x01a0 (the planet count is DS:0x007a) */
  for (unsigned i = 0; i < 8; i++) {
    DS_W((uint16_t)(0x31ba + 4 * i)) = (uint16_t)(100 + 50 * i);
    DS_W((uint16_t)(0x31ba + 4 * i + 2)) = (uint16_t)(60 * i);
  }
  static const uint16_t caps[] = {300, 450, 500, 700, 1000, 2500, 5000, 7500, 10000, 20000, 30000};
  DS_W(0x04c8) = caps[rb(11)];
}


/* More variety for the information panel: picture numbers, relations, kinds of map objects, fleet orders. */
static void info_extra(void) {
  for (unsigned q = 0; q < 4; q++) {
    DS_B((uint16_t)(0x5a6a + q * 0xc0)) = (uint8_t)rb(256);
    for (unsigned r = 0; r < 16; r++) DS_B((uint16_t)(0x5ad4 + q * 0xc0 + r)) = (uint8_t)rb(3);
    if (CH(2, 3)) DS_B((uint16_t)(0x5a64 + q * 0xc0 + 0x4c)) = (uint8_t)(CH(1, 2) ? 3 : 5); /* the type 3 sees through relations */
  }
  /* map objects: all kinds, random bytes */
  unsigned nm = DS_W(0x1892);
  for (unsigned i = 0; i < nm; i++) {
    uint8_t *o = mo_at(i);
    uint32_t key;
    memcpy(&key, o + 2, 4);
    static const uint8_t kinds[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 7};
    uint8_t kind = kinds[rb(sizeof kinds)];
    o[1] = (uint8_t)((kind << 5) | (rb(5) << 1));
    o[6] = (uint8_t)rb(256);
    o[7] = (uint8_t)rb(256);
    if (kind == 0) { o[8] = (uint8_t)rb(256); o[9] = (uint8_t)(rb(256) & 0x0f); o[7] &= 0x0f; o[6] = (uint8_t)rb(256); } /* the dword +6 is < 2^28 */
    *(uint16_t *)(o + 8) = (uint16_t)rb(0x10000);
    *(uint16_t *)(o + 0xa) = (uint16_t)rb(0x10000);
    if (kind == 0) { o[9] &= 0x0f; if (CH(1, 3)) { o[8] = 0; o[9] = 0; o[7] = 0; o[6] = (uint8_t)rb(8); } } /* the dword at +6 stays below 2^28: its square root is taken */
    o[0xc] = (uint8_t)(kind == 2 ? 0x40 + rb(nm) : rb(3));
    o[0xd] = (uint8_t)(kind == 2 ? 0 : rb(2));
    if (kind == 2) *(uint16_t *)(o + 0xa) = (uint16_t)(CH(1, 2) ? 0xffff : 0);   /* visible to every player or none */
  }
  for (unsigned i = 0; i < nm; i++) { /* the object a kind 2 object refers to (word +0xc): another one's first word */
    uint8_t *o = mo_at(i);
    if ((o[1] & 0xe0) == 0x40 && CH(2, 3)) memcpy(o + 0xc, mo_at(rb(nm)), 2);
  }
  /* fleets: kinds 7 (orders), 4 and others, with the flags byte at +0x76 */
  unsigned nf = DS_W(0x553a);
  for (unsigned i = 0; i < nf && i < 7; i++) {
    uint32_t fp = MK_FP(HI(g_fl_blk), (uint16_t)(i * FL_SIZE));
    wr8f(fp, 4, CH(1, 2) ? 7 : (CH(1, 3) ? 4 : (uint8_t)rb(9)));
    wr8f(fp, 0x76, (uint8_t)(CH(1, 2) ? 0x10 | (CH(1, 3) ? 0 : rb(16)) : rb(256)));
    wr8f(fp, 1, (uint8_t)(rb(16) << 1));          /* the "player" of the id word */
    wr32f(fp, 0x2c, rb(100000));
    for (unsigned k = 0; k < 16; k++) wr16f(fp, 0x0c + 2 * k, CH(1, 3) ? (uint16_t)(1 + rb(300)) : 0);
    wr16f(fp, 0x62, (uint16_t)rb(4));
    uint8_t *wp = (uint8_t *)LIN(g_wp5[i]);
    for (unsigned k = 0; k < 0x40; k++) wp[k] = (uint8_t)rb(256);
    wr8f(g_wp5[i], 0xa, (uint8_t)(CH(1, 2) ? 3 : rb(256)));
    wr8f(g_wp5[i], 0x1c, (uint8_t)(CH(1, 3) ? 0xb0 | rb(16) : rb(256)));
    {
      /* designs of the owner with a cost slot (category 0x10) for 16:0x1ca2 */
      uint16_t ow = *(uint16_t *)((uint8_t *)LIN(fp) + 2) & 3;
      uint32_t dsg = g_dtab5[ow];
      for (unsigned k = 0; k < 16; k++) {
        uint32_t dk = MK_FP(HI(dsg), (uint16_t)(0x8d * k));
        wr8f(dk, 0x7a, 2);
        wr16f(dk, 0x3e, 0x10);
        wr8f(dk, 0x40, (uint8_t)rb(16));
        wr8f(dk, 0x41, (uint8_t)(1 + rb(3)));
      }
    }
  }
  /* planets */
  for (unsigned i = 0; i < DS_W(0x4d96); i++) {
    uint8_t *r = (uint8_t *)LIN(g_pl) + 0x38 * i;
    r[4] = (uint8_t)(CH(1, 3) ? 7 : rb(8));
    *(uint16_t *)(r + 0x32) = (uint16_t)(CH(1, 4) ? 100 : CH(1, 2) ? 100 - rb(5) : 100 - rb(20));   /* the turn of the last information */
    *(uint16_t *)(r + 0x12) = (uint16_t)(CH(1, 4) ? 0 : rb(0x10000));
    if (CH(2, 3)) { /* an environment inside the range of the current player: a habitable planet */
      uint16_t cur = DS_W(0x014c) & 3;
      for (unsigned k = 0; k < 3; k++) {
        int mid = (int8_t)DS_B((uint16_t)(0x5a74 + cur * 0xc0 + k)), lo = (int8_t)DS_B((uint16_t)(0x5a77 + cur * 0xc0 + k));
        r[0xc + k] = (uint8_t)(mid >= 0 ? mid + (int)rb(5) - 2 : rb(100));
        (void)lo;
      }
    }
    r[0x2c] = (uint8_t)rb(256);
  }
  if (CH(1, 12)) { DS_W(0x4d96) = 0; DS_W(0x007a) = 0; }     /* no planet table: 7:0x0358 fails */
  if (CH(1, 3)) { /* sixteen planet positions close together for 33:0x01a0 (the planet table itself keeps eight) */
    for (unsigned i = 0; i < nm; i++) { /* the map objects of kind 0 sit in the cluster with a big radius */
      uint8_t *o = mo_at(i);
      if ((o[1] & 0xe0) == 0) { *(uint16_t *)(o + 2) = 100; *(uint16_t *)(o + 4) = 100; o[6] = 0xe8; o[7] = 0x03; o[8] = 0; o[9] = 0; }
    }
    for (unsigned i = 0; i < 16; i++) {
      DS_W((uint16_t)(0x31ba + 4 * i)) = (uint16_t)(100 + i);
      DS_W((uint16_t)(0x31ba + 4 * i + 2)) = (uint16_t)(100 + i);
    }
    DS_W(0x007a) = 16;
  }
  for (unsigned i = 0; i < DS_W(0x4d96); i++) { /* extreme environments */
    uint8_t *r = (uint8_t *)LIN(g_pl) + 0x38 * i;
    if (CH(1, 4)) for (unsigned k = 0; k < 3; k++) r[0xc + k] = (uint8_t)(CH(1, 2) ? 0 : 99);
  }
}

static void t_04e8(void) {
  uint16_t dc = g_dc;
  g_noneg_cur = true;
  unsigned iters = 300;
  if (getenv("SEG05_ITERS")) iters = (unsigned)atoi(getenv("SEG05_ITERS"));
  for (unsigned it = 0; it < iters; it++) {
    world_random();
    info_setup();
    info_extra();
    setrect(SCR, 10, 10, (int16_t)((it % 4 == 0) ? 165 + rb(35) : 260 + rb(80)), (int16_t)(CH(1, 5) ? 100 + rb(40) : 220 + rb(60)));
    DIFF(5, 0x04e8, FAR_CDECL, dc, SCR);
  }
  g_noneg_cur = false;
}

/* ---- 5:0x0000 MINEWNDPROC ---- */

static uint32_t g_undo5;
static void t_0000(void) {
  g_noneg_cur = true;
  unsigned iters = 100;
  if (getenv("SEG05_ITERS")) iters = (unsigned)atoi(getenv("SEG05_ITERS"));
  if (!g_undo5) g_undo5 = gblock(0x8000);
  DS_W(0x5442) = 0x1111; /* distinct cursor handles: zone 9 vs other */
  DS_W(0x4ac8) = 0x2222;
  static const uint16_t msgs[] = {0x0001, 0x000f, 0x0014, 0x0019, 0x0020, 0x0111, 0x0201, 0x0203, 0x0204, 0x0202, 0x0005, 0x0002, 0x0000, 0x0113, 0x0021, 0x0050};
  for (unsigned it = 0; it < iters; it++) {
    world_random();
    info_setup();
    info_extra();
    DS_W(0x0154) = g_win2;
    memcpy(LINN(SCR + 0xc0), "SEG05TEST", 10);
    DS_W(0x07c0) = SCR + 0xc0;
    DS_W(0x54f4) = 0x1234;
    DS_W(0x0bbe) = (uint16_t)(CH(1, 4) ? 1 : 0);
    ds_w32(0x096c, g_undo5);
    DS_W(0x0970) = (uint16_t)rb(0x100);
    DS_W(0x0974) = (uint16_t)(CH(1, 4) ? 1 : 0);
    DS_W(0x0972) = 0;
    setrect(SCR, 10, 10, 300, 200);
    {
      static const char *const titles[] = {"", "Mine field 1", "A very long title that cannot fit the window width at all, so it is cut", "x"};
      const char *tt = titles[rb(4)];
      memcpy(LINN(0x2816), tt, strlen(tt) + 1);
    }
    for (unsigned m = 0; m < sizeof msgs / sizeof msgs[0]; m++) {
      uint16_t msg = msgs[m];
      uint16_t wp = 0;
      uint32_t lp = rnd();
      if (msg == 0x0014 || msg == 0x0019) wp = g_dc;
      if (msg == 0x0111) {
        wp = (uint16_t)rb(4);
        lp = ((uint32_t)(CH(3, 4) ? 0 : rb(3)) << 16) | (CH(2, 3) ? DS_W(0x573c) : (uint16_t)rb(100));
      }
      if (msg == 0x0020) { /* the cursor in or out of the client area, in the zones */
        int16_t x = (int16_t)(rb(330) - 15), y = (int16_t)(rb(230) - 15);
        if (CH(1, 3)) { x = (int16_t)(270 + rb(25)); y = (int16_t)(6 + rb(20)); } /* the strip of zone 9 */
        rt_input_mouse(x, y, 0, 1000 + it);
      }
      if (msg == 0x0201 || msg == 0x0203 || msg == 0x0204) {
        /* a click in a zone of the panel: found with the ORIGINAL hit test */
        for (unsigned k = 0; k < 40; k++) {
          int16_t x = (int16_t)(rb(310) - 5), y = (int16_t)(rb(210) - 5);
          if (zone_of(g_win2, x, y) != 0) { lp = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x; break; }
        }
      }
      DIFF(5, 0x0000, FAR_PASCAL, g_win2, msg, wp, HI(lp), LO(lp));
    }
  }
  g_noneg_cur = false;
}

int main(void) {
  g_only = getenv("SEG05_ONLY");
  if (getenv("SEG05_LIMIT")) g_limit = (unsigned)atoi(getenv("SEG05_LIMIT"));
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
  register_class("SEG05TEST", USER_CB_DEFWNDPROC);
  doubles_init();
  gdi_setup();
  if (want("0474")) t_0474();
  if (want("344a")) t_344a();
  if (want("34fc")) t_34fc();
  if (want("36a8")) t_36a8();
  if (want("2840")) t_2840();
  if (want("0324")) t_0324();
  if (want("329a")) t_329a();
  if (want("3a72")) t_3a72();
  if (want("3774")) t_3774();
  if (want("2ab4")) t_2ab4();
  if (want("04e8")) t_04e8();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
