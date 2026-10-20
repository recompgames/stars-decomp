/* Decompiled code segment 12 (NE segment 12, selector 0x0060). Local-only.
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg12.c; Ghidra's pseudo-C in re/decomp/seg12 is only a reading aid.
 *
 * Segment 12 is the game's heap manager on top of KERNEL.GlobalAlloc. There are 12 heaps. Heap n is
 * a linked list of global-memory segments (struct HeapSeg, src/stars.h): DS:0x0D74 + 4n holds the
 * far pointer to the first one, DS:0x0DA4 + 2n the minimum size of a new segment. Inside a segment
 * the blocks start at offset 0x10, each with a size word before its data (size, bit 0 = free), and
 * an untouched tail from tail_off on. Block pointers returned to the game point at the data.
 *   12:0x0000  heap_new_segment: allocate a global segment and link it at the head of a heap
 *   12:0x0098  heap_grow_segment: grow a segment with GlobalReAlloc and repair its list link
 *   12:0x01b0  heap_free_chain: unlock and free every global segment of a chain
 *   12:0x01fc  heap_reset: mark every segment of a heap empty again
 *   12:0x0232  heap_alloc: first-fit allocation of a block in a heap
 *   12:0x0372  heap_find_segment: the segment of a heap that contains a pointer
 *   12:0x03ec  heap_realloc: grow a block in place (heaps 3 and 10 grow the segment) or move it
 *   12:0x04d0  heap_free: mark a block free and give it back to the segment
 *   12:0x0528  typed_array_resize: reallocate a typed array to a new element count
 *   12:0x0568  typed_array_alloc: allocate a typed array (element size, count, heap in its header)
 *   12:0x05be  typed_array_free: free a typed array in the heap its header names
 * Names: Ghidra's are all generic (FUN_1058_<off>) and no function here has an export name or wraps
 * a single Win16 API, so the C names describe what each function does (CONVENTIONS rule f). A
 * "typed array" is a block from heap_alloc whose first word holds the element size (low byte) and
 * the heap number (bits 9-11) and whose byte 2 holds the low byte of the element count; the name is
 * descriptive, not the game's. The old fn_12_<off> names are kept as aliases in the comment above
 * each function.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n + 2 * pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI, and a mirrored callee is reached through
 * rt_call_far. 12:0x01fc (heap_reset) and 12:0x0372 (heap_find_segment) are leaves, so they
 * use ordinary C locals and may be called as plain C functions.
 *
 * Private helpers below read and write far memory in 16-bit pieces: an offset is a uint16_t and
 * wraps like the original's. */
#include <string.h>

#include "src/stars.h"

/* KERNEL global-memory calls are made by name (runtime/mem.h, CONVENTIONS rule e). In native
 * builds src/stars.h includes harness/byname.h, which logs each by-name call to the harness API
 * log exactly as the import thunk would, so the DIFF compares the ordered Win16 API calls. */

static inline uint8_t rd8(uint16_t seg, uint16_t off) { return *(uint8_t *)LIN(MK_FP(seg, off)); }
static inline void wr8(uint16_t seg, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(seg, off)) = v; }
static inline uint16_t rd16(uint16_t seg, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 2);
  return v;
}
static inline void wr16(uint16_t seg, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 2); }
static inline uint32_t rd32(uint16_t seg, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 4);
  return v;
}
/* DGROUP words (the heap tables). */
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline struct HeapSeg *hseg(uint16_t seg, uint16_t off) { return (struct HeapSeg *)LIN(MK_FP(seg, off)); }

/* Forward byte copy (REP MOVSW then REP MOVSB with DF clear), offsets wrap in 16 bits. */
static void copy_fwd(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) wr8(dseg, (uint16_t)(doff + i), rd8(sseg, (uint16_t)(soff + i)));
}

/* heap_new_segment: 12:0x0000 (Ghidra FUN_1058_0000, alias fn_12_0000).
 * 152 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * heap_new_segment(size, heap): FAR CDECL, size at [BP+6], heap at [BP+8]. Allocates a global
 * segment of max(size + 0x10, DS:0xDA4[heap]) bytes (GMEM_MOVEABLE | GMEM_ZEROINIT), locks it,
 * fills the HeapSeg header and links it at the head of the heap's list. Returns the far pointer to
 * the new HeapSeg (DX:AX). A failed GlobalAlloc shows the fatal error box (8:0x8d94) and throws to
 * the catch buffer at DS:0x6E (34:0x0825), which does not return into this function. */
DECOMP(12, 0x0000, heap_new_segment, FAR_CDECL)
uint32_t heap_new_segment(uint16_t size, int16_t heap)
{
  FRAME_ENTER(8 + 4);
  uint16_t di = (uint16_t)(size + 0x10);
  uint16_t minsz = drd16((uint16_t)(DS_HEAP_MIN + (uint16_t)((uint16_t)heap + (uint16_t)heap)));
  if (minsz > di) di = minsz;
  uint16_t h = GlobalAlloc(0x22, di);
  if (h == 0) {
    CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x1a, 0x10);
    CALL_ORIG(34, 0x0825, FAR_CDECL, 2, g_DAT_1110_006e, 0xffff);
  }
  uint32_t fp = GlobalLock(h);
  uint16_t seg = FP_SEG(fp), off = FP_OFF(fp);
  struct HeapSeg *hs = hseg(seg, off);
  hs->handle = h;
  hs->seg_size = di;
  hs->tail_free = (uint16_t)(di - 0x10);
  hs->free_total = (uint16_t)(di - 0x10);
  hs->tail_off = 0x10;
  hs->heap = (uint8_t)heap;
  uint16_t slot = (uint16_t)((uint16_t)heap << 2);
  hs->next = rd32(MEM_SEL_DGROUP, (uint16_t)(DS_HEAP_HEADS + slot));
  dwr16((uint16_t)(DS_HEAP_HEADS + slot), off);
  dwr16((uint16_t)(DS_HEAP_HEADS + slot + 2), seg);
  FRAME_LEAVE();
  return MK_FP(seg, off);
}

/* heap_grow_segment: 12:0x0098 (Ghidra FUN_1058_0098, alias fn_12_0098).
 * 279 bytes. ENTER 0xC,0; PUSH DI; PUSH SI.
 * heap_grow_segment(hs): FAR CDECL, the far pointer to a HeapSeg at [BP+6]. Returns 0 for a null
 * pointer. Otherwise grows the segment by the heap's minimum size (clamped so the total stays
 * below 0xFFDC) with GlobalUnlock + GlobalReAlloc(0x22) + GlobalLock, repairs the list link that
 * pointed at the old address, and adds the growth to seg_size, free_total and tail_free. A segment
 * already 0xFFDC bytes or more, or a failed GlobalReAlloc, takes the fatal error path of 12:0x0000
 * (and then, if it returned, locks the old handle). Returns the (new) far pointer. */
DECOMP(12, 0x0098, heap_grow_segment, FAR_CDECL)
uint32_t heap_grow_segment(uint32_t p)
{
  FRAME_ENTER(0xc + 4);
  if (p == 0) {
    FRAME_LEAVE();
    return 0;
  }
  uint16_t poff = FP_OFF(p), pseg = FP_SEG(p);
  uint16_t si = hseg(pseg, poff)->handle;
  uint16_t cx = hseg(pseg, poff)->seg_size;
  uint16_t grow = drd16((uint16_t)(DS_HEAP_MIN + (uint16_t)(2 * (uint16_t)hseg(pseg, poff)->heap)));
  bool failed = true;
  if (hseg(pseg, poff)->seg_size < 0xffdc) {
    if ((uint16_t)(0xffdc - grow) < cx) grow = (uint16_t)(0xffdc - cx);
    GlobalUnlock(si);
    uint16_t newsz = (uint16_t)(rd16(pseg, (uint16_t)(poff + 2)) + grow);
    si = GlobalReAlloc(si, newsz, 0x22);
    failed = (si == 0);
  }
  if (failed) {
    CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x1a, 0x10);
    CALL_ORIG(34, 0x0825, FAR_CDECL, 2, g_DAT_1110_006e, 0xffff);
  }
  uint32_t np = GlobalLock(si);
  uint16_t nseg = FP_SEG(np), nof = FP_OFF(np);
  hseg(nseg, nof)->handle = si;
  uint16_t slot = (uint16_t)(DS_HEAP_HEADS + (uint16_t)((uint16_t)hseg(nseg, nof)->heap << 2));
  if (drd16(slot) == poff && drd16((uint16_t)(slot + 2)) == pseg) {
    dwr16(slot, nof);
    dwr16((uint16_t)(slot + 2), nseg);
  } else {
    uint16_t dsp = drd16((uint16_t)(slot + 2));   /* segment of the predecessor */
    uint16_t sip = drd16(slot);                   /* offset of the predecessor */
    if ((dsp | sip) != 0) {
      for (;;) {
        uint32_t nx = rd32(dsp, (uint16_t)(sip + 8));
        if (nx == p) break;
        sip = FP_OFF(nx);
        dsp = FP_SEG(nx);
        if ((dsp | sip) == 0) break;
      }
    }
    wr16(dsp, (uint16_t)(sip + 8), nof);
    wr16(dsp, (uint16_t)(sip + 0xa), nseg);
  }
  struct HeapSeg *hs = hseg(nseg, nof);
  hs->seg_size = (uint16_t)(hs->seg_size + grow);
  hs->free_total = (uint16_t)(hs->free_total + grow);
  hs->tail_free = (uint16_t)(hs->tail_free + grow);
  FRAME_LEAVE();
  return MK_FP(nseg, nof);
}

/* heap_free_chain: 12:0x01b0 (Ghidra FUN_1058_01b0, alias fn_12_01b0).
 * 76 bytes. ENTER 6,0; PUSH DI; PUSH SI.
 * heap_free_chain(hs): FAR CDECL, far pointer to the first HeapSeg of a chain at [BP+6]. For each
 * segment of the chain: reads the next link and the handle, then GlobalUnlock + GlobalFree. */
DECOMP(12, 0x01b0, heap_free_chain, FAR_CDECL)
void heap_free_chain(uint32_t p)
{
  FRAME_ENTER(6 + 4);
  uint16_t si = FP_OFF(p), seg = FP_SEG(p);
  if ((seg | si) != 0) {
    do {
      uint16_t di = rd16(seg, (uint16_t)(si + 8));
      uint16_t nseg = rd16(seg, (uint16_t)(si + 0xa));
      uint16_t h = rd16(seg, (uint16_t)(si + 0xc));
      GlobalUnlock(h);
      GlobalFree(h);
      si = di;
      seg = nseg;
    } while ((seg | si) != 0);
  }
  FRAME_LEAVE();
}

/* heap_reset: 12:0x01fc (Ghidra FUN_1058_01fc, alias fn_12_01fc).
 * 53 bytes. PUSH BP; MOV BP,SP; PUSH SI. A leaf.
 * heap_reset(heap): FAR CDECL, heap number at [BP+6]. Marks every segment of the heap empty again:
 * tail_off = 0x10, tail_free = free_total = seg_size - 0x10. */
DECOMP(12, 0x01fc, heap_reset, FAR_CDECL)
void heap_reset(int16_t heap)
{
  uint16_t slot = (uint16_t)((uint16_t)heap << 2);
  uint16_t off = drd16((uint16_t)(DS_HEAP_HEADS + slot));
  uint16_t seg = drd16((uint16_t)(DS_HEAP_HEADS + slot + 2));
  while ((seg | off) != 0) {
    struct HeapSeg *hs = hseg(seg, off);
    hs->tail_off = 0x10;
    uint16_t ax = (uint16_t)(hs->seg_size - 0x10);
    hs->tail_free = ax;
    hs->free_total = ax;
    uint32_t nx = hs->next;
    off = FP_OFF(nx);
    seg = FP_SEG(nx);
  }
}

/* heap_alloc: 12:0x0232 (Ghidra FUN_1058_0232, alias fn_12_0232).
 * 319 bytes. ENTER 0x10,0; PUSH DI; PUSH SI.
 * heap_alloc(size, heap): FAR CDECL, size at [BP+6], heap at [BP+8]. First-fit allocation. The size
 * is rounded up to even (size + 3 & ~1: it includes the 2-byte size word). Each segment of the heap
 * is tried in list order: skipped when free_total < size; the fast path takes the block from the
 * never-used tail (tail_free >= size); otherwise the used part (offset 0x10 up to tail_off) is
 * scanned, and runs of free blocks (size word bit 0 set) are merged until one is big enough. With
 * no segment left a new one comes from 12:0x0000. Returns the far pointer to the data (block + 2).
 * Ghidra's output mixes the stack slots; the locals here are named after them: l_e = [BP-0xE]
 * (segment of the current HeapSeg), l_a = [BP-0xA], l_10 = [BP-0x10] (offset of the HeapSeg),
 * l_2 = [BP-2], l_c = [BP-0xC] (end of the used part), l_8/l_6 = [BP-8]/[BP-6] (start of the free
 * run). */
DECOMP(12, 0x0232, heap_alloc, FAR_CDECL)
uint32_t heap_alloc(uint16_t size, int16_t heap)
{
  FRAME_ENTER(0x10 + 4);
  uint16_t slot = (uint16_t)((uint16_t)heap << 2);
  uint16_t si = drd16((uint16_t)(DS_HEAP_HEADS + slot));
  uint16_t l_e = drd16((uint16_t)(DS_HEAP_HEADS + slot + 2));
  uint16_t sz = (uint16_t)((uint16_t)(size + 3) & 0xfffe);
  for (;;) {
    if ((l_e | si) != 0 && rd16(l_e, si) < sz) goto next_segment;
    if ((l_e | si) == 0) {
      uint16_t args[2] = {sz, (uint16_t)heap};
      uint32_t r = rt_call_far(MK_FP(SEG_SEL(12), 0x0000), FAR_CDECL, args, 2);
      si = FP_OFF(r);
      l_e = FP_SEG(r);
    }
    {
      uint16_t l_a = l_e;
      uint16_t cx = (uint16_t)(hseg(l_e, si)->tail_off + si);   /* end of the used part */
      if (hseg(l_e, si)->tail_free >= sz) {
        wr16(l_a, cx, (uint16_t)(sz - 2));
        struct HeapSeg *hs = hseg(l_e, si);
        hs->tail_off = (uint16_t)(hs->tail_off + sz);
        hs->free_total = (uint16_t)(hs->free_total - sz);
        hs->tail_free = (uint16_t)(hs->tail_free - sz);
        FRAME_LEAVE();
        return MK_FP(l_a, (uint16_t)(cx + 2));
      }
      uint16_t l_2 = l_e;
      uint16_t l_10 = si;
      uint16_t bx = (uint16_t)(si + 0x10);
      uint16_t l_c = cx;
      for (;;) {
        uint16_t di = l_c;
        if (bx >= di) break;                         /* scanned the used part: next segment */
        uint16_t l_8 = bx;
        uint16_t l_6 = l_2;
        uint16_t w = rd16(l_2, bx);
        bx = (uint16_t)(bx + (uint16_t)((w & 0xfffe) + 2));
        if ((w & 1) == 0) continue;                  /* allocated block */
        cx = l_8;
        if (bx < di) {
          for (;;) {                                 /* merge following free blocks */
            if ((rd8(l_2, bx) & 1) == 0) break;
            if ((uint16_t)(bx - cx) >= sz) break;
            uint16_t w2 = rd16(l_2, bx);
            bx = (uint16_t)(bx + (uint16_t)((w2 & 0xfffe) + 2));
            if (bx >= di) break;
          }
        }
        di = (uint16_t)((uint16_t)(bx - cx) - 2);
        wr16(l_6, cx, (uint16_t)(di | 1));
        uint16_t ax = (uint16_t)(di + 2);
        if (ax < sz) continue;                       /* run still too small: keep scanning */
        wr8(l_6, cx, (uint8_t)(rd8(l_6, cx) & 0xfe));
        struct HeapSeg *hs = hseg(l_e, l_10);
        hs->free_total = (uint16_t)(hs->free_total - ax);
        FRAME_LEAVE();
        return MK_FP(l_6, (uint16_t)(cx + 2));
      }
      si = l_10;
    }
  next_segment:
    {
      uint32_t nx = hseg(l_e, si)->next;
      si = FP_OFF(nx);
      l_e = FP_SEG(nx);
    }
  }
}

/* heap_find_segment: 12:0x0372 (Ghidra FUN_1058_0372, alias fn_12_0372).
 * 121 bytes. ENTER 4,0; PUSH DI; PUSH SI. A leaf.
 * heap_find_segment(off, seg, heap): FAR CDECL, pointer offset at [BP+6], selector at [BP+8],
 * heap number at [BP+0xA]. Walks the heap's segment list and returns the far pointer of the HeapSeg
 * that contains the pointer (same selector and block offset < off < offset + seg_size), or 0 when
 * the heap number is outside 0..11 or no segment matches. The unsigned compares follow the
 * original's JB/JA/JBE. */
DECOMP(12, 0x0372, heap_find_segment, FAR_CDECL)
uint32_t heap_find_segment(uint16_t off, uint16_t seg, int16_t heap)
{
  if (heap < 0 || heap >= 12) return 0;
  uint16_t slot = (uint16_t)((uint16_t)heap << 2);
  uint16_t bx = drd16((uint16_t)(DS_HEAP_HEADS + slot));
  uint16_t ds = drd16((uint16_t)(DS_HEAP_HEADS + slot + 2));
  if ((ds | bx) != 0) {
    for (;;) {
      if (seg < ds) goto next;
      if (seg == ds && off <= bx) goto next;
      {
        uint16_t end = (uint16_t)(rd16(ds, (uint16_t)(bx + 2)) + bx);
        if (ds > seg) break;
        if (ds < seg) goto next;
        if (end > off) break;
      }
    next:
      {
        uint32_t nx = rd32(ds, (uint16_t)(bx + 8));
        bx = FP_OFF(nx);
        ds = FP_SEG(nx);
        if ((ds | bx) == 0) break;
      }
    }
  }
  if ((ds | bx) == 0) return 0;
  return MK_FP(ds, bx);
}

/* heap_realloc: 12:0x03ec (Ghidra FUN_1058_03ec, alias fn_12_03ec).
 * 227 bytes. ENTER 0xA,0; PUSH DI; PUSH SI.
 * heap_realloc(p, size, heap): FAR CDECL, far pointer at [BP+6], new size at [BP+0xA], heap at
 * [BP+0xC]. The new size is rounded up to even. A block that does not grow (size <= its size word)
 * is returned as is. Otherwise, with the block's segment found by 12:0x0372: the block grows in
 * place when it ends exactly at tail_off and the tail is big enough (the segment header and the
 * size word are updated). Heaps 3 and 10 hold one block per segment, so a too-small tail makes
 * the segment grow (12:0x0098); the original then only re-points p at the grown segment plus
 * 0x12 (nothing is copied) before the test is repeated. Any other heap allocates a new block
 * (12:0x0232), copies the old contents, frees the old block (12:0x04d0) and returns the new
 * pointer. The original writes its result into [BP+6] and [BP+8]; here p is updated in place. */
DECOMP(12, 0x03ec, heap_realloc, FAR_CDECL)
uint32_t heap_realloc(uint32_t p, uint16_t size, int16_t heap)
{
  FRAME_ENTER(0xa + 4);
  uint16_t poff = FP_OFF(p), pseg = FP_SEG(p);
  uint16_t cur = rd16(pseg, (uint16_t)(poff - 2));
  uint16_t want = (uint16_t)((uint16_t)(size + 1) & 0xfffe);
  uint16_t diff = (uint16_t)(want - cur);
  if (want > cur) {
    uint32_t r = heap_find_segment(poff, pseg, heap);
    uint16_t si = FP_OFF(r), seg = FP_SEG(r);
    for (;;) {
      uint16_t ax = (uint16_t)(poff + cur);
      uint16_t cx = (uint16_t)(hseg(seg, si)->tail_off + si);
      if (cx == ax && seg == pseg && hseg(seg, si)->tail_free >= diff) {
        struct HeapSeg *hs = hseg(seg, si);
        hs->tail_free = (uint16_t)(hs->tail_free - diff);
        hs->free_total = (uint16_t)(hs->free_total - diff);
        hs->tail_off = (uint16_t)(hs->tail_off + diff);
        wr16(pseg, (uint16_t)(poff - 2), want);
        FRAME_LEAVE();
        return MK_FP(pseg, poff);
      }
      if (heap != 3 && heap != 10) break;
      uint16_t gargs[2] = {si, seg};
      uint32_t g = rt_call_far(MK_FP(SEG_SEL(12), 0x0098), FAR_CDECL, gargs, 2);
      si = FP_OFF(g);
      seg = FP_SEG(g);
      poff = (uint16_t)(si + 0x12);
      pseg = seg;
    }
    uint16_t aargs[2] = {want, (uint16_t)heap};
    uint32_t n = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, aargs, 2);
    copy_fwd(FP_SEG(n), FP_OFF(n), pseg, poff, cur);
    uint16_t fargs[3] = {poff, pseg, (uint16_t)heap};
    rt_call_far(MK_FP(SEG_SEL(12), 0x04d0), FAR_CDECL, fargs, 3);
    poff = FP_OFF(n);
    pseg = FP_SEG(n);
  }
  FRAME_LEAVE();
  return MK_FP(pseg, poff);
}

/* heap_free: 12:0x04d0 (Ghidra FUN_1058_04d0, alias fn_12_04d0).
 * 88 bytes. ENTER 4,0; PUSH DI; PUSH SI.
 * heap_free(p, heap): FAR CDECL, far pointer at [BP+6], heap at [BP+0xA]. A null pointer does
 * nothing. Otherwise the block is marked free (size word | 1), its size + 2 is added to the
 * segment's free_total (the segment comes from 12:0x0372), and when the block was the last one
 * before tail_off the tail takes it back (tail_off -= size, tail_free += size). */
DECOMP(12, 0x04d0, heap_free, FAR_CDECL)
void heap_free(uint32_t p, int16_t heap)
{
  FRAME_ENTER(4 + 4);
  if (p != 0) {
    uint16_t poff = FP_OFF(p), pseg = FP_SEG(p);
    uint32_t r = heap_find_segment(poff, pseg, heap);
    uint16_t si = FP_OFF(r), seg = FP_SEG(r);
    uint16_t di = (uint16_t)(rd16(pseg, (uint16_t)(poff - 2)) + 2);
    wr8(pseg, (uint16_t)(poff - 2), (uint8_t)(rd8(pseg, (uint16_t)(poff - 2)) | 1));
    struct HeapSeg *hs = hseg(seg, si);
    hs->free_total = (uint16_t)(hs->free_total + di);
    uint16_t ax = (uint16_t)(poff - si);
    ax = (uint16_t)(ax - hs->tail_off);
    ax = (uint16_t)(ax + di);
    if (ax == 2) {
      hs->tail_off = (uint16_t)(hs->tail_off - di);
      hs->tail_free = (uint16_t)(hs->tail_free + di);
    }
  }
  FRAME_LEAVE();
}

/* typed_array_resize: 12:0x0528 (Ghidra FUN_1058_0528, alias fn_12_0528).
 * 64 bytes. PUSH BP; MOV BP,SP; PUSH DI; PUSH SI.
 * typed_array_resize(p, count): FAR CDECL, far pointer at [BP+6], count at [BP+0xA]. p points at a
 * typed array made by 12:0x0568: its first word has the element size in the low byte and the heap
 * number in bits 9-11. Reallocates it (12:0x03ec) to element size * count + 4 bytes (16-bit
 * product), stores the low byte of count at offset 2 of the result, and returns it. */
DECOMP(12, 0x0528, typed_array_resize, FAR_CDECL)
uint32_t typed_array_resize(uint32_t p, uint16_t count)
{
  FRAME_ENTER(0 + 4);
  uint16_t w = rd16(FP_SEG(p), FP_OFF(p));
  uint16_t code = (uint16_t)((uint16_t)(w & 0xe00) >> 9);
  uint16_t sz = (uint16_t)((uint16_t)((w & 0xff) * count) + 4);
  uint16_t args[4] = {FP_OFF(p), FP_SEG(p), sz, code};
  uint32_t r = rt_call_far(MK_FP(SEG_SEL(12), 0x03ec), FAR_CDECL, args, 4);
  wr8(FP_SEG(r), (uint16_t)(FP_OFF(r) + 2), (uint8_t)count);
  FRAME_LEAVE();
  return r;
}

/* typed_array_alloc: 12:0x0568 (Ghidra FUN_1058_0568, alias fn_12_0568).
 * 85 bytes. ENTER 4,0; PUSH DI.
 * typed_array_alloc(elem, count, heap): FAR CDECL, element size at [BP+6], count at [BP+8], heap
 * at [BP+0xA]. Allocates elem * count + 4 bytes (16-bit product; 12:0x0232), stores the low byte
 * of count at offset 2 and 0 at offset 3, and builds the first word: low byte = element size,
 * high byte = what was there with bit 0 cleared, bits 9-11 set to heap & 7 (the code spreads this
 * as an XOR of the old high bits, so the result does not depend on them). Returns the far
 * pointer. */
DECOMP(12, 0x0568, typed_array_alloc, FAR_CDECL)
uint32_t typed_array_alloc(uint16_t elem, uint16_t count, int16_t heap)
{
  FRAME_ENTER(4 + 2);
  uint16_t args[2] = {(uint16_t)((uint32_t)count * elem + 4), (uint16_t)heap};
  uint32_t r = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, args, 2);
  uint16_t seg = FP_SEG(r), off = FP_OFF(r);
  wr8(seg, (uint16_t)(off + 2), (uint8_t)count);
  wr8(seg, (uint16_t)(off + 3), 0);
  uint16_t ax = rd16(seg, off);
  ax = (uint16_t)(ax & 0xfeff);
  wr16(seg, off, ax);
  ax = (uint16_t)((uint16_t)(rd8(seg, (uint16_t)(off + 1)) << 8) | (uint8_t)elem);
  wr16(seg, off, ax);
  uint16_t cx = ax;
  uint16_t di = (uint16_t)((uint16_t)heap << 9);
  ax = (uint16_t)(ax ^ (uint16_t)(di & 0xff00));
  ax = (uint16_t)(ax & 0xe00);
  cx = (uint16_t)(cx ^ ax);
  wr16(seg, off, cx);
  FRAME_LEAVE();
  return MK_FP(seg, off);
}

/* typed_array_free: 12:0x05be (Ghidra FUN_1058_05be, alias fn_12_05be).
 * 40 bytes. PUSH BP; MOV BP,SP; PUSH SI.
 * typed_array_free(p): FAR CDECL, far pointer at [BP+6]. A null pointer does nothing. Otherwise
 * the heap number is read from bits 9-11 of the first word (the high byte's bits 1-3) and the block
 * is freed with 12:0x04d0. */
DECOMP(12, 0x05be, typed_array_free, FAR_CDECL)
void typed_array_free(uint32_t p)
{
  FRAME_ENTER(0 + 2);
  if (p != 0) {
    uint16_t ax = (uint16_t)((uint16_t)rd8(FP_SEG(p), (uint16_t)(FP_OFF(p) + 1)) << 8);
    uint16_t args[3] = {FP_OFF(p), FP_SEG(p), (uint16_t)((uint16_t)(ax & 0xe00) >> 9)};
    rt_call_far(MK_FP(SEG_SEL(12), 0x04d0), FAR_CDECL, args, 3);
  }
  FRAME_LEAVE();
}
