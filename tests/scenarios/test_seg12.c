/* Diff tests for decompiled segment 12 (src/seg12.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 12 is the game's own heap manager on top of KERNEL.GlobalAlloc: twelve heaps, each a
 * linked list of global-memory segments (DGROUP 0x0D74 holds the list heads, 0x0DA4 the minimum
 * segment size per heap), with first-fit allocation inside a segment. Every scenario first builds
 * a heap state with the ORIGINAL code (harness_call), then DIFF snapshots it and compares the
 * original with the C version. The KERNEL memory handlers must be registered (kernel_register)
 * because the functions call GlobalAlloc / GlobalLock / GlobalReAlloc / GlobalUnlock / GlobalFree.
 *
 * Offsets: 0000 new segment, 0098 grow segment, 01b0 free a whole heap chain, 01fc reset a heap's
 * segments, 0232 allocate, 0372 find the segment holding a pointer, 03ec reallocate, 04d0 free,
 * 0528 resize a typed array, 0568 allocate a typed array, 05be free a typed array. */
#include <stdint.h>
#include <stdio.h>

#include "harness/difftest.h"
#include "harness/kernel_thunks.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL12 MEM_SEL_SEG(12)
#define HEAD(h) (*(uint32_t *)LINN((uint16_t)(0x0D74 + 4 * (h))))

static void heaps_clear(void) {
  for (int h = 0; h < 12; h++) HEAD(h) = 0;
}

/* Runs original code of segment 12 (setup, not under test). */
static uint32_t orig(uint16_t off, const uint16_t *args, int n) {
  return harness_call(SEL12, off, HCONV_CDECL, args, n, NULL);
}
static uint32_t o_alloc(uint16_t size, uint16_t heap) {
  uint16_t a[] = {size, heap};
  return orig(0x0232, a, 2);
}
static void o_free(uint32_t p, uint16_t heap) {
  uint16_t a[] = {(uint16_t)p, (uint16_t)(p >> 16), heap};
  orig(0x04d0, a, 3);
}
static uint32_t o_typed(uint16_t a, uint16_t b, uint16_t heap) {
  uint16_t w[] = {a, b, heap};
  return orig(0x0568, w, 3);
}
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))
/* Word at byte offset `off` of the HeapSeg header at far pointer hs (0 free_total, 2 seg_size,
 * 4 tail_free, 8/10 next). */
static uint16_t hs_word(uint32_t hs, uint16_t off) {
  return *(uint16_t *)LIN(MK_FP(HI(hs), (uint16_t)(LO(hs) + off)));
}

/* ---- 12:0x0372 find the heap segment that contains a pointer ---- */
static void t_0372(void) {
  heaps_clear();
  uint32_t p1 = o_alloc(0x40, 3);   /* segment S1 of heap 3 */
  uint32_t p2 = o_alloc(0xFC0, 3);  /* does not fit S1: segment S2, new list head */
  /* inside S1 (second in the list), inside S2 (first), on the block start, beyond the end */
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 3);
  DIFF(12, 0x0372, FAR_CDECL, LO(p2), HI(p2), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0x0010, HI(p1), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0x0011, HI(p1), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0x0012, HI(p1), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0xFFFF, HI(p1), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0x0012, HI(p2), 3);
  DIFF(12, 0x0372, FAR_CDECL, 0x0000, HI(p2), 3);
  /* pointer offset exactly at segment offset + seg_size: one past the end, not inside */
  {
    uint32_t h2 = HEAD(3);
    uint32_t h1 = ((uint32_t)hs_word(h2, 10) << 16) | hs_word(h2, 8);
    DIFF(12, 0x0372, FAR_CDECL, (uint16_t)(LO(h2) + hs_word(h2, 2)), HI(h2), 3);
    DIFF(12, 0x0372, FAR_CDECL, (uint16_t)(LO(h2) + hs_word(h2, 2) - 1), HI(h2), 3);
    DIFF(12, 0x0372, FAR_CDECL, (uint16_t)(LO(h1) + hs_word(h1, 2)), HI(h1), 3);
    DIFF(12, 0x0372, FAR_CDECL, (uint16_t)(LO(h1) + hs_word(h1, 2) - 1), HI(h1), 3);
  }
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1) + 8, 3); /* a segment above both */
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1) - 8, 3); /* a segment below both */
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 4);     /* empty heap */
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 0xFFFF); /* heap -1 */
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 12);
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 0x8000);
  DIFF(12, 0x0372, FAR_CDECL, LO(p1), HI(p1), 0);
  DIFF_FUZZ(12, 0x0372, FAR_CDECL, 3, 3000, 0x5EED0C01u);
  heaps_clear();
}

/* ---- 12:0x01fc reset every segment of a heap to empty ---- */
static void t_01fc(void) {
  heaps_clear();
  (void)o_alloc(0x40, 3);
  (void)o_alloc(0xFC0, 3);
  (void)o_alloc(0x20, 3);
  (void)o_alloc(0x30, 6);
  DIFF(12, 0x01fc, FAR_CDECL, 3);
  DIFF(12, 0x01fc, FAR_CDECL, 6);
  DIFF(12, 0x01fc, FAR_CDECL, 9); /* empty heap */
  heaps_clear();
}

/* ---- 12:0x0000 new segment ---- */
static void t_0000(void) {
  heaps_clear();
  DIFF(12, 0x0000, FAR_CDECL, 0x0040, 3);   /* below the heap's minimum: the minimum is used */
  DIFF(12, 0x0000, FAR_CDECL, 0x2000, 3);   /* above the minimum */
  DIFF(12, 0x0000, FAR_CDECL, 0x0000, 0);
  DIFF(12, 0x0000, FAR_CDECL, 0x0100, 10);
  (void)o_alloc(0x10, 3);                    /* head exists: the new segment links to it */
  DIFF(12, 0x0000, FAR_CDECL, 0x0100, 3);
  DIFF(12, 0x0000, FAR_CDECL, 0xFFF0, 3);   /* size + 0x10 wraps to 0: the minimum is used */
  heaps_clear();
}

/* ---- 12:0x0232 allocate ---- */
static void t_0232(void) {
  heaps_clear();
  DIFF(12, 0x0232, FAR_CDECL, 0x0040, 3);   /* empty heap: creates a segment through 0000 */
  DIFF(12, 0x0232, FAR_CDECL, 0x0001, 3);   /* rounding: 1 + 3 & ~1 = 4 */
  DIFF(12, 0x0232, FAR_CDECL, 0x0000, 3);
  (void)o_alloc(0x40, 3);
  (void)o_alloc(0x40, 3);
  (void)o_alloc(0x40, 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0040, 3);   /* tail fast path */
  DIFF(12, 0x0232, FAR_CDECL, 0x0F00, 3);   /* fits the segment tail */
  DIFF(12, 0x0232, FAR_CDECL, 0x0F80, 3);   /* does not fit: a second segment (no free blocks) */
  /* Free list inside the segment: make the tail too small, then ask for a freed size. */
  heaps_clear();
  uint32_t big[3];
  big[0] = o_alloc(0x400, 3);
  big[1] = o_alloc(0x400, 3);
  big[2] = o_alloc(0x400, 3);                 /* tail now 0x3e6 bytes: less than 0x400 */
  o_free(big[1], 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0400, 3);   /* scan finds the freed middle block */
  DIFF(12, 0x0232, FAR_CDECL, 0x0200, 3);   /* the same block is also big enough for less */
  DIFF(12, 0x0232, FAR_CDECL, 0x0410, 3);   /* too big for the hole, tail too small: new segment */
  /* Merge adjacent free blocks: free the first one too, ask for more than either. */
  o_free(big[0], 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0800, 3);   /* needs the two merged blocks */
  DIFF(12, 0x0232, FAR_CDECL, 0x0801, 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x07FE, 3);
  /* Three free in a row, last block allocated. */
  heaps_clear();
  uint32_t q[5];
  for (int i = 0; i < 5; i++) q[i] = o_alloc(0x300, 3);   /* 5 * 0x302 = 0xf0a: tail 0xe6 */
  o_free(q[0], 3);
  o_free(q[1], 3);
  o_free(q[3], 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0600, 3);   /* merged q0+q1 */
  DIFF(12, 0x0232, FAR_CDECL, 0x0300, 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0302, 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0900, 3);   /* more than any hole: new segment */
  /* A second segment already in the list: first segment too full, search continues. */
  (void)o_alloc(0x0F00, 3);                  /* cannot fit S1: S2 is created at the head */
  DIFF(12, 0x0232, FAR_CDECL, 0x0050, 3);   /* fits S2 */
  DIFF(12, 0x0232, FAR_CDECL, 0x0F50, 3);   /* fits neither */
  DIFF(12, 0x0232, FAR_CDECL, 0x0300, 3);   /* fits S2 tail, free holes in S1 not reached */
  /* free_total smaller than the request skips a segment without scanning it */
  heaps_clear();
  (void)o_alloc(0x0F80, 3);
  DIFF(12, 0x0232, FAR_CDECL, 0x0200, 3);
  /* a request exactly equal to free_total / tail_free (no holes: both are the same number) */
  heaps_clear();
  (void)o_alloc(0x40, 3);
  {
    uint32_t h = HEAD(3);
    uint16_t ft = hs_word(h, 0), tf = hs_word(h, 4);
    DIFF(12, 0x0232, FAR_CDECL, (uint16_t)(ft - 3), 3);   /* sz == free_total: fits the tail */
    DIFF(12, 0x0232, FAR_CDECL, (uint16_t)(tf - 3), 3);   /* sz == tail_free: tail fast path */
    DIFF(12, 0x0232, FAR_CDECL, (uint16_t)(tf - 1), 3);   /* sz == tail_free + 2: new segment */
  }
  /* with a hole: free_total > tail_free, request == tail_free exactly */
  heaps_clear();
  {
    uint32_t b0 = o_alloc(0x40, 3), b1 = o_alloc(0x40, 3);
    (void)b1;
    (void)o_alloc(0x40, 3);
    o_free(b0, 3);
    uint32_t h = HEAD(3);
    uint16_t tf = hs_word(h, 4);
    DIFF(12, 0x0232, FAR_CDECL, (uint16_t)(tf - 3), 3);
    DIFF(12, 0x0232, FAR_CDECL, (uint16_t)(hs_word(h, 0) - 3), 3);
  }
  /* A heap with a large minimum (0): bigger segment */
  heaps_clear();
  DIFF(12, 0x0232, FAR_CDECL, 0x0100, 0);
  heaps_clear();
}

/* ---- 12:0x04d0 free ---- */
static void t_04d0(void) {
  heaps_clear();
  uint32_t p1 = o_alloc(0x40, 3), p2 = o_alloc(0x30, 3);
  DIFF(12, 0x04d0, FAR_CDECL, 0, 0, 3);                     /* null pointer */
  DIFF(12, 0x04d0, FAR_CDECL, LO(p1), HI(p1), 3);           /* not last: only the flag */
  DIFF(12, 0x04d0, FAR_CDECL, LO(p2), HI(p2), 3);           /* last: gives back the tail */
  uint32_t p3 = o_alloc(0xFC0, 3);                           /* second segment */
  DIFF(12, 0x04d0, FAR_CDECL, LO(p3), HI(p3), 3);
  DIFF(12, 0x04d0, FAR_CDECL, LO(p1), HI(p1), 3);           /* in the older segment (list walk) */
  heaps_clear();
}

/* ---- 12:0x01b0 free a whole heap chain ---- */
static void t_01b0(void) {
  heaps_clear();
  DIFF(12, 0x01b0, FAR_CDECL, 0, 0);                        /* null */
  (void)o_alloc(0x40, 3);
  uint32_t head = HEAD(3);
  DIFF(12, 0x01b0, FAR_CDECL, LO(head), HI(head));          /* one segment */
  (void)o_alloc(0xFC0, 3);
  (void)o_alloc(0xFC0, 3);
  head = HEAD(3);
  DIFF(12, 0x01b0, FAR_CDECL, LO(head), HI(head));          /* three segments */
  heaps_clear();
}

/* ---- 12:0x0098 grow a segment ---- */
static void t_0098(void) {
  heaps_clear();
  DIFF(12, 0x0098, FAR_CDECL, 0, 0);                        /* null */
  (void)o_alloc(0x40, 3);
  uint32_t s1 = HEAD(3);
  DIFF(12, 0x0098, FAR_CDECL, LO(s1), HI(s1));              /* the list head: grows by the minimum */
  (void)o_alloc(0xFC0, 3);
  uint32_t s2 = HEAD(3);
  DIFF(12, 0x0098, FAR_CDECL, LO(s1), HI(s1));              /* second in the list: list walk */
  DIFF(12, 0x0098, FAR_CDECL, LO(s2), HI(s2));
  /* near the 64 KB limit the growth is clamped to 0xffdc - size */
  heaps_clear();
  (void)o_alloc(0xF000, 3);
  s1 = HEAD(3);
  DIFF(12, 0x0098, FAR_CDECL, LO(s1), HI(s1));
  heaps_clear();
  (void)o_alloc(0x0100, 0);
  s1 = HEAD(0);
  DIFF(12, 0x0098, FAR_CDECL, LO(s1), HI(s1));              /* heap 0: minimum 0xf800 */
  heaps_clear();
}

/* ---- 12:0x03ec reallocate ---- */
static void t_03ec(void) {
  heaps_clear();
  uint32_t p1 = o_alloc(0x40, 4), p2 = o_alloc(0x40, 4);
  DIFF(12, 0x03ec, FAR_CDECL, LO(p2), HI(p2), 0x0010, 4);  /* shrink: unchanged */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p2), HI(p2), 0x003E, 4);  /* same size: unchanged */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p2), HI(p2), 0x0100, 4);  /* last block: grows in place */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p1), HI(p1), 0x0100, 4);  /* not last: moves */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p1), HI(p1), 0x0101, 4);  /* odd size, moves */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p1), HI(p1), 0x0041, 4);  /* odd size, moves */
  /* a grow that uses up the tail exactly (diff == tail_free), and a same-size realloc of a block
   * that is not the last one */
  {
    heaps_clear();
    uint32_t r1 = o_alloc(0x40, 4), r2 = o_alloc(0x40, 4);
    uint16_t tf = hs_word(HEAD(4), 4);
    DIFF(12, 0x03ec, FAR_CDECL, LO(r2), HI(r2), (uint16_t)(0x40 + tf), 4);
    DIFF(12, 0x03ec, FAR_CDECL, LO(r2), HI(r2), (uint16_t)(0x40 + tf + 2), 4); /* one step more */
    DIFF(12, 0x03ec, FAR_CDECL, LO(r1), HI(r1), 0x0040, 4);
  }
  /* heaps 3 and 10 hold one block per segment (at offset 0x12) and grow the segment instead of
   * moving the block */
  heaps_clear();
  uint32_t p3 = o_alloc(0x100, 3);
  DIFF(12, 0x03ec, FAR_CDECL, LO(p3), HI(p3), 0x0200, 3);  /* tail is big enough: in place */
  DIFF(12, 0x03ec, FAR_CDECL, LO(p3), HI(p3), 0x1000, 3);  /* tail too small: grows the segment */
  heaps_clear();
  uint32_t p10 = o_alloc(0x100, 10);
  DIFF(12, 0x03ec, FAR_CDECL, LO(p10), HI(p10), 0x1000, 10);
  /* a block that is last in its segment but heap is not 3 or 10: moves to another segment */
  heaps_clear();
  (void)o_alloc(0x3E0, 4);
  uint32_t p4 = o_alloc(0xB00, 4);
  DIFF(12, 0x03ec, FAR_CDECL, LO(p4), HI(p4), 0x0F00, 4);
  heaps_clear();
}

/* ---- 12:0x0568 allocate a typed array ---- */
static void t_0568(void) {
  heaps_clear();
  DIFF(12, 0x0568, FAR_CDECL, 4, 0x10, 3);
  DIFF(12, 0x0568, FAR_CDECL, 1, 1, 0);
  DIFF(12, 0x0568, FAR_CDECL, 0x23, 7, 5);
  DIFF(12, 0x0568, FAR_CDECL, 0x1FF, 2, 7);   /* element size above 8 bits: low byte stored */
  DIFF(12, 0x0568, FAR_CDECL, 0x1000, 0x20, 1); /* the 16-bit product wraps */
  DIFF(12, 0x0568, FAR_CDECL, 0xFFFF, 0xFFFF, 1); /* 0xFFFF * 0xFFFF must not overflow int */
  /* reuse of a freed block with garbage in the header bytes */
  uint32_t g = o_alloc(0x40, 2);
  *(uint16_t *)LIN(g) = 0xFFFF;
  *(uint16_t *)LIN(MK_FP(HI(g), (uint16_t)(LO(g) + 2))) = 0xFFFF;
  o_free(g, 2);
  DIFF(12, 0x0568, FAR_CDECL, 4, 0x10, 2);
  DIFF(12, 0x0568, FAR_CDECL, 0x11, 0x03, 6);
  heaps_clear();
}

/* ---- 12:0x0528 resize a typed array ---- */
static void t_0528(void) {
  heaps_clear();
  uint32_t t1 = o_typed(4, 0x10, 4), t2 = o_typed(4, 0x10, 4);
  DIFF(12, 0x0528, FAR_CDECL, LO(t2), HI(t2), 0x40);   /* last block: grows in place */
  DIFF(12, 0x0528, FAR_CDECL, LO(t1), HI(t1), 0x40);   /* not last: moves */
  DIFF(12, 0x0528, FAR_CDECL, LO(t1), HI(t1), 3);      /* shrinks: block kept */
  DIFF(12, 0x0528, FAR_CDECL, LO(t1), HI(t1), 0x10);   /* same count */
  heaps_clear();
  uint32_t u1 = o_typed(0x20, 5, 6), u2 = o_typed(0x20, 5, 6);
  DIFF(12, 0x0528, FAR_CDECL, LO(u1), HI(u1), 0x20);   /* heap 6, moves */
  DIFF(12, 0x0528, FAR_CDECL, LO(u2), HI(u2), 0x30);
  heaps_clear();
}

/* ---- 12:0x05be free a typed array ---- */
static void t_05be(void) {
  heaps_clear();
  DIFF(12, 0x05be, FAR_CDECL, 0, 0);
  uint32_t t1 = o_typed(4, 0x10, 3), t2 = o_typed(4, 0x10, 3);
  DIFF(12, 0x05be, FAR_CDECL, LO(t1), HI(t1));
  DIFF(12, 0x05be, FAR_CDECL, LO(t2), HI(t2));
  heaps_clear();
}

/* ---- random workload: each step is diffed, then applied with the original code ---- */
static uint32_t g_rng = 0x12345678u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static void t_stress(uint16_t heap, int steps) {
  uint32_t live[24];
  int n = 0;
  heaps_clear();
  for (int i = 0; i < steps; i++) {
    uint32_t op = rnd() % 8;
    if (n == 0 || (op < 4 && n < 24)) {                      /* allocate */
      uint16_t sz = (uint16_t)(1 + rnd() % (rnd() % 4 == 0 ? 3000 : 200));
      DIFF(12, 0x0232, FAR_CDECL, sz, heap);
      live[n++] = o_alloc(sz, heap);
    } else if (op < 6) {                                     /* free */
      int k = (int)(rnd() % (uint32_t)n);
      DIFF(12, 0x04d0, FAR_CDECL, LO(live[k]), HI(live[k]), heap);
      o_free(live[k], heap);
      live[k] = live[--n];
    } else {                                                 /* reallocate */
      int k = (int)(rnd() % (uint32_t)n);
      uint16_t sz = (uint16_t)(1 + rnd() % 1500);
      DIFF(12, 0x03ec, FAR_CDECL, LO(live[k]), HI(live[k]), sz, heap);
      uint16_t a[] = {LO(live[k]), HI(live[k]), sz, heap};
      live[k] = orig(0x03ec, a, 4);
    }
  }
  heaps_clear();
}

int main(void) {
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  t_01fc();
  t_0372();
  t_0000();
  t_0232();
  t_04d0();
  t_01b0();
  t_0098();
  t_03ec();
  t_0568();
  t_0528();
  t_05be();
  t_stress(4, 150);
  t_stress(7, 100);
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
