/* Segmented memory model. See mem.h for the layout. */
#include "runtime/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mutable runtime state, kept inside the arena at the start of the runtime
 * heap so that a snapshot of the arena captures it. */
typedef struct {
  uint32_t sel_used[MEM_GDT_ENTRIES / 32]; /* bit per GDT index */
  uint32_t gblk_head;                      /* arena offset of first GBlk, 0 none */
  uint32_t rt_end;                         /* end of the runtime heap */
  uint16_t local_start, local_end;         /* local heap range in DGROUP */
} MemState;

typedef struct { /* runtime-heap block header, 16 bytes */
  uint32_t size; /* payload bytes, multiple of 16 */
  uint32_t used;
  uint32_t pad[2];
} RtHdr;

typedef struct { /* one per global block, sorted by base */
  uint32_t next;
  uint32_t base, size; /* size rounded to 16 */
  uint16_t sel, nsel;
  uint16_t lock, flags;
} GBlk;

typedef struct { /* local-heap block header, 4 bytes before the payload */
  uint16_t size; /* total bytes including header, multiple of 4 */
  uint16_t used;
} LHdr;

static uint8_t *g_arena;
static size_t g_size;
static uint32_t g_rt_base;

#define STATE_BYTES ((sizeof(MemState) + 15u) & ~15u)
#define ST() ((MemState *)(g_arena + g_rt_base))

static void die(const char *msg, uint32_t a, uint32_t b, uint32_t c) {
  fprintf(stderr, "mem: %s (%08x %08x %08x)\n", msg, a, b, c);
  abort();
}

void mem_init(size_t bytes) {
  if (bytes == 0) bytes = MEM_DEFAULT_ARENA;
  /* Arena offsets and descriptor bases are 32-bit. */
  if (bytes >= 0x100000000ull) die("arena size must be below 4 GB", (uint32_t)(bytes >> 32), (uint32_t)bytes, 0);
  if (bytes % 4096 != 0 || bytes <= MEM_HEAP_LINEAR * 2u) die("bad arena size", (uint32_t)bytes, 0, 0);
  if (g_arena) mem_shutdown();
  g_arena = aligned_alloc(4096, bytes);
  if (!g_arena) die("arena allocation failed", (uint32_t)bytes, 0, 0);
  memset(g_arena, 0, bytes);
  g_size = bytes;
  g_rt_base = (uint32_t)((bytes - bytes / 8) & ~4095u);
  MemState *st = ST();
  st->rt_end = (uint32_t)bytes;
  /* One free block covers the rest of the runtime heap. */
  RtHdr *h = (RtHdr *)(g_arena + g_rt_base + STATE_BYTES);
  h->size = (uint32_t)bytes - g_rt_base - (uint32_t)STATE_BYTES - sizeof(RtHdr);
  h->used = 0;
  /* Selector 0 is permanently taken, and so is every fixed-layout index up to
   * the last reserved one (index 63): dynamic selectors start at index 64. */
  for (unsigned i = 0; i < 64; i++) st->sel_used[i / 32] |= 1u << (i % 32);
}

void mem_shutdown(void) {
  free(g_arena);
  g_arena = NULL;
  g_size = 0;
  g_rt_base = 0;
}

uint8_t *mem_arena(void) { return g_arena; }
size_t mem_size(void) { return g_size; }
uint8_t *mem_gdt(void) { return g_arena + MEM_GDT_LINEAR; }
uint32_t mem_rt_base(void) { return g_rt_base; }
uint32_t mem_off(const void *p) { return (uint32_t)((const uint8_t *)p - g_arena); }

/* ---- descriptors ---- */

void desc_set(uint16_t sel, uint32_t base, uint32_t limit, uint8_t access) {
  uint8_t *d = mem_gdt() + (sel >> 3) * 8u;
  d[0] = limit & 0xFF;
  d[1] = (limit >> 8) & 0xFF;
  d[2] = base & 0xFF;
  d[3] = (base >> 8) & 0xFF;
  d[4] = (base >> 16) & 0xFF;
  d[5] = access;
  d[6] = (limit >> 16) & 0x0F; /* D=0, G=0 */
  d[7] = (base >> 24) & 0xFF;
}

uint32_t desc_base(uint16_t sel) {
  const uint8_t *d = mem_gdt() + (sel >> 3) * 8u;
  return d[2] | (d[3] << 8) | (d[4] << 16) | ((uint32_t)d[7] << 24);
}

uint32_t desc_limit(uint16_t sel) {
  const uint8_t *d = mem_gdt() + (sel >> 3) * 8u;
  return d[0] | (d[1] << 8) | ((uint32_t)(d[6] & 0x0F) << 16);
}

static bool sel_present(uint16_t sel) { return (mem_gdt()[(sel >> 3) * 8u + 5] & 0x80) != 0; }

static bool idx_used(unsigned i) { return (ST()->sel_used[i / 32] >> (i % 32)) & 1u; }
static void idx_set(unsigned i, bool v) {
  if (v) ST()->sel_used[i / 32] |= 1u << (i % 32);
  else ST()->sel_used[i / 32] &= ~(1u << (i % 32));
}

uint16_t sel_alloc(unsigned count) {
  if (count == 0 || count > MEM_GDT_ENTRIES) return 0;
  unsigned run = 0;
  for (unsigned i = MEM_SEL_FIRST_DYNAMIC >> 3; i < MEM_GDT_ENTRIES; i++) {
    run = idx_used(i) ? 0 : run + 1;
    if (run == count) {
      unsigned first = i - count + 1;
      for (unsigned k = 0; k < count; k++) idx_set(first + k, true);
      return (uint16_t)(first << 3);
    }
  }
  return 0;
}

void sel_free(uint16_t sel, unsigned count) {
  for (unsigned k = 0; k < count; k++) {
    unsigned i = (sel >> 3) + k;
    if (i >= MEM_GDT_ENTRIES) break;
    if (i >= (MEM_SEL_FIRST_DYNAMIC >> 3)) idx_set(i, false);
    desc_set((uint16_t)(i << 3), 0, 0, 0);
  }
}

/* ---- pointers ---- */

bool mem_check(uint32_t fp, uint32_t len) {
  uint16_t sel = FP_SEG(fp);
  if ((sel >> 3) == 0 || !g_arena || !sel_present(sel)) return false;
  uint64_t end = (uint64_t)FP_OFF(fp) + len;
  uint64_t lim = (uint64_t)desc_limit(sel) + 1;
  if (end > lim) return false;
  /* No selector may reach the runtime region (g_rt_base to the arena end). */
  return (uint64_t)desc_base(sel) + end <= g_rt_base;
}

void *LIN(uint32_t fp) {
  if (!mem_check(fp, 1)) {
    uint16_t sel = FP_SEG(fp);
    if (!g_arena) {
      fprintf(stderr, "mem: LIN before mem_init: selector %04x offset %04x\n", sel, FP_OFF(fp));
      abort();
    }
    fprintf(stderr, "mem: LIN out of bounds: selector %04x offset %04x limit %05x%s\n", sel, FP_OFF(fp),
            desc_limit(sel), sel_present(sel) ? "" : " (not present)");
    abort();
  }
  return g_arena + desc_base(FP_SEG(fp)) + FP_OFF(fp);
}

void *LINN(uint16_t near_off) { return LIN(MK_FP(MEM_SEL_DGROUP, near_off)); }

/* ---- runtime heap ---- */

static RtHdr *rt_first(void) { return (RtHdr *)(g_arena + g_rt_base + STATE_BYTES); }
static RtHdr *rt_next(RtHdr *h) {
  uint8_t *n = (uint8_t *)(h + 1) + h->size;
  return n >= g_arena + ST()->rt_end ? NULL : (RtHdr *)n;
}

void *rt_alloc(size_t bytes) {
  if (!g_arena || bytes > UINT32_MAX - 15) return NULL;
  uint32_t need = (uint32_t)((bytes + 15u) & ~15u);
  if (need == 0) need = 16;
  for (RtHdr *h = rt_first(); h; h = rt_next(h)) {
    if (h->used) continue;
    /* Merge following free blocks. */
    for (RtHdr *n = rt_next(h); n && !n->used; n = rt_next(h)) h->size += sizeof(RtHdr) + n->size;
    if (h->size < need) continue;
    if (h->size >= need + sizeof(RtHdr) + 16) {
      RtHdr *rest = (RtHdr *)((uint8_t *)(h + 1) + need);
      rest->size = h->size - need - (uint32_t)sizeof(RtHdr);
      rest->used = 0;
      h->size = need;
    }
    h->used = 1;
    return h + 1;
  }
  return NULL;
}

void rt_free(void *p) {
  if (!p) return;
  RtHdr *h = (RtHdr *)p - 1;
  if (!g_arena || (uint8_t *)h < (uint8_t *)rt_first() || (uint8_t *)p >= g_arena + ST()->rt_end || !h->used) die("rt_free of bad block", mem_off(p), 0, 0);
  h->used = 0;
}

/* ---- global heap ---- */

static GBlk *gblk_at(uint32_t off) { return off ? (GBlk *)(g_arena + off) : NULL; }

static GBlk *gblk_find(uint16_t h) {
  for (GBlk *b = gblk_at(ST()->gblk_head); b; b = gblk_at(b->next))
    if (b->sel == h) return b;
  return NULL;
}

/* First-fit linear range of `size` bytes, ignoring block `skip`. 0 if none. */
static uint32_t lin_find(uint32_t size, const GBlk *skip) {
  uint32_t cursor = MEM_HEAP_LINEAR;
  for (GBlk *b = gblk_at(ST()->gblk_head); b; b = gblk_at(b->next)) {
    if (b == skip) continue;
    if ((uint64_t)cursor + size <= b->base) return cursor;
    cursor = b->base + b->size;
  }
  return (uint64_t)cursor + size <= g_rt_base ? cursor : 0;
}

static void gblk_link(GBlk *nb) {
  uint32_t *link = &ST()->gblk_head;
  while (*link && gblk_at(*link)->base < nb->base) link = &gblk_at(*link)->next;
  nb->next = *link;
  *link = mem_off(nb);
}

static void gblk_unlink(GBlk *b) {
  uint32_t *link = &ST()->gblk_head;
  while (*link && gblk_at(*link) != b) link = &gblk_at(*link)->next;
  if (*link) *link = b->next;
}

static void gblk_describe(const GBlk *b, uint32_t size_req) {
  /* Descriptors: 64 KB each, the last one covers the remainder. */
  for (unsigned i = 0; i < b->nsel; i++) {
    uint32_t off = i * 0x10000u;
    uint32_t len = size_req - off;
    if (len > 0x10000u) len = 0x10000u;
    desc_set((uint16_t)(b->sel + i * MEM_AHINCR), b->base + off, len - 1, 0x93);
  }
}

static uint32_t round16(uint32_t n) { return n == 0 ? 16 : (uint32_t)(((uint64_t)n + 15u) & ~15u); }

/* Validates a request before anything is mutated. On success returns true and
 * sets *size (rounded to 16) and *nsel (selectors needed, 1..8192). */
static bool global_dims(uint32_t bytes, uint32_t *size, unsigned *nsel) {
  if (bytes > 0xFFFFFFF0u) return false;
  uint32_t sz = round16(bytes);
  uint64_t n = ((uint64_t)sz + 0xFFFFu) >> 16;
  if (n == 0 || n > MEM_GDT_ENTRIES) return false;
  if (sz > g_rt_base - MEM_HEAP_LINEAR) return false; /* beyond the heap span */
  *size = sz;
  *nsel = (unsigned)n;
  return true;
}

uint16_t GlobalAlloc(uint16_t flags, uint32_t bytes) {
  if (!g_arena) return 0;
  uint32_t size;
  unsigned nsel;
  if (!global_dims(bytes, &size, &nsel)) return 0;
  uint32_t base = lin_find(size, NULL);
  if (!base) return 0;
  GBlk *b = rt_alloc(sizeof(GBlk));
  if (!b) return 0;
  uint16_t sel = sel_alloc(nsel);
  if (!sel) {
    rt_free(b);
    return 0;
  }
  b->base = base;
  b->size = size;
  b->sel = sel;
  b->nsel = (uint16_t)nsel;
  b->lock = 0;
  b->flags = flags;
  gblk_describe(b, size);
  gblk_link(b);
  if (flags & GMEM_ZEROINIT) memset(g_arena + base, 0, size);
  return sel;
}

uint16_t GlobalReAlloc(uint16_t h, uint32_t bytes, uint16_t flags) {
  GBlk *b = gblk_find(h);
  uint32_t size;
  unsigned nsel;
  if (!b || !global_dims(bytes, &size, &nsel)) return 0;
  /* Extra selectors must be free right after the handle. */
  for (unsigned i = b->nsel; i < nsel; i++) {
    unsigned idx = (h >> 3) + i;
    if (idx >= MEM_GDT_ENTRIES || idx_used(idx)) return 0;
  }
  uint32_t old_base = b->base, old_size = b->size;
  uint32_t base = old_base;
  if (size > old_size) {
    base = lin_find(size, b);
    if (!base) return 0;
  }
  if (base != old_base) {
    memmove(g_arena + base, g_arena + old_base, old_size < size ? old_size : size);
    gblk_unlink(b);
    b->base = base;
  } else {
    gblk_unlink(b);
  }
  if (size > old_size && (flags & GMEM_ZEROINIT)) memset(g_arena + base + old_size, 0, size - old_size);
  for (unsigned i = b->nsel; i < nsel; i++) idx_set((h >> 3) + i, true);
  if (nsel < b->nsel) sel_free((uint16_t)(h + nsel * MEM_AHINCR), b->nsel - nsel);
  b->size = size;
  b->nsel = (uint16_t)nsel;
  gblk_describe(b, size);
  gblk_link(b);
  return h;
}

uint16_t GlobalFree(uint16_t h) {
  GBlk *b = gblk_find(h);
  if (!b) return h;
  gblk_unlink(b);
  sel_free(b->sel, b->nsel);
  rt_free(b);
  return 0;
}

uint32_t GlobalLock(uint16_t h) {
  GBlk *b = gblk_find(h);
  if (!b) return 0;
  b->lock++;
  return MK_FP(h, 0);
}

bool GlobalUnlock(uint16_t h) {
  GBlk *b = gblk_find(h);
  if (!b || b->lock == 0) return false;
  return --b->lock != 0;
}

uint32_t GlobalSize(uint16_t h) {
  GBlk *b = gblk_find(h);
  return b ? b->size : 0;
}

/* ---- local heap ---- */

static LHdr *lh_at(uint16_t off) { return (LHdr *)LINN(off); }

void mem_local_init(uint16_t start, uint16_t end) {
  start = (uint16_t)((start + 3u) & ~3u);
  end = (uint16_t)(end & ~3u);
  if (end <= start + sizeof(LHdr)) die("bad local heap range", start, end, 0);
  if (!mem_check(MK_FP(MEM_SEL_DGROUP, start), (uint32_t)(end - start)))
    die("local heap range [start,end) not inside DGROUP (descriptor missing or too small)", start, end, 0);
  ST()->local_start = start;
  ST()->local_end = end;
  LHdr *h = lh_at(start);
  h->size = (uint16_t)(end - start);
  h->used = 0;
}

static void local_flags(uint16_t flags) {
  if (flags & ~LMEM_ZEROINIT) die("unsupported LocalAlloc flags (only LMEM_FIXED/LMEM_ZEROINIT)", flags, 0, 0);
}

uint16_t LocalAlloc(uint16_t flags, uint16_t bytes) {
  local_flags(flags);
  uint32_t need = (((uint32_t)bytes + 3u) & ~3u) + sizeof(LHdr);
  if (need < 8) need = 8;
  uint16_t end = ST()->local_end;
  if (end == 0) die("local heap not initialised", 0, 0, 0);
  for (uint32_t off = ST()->local_start; off < end;) {
    LHdr *h = lh_at((uint16_t)off);
    if (!h->used) {
      /* Merge following free blocks. */
      while (off + h->size < end && !lh_at((uint16_t)(off + h->size))->used)
        h->size = (uint16_t)(h->size + lh_at((uint16_t)(off + h->size))->size);
      if (h->size >= need) {
        if (h->size >= need + 8) {
          LHdr *rest = lh_at((uint16_t)(off + need));
          rest->size = (uint16_t)(h->size - need);
          rest->used = 0;
          h->size = (uint16_t)need;
        }
        h->used = 1;
        if (flags & LMEM_ZEROINIT) memset(h + 1, 0, h->size - sizeof(LHdr));
        return (uint16_t)(off + sizeof(LHdr));
      }
    }
    off += h->size;
  }
  return 0;
}

static LHdr *local_hdr(uint16_t off) {
  if (off < ST()->local_start + sizeof(LHdr) || off >= ST()->local_end) return NULL;
  LHdr *h = lh_at((uint16_t)(off - sizeof(LHdr)));
  return h->used ? h : NULL;
}

uint16_t LocalFree(uint16_t off) {
  LHdr *h = local_hdr(off);
  if (!h) return off;
  h->used = 0;
  return 0;
}

uint16_t LocalSize(uint16_t off) {
  LHdr *h = local_hdr(off);
  return h ? (uint16_t)(h->size - sizeof(LHdr)) : 0;
}

uint16_t LocalReAlloc(uint16_t off, uint16_t bytes, uint16_t flags) {
  local_flags(flags);
  LHdr *h = local_hdr(off);
  if (!h) return 0;
  uint16_t old = (uint16_t)(h->size - sizeof(LHdr));
  if (bytes <= old) return off;
  uint32_t need = (((uint32_t)bytes + 3u) & ~3u) + sizeof(LHdr);
  uint16_t hoff = (uint16_t)(off - sizeof(LHdr));
  uint32_t end = ST()->local_end;
  /* Grow in place when the free blocks right after this one are big enough. */
  uint32_t total = h->size;
  while (hoff + total < end && !lh_at((uint16_t)(hoff + total))->used) total += lh_at((uint16_t)(hoff + total))->size;
  if (total >= need) {
    while (hoff + h->size < (uint32_t)(hoff + total)) h->size = (uint16_t)(h->size + lh_at((uint16_t)(hoff + h->size))->size);
    if (h->size >= need + 8) {
      LHdr *rest = lh_at((uint16_t)(hoff + need));
      rest->size = (uint16_t)(h->size - need);
      rest->used = 0;
      h->size = (uint16_t)need;
    }
    if (flags & LMEM_ZEROINIT) memset((uint8_t *)LINN(off) + old, 0, bytes - old);
    return off;
  }
  /* Otherwise allocate while this block is still marked used, so the
   * allocator cannot merge it into a free neighbour and overwrite its data. */
  uint16_t n = LocalAlloc(0, bytes);
  if (!n) return 0;
  memcpy(LINN(n), LINN(off), old);
  if (flags & LMEM_ZEROINIT) memset((uint8_t *)LINN(n) + old, 0, bytes - old);
  LocalFree(off);
  return n;
}
