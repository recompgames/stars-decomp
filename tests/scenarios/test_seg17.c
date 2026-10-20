/* Diff tests for decompiled segment 17 (src/seg17.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 17 is the second half of the computer players' code (the "AI" turn of the older
 * players; the callers pick one of 17:0x0462, 17:0x234e, 19:0x0400, 19:0x1594, ... by the type
 * bits of the player's record):
 *   17:0x0000  run one computer player's turn: allocate the work tables, call the turn function
 *   17:0x0462  the turn of the type-0 player
 *   17:0x1730  queue the messages of the techs the type-0 player has reached
 *   17:0x1b70  the same for the type-0 player's second tech list
 *   17:0x20f4  does a fleet hold enough ships of the designs 1 to 4 (like 19:0x03a0)
 *   17:0x2154  a filter callback: does the record at the second far pointer belong to someone else
 *   17:0x216c, 0x2252, 0x3c30, 0x3c82  scoring callbacks over planets and fleets
 *   17:0x234e  the turn of the type-1 player
 *   17:0x38b0  queue the messages of the techs the type-1 player has reached
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered API call log, which here holds the test doubles' calls).
 *
 * Run as ORIGINAL code in both runs (no double): 8:0x8730 (the random generator), 8:0xbc50 (the
 * squared distance of two planet coordinates), 28:0x21ee (a player attribute). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
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
static int g_iter = -1; /* SEG17_ITER: run only this iteration of the big scenarios (with SEG17_TRACE) */
#define ITER_LOG(it) do { if (getenv("SEG17_LOG")) fprintf(stderr, "it %u\n", (unsigned)(it)); } while (0)
#define ITER_SKIP(it) if (g_iter >= 0 && (int)(it) != g_iter) continue
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void dsw(uint16_t off, uint16_t v) { memcpy(LINN(off), &v, 2); }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }
__attribute__((unused)) static uint8_t rb_(uint32_t fp, uint16_t off) { return *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))); }
static uint16_t rw(uint32_t fp, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 2);
  return v;
}
static uint32_t rd32_far(uint32_t fp, uint16_t off) { return (uint32_t)rw(fp, off) | ((uint32_t)rw(fp, (uint16_t)(off + 2)) << 16); }
static void ww(uint32_t fp, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 2); }
__attribute__((unused)) static void wd(uint32_t fp, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 4); }
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
static void fill_rand(uint32_t fp, uint16_t n) {
  uint8_t *p = LIN(fp);
  for (uint16_t i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

/* ---- the scenario world of the scoring callbacks ---- */

static uint32_t g_info;   /* DS:0x1600: 16 bytes per planet id */
static uint32_t g_rec;    /* records of (id word, owner word) the callbacks are called with */
static uint32_t g_fl;     /* a scratch fleet record block */
#define NREC 8

static void world_cb(void) {
  /* the state words of the random generator 8:0x8730 reads (DS:0x151e, DS:0x1522): non-zero, so that it draws */
  ds_w32(0x151e, 1 + rn(0x7fffff00u));
  ds_w32(0x1522, 1 + rn(0x7fffff00u));
  fill_rand(g_info, 64 * 16);
  ds_w32(0x1600, g_info);
  /* the info bytes the callbacks read: +1, +3, +0xa; small values and values with bit 7 */
  for (unsigned i = 0; i < 64; i++) {
    uint8_t v;
    switch (rn(6)) {
      case 0: v = 0; break;
      case 1: v = 1; break;
      case 2: v = (uint8_t)(0x80 | rn(0x80)); break;
      case 3: v = (uint8_t)(0xf8 + rn(8)); break;
      default: v = (uint8_t)rn(0x40); break;
    }
    wb(g_info, (uint16_t)(16 * i + 0xa), v);
    wb(g_info, (uint16_t)(16 * i + 1), (uint8_t)(chance(1, 4) ? 0 : (chance(1, 2) ? 1 + rn(0x7f) : 0x80 | rn(0x80))));
    wb(g_info, (uint16_t)(16 * i + 3), (uint8_t)(chance(1, 3) ? 0 : 1 + rn(3)));
  }
  /* planet coordinates: x and y words at DS:0x31ba + 4 * id; mostly near each other, sometimes far */
  static const uint16_t scale[] = {20, 60, 120, 180, 250, 400, 700, 2000};
  uint16_t cx = (uint16_t)rn(1000), cy = (uint16_t)rn(1000);
  for (unsigned i = 0; i < 64; i++) {
    uint16_t s = scale[rn(8)];
    uint16_t x = (uint16_t)(cx + rn(2u * s + 1) - s), y = (uint16_t)(cy + rn(2u * s + 1) - s);
    if (chance(1, 12)) x = (uint16_t)rnd(), y = (uint16_t)rnd();
    DS_W((uint16_t)(0x31ba + 4 * i)) = x;
    DS_W((uint16_t)(0x31ba + 4 * i + 2)) = y;
  }
  /* the player records (flags at +7 and +0x4c are read, 0xc0 bytes each) */
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    row[0x4c] = (uint8_t)(chance(1, 3) ? 8 : rn(16));
  }
  for (unsigned i = 0; i < NREC; i++) {
    ww(g_rec, (uint16_t)(4 * i), (uint16_t)rn(64));
    ww(g_rec, (uint16_t)(4 * i + 2), (uint16_t)rn(16));
  }
  DS_W(0x14c) = (uint16_t)rn(16);
}
static void rec_args(unsigned a, unsigned b, uint16_t out[4]) {
  out[0] = LO(g_rec) + (uint16_t)(4 * a);
  out[1] = HI(g_rec);
  out[2] = LO(g_rec) + (uint16_t)(4 * b);
  out[3] = HI(g_rec);
}

/* ---- 17:0x20f4: does the fleet hold enough of the designs 1 to 4 ---- */

static void t_20f4(void) {
  static const uint16_t w[] = {0, 1, 2, 3, 0x7fff, 0x8000, 0xffff, 0x4000, 0x00fe, 0x0100, 0xff00};
  static const int16_t modes[] = {-32768, 1, 2, 3, 32767};
  static const uint8_t thr[] = {0, 1, 5, 0x80, 0xff};
  uint32_t fl = g_fl;
  for (unsigned t = 0; t < sizeof thr; t++) {
    DS_B(0x5340) = thr[t];
    for (unsigned k = 0; k < 30; k++) {
      uint16_t v[6];
      for (unsigned j = 0; j < 6; j++) v[j] = w[rn(sizeof w / 2)];
      if (k < 8) {
        for (unsigned j = 0; j < 6; j++) v[j] = (uint16_t)((k >> (j % 3)) & 1);
      }
      for (unsigned j = 0; j < 4; j++) ww(fl, (uint16_t)(0x10 + 2 * j), v[j]);
      ww(fl, 0x18, v[4]);
      ww(fl, 0x1a, v[5]);
      for (unsigned m = 0; m < sizeof modes / 2; m++)
        DIFF(17, 0x20f4, FAR_CDECL, LO(fl), HI(fl), (uint16_t)modes[m]);
    }
  }
}

/* ---- 17:0x2154: is the record the owner's? ---- */

static void t_2154(void) {
  for (unsigned it = 0; it < 40; it++) {
    g_rng = 0x21540001u + 0x9e3779b9u * (it + 1);
    uint16_t me = (uint16_t)rn(16);
    DS_W(0x14c) = me;
    ww(g_rec, 2, chance(1, 2) ? me : (uint16_t)rnd());
    DIFF(17, 0x2154, FAR_CDECL, (uint16_t)rnd(), (uint16_t)rnd(), LO(g_rec), HI(g_rec));
    ww(g_rec, 6, chance(1, 2) ? me : (uint16_t)rnd());
    DIFF(17, 0x2154, FAR_CDECL, (uint16_t)rnd(), (uint16_t)rnd(), (uint16_t)(LO(g_rec) + 4), HI(g_rec));
  }
}

/* ---- the planet-pair scoring callbacks 17:0x216c, 17:0x2252, 17:0x3c30, 17:0x3c82 ---- */

/* The squared-distance boundary of dist_bonus (src/seg17.c), shared by 17:0x216c and 17:0x2252. Both read
 * the planet ids from the first word of the records a and b, and the coordinates (x, y words) at
 * DS:0x31ba + 4 * id; 8:0xbc50 returns dx*dx + dy*dy. The thresholds are d*d for d in 50, 100, 150, 200, 300,
 * 500. For each T = d*d and S in T-1, T, T+1 the pairs (dx, dy) with dx*dx + dy*dy == S exactly are searched
 * (S = d*d - 1 is not a sum of two squares for every d: then the largest S below d*d that is one is
 * tested instead, searched downward, e.g. 2498 = 47^2 + 17^2 for 2500, so a value strictly below every threshold is
 * always covered); up to 3 pairs per S (the first,
 * a middle one, the last), each with the four sign combinations. The info byte of b's id is 0x10 (no bit 7,
 * so the result is the byte plus the bonus and 8:0x8730 is not asked); for 17:0x2252 the owner's player
 * record has bit 1 of +7 clear. The squared distance is asserted in the test. */
static void dist_boundary(uint16_t fn, const char *name) {
  static const int32_t ds[] = {50, 100, 150, 200, 300, 500};
  unsigned cases = 0, skipped = 0;
  for (unsigned t = 0; t < 6; t++) {
    for (int32_t k = -1; k <= 1; k++) {
      int32_t S = ds[t] * ds[t] + k;
      int32_t sol[64][2];
      unsigned n = 0;
      for (;;) {
        n = 0;
        for (int32_t dx = 0; dx * dx <= S && n < 64; dx++) {
          int32_t r = S - dx * dx, dy = 0;
          while (dy * dy < r) dy++;
          if (dy * dy == r && dy <= dx) { sol[n][0] = dx; sol[n][1] = dy; n++; }
        }
        if (n != 0 || k != -1) break;
        /* T-1 is not reachable: test the largest S < T that is, so a value below T is always covered */
        skipped++;
        fprintf(stderr, "%s: S=%d is not a sum of two squares, using a smaller one\n", name, (int)S);
        S--;
      }
      if (n == 0) { skipped++; fprintf(stderr, "%s: S=%d is not a sum of two squares, skipped\n", name, (int)S); continue; }
      unsigned pick[3] = {0, n / 2, n - 1};
      for (unsigned pi = 0; pi < 3; pi++) {
        if (pi > 0 && pick[pi] == pick[pi - 1]) continue;
        for (unsigned sg = 0; sg < 4; sg++) {
          int32_t dx = sol[pick[pi]][0], dy = sol[pick[pi]][1];
          CHECK(dx * dx + dy * dy == S);
          if (sg & 1) dx = -dx;
          if (sg & 2) dy = -dy;
          if ((t + pi) & 1) { int32_t tmp = dx; dx = dy; dy = tmp; } /* also the x/y roles swapped */
          g_rng = 0xd1570001u + 0x9e3779b9u * (cases + 1) + fn;
          world_cb();
          unsigned ia = rn(32), ib = 32 + rn(32);
          ww(g_rec, 0, (uint16_t)ia);
          ww(g_rec, 2, (uint16_t)rn(16));
          ww(g_rec, 4, (uint16_t)ib);
          unsigned owner = rn(16);
          ww(g_rec, 6, (uint16_t)owner);
          DS_B((uint16_t)(0x5a6b + 0xc0 * owner)) &= (uint8_t)~2u;
          wb(g_info, (uint16_t)(16 * ib + 0xa), 0x10);
          uint16_t x0 = (uint16_t)(1000 + rn(1000)), y0 = (uint16_t)(1000 + rn(1000));
          DS_W((uint16_t)(0x31ba + 4 * ia)) = x0;
          DS_W((uint16_t)(0x31ba + 4 * ia + 2)) = y0;
          DS_W((uint16_t)(0x31ba + 4 * ib)) = (uint16_t)(x0 + dx);
          DS_W((uint16_t)(0x31ba + 4 * ib + 2)) = (uint16_t)(y0 + dy);
          uint16_t a[4];
          rec_args(0, 1, a);
          DIFF(17, fn, FAR_CDECL, a[0], a[1], a[2], a[3]);
          cases++;
        }
      }
    }
  }
  fprintf(stderr, "%s: %u boundary cases, %u unreachable T-1 replaced by a smaller sum\n", name, cases, skipped);
}

static void t_216c(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x216c0001u + 0x9e3779b9u * (it + 1);
    world_cb();
    uint16_t a[4];
    unsigned x = rn(NREC), y = rn(NREC);
    if (it % 9 == 0) y = x;  /* the same record: score 0 */
    rec_args(x, y, a);
    if (it % 11 == 0) a[2] = a[0]; /* the same pointer: score 0 */
    DIFF(17, 0x216c, FAR_CDECL, a[0], a[1], a[2], a[3]);
  }
  dist_boundary(0x216c, "216c");
}
static void t_2252(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x22520001u + 0x9e3779b9u * (it + 1);
    world_cb();
    uint16_t a[4];
    unsigned x = rn(NREC), y = rn(NREC);
    if (it % 9 == 0) y = x;
    rec_args(x, y, a);
    DIFF(17, 0x2252, FAR_CDECL, a[0], a[1], a[2], a[3]);
  }
  dist_boundary(0x2252, "2252");
}
static void t_3c30(void) {
  /* many iterations, and info byte +1 of b's id with bit 7 set in two thirds of them: the boundary
   * draws 0x18 and 0x19 (1% each) must be hit often enough to pin the signed compare against 0x19. */
  for (unsigned it = 0; it < 6000; it++) {
    g_rng = 0x3c300001u + 0x9e3779b9u * (it + 1);
    world_cb();
    uint16_t a[4];
    unsigned x = rn(NREC), y = rn(NREC);
    if (it % 9 == 0) y = x;
    rec_args(x, y, a);
    if (it % 3 != 0) wb(g_info, (uint16_t)(16 * rw(g_rec, (uint16_t)(4 * y))) + 1, (uint8_t)(0x80 | rn(0x80)));
    DIFF(17, 0x3c30, FAR_CDECL, a[0], a[1], a[2], a[3]);
  }
}
static void t_3c82(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x3c820001u + 0x9e3779b9u * (it + 1);
    world_cb();
    uint16_t a[4];
    unsigned x = rn(NREC), y = rn(NREC);
    if (it % 9 == 0) y = x;
    rec_args(x, y, a);
    /* the callback wants info byte +0xa == 1 for the id of the second record */
    if (it % 3 != 0) wb(g_info, (uint16_t)(16 * rw(g_rec, (uint16_t)(4 * y))) + 0xa, 1);
    DIFF(17, 0x3c82, FAR_CDECL, a[0], a[1], a[2], a[3]);
  }
}


/* ---- the test doubles (CONVENTIONS rule j2) ----
 *
 * Logged thunks, installed over the first 5 bytes of a callee in the code segment image so that the
 * same bytes serve the ORIGINAL run and the C version's CALL_ORIG; the functions under test are never
 * doubled. A double returns the next queued value (a queue per callee, kept in the stub block in the
 * arena, so the snapshot rewinds it between the two runs), optionally writes queued bytes to the
 * buffer a pointer argument names (the out-buffers), and at every call hashes the state the call
 * could observe: all of DGROUP's static data, the far regions the scenario registered (planets,
 * fleets, the info array, the table and list structs, ...), the fleet record the call names and the
 * stack buffers it is handed. The hashes are compared as arena memory, so a call made before or
 * after a write it should follow (or precede) shows as a difference. */

#define STUB_BLKSZ 0xc000
#define STUB_DEF 0x10     /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200   /* queue length of id (word) */
#define STUB_QIDX 0x280   /* queue index of id (word) */
#define STUB_Q 0x400      /* queue of id: STUB_QMAX entries of 20 bytes (ax, dx, 16 poke bytes) */
#define STUB_QENT 20
#define STUB_QMAX 24
#define STUB_LOG0 0xa000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs;     /* argument words */
  int8_t farw;       /* argument word index of a far pointer to a record whose 0x7c bytes are hashed, -1 none */
  int8_t poke;       /* argument word index of a near pointer (SS) the queued bytes are written to, -1 none */
  int8_t ssw;        /* argument word index of a near pointer (SS) whose ssn bytes are hashed, -1 none */
  uint8_t ssn;
  uint8_t pokelen;   /* bytes a queued poke writes */
  uint8_t late;      /* installed only by the scenario that needs it (callees decompiled in this or another segment) */
} StubCfg;

enum {
  S_18_0000, S_18_0630, S_18_0750, S_18_07EE, S_18_0864, S_18_08FE, S_18_098A, S_18_1434, S_18_19C4,
  S_18_2664, S_18_2732, S_18_28FC, S_18_2BA6, S_18_2DB2, S_18_2E4E, S_18_2E90, S_18_3558, S_18_3600,
  S_18_36AA, S_18_3854, S_18_3922, S_18_39F6, S_18_3AD8, S_18_3BB4, S_18_3C64, S_18_4510, S_18_457A,
  S_18_59BA, S_18_5BAE, S_18_5D3C, S_18_5E82, S_9_2A74, S_9_462A, S_9_474C, S_9_4DE8, S_7_0358,
  S_7_153E, S_8_B90E, S_21_1E2A, S_26_0100, S_26_0810,
  S_14_2992, S_14_0508, S_7_09AE, S_18_40BA, S_18_5E0E, S_18_60B2, S_9_7D1E, S_9_80DA, S_20_0000, S_21_0000,
  S_12_0232, S_12_04D0, S_17_0462, S_17_234E, S_19_0400, S_19_1594, S_19_0000, S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    [S_18_0000] = {"18:0000", 18, 0x0000, 4, -1, -1, -1, 0, 0, 0},
    [S_18_0630] = {"18:0630", 18, 0x0630, 2, -1, -1, 0, 0x8d, 0, 0},
    [S_18_0750] = {"18:0750", 18, 0x0750, 6, -1, -1, -1, 0, 0, 0},
    [S_18_07EE] = {"18:07ee", 18, 0x07ee, 3, -1, -1, -1, 0, 0, 0},
    [S_18_0864] = {"18:0864", 18, 0x0864, 3, 0, -1, -1, 0, 0, 0},
    [S_18_08FE] = {"18:08fe", 18, 0x08fe, 4, 0, -1, -1, 0, 0, 0},
    [S_18_098A] = {"18:098a", 18, 0x098a, 3, 0, 2, -1, 0, 4, 0},
    [S_18_1434] = {"18:1434", 18, 0x1434, 7, 0, -1, -1, 0, 0, 0},
    [S_18_19C4] = {"18:19c4", 18, 0x19c4, 4, 0, -1, -1, 0, 0, 0},
    [S_18_2664] = {"18:2664", 18, 0x2664, 4, 0, -1, 2, 8, 0, 0},
    [S_18_2732] = {"18:2732", 18, 0x2732, 4, -1, -1, -1, 0, 0, 0},
    [S_18_28FC] = {"18:28fc", 18, 0x28fc, 5, -1, -1, -1, 0, 0, 0},
    [S_18_2BA6] = {"18:2ba6", 18, 0x2ba6, 1, -1, -1, -1, 0, 0, 0},
    [S_18_2DB2] = {"18:2db2", 18, 0x2db2, 2, 0, -1, -1, 0, 0, 0},
    [S_18_2E4E] = {"18:2e4e", 18, 0x2e4e, 2, 0, -1, -1, 0, 0, 0},
    [S_18_2E90] = {"18:2e90", 18, 0x2e90, 2, 0, -1, -1, 0, 0, 0},
    [S_18_3558] = {"18:3558", 18, 0x3558, 3, 0, 2, -1, 0, 16, 0},
    [S_18_3600] = {"18:3600", 18, 0x3600, 3, 0, 2, -1, 0, 16, 0},
    [S_18_36AA] = {"18:36aa", 18, 0x36aa, 1, -1, -1, -1, 0, 0, 0},
    [S_18_3854] = {"18:3854", 18, 0x3854, 4, 0, -1, -1, 0, 0, 0},
    [S_18_3922] = {"18:3922", 18, 0x3922, 4, 0, -1, -1, 0, 0, 0},
    [S_18_39F6] = {"18:39f6", 18, 0x39f6, 4, 0, -1, -1, 0, 0, 0},
    [S_18_3AD8] = {"18:3ad8", 18, 0x3ad8, 4, 0, -1, -1, 0, 0, 0},
    [S_18_3BB4] = {"18:3bb4", 18, 0x3bb4, 3, 0, -1, -1, 0, 0, 0},
    [S_18_3C64] = {"18:3c64", 18, 0x3c64, 8, 0, -1, -1, 0, 0, 0},
    [S_18_4510] = {"18:4510", 18, 0x4510, 3, 0, -1, -1, 0, 0, 0},
    [S_18_457A] = {"18:457a", 18, 0x457a, 3, 0, -1, -1, 0, 0, 0},
    [S_18_59BA] = {"18:59ba", 18, 0x59ba, 5, -1, -1, -1, 0, 0, 0},
    [S_18_5BAE] = {"18:5bae", 18, 0x5bae, 1, -1, -1, -1, 0, 0, 0},
    [S_18_5D3C] = {"18:5d3c", 18, 0x5d3c, 5, -1, 3, 4, 16, 2, 0},
    [S_18_5E82] = {"18:5e82", 18, 0x5e82, 8, 0, -1, -1, 0, 0, 0},
    [S_9_2A74] = {"9:2a74", 9, 0x2a74, 2, -1, -1, -1, 0, 0, 0},
    [S_9_462A] = {"9:462a", 9, 0x462a, 3, 0, -1, -1, 0, 0, 0},
    [S_9_474C] = {"9:474c", 9, 0x474c, 3, 0, -1, -1, 0, 0, 0},
    [S_9_4DE8] = {"9:4de8", 9, 0x4de8, 3, 0, -1, -1, 0, 0, 0},
    [S_7_0358] = {"7:0358", 7, 0x0358, 2, -1, -1, -1, 0, 0, 0},
    [S_7_153E] = {"7:153e", 7, 0x153e, 2, -1, -1, -1, 0, 0, 0},
    [S_8_B90E] = {"8:b90e", 8, 0xb90e, 1, -1, -1, -1, 0, 0, 0},
    [S_21_1E2A] = {"21:1e2a", 21, 0x1e2a, 0, -1, -1, -1, 0, 0, 0},
    /* Rule j2 justification: 26:0x0100 and 26:0x0810 are decompiled (src/seg26.c, diffed in tests/scenarios/test_seg26.c) but stays a double here. These
     * scenarios build random worlds for the computer players' turn; the real function reads the planet copy at DS:0x4c08, its
     * production queue and the player tables, which no scenario here sets up consistently. Measured with the double removed:
     * the ORIGINAL run faults or the heap blocks the function allocates differ between the runs. Blind spot: only the call
     * (arguments and order against the other calls) is checked here, not what the function does. */
    [S_26_0100] = {"26:0100", 26, 0x0100, 1, -1, -1, -1, 0, 0, 0},
    [S_26_0810] = {"26:0810", 26, 0x0810, 1, -1, -1, -1, 0, 0, 0},
    [S_14_2992] = {"14:2992", 14, 0x2992, 0, -1, -1, -1, 0, 0, 0},
    [S_14_0508] = {"14:0508", 14, 0x0508, 2, -1, -1, -1, 0, 0, 0},
    [S_7_09AE] = {"7:09ae", 7, 0x09ae, 0, -1, -1, -1, 0, 0, 0},
    [S_18_40BA] = {"18:40ba", 18, 0x40ba, 0, -1, -1, -1, 0, 0, 0},
    [S_18_5E0E] = {"18:5e0e", 18, 0x5e0e, 0, -1, -1, -1, 0, 0, 0},
    [S_18_60B2] = {"18:60b2", 18, 0x60b2, 0, -1, -1, -1, 0, 0, 0},
    [S_9_7D1E] = {"9:7d1e", 9, 0x7d1e, 2, -1, -1, -1, 0, 0, 0},
    [S_9_80DA] = {"9:80da", 9, 0x80da, 1, -1, -1, -1, 0, 0, 0},
    /* 20:0x0000 is decompiled now (src/seg20.c, diffed in tests/scenarios/test_seg20.c), but it stays a test double
     * here: the callee is the whole turn of a computer player, far too state-hungry to run in this scenario, and its
     * logged call is the expectation (docs/CONVENTIONS.md rule j2). */
    [S_20_0000] = {"20:0000", 20, 0x0000, 1, -1, -1, -1, 0, 0, 0},
    [S_21_0000] = {"21:0000", 21, 0x0000, 1, -1, -1, -1, 0, 0, 0},
    [S_12_0232] = {"12:0232", 12, 0x0232, 2, -1, -1, -1, 0, 0, 1},
    [S_12_04D0] = {"12:04d0", 12, 0x04d0, 3, -1, -1, -1, 0, 0, 1},
    [S_17_0462] = {"17:0462", 17, 0x0462, 1, -1, -1, -1, 0, 0, 1},
    [S_17_234E] = {"17:234e", 17, 0x234e, 1, -1, -1, -1, 0, 0, 1},
    [S_19_0400] = {"19:0400", 19, 0x0400, 1, -1, -1, -1, 0, 0, 1},
    [S_19_1594] = {"19:1594", 19, 0x1594, 1, -1, -1, -1, 0, 0, 1},
    [S_19_0000] = {"19:0000", 19, 0x0000, 1, -1, -1, -1, 0, 0, 1},
};

/* Far regions the doubles hash at every call (planets, fleets, ...), set by the scenario builders. */
#define MAXREG 24
static struct { uint32_t fp; uint16_t len; } g_reg[MAXREG];
static unsigned g_nreg;
static void reg_clear(void) { g_nreg = 0; }
__attribute__((unused)) static void reg_add(uint32_t fp, uint16_t len) {
  if (g_nreg < MAXREG && fp != 0) {
    g_reg[g_nreg].fp = fp;
    g_reg[g_nreg].len = len;
    g_nreg++;
  }
}

static uint64_t fnv64(uint64_t h, const uint8_t *p, size_t n) {
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, p + i, 8);
    h = (h ^ w) * 0x100000001b3ull;
    h ^= h >> 29;
  }
  for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
}
static int g_trace = -1;
static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  if (g_trace < 0) g_trace = getenv("SEG17_TRACE") != NULL;
  uint64_t h = 14695981039346656037ull;
  uint64_t hf = 0, hs = 0, hd, hr[MAXREG] = {0};
  if (cfg->farw >= 0) {
    uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
    if (fp != 0 && mem_check(fp, 0x7c)) hf = fnv64(1, LIN(fp), 0x7c);
  }
  if (cfg->ssw >= 0 && cfg->ssn) {
    uint16_t p = hcall_word(c, (unsigned)cfg->ssw);
    hs = fnv64(1, LIN(MK_FP(c->regs->ss, p)), cfg->ssn);
  }
  hd = fnv64(1, LINN(0), harness_stack_limit());
  for (unsigned i = 0; i < g_nreg; i++)
    if (mem_check(g_reg[i].fp, g_reg[i].len)) hr[i] = fnv64(1, LIN(g_reg[i].fp), g_reg[i].len);
  h = fnv64(h, (const uint8_t *)&hf, 8);
  h = fnv64(h, (const uint8_t *)&hs, 8);
  h = fnv64(h, (const uint8_t *)&hd, 8);
  h = fnv64(h, (const uint8_t *)hr, sizeof(uint64_t) * g_nreg);
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 8 > STUB_BLKSZ) at = STUB_LOG0;
  if (g_trace) {
    fprintf(stderr, "TRACE #%u %s args", (unsigned)(at - STUB_LOG0) / 8, cfg->name);
    for (unsigned i = 0; i < cfg->nargs; i++) fprintf(stderr, " %04x", hcall_word(c, i));
    fprintf(stderr, " | far %016llx ss %016llx ds %016llx reg", (unsigned long long)hf, (unsigned long long)hs, (unsigned long long)hd);
    for (unsigned i = 0; i < g_nreg; i++) fprintf(stderr, " %04llx", (unsigned long long)(hr[i] & 0xffff));
    if (cfg->ssw >= 0 && cfg->ssn && cfg->ssn <= 0x20) {
      fprintf(stderr, " ssbytes");
      const uint8_t *q = LIN(MK_FP(c->regs->ss, hcall_word(c, (unsigned)cfg->ssw)));
      for (unsigned i = 0; i < cfg->ssn; i++) fprintf(stderr, " %02x", q[i]);
    }
    fprintf(stderr, "\n");
    if (getenv("SEG17_DUMP") && (unsigned)atoi(getenv("SEG17_DUMP")) == (unsigned)(at - STUB_LOG0) / 8) {
      uint16_t bp = c->regs->bp;
      fprintf(stderr, "FRAME bp=%04x sp=%04x\n", bp, c->regs->sp);
      if (cfg->farw >= 0) {
        uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
        uint32_t wp;
        memcpy(&wp, (uint8_t *)LIN(fp) + 0x64, 4);
        fprintf(stderr, "FRAME fleet %08x wp %08x +16:", fp, wp);
        for (int j = 0x16; j < 0x28; j++) fprintf(stderr, " %02x", *(uint8_t *)LIN(MK_FP(HI(wp), (uint16_t)(LO(wp) + j))));
        fprintf(stderr, "\n");
      }
      for (int i = -0xa8; i < 0x10; i += 16) {
        fprintf(stderr, "FRAME bp%c%03x:", i < 0 ? '-' : '+', (unsigned)(i < 0 ? -i : i));
        for (int j = 0; j < 16; j++) fprintf(stderr, " %02x", *(uint8_t *)LINN((uint16_t)(bp + i + j)));
        fprintf(stderr, "\n");
      }
    }
  }
  memcpy(blk + at, &h, 8);
  at = (uint16_t)(at + 8);
  memcpy(blk, &at, 2);
  if (qidx < qlen) {
    const uint8_t *e = blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * STUB_QENT + (size_t)qidx * STUB_QENT;
    c->ret = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
    if (cfg->poke >= 0) {
      uint16_t p = hcall_word(c, (unsigned)cfg->poke);
      if (p != 0) memcpy(LIN(MK_FP(c->regs->ss, p)), e + 4, cfg->pokelen);
    }
    if (c->ordinal == S_18_5D3C) {
      /* the real 18:0x5d3c also sets buf[k] = 1 for every k in [arg0, arg1] of the 16-byte buffer
       * at arg4; the double does so for the k whose bit is set in the queued mask (poke bytes 2, 3) */
      uint16_t lo = hcall_word(c, 0), hi = hcall_word(c, 1), bp_ = hcall_word(c, 4), mask = (uint16_t)(e[6] | (e[7] << 8));
      if (bp_ != 0)
        for (unsigned k = lo; k <= hi && k < 16; k++)
          if ((mask >> k) & 1) *(uint8_t *)LIN(MK_FP(c->regs->ss, (uint16_t)(bp_ + k))) = 1;
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
/* Queues one return (dx:ax) and, for a poking double, up to 16 bytes written to the out-buffer. */
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
/* Overwrites the AX of the idx-th queued return of a double. */
__attribute__((unused)) static void stub_set_ax(unsigned id, unsigned idx, uint16_t ax) {
  uint8_t *e = (uint8_t *)LIN(g_stub_blk) + STUB_Q + (size_t)id * STUB_QMAX * STUB_QENT + (size_t)idx * STUB_QENT;
  e[0] = (uint8_t)ax;
  e[1] = (uint8_t)(ax >> 8);
}
/* Overwrites the mask (poke bytes 2 and 3) of the idx-th queued return of the 18:0x5d3c double. */
static void stub_set_mask(unsigned id, unsigned idx, uint16_t mask) {
  uint8_t *e = (uint8_t *)LIN(g_stub_blk) + STUB_Q + (size_t)id * STUB_QMAX * STUB_QENT + (size_t)idx * STUB_QENT;
  e[6] = (uint8_t)mask;
  e[7] = (uint8_t)(mask >> 8);
}
static uint8_t g_stub_saved[S_NSTUB][5];
static bool g_stub_on[S_NSTUB];
static uint32_t g_stub_thunk[S_NSTUB];
static void stub_install(unsigned i) {
  const StubCfg *cfg = &g_stubs[i];
  uint8_t c[5] = {0xEA, (uint8_t)g_stub_thunk[i], (uint8_t)(g_stub_thunk[i] >> 8), (uint8_t)(g_stub_thunk[i] >> 16), (uint8_t)(g_stub_thunk[i] >> 24)};
  uint8_t *code = LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off));
  if (g_stub_on[i]) return;
  memcpy(g_stub_saved[i], code, 5);
  memcpy(code, c, sizeof c);
  g_stub_on[i] = true;
}
static void stub_remove(unsigned i) {
  const StubCfg *cfg = &g_stubs[i];
  if (!g_stub_on[i]) return;
  memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), g_stub_saved[i], 5);
  g_stub_on[i] = false;
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    g_stub_thunk[i] = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(g_stub_thunk[i] != 0);
    if (!cfg->late) stub_install(i);
  }
  stubs_clear_all();
}

/* ---- the scenario world of the tech-message functions 17:0x1730 and 17:0x38b0 ---- */

/* The current player's record (DS:0x5a64 + 0xc0 * player, attribute bytes read signed at +0x1a to +0x1f),
 * the tech flags and the design records (0x8d bytes from DS:0x417a, flag byte at +0x7c with bit 1 as the
 * "has this tech" bit, a turn stamp word at +0x7d) these two functions read. */
#define DSG(k) ((uint16_t)(0x417a + 0x8d * (k)))
static void world_tech(unsigned it) {
  /* the state words of the random generator 8:0x8730 reads (DS:0x151e, DS:0x1522): non-zero, so that it draws */
  ds_w32(0x151e, 1 + rn(0x7fffff00u));
  ds_w32(0x1522, 1 + rn(0x7fffff00u));
  uint16_t me = (uint16_t)rn(16);
  DS_W(0x14c) = me;
  uint16_t turn = (uint16_t)(chance(1, 4) ? rn(40) : chance(1, 2) ? 0x20 + rn(0x100) : rnd());
  DS_W(0x82) = turn;
  uint8_t *pl = LINN((uint16_t)(0x5a64 + 0xc0 * me));
  for (unsigned i = 0; i < 0xc0; i++) pl[i] = (uint8_t)rnd();
  for (unsigned k = 0x1a; k < 0x20; k++) pl[k] = (uint8_t)(it % 11 == 0 ? 0x7f : chance(1, 12) ? 0xf0 + rn(16) : rn(20));
  for (uint16_t a = 0x417a; a < 0x4a4a; a++) DS_B(a) = (uint8_t)rnd();
  for (unsigned k = 0; k < 16; k++) {
    uint8_t f = DS_B((uint16_t)(DSG(k) + 0x7c));
    f = (uint8_t)(chance(3, 4) ? (f | 2) : (f & ~2));
    DS_B((uint16_t)(DSG(k) + 0x7c)) = f;
    dsw((uint16_t)(DSG(k) + 0x7d), (uint16_t)(chance(1, 2) ? turn - rn(30) : rnd()));
  }
  DS_W(0x417a) = (uint16_t)(chance(1, 3) ? 5 : rn(8));
  ds_w32(0x41fd, chance(1, 2) ? 0 : rnd());
  dsw(0x4207, (uint16_t)(chance(1, 3) ? 0xb : rn(16)));
  ds_w32(0x428a, chance(1, 2) ? 0 : rnd());
  ds_w32(0x4317, chance(1, 2) ? 0 : rnd());
  ds_w32(0x4899, chance(1, 2) ? 0 : rnd());
}

/* Random queues for the doubles of the tech-message functions. */
static void stubs_tech(void) {
  stubs_clear_all();
  stub_def(S_18_0000, (uint16_t)(chance(1, 3) ? 1 : 0), 0);
  for (unsigned k = 0; k < 20; k++) stub_push(S_18_0000, (uint16_t)(chance(1, 3) ? 1 : 0), 0, NULL);
}


/* ---- the scenario world of the turn functions: fleets, planets and the tables that point at them ---- */

#define NFL_MAX 8
#define NPL_MAX 8
static uint32_t g_ptrs;       /* DS:0xba: the fleet pointer array */
static uint32_t g_fblk[3];    /* blocks that hold the fleet records */
static uint32_t g_wpb[4];     /* blocks that hold waypoint lists */
static uint32_t g_cur;        /* DS:0x4bf0: the cursor struct */
static uint32_t g_fleet[NFL_MAX];
static uint32_t g_pln;        /* the planet table block */
static uint32_t g_lst;        /* DS:0xcd2: the list struct */
static uint32_t g_tab;        /* DS:0x1604: the table struct */
static uint32_t g_arr;        /* DS:0x1608: array of far pointers to planets (count DS:0x4b62) */
static uint32_t g_pl[NPL_MAX];
static uint16_t g_plid[NPL_MAX];
static unsigned g_nfl, g_npl;

static void set_far(uint16_t off, uint32_t fp) { ds_w32(off, fp); }

/* Lays out nfl fleets: records spread over the three blocks at varied offsets, pointer array, count. */
static void world_fleets(unsigned nfl) {
  CHECK(nfl <= NFL_MAX);
  g_nfl = nfl;
  for (unsigned i = 0; i < NFL_MAX; i++) {
    g_fleet[i] = MK_FP(HI(g_fblk[i % 3]), (uint16_t)(0x10 + 0x80 * (i / 3)));
    memset(LIN(g_fleet[i]), 0, 0x7c);
    memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * i))), &g_fleet[i], 4);
  }
  set_far(0x00ba, g_ptrs);
  DS_W(0x553a) = (uint16_t)nfl;
  set_far(0x1600, g_info);
  set_far(0x4bf0, g_cur);
  fill_rand(g_info, 64 * 16);
  for (unsigned i = 0; i < 64; i++) {
    wb(g_info, (uint16_t)(16 * i + 3), (uint8_t)(chance(1, 2) ? 0 : rn(3)));
    wb(g_info, (uint16_t)(16 * i + 1), (uint8_t)(rn(8)));
  }
  fill_rand(g_cur, 0x40);
  ww(g_cur, 8, (uint16_t)rn(13));
}

static void world_planets(unsigned npl, uint16_t me, bool sparse) {
  CHECK(npl <= NPL_MAX);
  g_npl = npl;
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
    ww(pf, 0x12, (uint16_t)rnd());
    wd(pf, 0x28, chance(1, 3) ? (uint32_t)(0x3e8 + rn(3) - 1) : chance(1, 3) ? 0x5dc + rn(3) - 1 : chance(1, 3) ? 0xc8 + rn(3) - 1 : rnd());
    wd(pf, 8, rnd());
    ww(pf, 0x32, DS_W(0x82));
    for (unsigned j = 0; j < 3; j++) wd(pf, (uint16_t)(0x1c + 4 * j), chance(1, 3) ? 0x1388 + rn(3) - 1 : chance(1, 2) ? rn(0x2000) : rnd());
  }
  if (sparse && npl) {
    uint16_t m = (uint16_t)(g_plid[npl - 1] + 1 + rn(2));
    if (chance(1, 2)) {
      uint16_t q = chance(1, 2) ? 12 : 24;
      m = (uint16_t)(((m + q - 1) / q) * q + (chance(1, 3) ? q : 0));
    }
    DS_W(0x007a) = m;
  }
  /* the array of planet pointers DS:0x1608 (count DS:0x4b62): own and foreign planets, a null end */
  unsigned n = (unsigned)(chance(1, 5) ? 0 : 1 + rn(npl ? npl : 1));
  if (!npl) n = 0;
  for (unsigned k = 0; k < 8; k++) {
    uint32_t p = (k < n) ? g_pl[rn(npl)] : 0;
    memcpy(LIN(MK_FP(HI(g_arr), (uint16_t)(4 * k))), &p, 4);
  }
  set_far(0x1608, g_arr);
  DS_W(0x4b62) = (uint16_t)(chance(1, 6) ? rn(3) : n);
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
    ww(t, base, (npl && chance(3, 4)) ? g_plid[rn(npl)] : (uint16_t)0xffff);
    uint16_t m = (uint16_t)(chance(1, 4) ? 0 : 1 + rn(4));
    ww(t, (uint16_t)(base + 2), m);
    for (unsigned j = 0; j < m; j++) ww(t, (uint16_t)(base + 4 + 2 * j), (uint16_t)(chance(2, 3) ? rn(16) : 0xffff));
  }
}

/* DGROUP: the planet coordinates, the player records, the design records and the scalars the turn
 * functions read. */
static void world_ds(uint16_t me, unsigned it) {
  {
    uint8_t *xy = LINN(0x31ba);
    for (unsigned i = 0; i < 0x100; i++) xy[i] = (uint8_t)rnd();
    for (unsigned p = 0; p < 16; p++) {
      uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
      for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
      row[0x4c] = (uint8_t)(chance(1, 3) ? 8 : rn(16));
      for (unsigned k = 0x1a; k < 0x20; k++) row[k] = (uint8_t)(it % 11 == 0 ? 0x7f : chance(1, 12) ? 0xf0 + rn(16) : rn(20));
      uint16_t pc = (uint16_t)(chance(1, 4) ? rn(8) : rn(120));
      memcpy(row + 2, &pc, 2);
    }
  }
  DS_W(0x14c) = me;
  {
    static const uint16_t edge[] = {0, 1, 4, 5, 9, 0xa, 0xb, 0x13, 0x14, 0x15, 0x1d, 0x1e, 0x1f, 0x31, 0x32, 0x33, 0x3b, 0x3c, 0x3d,
                                    0x4f, 0x50, 0x51, 0x63, 0x64, 0x65, 0x72, 0x73, 0x74, 0x77, 0x78, 0x79, 0x81, 0x82, 0x83, 0xc7, 0xc8, 0xc9};
    DS_W(0x82) = chance(1, 3) ? edge[rn(sizeof edge / 2)] : (uint16_t)(chance(1, 6) ? 0 : chance(1, 9) ? 1000 + rn(60000) : chance(1, 3) ? rn(0x20) : 0x14 + rn(0x100));
  }
  DS_B(0x80) = (uint8_t)(chance(1, 2) ? 0x10 : 0);
  DS_W(0x255c) = 0x1234;
  DS_W(0x4bee) = 0;
  DS_W(0x4c0a) = chance(2, 3) ? me : (uint16_t)(me + 1);
  ds_w32(0x4c30, chance(1, 3) ? 0x32 : chance(1, 2) ? 0xc8 : chance(1, 3) ? 0xbb8 + rn(3) - 1 : chance(1, 3) ? 0x7d0 + rn(3) - 1 : chance(1, 3) ? 0x3e8 + rn(3) - 1 : (uint32_t)(int32_t)-5 + rn(5000));
  DS_W(0x4b92) = chance(1, 2) ? 0xffff : 5;
  DS_B(0x4c0d) = (uint8_t)(chance(1, 2) ? 2 : rn(2));
  dsw(0x4c20, (uint16_t)rnd());
  dsw(0x4c22, (uint16_t)rnd());
  /* the design records, flags and stamps (see world_tech) */
  for (uint16_t a = 0x417a; a < 0x4a4a; a++) DS_B(a) = (uint8_t)rnd();
  for (unsigned k = 0; k < 16; k++) {
    uint8_t f = DS_B((uint16_t)(DSG(k) + 0x7c));
    f = (uint8_t)(chance(3, 4) ? (f | 2) : (f & ~2));
    DS_B((uint16_t)(DSG(k) + 0x7c)) = f;
    dsw((uint16_t)(DSG(k) + 0x7d), (uint16_t)(chance(1, 2) ? DS_W(0x82) - rn(30) : rnd()));
    /* the dword at +0x83 (turn needed) is compared with DS:0x7a / 12 + 8 and the like */
    uint16_t lo = (uint16_t)(chance(1, 2) ? (DS_W(0x007a) / 12 + 8) : (DS_W(0x007a) / 24 + 4)), hi = (uint16_t)(chance(4, 5) ? 0 : rn(2));
    lo = (uint16_t)(lo + rn(3) - 1);
    dsw((uint16_t)(DSG(k) + 0x83), lo);
    dsw((uint16_t)(DSG(k) + 0x85), hi);
    for (unsigned j = 0; j < 4; j++) dsw((uint16_t)(DSG(k) + 0x2a + 2 * j), (uint16_t)(chance(1, 3) ? 0 : chance(2, 3) ? rn(30) : rn(1500)));
  }
  /* the first word of the design records 14 and 15 (DS:0x4930, DS:0x49bd): 0x1d about half the time
   * (17:0x0462 clears the flags k = 14, 15 of the 18:0x5d3c buffer for it when bit 1 of +0x7c is clear) */
  for (unsigned k = 14; k < 16; k++)
    if (chance(1, 2)) dsw(DSG(k), 0x1d);
  DS_W(0x417a) = (uint16_t)(chance(2, 3) ? 5 : rn(8));
  dsw(0x4207, (uint16_t)(chance(3, 4) ? 1 : chance(1, 2) ? 0xb : rn(3)));
  ds_w32(0x428a, chance(1, 3) ? 0 : chance(1, 2) ? rn(4) : rnd());
  ds_w32(0x41fd, chance(1, 3) ? 0 : chance(1, 2) ? rn(40) : rnd());
  ds_w32(0x151e, rnd());
  ds_w32(0x1522, rnd());
  DS_B(0x5340) = (uint8_t)rn(8);
  DS_B(0x5341) = (uint8_t)rn(8);
  DS_B(0x5342) = (uint8_t)rn(8);
  DS_B(0x5343) = (uint8_t)rn(8);
  set_far(0x00ba, g_ptrs);
}

/* Random fleet records. The player's fleets get small counts so that the checks of 17:0x20f4 and the
 * order records vary. */
static void world_rand_fleets(unsigned nfl, uint16_t me, unsigned npl) {
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    fill_rand(f, 0x7c);
    ww(f, 0, (uint16_t)(chance(1, 8) ? 20 : rn(16)));
    ww(f, 2, chance(3, 5) ? me : chance(1, 5) ? 0xffff : (uint16_t)(me + 1 + rn(3)));
    ww(f, 6, (chance(1, 3) || !npl) ? 0xffff : g_plid[rn(npl)]);
    ww(f, 8, rn(2));
    /* sometimes equal to a planet's word +8 (17:0x234e compares them before 18:0x2db2) */
    if (npl && chance(1, 4)) ww(f, 8, rw(g_pl[rn(npl)], 8));
    ww(f, 0xa, rn(2));
    ww(f, 0xc, chance(1, 3) ? 0 : (uint16_t)rn(10));
    ww(f, 0xe, chance(1, 3) ? 0 : (uint16_t)rn(5));
    for (unsigned j = 0; j < 4; j++) ww(f, (uint16_t)(0x10 + 2 * j), chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x1a, chance(1, 2) ? 0 : (uint16_t)rn(5));
    ww(f, 0x1c, chance(1, 2) ? 0 : (uint16_t)rn(5));
    ww(f, 0x1e, (uint16_t)rn(4));
    ww(f, 0x20, (uint16_t)rn(4));
    ww(f, 0x22, (uint16_t)rn(4));
    ww(f, 0x24, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x26, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x28, chance(1, 3) ? 0 : (uint16_t)rn(4));
    ww(f, 0x62, (uint16_t)rn(4));
    wd(f, 0x58, chance(1, 3) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-1 : chance(1, 2) ? rn(500) : rnd() & 0xffff);
    wd(f, 0x5c, chance(1, 3) ? 0 : chance(1, 2) ? 1 + rn(3) : rnd());
    wb(f, 5, (uint8_t)(rnd()));
    wd(f, 0x68, 0);
    if (chance(1, 6)) memset(LIN(MK_FP(HI(f), (uint16_t)(LO(f) + 0x10))), 0, 0x20); /* an empty fleet */
    if (chance(2, 5)) {
      /* an idle fleet: no orders pending */
      ww(f, 0xe, 0);
      ww(f, 0x1a, 0);
      ww(f, 0x1c, 0);
      ww(f, 0x62, (uint16_t)rn(2));
      if (chance(1, 2)) ww(f, 0x26, 0), ww(f, 0x28, 0);
    }
    if (chance(1, 8)) {
      /* a standing fleet of ours: the words +0xc to +0x28 zero except +0x24 (17:0x234e: the arm that
       * tests the waypoint flags +0xa and queues a new order) */
      ww(f, 2, me);
      ww(f, 0x10, 0);
      ww(f, 0x12, 0);
      for (uint16_t o = 0xc; o <= 0x28; o = (uint16_t)(o + 2)) ww(f, o, 0);
      ww(f, 0x24, (uint16_t)(1 + rn(3)));
      ww(f, 0x62, 1);
    } else if (chance(1, 8)) {
      /* a fleet of ours with ships of the designs 1 to 4 and a strong +0x26 / +0x28 (the arm of
       * 17:0x234e that sends it on: 17:0x20f4 true, then the compare with DS:0x5342) */
      ww(f, 2, me);
      for (unsigned j = 0; j < 4; j++) ww(f, (uint16_t)(0x10 + 2 * j), (uint16_t)(1 + rn(3)));
      ww(f, 0x26, (uint16_t)(4 + rn(3)));
      ww(f, 0x28, (uint16_t)rn(3));
    } else if (chance(1, 8)) {
      /* no ships of the designs 1 and 2, +0x26 / +0x28 and +0x14 / +0x16 set (the arm of 17:0x234e
       * that compares them with DS:0x5342 and DS:0x5341) */
      ww(f, 2, me);
      ww(f, 0x10, 0);
      ww(f, 0x12, 0);
      ww(f, 0xe, 0);
      ww(f, 0x1c, 0);
      ww(f, 0x1e, 0);
      ww(f, 0x26, (uint16_t)(1 + rn(4)));
      ww(f, 0x28, (uint16_t)rn(4));
      ww(f, 0x14, (uint16_t)(1 + rn(3)));
      ww(f, 0x16, (uint16_t)rn(3));
      ww(f, 0x62, (uint16_t)rn(2));
    }
    {
      /* the waypoint list (always valid: the game keeps at least one waypoint): a record of 0x40 bytes somewhere in a waypoint block */
      uint32_t w = MK_FP(HI(g_wpb[rn(4)]), (uint16_t)(0x20 + 0x40 * rn(3)));
      fill_rand(w, 0x40);
      wb(w, 0x1d, (uint8_t)(chance(1, 3) ? 1 : chance(1, 3) ? 2 : chance(1, 3) ? 4 : chance(1, 3) ? 8 : rn(16)));
      wb(w, 0x1c, (uint8_t)rn(256));
      wb(w, 0xa, (uint8_t)(chance(1, 2) ? 0 : rn(256)));
      wb(w, 0x1b, (uint8_t)(chance(1, 3) ? 0xf0 | rn(16) : rn(256)));
      ww(w, 0x1a, g_plid[rn(npl)]);
      ww(w, 0x12, (uint16_t)rnd());
      wd(f, 0x64, w);
    }
  }
  if (nfl && chance(1, 10)) memset(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * (nfl - 1)))), 0, 4);
}

static void world_reg(void) {
  reg_clear();
  reg_add(g_ptrs, 0x40);
  for (unsigned i = 0; i < 3; i++) reg_add(g_fblk[i], 0x400);
  for (unsigned i = 0; i < 4; i++) reg_add(g_wpb[i], 0x100);
  reg_add(g_info, 64 * 16);
  reg_add(g_cur, 0x40);
  reg_add(g_pln, 0x240);
  reg_add(g_lst, 0x40);
  reg_add(g_tab, 0x100);
  reg_add(g_arr, 0x20);
}

/* One random far pointer to a planet (or to a fleet) record, or null. */
static uint32_t rand_planet(void) { return (g_npl && !chance(1, 6)) ? g_pl[rn(g_npl)] : 0; }
static uint32_t rand_fleet(void) { return (g_nfl && !chance(1, 6)) ? g_fleet[rn(g_nfl)] : 0; }

/* Random returns for the doubles of the turn functions: queues of up to STUB_QMAX for each. */
static void stubs_random(void) {
  uint8_t poke[16];
  stubs_clear_all();
  for (unsigned k = 0; k < STUB_QMAX; k++) {
    stub_push(S_18_28FC, (uint16_t)rnd(), 0, NULL);
    stub_push(S_9_462A, (uint16_t)(chance(1, 3) ? 0 : chance(1, 3) ? 0xffff : 1), 0, NULL);
    stub_push(S_9_4DE8, (uint16_t)(chance(1, 2) ? rn(80) : rn(3)), 0, NULL);
    stub_push(S_9_474C, (uint16_t)(chance(1, 3) ? 0 : chance(1, 2) ? 0xffff : 1), 0, NULL);
    stub_push(S_18_2E90, (uint16_t)rn(2), 0, NULL);
    stub_push(S_18_2DB2, (uint16_t)rn(2), 0, NULL);
    stub_push(S_18_2E4E, (uint16_t)rn(2), 0, NULL);
    stub_push(S_18_4510, (uint16_t)rn(2), 0, NULL);
    stub_push(S_18_0000, (uint16_t)rn(2), 0, NULL);
    stub_push(S_18_2BA6, (uint16_t)rn(40), 0, NULL);
    {
      uint8_t pk[16] = {0};
      uint32_t pp = rand_fleet();
      memcpy(pk, &pp, 4);
      stub_push(S_18_098A, (uint16_t)(chance(1, 3) ? 0xffff : rn(16)), 0, pk);
    }
    memset(poke, 0, sizeof poke);
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(2000);
      if (chance(1, 3)) v = 2000 + rn(2000); /* a cost the other table (18:0x3600) can pay */
      memcpy(poke + 4 * j, &v, 4);
    }
    stub_push(S_18_3558, 0, 0, poke);
    for (unsigned j = 0; j < 4; j++) {
      uint32_t v = chance(1, 4) ? 0 : chance(1, 3) ? (uint32_t)(int32_t)-(int32_t)rn(3) : rn(2000);
      memcpy(poke + 4 * j, &v, 4);
    }
    stub_push(S_18_3600, 0, 0, poke);
    memset(poke, 0, sizeof poke);
    uint16_t o = chance(1, 3) ? 0xffff : (uint16_t)rn(16);
    memcpy(poke, &o, 2);
    {
      uint16_t mask = (uint16_t)(chance(1, 2) ? 0 : chance(1, 3) ? 0xffff : rnd() & rnd()); /* the buf[k] the double sets to 1 */
      memcpy(poke + 2, &mask, 2);
    }
    stub_push(S_18_5D3C, (uint16_t)(chance(1, 2) ? rn(3000) : rn(10)), 0, poke);
    {
      uint32_t fp = rand_planet();
      stub_push(S_18_3854, LO(fp), HI(fp), NULL);
      fp = rand_planet();
      stub_push(S_18_39F6, LO(fp), HI(fp), NULL);
      fp = rand_planet();
      stub_push(S_18_3922, LO(fp), HI(fp), NULL);
    }
  }
}

/* ---- 17:0x1730 ---- */
static void t_1730(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x17300001u + 0x9e3779b9u * (it + 1);
    reg_clear();
    world_tech(it);
    stubs_tech();
    DIFF(17, 0x1730, FAR_CDECL);
  }
}
/* ---- 17:0x38b0 ---- */
static void t_38b0(void) {
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x38b00001u + 0x9e3779b9u * (it + 1);
    reg_clear();
    world_tech(it);
    stubs_tech();
    DIFF(17, 0x38b0, FAR_CDECL);
  }
}


/* The common part of the scenarios of the three big functions. */
static uint16_t build_world(unsigned it, unsigned nfl, unsigned npl) {
  uint16_t me = (uint16_t)(3 + rn(2));
  world_fleets(nfl);
  world_planets(npl, me, chance(1, 3));
  world_ds(me, it);
  world_list();
  world_table(npl);
  world_rand_fleets(nfl, me, npl);
  if (npl && chance(1, 2)) ww(g_cur, 8, g_plid[rn(npl)]);
  world_reg();
  stubs_random();
  return me;
}

/* ---- 17:0x1b70: the orders of one fleet of the type-0 player ---- */
static void t_1b70(void) {
  for (unsigned it = 0; it < 1500; it++) {
    ITER_SKIP(it);
    g_rng = 0x1b700001u + 0x9e3779b9u * (it + 1);
    unsigned nfl = 1 + it % 7, npl = 1 + (it / 7) % 6;
    uint16_t me = build_world(it, nfl, npl);
    uint32_t f = g_fleet[rn(nfl)];
    if (it % 3 != 0) ww(f, 2, me);
    if (it % 2 == 0) {
      /* the foreign-planet branch: a fleet of ours bound for another player's planet, thresholds low */
      unsigned pi = rn(npl);
      ww(f, 2, me);
      ww(f, 6, g_plid[pi]);
      ww(f, 0x62, 1);
      ww(g_pl[pi], 2, (uint16_t)(me + 1));
      DS_B(0x5341) = 0;
      DS_B(0x5343) = 0;
      static const uint32_t pw[] = {0x15f, 0x200, 0x100, 0x97, 0x96, 0x15e, 0xc8, 0x20, 0};
      wd(f, 0x58, pw[rn(9)] + rn(3));
      wd(f, 0x5c, chance(1, 2) ? 0 : rnd());
      ww(g_pl[pi], 0x12, (uint16_t)rnd());
    }
    ITER_LOG(it);
    if (DIFF(17, 0x1b70, FAR_CDECL, LO(f), HI(f)) != DT_PASS) fprintf(stderr, "  ^ iteration %u\n", it);
  }
  /* The squared-distance boundary: the fleet (+8) and its last waypoint (+0x16 of the list at +0x64) at
   * offsets whose 8:0xbc50 distance dx*dx+dy*dy is exactly 62500 = 0xf424 (250,0) (150,200), one above
   * (250,1), one below (249,0); +0x62 >= 2 and the waypoint byte +0x1d = 2, so that "at most 0xf424"
   * ends the turn of the fleet and anything more lets the orders go on. The mutants 0xf424 -> 0xf425 and
   * -> 0xf423 change the outcome for (250,1) and for (250,0) / (150,200). */
  static const int16_t off[][2] = {{250, 0}, {250, 1}, {150, 200}, {249, 0}, {0, 250}, {0, 251}, {200, 150}, {151, 200}};
  for (unsigned it = 0; it < 8 * 40; it++) {
    g_rng = 0x1b710001u + 0x9e3779b9u * (it + 1);
    unsigned nfl = 1 + it % 7, npl = 1 + (it / 7) % 6;
    uint16_t me = build_world(it, nfl, npl);
    uint32_t f = g_fleet[rn(nfl)];
    if (it % 3 != 0) ww(f, 2, me);
    uint32_t w = rd32_far(f, 0x64);
    int16_t dx = off[it % 8][0], dy = off[it % 8][1];
    if (it / 8 % 2) dx = (int16_t)-dx;
    if (it / 16 % 2) dy = (int16_t)-dy;
    uint16_t x0 = (uint16_t)(1000 + rn(1000)), y0 = (uint16_t)(1000 + rn(1000));
    ww(f, 0x62, (uint16_t)(2 + rn(3)));
    ww(f, 8, x0);
    ww(f, 0xa, y0);
    ww(w, 0x16, (uint16_t)(x0 + dx));
    ww(w, 0x18, (uint16_t)(y0 + dy));
    wb(w, 0x1d, (uint8_t)(chance(1, 2) ? 2 : (0xf0 | 2)));
    if (DIFF(17, 0x1b70, FAR_CDECL, LO(f), HI(f)) != DT_PASS) fprintf(stderr, "  ^ boundary iteration %u\n", it);
  }
}


/* ---- 17:0x0462: the turn of the type-0 player ---- */
static void t_0462(void) {
  for (unsigned it = 0; it < 1500; it++) {
    ITER_SKIP(it);
    g_rng = 0x04620001u + 0x9e3779b9u * (it + 1);
    unsigned nfl = it % 8, npl = 1 + (it / 8) % 6;
    build_world(it, nfl, npl);
    ITER_LOG(it);
    if (DIFF(17, 0x0462, FAR_CDECL, (uint16_t)rnd()) != DT_PASS) fprintf(stderr, "  ^ iteration %u\n", it);
  }
}

/* The flag loop at 17:0x058e..0x05ae (design records 14 and 15, DS:0x4930 and DS:0x49bd): a buffer flag
 * the first 18:0x5d3c call set (k = 14, 15) is cleared when the record's first word is 0x1d and bit 1
 * of its flag byte +0x7c is clear. Made on purpose for all four combinations of
 * the two records, so that the loop bound (0x4a38) and the compare value (0x1d) are pinned. */
static void t_0462_flags(void) {
  for (unsigned it = 0; it < 160; it++) {
    g_rng = 0x04620f01u + 0x9e3779b9u * (it + 1);
    unsigned nfl = it % 8, npl = 1 + (it / 8) % 6;
    build_world(it, nfl, npl);
    for (unsigned k = 14; k < 16; k++) {
      bool q = (it >> (k - 14)) & 1;
      dsw(DSG(k), q ? 0x1d : (uint16_t)(chance(1, 2) ? 0x1c : 0x1e));
      DS_B((uint16_t)(DSG(k) + 0x7c)) &= (uint8_t)~2u;
    }
    if (it >= 80) DS_B((uint16_t)(DSG(15) + 0x7c)) |= 2; /* the flag blocks the clearing of record 15 */
    stub_set_mask(S_18_5D3C, 0, 0xc000 | (uint16_t)rnd());
    ITER_LOG(it);
    if (DIFF(17, 0x0462, FAR_CDECL, (uint16_t)rnd()) != DT_PASS) fprintf(stderr, "  ^ flag iteration %u\n", it);
  }
}

/* ---- 17:0x234e: the turn of the type-1 player ---- */
static void t_234e(void) {
  for (unsigned it = 0; it < 1500; it++) {
    ITER_SKIP(it);
    g_rng = 0x234e0001u + 0x9e3779b9u * (it + 1);
    unsigned nfl = it % 8, npl = 1 + (it / 8) % 6;
    build_world(it, nfl, npl);
    ITER_LOG(it);
    if (DIFF(17, 0x234e, FAR_CDECL, (uint16_t)rnd()) != DT_PASS) fprintf(stderr, "  ^ iteration %u\n", it);
  }
}


/* ---- 17:0x0000: one computer player's turn ---- */

/* The turn functions this one dispatches to (17:0x0462, 17:0x234e, 19:0x0400, 19:0x1594, 19:0x0000) and
 * the allocator 12:0x0232 / 12:0x04d0 are decompiled, so rt_call_far would run their C versions in the
 * C run. Their behaviour is tested on its own; here they are logged thunks: the first 5 bytes of the
 * original are replaced (stub_install) and a registered adapter makes the C run execute the very same
 * bytes through CALL_ORIG, so both runs log the same call. */
#define FAKE_AD(name, seg, off, nargs)                                                         \
  static uint32_t name(uint16_t ss, uint16_t sp, Regs16 *regs) {                               \
    (void)regs;                                                                                \
    uint16_t a[4] = {0};                                                                       \
    for (unsigned i = 0; i < (nargs); i++) a[i] = DT_ARG16(ss, sp, 4 + 2 * i);                 \
    return rt_call_orig((seg), (off), FAR_CDECL, (nargs), a[0], a[1], a[2]);                   \
  }
FAKE_AD(ad_12_0232, 12, 0x0232, 2)
FAKE_AD(ad_12_04d0, 12, 0x04d0, 3)
FAKE_AD(ad_17_0462, 17, 0x0462, 1)
FAKE_AD(ad_17_234e, 17, 0x234e, 1)
FAKE_AD(ad_19_0400, 19, 0x0400, 1)
FAKE_AD(ad_19_1594, 19, 0x1594, 1)
FAKE_AD(ad_19_0000, 19, 0x0000, 1)

static void fake_register(void) {
  static const DtEntry e[] = {
      {12, 0x0232, "stub_12_0232", FAR_CDECL, ad_12_0232, 2},
      {12, 0x04d0, "stub_12_04d0", FAR_CDECL, ad_12_04d0, 0},
      {17, 0x0462, "stub_17_0462", FAR_CDECL, ad_17_0462, 0},
      {17, 0x234e, "stub_17_234e", FAR_CDECL, ad_17_234e, 0},
      {19, 0x0400, "stub_19_0400", FAR_CDECL, ad_19_0400, 0},
      {19, 0x1594, "stub_19_1594", FAR_CDECL, ad_19_1594, 0},
      {19, 0x0000, "stub_19_0000", FAR_CDECL, ad_19_0000, 0},
  };
  for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) difftest_register(&e[i]);
  static const unsigned ids[] = {S_12_0232, S_12_04D0, S_17_0462, S_17_234E, S_19_0400, S_19_1594, S_19_0000};
  for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) stub_install(ids[i]);
}
static void fake_unregister(void) {
  difftest_unregister_all();
  static const unsigned ids[] = {S_12_0232, S_12_04D0, S_17_0462, S_17_234E, S_19_0400, S_19_1594, S_19_0000};
  for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) stub_remove(ids[i]);
}

static uint32_t g_alloc[2];
static void t_0000(void) {
  fake_register();
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_fleets(2);
    world_planets(1 + it % 5, 3, false);
    world_reg();
    reg_add(g_alloc[0], 0x400);
    reg_add(g_alloc[1], 0x100);
    memset(LIN(g_info), 0xa5, 0x400);
    memset(LIN(g_alloc[0]), 0xa5, 0x400);
    memset(LIN(g_alloc[1]), 0xa5, 0x100);
    uint16_t me = (uint16_t)rn(16), pl = (uint16_t)rn(16);
    DS_W(0x14c) = me;
    DS_W(0x7a) = (uint16_t)(chance(1, 8) ? 0x1000 + rn(16) : rn(40));
    DS_W(0x190) = 0x5800;
    strcpy((char *)LINN(0x5800), chance(1, 6) ? "pl%d" : "%d");
    for (unsigned p = 0; p < 16; p++) DS_B((uint16_t)(0x5ab8 + 0xc0 * p)) = (uint8_t)rnd();
    DS_B((uint16_t)(0x5ab8 + 0xc0 * me)) = (uint8_t)(chance(1, 4) ? 1 : (rnd() & ~1u));
    dsw((uint16_t)(0x5a6a + 0xc0 * pl), (uint16_t)rnd());
    ds_w32(0x1600, chance(1, 3) ? rnd() : 0);
    ds_w32(0x1608, chance(1, 3) ? rnd() : 0);
    set_far(0x1604, chance(1, 2) ? g_tab : 0);
    memset(LIN(g_tab), 0, 0x100);
    stub_push(S_14_0508, (uint16_t)(chance(5, 6) ? 1 : 0), 0, NULL);
    stub_push(S_12_0232, chance(1, 6) ? 0 : LO(g_alloc[0]), chance(1, 6) ? 0 : HI(g_alloc[0]), NULL);
    stub_push(S_12_0232, chance(1, 6) ? 0 : LO(g_alloc[1]), chance(1, 6) ? 0 : HI(g_alloc[1]), NULL);
    stub_push(S_12_0232, chance(1, 5) ? 0 : LO(g_tab), chance(1, 5) ? 0 : HI(g_tab), NULL);
    uint16_t type = chance(1, 3) ? 0xffff : (uint16_t)((rn(8) << 13) | rn(0x2000));
    ITER_LOG(it);
    if (DIFF(17, 0x0000, FAR_CDECL, pl, type) != DT_PASS) fprintf(stderr, "  ^ iteration %u\n", it);
  }
  fake_unregister();
}

/* 21:0x1e2a and 21:0x0000 are decompiled now (src/seg21.c), but here they stay test doubles: the double
 * logged by the stub is the expectation. The C version of the caller reaches them through rt_call_far,
 * which would run the C functions, so adapters make rt_call_far run the stubbed code instead
 * (docs/CONVENTIONS.md rule j2: the doubled callees are not under test here; they are diffed in
 * tests/scenarios/test_seg21.c). */
static uint32_t double_21_1e2a(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss;
  (void)sp;
  (void)r;
  return rt_call_orig(21, 0x1e2a, FAR_CDECL, 0);
}
static uint32_t double_21_0000(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  return rt_call_orig(21, 0x0000, FAR_CDECL, 1, DT_ARG16(ss, sp, 4));
}

int main(void) {
  g_only = getenv("SEG17_ONLY");
  if (getenv("SEG17_ITER")) g_iter = atoi(getenv("SEG17_ITER"));
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
  g_fl = gblock(0x200);
  g_info = gblock(0x400);
  g_rec = gblock(0x100);
  if (want("20f4")) t_20f4();
  if (want("2154")) t_2154();
  if (want("216c")) t_216c();
  if (want("2252")) t_2252();
  if (want("3c30")) t_3c30();
  if (want("3c82")) t_3c82();
  stubs_init();
  {
    static const DtEntry e1 = {21, 0x1e2a, "double_21_1e2a", FAR_CDECL, double_21_1e2a, 0};
    static const DtEntry e2 = {21, 0x0000, "double_21_0000", FAR_CDECL, double_21_0000, 0};
    difftest_register(&e1);
    difftest_register(&e2);
  }
  g_ptrs = gblock(0x100);
  for (unsigned i = 0; i < 3; i++) g_fblk[i] = gblock(0x400);
  for (unsigned i = 0; i < 4; i++) g_wpb[i] = gblock(0x100);
  g_cur = gblock(0x100);
  g_pln = gblock(0x400);
  g_lst = gblock(0x100);
  g_tab = gblock(0x2000);
  g_arr = gblock(0x40);
  g_alloc[0] = gblock(0x400);
  g_alloc[1] = gblock(0x100);
  if (want("1730")) t_1730();
  if (want("38b0")) t_38b0();
  if (want("1b70")) t_1b70();
  if (want("0462")) t_0462(), t_0462_flags();
  if (want("234e")) t_234e();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
