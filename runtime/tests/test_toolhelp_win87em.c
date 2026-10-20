/* TOOLHELP.TimerCount and WIN87EM.__fpMath (task stars-0z4.8). TimerCount is called both
 * directly and through its harness thunk; __fpMath is register based, so it is called
 * through its thunk with harness_call (BX selects the operation), and the FPU effects are
 * read back from the emulated x87. Semantics: Wine dlls/win87em.dll16 (WIN87_Init,
 * WIN87_SetCtrlWord, WIN87_ClearCtrlWord, case 0/2/3/11). Run from the repo root. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "harness/harness.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "runtime/include/win16.h"
#include "runtime/kernel.h"
#include "runtime/mem.h"
#include "runtime/toolhelp.h"
#include "runtime/vfs.h"
#include "runtime/win87em.h"
#include "test.h"

#define EXE "original/stars26i.exe"

/* ---- TimerCount ---- */

typedef struct {
  uint32_t dwSize, dwmsSinceStart, dwmsThisVM;
} TimerInfo;

static FARPTR g_ti_fp;
static TimerInfo *g_ti;

static void ti_fill(uint32_t size) {
  g_ti->dwSize = size;
  g_ti->dwmsSinceStart = 0xDEADBEEF;
  g_ti->dwmsThisVM = 0xCAFEF00D;
}

static uint32_t thunk_for(const char *mod, uint16_t ord, const char *name, int kind,
                          uint16_t argbytes, int ret) {
  uint32_t fp = harness_thunk_far(mod, ord);
  if (!fp) fp = harness_add_import(mod, ord, name, kind, argbytes, ret, NULL);
  CHECK(fp != 0);
  return fp;
}

static void test_timercount_direct(void) {
  /* default clock 1980-01-01 00:00:00 -> 0 ms */
  rt_clock_set(1980, 1, 1, 0, 0, 0);
  ti_fill(12);
  CHECK_EQ(TimerCount(g_ti_fp), 1);
  CHECK_EQ(g_ti->dwSize, 12);
  CHECK_EQ(g_ti->dwmsSinceStart, 0);
  CHECK_EQ(g_ti->dwmsThisVM, 0);

  rt_clock_set(1980, 1, 2, 0, 0, 1);
  ti_fill(12);
  CHECK_EQ(TimerCount(g_ti_fp), 1);
  CHECK_EQ(g_ti->dwmsSinceStart, 86401000u);
  CHECK_EQ(g_ti->dwmsThisVM, 86401000u);

  /* 1981-03-04 05:06:07 = 36997567 s since the epoch; x1000 wraps modulo 2^32 */
  rt_clock_set(1981, 3, 4, 5, 6, 7);
  ti_fill(12);
  CHECK_EQ(TimerCount(g_ti_fp), 1);
  CHECK_EQ(g_ti->dwmsSinceStart, (uint32_t)36997567000ULL);
  CHECK_EQ(g_ti->dwmsThisVM, (uint32_t)36997567000ULL);

  /* the clock moves: the next call sees the new time */
  rt_clock_set(1981, 3, 4, 5, 6, 8);
  ti_fill(12);
  CHECK_EQ(TimerCount(g_ti_fp), 1);
  CHECK_EQ(g_ti->dwmsSinceStart, (uint32_t)36997568000ULL);
}

static void test_timercount_bad_size(void) {
  static const uint32_t bad[] = {0, 8, 11, 13, 0xFFFFFFFFu};
  rt_clock_set(1980, 1, 2, 0, 0, 1);
  for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    ti_fill(bad[i]);
    CHECK_EQ(TimerCount(g_ti_fp), 0);
    CHECK_EQ(g_ti->dwSize, bad[i]);
    CHECK_EQ(g_ti->dwmsSinceStart, 0xDEADBEEFu); /* untouched */
    CHECK_EQ(g_ti->dwmsThisVM, 0xCAFEF00Du);
  }
  CHECK_EQ(TimerCount(0), 0);                      /* null */
  CHECK_EQ(TimerCount(MK_FP(0x7FF8, 0)), 0);       /* not a selector */
}

static void test_timercount_thunk(void) {
  uint32_t fp = thunk_for("TOOLHELP", 80, "TimerCount", HK_PASCAL, 4, HR_RET16);
  uint16_t a[2] = {FP_SEG(g_ti_fp), FP_OFF(g_ti_fp)}; /* PASCAL: high word first */
  rt_clock_set(1980, 1, 2, 0, 0, 1);
  ti_fill(12);
  CHECK_EQ(harness_call_far(fp, HCONV_PASCAL, a, 2) & 0xFFFF, 1);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(g_ti->dwmsSinceStart, 86401000u);
  ti_fill(0);
  CHECK_EQ(harness_call_far(fp, HCONV_PASCAL, a, 2) & 0xFFFF, 0);
  CHECK_EQ(g_ti->dwmsSinceStart, 0xDEADBEEFu);
}

/* ---- __fpMath ---- */

/* Calls the WIN87EM.1 thunk with BX = code and the other registers from io (ax/dx/flags
 * in, results out). */
static void fp_call(uint16_t bx, Regs16 *io) {
  uint32_t fp = thunk_for("WIN87EM", 1, "__fpMath", HK_REGISTER, 0, HR_NONE);
  io->bx = bx;
  harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, io);
  CHECK_EQ(harness_last_error(), 0);
}

static void test_fpmath_init(void) {
  static const uint16_t codes[] = {0, 2};
  for (unsigned i = 0; i < 2; i++) {
    Regs16 r;
    memset(&r, 0, sizeof r);
    r.ax = 0x1234; r.dx = 0x5678; r.flags = 1; /* CF set on entry */
    harness_set_fpcw(0x0000);
    harness_set_fpsw(0x8081); /* ES, B, IE pending */
    fp_call(codes[i], &r);
    CHECK_EQ(r.ax, 0);                    /* WIN87_Init ends with ClearCtrlWord: AX = 0 */
    CHECK_EQ(r.dx, 0x5678);               /* DX untouched */
    CHECK_EQ(r.flags & 1, 0);             /* CF clear: the C runtime tests it (JNC) */
    CHECK_EQ(harness_fpcw(), 0x1330);     /* FLDCW (0x1332 & ~0xC3) */
    CHECK_EQ(harness_fpsw() & 0x80FF, 0); /* FNCLEX */
    CHECK_EQ(win87em_ctrl_word_1(), 0x1332);
  }
}

static void test_fpmath_handler(void) {
  Regs16 r;
  memset(&r, 0, sizeof r);
  r.ax = 0x1DB4; r.dx = 0x0110; r.es = MEM_SEL_DGROUP; r.flags = 1;
  harness_set_fpcw(0x1330);
  fp_call(3, &r);
  CHECK_EQ(r.ax, 0x1DB4);               /* Wine: no register or FPU effect */
  CHECK_EQ(r.dx, 0x0110);
  CHECK_EQ(r.es, MEM_SEL_DGROUP);
  CHECK_EQ(harness_fpcw(), 0x1330);
  CHECK_EQ(win87em_handler(), MK_FP(0x0110, 0x1DB4)); /* recorded for the arena state */
}

static void test_fpmath_installed(void) {
  Regs16 r;
  memset(&r, 0, sizeof r);
  r.ax = 0x4321; r.dx = 0x8765;
  harness_set_fpcw(0x1330);
  fp_call(0x0B, &r);
  CHECK_EQ(r.ax, 1); /* Wine: Installed flag in DX:AX */
  CHECK_EQ(r.dx, 0);
  CHECK_EQ(harness_fpcw(), 0x1330);
}

static void test_fpmath_unknown(void) {
  static const uint16_t codes[] = {1, 4, 5, 6, 7, 8, 9, 10, 12, 0x0D, 0x100};
  Regs16 r;
  /* lenient: Wine's error result AX = DX = 0xFFFF, no FPU change */
  harness_set_strict(false);
  for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
    memset(&r, 0, sizeof r);
    r.ax = 0x1111; r.dx = 0x2222;
    harness_set_fpcw(0x1330);
    fp_call(codes[i], &r);
    CHECK_EQ(r.ax, 0xFFFF);
    CHECK_EQ(r.dx, 0xFFFF);
    CHECK_EQ(harness_fpcw(), 0x1330);
  }
  /* strict: abort the run */
  harness_set_strict(true);
  memset(&r, 0, sizeof r);
  {
    uint32_t fp = thunk_for("WIN87EM", 1, "__fpMath", HK_REGISTER, 0, HR_NONE);
    r.bx = 7;
    harness_call(FP_SEG(fp), FP_OFF(fp), HCONV_PASCAL, NULL, 0, &r);
    CHECK(harness_last_error() != 0);
  }
  harness_set_strict(false);
}

/* Direct API (no harness): the same results, and state lives in the arena. */
static void test_fpmath_direct(void) {
  W87Regs r;
  memset(&r, 0, sizeof r);
  r.ax = 9; r.bx = 0; r.fpcw = 0x037F; r.fpsw = 0x0001; r.cf = 1;
  CHECK_EQ(win87em_fpmath(&r), 0);
  CHECK_EQ(r.ax, 0);
  CHECK_EQ(r.fpcw, FPU_CW_DEFAULT);
  CHECK_EQ(r.fpcw, 0x1332 & ~0x00C3);
  CHECK_EQ(r.fpsw & 0x80FF, 0);
  CHECK_EQ(r.cf, 0);
  r.bx = 0x0B;
  CHECK_EQ(win87em_fpmath(&r), 0);
  CHECK_EQ(r.ax, 1);
  CHECK_EQ(r.dx, 0);
  r.bx = 99;
  CHECK_EQ(win87em_fpmath(&r), -1);
}

/* ---- start-up: no UNIMPLEMENTED WIN87EM / TOOLHELP ---- */

static void h_inittask(HCall *c) {
  c->regs->ax = 1; c->regs->cx = 0x2000; c->regs->di = 1; c->regs->si = 0;
  c->regs->bx = 0x80; c->regs->es = MEM_SEL_DGROUP; c->regs->dx = 1;
}
static void h_one(HCall *c) { c->ret = 1; }
static void h_version(HCall *c) { c->ret = 0x0A03; }
static uint16_t g_heap_next = 0x4000;
static void h_localalloc(HCall *c) { c->ret = g_heap_next; g_heap_next += 0x100; }

static uint32_t g_lin_winmain;
static unsigned g_winmain_hits;
static void on_insn(uint32_t lin, void *ud) {
  (void)ud;
  if (lin == g_lin_winmain) { g_winmain_hits++; harness_stop_emu(); }
}

static void test_startup_no_unimplemented(void) {
  uint32_t b3 = desc_base(MEM_SEL_SEG(3)), b34 = desc_base(MEM_SEL_SEG(34));
  uint32_t lo = b3 < b34 ? b3 : b34, hi = (b3 > b34 ? b3 : b34) + 0x10000;
  unsigned n_w87 = 0, n_bad = 0;
  g_lin_winmain = b3;
  g_winmain_hits = 0;
  harness_set_strict(false);
  harness_set_quiet(true);
  harness_register("KERNEL", 91, h_inittask);
  harness_register("KERNEL", 3, h_version);
  harness_register("KERNEL", 30, h_one);
  harness_register("USER", 5, h_one);
  harness_register("KERNEL", 5, h_localalloc);
  harness_register("KERNEL", 23, h_one);
  harness_register("KERNEL", 24, h_one);
  toolhelp_win87em_register(); /* after the stubs: its handlers must be the ones used */
  harness_log_clear();
  harness_trace(lo, hi, on_insn, NULL);
  harness_set_fpcw(0x037F);
  harness_call(MEM_SEL_SEG(34), 0x001A, HCONV_CDECL, NULL, 0, NULL);
  harness_trace(0, 0, NULL, NULL);
  harness_set_quiet(false);
  CHECK_EQ(g_winmain_hits, 1);
  for (size_t i = 0; i < harness_log_count(); i++) {
    const HLogEntry *e = harness_log_get(i);
    if (strcmp(e->module, "WIN87EM") == 0) {
      n_w87++;
      if (!e->implemented) n_bad++;
    }
    if (strcmp(e->module, "TOOLHELP") == 0 && !e->implemented) n_bad++;
  }
  CHECK(n_w87 >= 3); /* BX = 0, 3 and 0xB are reached before WinMain */
  CHECK_EQ(n_bad, 0);
}

int main(void) {
  FILE *fp = fopen(EXE, "rb");
  uint16_t h;
  if (!fp) { fprintf(stderr, "cannot open %s (run from the repo root)\n", EXE); return 1; }
  fclose(fp);
  harness_set_quiet(true);
  if (harness_load(EXE) != 0) return 1;
  toolhelp_win87em_register();

  h = GlobalAlloc(GMEM_MOVEABLE, 64);
  g_ti_fp = GlobalLock(h);
  CHECK(h != 0 && g_ti_fp != 0);
  g_ti = (TimerInfo *)LIN(g_ti_fp);

  test_timercount_direct();
  test_timercount_bad_size();
  test_timercount_thunk();
  test_fpmath_direct();
  test_fpmath_init();
  test_fpmath_handler();
  test_fpmath_installed();
  test_fpmath_unknown();
  test_startup_no_unimplemented();

  win87em_shutdown();
  harness_shutdown();
  return TEST_DONE();
}
