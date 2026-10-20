/* Expectations for the segmented memory model (runtime/mem.c), issue stars-dwf.1.
 * Do not edit an expectation to match the code: a disagreement is reported.
 */
#include "test.h"
#include "runtime/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARENA_BYTES (8u * 1024 * 1024)

static void test_layout(void) {
  CHECK_EQ(MEM_SEL_SEG(1), 0x0008);
  CHECK_EQ(MEM_SEL_SEG(9), 0x0048);
  CHECK_EQ(MEM_SEL_DGROUP, 0x0118);
  CHECK_EQ(MEM_SEL_SEG(35), MEM_SEL_DGROUP);
  CHECK_EQ(MEM_SEL_THUNK, 0x0120);
  CHECK_EQ(MEM_SEL_CALLBACK, 0x0128);
  CHECK_EQ(MEM_SEL_FIRST_DYNAMIC, 0x0200);
  CHECK_EQ(MEM_GDT_LINEAR, 0x10000);
  CHECK_EQ(MEM_BOOT_LINEAR, 0x500);
  CHECK_EQ(MEM_HEAP_LINEAR, 0x100000);
  CHECK_EQ(MEM_AHINCR, 8);
  CHECK_EQ(MEM_AHSHIFT, 3);
  CHECK(mem_arena() != NULL);
  CHECK_EQ(((uintptr_t)mem_arena()) % 4096, 0);
  CHECK_EQ(mem_size(), ARENA_BYTES);
  CHECK(mem_gdt() == mem_arena() + 0x10000);
}

static void test_descriptor(void) {
  static const uint8_t want[8] = {0xff, 0xff, 0x00, 0x00, 0x31, 0x93, 0x00, 0x00};
  desc_set(0x0018, 0x310000, 0xFFFF, 0x93);
  CHECK_MEM(mem_arena() + 0x10000 + 0x18, want, 8);
  CHECK_EQ(desc_base(0x0018), 0x310000);
  CHECK_EQ(desc_limit(0x0018), 0xFFFF);
  /* Low selector bits (TI/RPL) do not select a different descriptor. */
  CHECK_EQ(desc_base(0x001F), 0x310000);
  /* Base bits 24-31 and limit bits 16-19 land in the right fields. */
  desc_set(0x0020, 0x12345678, 0xABCDE, 0x93);
  CHECK_EQ(desc_base(0x0020), 0x12345678);
  CHECK_EQ(desc_limit(0x0020), 0xABCDE);
  CHECK_EQ(mem_gdt()[0x20 + 7], 0x12);
  CHECK_EQ(mem_gdt()[0x20 + 6], 0x0A);
  desc_set(0x0018, 0, 0, 0);
  desc_set(0x0020, 0, 0, 0);
}

static void test_lin(void) {
  uint16_t sel = MEM_SEL_SEG(9);
  desc_set(sel, 0x200000, 0x0FFF, 0x93);
  CHECK(LIN(MK_FP(sel, 0x10)) == (void *)(mem_arena() + 0x200000 + 0x10));
  CHECK_EQ(FP_SEG(MK_FP(0x1234, 0x5678)), 0x1234);
  CHECK_EQ(FP_OFF(MK_FP(0x1234, 0x5678)), 0x5678);
  CHECK_EQ(MK_FP(0x1234, 0x5678), 0x12345678u);
  CHECK(mem_check(MK_FP(sel, 0), 0x1000));
  CHECK(mem_check(MK_FP(sel, 0xFFF), 1));
  CHECK(!mem_check(MK_FP(sel, 0x1000), 1));
  CHECK(!mem_check(MK_FP(sel, 0xFFF), 2));
  CHECK(!mem_check(MK_FP(sel, 0), 0x1001));
  CHECK(!mem_check(MK_FP(0, 0), 1));                  /* null selector */
  CHECK(!mem_check(MK_FP(0x3000, 0), 1));             /* not present */
  /* No selector may cover the runtime region: a range reaching mem_rt_base()
   * is rejected, even though it is still inside the arena. */
  uint32_t rt = mem_rt_base();
  desc_set(sel, rt - 16, 0xFFFF, 0x93);
  CHECK(mem_check(MK_FP(sel, 0), 16));
  CHECK(mem_check(MK_FP(sel, 15), 1));
  CHECK(!mem_check(MK_FP(sel, 16), 1));
  CHECK(!mem_check(MK_FP(sel, 0), 17));
  CHECK(!mem_check(MK_FP(sel, 8), 9));
  desc_set(sel, rt, 0xFFFF, 0x93);       /* starts exactly at the boundary */
  CHECK(!mem_check(MK_FP(sel, 0), 4));
  CHECK(!mem_check(MK_FP(sel, 0), 1));
  desc_set(sel, rt + 4096, 0xFFFF, 0x93);
  CHECK(!mem_check(MK_FP(sel, 0), 1));
  /* A segment whose limit runs past the arena is rejected too. */
  desc_set(sel, ARENA_BYTES - 16, 0xFFFF, 0x93);
  CHECK(!mem_check(MK_FP(sel, 0), 1));
  desc_set(sel, 0, 0, 0);
}

static void test_selectors(void) {
  uint16_t a = sel_alloc(3);
  CHECK_EQ(a, MEM_SEL_FIRST_DYNAMIC);
  uint16_t b = sel_alloc(1);
  CHECK_EQ(b, a + 3 * 8);
  sel_free(a, 3);
  CHECK_EQ(sel_alloc(2), a);            /* freed run is reused */
  CHECK_EQ(sel_alloc(2), b + 8);        /* the 1-selector hole is too small */
  CHECK_EQ(sel_alloc(1), a + 2 * 8);    /* ... but fits 1 */
  CHECK_EQ(sel_alloc(9000), 0);         /* more than the table holds */
}

static void test_global_basic(void) {
  uint16_t h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 100);
  CHECK(h != 0);
  CHECK_EQ(GlobalSize(h), 112);         /* rounded up to 16 */
  uint32_t fp = GlobalLock(h);
  CHECK_EQ(fp, MK_FP(h, 0));
  CHECK_EQ(desc_limit(h), 111);
  uint8_t *p = LIN(fp);
  for (int i = 0; i < 100; i++) CHECK_EQ(p[i], 0);
  memset(p, 0xAB, 100);
  /* Locked twice: GlobalUnlock is true while still locked. */
  CHECK(GlobalLock(h) == fp);
  CHECK(GlobalUnlock(h));                /* one lock left */
  CHECK(!GlobalUnlock(h));               /* last lock released */
  CHECK(!GlobalUnlock(h));               /* not locked at all */
  /* Two blocks never overlap. */
  uint16_t h2 = GlobalAlloc(GMEM_FIXED, 100);
  CHECK(h2 != 0 && h2 != h);
  uint32_t b1 = desc_base(h), b2 = desc_base(h2);
  CHECK(b1 >= MEM_HEAP_LINEAR && b2 >= MEM_HEAP_LINEAR);
  CHECK(b1 + 112 <= b2 || b2 + 112 <= b1);
  CHECK_EQ(GlobalFree(h), 0);
  CHECK(!mem_check(MK_FP(h, 0), 1));    /* freed selector is gone */
  /* Dirty memory is zeroed again by GMEM_ZEROINIT on reuse. */
  memset(LIN(MK_FP(h2, 0)), 0xCD, 100);
  GlobalFree(h2);
  uint16_t h3 = GlobalAlloc(GMEM_ZEROINIT, 100);
  uint8_t *q = LIN(MK_FP(h3, 0));
  for (int i = 0; i < 100; i++) CHECK_EQ(q[i], 0);
  GlobalFree(h3);
  CHECK_EQ(GlobalFree(0x7770), 0x7770); /* invalid handle is returned */
  CHECK_EQ(GlobalAlloc(GMEM_FIXED, 0x7FFFFFFFu), 0); /* does not fit */
}

static void test_global_huge(void) {
  uint16_t h = GlobalAlloc(GMEM_ZEROINIT, 0x20010);
  CHECK(h != 0);
  uint16_t h1 = h + 8, h2 = h + 16;
  CHECK(mem_check(MK_FP(h, 0), 1));
  CHECK(mem_check(MK_FP(h1, 0), 1));
  CHECK(mem_check(MK_FP(h2, 0), 1));
  CHECK_EQ(desc_base(h1), desc_base(h) + 0x10000);
  CHECK_EQ(desc_base(h2), desc_base(h) + 0x20000);
  CHECK_EQ(desc_limit(h), 0xFFFF);
  CHECK_EQ(desc_limit(h1), 0xFFFF);
  CHECK_EQ(desc_limit(h2), 0x0F);
  CHECK(!mem_check(MK_FP(h2, 0x10), 1));
  CHECK(!mem_check(MK_FP(h + 24, 0), 1)); /* no fourth selector */
  *(uint8_t *)LIN(MK_FP(h1, 0)) = 0x5A;
  CHECK_EQ(*(mem_arena() + desc_base(h) + 0x10000), 0x5A);
  CHECK(GlobalSize(h) >= 0x20010);
  CHECK_EQ(GlobalFree(h), 0);
  CHECK(!mem_check(MK_FP(h1, 0), 1));
  CHECK(!mem_check(MK_FP(h2, 0), 1));
}

static void test_global_realloc(void) {
  uint16_t h = GlobalAlloc(GMEM_MOVEABLE, 100);
  uint8_t *p = LIN(MK_FP(h, 0));
  for (int i = 0; i < 100; i++) p[i] = (uint8_t)(i * 7 + 1);
  uint16_t r = GlobalReAlloc(h, 200000, GMEM_ZEROINIT);
  CHECK_EQ(r, h);                       /* handle kept */
  CHECK(GlobalSize(h) >= 200000);
  p = LIN(MK_FP(h, 0));
  int ok = 1;
  for (int i = 0; i < 100; i++) ok &= p[i] == (uint8_t)(i * 7 + 1);
  CHECK(ok);
  CHECK(mem_check(MK_FP(h + 24, 0), 1)); /* 4th selector, 200000 > 3*64K */
  CHECK_EQ(*(uint8_t *)LIN(MK_FP(h + 24, 0)), 0); /* grown part zeroed */
  CHECK_EQ(*(uint8_t *)LIN(MK_FP(h + 16, 0xFFFF)), 0);
  /* Shrink: contents preserved, surplus selectors released. */
  r = GlobalReAlloc(h, 50, 0);
  CHECK_EQ(r, h);
  CHECK(GlobalSize(h) >= 50 && GlobalSize(h) < 100);
  CHECK(!mem_check(MK_FP(h + 8, 0), 1));
  p = LIN(MK_FP(h, 0));
  CHECK_EQ(p[49], (uint8_t)(49 * 7 + 1));
  CHECK_EQ(GlobalFree(h), 0);
}

static void test_local(void) {
  desc_set(MEM_SEL_DGROUP, 0x400000, 0xFFFF, 0x93);
  /* docs/PLAN.md: static data 26248 + stack 16384 = 0xA688; heap 0xA688-0xC688. */
  mem_local_init(0xA688, 0xC688);
  CHECK(LINN(0x10) == (void *)(mem_arena() + 0x400000 + 0x10));
  CHECK(mem_check(MK_FP(MEM_SEL_DGROUP, 0xFFFF), 1));
  uint16_t a = LocalAlloc(LMEM_FIXED, 10);
  CHECK(a >= 0xA688 && a < 0xC688);
  uint16_t b = LocalAlloc(LMEM_FIXED, 10);
  CHECK(b >= a + 10);
  CHECK(LocalSize(a) >= 10);
  memset(LINN(a), 0xEE, 10);
  CHECK_EQ(LocalFree(a), 0);
  uint16_t c = LocalAlloc(LMEM_FIXED, 10);
  CHECK_EQ(c, a);                       /* same offset reused */
  /* LMEM_ZEROINIT zeroes reused dirty memory. */
  memset(LINN(c), 0xEE, 10);
  LocalFree(c);
  uint16_t z = LocalAlloc(LMEM_ZEROINIT, 10);
  CHECK_EQ(z, a);
  for (int i = 0; i < 10; i++) CHECK_EQ(((uint8_t *)LINN(z))[i], 0);
  /* Realloc preserves contents. */
  memcpy(LINN(z), "0123456789", 10);
  uint16_t r = LocalReAlloc(z, 5000, LMEM_ZEROINIT);
  CHECK(r != 0);
  CHECK_MEM(LINN(r), "0123456789", 10);
  CHECK(LocalSize(r) >= 5000);
  CHECK_EQ(((uint8_t *)LINN(r))[4999], 0);
  /* Allocations stay inside the heap and never overlap. */
  CHECK_EQ(LocalAlloc(LMEM_FIXED, 0xFFFF), 0);
  LocalFree(r);
  LocalFree(b);
  /* After freeing everything the whole heap is available again. */
  uint16_t big = LocalAlloc(LMEM_FIXED, 0xC688 - 0xA688 - 8);
  CHECK(big != 0);
  LocalFree(big);
}

static int covered_by_selector(uint32_t lin) {
  for (uint32_t i = 1; i < 8192; i++) {
    uint16_t sel = (uint16_t)(i << 3);
    if (!(mem_gdt()[i * 8 + 5] & 0x80)) continue;
    uint32_t base = desc_base(sel), lim = desc_limit(sel);
    if (lin >= base && lin <= base + lim) return 1;
  }
  return 0;
}

static void test_rt(void) {
  /* Make selectors exist so the check is meaningful. */
  uint16_t h = GlobalAlloc(GMEM_FIXED, 0x30000);
  void *p = rt_alloc(100);
  void *q = rt_alloc(200);
  CHECK(p != NULL && q != NULL && p != q);
  uint32_t lp = (uint32_t)((uint8_t *)p - mem_arena());
  uint32_t lq = (uint32_t)((uint8_t *)q - mem_arena());
  CHECK(lp >= mem_rt_base() && lq >= mem_rt_base());
  CHECK(lp + 100 <= mem_size() && lq + 200 <= mem_size());
  CHECK(!covered_by_selector(lp) && !covered_by_selector(lp + 99));
  CHECK(!covered_by_selector(lq) && !covered_by_selector(lq + 199));
  CHECK(lp + 100 <= lq || lq + 200 <= lp);
  memset(p, 1, 100);
  memset(q, 2, 200);
  rt_free(p);
  void *p2 = rt_alloc(100);
  CHECK(p2 == p);                       /* freed block is reused */
  CHECK_EQ(((uint8_t *)q)[199], 2);
  rt_free(p2);
  rt_free(q);
  GlobalFree(h);
}

static void test_local_realloc_moves(void) {
  mem_local_init(0xA688, 0xC688);
  uint16_t a = LocalAlloc(0, 4);
  uint16_t b = LocalAlloc(0, 100);
  CHECK(a != 0 && b != 0);
  for (int i = 0; i < 100; i++) ((uint8_t *)LINN(b))[i] = (uint8_t)(i + 1);
  CHECK_EQ(LocalFree(a), 0);
  uint16_t r = LocalReAlloc(b, 104, 0);
  CHECK(r != 0);
  int ok = 1;
  for (int i = 0; i < 100; i++) ok &= ((uint8_t *)LINN(r))[i] == (uint8_t)(i + 1);
  CHECK(ok);
  CHECK_EQ(((uint8_t *)LINN(r))[96], 0x61);
  CHECK_EQ(((uint8_t *)LINN(r))[99], 0x64);
  CHECK(LocalSize(r) >= 104);
  /* Same again when a used block follows, so the block must move. */
  mem_local_init(0xA688, 0xC688);
  a = LocalAlloc(0, 4);
  b = LocalAlloc(0, 100);
  uint16_t c = LocalAlloc(0, 8);
  for (int i = 0; i < 100; i++) ((uint8_t *)LINN(b))[i] = (uint8_t)(i + 1);
  memset(LINN(c), 0x77, 8);
  LocalFree(a);
  r = LocalReAlloc(b, 104, 0);
  CHECK(r != 0 && r != c);
  ok = 1;
  for (int i = 0; i < 100; i++) ok &= ((uint8_t *)LINN(r))[i] == (uint8_t)(i + 1);
  CHECK(ok);
  CHECK_MEM(LINN(c), "\x77\x77\x77\x77\x77\x77\x77\x77", 8);
  LocalFree(r);
  LocalFree(c);
}

static void test_global_overflow(void) {
  uint16_t g1 = GlobalAlloc(0, 100);
  uint16_t g2 = GlobalAlloc(0, 100);
  CHECK(g1 != 0 && g2 != 0);
  uint32_t size = GlobalSize(g1), base = desc_base(g1), lim = desc_limit(g1);
  CHECK_EQ(GlobalReAlloc(g1, 0xFFFFFFF0u, 0), 0);
  CHECK_EQ(GlobalSize(g1), size);
  CHECK_EQ(desc_base(g1), base);
  CHECK_EQ(desc_limit(g1), lim);
  CHECK_EQ(GlobalReAlloc(g1, 0xFFFF0001u, 0), 0);
  CHECK_EQ(GlobalSize(g1), size);
  CHECK_EQ(desc_base(g1), base);
  CHECK_EQ(desc_limit(g1), lim);
  CHECK_EQ(GlobalAlloc(0, 0xFFFFFFF0u), 0);
  CHECK_EQ(GlobalAlloc(0, 0xFFFF0001u), 0);
  CHECK_EQ(GlobalAlloc(0, 0xFFFFFFFFu), 0);
  uint16_t g3 = GlobalAlloc(0, 100);
  CHECK(g3 != 0);
  CHECK(desc_base(g3) >= MEM_HEAP_LINEAR);
  CHECK(desc_base(g3) != desc_base(g1) && desc_base(g3) != desc_base(g2));
  CHECK_EQ(GlobalFree(g1), 0);
  CHECK_EQ(GlobalFree(g2), 0);
  CHECK_EQ(GlobalFree(g3), 0);
}

static void test_rt_edges(void) {
  if (sizeof(size_t) > 4) {
    CHECK(rt_alloc((size_t)1 << 32) == NULL);
    CHECK(rt_alloc((size_t)0xFFFFFFF1u) == NULL);
  }
  CHECK(rt_alloc((size_t)-1) == NULL);
  /* rt_free aborts on a pointer outside the runtime region; not testable
   * without fork, so only the legal path is exercised here. */
  void *p = rt_alloc(1);
  CHECK(p != NULL);
  rt_free(p);
  rt_free(NULL);
}

int main(void) {
  mem_init(ARENA_BYTES);
  test_layout();
  test_descriptor();
  test_lin();
  test_selectors();
  mem_shutdown();
  mem_init(ARENA_BYTES);
  test_global_basic();
  test_global_huge();
  test_global_realloc();
  test_local();
  test_local_realloc_moves();
  test_global_overflow();
  test_rt_edges();
  test_rt();
  mem_shutdown();
  return TEST_DONE();
}
