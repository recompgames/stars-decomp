/* Diff tests for decompiled segment 33 (src/seg33.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 33 is the map-object (minefield) code: 18-byte records sorted by their first word in a
 * far array (DS:0x188E/0x1890 base, DS:0x1892 count, DS:0x1894 capacity), a planet table
 * (DS:0x31BA, x/y word pairs sorted by x, count DS:0x7A), fleets (far pointers at DS:0xBA/0xBC,
 * count DS:0x553A) and the player table (DS:0x5A64, 0xC0 bytes each). Every scenario builds its
 * state with the ORIGINAL code or by writing the tables directly; DIFF then snapshots it and
 * compares the original with the C version (return value, all memory, the ordered API calls).
 *
 * Offsets: 0000 insert a record, 015a delete a record, 01a0 count planets within a radius (x87),
 * 02d6 draw one text line of the minefield window, 0456 check a position for a new minefield,
 * 0742 level from a record word, 0784 the minefield turn processing, 1194 flag bit to message.
 *
 * Not tested, by design: 33:0x01a0 with a negative squared radius (the original takes the sqrt
 * domain-error path, 34:0x0d68, which is not modelled and stops the C version) and with a planet
 * table whose first and last x are equal (IDIV by zero: the original raises #DE, the C version
 * aborts). */
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
#include "runtime/user.h"
#include "src/stars.h"
#include "src/crt34.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL33 MEM_SEL_SEG(33)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))

static uint32_t g_rng = 0x33A5EED1u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static uint16_t rd16(uint32_t fp) {
  uint16_t v;
  memcpy(&v, LIN(fp), 2);
  return v;
}
static void wr16(uint32_t fp, uint16_t v) { memcpy(LIN(fp), &v, 2); }
static void wr32(uint32_t fp, uint32_t v) { memcpy(LIN(fp), &v, 4); }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }
static uint32_t ds_r32(uint16_t off) {
  uint32_t v;
  memcpy(&v, LINN(off), 4);
  return v;
}
#define FP_AT(p, o) MK_FP(FP_SEG(p), (uint16_t)(FP_OFF(p) + (o)))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

/* A zeroed global-memory block; returns the far pointer to its start. */
static uint32_t gblock(uint16_t size) {
  uint16_t h = GlobalAlloc(GPTR, size);
  CHECK(h != 0);
  return GlobalLock(h);
}

/* ---- the map-object array ---- */

/* Frees the array (original 12:0x04d0, heap 10) and clears the four words. */
static void objs_reset(void) {
  if ((DS_W(0x1890) | DS_W(0x188e)) != 0) {
    const uint16_t a[] = {DS_W(0x188e), DS_W(0x1890), 10};
    harness_call(MEM_SEL_SEG(12), 0x04d0, HCONV_CDECL, a, 3, NULL);
    CHECK_EQ(harness_last_error(), 0);
  }
  DS_W(0x188e) = 0;
  DS_W(0x1890) = 0;
  DS_W(0x1892) = 0;
  DS_W(0x1894) = 0;
}
/* Original 33:0x0000: inserts a record; returns its far pointer. */
static uint32_t o_ins(uint16_t player, uint16_t kind) {
  const uint16_t a[] = {player, kind};
  uint32_t r = harness_call(SEL33, 0x0000, HCONV_CDECL, a, 2, NULL);
  CHECK_EQ(harness_last_error(), 0);
  return r;
}
static uint32_t obj_at(unsigned i) { return MK_FP(DS_W(0x1890), (uint16_t)(DS_W(0x188e) + 0x12 * i)); }

/* ---- 33:0x0742 level from a record word ---- */
static void t_0742(void) {
  uint32_t p = gblock(0x40);
  /* every (w & 3) with (w & 0xFFC) >> 2 around the steps of the quotient by 5 and the clamps */
  static const uint16_t qs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 38, 39, 40, 41, 44, 45, 46, 64, 255, 1023};
  for (unsigned qi = 0; qi < sizeof qs / sizeof qs[0]; qi++)
    for (uint32_t low = 0; low < 4; low++) {
      uint32_t q = qs[qi];
      wr16(FP_AT(p, 6), (uint16_t)((q << 2) | low | ((q & 1) ? 0xF000 : 0)));
      DIFF(33, 0x0742, FAR_CDECL, LO(p), HI(p));
    }
  for (unsigned i = 0; i < 120; i++) {
    wr16(FP_AT(p, 6), (uint16_t)rnd());
    DIFF(33, 0x0742, FAR_CDECL, LO(p), HI(p));
  }
  wr16(FP_AT(p, 6), 0xffff);
  DIFF(33, 0x0742, FAR_CDECL, LO(p), HI(p));
  wr16(FP_AT(p, 6), 0x0000);
  DIFF(33, 0x0742, FAR_CDECL, LO(p), HI(p));
}

/* ---- 33:0x1194 flag bit to message ---- */
static void t_1194(void) {
  static const uint16_t flags[] = {0, 1, 2, 3, 4, 5, 6, 8, 0x10, 0x20, 0x21, 0x30, 0x40, 0x41, 0x60,
                                   0x7f, 0x80, 0x81, 0xc0, 0x100, 0x101, 0x200, 0x300, 0x400, 0x401,
                                   0x800, 0x801, 0x1000, 0x2000, 0x4000, 0x8000, 0xffff, 0x0802};
  for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++)
    for (uint16_t player = 0; player < 16; player += (i % 2) ? 8 : 6)
      DIFF(33, 0x1194, FAR_CDECL, flags[i], player, 0x2000 + 2 * i);
  /* the out word is one DS word; a word at the player table itself (flags word = out) */
  DIFF(33, 0x1194, FAR_CDECL, 0x40, 1, 0x5AB6 + 0xC0);
  DIFF(33, 0x1194, FAR_CDECL, 0x40, 0, 0x5AB6);
  for (unsigned i = 0; i < 100; i++)
    DIFF(33, 0x1194, FAR_CDECL, (uint16_t)rnd(), (uint16_t)(rnd() % 16), 0x2400 + 2 * (rnd() % 100));
}

/* ---- 33:0x0000 insert, 33:0x015a delete ---- */
static void fill(unsigned n) {
  for (unsigned i = 0; i < n; i++) (void)o_ins(rnd() % 16, rnd() % 8);
}

static void t_0000(void) {
  objs_reset();
  /* empty array: the allocation path (12:0x0232) */
  DIFF(33, 0x0000, FAR_CDECL, 3, 2);
  DIFF(33, 0x0000, FAR_CDECL, 0, 0);
  DIFF(33, 0x0000, FAR_CDECL, 15, 7);
  DIFF(33, 0x0000, FAR_CDECL, 0xFFFF, 0xFFFF);
  /* a few records: keys below, between, above and equal to existing ones */
  (void)o_ins(3, 2);
  (void)o_ins(5, 1);
  (void)o_ins(3, 2); /* equal key: the serial is bumped */
  (void)o_ins(9, 6);
  DIFF(33, 0x0000, FAR_CDECL, 0, 0);     /* sorts first */
  DIFF(33, 0x0000, FAR_CDECL, 3, 2);     /* equal to two records */
  DIFF(33, 0x0000, FAR_CDECL, 5, 1);     /* equal to one */
  DIFF(33, 0x0000, FAR_CDECL, 4, 2);     /* between */
  DIFF(33, 0x0000, FAR_CDECL, 9, 7);     /* last */
  DIFF(33, 0x0000, FAR_CDECL, 15, 7);    /* last, no memmove */
  DIFF(33, 0x0000, FAR_CDECL, 2, 7);     /* different kind and owner bits */
  DIFF(33, 0x0000, FAR_CDECL, 0x11, 0x9); /* parameters beyond the 4-bit and 3-bit fields */
  /* the capacity is reached: 10 records, then the growth path (12:0x03ec) */
  fill(6);
  CHECK_EQ(DS_W(0x1892), 10);
  CHECK_EQ(DS_W(0x1894), 10);
  DIFF(33, 0x0000, FAR_CDECL, 3, 2);
  DIFF(33, 0x0000, FAR_CDECL, 15, 7);
  DIFF(33, 0x0000, FAR_CDECL, 0, 0);
  /* capacity above the count: no growth */
  (void)o_ins(2, 2);
  DIFF(33, 0x0000, FAR_CDECL, 2, 2);
  DIFF(33, 0x0000, FAR_CDECL, 8, 3);
  /* a negative player/kind pair of the raw words */
  DIFF(33, 0x0000, FAR_CDECL, 0x8001, 0x7fff);
  /* 0x1FF records of one key: the serial runs out and the result is 0 */
  objs_reset();
  unsigned made = 0;
  for (; made < 600; made++) {
    if (o_ins(6, 3) == 0) break;
  }
  CHECK_EQ(made, 0x200); /* the 0x201st key would need the serial 0x1FF + 1 */
  DIFF(33, 0x0000, FAR_CDECL, 6, 3);   /* full: returns 0 */
  DIFF(33, 0x0000, FAR_CDECL, 6, 4);   /* another key: still inserts (sorts after) */
  DIFF(33, 0x0000, FAR_CDECL, 6, 2);   /* sorts before everything */
  DIFF(33, 0x0000, FAR_CDECL, 7, 3);
  /* the total limit 0xFD2 (signed compare) */
  objs_reset();
  (void)o_ins(1, 1);
  DS_W(0x1892) = 0xfd2;
  DIFF(33, 0x0000, FAR_CDECL, 1, 1);
  DS_W(0x1892) = 0xfd3;
  DIFF(33, 0x0000, FAR_CDECL, 1, 1);
  DS_W(0x1892) = 0x7fff;
  DIFF(33, 0x0000, FAR_CDECL, 1, 1);
  /* randomized: build an array, diff an insert of every kind of key, repeat */
  objs_reset();
  for (unsigned round = 0; round < 40; round++) {
    fill(1 + rnd() % 4);
    DIFF(33, 0x0000, FAR_CDECL, (uint16_t)(rnd() % 16), (uint16_t)(rnd() % 8));
    DIFF(33, 0x0000, FAR_CDECL, (uint16_t)(rnd() % 3), (uint16_t)(rnd() % 3));
  }
  objs_reset();
}

#define D15(off, seg) \
  do { \
    if (getenv("T33_TRACE")) fprintf(stderr, "015a off %04x seg %04x count %04x\n", (unsigned)(off), (unsigned)(seg), DS_W(0x1892)); \
    DIFF(33, 0x015a, FAR_CDECL, off, seg); \
  } while (0)
static void t_015a(void) {
  objs_reset();
  /* count 0: last = 0 - 0x12 wraps to 0xFFEE, an offset above it moves nothing; the count drops to 0xFFFF */
  D15(0xfff0, 0x1234);
  fill(7);
  unsigned n = DS_W(0x1892);
  CHECK_EQ(n, 7);
  for (unsigned i = 0; i < n; i++) {
    uint32_t o = obj_at(i);
    D15(LO(o), HI(o));   /* first .. last record */
  }
  uint32_t past = obj_at(n);                       /* one past the end: nothing moves */
  D15(LO(past), HI(past));
  uint32_t beyond = obj_at(n + 3);
  D15(LO(beyond), HI(beyond));
  uint32_t before = obj_at(0);
  D15((uint16_t)(LO(before) + 5), HI(before)); /* not on a record start */
  D15((uint16_t)(LO(before) + 0x13), HI(before));
  /* a one-record array: the last record is the first */
  objs_reset();
  (void)o_ins(2, 2);
  uint32_t one = obj_at(0);
  D15(LO(one), HI(one));
  /* a large array: the offsets wrap in 16 bits */
  objs_reset();
  fill(200);
  for (unsigned i = 0; i < 200; i += 23) {
    uint32_t o = obj_at(i);
    D15(LO(o), HI(o));
  }
  objs_reset();
}

/* ---- 33:0x0456 position check ---- */

/* Environment: 3 players (limit 0x578 + 3 * 400 = 0xA28), six planets, three fleets. */
static uint32_t g_p6, g_fleet[4];
static void env_0456(void) {
  objs_reset();
  DS_W(0x0074) = 3;
  static const uint16_t pxy[][2] = {{1100, 1500}, {1500, 1100}, {1500, 1800}, {1800, 1500}, {2200, 2000}, {2300, 1300}};
  DS_W(0x007a) = 6;
  for (unsigned i = 0; i < 6; i++) {
    DS_W(0x31ba + 4 * i) = pxy[i][0];
    DS_W(0x31ba + 4 * i + 2) = pxy[i][1];
  }
  /* fleets: far pointer array at DS:0xBA, count at DS:0x553A, a null pointer ends it early */
  uint32_t arr = gblock(0x40);
  ds_w32(0x00ba, arr);
  DS_W(0x553a) = 4;
  static const uint16_t fxy[][2] = {{1700, 1700}, {1300, 2200}, {2100, 1100}};
  for (unsigned i = 0; i < 3; i++) {
    g_fleet[i] = gblock(0x80);
    wr32(FP_AT(arr, 4 * i), g_fleet[i]);
    wr16(FP_AT(g_fleet[i], 8), fxy[i][0]);
    wr16(FP_AT(g_fleet[i], 10), fxy[i][1]);
  }
  /* the fourth entry is null (ends the loop although the count says 4) */
  wr32(FP_AT(arr, 12), 0);
  /* map objects: kind 2 (id word 0x4000..): different owners and distances */
  static const uint16_t oxy[][3] = {{1000 + 700, 1000 + 800, 0}, {1200, 1900, 1}, {2000, 1700, 2}, {1750, 2100, 1}};
  for (unsigned i = 0; i < 4; i++) {
    uint32_t o = o_ins(oxy[i][2], 2);
    wr16(FP_AT(o, 2), oxy[i][0]);
    wr16(FP_AT(o, 4), oxy[i][1]);
  }
  (void)o_ins(1, 3); /* another kind: ignored by the distance loop but its xy is compared */
  g_p6 = gblock(0x40);
}

static void try_pos(uint32_t p, uint16_t x, uint16_t y) {
  wr16(FP_AT(p, 2), x);
  wr16(FP_AT(p, 4), y);
  DIFF(33, 0x0456, FAR_CDECL, LO(p), HI(p));
}

/* Finds (dx, dy) with dx*dx + dy*dy == v (the first in a fixed search order); false if none. */
static bool two_squares(int v, int *dx, int *dy) {
  for (int a = 0; a <= 90; a++)
    for (int b = a; b <= 90; b++)
      if (a * a + b * b == v) {
        *dx = a;
        *dy = -b;
        return true;
      }
  return false;
}

/* Positions whose squared distance to (cx, cy) is each of t - 1, t, t + 1 (those that exist). */
static void try_around(uint32_t p, uint16_t cx, uint16_t cy, int t) {
  for (int v = t - 1; v <= t + 1; v++) {
    int dx, dy;
    if (v >= 0 && two_squares(v, &dx, &dy)) {
      try_pos(p, (uint16_t)(cx + dx), (uint16_t)(cy + dy));
      try_pos(p, (uint16_t)(cx - dy), (uint16_t)(cy + dx)); /* swapped and mirrored */
    }
  }
}

static void t_0456(void) {
  env_0456();
  uint32_t p = g_p6;
  /* bounds: below 1000, above the limit, the 10 border, in range */
  static const uint16_t edge[] = {0, 999, 1000, 1001, 1009, 1010, 1011, 1012, 1500, 2500, 2574, 2575, 2576, 2577, 2580, 2600, 2601, 0x7fff, 0x8000, 0xffff};
  for (unsigned i = 0; i < sizeof edge / 2; i++)
    for (unsigned j = 0; j < sizeof edge / 2; j += 3) try_pos(p, edge[i], edge[j]);
  /* the same xy as a planet, a fleet, an object (itself and another), and next to them */
  for (unsigned i = 0; i < 6; i++) {
    uint16_t px = DS_W(0x31ba + 4 * i), py = DS_W(0x31ba + 4 * i + 2);
    try_pos(p, px, py);
    try_pos(p, (uint16_t)(px + 1), py);
    try_pos(p, px, (uint16_t)(py - 1));
  }
  for (unsigned i = 0; i < 3; i++) {
    uint16_t fx = rd16(FP_AT(g_fleet[i], 8)), fy = rd16(FP_AT(g_fleet[i], 10));
    try_pos(p, fx, fy);
    try_pos(p, (uint16_t)(fx + 1), fy);
  }
  /* an object other than p at the same xy: 0xF; p inside the array at its own xy: allowed */
  for (unsigned i = 0; i < DS_W(0x1892); i++) {
    uint32_t o = obj_at(i);
    uint16_t ox = rd16(FP_AT(o, 2)), oy = rd16(FP_AT(o, 4));
    try_pos(p, ox, oy);
    DIFF(33, 0x0456, FAR_CDECL, LO(o), HI(o));          /* itself: skipped */
    if (i >= 3) continue;
    /* squared distances around every threshold: other owner word (p +0xC unrelated), same word */
    static const int tother[] = {0x10, 0x40, 0xe1, 0x384};
    static const int tsame[] = {0x19, 0x64, 0x384, 0x1324};
    wr16(FP_AT(p, 0xc), 0);
    for (unsigned k = 0; k < 4; k++) try_around(p, ox, oy, tother[k]);
    wr16(FP_AT(p, 0xc), rd16(o));
    for (unsigned k = 0; k < 4; k++) try_around(p, ox, oy, tsame[k]);
    for (int d = 0; d <= 100; d += 7) {                  /* and a spread in between */
      try_pos(p, (uint16_t)(ox + d), oy);
      wr16(FP_AT(p, 0xc), 0);
      try_pos(p, ox, (uint16_t)(oy - d));
      wr16(FP_AT(p, 0xc), rd16(o));
    }
    wr16(FP_AT(p, 0xc), 0);
  }
  /* a far-away object makes dx*dx + dy*dy exceed 16 bits (and 31 bits) */
  {
    uint32_t o = obj_at(1);
    uint16_t save2 = rd16(FP_AT(o, 2)), save4 = rd16(FP_AT(o, 4));
    wr16(FP_AT(o, 2), 0x7000);
    wr16(FP_AT(o, 4), 0x7000);
    try_pos(p, 1500, 1500);
    wr16(FP_AT(o, 2), 0x8000);
    wr16(FP_AT(o, 4), 0x8000);
    try_pos(p, 0x7fff, 0x7fff);
    try_pos(p, 0x7ff0, 0x7ff0);
    wr16(FP_AT(p, 0xc), rd16(o));
    try_pos(p, 0x7ff0, 0x7ff0);
    wr16(FP_AT(p, 0xc), 0);
    DS_W(0x0074) = 200; /* the limit rises to 0x1318 + ... wrapping through 16 bits */
    try_pos(p, 0x7fff, 0x7fff);
    try_pos(p, 1500, 2000);
    wr16(FP_AT(o, 2), 0x7fff);
    wr16(FP_AT(o, 4), 0x8000);
    try_pos(p, 1500, 1500);
    DS_W(0x0074) = 3;
    wr16(FP_AT(o, 2), save2);
    wr16(FP_AT(o, 4), save4);
  }
  /* planet distances: the 0x310 limit and the 0x19 / 0x64 / 0x190 classes */
  {
    DS_W(0x007a) = 2;
    DS_W(0x31ba) = 3000;
    DS_W(0x31bc) = 3000;
    DS_W(0x31be) = 0x7000;
    DS_W(0x31c0) = 0x7000;
    try_pos(p, 1500, 1500);
    DS_W(0x31be) = 0x8000;
    DS_W(0x31c0) = 0x8000;
    try_pos(p, 1500, 1500);
    try_pos(p, 0x7000, 0x7000);
    DS_W(0x31ba) = 1500;
    DS_W(0x31bc) = 1500;
    DS_W(0x007a) = 1;
    static const int tplanet[] = {0x19, 0x64, 0x190, 0x310};
    for (unsigned k = 0; k < 4; k++) try_around(p, 1500, 1500, tplanet[k]);
    DS_W(0x007a) = 0;
    try_pos(p, 1800, 1800);
    DS_W(0x007a) = 6;
    env_0456();
  }
  /* no fleets / no objects */
  DS_W(0x553a) = 0;
  try_pos(p, 1800, 1800);
  DS_W(0x1892) = 0;
  try_pos(p, 1800, 1800);
  try_pos(p, 1005, 1005);
  /* fuzz: random positions around the environment */
  env_0456();
  for (unsigned i = 0; i < 500; i++) {
    uint32_t r = rnd();
    uint16_t x = (uint16_t)(1000 + (r & 0x7ff) % 1700);
    uint16_t y = (uint16_t)(1000 + ((r >> 11) & 0x7ff) % 1700);
    if (i % 3 == 0) {
      uint32_t o = obj_at(i % 4);
      x = (uint16_t)(rd16(FP_AT(o, 2)) + (int)(r % 30) - 15);
      y = (uint16_t)(rd16(FP_AT(o, 4)) + (int)((r >> 8) % 30) - 15);
      wr16(FP_AT(p, 0xc), (i & 1) ? rd16(o) : 0);
    } else if (i % 3 == 1) {
      x = (uint16_t)(DS_W(0x31ba + 4 * (i % 6)) + (int)(r % 24) - 12);
      y = (uint16_t)(DS_W(0x31ba + 4 * (i % 6) + 2) + (int)((r >> 8) % 24) - 12);
    }
    try_pos(p, x, y);
  }
  objs_reset();
  DS_W(0x007a) = 0;
  DS_W(0x553a) = 0;
}

/* ---- 33:0x01a0 planets within a radius ---- */
static void t_01a0(void) {
  objs_reset();
  /* n = 0: the span is read from the two words around DS:0x31B6, which differ */
  DS_W(0x007a) = 0;
  DS_W(0x31b6) = 5;
  DS_W(0x31ba) = 9;
  DIFF(33, 0x01a0, FAR_CDECL, 100, 100, 100, 0);
  /* a table of sorted planets */
  for (unsigned round = 0; round < 5; round++) {
    unsigned n = round == 0 ? 2 : round == 1 ? 3 : 20 + round * 12;
    uint16_t x = (uint16_t)(1000 + rnd() % 50);
    for (unsigned i = 0; i < n; i++) {
      DS_W(0x31ba + 4 * i) = x;
      DS_W(0x31ba + 4 * i + 2) = (uint16_t)(1000 + rnd() % 1500);
      x = (uint16_t)(x + rnd() % (round == 4 ? 3 : 70));   /* round 4: many equal x */
    }
    if (DS_W(0x31ba) == DS_W(0x31ba + 4 * (n - 1))) DS_W(0x31ba + 4 * (n - 1)) = (uint16_t)(DS_W(0x31ba) + 1);
    DS_W(0x007a) = (uint16_t)n;
    uint16_t x0 = DS_W(0x31ba), xe = DS_W(0x31ba + 4 * (n - 1));
    static const uint32_t r2s[] = {0, 1, 2, 3, 4, 9, 10, 99, 100, 101, 1000, 4096, 10000, 40000, 250000, 1000000, 4000000, 100000000, 2000000000u, 2147483647u};
    for (unsigned k = 0; k < sizeof r2s / 4; k++) {
      for (unsigned q = 0; q < 4; q++) {
        uint16_t px = q == 0 ? x0 : q == 1 ? xe : q == 2 ? (uint16_t)(x0 - 60) : q == 3 ? (uint16_t)(xe + 60) : (uint16_t)(x0 + rnd() % (xe - x0 + 1));
        uint16_t py = (uint16_t)(1000 + rnd() % 1500);
        DIFF(33, 0x01a0, FAR_CDECL, px, py, LO(r2s[k]), HI(r2s[k]));
      }
    }
    /* centred on a planet: radius exactly its distance to a neighbour */
    for (unsigned i = 0; i + 1 < n; i += 3) {
      int dx = (int16_t)(DS_W(0x31ba + 4 * (i + 1)) - DS_W(0x31ba + 4 * i));
      int dy = (int16_t)(DS_W(0x31ba + 4 * (i + 1) + 2) - DS_W(0x31ba + 4 * i + 2));
      uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
      for (int e = -1; e <= 1; e++) {
        uint32_t r2 = d2 + (uint32_t)e;
        DIFF(33, 0x01a0, FAR_CDECL, DS_W(0x31ba + 4 * i), DS_W(0x31ba + 4 * i + 2), LO(r2), HI(r2));
      }
    }
    for (unsigned i = 0; i < 40; i++) {
      uint32_t r2 = rnd() % (i % 4 == 0 ? 0x7fffffffu : i % 4 == 1 ? 40000u : 2000000u);
      DIFF(33, 0x01a0, FAR_CDECL, (uint16_t)(x0 + rnd() % 120 - 30), (uint16_t)(rnd() % 3000), LO(r2), HI(r2));
    }
  }
  /* DS:0x1E1E nonzero: the CRT sqrt does not store the operand at DS:0x1C6A */
  DS_B(0x1e1e) = 1;
  DIFF(33, 0x01a0, FAR_CDECL, 1200, 1200, 40000, 0);
  DIFF(33, 0x01a0, FAR_CDECL, 1200, 1200, 0, 0);
  DS_B(0x1e1e) = 0;
  /* the far ends of 16-bit space: x - r and x + r wrap */
  DS_W(0x007a) = 4;
  static const uint16_t ex[] = {0x0010, 0x4000, 0x7ff0, 0xffe0};
  for (unsigned i = 0; i < 4; i++) {
    DS_W(0x31ba + 4 * i) = ex[i];
    DS_W(0x31ba + 4 * i + 2) = (uint16_t)(0x100 * i + 7);
  }
  DIFF(33, 0x01a0, FAR_CDECL, 0x4000, 0x0107, 40000, 0);
  DIFF(33, 0x01a0, FAR_CDECL, 0x7ff0, 0x0207, 0xffff, 0x7fff);
  DIFF(33, 0x01a0, FAR_CDECL, 0xffe0, 0x0307, 10000, 0);
  DIFF(33, 0x01a0, FAR_CDECL, 0x0010, 0x0007, 2000000000u & 0xffff, 2000000000u >> 16);
  DS_W(0x007a) = 0;
}


/* ---- 33:0x02d6 one text line of the minefield window ---- */
static void rect_at(uint16_t off, int16_t l, int16_t t, int16_t r, int16_t b) {
  DS_W(off) = (uint16_t)l;
  DS_W(off + 2) = (uint16_t)t;
  DS_W(off + 4) = (uint16_t)r;
  DS_W(off + 6) = (uint16_t)b;
}

static void t_02d6(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  DS_W(0x2930) = GetStockObject(16); /* OEM_FIXED_FONT: the game's own font object is not created here */
  /* 8:0x9874 draws a bar: the brushes are DS:0x1A (frame), DS:0x10 (rest), and the unit brushes
   * DS:0x27F6 + 2 * i for the three values (DS:0x16 for the modes 3 and 4). The image has none yet. */
  DS_W(0x001a) = GetStockObject(4);
  DS_W(0x0010) = GetStockObject(2);
  DS_W(0x0016) = GetStockObject(3);
  DS_W(0x27f6) = GetStockObject(0);
  DS_W(0x27f8) = GetStockObject(1);
  DS_W(0x27fa) = GetStockObject(3);
  uint32_t o = gblock(0x40);
  static const int16_t modes[] = {0, 1, 2, 3, 4, 5, -1, 6, 7, 0x7fff, -0x8000};
  static const uint16_t w8[] = {0, 1, 7, 100, 1234, 32767, 0x8000, 0xffff, 0xfffe};
  for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    for (unsigned k = 0; k < 9; k++) {
      wr16(FP_AT(o, 8), w8[k]);
      wr16(FP_AT(o, 0xa), w8[(k + 2) % 9]);
      wr16(FP_AT(o, 0xc), w8[(k + 5) % 9]);
      wr16(FP_AT(o, 0xe), (uint16_t)(k * 0x1b3f + (k & 1) * 0xc000));
      rect_at(0x2300, 10, 10, (k % 3) == 0 ? 25 : 300, 30);  /* narrow: nothing is drawn; wide */
      DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), (uint16_t)modes[i]);
    }
  }
  /* the text width against right - left - 3: scan the rectangle width across the text extent */
  wr16(FP_AT(o, 8), 12345);
  wr16(FP_AT(o, 0xe), 0x2222);
  for (int w = 0; w < 140; w += 4) {
    rect_at(0x2300, 5, 5, (int16_t)(5 + w), 20);
    DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 0);
    DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 3);
    DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 5);
  }
  rect_at(0x2300, 100, 5, 20, 20); /* negative width */
  DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 1);
  rect_at(0x2300, 10, 5, 0x7fff, 20);
  DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 2);
  rect_at(0x2300, -0x7000, 5, 0x7000, 20); /* right - left - 3 wraps */
  DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 2);
  /* a different font object and a background mode already set */
  DS_W(0x2930) = GetStockObject(13);
  SetBkMode(dc, 2);
  rect_at(0x2300, 10, 10, 300, 30);
  DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 5);
  SetBkMode(dc, 1);
  DIFF(33, 0x02d6, FAR_CDECL, dc, 0x2300, LO(o), HI(o), 5);
}

/* ---- 33:0x0784 the minefield turn processing ---- */

/* Original code of another segment (setup, not under test); the call must not fail. */
static uint32_t oc(uint16_t seg, uint16_t off, const uint16_t *a, int n) {
  uint32_t r = harness_call(MEM_SEL_SEG(seg), off, HCONV_CDECL, a, n, NULL);
  CHECK_EQ(harness_last_error(), 0);
  return r;
}
/* 7:0x1f2e creates a fleet (heap 5, the pointer array in heap 6) for a player. */
static uint32_t o_fleet(uint16_t owner) {
  const uint16_t a[] = {owner, 0xffff};
  return oc(7, 0x1f2e, a, 2);
}
#define PW(p, off) DS_W(0x5a64 + 0xc0 * (p) + (off))
#define PB(p, off) DS_B(0x5a64 + 0xc0 * (p) + (off))

#define NPL 4
#define NFLEET 8
#define NOBJ 4
#define NPLAN 5
static uint32_t g_fl[NFLEET], g_ob[NOBJ], g_pl[NPLAN];

/* The world: NPL players, NFLEET fleets (two per player), NOBJ kind-3 map objects, NPLAN planets
 * (sorted x/y table DS:0x31BA) and one design array per player. The scenarios then poke the
 * numbers the function looks at. */
static void world_784(void) {
  objs_reset();
  memset(LINN(0x5a64), 0, 0xc0 * 8);
  uint32_t mb = gblock(0x8000);
  ds_w32(0x0aac, mb);
  DS_W(0x0ab0) = 0;
  DS_W(0x0ab4) = 0;
  for (unsigned p = 0; p < 8; p++) ds_w32(0x00be + 4 * p, gblock(16 * 0x8d));
  ds_w32(0x00ba, 0);
  DS_W(0x553a) = 0;
  for (unsigned i = 0; i < NFLEET; i++) {
    g_fl[i] = o_fleet(i / 2 % NPL);
    CHECK(g_fl[i] != 0);
  }
  for (unsigned i = 0; i < NOBJ; i++) g_ob[i] = o_ins(i % NPL, 3);
  uint32_t pa = gblock(NPLAN * 0x38);
  ds_w32(0x00b6, pa);
  DS_W(0x4d96) = NPLAN;
  DS_W(0x007a) = NPLAN;
  for (unsigned i = 0; i < NPLAN; i++) {
    g_pl[i] = FP_AT(pa, 0x38 * i);
    wr16(g_pl[i], (uint16_t)i);
    DS_W(0x31ba + 4 * i) = (uint16_t)(1200 + 40 * i);
    DS_W(0x31ba + 4 * i + 2) = (uint16_t)(1200 + 30 * i);
  }
}

/* The message buffer (far pointer DS:0xAAC, used length DS:0xAB0, count DS:0xAB4) holds records
 * (payload length << 4 | player, id word, argument word, payload). 6:0x80fa marks the ones with
 * the player, id 0x4E and the fleet id | 0x8000 it is given: seed such records for every fleet,
 * with decoys differing in the player, the id and the argument. */
static void seed_msgs(void) {
  uint32_t mb = ds_r32(0x0aac);
  uint16_t pos = 0, count = 0;
  for (unsigned i = 0; i < NFLEET; i++) {
    uint16_t id = rd16(g_fl[i]);
    uint16_t owner = rd16(FP_AT(g_fl[i], 2));
    for (unsigned k = 0; k < 4; k++) {
      uint8_t plen = (uint8_t)(k == 3 ? 2 : 0);
      uint8_t b0 = (uint8_t)((plen << 4) | ((k == 1 ? owner + 1 : owner) & 0xf));
      uint16_t mid = (uint16_t)((k == 2 ? 0x4f : 0x4e) | ((i & 1) << 9));
      uint16_t arg = (uint16_t)((id | 0x8000) + (k == 3 ? 1 : 0));
      if (k == 3) mid = 0x4e;
      memcpy(LIN(FP_AT(mb, pos)), &b0, 1);
      wr16(FP_AT(mb, pos + 1), mid);
      wr16(FP_AT(mb, pos + 3), arg);
      pos = (uint16_t)(pos + 5 + plen);
      count++;
    }
  }
  DS_W(0x0ab0) = pos;
  DS_W(0x0ab4) = count;
}

/* Pokes the numbers 33:0x0784 looks at with values drawn from the generator. */
static void poke_784(uint32_t knob) {
  uint32_t pos = rnd();
  DS_W(0x151e) = (uint16_t)rnd();
  DS_W(0x1520) = (uint16_t)(rnd() & 0x7fff);
  DS_W(0x1522) = (uint16_t)rnd();
  DS_W(0x1524) = (uint16_t)(rnd() & 0x7fff);
  DS_W(0x0082) = (uint16_t)(knob & 1 ? 30 + rnd() % 50 : (knob & 4) ? 600 + rnd() % 900 : 101 + rnd() % 400);
  DS_B(0x0080) = (uint8_t)((knob & 2) ? 4 : 0);
  for (unsigned p = 0; p < NPL; p++) {
    unsigned prof = rnd() % 4; /* 0: random, 1: all levels high, 2: all high and every flag set, 3: low */
    PB(p, 7) = (uint8_t)((prof == 2) ? 0x08 : (rnd() & 1) ? 0x0a : (rnd() & 1) ? 0x1e : (rnd() & 1) ? 0x02 : 0x08);
    PB(p, 0x54) = (uint8_t)(rnd() % 4);
    PW(p, 0x52) = (uint16_t)((prof == 2 || (rnd() & 3) == 0) ? 0x1fff : rnd() & 0xfff);
    PW(p, 4) = (uint16_t)((PW(p, 4) & 0xf000) | ((rnd() % 4) ? (rnd() % 0x100) : 0x1ff + rnd() % 2));
    for (unsigned l = 0; l < 6; l++) {
      uint8_t v = (uint8_t)((rnd() & 1) ? 10 + rnd() % 20 : rnd() % 12);
      if (prof == 1 || prof == 2) v = (uint8_t)(26 + rnd() % 20);
      if (prof == 3) v = (uint8_t)(rnd() % 9);
      PB(p, 0x1a + l) = v;
    }
    for (unsigned l = 0; l < 6; l++) ds_w32(0x5a84 + 0xc0 * p + 4 * l, rnd() % 100000);
    /* design slots: byte +0x7c bit 1 = free slot, +0x7b..: the flag/name bytes */
    for (unsigned d = 0; d < 16; d++) {
      uint32_t dp = FP_AT(ds_r32((uint16_t)(0x00be + 4 * p)), 0x8d * d);
      uint8_t f = (uint8_t)rnd();
      memcpy(LIN(FP_AT(dp, 0x7c)), &f, 1);
    }
  }
  for (unsigned i = 0; i < NFLEET; i++) {
    uint32_t f = g_fl[i];
    uint16_t ox = (uint16_t)(1100 + 50 * (pos % 3)), oy = (uint16_t)(1100 + 50 * ((pos >> 3) % 3));
    wr16(FP_AT(f, 8), (rnd() % 3) ? ox : (uint16_t)(ox + 1));
    wr16(FP_AT(f, 10), (rnd() % 4) ? oy : (uint16_t)(oy + 7));
    uint32_t c0 = rnd() % 6000, c1 = rnd() % 6000, c2 = rnd() % 6000;
    if (rnd() & 1) {
      c0 = rnd() % 12000;
      c1 = 0;
      c2 = 0;
    }
    wr32(FP_AT(f, 0x4c), c0);
    wr32(FP_AT(f, 0x50), c1);
    wr32(FP_AT(f, 0x54), c2);
    uint8_t fl = 0;
    memcpy(&fl, LIN(FP_AT(f, 5)), 1);
    fl = (uint8_t)((fl & ~0x24) | ((rnd() % 5 == 0) ? 4 : 0) | ((rnd() % 4 == 0) ? 0x20 : 0));
    memcpy(LIN(FP_AT(f, 5)), &fl, 1);
    wr16(FP_AT(f, 6), (uint16_t)(rnd() % 5 == 0 ? 0xffff : rnd() % NPLAN));
    if (rnd() % 3 == 0) wr16(f, (uint16_t)((rd16(f) & 0xfe00) | (3 + rnd() % 6))); /* a gap below this serial */
  }
  for (unsigned i = 0; i < NOBJ; i++) {
    uint32_t o = g_ob[i];
    wr16(FP_AT(o, 2), (uint16_t)(1100 + 50 * (pos % 3)));
    wr16(FP_AT(o, 4), (uint16_t)(1100 + 50 * ((pos >> 3) % 3)));
    wr16(FP_AT(o, 0xc), (uint16_t)((rnd() % 3) ? 0 : rnd() & 0xf));
    wr16(FP_AT(o, 0xe), (uint16_t)((rnd() % 3) ? 0 : rnd() & 0x1fff));
  }
  for (unsigned i = 0; i < NPLAN; i++) {
    uint32_t pl = g_pl[i];
    wr16(FP_AT(pl, 2), (uint16_t)(rnd() % 6 == 0 ? 0xffff : rnd() % NPL));
    uint8_t f5 = (uint8_t)((rnd() % 5) ? 2 : 0);
    memcpy(LIN(FP_AT(pl, 5)), &f5, 1);
    wr32(FP_AT(pl, 0x1c), rnd() % 4000);
    wr32(FP_AT(pl, 0x20), rnd() % 4000);
    wr32(FP_AT(pl, 0x24), rnd() % 4000);
    DS_W(0x31ba + 4 * i) = (uint16_t)(1100 + 50 * (pos % 3) - 60 + rnd() % 130);
    DS_W(0x31ba + 4 * i + 2) = (uint16_t)(1100 + 50 * ((pos >> 3) % 3) - 60 + rnd() % 130);
  }
  seed_msgs();
}

/* A calm baseline: nothing but fleet 0 sits on the minefields, no planet is eligible, every
 * player has equal low levels, the RNG state is fixed. The boundary scenarios change one thing. */
static void base_784(void) {
  DS_W(0x151e) = 0x1234;
  DS_W(0x1520) = 0x0456;
  DS_W(0x1522) = 0x2345;
  DS_W(0x1524) = 0x0567;
  DS_W(0x0082) = 50;
  DS_B(0x0080) = 0;
  for (unsigned p = 0; p < NPL; p++) {
    PB(p, 7) = 0x0a;
    PB(p, 0x54) = 0;
    PW(p, 0x52) = 0;
    PW(p, 4) = (uint16_t)((PW(p, 4) & 0xf000) | 2);
    for (unsigned l = 0; l < 6; l++) PB(p, 0x1a + l) = 5;
    for (unsigned l = 0; l < 6; l++) ds_w32(0x5a84 + 0xc0 * p + 4 * l, 1000 * (l + 1));
    for (unsigned d = 0; d < 16; d++) {
      uint32_t dp = FP_AT(ds_r32((uint16_t)(0x00be + 4 * p)), 0x8d * d);
      memcpy(LIN(FP_AT(dp, 0x7c)), &(uint8_t){0}, 1);
    }
  }
  for (unsigned i = 0; i < NFLEET; i++) {
    uint32_t f = g_fl[i];
    wr16(FP_AT(f, 8), (uint16_t)(3000 + i));
    wr16(FP_AT(f, 10), 3000);
    wr32(FP_AT(f, 0x4c), 0);
    wr32(FP_AT(f, 0x50), 0);
    wr32(FP_AT(f, 0x54), 0);
    memcpy(LIN(FP_AT(f, 5)), &(uint8_t){0}, 1);
    wr16(FP_AT(f, 6), 0xffff);
  }
  wr16(FP_AT(g_fl[0], 8), 1100);
  wr16(FP_AT(g_fl[0], 10), 1100);
  for (unsigned i = 0; i < NOBJ; i++) {
    uint32_t o = g_ob[i];
    wr16(FP_AT(o, 2), 1100);
    wr16(FP_AT(o, 4), 1100);
    wr16(FP_AT(o, 0xc), 0);
    wr16(FP_AT(o, 0xe), 0);
  }
  for (unsigned i = 0; i < NPLAN; i++) {
    wr16(FP_AT(g_pl[i], 2), 0xffff);
    memcpy(LIN(FP_AT(g_pl[i], 5)), &(uint8_t){0}, 1);
    wr32(FP_AT(g_pl[i], 0x1c), 0);
    wr32(FP_AT(g_pl[i], 0x20), 0);
    wr32(FP_AT(g_pl[i], 0x24), 0);
    DS_W(0x31ba + 4 * i) = (uint16_t)(5000 + 40 * i);
    DS_W(0x31ba + 4 * i + 2) = 5000;
  }
  seed_msgs();
}
static void set_cargo(uint32_t f, uint32_t total) {
  wr32(FP_AT(f, 0x4c), total / 3);
  wr32(FP_AT(f, 0x50), total / 3);
  wr32(FP_AT(f, 0x54), total - 2 * (total / 3));
}
static void set_levels(unsigned p, unsigned sum) {
  for (unsigned l = 0; l < 6; l++) PB(p, 0x1a + l) = (uint8_t)(sum / 6 + (l < sum % 6 ? 1 : 0));
}

static void boundaries_784(void) {
  /* the cargo sum against 5000 (signed 32-bit): a message for the small sum, the hit above */
  static const uint32_t sums[] = {0, 4999, 5000, 5001, 5002, 0xffffffffu, 0x7fffffffu, 0x80000000u, 9800, 11000, 9799, 5000 + 1200, 5000 + 1199, 5000 + 1201};
  for (unsigned k = 0; k < sizeof sums / sizeof sums[0]; k++)
    for (unsigned var = 0; var < 3; var++) {
      base_784();
      set_cargo(g_fl[0], sums[k]);
      if (var == 1) {
        uint8_t f = 0x20;
        memcpy(LIN(FP_AT(g_fl[0], 5)), &f, 1);
      }
      if (var == 2) set_levels(0, 20);
      DIFF(33, 0x0784, FAR_CDECL, 1);
    }
  /* a fleet that is not on the object, or already flagged (bit 2), is left alone */
  base_784();
  set_cargo(g_fl[0], 6000);
  wr16(FP_AT(g_fl[0], 8), 1101);
  DIFF(33, 0x0784, FAR_CDECL, 1);
  wr16(FP_AT(g_fl[0], 8), 1100);
  wr16(FP_AT(g_fl[0], 10), 1101);
  DIFF(33, 0x0784, FAR_CDECL, 1);
  wr16(FP_AT(g_fl[0], 10), 1100);
  {
    uint8_t f = 4;
    memcpy(LIN(FP_AT(g_fl[0], 5)), &f, 1);
  }
  DIFF(33, 0x0784, FAR_CDECL, 1);
  /* the sum of the levels against the steps 0x3c, 0x48, 0x54, 0x60, 0x6c (hit, levels not all high) */
  for (unsigned sum = 0x38; sum <= 0x70; sum++) {
    if (!(sum == 0x3b || sum == 0x3c || sum == 0x47 || sum == 0x48 || sum == 0x53 || sum == 0x54 || sum == 0x5f ||
          sum == 0x60 || sum == 0x6b || sum == 0x6c || sum == 0x6d || sum == 0x38))
      continue;
    for (unsigned cs = 0; cs < 2; cs++) {
      base_784();
      set_cargo(g_fl[0], cs ? 12000 : 5000 + 1200 * 2);
      set_levels(0, sum);
      DIFF(33, 0x0784, FAR_CDECL, 1);
    }
  }
  /* the level threshold: 26 (bit 1 of DS:0x5AB8 clear) or 10 (set), at and below it */
  for (unsigned bitset = 0; bitset < 2; bitset++)
    for (unsigned lv = 8; lv < 28; lv++) {
      base_784();
      PB(0, 0x54) = (uint8_t)(bitset ? 2 : 0);
      set_cargo(g_fl[0], 7000);
      for (unsigned l = 0; l < 6; l++) PB(0, 0x1a + l) = (uint8_t)lv;
      if (lv & 1) PB(0, 0x1a + 5) = (uint8_t)(lv - 1);
      DIFF(33, 0x0784, FAR_CDECL, 1);
      PW(0, 0x52) = 0x1fff; /* every flag already set: the destroy branch */
      PB(0, 7) = 0x08;
      DIFF(33, 0x0784, FAR_CDECL, 1);
    }
  /* object word +0xE (a mask of allowed flags) against the owner's flag word */
  for (unsigned var = 0; var < 6; var++) {
    base_784();
    set_cargo(g_fl[0], 7000);
    for (unsigned l = 0; l < 6; l++) PB(0, 0x1a + l) = 30;
    PW(0, 0x52) = (uint16_t)(var < 3 ? 0x0f0f : 0);
    wr16(FP_AT(g_ob[0], 0xe), (uint16_t)(var == 0 ? 0 : var == 1 ? 0x0f00 : var == 2 ? 0x00f0 : var == 3 ? 0x1fff : 0x0001));
    DIFF(33, 0x0784, FAR_CDECL, 1);
    wr16(FP_AT(g_ob[0], 0xc), 1); /* player 0 already hit this object */
    DIFF(33, 0x0784, FAR_CDECL, 1);
  }
  /* planets: x distance 100 / 101, squared distance 10000 / 10001, the cargo against 3500 / 5000 */
  static const struct { int dx, dy; } pd[] = {{100, 0}, {101, 0}, {99, 0}, {0, 100}, {0, 101}, {60, 80}, {60, 81}, {-100, 0}, {-101, 0}, {0, -100}, {80, 60}, {-60, 80}};
  for (unsigned k = 0; k < sizeof pd / sizeof pd[0]; k++)
    for (unsigned stat = 0; stat < 4; stat++) {
      base_784();
      wr16(FP_AT(g_pl[2], 2), 0);
      uint8_t f5 = 2;
      memcpy(LIN(FP_AT(g_pl[2], 5)), &f5, 1);
      DS_W(0x31ba + 8) = (uint16_t)(1100 + pd[k].dx);
      DS_W(0x31ba + 10) = (uint16_t)(1100 + pd[k].dy);
      PB(0, 7) = (uint8_t)(stat == 0 ? 0x0a : stat == 1 ? 0x1e : stat == 2 ? 0x06 : 0x02);
      wr32(FP_AT(g_pl[2], 0x1c), 2000);
      wr32(FP_AT(g_pl[2], 0x20), 1500);
      wr32(FP_AT(g_pl[2], 0x24), 1500);
      DIFF(33, 0x0784, FAR_CDECL, 1);
    }
  /* a planet more than 100 east of the object ends the planet loop (the table is x-sorted in the
   * game): an eligible planet behind it is only reached when the x distance is 100 or less */
  static const int far_dx[] = {99, 100, 101, 102, 200, -101, -300};
  for (unsigned k = 0; k < sizeof far_dx / sizeof far_dx[0]; k++) {
    base_784();
    for (unsigned i = 2; i < 4; i++) {
      wr16(FP_AT(g_pl[i], 2), 0);
      uint8_t f5 = 2;
      memcpy(LIN(FP_AT(g_pl[i], 5)), &f5, 1);
      wr32(FP_AT(g_pl[i], 0x1c), 2000);
      wr32(FP_AT(g_pl[i], 0x20), 2000);
      wr32(FP_AT(g_pl[i], 0x24), 2000);
    }
    DS_W(0x31ba + 8) = (uint16_t)(1100 + far_dx[k]);
    DS_W(0x31ba + 10) = 1100;
    DS_W(0x31ba + 12) = 1100;
    DS_W(0x31ba + 14) = 1100;
    DIFF(33, 0x0784, FAR_CDECL, 1);
  }
  for (unsigned stat = 0; stat < 2; stat++)
    for (int delta = -2; delta <= 2; delta++) {
      base_784();
      uint32_t t = stat ? 0x1388 : 0xdac;
      wr16(FP_AT(g_pl[2], 2), 0);
      uint8_t f5 = 2;
      memcpy(LIN(FP_AT(g_pl[2], 5)), &f5, 1);
      DS_W(0x31ba + 8) = 1100;
      DS_W(0x31ba + 10) = 1100;
      PB(0, 7) = (uint8_t)(stat ? 0x1e : 0x0a);
      wr32(FP_AT(g_pl[2], 0x1c), t / 4);
      wr32(FP_AT(g_pl[2], 0x20), t / 4);
      wr32(FP_AT(g_pl[2], 0x24), (uint32_t)((int32_t)t - 2 * (int32_t)(t / 4) + delta));
      DIFF(33, 0x0784, FAR_CDECL, 1);
      for (unsigned l = 0; l < 6; l++) PB(0, 0x1a + l) = 5;
      set_levels(0, 50);
      DIFF(33, 0x0784, FAR_CDECL, 1);
      set_levels(0, 53 + (unsigned)(delta + 2)); /* around 6 * 9 = 54 and 6 * 25 = 150 */
      DIFF(33, 0x0784, FAR_CDECL, 1);
      PB(2 * 0 + 0, 0x54) = 2;
      DIFF(33, 0x0784, FAR_CDECL, 1);
    }
}


/* Negative planet cargo dwords (Planet +0x1C/+0x20/+0x24, signed 32-bit) through the take loop at
 * 0x1131-0x1176, by both routes into it (the threshold path through 0x10cb, and the object-flag
 * path 0x102e -> 0x1109 that starts from the cargo sum). The aim was to leave the loop by the
 * "cx < 0" test at 0x1131-0x1136 (-> 0x1138); that exit is not reachable: the remaining amount
 * after the three takes is start - (sum of the takes), which is 0 when a take was capped and
 * start - sum <= 0 when none was (start = sum, or the threshold with sum >= threshold), so the
 * loop always breaks at 0x1172 first. Replacing "cx < 0" by "cx < -1" in src/seg33.c is not
 * detected (checked). These scenarios stay as the negative-dword regression. */
static void negative_cargo_784(void) {
  static const int32_t tri[][3] = {{-1000, 4000, 1000}, {-1, 1750, 1750}, {-2000, 3000, 3000}, {1000, -500, 4500},
                                   {2000, 2000, -1}, {-0x40000000, -0x40000000, -0x40000000}, {0x7fffffff, -0x7ffff000, 3000}};
  for (unsigned k = 0; k < sizeof tri / sizeof tri[0]; k++)
    for (unsigned stat = 0; stat < 2; stat++)
      for (unsigned route = 0; route < 2; route++) {
        base_784();
        wr16(FP_AT(g_pl[2], 2), 0);
        uint8_t f5 = 2;
        memcpy(LIN(FP_AT(g_pl[2], 5)), &f5, 1);
        DS_W(0x31ba + 8) = 1100;
        DS_W(0x31ba + 10) = 1100;
        PB(0, 7) = (uint8_t)(stat ? 0x1e : 0x0a);
        wr32(FP_AT(g_pl[2], 0x1c), (uint32_t)tri[k][0]);
        wr32(FP_AT(g_pl[2], 0x20), (uint32_t)tri[k][1]);
        wr32(FP_AT(g_pl[2], 0x24), (uint32_t)tri[k][2]);
        if (route) wr16(FP_AT(g_ob[0], 0xe), 0x0001);
        DIFF(33, 0x0784, FAR_CDECL, 1);
      }
}

static void t_0784(void) {
  world_784();
  DIFF(33, 0x0784, FAR_CDECL, 0);
  for (unsigned it = 0; it < 300; it++) {
    poke_784(it);
    if (getenv("T33_TRACE")) fprintf(stderr, "0784 iteration %u\n", it);
    DIFF(33, 0x0784, FAR_CDECL, 1);
  }
  boundaries_784();
  negative_cargo_784();
}

/* T33_ONLY=0784 (a function offset) runs one function's scenarios only; used for mutation checks. */
static bool want(const char *off) {
  const char *only = getenv("T33_ONLY");
  return only == NULL || strstr(only, off) != NULL;
}

/* The CRT sqrt stand-in changes the control word around FSQRT like 34:0x2068 and restores it. */
static void t_crt_sqrt_cw(void) {
  static const uint16_t cws[] = {0x037f, 0x1330, 0x027f, 0x0c7f};
  for (unsigned i = 0; i < sizeof cws / sizeof cws[0]; i++) {
    fp80_set_cw(cws[i]);
    Fp80 r = crt_sqrt_34_0d68(fp80_from_i32(144));
    CHECK_EQ(fp80_get_cw(), cws[i]);
    CHECK_EQ(fp80_to_i32_ftol(r), 12);
  }
  fp80_set_cw(FPU_CW_DEFAULT);
}

int main(void) {
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
  if (want("0742")) t_0742();
  if (want("1194")) t_1194();
  if (want("0000")) t_0000();
  if (want("015a")) t_015a();
  if (want("0456")) t_0456();
  if (want("01a0")) t_01a0();
  t_crt_sqrt_cw();
  if (want("02d6")) t_02d6();
  if (want("0784")) t_0784();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
