/* Differential-test driver. See harness/difftest.h. */
#include "harness/difftest.h"

#include <capstone/capstone.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "runtime/mem.h"

#define PAGE 4096u
#define SLACK (2u * 1024 * 1024)
#define FUZZ_MAX_ARGS 16

static uint16_t seg_sel(uint16_t seg) { return seg < 0x100 ? (uint16_t)(seg << 3) : seg; }

static bool conv_near(int conv) { return conv == NEAR_PASCAL || conv == NEAR_CDECL; }
static int hconv(int conv) {
  switch (conv) {
    case FAR_CDECL: return HCONV_CDECL;
    case NEAR_PASCAL: return HCONV_NEAR_PASCAL;
    case NEAR_CDECL: return HCONV_NEAR_CDECL;
    default: return HCONV_PASCAL;
  }
}

#define DT_FLAG_MASK 0x0CD5u /* CF PF AF ZF SF DF OF, compared for REGISTER functions */

/* ---- registry ---- */

static DtEntry g_dyn[256];
static unsigned g_n_dyn;
static int g_mode;

void difftest_register(const DtEntry *e) {
  for (unsigned i = 0; i < g_n_dyn; i++) {
    if (g_dyn[i].seg == e->seg && g_dyn[i].off == e->off) {
      g_dyn[i] = *e;
      return;
    }
  }
  if (g_n_dyn >= sizeof g_dyn / sizeof g_dyn[0]) abort();
  g_dyn[g_n_dyn++] = *e;
}

void difftest_unregister_all(void) { g_n_dyn = 0; }

const DtEntry *difftest_lookup(uint16_t seg, uint16_t off) {
  for (unsigned i = 0; i < g_n_dyn; i++)
    if (g_dyn[i].seg == seg && g_dyn[i].off == off) return &g_dyn[i];
  for (unsigned i = 0; i < dt_registry_count; i++)
    if (dt_registry[i].seg == seg && dt_registry[i].off == off) return &dt_registry[i];
  return NULL;
}

/* ---- per-function bookkeeping: extent, coverage, status ---- */

typedef struct {
  uint16_t seg, off;
  char name[64];
  uint32_t size;
  uint8_t *hit; /* bitmap over offsets 0..65535 */
  DtStatus status;
  bool ran;
  /* What the last coverage file of this function was made from (write_coverage). */
  bool cov_written;
  char cov_name[64];
  uint32_t cov_size;
  uint8_t *cov_hit;  /* copy of hit */
  uint8_t *cov_code; /* copy of the cov_size code bytes at the function */
} Fn;

static Fn *g_fns;
static size_t g_n_fns, g_cap_fns;
static bool g_csv_loaded;

static Fn *fn_find(uint16_t seg, uint16_t off, bool create) {
  for (size_t i = 0; i < g_n_fns; i++)
    if (g_fns[i].seg == seg && g_fns[i].off == off) return &g_fns[i];
  if (!create) return NULL;
  if (g_n_fns == g_cap_fns) {
    g_cap_fns = g_cap_fns ? g_cap_fns * 2 : 64;
    g_fns = realloc(g_fns, g_cap_fns * sizeof *g_fns);
    if (!g_fns) abort();
  }
  memset(&g_fns[g_n_fns], 0, sizeof g_fns[0]);
  g_fns[g_n_fns].seg = seg;
  g_fns[g_n_fns].off = off;
  snprintf(g_fns[g_n_fns].name, sizeof g_fns[0].name, "%u:%04x", seg, off);
  return &g_fns[g_n_fns++];
}

void difftest_set_extent(uint16_t seg, uint16_t off, uint32_t size, const char *name) {
  Fn *f = fn_find(seg, off, true);
  f->size = size;
  if (name) snprintf(f->name, sizeof f->name, "%s", name);
}

/* docs/functions.csv (optional, local data): fills the extent and name of every
 * function not set explicitly. Only the first four columns are read. */
static void load_csv(void) {
  FILE *fp;
  char line[4096];
  if (g_csv_loaded) return;
  g_csv_loaded = true;
  fp = fopen("docs/functions.csv", "r");
  if (!fp) return;
  if (!fgets(line, sizeof line, fp)) {
    fclose(fp);
    return;
  }
  while (fgets(line, sizeof line, fp)) {
    unsigned seg, off, size;
    char name[64];
    if (sscanf(line, "%u,%x,%63[^,],%u", &seg, &off, name, &size) == 4) {
      Fn *f = fn_find((uint16_t)seg, (uint16_t)off, true);
      if (f->size == 0) f->size = size;
      if (strchr(f->name, ':')) snprintf(f->name, sizeof f->name, "%s", name);
    }
  }
  fclose(fp);
}

static Fn *fn_get(uint16_t seg, uint16_t off) {
  Fn *f;
  load_csv();
  f = fn_find(seg, off, true);
  return f;
}

typedef struct {
  Fn *fn;
  uint32_t base; /* linear base of the function's segment */
} TraceCtx;

static void on_trace(uint32_t lin, void *ud) {
  TraceCtx *t = ud;
  uint32_t o = lin - t->base;
  if (o < 65536u) t->fn->hit[o >> 3] |= (uint8_t)(1u << (o & 7));
}

/* Linear sweep of [off, off+size): the set of instruction start offsets. */
static size_t sweep(uint16_t sel, uint16_t off, uint32_t size, uint16_t **out) {
  csh h;
  cs_insn *insn = NULL;
  size_t n, k = 0;
  uint16_t *offs;
  if (cs_open(CS_ARCH_X86, CS_MODE_16, &h) != CS_ERR_OK) return 0;
  n = cs_disasm(h, mem_arena() + desc_base(sel) + off, size, off, 0, &insn);
  offs = malloc((n + 1) * sizeof *offs);
  for (size_t i = 0; i < n; i++) offs[k++] = (uint16_t)insn[i].address;
  if (insn) cs_free(insn, n);
  cs_close(&h);
  *out = offs;
  return k;
}

double difftest_coverage(uint16_t seg, uint16_t off) {
  Fn *f = fn_find(seg, off, false);
  uint16_t *offs = NULL;
  size_t n, hits = 0;
  if (!f || !f->hit || f->size == 0) return -1;
  n = sweep(seg_sel(seg), off, f->size, &offs);
  if (n == 0) {
    free(offs);
    return -1;
  }
  for (size_t i = 0; i < n; i++)
    if (f->hit[offs[i] >> 3] & (1u << (offs[i] & 7))) hits++;
  free(offs);
  return 100.0 * (double)hits / (double)n;
}

/* The file is a function of the name, seg, off, size, the code bytes the sweep reads and the hit
 * bitmap. When none of them changed since this process last wrote it and the file is still
 * there, writing it again would produce the same bytes, so it is skipped (a scenario DIFFs
 * the same function hundreds of times; the disassembly and the file write cost about a tenth
 * of a run, stars-a7j). */
static bool coverage_unchanged(Fn *f, const char *path) {
  const uint8_t *code = mem_arena() + desc_base(seg_sel(f->seg)) + f->off;
  struct stat st;
  bool same = f->cov_written && f->cov_size == f->size && strcmp(f->cov_name, f->name) == 0 &&
              memcmp(f->cov_hit, f->hit, 65536 / 8) == 0 && memcmp(f->cov_code, code, f->size) == 0 &&
              stat(path, &st) == 0;
  if (same) return true;
  f->cov_written = false;
  free(f->cov_code);
  f->cov_code = malloc(f->size);
  if (!f->cov_hit) f->cov_hit = malloc(65536 / 8);
  if (!f->cov_code || !f->cov_hit) abort();
  memcpy(f->cov_code, code, f->size);
  memcpy(f->cov_hit, f->hit, 65536 / 8);
  f->cov_size = f->size;
  snprintf(f->cov_name, sizeof f->cov_name, "%s", f->name);
  return false;
}

static void write_coverage(Fn *f) {
  uint16_t *offs = NULL;
  size_t n, hits = 0;
  char path[128];
  FILE *fp;
  if (!f->hit || f->size == 0) return;
  snprintf(path, sizeof path, "build/coverage/%u_%04x.txt", f->seg, f->off);
  if (coverage_unchanged(f, path)) return;
  n = sweep(seg_sel(f->seg), f->off, f->size, &offs);
  for (size_t i = 0; i < n; i++)
    if (f->hit[offs[i] >> 3] & (1u << (offs[i] & 7))) hits++;
  mkdir("build", 0777);
  mkdir("build/coverage", 0777);
  fp = fopen(path, "w");
  if (!fp) {
    free(offs);
    return;
  }
  fprintf(fp, "function %s seg=%u off=0x%04x size=%u\n", f->name, f->seg, f->off, f->size);
  fprintf(fp, "instructions=%zu executed=%zu\n", n, hits);
  fprintf(fp, "coverage=%.1f%%\n", n ? 100.0 * (double)hits / (double)n : 0.0);
  /* Offsets of the instructions never executed (an aid for adding scenarios). */
  fprintf(fp, "uncovered:");
  for (size_t i = 0; i < n; i++)
    if (!(f->hit[offs[i] >> 3] & (1u << (offs[i] & 7)))) fprintf(fp, " %04x", offs[i]);
  fprintf(fp, "\n");
  /* Every executed offset, for functions whose linear sweep stops early (jump tables inside the code). */
  fprintf(fp, "executed_offsets:");
  for (uint32_t o = f->off; o < (uint32_t)f->off + f->size && o < 65536u; o++)
    if (f->hit[o >> 3] & (1u << (o & 7))) fprintf(fp, " %04x", o);
  fprintf(fp, "\n");
  free(offs);
  {
    bool werr = ferror(fp) != 0;
    f->cov_written = fclose(fp) == 0 && !werr;
  }
}

/* ---- snapshot ---- */

typedef struct {
  uint8_t *lo;      /* copy of [0, lo_n) */
  size_t lo_n;
  uint8_t *rt;      /* copy of [rt_base, size) */
  size_t rt_base, rt_n;
} Snap;

typedef struct {
  size_t *page;     /* arena page numbers (lo pages first, then rt pages) */
  uint8_t *data;    /* PAGE bytes each */
  size_t n, cap;
} Dirty;

static size_t max_desc_end(void) {
  size_t m = MEM_HEAP_LINEAR;
  for (unsigned s = 8; s < MEM_GDT_ENTRIES * 8u; s += 8) {
    if (!(mem_gdt()[s + 5] & 0x80)) continue;
    size_t e = (size_t)desc_base((uint16_t)s) + desc_limit((uint16_t)s) + 1u;
    if (e > m) m = e;
  }
  return m;
}

/* The snapshot buffers (several MB: the arena up to the highest block plus SLACK, and the runtime
 * heap) are kept between DIFFs instead of being allocated and freed for each one: a fresh
 * allocation of that size is a new mapping whose every page faults on the first copy, which
 * cost more than the copy itself (stars-a7j). Only one snapshot is live at a time (DIFF refuses
 * to nest); a second one, should it ever be taken, gets its own buffers. */
static uint8_t *g_snap_lo, *g_snap_rt;
static size_t g_snap_lo_cap, g_snap_rt_cap;
static bool g_snap_busy;

/* Under ASan the part of a kept buffer past the current snapshot, and the whole buffer while no
 * snapshot uses it, is poisoned, so an access outside the live snapshot is still reported as
 * it was with a buffer of the exact size that was freed after each DIFF. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define SNAP_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#define SNAP_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#endif
#endif
#ifndef SNAP_POISON
#define SNAP_POISON(p, n) ((void)(p), (void)(n))
#define SNAP_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

static uint8_t *snap_buf(uint8_t **buf, size_t *cap, size_t n) {
  if (n > *cap) {
    if (*buf) SNAP_UNPOISON(*buf, *cap);
    free(*buf);
    *buf = malloc(n);
    *cap = *buf ? n : 0;
  }
  if (*buf) {
    SNAP_UNPOISON(*buf, n);
    SNAP_POISON(*buf + n, *cap - n);
  }
  return *buf;
}

static void snap_take(Snap *s) {
  size_t ext = (max_desc_end() + SLACK + PAGE - 1) & ~(size_t)(PAGE - 1);
  s->rt_base = mem_rt_base();
  if (ext > s->rt_base) ext = s->rt_base;
  s->lo_n = ext;
  s->rt_n = mem_size() - s->rt_base;
  if (!g_snap_busy) {
    g_snap_busy = true;
    s->lo = snap_buf(&g_snap_lo, &g_snap_lo_cap, s->lo_n);
    s->rt = snap_buf(&g_snap_rt, &g_snap_rt_cap, s->rt_n);
  } else {
    s->lo = malloc(s->lo_n);
    s->rt = malloc(s->rt_n);
  }
  if (!s->lo || !s->rt) abort();
  memcpy(s->lo, mem_arena(), s->lo_n);
  memcpy(s->rt, mem_arena() + s->rt_base, s->rt_n);
}

static void snap_free(Snap *s) {
  if (s->lo == g_snap_lo && s->lo) {
    g_snap_busy = false; /* the kept buffers, not freed */
    SNAP_POISON(g_snap_lo, g_snap_lo_cap);
    SNAP_POISON(g_snap_rt, g_snap_rt_cap);
  } else {
    free(s->lo);
    free(s->rt);
  }
  memset(s, 0, sizeof *s);
}

static const uint8_t *snap_page(const Snap *s, size_t pg) {
  size_t a = pg * PAGE;
  return a < s->lo_n ? s->lo + a : s->rt + (a - s->rt_base);
}

static void dirty_free(Dirty *d) {
  free(d->page);
  free(d->data);
  memset(d, 0, sizeof *d);
}

static void dirty_add(Dirty *d, size_t pg) {
  if (d->n == d->cap) {
    d->cap = d->cap ? d->cap * 2 : 64;
    d->page = realloc(d->page, d->cap * sizeof *d->page);
    d->data = realloc(d->data, d->cap * PAGE);
    if (!d->page || !d->data) abort();
  }
  d->page[d->n] = pg;
  memcpy(d->data + d->n * PAGE, mem_arena() + pg * PAGE, PAGE);
  d->n++;
}

static void dirty_collect(const Snap *s, Dirty *d) {
  for (size_t a = 0; a < s->lo_n; a += PAGE)
    if (memcmp(mem_arena() + a, s->lo + a, PAGE) != 0) dirty_add(d, a / PAGE);
  for (size_t a = s->rt_base; a < mem_size(); a += PAGE)
    if (memcmp(mem_arena() + a, s->rt + (a - s->rt_base), PAGE) != 0) dirty_add(d, a / PAGE);
}

static void restore(const Snap *s, const Dirty *d) {
  for (size_t i = 0; i < d->n; i++)
    memcpy(mem_arena() + d->page[i] * PAGE, snap_page(s, d->page[i]), PAGE);
}

/* Returns the page image of the run that produced d (or the snapshot). */
static const uint8_t *dirty_page(const Snap *s, const Dirty *d, size_t pg) {
  size_t lo = 0, hi = d->n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (d->page[mid] == pg) return d->data + mid * PAGE;
    if (d->page[mid] < pg) lo = mid + 1;
    else hi = mid;
  }
  return snap_page(s, pg);
}

/* "sel:off" of a linear address, from the first present descriptor covering it. */
static void describe_lin(size_t lin, char *out, size_t n) {
  for (unsigned s = 8; s < MEM_GDT_ENTRIES * 8u; s += 8) {
    if (!(mem_gdt()[s + 5] & 0x80)) continue;
    size_t b = desc_base((uint16_t)s);
    if (lin >= b && lin <= b + desc_limit((uint16_t)s)) {
      snprintf(out, n, "selector 0x%04x, offset 0x%04zx", s, lin - b);
      return;
    }
  }
  snprintf(out, n, "linear 0x%zx (no selector)", lin);
}

/* ---- API log copy and compare ---- */

typedef struct {
  HLogEntry *e;
  size_t n;
} LogCopy;

static LogCopy log_copy(void) {
  LogCopy c;
  c.n = harness_log_count();
  c.e = malloc((c.n + 1) * sizeof *c.e);
  for (size_t i = 0; i < c.n; i++) c.e[i] = *harness_log_get(i);
  return c;
}

static bool log_entry_eq(const HLogEntry *a, const HLogEntry *b) {
  unsigned n;
  if (a->ordinal != b->ordinal || strcmp(a->module, b->module) != 0 || a->argbytes != b->argbytes ||
      a->ret != b->ret)
    return false;
  n = a->argbytes < HARNESS_MAX_LOG_ARGS ? a->argbytes : HARNESS_MAX_LOG_ARGS;
  if (memcmp(a->args, b->args, n) != 0) return false;
  /* register inputs (REGISTER imports) and variable arguments (VARARGS imports) */
  if (a->has_regs != b->has_regs || memcmp(a->regs, b->regs, sizeof a->regs) != 0) return false;
  if (a->has_va != b->has_va || a->va_bytes != b->va_bytes) return false;
  n = a->va_bytes < HARNESS_MAX_LOG_ARGS ? a->va_bytes : HARNESS_MAX_LOG_ARGS;
  return memcmp(a->va, b->va, n) == 0;
}

static void fmt_bytes(char *out, size_t n, const uint8_t *p, size_t cnt) {
  size_t k = 0;
  out[0] = 0;
  for (size_t i = 0; i < cnt && k + 4 < n; i++) k += (size_t)snprintf(out + k, n - k, "%02x ", p[i]);
}

static void fmt_log(char *out, size_t n, const HLogEntry *e) {
  static const char *const rn[HARNESS_LOG_NREGS] = {"AX", "BX", "CX", "DX", "SI", "DI", "DS", "ES"};
  char b[HARNESS_MAX_LOG_ARGS * 3 + 1], v[HARNESS_MAX_LOG_ARGS * 3 + 1], rg[HARNESS_LOG_NREGS * 9 + 8];
  unsigned c = e->argbytes < HARNESS_MAX_LOG_ARGS ? e->argbytes : HARNESS_MAX_LOG_ARGS;
  unsigned cv = e->va_bytes < HARNESS_MAX_LOG_ARGS ? e->va_bytes : HARNESS_MAX_LOG_ARGS;
  size_t k = 0;
  fmt_bytes(b, sizeof b, e->args, c);
  rg[0] = 0;
  if (e->has_regs) {
    k += (size_t)snprintf(rg, sizeof rg, " regs[");
    for (unsigned i = 0; i < HARNESS_LOG_NREGS; i++)
      k += (size_t)snprintf(rg + k, sizeof rg - k, "%s%s=%04x", i ? " " : "", rn[i], e->regs[i]);
    snprintf(rg + k, sizeof rg - k, "]");
  }
  v[0] = 0;
  if (e->has_va) fmt_bytes(v, sizeof v, e->va, cv);
  snprintf(out, n, "%s.%u (%s) args[%s]%s%s%s%s ret=0x%x", e->module, e->ordinal,
           e->name ? e->name : "?", b, rg, e->has_va ? " va[" : "", v, e->has_va ? "]" : "", e->ret);
}

/* ---- one comparison ---- */

static void push_word(uint16_t *sp, uint16_t v) {
  *sp = (uint16_t)(*sp - 2);
  memcpy(LIN(MK_FP(MEM_SEL_DGROUP, *sp)), &v, 2);
}

static bool in_range(size_t a, size_t lo, size_t hi) { return a >= lo && a < hi; }

static DtResult run_once(const Snap *snap, uint16_t seg, uint16_t off, int conv, const uint16_t *args,
                         int nargs, const Regs16 *in, Fn *fn) {
  DtResult r;
  Dirty od, dd;
  LogCopy ol, dl;
  TraceCtx tc;
  Regs16 oreg, dreg;
  uint32_t oret, dret = 0;
  uint16_t sel = seg_sel(seg), top = harness_stack_top();
  const DtEntry *e;
  bool isreg = conv == REGISTER;
  bool isnear = conv_near(conv);
  size_t ex_lo, ex_hi, tv_lo, tv_hi;
  char where[96];
  int sv_mode;
  uint16_t sv_rt_sp, sv_call_sp;
  bool grew;
  unsigned sv_stop = harness_stop_after_api_remaining();

  memset(&r, 0, sizeof r);
  memset(&od, 0, sizeof od);
  memset(&dd, 0, sizeof dd);
  if (isreg) nargs = 0;
  if (max_desc_end() > snap->lo_n) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "the arena grew beyond the snapshot (%zu bytes) before the ORIGINAL run", snap->lo_n);
    return r;
  }
  /* The callee's whole frame, from the stack limit up to the top of the stack,
   * is scratch: that includes the argument words and the return address, which
   * the original may legitimately write (INC [BP+6]); the C twin never does.
   * The local heap starts at the top, so a write there is still compared. */
  ex_lo = desc_base(MEM_SEL_DGROUP) + harness_stack_limit();
  ex_hi = desc_base(MEM_SEL_DGROUP) + top;
  tv_lo = desc_base(MEM_SEL_THUNK) + HARNESS_THUNK_VARS_OFF;
  tv_hi = tv_lo + HARNESS_THUNK_VARS_LEN;

  /* ORIGINAL run, collecting executed instruction addresses. A pending
   * harness_stop_after_api would truncate the reference run, so it is cleared for
   * the DIFF and restored (remaining count) when it ends. */
  harness_stop_after_api(0);
  harness_log_clear();
  if (fn->size) {
    if (!fn->hit) fn->hit = calloc(65536 / 8, 1);
    tc.fn = fn;
    tc.base = desc_base(sel);
    harness_trace(tc.base + off, tc.base + off + fn->size, on_trace, &tc);
  }
  memset(&oreg, 0, sizeof oreg);
  if (in) oreg = *in;
  /* The reference run must be a pure emulation: callbacks from API handlers go
   * to the original code, whatever mode the scenario is in, and no C frame of
   * the scenario is active (rt_sp == 0) or call SP is pending. */
  sv_mode = g_mode;
  sv_rt_sp = rt_sp;
  sv_call_sp = harness_get_call_sp();
  g_mode = RT_MODE_ORIGINAL;
  rt_sp = 0;
  harness_set_call_sp(0);
  oret = harness_call(sel, off, hconv(conv), args, nargs, isreg ? &oreg : NULL);
  g_mode = sv_mode;
  rt_sp = sv_rt_sp;
  harness_set_call_sp(sv_call_sp);
  if (fn->size) harness_trace(0, 0, NULL, NULL);
  r.ret = oret;
  r.regs = oreg;
  grew = max_desc_end() > snap->lo_n; /* before restore() rolls the GDT back */
  ol = log_copy();
  dirty_collect(snap, &od);
  restore(snap, &od);
  harness_log_clear();
  if (grew) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "the arena grew beyond the snapshot (%zu bytes) during the ORIGINAL run", snap->lo_n);
    goto done;
  }
  if (harness_stopped_early()) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "the ORIGINAL run stopped early (harness_stop_after_api); it cannot be the reference");
    goto done;
  }
  if (harness_last_error() != 0) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "the ORIGINAL run failed (engine error %d)", harness_last_error());
    goto done;
  }

  e = difftest_lookup(seg, off);
  if (!e) {
    r.status = DT_MISSING;
    snprintf(r.msg, sizeof r.msg, "MISSING: no decompiled function registered for %u:%04x", seg, off);
    goto done;
  }
  if (e->conv != conv) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "convention mismatch: DIFF says %d, registry says %d", conv, e->conv);
    goto done;
  }

  /* DECOMP run on the same emulated stack layout. */
  {
    uint16_t sv_ss = rt_ss, sv_sp = rt_sp, sv_bp = rt_bp;
    uint16_t sv_si = rt_entry_si, sv_di = rt_entry_di;
    uint16_t sp = top;
    if (conv == FAR_CDECL || conv == NEAR_CDECL) {
      for (int i = nargs - 1; i >= 0; i--) push_word(&sp, args[i]);
    } else {
      for (int i = 0; i < nargs; i++) push_word(&sp, args[i]);
    }
    if (isnear) {
      push_word(&sp, harness_near_return_offset(sel));
    } else {
      push_word(&sp, MEM_SEL_THUNK);
      push_word(&sp, HARNESS_THUNK_RET_OFF);
    }
    rt_ss = MEM_SEL_DGROUP;
    rt_sp = sp;
    /* The original pushes the caller's BP, SI and DI into its frame: the C side writes the same
     * words (only a REGISTER function takes them from the input registers). */
    rt_bp = isreg && in ? in->bp : 0;
    rt_entry_si = isreg && in ? in->si : 0;
    rt_entry_di = isreg && in ? in->di : 0;
    /* The C side starts from the registers the original starts with: the
     * caller's input registers, DS = DGROUP, ES = in->es or 0, SS:SP at the
     * return address, CS:IP at the entry (harness_call). */
    memset(&dreg, 0, sizeof dreg);
    if (in) dreg = *in;
    dreg.ds = MEM_SEL_DGROUP;
    dreg.es = in ? in->es : 0;
    dreg.ss = MEM_SEL_DGROUP;
    dreg.sp = sp;
    dreg.cs = sel;
    dreg.ip = off;
    dret = e->adapter(rt_ss, rt_sp, &dreg);
    rt_ss = sv_ss;
    rt_sp = sv_sp;
    rt_bp = sv_bp;
    rt_entry_si = sv_si;
    rt_entry_di = sv_di;
  }
  dl = log_copy();
  dirty_collect(snap, &dd);
  harness_log_clear();

  /* 1. return values */
  if (isreg) {
    static const struct { const char *n; size_t o; } rn[] = {
        {"AX", offsetof(Regs16, ax)}, {"BX", offsetof(Regs16, bx)}, {"CX", offsetof(Regs16, cx)},
        {"DX", offsetof(Regs16, dx)}, {"SI", offsetof(Regs16, si)}, {"DI", offsetof(Regs16, di)},
        {"BP", offsetof(Regs16, bp)}, {"DS", offsetof(Regs16, ds)}, {"ES", offsetof(Regs16, es)}};
    for (size_t i = 0; i < sizeof rn / sizeof rn[0]; i++) {
      uint16_t a = *(uint16_t *)((char *)&oreg + rn[i].o), b = *(uint16_t *)((char *)&dreg + rn[i].o);
      if (a != b) {
        r.status = DT_FAIL;
        snprintf(r.msg, sizeof r.msg, "register %s differs: expected 0x%04x, actual 0x%04x", rn[i].n, a, b);
        goto cmp_done;
      }
    }
    if ((oreg.flags & DT_FLAG_MASK) != (dreg.flags & DT_FLAG_MASK)) {
      r.status = DT_FAIL;
      snprintf(r.msg, sizeof r.msg, "FLAGS differ (mask 0x%04x): expected 0x%04x, actual 0x%04x", DT_FLAG_MASK,
               oreg.flags & DT_FLAG_MASK, dreg.flags & DT_FLAG_MASK);
      goto cmp_done;
    }
  } else if (e->ret_words >= 1) {
    if ((uint16_t)oret != (uint16_t)dret) {
      r.status = DT_FAIL;
      snprintf(r.msg, sizeof r.msg, "return value AX differs: expected 0x%04x, actual 0x%04x",
               (uint16_t)oret, (uint16_t)dret);
      goto cmp_done;
    }
    if (e->ret_words == 2 && (uint16_t)(oret >> 16) != (uint16_t)(dret >> 16)) {
      r.status = DT_FAIL;
      snprintf(r.msg, sizeof r.msg, "return value DX differs: expected 0x%04x, actual 0x%04x",
               (uint16_t)(oret >> 16), (uint16_t)(dret >> 16));
      goto cmp_done;
    }
  }

  /* 2. arena bytes, excluding the callee's scratch stack [limit, top) */
  if (max_desc_end() > snap->lo_n) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "the arena grew beyond the snapshot (%zu bytes)", snap->lo_n);
    goto cmp_done;
  }
  {
    size_t i = 0, j = 0;
    while (i < od.n || j < dd.n) {
      size_t pg;
      const uint8_t *a, *b;
      if (j >= dd.n || (i < od.n && od.page[i] <= dd.page[j])) pg = od.page[i];
      else pg = dd.page[j];
      if (i < od.n && od.page[i] == pg) i++;
      if (j < dd.n && dd.page[j] == pg) j++;
      a = dirty_page(snap, &od, pg);
      b = dirty_page(snap, &dd, pg);
      if (memcmp(a, b, PAGE) == 0) continue;
      for (size_t k = 0; k < PAGE; k++) {
        size_t lin = pg * PAGE + k;
        if (a[k] == b[k] || in_range(lin, ex_lo, ex_hi) || in_range(lin, tv_lo, tv_hi)) continue;
        {
          char eb[64], ab[64];
          size_t cnt = 16;
          if (k + cnt > PAGE) cnt = PAGE - k;
          describe_lin(lin, where, sizeof where);
          fmt_bytes(eb, sizeof eb, a + k, cnt);
          fmt_bytes(ab, sizeof ab, b + k, cnt);
          r.status = DT_FAIL;
          snprintf(r.msg, sizeof r.msg, "memory differs at %s: expected [%s] actual [%s]", where, eb, ab);
        }
        goto cmp_done;
      }
    }
  }

  /* 3. API logs */
  {
    size_t n = ol.n < dl.n ? ol.n : dl.n, i;
    for (i = 0; i < n; i++) {
      if (!log_entry_eq(&ol.e[i], &dl.e[i])) {
        char ea[160], eb[160];
        fmt_log(ea, sizeof ea, &ol.e[i]);
        fmt_log(eb, sizeof eb, &dl.e[i]);
        r.status = DT_FAIL;
        snprintf(r.msg, sizeof r.msg, "API log differs at index %zu: expected %s, actual %s", i, ea, eb);
        goto cmp_done;
      }
    }
    if (ol.n != dl.n) {
      char ex[160];
      const HLogEntry *x = ol.n > dl.n ? &ol.e[i] : &dl.e[i];
      fmt_log(ex, sizeof ex, x);
      r.status = DT_FAIL;
      snprintf(r.msg, sizeof r.msg, "API log differs at index %zu: %s has an extra call %s (expected %zu calls, actual %zu)",
               i, ol.n > dl.n ? "the ORIGINAL" : "the DECOMP", ex, ol.n, dl.n);
      goto cmp_done;
    }
  }
  r.status = DT_PASS;
cmp_done:
  restore(snap, &dd);
  free(dl.e);
done:
  harness_stop_after_api(sv_stop); /* restore the scenario's pending limit */
  free(ol.e);
  dirty_free(&od);
  dirty_free(&dd);
  return r;
}

static void refuse_nested(const char *who) {
  if (harness_depth() > 0) {
    fprintf(stderr, "difftest: %s inside a nested harness_call (harness_depth() > 0) is not supported: "
                    "DIFF must be called from scenario code, not from an API handler\n", who);
    abort();
  }
}

DtResult difftest_run(uint16_t seg, uint16_t off, int conv, const uint16_t *args, int nargs,
                      const Regs16 *in) {
  Snap snap;
  Fn *fn = fn_get(seg, off);
  DtResult r;
  LogCopy saved;
  refuse_nested("DIFF");
  saved = log_copy(); /* the scenario's own API log is not ours to clear */
  snap_take(&snap);
  r = run_once(&snap, seg, off, conv, args, nargs, in, fn);
  snap_free(&snap);
  harness_log_set(saved.e, saved.n);
  free(saved.e);
  write_coverage(fn);
  return r;
}

static const uint16_t g_boundary[5] = {0, 1, 0x7FFF, 0x8000, 0xFFFF};

DtResult difftest_fuzz_run(uint16_t seg, uint16_t off, int conv, int nargs, unsigned iterations,
                           uint32_t seed) {
  Snap snap;
  Fn *fn = fn_get(seg, off);
  uint32_t x = seed ? seed : 1;
  DtResult r;
  LogCopy saved;
  uint16_t args[FUZZ_MAX_ARGS];
  refuse_nested("DIFF_FUZZ");
  memset(&r, 0, sizeof r);
  if (nargs < 0 || nargs > FUZZ_MAX_ARGS) {
    fprintf(stderr, "difftest: fuzz nargs %d is out of range (0..%d)\n", nargs, FUZZ_MAX_ARGS);
    abort();
  }
  if (conv == REGISTER) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg,
             "cannot fuzz a REGISTER function: its inputs are registers, which the fuzzer does not generate "
             "(use DIFF with explicit input registers)");
    return r;
  }
  if (iterations == 0) {
    r.status = DT_FAIL;
    snprintf(r.msg, sizeof r.msg, "fuzz run with 0 iterations compares nothing (iterations must be > 0)");
    return r;
  }
  saved = log_copy();
  snap_take(&snap);
  for (unsigned it = 0; it < iterations; it++) {
    for (int i = 0; i < nargs; i++) {
      if (it < 5) {
        args[i] = g_boundary[it]; /* every argument at the same boundary value */
      } else if (it < 10) {
        args[i] = g_boundary[(it + (unsigned)i) % 5]; /* boundary values mixed across arguments */
      } else {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        args[i] = (uint16_t)x;
      }
    }
    r = run_once(&snap, seg, off, conv, args, nargs, NULL, fn);
    if (r.status != DT_PASS) {
      size_t k = strlen(r.msg);
      k += (size_t)snprintf(r.msg + k, sizeof r.msg - k, " [iteration %u, args", it);
      for (int i = 0; i < nargs && k < sizeof r.msg; i++)
        k += (size_t)snprintf(r.msg + k, sizeof r.msg - k, " 0x%04x", args[i]);
      if (k < sizeof r.msg) snprintf(r.msg + k, sizeof r.msg - k, "]");
      break;
    }
  }
  snap_free(&snap);
  harness_log_set(saved.e, saved.n);
  free(saved.e);
  write_coverage(fn);
  return r;
}

/* ---- recording variants and summary ---- */

static void record(uint16_t seg, uint16_t off, const DtResult *r) {
  Fn *f = fn_get(seg, off);
  if (!f->ran || r->status > f->status) f->status = r->status;
  f->ran = true;
  if (r->status != DT_PASS) fprintf(stderr, "DIFF %s %s: %s\n", r->status == DT_MISSING ? "MISSING" : "FAIL", f->name, r->msg);
}

DtStatus difftest_diff(uint16_t seg, uint16_t off, int conv, const uint16_t *args, int nargs) {
  DtResult r = difftest_run(seg, off, conv, args, nargs, NULL);
  record(seg, off, &r);
  return r.status;
}

DtStatus difftest_fuzz(uint16_t seg, uint16_t off, int conv, int nargs, unsigned iterations,
                       uint32_t seed) {
  DtResult r = difftest_fuzz_run(seg, off, conv, nargs, iterations, seed);
  record(seg, off, &r);
  return r.status;
}

int difftest_summary(void) {
  int bad = 0;
  printf("difftest summary:\n");
  for (size_t i = 0; i < g_n_fns; i++) {
    const Fn *f = &g_fns[i];
    double c;
    if (!f->ran) continue;
    c = difftest_coverage(f->seg, f->off);
    if (f->status != DT_PASS) bad = 1;
    if (c < 0) printf("  %-8s %s (%u:%04x) coverage n/a\n", f->status == DT_PASS ? "PASS" : f->status == DT_FAIL ? "FAIL" : "MISSING", f->name, f->seg, f->off);
    else printf("  %-8s %s (%u:%04x) coverage %.1f%%\n", f->status == DT_PASS ? "PASS" : f->status == DT_FAIL ? "FAIL" : "MISSING", f->name, f->seg, f->off, c);
  }
  return bad;
}

void difftest_reset_summary(void) {
  for (size_t i = 0; i < g_n_fns; i++) {
    g_fns[i].ran = false;
    g_fns[i].status = DT_PASS;
  }
}

int difftest_recorded_status(uint16_t seg, uint16_t off) {
  const Fn *f = fn_find(seg, off, false);
  return f && f->ran ? (int)f->status : -1;
}

/* ---- rt_call_far / CALL_ORIG ---- */

void rt_set_mode(int mode) { g_mode = mode; }

static const DtEntry *lookup_sel(uint16_t sel, uint16_t off) {
  const DtEntry *e = NULL;
  if (sel >= 8 && sel < 0x120 && (sel & 7) == 0) e = difftest_lookup((uint16_t)(sel >> 3), off);
  if (!e) e = difftest_lookup(sel, off);
  return e;
}

/* Calls code that is not decompiled. While the emulator runs no C frame is
 * active (rt_sp == 0), so a handler that calls back into C starts a fresh frame
 * at the emulator's SP. When a C frame is active the callee starts below it
 * (the call SP), at any depth, so it cannot overwrite the frame's locals. */
static uint32_t call_emulated(uint16_t sel, uint16_t off, int conv, const uint16_t *args, int nargs) {
  uint16_t sv_sp = rt_sp, sv_call = harness_get_call_sp();
  uint32_t ret;
  Regs16 io;
  /* The original callee pushes the caller's BP: with a C frame active that is the mirrored rt_bp,
   * not whatever the emulator's BP register holds. Every other register is as without io. */
  harness_regs(&io);
  if (harness_depth() == 0) io.ax = io.bx = io.cx = io.dx = io.si = io.di = io.es = 0;
  if (rt_sp != 0) {
    io.bp = rt_bp;
    harness_set_call_sp(rt_sp);
  }
  rt_sp = 0;
  ret = harness_call(sel, off, hconv(conv), args, nargs, sv_sp != 0 ? &io : NULL);
  rt_sp = sv_sp;
  harness_set_call_sp(sv_call);
  return ret;
}

uint32_t rt_call_far(uint32_t fn, int conv, const uint16_t *args, int nargs) {
  uint16_t sel = FP_SEG(fn), off = FP_OFF(fn);
  const DtEntry *e;
  if (conv_near(conv)) {
    fprintf(stderr, "rt_call_far: NEAR convention (%d) is not supported; rt_call_far makes far calls\n", conv);
    abort();
  }
  e = g_mode == RT_MODE_DECOMP ? lookup_sel(sel, off) : NULL;
  if (e && e->conv != conv) {
    fprintf(stderr, "rt_call_far %u:%04x: convention mismatch: caller says %d, registry says %d\n", sel, off, conv,
            e->conv);
    abort();
  }
  if (e) {
    uint16_t sv_ss = rt_ss, sv_sp = rt_sp, sv_bp = rt_bp, sp;
    uint16_t sv_si = rt_entry_si, sv_di = rt_entry_di, new_bp = rt_bp, new_si = 0, new_di = 0;
    Regs16 regs;
    uint32_t ret;
    if (rt_sp != 0) {
      sp = rt_sp; /* an active C frame: build below it, keep its stack segment */
      /* The original callee pushes the caller's BP (the mirrored rt_bp); the caller's live SI/DI
       * are C locals the runtime cannot see, so those slots get 0. */
    } else if (harness_depth() > 0) {
      harness_regs(&regs);
      sp = regs.sp;
      rt_ss = regs.ss;
      new_bp = regs.bp; new_si = regs.si; new_di = regs.di; /* called back from the emulator */
    } else {
      sp = harness_stack_top();
      rt_ss = MEM_SEL_DGROUP;
      new_bp = 0;
    }
    memset(&regs, 0, sizeof regs);
    if (conv == FAR_CDECL) {
      for (int i = nargs - 1; i >= 0; i--) {
        sp = (uint16_t)(sp - 2);
        memcpy(LIN(MK_FP(rt_ss, sp)), &args[i], 2);
      }
    } else {
      for (int i = 0; i < nargs; i++) {
        sp = (uint16_t)(sp - 2);
        memcpy(LIN(MK_FP(rt_ss, sp)), &args[i], 2);
      }
    }
    sp = (uint16_t)(sp - 2);
    memcpy(LIN(MK_FP(rt_ss, sp)), &(uint16_t){MEM_SEL_THUNK}, 2);
    sp = (uint16_t)(sp - 2);
    memcpy(LIN(MK_FP(rt_ss, sp)), &(uint16_t){HARNESS_THUNK_RET_OFF}, 2);
    rt_sp = sp;
    rt_bp = new_bp;
    rt_entry_si = new_si;
    rt_entry_di = new_di;
    ret = e->adapter(rt_ss, rt_sp, &regs);
    rt_ss = sv_ss;
    rt_sp = sv_sp;
    rt_bp = sv_bp;
    rt_entry_si = sv_si;
    rt_entry_di = sv_di;
    return ret;
  }
  return call_emulated(sel, off, conv, args, nargs);
}

uint32_t rt_call_orig(unsigned seg, unsigned off, int conv, int nargs, ...) {
  uint16_t words[FUZZ_MAX_ARGS * 2];
  va_list ap;
  if (conv_near(conv)) {
    fprintf(stderr, "CALL_ORIG %u:%04x: NEAR convention (%d) is not supported; CALL_ORIG makes far calls\n", seg,
            off, conv);
    abort();
  }
  if (nargs < 0 || nargs > (int)(sizeof words / sizeof words[0])) abort();
  va_start(ap, nargs);
  for (int i = 0; i < nargs; i++) words[i] = (uint16_t)va_arg(ap, int);
  va_end(ap);
  return call_emulated(seg_sel((uint16_t)seg), (uint16_t)off, conv, words, nargs);
}

/* ---- whole-program DECOMP mode: decompiled C inside the emulator ---- */

/* rt_call_far only picks C for the function a scenario calls itself. For the golden master the
 * original code runs and calls the functions we have decompiled; each of those must run as C. The
 * entry of every FAR_CDECL function of the registry gets "JMP FAR thunk" written over its first
 * 5 bytes (the technique the segment scenarios use for test doubles); the thunk is a logged
 * import whose handler runs the registry adapter on the stack as the caller left it (return
 * address at SS:SP, arguments above), like rt_call_far does for a callback from the emulator.
 * A CDECL callee leaves its arguments to the caller, so the thunk's plain RETF is exactly the
 * return. FAR_PASCAL entries are window and dialog procedures that only the Win16 runtime calls,
 * and functions shorter than 5 bytes cannot hold the jump: both keep running as original code. */
#define DIVERT_MAX 512
static const DtEntry *g_divert[DIVERT_MAX];
static unsigned g_divert_calls;

static void divert_handler(HCall *c) {
  const DtEntry *e = g_divert[c->ordinal];
  g_divert_calls++;
  uint16_t sv_ss = rt_ss, sv_sp = rt_sp, sv_bp = rt_bp, sv_si = rt_entry_si, sv_di = rt_entry_di;
  Regs16 regs;
  memset(&regs, 0, sizeof regs);
  rt_ss = c->regs->ss;
  rt_sp = c->regs->sp;
  rt_bp = c->regs->bp;
  rt_entry_si = c->regs->si;
  rt_entry_di = c->regs->di;
  c->ret = e->adapter(rt_ss, rt_sp, &regs);
  rt_ss = sv_ss;
  rt_sp = sv_sp;
  rt_bp = sv_bp;
  rt_entry_si = sv_si;
  rt_entry_di = sv_di;
}

unsigned difftest_divert_calls(void) { return g_divert_calls; }

int difftest_divert_decomp(void) {
  int n = 0;
  g_divert_calls = 0;
  memset(g_divert, 0, sizeof g_divert);
  load_csv();
  for (unsigned i = 0; i < dt_registry_count && i < DIVERT_MAX; i++) {
    const DtEntry *e = difftest_lookup(dt_registry[i].seg, dt_registry[i].off);
    const Fn *f;
    uint32_t th;
    uint8_t jmp[5];
    uint8_t *code;
    if (!e || e->conv != FAR_CDECL || e->seg >= 0x100) continue;
    f = fn_find(e->seg, e->off, false);
    if (!f || f->size < 5) continue;
    th = harness_add_import("DECOMP", (uint16_t)i, e->name, HK_PASCAL, 0,
                            e->ret_words == 0 ? HR_NONE : e->ret_words == 1 ? HR_RET16 : HR_RET32,
                            divert_handler);
    if (!th) return -1;
    g_divert[i] = e;
    jmp[0] = 0xEA;
    memcpy(jmp + 1, &th, 4);
    code = LIN(MK_FP(MEM_SEL_SEG(e->seg), e->off));
    memcpy(code, jmp, sizeof jmp);
    n++;
  }
  return n;
}
