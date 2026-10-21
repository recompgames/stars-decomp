/* Diff tests for decompiled segment 22 (src/seg22.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 22 is the turn generation (see src/seg22.c). Every scenario builds its state by writing DGROUP
 * and a few far blocks directly (the planet table, the fleet pointer array and fleet records, the
 * players' rows and design tables); DIFF then snapshots it and compares the original with the C version
 * (return value, all memory outside the callee's own frame, the ordered API call log).
 *   22:0x1cfa  regenerate_fleet_fuel: refuel the fleets (starbase or fuel generators)
 *   22:0x0e92  run_fleet_order_phases: planet owner flag sync, then the fleet phases 22:0x3ec6
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/commdlg_thunks.h"
#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/kernel_thunks.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/user.h"
#include "runtime/vfs.h"
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
static uint16_t rw(uint32_t fp, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 2);
  return v;
}
static uint32_t rd(uint32_t fp, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 4);
  return v;
}
static void ww(uint32_t fp, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 2); }
static void wd(uint32_t fp, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 4); }
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

/* ---- the scenario world: players, designs, planets, fleets ---- */

#define NPLAYER 4
#define NFL_MAX 8
#define NPL_MAX 8
static uint32_t g_ptrs;            /* DS:0xba: the fleet pointer array */
static uint32_t g_fleet[NFL_MAX];  /* fleet records (0x7c bytes), spread over g_fblk */
static uint32_t g_fblk[3];
static uint32_t g_pln;             /* DS:0xb6: the planet table */
static uint32_t g_dsg[NPLAYER];    /* DS:0xbe + 4 * player: 16 ship designs of 0x8d bytes */
static uint32_t g_sbd[NPLAYER];    /* DS:0x10c + 4 * player: 10 starbase designs */

static uint32_t g_plr_rec;             /* the battle plan record every DS:0x59fa entry points to */
static void world_alloc(void) {
  g_plr_rec = gblock(0x40);
  g_ptrs = gblock(0x100);
  for (unsigned i = 0; i < 3; i++) g_fblk[i] = gblock(0x400);
  g_pln = gblock(0x38 * 16 + 0x40);
  for (unsigned p = 0; p < NPLAYER; p++) {
    g_dsg[p] = gblock(0x8d0);
    g_sbd[p] = gblock(0x8d * 16);
  }
}

/* The players' rows: random bytes, the relation bytes (+0x70) steered to 0, 1 or 2. */
static void world_players(void) {
  /* DS:0x59fa + 4 * player: far pointers to the players' battle plans (DS_BATTLE_PLAN_PTRS), read by 30:0x1ae2 (called by 30:0x24aa). After the
   * whole-turn scenario they would still point into that game, whose byte +3 (low 5 bits >= 4) makes 30:0x1ae2 read an
   * uninitialised stack word as a table index; the ORIGINAL and the C run leave different stack residue behind
   * (docs/CONVENTIONS.md rule d, known gaps). A zero record keeps that code on its first test. */
  memset(LIN(g_plr_rec), 0, 0x40);
  for (unsigned p = 0; p < 16; p++) ds_w32((uint16_t)(0x59fa + 4 * p), g_plr_rec);
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    for (unsigned i = 0; i < 16; i++) row[0x70 + i] = (uint8_t)(chance(1, 2) ? 1 : rn(3));
  }
}

/* A ship design: hull word and slots. hull_small keeps the hull below 0x20 (a record of segment 2). */
static void fill_design(uint32_t d, bool hot) {
  fill_rand(d, 0x8d);
  static const uint16_t hulls[] = {0, 1, 2, 3, 4, 5, 0xe, 0xf, 0x19, 0x1a, 0x1b, 0x1c, 0, 1, 2, 3};
  ww(d, 0, hulls[rn(sizeof hulls / 2)]);
  wb(d, 0x7a, (uint8_t)(chance(1, 6) ? 0 : chance(1, 8) ? 16 : 1 + rn(8)));
  for (unsigned k = 0; k < 8; k++) ww(d, (uint16_t)(0x28 + 2 * k), (uint16_t)(chance(1, 4) ? 0 : rn(1200))); /* mass and costs */
  ww(d, 0x38, (uint16_t)(1 + rn(1500))); /* the design's strength: a divisor */
  static const uint16_t cats[] = {1, 1, 2, 4, 8, 0x10, 0x20, 0x80, 0x80, 0x100, 0x100, 0x200, 0x200, 0x1000, 0x1000, 0};
  for (unsigned s = 0; s < 16; s++) {
    uint16_t cat = (uint16_t)(hot && chance(1, 2) ? 0x800 : cats[rn(sizeof cats / 2)]);
    ww(d, (uint16_t)(0x3a + 4 * s), cat);
    /* an item index inside the table of its category (2:0x50f8), item 0x10 of 0x800 generates fuel */
    ww(d, (uint16_t)(0x3c + 4 * s), (uint16_t)((cat == 0x800 ? (chance(1, 2) ? 0x10 : rn(5)) : cat == 1 ? rn(11) : cat == 0x200 ? rn(16) : rn(7)) | ((1 + rn(5)) << 8)));
  }
}

/* Planets 0 .. npl-1 (and some spare ones): owner, flags (bit 1 = starbase), starbase design nibble. */
static void world_planets(unsigned npl) {
  for (unsigned i = 0; i < 16; i++) {
    uint32_t pf = MK_FP(HI(g_pln), (uint16_t)(0x10 + 0x38 * i));
    fill_rand(pf, 0x38);
    ww(pf, 0, (uint16_t)i);
    ww(pf, 2, (uint16_t)(chance(1, 4) ? 0xffff : rn(NPLAYER)));
    wb(pf, 5, (uint8_t)(rnd() & (chance(1, 3) ? 0xdf : 0xff)));
    if (rw(pf, 2) == 0xffff) wb(pf, 5, (uint8_t)(*(uint8_t *)LIN(MK_FP(HI(pf), (uint16_t)(LO(pf) + 5))) & 0xfd)); /* a starbase has an owner */
    wb(pf, 0x2c, (uint8_t)(rnd() & 0xff));
    wd(pf, 0x34, 0); /* no production queue */
    for (unsigned k = 0; k < 4; k++) wd(pf, (uint16_t)(0x1c + 4 * k), chance(1, 4) ? 0 : rn(chance(1, 3) ? 200000 : 5000)); /* surface amounts */
  }
  ds_w32(0x00b6, MK_FP(HI(g_pln), 0x10));
  DS_W(0x4d96) = (uint16_t)npl;
}

/* nfl fleets. */
static void world_fleets(unsigned nfl, bool all_ok) {
  CHECK(nfl <= NFL_MAX);
  for (unsigned i = 0; i < NFL_MAX; i++) {
    g_fleet[i] = MK_FP(HI(g_fblk[i % 3]), (uint16_t)(0x10 + 0x80 * (i / 3)));
    memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * i))), &g_fleet[i], 4);
  }
  ds_w32(0x00ba, g_ptrs);
  DS_W(0x553a) = (uint16_t)nfl;
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    fill_rand(f, 0x7c);
    ww(f, 0, (uint16_t)(i + 1));
    ww(f, 2, (uint16_t)rn(NPLAYER));
    wb(f, 4, (uint8_t)(all_ok || chance(9, 10) ? 7 : rn(8)));
    ww(f, 6, (uint16_t)(chance(1, 3) ? 0xffff : rn(DS_W(0x4d96) + 2)));
    wb(f, 5, (uint8_t)(rnd() & (chance(3, 4) ? 0xfb : 0xff)));
    for (unsigned k = 0; k < 16; k++) ww(f, (uint16_t)(0xc + 2 * k), (uint16_t)(chance(1, 2) ? 0 : chance(1, 8) ? 0x8000 + rn(3) : rn(40)));
    wd(f, 0x5c, chance(1, 5) ? 0 : chance(1, 5) ? 0xfffffff0u + rn(16) : rnd() >> rn(32));
  }
}

static void world_all(unsigned nfl, unsigned npl) {
  DS_W(0x5a5a) = 0; /* no records in the 23:0x1e82 table (DS:0x988) */
  world_players();
  for (unsigned p = 0; p < NPLAYER; p++) {
    for (unsigned k = 0; k < 16; k++) fill_design(MK_FP(HI(g_dsg[p]), (uint16_t)(0x8d * k)), true);
    for (unsigned k = 0; k < 16; k++) fill_design(MK_FP(HI(g_sbd[p]), (uint16_t)(0x8d * k)), false);
    ds_w32((uint16_t)(0xbe + 4 * p), g_dsg[p]);
    ds_w32((uint16_t)(0x10c + 4 * p), g_sbd[p]);
  }
  world_planets(npl);
  world_fleets(nfl, false);
}

/* ---- 22:0x1cfa: refuel the fleets ---- */

static void t_1cfa(void) {
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x1cfa0001u + 0x9e3779b9u * (it + 1);
    unsigned nfl = it % 9 > NFL_MAX ? NFL_MAX : it % 9;
    world_all(nfl, 1 + rn(8));
    if (nfl && chance(1, 6)) {
      /* a null pointer ends the walk */
      uint32_t zero = 0;
      memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * rn(nfl)))), &zero, 4);
    }
    DIFF(22, 0x1cfa, FAR_CDECL);
  }
}

/* ---- 22:0x0e92: the flag sync and the fleet phases ---- */

static void t_0e92(void) {
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x0e920001u + 0x9e3779b9u * (it + 1);
    world_all(0, 1 + rn(8));
    DS_W(0x82) = (uint16_t)rnd();
    DIFF(22, 0x0e92, FAR_CDECL, (uint16_t)(it % 3 == 0 ? 0 : it % 3 == 1 ? 1 : rnd()));
  }
}


static uint32_t g_obj;                 /* DS:0x188e: map objects */

/* ---- scenario table and the debugging aids (compiled in with -DT22_TRACE) ---- */

typedef struct Scn {
  const char *tag;  /* SEG22_ONLY tag */
  uint16_t off;     /* function offset in segment 22 */
  unsigned iters;
  unsigned nargs;
  /* Rebuilds the (deterministic) world of iteration `it` and fills the argument words. */
  void (*build)(unsigned it, uint16_t *args);
  /* The DIFF of the function with those arguments (spelled out per function so each DIFF is visible to the linter). */
  DtStatus (*diff)(const uint16_t *a);
} Scn;

static DtStatus diff_3ec6(const uint16_t *a) { return DIFF(22, 0x3ec6, FAR_CDECL, a[0]); }
static DtStatus diff_30b6(const uint16_t *a) { return DIFF(22, 0x30b6, FAR_CDECL, a[0], a[1], a[2], a[3], a[4]); }
static DtStatus diff_1f18(const uint16_t *a) { (void)a; return DIFF(22, 0x1f18, FAR_CDECL); }
static DtStatus diff_0e92(const uint16_t *a) { return DIFF(22, 0x0e92, FAR_CDECL, a[0]); }
static DtStatus diff_0f6e(const uint16_t *a) { return DIFF(22, 0x0f6e, FAR_CDECL, a[0]); }

#ifdef T22_TRACE
/* SEG22_REPLAY=1 SEG22_IT=n runs iteration n once as the ORIGINAL (instruction trace) and once as C (label trace) and
 * writes both to SEG22_DUMP; SEG22_PROGRESS prints the iteration; SEG22_ALARM=s aborts a run that hangs after s
 * seconds and prints where; SEG22_WATCH=off watches the word at fleet 0 + off in both runs; SEG22_BP=seg:off prints the
 * stack words at each entry of that function in the original; SEG22_CHAIN prints the segments the original visited. */
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#define TR_N 48
static uint32_t g_tr[TR_N];
static unsigned g_trn;
static uint32_t g_s22_lo, g_s22_hi;
static uint16_t g_o22[65536];
static unsigned g_o22n;
static uint32_t g_segbase[36];
static unsigned g_prevsg;
#define TRE_N 256
static uint32_t g_tre[TRE_N]; /* (segment << 16) | offset of the first instruction of each run in one segment */
static unsigned g_tren;
static uint32_t g_owatch_fp, g_owatch_prev, g_bp_lin;
static uint16_t g_owatch_val;
uint16_t g_c22[4096];
unsigned g_c22n;
static uint16_t g_c22ring[32];
static unsigned g_c22rn;
static uint32_t g_watch_fp;
static uint16_t g_watch_val;

static unsigned seg_of_lin(uint32_t lin, uint32_t *base_out) {
  unsigned best = 0;
  uint32_t bbase = 0;
  for (unsigned sg = 1; sg <= 35; sg++)
    if (lin >= g_segbase[sg] && g_segbase[sg] >= bbase) best = sg, bbase = g_segbase[sg];
  *base_out = bbase;
  return best;
}
static void tr_chain(unsigned last) {
  fprintf(stderr, "segment entries (last %u):", last);
  unsigned n = g_tren < TRE_N ? g_tren : TRE_N;
  for (unsigned k = n > last ? n - last : 0; k < n; k++) {
    uint32_t r = g_tre[(g_tren - n + k) % TRE_N];
    fprintf(stderr, " %u:%04x", r >> 16, r & 0xffff);
  }
  fprintf(stderr, "\n");
}
static void tr_dump(void) {
  for (unsigned k = 0; k < TR_N && k < g_trn; k++) {
    uint32_t lin = g_trn < TR_N ? g_tr[k] : g_tr[(g_trn + k) % TR_N], base;
    unsigned sg = seg_of_lin(lin, &base);
    fprintf(stderr, "  %u:%04x", sg, (unsigned)(lin - base));
  }
  fprintf(stderr, "\n");
}
static void tr_hook(uint32_t lin, void *ud) {
  (void)ud;
  if (g_bp_lin && lin == g_bp_lin) {
    Regs16 r;
    harness_regs(&r);
    fprintf(stderr, "bp hit regs ax %04x bx %04x cx %04x dx %04x si %04x di %04x bp %04x ds %04x es %04x ss %04x\n", r.ax, r.bx, r.cx, r.dx, r.si, r.di, r.bp, r.ds, r.es, r.ss);
    fprintf(stderr, "bp hit, args:");
    for (unsigned k = 0; k < 12; k++) { if (k == 0) { uint16_t w0, w1; memcpy(&w0, LIN(MK_FP(r.ss, r.sp)), 2); memcpy(&w1, LIN(MK_FP(r.ss, (uint16_t)(r.sp + 2))), 2); fprintf(stderr, "ret %04x:%04x sp %04x ", w1, w0, r.sp); }
      uint16_t w;
      memcpy(&w, LIN(MK_FP(r.ss, (uint16_t)(r.sp + 4 + 2 * k))), 2);
      fprintf(stderr, " %04x", w);
    }
    fprintf(stderr, "\n");
  }
  if (g_owatch_fp) {
    uint16_t v;
    memcpy(&v, LIN(g_owatch_fp), 2);
    if (v != g_owatch_val) {
      uint32_t base;
      unsigned sg = seg_of_lin(g_owatch_prev, &base);
      fprintf(stderr, "owatch: %04x -> %04x written by %u:%04x\n", g_owatch_val, v, sg, (unsigned)(g_owatch_prev - base));
      g_owatch_val = v;
      tr_chain(14);
    }
    g_owatch_prev = lin;
  }
  {
    uint32_t base;
    unsigned sg = seg_of_lin(lin, &base);
    if (sg != g_prevsg) {
      g_tre[g_tren++ % TRE_N] = (sg << 16) | ((lin - base) & 0xffff);
      g_prevsg = sg;
    }
  }
  g_tr[g_trn++ % TR_N] = lin;
  if (lin >= g_s22_lo && lin < g_s22_hi && g_o22n < 65536) g_o22[g_o22n++] = (uint16_t)(lin - g_s22_lo);
}
void t22_hook(uint16_t at) {
  if (g_watch_fp) {
    uint16_t v;
    memcpy(&v, LIN(g_watch_fp), 2);
    if (v != g_watch_val) {
      fprintf(stderr, "watch: changed %04x -> %04x before label %04x (previous labels:", g_watch_val, v, at);
      for (unsigned k = 6; k > 0; k--) fprintf(stderr, " %04x", g_c22ring[(g_c22rn - k) % 32]);
      fprintf(stderr, ")\n");
      g_watch_val = v;
    }
  }
  g_c22ring[g_c22rn++ % 32] = at;
  if (g_c22n < 4096) g_c22[g_c22n++] = at;
}
static void dump_traces(const char *path) {
  FILE *fo = fopen(path, "w");
  if (!fo) return;
  for (unsigned k = 0; k < g_o22n; k++) fprintf(fo, "O %04x\n", g_o22[k]);
  for (unsigned k = 0; k < g_c22n; k++) fprintf(fo, "C %04x\n", g_c22[k]);
  fclose(fo);
}
static void on_alarm(int sig) {
  Regs16 r;
  if (getenv("SEG22_DUMP")) dump_traces(getenv("SEG22_DUMP"));
  if (sig == SIGABRT) fprintf(stderr, "ABORT (in the C version?)\n");
  harness_regs(&r);
  fprintf(stderr, "ALARM: emulator cs:ip %04x:%04x; last C labels:", r.cs, r.ip);
  for (unsigned k = 0; k < 32; k++) fprintf(stderr, " %04x", g_c22ring[(g_c22rn + k) % 32]);
  fprintf(stderr, "\n");
  tr_dump();
  tr_chain(40);
  {
    void *bt[24];
    int n = backtrace(bt, 24);
    backtrace_symbols_fd(bt, n, 2);
    fprintf(stderr, "on_alarm at %p\n", (void *)on_alarm);
  }
  _exit(3);
}
/* SEG22_WATCH=off: fleet 0 + off; SEG22_WATCHFP=sel:off: any address (hex) */
static uint32_t watch_fp(void) {
  unsigned sel, off;
  if (getenv("SEG22_WATCHFP") && sscanf(getenv("SEG22_WATCHFP"), "%x:%x", &sel, &off) == 2) return MK_FP((uint16_t)sel, (uint16_t)off);
  return MK_FP(HI(g_fleet[0]), (uint16_t)(LO(g_fleet[0]) + (uint16_t)strtoul(getenv("SEG22_WATCH"), NULL, 16)));
}
/* SEG22_MEMDIFF=1: run the ORIGINAL and the C version from one snapshot of the whole arena and list every byte
 * range that differs afterwards, the dead stack below SP included (DIFF leaves that out). */
static void memdiff(const Scn *sc, unsigned it) {
  uint16_t args[8] = {0};
  size_t n = mem_size();
  uint8_t *snap = malloc(n), *a = malloc(n);
  sc->build(it, args);
  memcpy(snap, mem_arena(), n);
  rt_set_mode(RT_MODE_ORIGINAL);
  (void)rt_call_far(MK_FP(SEG_SEL(22), sc->off), FAR_CDECL, args, (int)sc->nargs);
  memcpy(a, mem_arena(), n);
  memcpy(mem_arena(), snap, n);
  rt_set_mode(RT_MODE_DECOMP);
  (void)rt_call_far(MK_FP(SEG_SEL(22), sc->off), FAR_CDECL, args, (int)sc->nargs);
  rt_set_mode(RT_MODE_ORIGINAL);
  unsigned shown = 0;
  for (size_t i = 0; i < n && shown < 40;) {
    if (a[i] == mem_arena()[i]) { i++; continue; }
    size_t j = i;
    while (j < n && (a[j] != mem_arena()[j] || (j + 1 < n && a[j + 1] != mem_arena()[j + 1]))) j++;
    unsigned sel = 0, best_base = 0;
    for (unsigned sg = 8; sg < MEM_GDT_ENTRIES * 8u; sg += 8) {
      if (!(mem_gdt()[sg + 5] & 0x80)) continue;
      uint32_t b = desc_base((uint16_t)sg);
      if (i >= b && i <= (size_t)b + desc_limit((uint16_t)sg) && b >= best_base) sel = sg, best_base = b;
    }
    fprintf(stderr, "memdiff: arena %zx..%zx (%zu bytes) = %04x:%04zx  orig:", i, j, j - i, sel, i - best_base);
    for (size_t k = i; k < j && k < i + 8; k++) fprintf(stderr, " %02x", a[k]);
    fprintf(stderr, "  C:");
    for (size_t k = i; k < j && k < i + 8; k++) fprintf(stderr, " %02x", mem_arena()[k]);
    fprintf(stderr, "\n");
    shown++;
    i = j;
  }
  free(snap);
  free(a);
}
static void replay(const Scn *sc, unsigned it, const char *path) {
  if (getenv("SEG22_MEMDIFF")) {
    memdiff(sc, it);
    return;
  }
  uint16_t args[8];
  FILE *fo = fopen(path, "w");
  sc->build(it, args);
  for (unsigned sg = 1; sg <= 35; sg++) g_segbase[sg] = mem_off(LIN(MK_FP(SEG_SEL(sg), 0)));
  g_trn = g_tren = g_prevsg = g_o22n = 0;
  g_s22_lo = g_segbase[22];
  g_s22_hi = g_s22_lo + 0x6000;
  if (getenv("SEG22_WATCH") || getenv("SEG22_WATCHFP")) {
    uint32_t f = watch_fp();
    g_owatch_fp = f;
    memcpy(&g_owatch_val, LIN(f), 2);
  }
  if (getenv("SEG22_BP")) {
    unsigned bs, bo;
    if (sscanf(getenv("SEG22_BP"), "%u:%x", &bs, &bo) == 2) g_bp_lin = mem_off(LIN(MK_FP(SEG_SEL(bs), (uint16_t)bo)));
  }
  if (getenv("SEG22_WORLD")) {
    uint32_t fp = (uint32_t)args[0] | ((uint32_t)args[1] << 16);
    fprintf(stderr, "args %04x %04x %04x %04x %04x; fleet id %04x owner %u pos %u,%u wp0 %u,%u wp1 %u,%u (list %08x)\n", args[0], args[1], args[2], args[3], args[4], rw(fp, 0), rw(fp, 2), rw(fp, 8), rw(fp, 10), 0, 0, 0, 0, rw(fp, 0x64));
    for (unsigned i = 0; i < 4; i++) {
      uint32_t o = MK_FP(HI(g_obj), (uint16_t)(0x12 * i));
      fprintf(stderr, "object %u id %04x pos %u,%u r2 %u type %u\n", i, rw(o, 0), rw(o, 2), rw(o, 4), rw(o, 6) | (rw(o, 8) << 16), rw(o, 0xc) & 0xff);
    }
  }
  harness_trace(0, 0x10000000u, tr_hook, NULL);
  rt_set_mode(RT_MODE_ORIGINAL);
  (void)rt_call_far(MK_FP(SEG_SEL(22), sc->off), FAR_CDECL, args, (int)sc->nargs);
  harness_trace(0, 0, NULL, NULL);
  uint32_t bp_saved = g_bp_lin;
  g_owatch_fp = g_bp_lin = 0;
  if (getenv("SEG22_CHAIN")) tr_chain(20);
  sc->build(it, args);
  g_c22n = 0;
  if (getenv("SEG22_WATCH") || getenv("SEG22_WATCHFP")) {
    g_watch_fp = watch_fp();
    memcpy(&g_watch_val, LIN(g_watch_fp), 2);
  }
  rt_set_mode(RT_MODE_DECOMP);
  if (getenv("SEG22_BPC")) { g_bp_lin = bp_saved; fprintf(stderr, "--- C run\n"); harness_trace(0, 0x10000000u, tr_hook, NULL); }
  (void)rt_call_far(MK_FP(SEG_SEL(22), sc->off), FAR_CDECL, args, (int)sc->nargs);
  harness_trace(0, 0, NULL, NULL);
  g_watch_fp = 0;
  rt_set_mode(RT_MODE_ORIGINAL);
  fclose(fo);
  dump_traces(path);
}
#endif

static void run_scn(const Scn *sc) {
  const char *only_it = getenv("SEG22_IT");
  const unsigned iters = getenv("SEG22_N") ? (unsigned)atoi(getenv("SEG22_N")) : sc->iters;
  for (unsigned it = 0; it < iters; it++) {
    uint16_t args[8] = {0};
    if (only_it && (unsigned)atoi(only_it) != it) continue;
#ifdef T22_TRACE
    if (getenv("SEG22_PROGRESS")) fprintf(stderr, "it %u\n", it);
    if (getenv("SEG22_REPLAY")) {
      replay(sc, it, getenv("SEG22_DUMP") ? getenv("SEG22_DUMP") : "/dev/null");
      continue;
    }
#endif
    sc->build(it, args);
    DtStatus st = sc->diff(args);
#ifdef T22_TRACE
    if (st != DT_PASS) {
      fprintf(stderr, "iteration %u\n", it);
      if (getenv("SEG22_DUMP")) replay(sc, it, getenv("SEG22_DUMP"));
    }
#else
    (void)st;
#endif
  }
}

/* ---- the world of 22:0x3ec6: players, planets, fleets with waypoint lists, map objects ---- */

#define W_NPL 5
#define W_NFL 6
static uint32_t g_msg;                 /* DS:0xaac: the message buffer */
static uint32_t g_wpl[W_NFL];          /* waypoint list blocks */
static uint32_t g_rec;                 /* DS:0x988: the 12-byte records 23:0x1e82 walks (count DS:0x5a5a) */
static uint32_t g_a984;                /* DS:0x984: one word per planet (a per-planet counter table) */

static void world3_alloc(void) {
  g_msg = gblock(0x10000 - 16);
  for (unsigned i = 0; i < W_NFL; i++) g_wpl[i] = gblock(4 + 0x12 * 8);
  g_obj = gblock(0x12 * 64);
  g_a984 = gblock(0x200);
  g_rec = gblock(12 * 256);
}

/* sorted fleet ids: (owner << 9) | k.  counts[p] fleets of player p, in player order. */
static void world3(unsigned nfl, unsigned npl) {
  static const uint16_t tasks[] = {1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 8, 8, 9, 9, 6, 7, 0};
  static const uint16_t kinds[] = {1, 1, 1, 1, 1, 2, 2, 4, 8};
  CHECK(nfl <= W_NFL && npl <= W_NPL);
  world_all(0, npl);
  ds_w32(0x00ba, g_ptrs);
  uint16_t cnt[NPLAYER] = {0};
  uint16_t owner[W_NFL];
  for (unsigned i = 0; i < nfl; i++) owner[i] = (uint16_t)rn(NPLAYER);
  for (unsigned a = 0; a < nfl; a++)
    for (unsigned b = a + 1; b < nfl; b++)
      if (owner[b] < owner[a]) { uint16_t t = owner[a]; owner[a] = owner[b]; owner[b] = t; }
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
    row[4] = 0; row[5] = (uint8_t)(rnd() & 0xf0);
  }
  for (unsigned i = 0; i < nfl; i++) {
    uint32_t f = g_fleet[i];
    uint16_t o = owner[i], id = (uint16_t)((o << 9) | cnt[o]);
    cnt[o]++;
    fill_rand(f, 0x7c);
    ww(f, 0, id);
    ww(f, 2, o);
    wb(f, 4, 7);
    wb(f, 5, (uint8_t)(chance(1, 8) ? 4 : (rnd() & 0xfb)));
    uint16_t at_planet = chance(3, 4) ? (uint16_t)rn(npl) : 0xffff;
    ww(f, 6, at_planet);
    if (at_planet != 0xffff && chance(1, 2)) ww(g_pln, (uint16_t)(0x10 + 0x38 * at_planet + 2), o); /* the planet is the fleet owner's */
    ww(f, 0x8, (uint16_t)(1000 + rn(120))); ww(f, 0xa, (uint16_t)(1000 + rn(120)));
    for (unsigned k = 0; k < 16; k++) ww(f, (uint16_t)(0xc + 2 * k), (uint16_t)(chance(1, 2) ? 0 : 1 + rn(chance(1, 3) ? 200 : 30)));
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), chance(1, 3) ? 0 : rn(chance(1, 4) ? 20000 : 3000));
    wd(f, 0x5c, chance(1, 4) ? 0 : rn(chance(1, 3) ? 300000 : chance(1, 4) ? 60000 : 5000));
    for (unsigned k = 0; k < 16; k++) ww(f, (uint16_t)(0x2c + 2 * k), (uint16_t)(chance(2, 3) || rw(f, (uint16_t)(0xc + 2 * k)) == 0 ? 0 : (rn(300) << 7) | rn(101))); /* damage only where there are ships: percent in the low 7 bits */
    ww(f, 0x62, (uint16_t)(1 + rn(3)));
    wd(f, 0x64, g_wpl[i]);
    wb(f, 0x76, (uint8_t)(chance(1, 8) ? 0x80 : chance(1, 4) ? 0x60 : 0));
    wb(f, 0x7c - 4 + 3, 0);
    /* the waypoint list: header + 3 waypoints */
    uint32_t w = g_wpl[i];
    memset(LIN(w), 0, 4 + 0x12 * 8);
    wb(w, 3, 3);
    for (unsigned k = 0; k < 3; k++) {
      uint16_t b = (uint16_t)(4 + 0x12 * k);
      ww(w, b, (uint16_t)(1000 + rn(120))); ww(w, (uint16_t)(b + 2), (uint16_t)(1000 + rn(120)));
      uint16_t kind = kinds[rn(sizeof kinds / 2)];
      /* waypoint 0 is the fleet's own position: it targets a planet exactly when the fleet is at that planet (the game
       * loads the planet record into the order code's buffer only then; a planet target without a planet leaves the
       * buffer uninitialised, and its stale stack contents are not part of the behaviour under test). */
      if (k == 0 && kind == 1 && at_planet == 0xffff) kind = 4;
      uint16_t tid = kind == 1 ? (k == 0 ? at_planet : (at_planet != 0xffff && chance(3, 5) ? at_planet : (uint16_t)rn(npl))) : kind == 2 ? (uint16_t)((rn(NPLAYER) << 9) | rn(3)) : kind == 8 ? (uint16_t)(0x100 + rn(4)) : (uint16_t)rn(2000);
      ww(w, (uint16_t)(b + 4), tid);
      ww(w, (uint16_t)(b + 6), (uint16_t)(tasks[rn(sizeof tasks / 2)] | (kind << 8) | (rn(256) & 0xf0)));
      for (unsigned c = 0; c < 5; c++) {
        uint16_t type = (uint16_t)(chance(1, 4) ? 0 : 1 + rn(9));
        ww(w, (uint16_t)(b + 8 + 2 * c), (uint16_t)((type << 12) | (chance(1, 3) ? rn(5) : chance(1, 2) ? rn(0xfff) : rn(300))));
      }
      if ((rw(w, (uint16_t)(b + 6)) & 0xf) == 9) ww(w, (uint16_t)(b + 8), (uint16_t)rn(NPLAYER)); /* task 9 gives the fleet to player word0 */
    }
  }
  for (unsigned p = 0; p < NPLAYER; p++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
    row[4] = (uint8_t)cnt[p];
  }
  /* a waypoint that targets a fleet names one that exists (the original does not check) */
  for (unsigned i = 0; i < nfl; i++)
    for (unsigned k = 0; k < 3; k++) {
      uint16_t b = (uint16_t)(4 + 0x12 * k);
      uint32_t w = g_wpl[i];
      if (((rw(w, (uint16_t)(b + 6)) >> 8) & 0xf) == 2) ww(w, (uint16_t)(b + 4), rw(g_fleet[rn(nfl)], 0));
    }
  DS_W(0x553a) = (uint16_t)nfl;
  ds_w32(0x00ba, g_ptrs);
  for (unsigned i = 0; i < NFL_MAX; i++) memcpy(LIN(MK_FP(HI(g_ptrs), (uint16_t)(4 * i))), &g_fleet[i], 4);
  DS_W(0x007a) = (uint16_t)npl;
  DS_W(0x4d96) = (uint16_t)npl;
  DS_W(0x0078) = NPLAYER;
  for (unsigned i = 0; i < 16; i++) { /* the planets' positions, DS:0x31ba + 4 * id */
    DS_W((uint16_t)(0x31ba + 4 * i)) = (uint16_t)(1000 + rn(120));
    DS_W((uint16_t)(0x31bc + 4 * i)) = (uint16_t)(1000 + rn(120));
  }
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * p));
    static const uint8_t races[] = {0, 1, 2, 3, 4, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9, 9};
    for (unsigned a = 0; a < 14; a++) row[0x3e + a] = (uint8_t)(5 + rn(90)); /* the race's attributes */
    row[0x4c] = races[rn(sizeof races)]; /* player attribute 14 (a trait of the race) */
    row[0x54] = (uint8_t)(chance(1, 3) ? rn(8) : 0); /* byte +0x54: DS:0x5ab8, bit 0 and bit 2 are tested */
  }
  /* map objects: 4 records (minefields, some of another kind) whose ids the waypoints of kind 8 name */
  uint16_t objid[8];
  const unsigned nobj = 4 + rn(5);
  for (unsigned k = 0; k < nobj; k++) objid[k] = (uint16_t)((rn(NPLAYER) << 9) | (chance(1, 3) ? (uint16_t)(0x2000 * (1 + rn(3))) : 0) | (k + 1));
  for (unsigned a = 0; a < nobj; a++) /* the array is kept sorted by its first word (33:0x0000 inserts into it) */
    for (unsigned b = a + 1; b < nobj; b++)
      if (objid[b] < objid[a]) { uint16_t t = objid[a]; objid[a] = objid[b]; objid[b] = t; }
  for (unsigned k = 0; k < 8; k++) {
    uint32_t o = MK_FP(HI(g_obj), (uint16_t)(0x12 * k));
    fill_rand(o, 0x12);
    ww(o, 0, k < nobj ? objid[k] : (uint16_t)(0x7000 + k));
    uint32_t near_fleet = nfl ? g_fleet[rn(nfl)] : g_fleet[0];
    ww(o, 2, (uint16_t)(rw(near_fleet, 8) + (chance(1, 2) ? 0 : rn(40)) - 20));
    ww(o, 4, (uint16_t)(rw(near_fleet, 10) + (chance(1, 2) ? 0 : rn(40)) - 20));
    if ((rw(o, 0) & 0xe000) == 0) { /* a minefield: radius squared (a dword), mine type */
      wd(o, 6, chance(1, 4) ? 0 : rn(chance(1, 2) ? 4000 : 999000));
      wb(o, 0xc, (uint8_t)rn(3));
    } else { /* a salvage or packet: three mineral amounts */
      for (unsigned c = 0; c < 3; c++) ww(o, (uint16_t)(8 + 2 * c), (uint16_t)(chance(1, 3) ? 0 : rn(2000)));
      if (k < nobj && (rw(o, 0) & 0xe000) == 0x4000) ww(o, 0xc, objid[rn(nobj)]); /* it names another object (7:0x01a0 finds it) */
      if (k < nobj && (rw(o, 0) & 0xe000) == 0x2000) ww(o, 6, (uint16_t)(rn(npl) | (rn(11) << 10))); /* a packet: its target planet id and warp */
    }
  }
  for (unsigned i2 = 0; i2 < nfl; i2++)
    for (unsigned k = 0; k < 3; k++) {
      uint16_t b = (uint16_t)(4 + 0x12 * k);
      uint32_t w = g_wpl[i2];
      if (((rw(w, (uint16_t)(b + 6)) >> 8) & 0xf) == 8) {
        uint16_t pick = objid[rn(nobj)];
        for (unsigned q = 0; q < nobj && chance(1, 2); q++) /* often a wormhole (kind 0x4000) */
          if ((objid[q] & 0xe000) == 0x4000) pick = objid[q];
        ww(w, (uint16_t)(b + 4), pick);
      }
    }
  ds_w32(0x188e, g_obj);
  DS_W(0x1892) = (uint16_t)nobj;
  DS_W(0x1894) = 64; /* capacity: 33:0x0000 inserts without growing the array (no heap) */
  /* the message buffer */
  ds_w32(0x0aac, g_msg);
  DS_W(0x0ab0) = 0;
  DS_W(0x0ab4) = 0;
  fill_rand(g_a984, 0x200);
  ds_w32(0x0984, g_a984);
  DS_B(0x794) = (uint8_t)rnd();
  DS_B(0x790) = (uint8_t)rnd();
  DS_W(0x14c) = 0xffff;
  /* The records of 23:0x1e82 (DS:0x988, count DS:0x5a5a) are appended by 23:* during the pass; a record field they do not
   * write must not be stale memory: a stale planet id (field +4) makes 7:0x0358 store an uninitialised buffer, which
   * differs between the ORIGINAL and the C run only through stack residue. */
  memset(LIN(g_rec), 0, 12 * 256);
  ds_w32(0x0988, g_rec);
  DS_W(0x5a5a) = 0;
  DS_W(0x82) = (uint16_t)(chance(1, 4) ? rn(12) : 8 + rn(60)); /* the turn */
  ds_w32(0x151e, 1 + rn(0x7fffffa0u)); /* the state of the game's random generator 8:0x8730 */
  ds_w32(0x1522, 1 + rn(0x7ffffeffu));
}

static void build_3ec6(unsigned it, uint16_t *args) {
  g_rng = 0x3ec60001u + 0x9e3779b9u * (it + 1);
  world3(2 + it % 5, 1 + rn(W_NPL));
  args[0] = (uint16_t)(1 + it % 4);
}

/* Replaces the map objects by 2 to 8 minefields of other owners, spread along the fleet's course (fleet position to its second
 * waypoint) with random radii and types, so the crossings of one type overlap, touch, come in any order and fill the list of
 * eight (22:0x30b6 merges, inserts and drops them). */
static void lay_minefields(uint32_t fl) {
  uint32_t wl;
  memcpy(&wl, LIN(MK_FP(HI(fl), (uint16_t)(LO(fl) + 0x64))), 4);
  const int sx = rw(fl, 8), sy = rw(fl, 10), ex = rw(wl, 4 + 0x12), ey = rw(wl, 4 + 0x12 + 2);
  const uint16_t owner = rw(fl, 2);
  const unsigned n = 2 + rn(7);
  const uint8_t main_type = (uint8_t)rn(2); /* most fields share a type, so its list fills up and merges */
  uint16_t owners[8];
  for (unsigned k = 0; k < n; k++) {
    uint16_t o = (uint16_t)rn(NPLAYER);
    owners[k] = o == owner ? (uint16_t)((o + 1) % NPLAYER) : o;
  }
  for (unsigned a = 0; a < n; a++) /* ids ascend: the owner is in the id's high bits */
    for (unsigned b = a + 1; b < n; b++)
      if (owners[b] < owners[a]) { uint16_t t = owners[a]; owners[a] = owners[b]; owners[b] = t; }
  for (unsigned k = 0; k < n; k++) {
    uint32_t o = MK_FP(HI(g_obj), (uint16_t)(0x12 * k));
    fill_rand(o, 0x12);
    ww(o, 0, (uint16_t)((owners[k] << 9) | (k + 1)));
    const int t = (int)rn(101); /* the position along the course, in percent */
    ww(o, 2, (uint16_t)(sx + (ex - sx) * t / 100 + (int)rn(7) - 3));
    ww(o, 4, (uint16_t)(sy + (ey - sy) * t / 100 + (int)rn(7) - 3));
    const uint32_t radius = 2 + rn(chance(1, 2) ? 12 : 30);
    wd(o, 6, radius * radius);
    wb(o, 0xc, chance(3, 4) ? main_type : (uint8_t)rn(3));
  }
  DS_W(0x1892) = (uint16_t)n;
}

static void build_30b6(unsigned it, uint16_t *args) {
  g_rng = 0x30b60001u + 0x9e3779b9u * (it + 1);
  world3(1 + it % W_NFL, 1 + rn(W_NPL));
  uint32_t fl = g_fleet[rn(DS_W(0x553a))];
  args[0] = LO(fl);
  args[1] = HI(fl);
  args[2] = 0x3000;
  DS_W(0x3000) = (uint16_t)(chance(1, 8) ? rn(20) : 20 + rn(chance(1, 2) ? 400 : 30000));
  uint32_t ob = chance(1, 3) ? MK_FP(HI(g_obj), (uint16_t)(0x12 * rn(4))) : 0; /* the minefield that was hit (23:0x42a0), or 0 */
  if (!ob && chance(3, 4)) lay_minefields(fl);
  args[3] = LO(ob);
  args[4] = HI(ob);
}

static void build_0f6e(unsigned it, uint16_t *args) {
  g_rng = 0x0f6e0001u + 0x9e3779b9u * (it + 1);
  world3(1 + it % W_NFL, 1 + rn(W_NPL));
  args[0] = (uint16_t)(it & 1);
}

static void build_0e92(unsigned it, uint16_t *args) {
  g_rng = 0x0e920001u + 0x9e3779b9u * (it + 1);
  world3(it % W_NFL, 1 + rn(W_NPL));
  args[0] = (uint16_t)(it % 3 == 0 ? 0 : it % 3 == 1 ? 1 : 1 + rn(0xffff));
}

static void build_1f18(unsigned it, uint16_t *args) {
  (void)args;
  g_rng = 0x1f180001u + 0x9e3779b9u * (it + 1);
  world3(3 + it % 4, 1 + rn(W_NPL));
}

/* ---- 22:0x0000: the whole turn, on a game made by 15:0x47f0 ---- */

/* Rewrite the host file (14:0x35b0, as 22:0x0000 does at its end) from the game in memory, so that the next turn loads the edits. */
static void resave_host(void) {
  const uint16_t args[3] = {0x5764, 0xffff, 0};
  rt_set_mode(RT_MODE_ORIGINAL);
  (void)rt_call_far(MK_FP(SEG_SEL(14), 0x35b0), FAR_CDECL, args, 3);
}
/* Player p becomes a human player (Player.status_flags bit 1 clear): the turn then reads and checks his orders file. */
static void make_human(unsigned p) { DS_B(0x5a6b + 0xc0 * p) &= (uint8_t)~2u; }
/* Fleet 0 gets one waypoint that follows fleet 1 (kind 2): the follow loop of the turn gives it a route (or drops it). */
static void make_follower(bool followed_has_route) {
  uint32_t table;
  memcpy(&table, LINN(0x00ba), 4);
  if (*(uint16_t *)LINN(0x553a) < 2) return;
  const uint32_t f0 = rd(table, 0), f1 = rd(table, 4);
  if (!f0 || !f1) return;
  const uint32_t list = rd(f0, 0x64);
  ww(f0, 0x62, 1);
  if (followed_has_route) ww(f1, 0x62, 2); /* it has a route of its own: the follower takes it over */
  ww(list, 4 + 4, rw(f1, 0));
  ww(list, 4 + 6, (uint16_t)((rw(list, 4 + 6) & 0xf0ff) | 0x0200));
}

/* Player 0's first design loses its hull category: the turn puts the first slot back. */
static void break_design(void) {
  uint32_t designs;
  memcpy(&designs, LINN(0x00be), 4);
  wb(designs, 0x7c, (uint8_t)(rd(designs, 0x7c) & ~2u));
  ww(designs, 0x3a, 0x0777);
  wb(designs, 0x3d, 0);
}
/* Player p gets an out-of-range growth rate and cost adjustment, every trait and the highest attributes; the turn puts them right. */
static void break_race(unsigned p, uint8_t growth) {
  DS_B(0x5a6b + 0xc0 * p) &= (uint8_t)~2u;
  DS_B(0x5a64 + 0xc0 * p + 0x19) = growth;
  DS_B(0x5a64 + 0xc0 * p + 0x38) = 0x70; /* a cost adjustment above 100 percent */
  for (unsigned i = 0; i < 4; i++) DS_B(0x5a64 + 0xc0 * p + 0x4e + i) = 0xff;
  for (unsigned i = 0; i < 15; i++) DS_B(0x5a64 + 0xc0 * p + 0x3e + i) = 0x7f;
}

static void t_0000(void) {
  strcpy((char *)LINN(0x5764), "GOLD");
  rt_set_mode(RT_MODE_ORIGINAL);
  (void)rt_call_far(MK_FP(SEG_SEL(15), 0x47f0), FAR_CDECL, NULL, 0);
  /* DS:0x773 high nibble (a game mode), DS:0x791 bit 3 (re-seed the generator), DS:0x790 bit 0x800 */
  for (unsigned turn = 0; turn < 12; turn++) {
    if (getenv("SEG22_PROGRESS")) fprintf(stderr, "turn %u\n", turn);
    DS_B(0x773) = (uint8_t)((DS_B(0x773) & 0x0f) | ((turn & 1) ? 0x10 : 0));
    DS_B(0x791) = (uint8_t)((DS_B(0x791) & ~8u) | ((turn & 2) ? 8 : 0));
    DS_W(0x790) = (uint16_t)((DS_W(0x790) & ~0x800u) | ((turn & 4) ? 0x800 : 0));
    if (turn % 3 != 2) { /* the computer players write their orders first (4:0x4dba does the same before the turn) */
      DS_B(0x791) &= (uint8_t)~4u;
      (void)rt_call_far(MK_FP(SEG_SEL(4), 0x39f8), FAR_CDECL, NULL, 0);
    }
    if (turn == 3 || turn == 7) { make_human(1); resave_host(); }
    if (turn == 4 || turn == 8 || turn == 9) { make_follower(turn == 9); resave_host(); }
    if (turn == 5) { break_design(); resave_host(); }
    if (turn == 6) { break_race(2, 25); resave_host(); }
    if (turn == 10) { break_race(2, 0xf0); resave_host(); }
    if (turn == 10) { strcpy((char *)LINN(0x5764), "C:\\STARS\\GOLD"); strcpy((char *)LINN(0x5731), "BAK\\"); }
    DIFF(22, 0x0000, FAR_CDECL);
    if (turn == 10) { strcpy((char *)LINN(0x5764), "GOLD"); DS_B(0x5731) = 0; }
  }
  /* no game files: 14:0x0508 fails and the turn ends at once */
  (void)vfs_unlink("C:\\STARS\\GOLD.HST");
  DIFF(22, 0x0000, FAR_CDECL);
}

static const Scn g_scns[] = {
    {"3ec6", 0x3ec6, 7000, 1, build_3ec6, diff_3ec6},
    {"30b6", 0x30b6, 3500, 5, build_30b6, diff_30b6},
    {"1f18", 0x1f18, 7000, 0, build_1f18, diff_1f18},
    {"0e92f", 0x0e92, 1000, 1, build_0e92, diff_0e92},
    {"0f6e", 0x0f6e, 7000, 1, build_0f6e, diff_0f6e},
};

static void load_module(void) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}

int main(void) {
  g_only = getenv("SEG22_ONLY");
#ifdef T22_TRACE
  if (getenv("SEG22_ALARM") || getenv("SEG22_TRACE")) signal(SIGABRT, on_alarm);
  if (getenv("SEG22_ALARM")) {
    signal(SIGALRM, on_alarm);
    alarm((unsigned)atoi(getenv("SEG22_ALARM")));
  }
#endif
  if (harness_load(EXE) != 0) return 1;
  kernel_register();
  load_module();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  commdlg_register();
  toolhelp_win87em_register();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* LocalAlloc */
  world_alloc();
  world3_alloc();
  if (want("0000")) t_0000();
  memset(LINN(0x0d74), 0, 12 * 4); /* the turn left heap segments behind: the other scenarios start with empty heaps (DS:0xD74) */
  if (want("1cfa")) t_1cfa();
  if (want("0e92")) t_0e92();
  for (unsigned k = 0; k < sizeof g_scns / sizeof g_scns[0]; k++)
    if (want(g_scns[k].tag)) run_scn(&g_scns[k]);
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
