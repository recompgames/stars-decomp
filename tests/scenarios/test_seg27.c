/* Diff tests for decompiled segment 27 (src/seg27.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 27 is the research and tech-browser user interface:
 *   27:0x0000  RESEARCHDLG: the research dialog procedure (percent scroll, current / next field)
 *   27:0x06ba  paint the information panel of the research dialog
 *   27:0x134c  a click on the research panel: moves the research percent by 1 or 10
 *   27:0x1546  the research cost of one tech level
 *   27:0x1604  BROWSERDLG: the tech browser dialog procedure
 *   27:0x1c68  BROWSERWNDPROC: the window procedure of the browser's item panel
 *   27:0x1e06  paint one tech item in the browser panel
 *   27:0x492e  the resources a given research percent would spend
 *   27:0x49ee  the research cost of raising the tech levels to a target
 *   27:0x4af6  has the current player not yet got a special item
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered Win16 API call log, which here also holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. A double returns the next value queued for it
 * (a queue per callee kept in the arena, so a snapshot restore rewinds it) and hashes, at every call,
 * the whole data segment below the stack (see stub_handler), so a call made before or after a write it
 * should follow (or precede) shows as a difference in the hash log, which DIFF compares as arena
 * memory. The doubles of each function are listed in its notes in docs/functions.csv, with their
 * blind spots. Run as ORIGINAL code in both runs (no double): 28:0x21ee and 28:0x2230 (player
 * attributes and traits), 12:0x0568 and 12:0x05be (typed arrays), 5:0x3a72 (planet minerals). The
 * decompiled functions of other segments (2:0x50f8, 26:0x29da, 2:0x0000, 2:0x507c) run for real:
 * the ORIGINAL code in one run, the C functions in the other.
 *
 * SEG27_ONLY=<tags> runs only the scenarios whose tag appears in the string (4af6 1546 492e 49ee
 * 1c68 134c 06ba 0000 1604 1e06). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

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

/* A deterministic generator for the scenarios (xorshift32). */
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
static uint32_t fstr(const char *str) {
  uint32_t p = gblock((uint32_t)strlen(str) + 1);
  memcpy(LIN(p), str, strlen(str) + 1);
  return p;
}
static void register_test_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG27TEST");
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG27TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
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
  uint8_t nargs;  /* argument words */
  int8_t dcpoke;  /* argument word index of a near pointer (SS) whose second word the double sets to g_dc, -1 none */
  int8_t poke;    /* argument word index of a near pointer (SS) to 8 bytes the double fills from the queued value, -1 none */
} StubCfg;
enum { S_9_4DE8, S_23_0756, S_7_46FC, S_8_9AEA, S_8_9B74, S_24_091E, S_31_61B0, S_9_6372, S_10_7A92, S_10_2D28, S_8_AA4E, S_NSTUB };
static uint16_t g_dc; /* the screen DC the press double hands back */
static const StubCfg g_stubs[S_NSTUB] = {
    /* Rule j2 justification: 9:0x4de8 (a planet's resources: x87 code over the population, mines and
     * factories), 23:0x0756 (builds the items of a production queue) and 7:0x46fc (grows a planet copy) are
     * not decompiled yet and need a complete game world; 26:0x29da, which 27:0x492e calls, reaches them
     * only through its own simulation. Blind spot: only the calls (arguments, order against the other
     * calls and writes) are checked here, not what the three functions do. */
    [S_9_4DE8] = {"9:4de8", 9, 0x4de8, 3, -1, -1},
    [S_23_0756] = {"23:0756", 23, 0x0756, 9, -1, -1},
    [S_7_46FC] = {"7:46fc", 7, 0x46fc, 3, -1, -1},
    /* Rule j2 justification: 8:0x9aea / 8:0x9b74 are the mouse-button auto-repeat of the arrow buttons (they poll
     * the mouse and the timer), 24:0x091e is the information pop-up (a window of its own), 31:0x61b0 refreshes the
     * main window and 9:0x6372 queues a message for the host: none is decompiled yet and each needs the whole
     * game running. Blind spot: only the calls (arguments, order against the other calls and writes) are checked,
     * not what they do; 8:0x9aea writes the DC into the second word of the press state it is handed (the value
     * the double uses is the DC the scenario created). */
    [S_8_9AEA] = {"8:9aea", 8, 0x9aea, 9, 0, -1},
    [S_8_9B74] = {"8:9b74", 8, 0x9b74, 1, -1, -1},
    [S_24_091E] = {"24:091e", 24, 0x091e, 3, -1, -1},
    [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, -1, -1},
    [S_9_6372] = {"9:6372", 9, 0x6372, 3, -1, -1},
    /* Rule j2 justification: 10:0x7a92 (the cost of an item for a player: it walks the design and technology
     * tables), 10:0x2d28 (draws a hull picture from the hull's sprite sheet) and 8:0xaa4e (stretches a picture
     * bitmap into the panel through GlobalLock and StretchDIBits) are not decompiled and need the game's resources
     * and tables. Blind spot: only the calls (arguments, order against the drawing calls and writes) are checked;
     * the double of 10:0x7a92 fills the four cost words from its queued value. */
    [S_10_7A92] = {"10:7a92", 10, 0x7a92, 3, -1, 2},
    [S_10_2D28] = {"10:2d28", 10, 0x2d28, 11, -1, -1},
    [S_8_AA4E] = {"8:aa4e", 8, 0xaa4e, 12, -1, -1},
};

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
/* The double hashes the whole data segment below the stack at every call (the stack is excluded from
 * the comparison anyway), so a plain memory write moved across the call shows. */
static int g_dump_armed;
static unsigned g_dump_n;
static void stub_handler(HCall *c) {
  uint8_t *blk = LIN(g_stub_blk);
  if (g_dump_armed) { /* debugging aid: SEG27_DUMP=<prefix> writes the data segment at each call of a double */
    char name[300];
    snprintf(name, sizeof name, "%s.%u", getenv("SEG27_DUMP"), g_dump_n++);
    FILE *f = fopen(name, "wb");
    if (f) {
      fwrite(LINN(0), 1, harness_stack_limit(), f);
      fclose(f);
    }
  }
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  uint32_t h = 2166136261u;
  h = fnv(h, LINN(0), harness_stack_limit());
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
  if (g_stubs[c->ordinal].poke >= 0) {
    uint16_t p = hcall_word(c, (unsigned)g_stubs[c->ordinal].poke);
    uint16_t v[4] = {(uint16_t)c->ret, (uint16_t)(c->ret >> 16), (uint16_t)(c->ret + (c->ret >> 16)), (uint16_t)(c->ret ^ (c->ret >> 16))};
    if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), v, 8);
  }
  if (g_stubs[c->ordinal].dcpoke >= 0) {
    uint16_t p = hcall_word(c, (unsigned)g_stubs[c->ordinal].dcpoke);
    if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, (uint16_t)(p + 2))), &g_dc, 2);
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

/* ---- the world: the player rows ---- */

#define PLAYER_ROW(p) ((uint16_t)(0x5a64 + 0xc0 * (p)))
#define ROW_LEVEL(p, k) DS_B((uint16_t)(PLAYER_ROW(p) + 0x1a + (k)))
#define ROW_SPENT(p, k) ((uint16_t)(PLAYER_ROW(p) + 0x20 + 4 * (k)))
#define ROW_PERCENT(p) DS_B((uint16_t)(PLAYER_ROW(p) + 0x38))
#define ROW_FIELDS(p) DS_B((uint16_t)(PLAYER_ROW(p) + 0x39))
#define ROW_SPECIAL(p) DS_W((uint16_t)(PLAYER_ROW(p) + 0x52))

/* All 16 player rows random, with the tech levels in a plausible range, the field cost modifiers
 * (attribute bytes +0x46 to +0x4b) around 1 and the trait dword random. */
static void world_rows(void) {
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN(PLAYER_ROW(p));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    for (unsigned k = 0; k < 6; k++) {
      ROW_LEVEL(p, k) = (uint8_t)(chance(1, 8) ? rn(27) : rn(12));
      row[0x46 + k] = (uint8_t)(chance(1, 3) ? 1 : chance(1, 2) ? 0 : chance(1, 2) ? 2 : (uint8_t)(rn(5) - 2));
      uint32_t spent = chance(1, 3) ? 0 : chance(1, 2) ? rn(2000) : rn(0x7fffffff);
      memcpy(row + 0x20 + 4 * k, &spent, 4);
    }
    row[0x38] = (uint8_t)(chance(1, 4) ? 0 : chance(1, 4) ? 100 : rn(120));
  }
  DS_B(0x80) = (uint8_t)((DS_B(0x80) & ~2) | (chance(1, 2) ? 2 : 0));
}

/* ---- 27:0x4af6: has the current player not yet got a special item ---- */

static void t_4af6(void) {
  static const uint16_t kinds[] = {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
                                   0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000, 0x0000, 0x0003, 0x0101, 0xffff};
  /* the twelve special items, then neighbours of them */
  static const struct { uint16_t kind; uint8_t index; } special[] = {
      {0x1000, 4}, {0x0800, 4}, {0x0004, 6}, {0x0008, 9}, {0x0080, 6}, {0x0040, 8},
      {0x0020, 7}, {0x0010, 0x12}, {0x4000, 0x1e}, {0x0001, 8}, {0x8000, 0x0e}, {0x1000, 9}};
  static const uint16_t players[] = {0xffff, 0, 1, 7, 15};
  static const uint16_t words[] = {0x0000, 0xffff, 0x5555, 0xaaaa, 0x0fff, 0xf000};
  const uint16_t req = 0x252a;
  for (unsigned pi = 0; pi < sizeof players / 2; pi++) {
    for (unsigned wi = 0; wi < sizeof words / 2; wi++) {
      g_rng = 0x4af60001u + 0x9e3779b9u * (pi * 16 + wi + 1);
      world_rows();
      DS_W(0x14c) = players[pi];
      for (unsigned p = 0; p < 16; p++) DS_W((uint16_t)(PLAYER_ROW(p) + 0x52)) = words[wi];
      for (unsigned s = 0; s < 12; s++) {
        for (int d = -1; d <= 1; d++) {
          DS_W(req) = special[s].kind;
          DS_B(req + 2) = (uint8_t)(special[s].index + d);
          DS_B(req + 3) = (uint8_t)rnd();
          DIFF(27, 0x4af6, FAR_CDECL, req);
        }
        DS_W(req) = (uint16_t)(special[s].kind ^ 0x0100);
        DS_B(req + 2) = special[s].index;
        DIFF(27, 0x4af6, FAR_CDECL, req);
      }
      for (unsigned k = 0; k < sizeof kinds / 2; k++) {
        for (unsigned t = 0; t < 4; t++) {
          DS_W(req) = kinds[k];
          DS_B(req + 2) = (uint8_t)(t == 0 ? 0 : t == 1 ? 0xff : rn(0x24));
          DIFF(27, 0x4af6, FAR_CDECL, req);
        }
      }
    }
  }
  /* a request in the middle of DGROUP, with another near offset, and one that wraps the segment */
  DS_W(0x6400) = 0x1000;
  DS_B(0x6402) = 9;
  DS_W(0x14c) = 2;
  ROW_SPECIAL(2) = 0x0800;
  DIFF(27, 0x4af6, FAR_CDECL, 0x6400);
  ROW_SPECIAL(2) = 0x0000;
  DIFF(27, 0x4af6, FAR_CDECL, 0x6400);
}

/* ---- 27:0x1546: the research cost of one tech level ---- */

static void t_1546(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x15460001u + 0x9e3779b9u * (it + 1);
    world_rows();
    uint16_t me = (uint16_t)(it % 16);
    DS_W(0x14c) = me;
    uint16_t field = (uint16_t)(it % 6);
    uint16_t level = (uint16_t)(it % 33);
    /* levels past the table read code bytes of this segment as data (the offset wraps at 16 bits) */
    static const uint16_t odd_levels[] = {27, 28, 0x100, 0x1000, 0x7fff, 0x8000, 0xfffe, 0xffff};
    if (it % 50 == 49) level = odd_levels[(it / 50) % 8];
    if (it % 41 == 40) field = (uint16_t)rnd();
    DIFF(27, 0x1546, FAR_CDECL, field, level, me);
  }
  /* every level of every field, for each of the three cost modifiers (below 1, 1, above 1) and both
   * values of the slow-tech flag */
  g_rng = 0x15460777u;
  world_rows();
  for (unsigned flag = 0; flag < 2; flag++) {
    DS_B(0x80) = (uint8_t)((DS_B(0x80) & ~2) | (flag ? 2 : 0));
    for (int8_t mod = -1; mod <= 3; mod++) {
      for (unsigned field = 0; field < 6; field++) {
        ((uint8_t *)LINN(PLAYER_ROW(3)))[0x46 + field] = (uint8_t)mod;
        for (unsigned level = 0; level < 27; level++) DIFF(27, 0x1546, FAR_CDECL, field, level, 3);
      }
    }
  }
  /* a boundary player (0x7fff makes the row offset wrap) */
  DIFF(27, 0x1546, FAR_CDECL, 0, 5, 0x7fff);
  DIFF(27, 0x1546, FAR_CDECL, 5, 0xffff, 1);
}

/* ---- 27:0x492e: the resources a research percent would spend ---- */

#define NPLAN_MAX 6
static uint32_t g_planets; /* a block of planet records (stride 0x38), the array DS:0xb6 points at */

static uint32_t make_queue(unsigned n) {
  const uint16_t w[] = {4, (uint16_t)(n ? n : 1), 0};
  uint32_t q = harness_call(MEM_SEL_SEG(12), 0x0568, HCONV_CDECL, w, 3, NULL);
  CHECK(q != 0);
  uint8_t *p = LIN(q);
  p[3] = (uint8_t)n;
  for (unsigned k = 0; k < n; k++) {
    uint16_t qty = (uint16_t)(chance(1, 4) ? 0x3ff : rn(chance(1, 2) ? 6 : 0x3ff));
    uint16_t cls = chance(1, 2) ? 1 : 2;
    uint16_t item = (uint16_t)(chance(1, 2) ? rn(8) : rn(32));
    uint16_t w0 = (uint16_t)(qty | ((item & 0x3f) << 10));
    uint16_t w1 = (uint16_t)(((item >> 6) & 1) | (cls << 1) | (chance(1, 4) ? (rn(16) << 4) : 0));
    memcpy(p + 4 + 4 * k, &w0, 2);
    memcpy(p + 6 + 4 * k, &w1, 2);
  }
  return q;
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
static void t_492e(void) {
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x492e0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_ui();
    world_rows();
    uint16_t me = (uint16_t)(it % 16);
    DS_W(0x14c) = (it % 25 == 24) ? 0xffff : me;
    unsigned n = it % (NPLAN_MAX + 1);
    DS_W(0x4d96) = (uint16_t)n;
    ds_w32(0x00b6, g_planets);
    DS_W(0x553a) = 0;
    for (unsigned k = 0; k < NPLAN_MAX; k++) {
      uint8_t *pl = LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * k)));
      for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
      uint16_t owner = chance(2, 3) ? me : (uint16_t)rn(16);
      memcpy(pl + 2, &owner, 2);
      pl[5] &= 0xfb;
      for (unsigned i = 0; i < 3; i++) pl[0xc + i] = (uint8_t)(1 + rn(99));
      for (unsigned i = 0; i < 3; i++) pl[0x9 + i] = (uint8_t)(10 + rn(100));
      uint32_t q = chance(1, 3) ? 0 : make_queue(chance(1, 4) ? 0 : 1 + rn(4));
      memcpy(pl + 0x34, &q, 4);
    }
    for (unsigned k = 0; k < 24; k++) {
      stub_push(S_9_4DE8, (uint16_t)(chance(1, 5) ? 0 : chance(1, 4) ? 0x8000 + rn(0x8000) : rn(600)), 0);
      stub_push(S_23_0756, (uint16_t)(chance(1, 3) ? 0 : chance(1, 5) ? 0xffff : rn(40)),
                (uint16_t)(chance(1, 3) ? rn(3) : chance(1, 3) ? rn(7) : 3 + rn(2)));
    }
    DS_W(0x6500) = 0x5555;
    DS_W(0x6502) = 0x5555;
    uint16_t percent = (uint16_t)(it % 7 == 0 ? rnd() : it % 3 == 0 ? 100 : rn(101));
    DIFF(27, 0x492e, FAR_CDECL, percent);
  }
}

/* ---- 27:0x49ee: the research cost of raising the levels to a target ---- */

static void t_49ee(void) {
  uint32_t tgt = gblock(0x40);
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x49ee0001u + 0x9e3779b9u * (it + 1);
    world_rows();
    uint16_t me = (uint16_t)(it % 16);
    DS_W(0x14c) = me;
    for (unsigned k = 0; k < 6; k++) {
      uint8_t cur = ROW_LEVEL(me, k);
      uint8_t t = (uint8_t)(chance(1, 3) ? cur : chance(1, 2) ? cur + rn(8) : rn(27));
      if (it % 7 == 6 && k == it % 6) t = (uint8_t)(chance(1, 2) ? 27 : 0x80 + rn(0x80)); /* above 26, or negative */
      ((uint8_t *)LIN(tgt))[0x10 + k] = t;
    }
    DIFF(27, 0x49ee, FAR_CDECL, LO(tgt) + 0x10, HI(tgt));
  }
  /* each field alone, every target from the level to 26 */
  g_rng = 0x49ee0999u;
  world_rows();
  for (unsigned flag = 0; flag < 2; flag++) {
    DS_B(0x80) = (uint8_t)((DS_B(0x80) & ~2) | (flag ? 2 : 0));
    for (unsigned k = 0; k < 6; k++) {
      for (unsigned t = 0; t < 28; t++) {
        for (unsigned j = 0; j < 6; j++) ((uint8_t *)LIN(tgt))[0x10 + j] = ROW_LEVEL(5, j);
        ((uint8_t *)LIN(tgt))[0x10 + k] = (uint8_t)t;
        DS_W(0x14c) = 5;
        DIFF(27, 0x49ee, FAR_CDECL, LO(tgt) + 0x10, HI(tgt));
      }
    }
  }
}

/* ---- the research panel world ---- */

static const uint16_t kind_masks[16] = {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
                                        0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000};
/* The GDI handles and colours the panels use, and the layout words the research dialog computes at
 * start-up (random but plausible). */
static void world_gdi(void) {
  DS_W(0x0010) = GetStockObject(0);
  DS_W(0x0014) = GetStockObject(0);
  DS_W(0x0016) = GetStockObject(4);
  DS_W(0x0022) = GetStockObject(0);
  DS_W(0x2930) = GetStockObject(16);
  DS_W(0x292e) = GetStockObject(16);
  DS_W(0x4a5c) = GetStockObject(16);
  DS_W(0x59e0) = GetStockObject(16);
  DS_W(0x5752) = GetStockObject(16);
  DS_W(0x54f8) = 0;
  ds_w32(0x253a, 0x00C0C0C0);
  ds_w32(0x2870, 0x00800000);
  ds_w32(0x4ac0, 0x00000000);
  ds_w32(0x4166, 0x00FFFFFF);
  DS_W(0x4ac8) = 0x4321;
}
static void world_panel(void) {
  world_gdi();
  DS_W(0x267a) = (uint16_t)(chance(1, 8) ? 1 + rn(30) : 12 + rn(8));
  DS_W(0x54ee) = (uint16_t)(chance(1, 6) ? rn(40) : 14 + rn(4));
  DS_W(0x5a5e) = (uint16_t)(150 + rn(120));
  DS_W(0x4b6e) = (uint16_t)(20 + rn(100));
  DS_W(0x2888) = (uint16_t)rn(9);
  DS_W(0x0d10) = (uint16_t)(chance(1, 6) ? 0xffff : 100 + rn(150));
  DS_W(0x2672) = (uint16_t)(40 + rn(150));
  DS_W(0x53de) = (uint16_t)(40 + rn(100));
  DS_W(0x2660) = (uint16_t)(60 + rn(100));
  DS_W(0x0d0a) = (uint16_t)(chance(1, 5) ? (chance(1, 2) ? 0 : 100) : rn(101));
  DS_W(0x0c7e) = (uint16_t)(chance(1, 6) ? 1 : 0);
  DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 8 : rn(16));
  for (unsigned k = 0; k < 8; k++) {
    uint16_t w[2] = {kind_masks[rn(16)], (uint16_t)rn(8)};
    memcpy(LINN((uint16_t)(0x4c52 + 4 * k)), w, 4);
  }
  /* the two arrow rectangles (left, top, right, bottom) */
  int16_t x0 = (int16_t)(150 + rn(60)), y0 = (int16_t)(20 + rn(60));
  int16_t up[4] = {x0, y0, (int16_t)(x0 + 12), (int16_t)(y0 + 10)};
  int16_t dn[4] = {x0, (int16_t)(y0 + 12), (int16_t)(x0 + 12), (int16_t)(y0 + 22)};
  memcpy(LINN(0x29d6), up, 8);
  memcpy(LINN(0x29de), dn, 8);
  DS_W(0x2806) = 0;
  /* words and dwords that the screens compute and store: random so that a store made too early or too late shows */
  ds_w32(0x27b6, rnd());
  ds_w32(0x26b2, chance(1, 3) ? 0 : rn(50000));
  DS_W(0x0d3e) = (uint16_t)rn(2);
  DS_W(0x014e) = (uint16_t)rnd();
  DS_W(0x5658) = (uint16_t)rnd();
}
static void world_player_traits(uint16_t me) {
  world_rows();
  DS_W(0x14c) = me;
  uint32_t traits = (uint32_t)rnd();
  if (chance(1, 3)) traits |= 0x10 | 0x1000;
  if (chance(1, 3)) traits &= ~(0x10u | 0x1000u);
  memcpy(LINN((uint16_t)(PLAYER_ROW(me) + 0x4e)), &traits, 4);
}
static void world_planets(unsigned n, uint16_t me) {
  DS_W(0x4d96) = (uint16_t)n;
  ds_w32(0x00b6, g_planets);
  DS_W(0x553a) = 0;
  for (unsigned k = 0; k < NPLAN_MAX; k++) {
    uint8_t *pl = LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * k)));
    for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
    uint16_t owner = chance(2, 3) ? me : (uint16_t)rn(16);
    memcpy(pl + 2, &owner, 2);
    memset(pl + 0x34, 0, 4);
  }
}

/* ---- 27:0x1e06: the item panel of the tech browser ---- */

static uint32_t g_synth; /* a synthetic item record, for the requests 2:0x50f8 does not know */
static const uint8_t kind_item_count[16] = {16, 16, 10, 12, 24, 12, 15, 8, 10, 16, 5, 17, 11, 20, 32, 15};

/* The graphics handles and metrics the panel uses, random but plausible, and a player whose tech levels are all 0,
 * all 26 or random (mode 0, 1, 2). */
static void world_item_panel(uint16_t me, unsigned mode) {
  world_panel();
  world_player_traits(me);
  DS_W(0x2938) = GetStockObject(16);
  DS_W(0x293a) = GetStockObject(16);
  DS_W(0x5752) = GetStockObject(7);
  DS_W(0x59f8) = GetStockObject(7);
  DS_W(0x59e0) = GetStockObject(0);
  DS_W(0x54f8) = 0;
  DS_W(0x288a) = (uint16_t)(30 + rn(30));
  DS_W(0x54ee) = (uint16_t)(chance(1, 5) ? rn(30) : 14);
  DS_W(0x267a) = (uint16_t)(chance(1, 3) ? 10 + rn(4) : chance(1, 2) ? 14 : 15 + rn(4));
  DS_B(0x793) = (uint8_t)(chance(1, 2) ? 0x40 : rn(256));
  for (unsigned k = 0; k < 6; k++) ROW_LEVEL(me, k) = (uint8_t)(mode == 0 ? 0 : mode == 1 ? 26 : rn(27));
  if (chance(1, 3)) ROW_FIELDS(me) = (uint8_t)rnd();
  for (unsigned i = 0; i < 8; i++) DS_B((uint16_t)(0x2808 + i)) = (uint8_t)rnd();
  for (unsigned k = 0; k < 6; k++) {
    stub_push(S_10_7A92, (uint16_t)(chance(1, 3) ? 0 : rn(2000)), (uint16_t)(chance(1, 4) ? rn(100) : 0));
    stub_push(S_10_7A92, (uint16_t)rn(1500), 0);
  }
}
/* A request for (kind, item) as the browser keeps it at DS:0x252a, its record filled by the real 2:0x50f8. */
static void set_request(uint16_t kind, uint16_t item, bool synthetic) {
  DS_W(0x252a) = kind;
  DS_B(0x252c) = (uint8_t)item;
  DS_B(0x252d) = (uint8_t)rnd();
  ds_w32(0x252e, 0);
  if (synthetic) {
    uint8_t *r = LIN(g_synth);
    for (unsigned i = 0; i < 0x100; i++) r[i] = (uint8_t)rnd();
    r[0] = (uint8_t)rn(0x40); /* the hull index that 2:0x507c turns into a record */
    r[1] = 0;
    snprintf((char *)r + 8, 0x20, "Synthetic %u", (unsigned)(rnd() & 0xff));
    for (unsigned i = 2; i < 8; i++) r[i] = (uint8_t)(chance(1, 3) ? 0 : chance(1, 5) ? 0xf0 + rn(8) : rn(30));
    for (unsigned i = 0x34; i < 0x60; i += 2) {
      uint16_t w = (uint16_t)(chance(1, 4) ? 0 : chance(1, 5) ? 0xffff : chance(1, 5) ? 0x8000 + rn(100) : rn(chance(1, 2) ? 200 : 3000));
      memcpy(r + i, &w, 2);
    }
    ds_w32(0x252e, g_synth);
  } else {
    const uint16_t w[1] = {0x252a};
    (void)harness_call(MEM_SEL_SEG(2), 0x50f8, HCONV_CDECL, w, 1, NULL);
  }
}
static void t_1e06(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  g_synth = gblock(0x100);
  g_dc = dc;
  unsigned it = 0;
  for (unsigned mode = 0; mode < 6; mode++) {
    for (unsigned k = 0; k < 16; k++) {
      for (unsigned item = 0; item <= (unsigned)kind_item_count[k] + 1; item++) {
        g_rng = 0x1e060001u + 0x9e3779b9u * (++it);
        stubs_clear_all();
        uint16_t me = (uint16_t)(it % 16);
        world_item_panel(me, mode % 3);
        set_request(kind_masks[k], (uint16_t)item, false);
        uint16_t width = (uint16_t)(0x150 + rn(40)), height = (uint16_t)(0x150 + rn(60));
        if (DIFF(27, 0x1e06, FAR_CDECL, dc, width, height, 0x252a) != DT_PASS)
          fprintf(stderr, "  1e06 iteration %u: kind %x item %u mode %u\n", it, kind_masks[k], item, mode);
      }
    }
  }
  /* records of our own (the item index is past the kind's table, so 2:0x50f8 leaves the pointer alone), every kind */
  for (unsigned rep = 0; rep < 12; rep++) {
    for (unsigned k = 0; k < 16; k++) {
      g_rng = 0x1e06a000u + 0x9e3779b9u * (++it);
      stubs_clear_all();
      uint16_t me = (uint16_t)(it % 16);
      world_item_panel(me, rep % 3);
      set_request(kind_masks[k], (uint16_t)(0x70 + rn(8)), true);
      uint16_t width = (uint16_t)(0x150 + rn(40)), height = (uint16_t)(0x150 + rn(60));
      if (DIFF(27, 0x1e06, FAR_CDECL, dc, width, height, 0x252a) != DT_PASS)
        fprintf(stderr, "  1e06 synthetic iteration %u: kind %x rep %u\n", it, kind_masks[k], rep);
    }
  }
  /* no record at all, and a kind that is not one */
  g_rng = 0x1e06b000u;
  stubs_clear_all();
  world_item_panel(1, 2);
  ds_w32(0x252e, 0);
  DS_W(0x252a) = 0x4000;
  DIFF(27, 0x1e06, FAR_CDECL, dc, 0x150, 0x160, 0x252a);
  set_request(0x0003, 0x70, true);
  DIFF(27, 0x1e06, FAR_CDECL, dc, 0x150, 0x160, 0x252a);
  set_request(0x19ff, 0x70, true);
  DIFF(27, 0x1e06, FAR_CDECL, dc, 0x150, 0x160, 0x252a);
  /* kinds in the gaps of the dispatch (each takes the default way out) */
  static const uint16_t odd_kinds[] = {0x0030, 0x0300, 0x0900, 0x3000, 0xc000, 0x0041, 0x0081, 0x00c0, 0x0600, 0x1800, 0x0000, 0xffff};
  for (unsigned k = 0; k < sizeof odd_kinds / 2; k++) {
    stubs_clear_all();
    set_request(odd_kinds[k], 0x70, true);
    DIFF(27, 0x1e06, FAR_CDECL, dc, 0x150, 0x160, 0x252a);
  }
}

/* ---- 27:0x1c68: BROWSERWNDPROC ---- */

static void t_1c68(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 200, 160, 0, 0);
  g_dc = dc;
  for (unsigned it = 0; it < 400; it++) {
    g_rng = 0x1c680001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_panel();
    world_rows();
    DS_W(0x14c) = (uint16_t)(it % 16);
    uint16_t kind = (it % 3 == 0) ? 0x4000 : (it % 3 == 1) ? 0x0400 : kind_masks[rn(16)];
    DS_W(0x252a) = kind;
    DS_B(0x252c) = (uint8_t)(chance(1, 2) ? rn(5) : rn(40));
    DS_B(0x252d) = (uint8_t)rnd();
    DS_W(0x0c7e) = (uint16_t)(chance(1, 6) ? 1 : 0);
    for (unsigned i = 0; i < 0x40; i++) DS_B((uint16_t)(0x26b6 + i)) = (uint8_t)rnd();
    DS_B(0x2730) = (uint8_t)(chance(1, 5) ? 0 : chance(1, 5) ? 20 : rn(9));
    uint16_t cell = DS_W(0x54ee);
    /* points inside and around the 64 by 64 picture box (x 5 to 0x44, y cell + 5 to cell + 0x44) */
    int16_t xs[] = {4, 5, 6, 0x20, 0x43, 0x44, 0x45, -1, 0x1234};
    int16_t ys[] = {(int16_t)(cell + 4), (int16_t)(cell + 5), (int16_t)(cell + 6), (int16_t)(cell + 0x20),
                    (int16_t)(cell + 0x44), (int16_t)(cell + 0x45), 0, -3};
    int16_t px = xs[rn(9)], py = ys[rn(8)];
    rt_input_mouse((int16_t)(px + 10), (int16_t)(py + 10 + 20), 0, 0);
    uint32_t lp = ((uint32_t)(uint16_t)py << 16) | (uint16_t)px;
    uint16_t msg = (it % 4 == 0) ? 0x201 : (it % 4 == 1) ? 0x20 : (it % 4 == 2) ? 0x201 : 0x20;
    DIFF(27, 0x1c68, FAR_PASCAL, win, msg, dc, HI(lp), LO(lp)); /* PASCAL: lParam high word first */
  }
  /* WM_PAINT: BeginPaint, GetClientRect, the panel painter 27:0x1e06 and EndPaint */
  g_synth = g_synth ? g_synth : gblock(0x100);
  for (unsigned it = 0; it < 60; it++) {
    g_rng = 0x1c68a000u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t me = (uint16_t)(it % 16);
    world_item_panel(me, it % 3);
    unsigned k = it % 16;
    if (kind_masks[k] == 0x0100) k = 12;
    set_request(kind_masks[k], (uint16_t)rn(kind_item_count[k]), false);
    DIFF(27, 0x1c68, FAR_PASCAL, win, 0x000f, 0, 0, 0);
  }
  /* the plain messages */
  world_panel();
  DS_W(0x252a) = 0x400;
  DIFF(27, 0x1c68, FAR_PASCAL, win, 0x0001, 0, 0, 0);
  DIFF(27, 0x1c68, FAR_PASCAL, win, 0x0014, dc, 0, 0);
  DIFF(27, 0x1c68, FAR_PASCAL, 0, 0x0014, 0, 0x1234, 0x5678);
  DIFF(27, 0x1c68, FAR_PASCAL, win, 0x0010, 0, 0, 0);
  DIFF(27, 0x1c68, FAR_PASCAL, win, 0x0111, 5, 6, 7);
  DIFF(27, 0x1c68, FAR_PASCAL, win, 0x0202, 0, 9, 9);
  DestroyWindow(win);
}

/* ---- 27:0x134c: a click on the research panel ---- */

static void t_134c(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 300, 0, 0);
  g_dc = dc;
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x134c0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_panel();
    uint16_t me = (uint16_t)(it % 16);
    world_player_traits(me);
    world_planets(it % 3, me);
    for (unsigned k = 0; k < 6; k++) { /* the "still held" answers of 8:0x9b74, then 0 */
      stub_push(S_8_9B74, 1, 0);
    }
    unsigned held = rn(6);
    for (unsigned k = held; k < 6; k++) stub_push(S_8_9B74, 0, 0);
    int16_t col = (int16_t)DS_W(0x5a5e), top = (int16_t)DS_W(0x4b6e), lh = (int16_t)DS_W(0x267a), it0 = (int16_t)DS_W(0x0d10);
    int16_t cnt = (int16_t)DS_W(0x2888);
    int16_t x, y;
    switch (it % 8) {
      case 0: x = (int16_t)(DS_W(0x29d6) + rn(14) - 1); y = (int16_t)(DS_W(0x29d8) + rn(12) - 1); break; /* up arrow */
      case 1: x = (int16_t)(DS_W(0x29de) + rn(14) - 1); y = (int16_t)(DS_W(0x29e0) + rn(12) - 1); break; /* down arrow */
      case 2: x = (int16_t)(12 + rn(4)); y = (int16_t)(top + rn(cnt * lh + 3)); break;                      /* left edge of the list */
      case 3: x = (int16_t)(col - 14 + rn(4)); y = (int16_t)(top + rn(cnt * lh + 3) - 1); break;           /* right edge */
      case 4: x = (int16_t)(col - 3 + rn(8)); y = (int16_t)(it0 - 2 + rn(3 * lh + 4)); break;              /* the resources band */
      case 5: x = (int16_t)(20 + rn(col)); y = (int16_t)(top + rn(cnt * lh + 2)); break;                   /* inside the list */
      case 6: x = (int16_t)(col + 1 + rn(40)); y = (int16_t)(it0 + (3 * lh) / 2 - 1 + rn(3)); break;       /* the middle of the band */
      default: x = (int16_t)rn(400); y = (int16_t)rn(400); break;
    }
    uint16_t mods = (uint16_t)(chance(1, 2) ? 4 : (chance(1, 2) ? 0 : rnd()));
    DIFF(27, 0x134c, FAR_CDECL, win, (uint16_t)x, (uint16_t)y, mods);
  }
  /* the line height is what indexes the list: line 0 .. 7 */
  world_panel();
  world_player_traits(0);
  world_planets(0, 0);
  DS_W(0x267a) = 14;
  DS_W(0x4b6e) = 40;
  DS_W(0x2888) = 8;
  DS_W(0x5a5e) = 200;
  for (unsigned line = 0; line < 8; line++)
    for (unsigned dy = 0; dy < 14; dy += 13) DIFF(27, 0x134c, FAR_CDECL, win, 20, 40 + 14 * line + dy, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 13, 40, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 12, 40, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 187, 40, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 188, 40, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 20, 39, 0);
  DIFF(27, 0x134c, FAR_CDECL, win, 20, 40 + 14 * 8, 0);
  DestroyWindow(win);
}

/* ---- 27:0x06ba: paint the information panel of the research dialog ---- */

static uint16_t make_research_dialog(void);
/* One randomized world for 06ba; returns the "what" argument. With trap set the world is the one whose turns
 * line divides by zero: budget -1 (all bits set) and trait 4 of the player, which halves the budget to 0. */
static uint16_t world_06ba(unsigned it, uint16_t dlg, bool trap) {
  static const uint16_t whats[] = {0xffff, 0x0004, 0x4200, 0x0000, 0x0001, 0x0002, 0x0003, 0x0005, 0x0007, 0x000f, 0x0100, 0x0200, 0x0300,
                                   0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000, 0x0f00, 0x4204, 0x4203, 0x4400, 0x7f03, 0x7f00, 0x7f04};
  g_rng = 0x06ba0001u + 0x9e3779b9u * (it + 1);
  stubs_clear_all();
  world_panel();
  uint16_t me = (uint16_t)(it % 16);
  world_player_traits(me);
  if (trap) {
    uint32_t traits;
    memcpy(&traits, LINN((uint16_t)(PLAYER_ROW(me) + 0x4e)), 4);
    traits |= 0x10;
    memcpy(LINN((uint16_t)(PLAYER_ROW(me) + 0x4e)), &traits, 4);
  }
  for (unsigned k = 0; k < 6; k++) ROW_LEVEL(me, k) = (uint8_t)(chance(1, 6) ? 26 + rn(2) : chance(1, 5) ? 0 : rn(27));
  ROW_FIELDS(me) = (uint8_t)(rnd());
  uint32_t spent = chance(1, 3) ? rn(100) : rn(200000);
  memcpy(LINN((uint16_t)(PLAYER_ROW(me) + 0x3a)), &spent, 4);
  DS_W(0x51b2) = (uint16_t)(chance(1, 8) ? rn(8) : rn(6));
  DS_W(0x267a) = (uint16_t)(8 + rn(12));
  DS_W(0x5a5e) = (uint16_t)(140 + rn(60));
  DS_W(0x2672) = (uint16_t)(60 + rn(80));
  DS_W(0x53de) = (uint16_t)(30 + rn(60));
  DS_W(0x54ee) = 14;
  uint32_t budget = chance(1, 6) ? 0 : chance(1, 4) ? rn(60) : rn(40000);
  if (trap) budget = 0xffffffffu;
  ds_w32(0x26b2, budget);
  ds_w32(0x27b6, rn(100000));
  DS_W(0x4d96) = 0;
  DS_W(0x14c) = me;
  for (unsigned id = 0x431; id <= 0x436; id++) CheckDlgButton(dlg, (int16_t)id, 0);
  /* the near rect the client passes (right at +4, bottom at +6) */
  int16_t crect[4] = {0, 0, (int16_t)(DS_W(0x5a5e) + 140), (int16_t)(200 + rn(200))};
  memcpy(LINN(0x6300), crect, 8);
  return (it < sizeof whats / 2) ? whats[it] : (uint16_t)((chance(1, 3) ? rnd() : whats[rn(sizeof whats / 2)]));
}

static void t_06ba(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t dlg = make_research_dialog();
  uint16_t combo = GetDlgItem(dlg, 0x43b);
  for (unsigned k = 0; k < 8; k++) (void)SendMessage(combo, 0x403, 0, fstr("item"));
  g_dc = dc;
  for (unsigned it = 0; it < 700; it++) {
    uint16_t what = world_06ba(it, dlg, false);
    DIFF(27, 0x06ba, FAR_CDECL, dlg, (it % 3 == 0) ? 0 : dc, 0x6300, what);
  }
  /* The original divides the turns by the (halved) budget with IDIV and traps (#DE) when that is 0. The C stops with
   * abort() (CONVENTIONS rule b): a child process runs the C function on the trap world and must die of SIGABRT. */
  fflush(NULL);
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    rt_set_mode(RT_MODE_DECOMP);
    for (unsigned it = 0; it < 400; it++) {
      uint16_t what = world_06ba(it, dlg, true);
      const uint16_t args[4] = {dlg, dc, 0x6300, what};
      (void)rt_call_far(MK_FP(SEG_SEL(27), 0x06ba), FAR_CDECL, args, 4);
    }
    _exit(0); /* never reached the division: the test failed */
  }
  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid);
  CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  DestroyWindow(dlg);
}

/* ---- 27:0x1604: BROWSERDLG ---- */

/* The browser dialog: the check box 0x10a, the combo box 0x10b, the buttons 0x42e, 0x42f and 2, with the
 * window class "starsbrowser" registered (the game registers it with BROWSERWNDPROC; the item panel the
 * procedure creates is a plain window here). */
static uint16_t g_browser_class_ok;
static void register_browser_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = MK_FP(MEM_SEL_DGROUP, 0x01dc); /* the class name string of the game's data segment */
  w->hInstance = 0;
  g_browser_class_ok = RegisterClass(wc) != 0;
  CHECK(g_browser_class_ok);
}
static uint16_t make_browser_dialog(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 420, 320, 0, 0);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE | 3, 5, 5, 100, 18, dlg, 0x10a);
  mkctl("COMBOBOX", WS_CHILD | WS_VISIBLE | 3, 5, 30, 100, 100, dlg, 0x10b);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE, 5, 60, 70, 20, dlg, 0x42e);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE, 80, 60, 70, 20, dlg, 0x42f);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE, 160, 60, 70, 20, dlg, 2);
  return dlg;
}
static void t_1604(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t dlg = make_browser_dialog();
  uint16_t combo = GetDlgItem(dlg, 0x10b);
  for (unsigned k = 0; k < 17; k++) (void)SendMessage(combo, 0x403, 0, fstr("kind"));
  g_dc = dc;
  uint16_t panel = mkwin(WS_CHILD | WS_VISIBLE, 0, 100, 100, 100, dlg, 0x777);
  for (unsigned it = 0; it < 700; it++) {
    g_rng = 0x16040001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_panel();
    uint16_t me = (uint16_t)(it % 16);
    world_player_traits(me);
    DS_W(0x5658) = panel;
    DS_W(0x14e) = (uint16_t)(chance(1, 2) ? 0 : rnd());
    DS_W(0x0d3e) = (uint16_t)(chance(1, 2) ? 0 : 1);
    DS_W(0x2806) = (uint16_t)(chance(1, 3) ? 0 : rnd());
    DS_W(0x54f4) = 0;
    DS_W(0x54ee) = (uint16_t)(14 + rn(3));
    DS_W(0x267a) = (uint16_t)(chance(1, 2) ? 12 + rn(2) : 15 + rn(5));
    DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 8 : rn(16));
    DS_W(0x252a) = kind_masks[rn(16)];
    DS_B(0x252c) = (uint8_t)(chance(1, 2) ? rn(6) : rn(256));
    DS_B(0x252d) = (uint8_t)rnd();
    ds_w32(0x252e, rnd());
    int sel = (int)rn(19) - 1; /* -1 (CB_ERR) to 17 */
    (void)SendMessage(combo, 0x40e, (uint16_t)sel, 0);
    CheckDlgButton(dlg, 0x10a, (uint16_t)(chance(1, 2) ? 1 : 0));
    DS_W(0x51b2) = (uint16_t)rn(6);
    uint16_t msg, wp = 0, lpl = 0, lph = 0;
    switch (it % 12) {
      case 0: msg = 0x02; break;
      case 1: msg = 0x0f; break;
      case 2: msg = 0x14; wp = dc; break;
      case 3: msg = 0x19; wp = dc; lpl = GetDlgItem(dlg, (int16_t)(0x10a + rn(2))); lph = (uint16_t)rn(8); break;
      case 4: msg = 0x19; wp = dc; lpl = (uint16_t)(0x7000 + rn(100)); lph = (uint16_t)(chance(1, 2) ? 6 : rn(8)); break;
      case 5: msg = 0x110; break;
      case 6: msg = 0x111; wp = 2; lph = (uint16_t)rn(2); break;
      case 7: msg = 0x111; wp = 0x10b; lph = (uint16_t)(chance(2, 3) ? 1 : rn(4)); break;
      case 8: case 9: msg = 0x111; wp = 0x42f; break;
      case 10: msg = 0x111; wp = 0x42e; break;
      default: msg = (uint16_t)(chance(1, 3) ? 0x111 : rn(0x300)); wp = (uint16_t)rnd(); lpl = (uint16_t)rnd(); lph = (uint16_t)rnd(); break;
    }
    if (msg == 0x111 && (wp == 0x42e || wp == 0x42f)) {
      /* a request that exists (2:0x50f8 answers it) in some runs: the item byte small */
      DS_B(0x252c) = (uint8_t)(chance(2, 3) ? rn(12) : chance(1, 2) ? 0 : 0xff);
      if (chance(1, 3)) DS_B(0x252c) = (uint8_t)(0x62 + rn(6));
      (void)SendMessage(combo, 0x40e, (uint16_t)(chance(1, 3) ? 0 : rn(17)), 0);
    }
    if (DIFF(27, 0x1604, FAR_PASCAL, dlg, msg, wp, lph, lpl) != DT_PASS)
      fprintf(stderr, "  1604 iteration %u: msg %x wParam %x lParam %x:%x\n", it, msg, wp, lph, lpl);
  }
  /* the stepping at the ends: a few kinds (a table kind, one that is not a kind), items around the ends of
   * a kind and of the byte (0x64 and 0x65 are where stepping back into a kind starts), both buttons, the
   * combo entry "all kinds" and a single kind, the check box both ways */
  g_rng = 0x16040bb1u;
  world_panel();
  world_player_traits(1);
  DS_W(0x5658) = panel;
  DS_W(0x14e) = 0;
  DS_W(0x0d3e) = 0;
  DS_W(0x2806) = 0;
  static const uint16_t step_kinds[] = {0x0008, 0x4000, 0x0020, 0x0003, 0x19ff, 0x8000};
  static const uint8_t step_items[] = {0x00, 0x01, 0x64, 0x65, 0x70, 0xff};
  for (unsigned ki = 0; ki < sizeof step_kinds / 2; ki++)
    for (unsigned ii = 0; ii < sizeof step_items; ii++)
      for (unsigned b = 0; b < 2; b++)
        for (unsigned se = 0; se < 3; se++)
          for (unsigned ck = 0; ck < 2; ck++) {
            DS_W(0x252a) = step_kinds[ki];
            DS_B(0x252c) = step_items[ii];
            ds_w32(0x252e, 0x12345678);
            (void)SendMessage(combo, 0x40e, (uint16_t)(se == 0 ? 0 : se == 1 ? 1 : 5), 0);
            CheckDlgButton(dlg, 0x10a, (uint16_t)ck);
            DIFF(27, 0x1604, FAR_PASCAL, dlg, 0x111, b ? 0x42f : 0x42e, 0, 0);
          }
  /* a new kind picked in the combo box (notification 1), each entry, the check box both ways */
  for (unsigned k = 0; k < 3; k++) (void)SendMessage(combo, 0x403, 0, fstr("extra")); /* entries 17 to 19 index past the kind table: kind 0, no such item */
  for (unsigned entry = 0; entry < 20; entry++)
    for (unsigned ck = 0; ck < 2; ck++) {
      DS_W(0x252a) = 0x0008;
      DS_B(0x252c) = 3;
      (void)SendMessage(combo, 0x40e, (uint16_t)entry, 0);
      CheckDlgButton(dlg, 0x10a, (uint16_t)ck);
      DIFF(27, 0x1604, FAR_PASCAL, dlg, 0x111, 0x10b, 1, 0);
    }
  DestroyWindow(dlg);
}

/* ---- 27:0x0000: RESEARCHDLG ---- */

/* The dialog window with the controls the procedure addresses: the six radio buttons (0x431 to 0x436), the
 * "next field" combo box (0x43b) and the buttons 2 (close) and 0x76 (help). */
static uint16_t make_research_dialog(void) {
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 420, 320, 0, 0);
  for (unsigned id = 0x431; id <= 0x436; id++) mkctl("BUTTON", WS_CHILD | WS_VISIBLE | 4, 5, (int)(20 * (id - 0x430)), 100, 18, dlg, (uint16_t)id);
  mkctl("COMBOBOX", WS_CHILD | WS_VISIBLE | 3, 5, 150, 100, 100, dlg, 0x43b);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE, 200, 200, 70, 20, dlg, 2);
  mkctl("BUTTON", WS_CHILD | WS_VISIBLE, 280, 200, 70, 20, dlg, 0x76);
  return dlg;
}
static void t_0000(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t dlg = make_research_dialog();
  uint16_t combo = GetDlgItem(dlg, 0x43b);
  g_dc = dc;
  for (unsigned it = 0; it < 450; it++) {
    g_rng = 0x00000a01u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_panel();
    uint16_t me = (uint16_t)(it % 16);
    world_player_traits(me);
    world_planets(it % 3, me);
    DS_W(0x51b2) = (uint16_t)rn(7);
    for (unsigned k = 0; k < 8; k++) (void)SendMessage(combo, 0x403, 0, fstr("item"));
    (void)SendMessage(combo, 0x40e, (uint16_t)rn(9), 0);
    for (unsigned id = 0x431; id <= 0x436; id++) CheckDlgButton(dlg, (int16_t)id, (uint16_t)(chance(1, 3) ? 1 : 0));
    DS_B(0x53f0) = (uint8_t)rnd();
    uint16_t msg, wp = 0, lpl = 0, lph = 0;
    switch (it % 15) {
      case 0: msg = 0x14; wp = dc; break;                                     /* WM_ERASEBKGND */
      case 1: msg = 0x0f; break;                                              /* WM_PAINT */
      case 2: msg = 0x19; wp = dc; lpl = GetDlgItem(dlg, (int16_t)(0x431 + rn(6))); lph = (uint16_t)rn(8); break; /* WM_CTLCOLOR */
      case 3: msg = 0x19; wp = dc; lpl = (uint16_t)(0x7000 + rn(100)); lph = (uint16_t)(chance(1, 2) ? 6 : rn(8)); break;
      case 4: case 5: case 6: { /* WM_SETCURSOR: the cursor over the list, the band or elsewhere */
        msg = 0x20; wp = (uint16_t)(chance(1, 3) ? 2 : chance(1, 3) ? 0x76 : 0x431 + rn(8)); lpl = (uint16_t)rn(8); lph = (uint16_t)(chance(2, 3) ? 0 : rn(3));
        int16_t col = (int16_t)DS_W(0x5a5e), top = (int16_t)DS_W(0x4b6e), lh = (int16_t)DS_W(0x267a), it0 = (int16_t)DS_W(0x0d10);
        int16_t cx = (int16_t)(20 + rn(col)), cy = (int16_t)(top + rn(DS_W(0x2888) * lh + 3));
        if (it % 15 == 5) { cx = (int16_t)(col + 1 + rn(40)); cy = (int16_t)(it0 - 1 + rn(2 * lh + 3)); }
        if (it % 15 == 6) { cx = (int16_t)rn(300); cy = (int16_t)rn(300); }
        rt_input_mouse((int16_t)(cx + 10 + 2), (int16_t)(cy + 10 + 20), 0, 0);
        break;
      }
      case 7: msg = 0x110; break;                                             /* WM_INITDIALOG */
      case 8: msg = 0x111; wp = (uint16_t)(0x431 + rn(6)); lpl = 0; lph = 0; break;        /* a radio button */
      case 9: msg = 0x111; wp = (uint16_t)(0x431 + rn(6)); lph = (uint16_t)(1 + rn(3)); break; /* a notification */
      case 10: msg = 0x111; wp = 2; lph = (uint16_t)(chance(1, 3) ? 1 : 0); break;          /* close */
      case 11: msg = 0x111; wp = 0x76; break;                                  /* help */
      case 12: msg = 0x111; wp = (uint16_t)(chance(1, 2) ? 0x430 : 0x437 + rn(4)); break;  /* another id */
      case 13: msg = (uint16_t)(chance(1, 2) ? 0x201 : 0x203); wp = (uint16_t)rn(8); lpl = (uint16_t)(150 + rn(60)); lph = (uint16_t)(20 + rn(60)); break;
      default: msg = (uint16_t)(chance(1, 2) ? 0x200 : 0x202 + rn(2)); break;
    }
    if (msg == 0x111 && wp == 2) {
      /* the stored record must differ from the dialog's state in some of the runs */
      if (chance(1, 2)) { uint8_t b = ROW_FIELDS(me); DS_W(0x51b2) = (uint16_t)(b & 0xf); }
      if (chance(1, 2)) DS_W(0x0d0a) = (uint16_t)(int16_t)(int8_t)ROW_PERCENT(me);
      (void)SendMessage(combo, 0x40e, chance(1, 2) ? (uint16_t)(((int8_t)ROW_FIELDS(me) >> 4) == 6 ? 0 : ((int8_t)ROW_FIELDS(me) >> 4) + 1) : (uint16_t)rn(9), 0);
    }
    g_dump_armed = getenv("SEG27_DUMP") && getenv("SEG27_DUMP_IT") && (unsigned)atoi(getenv("SEG27_DUMP_IT")) == it;
    if (DIFF(27, 0x0000, FAR_PASCAL, dlg, msg, wp, lph, lpl) != DT_PASS) fprintf(stderr, "  0000 iteration %u: msg %x wParam %x lParam %x:%x\n", it, msg, wp, lph, lpl);
  }
  /* the radio ids in turn, with the button checked and not */
  g_rng = 0x00000b77u;
  world_panel();
  world_player_traits(2);
  for (unsigned id = 0x42f; id <= 0x438; id++) {
    for (unsigned chk = 0; chk < 2; chk++) {
      CheckDlgButton(dlg, (int16_t)id, (uint16_t)chk);
      DIFF(27, 0x0000, FAR_PASCAL, dlg, 0x111, id, 0, 0);
    }
  }
  /* a window handle of 0 and the unrelated messages */
  DIFF(27, 0x0000, FAR_PASCAL, 0, 0x14, dc, 0, 0);
  DIFF(27, 0x0000, FAR_PASCAL, dlg, 0x0010, 0, 0, 0);
  DIFF(27, 0x0000, FAR_PASCAL, dlg, 0x0113, 1, 2, 3);
  DestroyWindow(dlg);
}

int main(void) {
  g_only = getenv("SEG27_ONLY");
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
  register_browser_class();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* 8:0x9482 allocates a font record with LocalAlloc */
  stubs_init();
  g_planets = gblock(0x38 * NPLAN_MAX + 0x40);
  if (want("4af6")) t_4af6();
  if (want("1546")) t_1546();
  if (want("492e")) t_492e();
  if (want("49ee")) t_49ee();
  if (want("06ba")) t_06ba();
  if (want("1e06")) t_1e06();
  if (want("1c68")) t_1c68();
  if (want("134c")) t_134c();
  if (want("0000")) t_0000();
  if (want("1604")) t_1604();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
