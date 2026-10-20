/* Diff tests for decompiled segment 13 (src/seg13.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 13 is the toolbar of the main window and its tooltip:
 *   13:0x0000 TBWNDPROC     the toolbar window procedure (create, paint, mouse tracking, clicks)
 *   13:0x053e               paint every button of the bar
 *   13:0x05a4               paint one button (bevel, bitmap, pressed state)
 *   13:0x0844               hit test: which button is under a point (rewrites the point)
 *   13:0x08b0               width of a toolbar item
 *   13:0x0902               is toolbar item n switched on (reads the view flags DS:0x055e)
 *   13:0x09ae               toggle toolbar item n (flags, or a pop-up menu of choices)
 *   13:0x0fb8               zoom box: read or set the zoom percentage
 *   13:0x109e               show or hide the tooltip window
 *   13:0x1224 TOOLTIPWNDPROC the tooltip window procedure
 *   13:0x14bc FAKECOMBOPROC, 13:0x1508 FAKECEPROC  subclass procedures of the zoom combo box and its edit
 * Every scenario builds its state by writing DGROUP directly or by running ORIGINAL code; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory, the ordered
 * Win16 API calls). The runtime state the scenarios touch (windows, GDI objects, capture, the message
 * queue) lives in the arena, so DIFF restores it between the two runs.
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, listed with the scenarios that use
 * them: 8:0xa2c6 (the message pump of the click loop, scripted), 24:0x0e24 (the pop-up menu, scripted
 * choice, the item arrays hashed at call time), 31:0x61b0. 4:0x1f40 is decompiled and runs for real. 8:0x8dc0 (string lookup) and
 * 8:0xaa4e (bitmap blit) run as ORIGINAL code in both runs. The function under test is never doubled. */
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
#include "runtime/vfs.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL13 MEM_SEL_SEG(13)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* ---- test support: far memory, windows, callee test doubles ---- */

static uint32_t gblock(uint16_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  return GlobalLock(h);
}

/* A scratch area in DGROUP that no function of segment 13 touches (the bottom of the stack area,
 * far below the stack top 0xa688). */
#define SCR 0x6700

/* Windows. The test class uses the runtime's default window procedure; g_proc_sel is a 3-instruction
 * far procedure, MOV AX,1; XOR DX,DX; RETF 10 (a PASCAL window procedure that answers every message
 * with 1), the "previous procedure" of the subclassed combo box (g_proc_sel2: AX = 7, DX = 9, of its edit control). */
static uint16_t g_proc_sel, g_proc_sel2;
static void register_class(const char *name, uint32_t proc) {
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = proc;
  uint32_t nm = gblock((uint16_t)(strlen(name) + 1));
  memcpy(LIN(nm), name, strlen(name) + 1);
  w->lpszClassName = nm;
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static void register_classes(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  g_proc_sel = harness_alloc_code(code, sizeof code);
  CHECK(g_proc_sel != 0);
  /* a second one, MOV AX,7; MOV DX,9; RETF 10: the previous procedure of the edit control */
  static const uint8_t code2[] = {0xB8, 0x07, 0x00, 0xBA, 0x09, 0x00, 0xCA, 0x0A, 0x00};
  g_proc_sel2 = harness_alloc_code(code2, sizeof code2);
  CHECK(g_proc_sel2 != 0);
  register_class("SEG13TEST", USER_CB_DEFWNDPROC);
  /* the tooltip class named at DS:0x20a ("starstt") runs the ORIGINAL TOOLTIPWNDPROC */
  register_class("starstt", MK_FP(SEL13, 0x1224));
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent) {
  uint32_t cls = gblock(16), nm = gblock(4);
  memcpy(LIN(cls), "SEG13TEST", 10);
  memcpy(LIN(nm), "w", 2);
  uint16_t h = CreateWindow(cls, nm, style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, 0, 0x1234, 0);
  CHECK(h != 0);
  return h;
}

/* Test doubles. The first 5 bytes of the callee in the code segment image become JMP FAR to an import
 * thunk of the module "STUBS" (ordinal = stub id, CDECL: the caller pops the arguments), registered
 * with harness_add_import. The same image serves the ORIGINAL run and the C version's CALL_ORIG, so
 * each stubbed call is an entry in the harness API log (module, ordinal, argument words as pushed,
 * return value), compared in order with the real API calls. 24:0x0e24 also stores a hash of the memory
 * its arguments point to at every call (h_e24), in the stub block, which is compared as arena memory. */
#define STUB_LOG0 0x100
#define STUB_BLKSZ 0x4000
static uint32_t g_stub_blk;
static void stub_hash_to_block(uint32_t h) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
}
static void stub_handler(HCall *c) {
  uint8_t *blk = LIN(g_stub_blk);
  c->ret = (uint32_t)blk[0x12 + 4 * c->ordinal] | ((uint32_t)blk[0x13 + 4 * c->ordinal] << 8);
  c->ret = (c->ret << 16) | (uint32_t)blk[0x10 + 4 * c->ordinal] | ((uint32_t)blk[0x11 + 4 * c->ordinal] << 8);
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  *(uint16_t *)LIN(g_stub_blk) = STUB_LOG0;
}
static void stub_ret(unsigned id, uint16_t ax, uint16_t dx) {
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x10 + 4 * id))) = ax;
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x12 + 4 * id))) = dx;
}
static void stub_install_h(uint16_t seg, uint16_t off, unsigned id, unsigned nargs, HApiFn fn) {
  char name[32];
  snprintf(name, sizeof name, "STUB_%u_%04x", seg, off);
  uint32_t th = harness_add_import("STUBS", (uint16_t)id, name, HK_VARARGS, (uint16_t)(2 * nargs), HR_RET32, fn);
  CHECK(th != 0);
  uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
  memcpy(LIN(MK_FP(SEG_SEL(seg), off)), c, sizeof c);
}
static void stub_install(uint16_t seg, uint16_t off, unsigned id, unsigned nargs) {
  stub_install_h(seg, off, id, nargs, stub_handler);
}
/* Number of "STUBS" entries with this ordinal in the log before the one being handled (the
 * dispatcher appends the entry before it calls the handler): the index of the current call. */
static unsigned stub_call_index(unsigned id) {
  unsigned n = 0, cnt = (unsigned)harness_log_count();
  for (unsigned i = 0; i + 1 < cnt; i++) {
    const HLogEntry *e = harness_log_get(i);
    if (strcmp(e->module, "STUBS") == 0 && e->ordinal == id) n++;
  }
  return n;
}

/* ---- 13:0x08b0: width of toolbar item n ---- */

/* Runs ORIGINAL 13:0x08b0 (setup, not under test). */
static uint16_t orig_08b0(int16_t n) {
  const uint16_t a[] = {(uint16_t)n};
  return (uint16_t)harness_call(SEL13, 0x08b0, HCONV_CDECL, a, 1, NULL);
}

static void t_08b0(void) {
  static const uint16_t v[] = {0, 1, 5, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x1c, 0x7fff, 0xffff, 0xfffe, 0xfffd, 0xfffc,
                               0xfffb, 0x8000, 0x8001};
  for (unsigned s = 0; s < 3; s++) {
    DS_W(0x267a) = s == 0 ? 0x000f : s == 1 ? 0x0010 : 0x8000; /* < 0x10 signed, = 0x10, negative */
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) DIFF(13, 0x08b0, FAR_CDECL, v[i]);
    DIFF_FUZZ(13, 0x08b0, FAR_CDECL, 1, 300, 0x13080000u + s);
  }
  DS_W(0x267a) = 0x0012;
  DIFF(13, 0x08b0, FAR_CDECL, 0xfffd);
}

/* ---- 13:0x0902: is item n switched on ---- */

static void t_0902(void) {
  static const uint16_t flags[] = {0x0000, 0xffff, 0x0003, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
                                   0x0400, 0x0800, 0x1000, 0x2000, 0x4003, 0x40ff, 0x7f00, 0xefef, 0x1234};
  static const uint16_t sel[] = {0x000f, 0x0000, 0x0005, 0x0001};
  for (unsigned f = 0; f < sizeof flags / sizeof flags[0]; f++) {
    DS_W(0x055e) = flags[f];
    for (unsigned s = 0; s < sizeof sel / sizeof sel[0]; s++) {
      DS_W(0x4c76) = sel[s];
      for (uint16_t n = 0; n < 0x14; n++) DIFF(13, 0x0902, FAR_CDECL, n);
      DIFF(13, 0x0902, FAR_CDECL, 0xffff);
      DIFF(13, 0x0902, FAR_CDECL, 0x8000);
      DIFF(13, 0x0902, FAR_CDECL, 0x0012);
    }
  }
  DS_W(0x055e) = 0x0043;
  DS_W(0x4c76) = 0x000f;
  DIFF_FUZZ(13, 0x0902, FAR_CDECL, 1, 300, 0x13090200u);
  DS_W(0x055e) = 0xa5a5;
  DS_W(0x4c76) = 0x0002;
  DIFF_FUZZ(13, 0x0902, FAR_CDECL, 1, 300, 0x13090201u);
}

/* ---- 13:0x0844: which toolbar item is under the point (and rewrite the point) ---- */

/* The byte table of the bar in the code segment (13:0x0521, 29 signed bytes). */
static int8_t tb_item(unsigned i) { return *(int8_t *)LIN(MK_FP(SEL13, (uint16_t)(0x0521 + i))); }
static void setpt(uint16_t at, int16_t x, int16_t y) {
  DS_W(at) = (uint16_t)x;
  DS_W(at + 2) = (uint16_t)y;
}
static uint32_t g_rng = 0x1308a4c3u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static void t_0844(void) {
  static const int16_t ys[] = {4, 5, 0x10, 0x1f};
  for (unsigned hs = 0; hs < 2; hs++) {
    DS_W(0x267a) = hs ? 0x0010 : 0x000f; /* the zoom box is 0x3c or 0x46 wide */
    int16_t start = 4;
    for (unsigned i = 0; i < 0x1d; i++) {
      int16_t w = (int16_t)orig_08b0(tb_item(i));
      /* first, middle, last column of the item, and the first column of the next one */
      const int16_t xs[] = {start, (int16_t)(start + w / 2), (int16_t)(start + w - 1), (int16_t)(start + w)};
      for (unsigned k = 0; k < 4; k++) {
        for (unsigned j = 0; j < sizeof ys / sizeof ys[0]; j++) {
          setpt(SCR, xs[k], ys[j]);
          DIFF(13, 0x0844, FAR_CDECL, SCR);
        }
      }
      start = (int16_t)(start + w);
    }
    /* just before, at and beyond the end of the bar */
    for (int16_t dx = -2; dx <= 2; dx++) {
      setpt(SCR, (int16_t)(start + dx), 8);
      DIFF(13, 0x0844, FAR_CDECL, SCR);
    }
  }
  /* outside the bar: x < 4, y < 4, y >= 0x20 (signed compares), extreme values */
  static const int16_t bad[][2] = {{3, 8},      {0, 8},      {-1, 8},       {-32768, 8}, {10, 3},   {10, 0},
                                   {10, -1},    {10, -32768}, {10, 0x20},    {10, 0x21},  {10, 0x7fff}, {32767, 8},
                                   {4, 4},      {4, 0x1f},   {4, 0x20},     {3, 3},      {-5, -5}};
  for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    setpt(SCR, bad[i][0], bad[i][1]);
    DIFF(13, 0x0844, FAR_CDECL, SCR);
  }
  /* random points over the bar */
  DS_W(0x267a) = 0x0010;
  for (unsigned i = 0; i < 400; i++) {
    setpt(SCR, (int16_t)(rnd() % 700), (int16_t)(rnd() % 40));
    DIFF(13, 0x0844, FAR_CDECL, SCR);
  }
  /* the pointer is a DGROUP offset anywhere: the point at the very top of the scratch area */
  setpt(SCR + 0x40, 30, 9);
  DIFF(13, 0x0844, FAR_CDECL, SCR + 0x40);
}

/* ---- 13:0x05a4 paint one button, 13:0x053e paint the bar ---- */

/* The brushes the bar draws with (DS:0x10 face, 0x14 light, 0x16 dark) and no DIB for the glyphs
 * (DS:0x5a4e = 0: 8:0xaa4e then ends in one PatBlt). 8:0xaa4e runs as ORIGINAL code in both runs. */
static uint16_t bar_setup(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  DS_W(0x0010) = GetStockObject(0); /* WHITE_BRUSH */
  DS_W(0x0014) = GetStockObject(4); /* BLACK_BRUSH */
  DS_W(0x0016) = GetStockObject(2); /* GRAY_BRUSH */
  DS_W(0x5a4e) = 0;
  DS_W(0x267a) = 0x0010;
  return dc;
}

static void t_05a4(void) {
  uint16_t dc = bar_setup();
  static const uint16_t items[] = {0, 1, 5, 0xc, 0xd, 0xf, 0x10, 0xffff, 0xfffe, 0xfffd, 0xfffc, 0x7fff};
  static const uint16_t states[] = {0, 1, 2, 3, 0xffff, 0x7fff, 0x8000};
  for (unsigned i = 0; i < sizeof items / sizeof items[0]; i++)
    for (unsigned j = 0; j < sizeof states / sizeof states[0]; j++)
      DIFF(13, 0x05a4, FAR_CDECL, dc, 4, 4, items[i], states[j]);
  DS_W(0x267a) = 0x000f; /* the zoom box is narrower */
  DIFF(13, 0x05a4, FAR_CDECL, dc, 4, 4, 0xfffd, 0);
  DIFF(13, 0x05a4, FAR_CDECL, dc, 4, 4, 0xfffd, 2);
  static const uint16_t pos[][2] = {{0, 0}, {0xfffe, 0xfffe}, {100, 50}, {0x7fff, 0x7fff}, {0x8000, 0x8000}, {300, 2}};
  for (unsigned p = 0; p < sizeof pos / sizeof pos[0]; p++)
    for (uint16_t st = 0; st < 4; st++) DIFF(13, 0x05a4, FAR_CDECL, dc, pos[p][0], pos[p][1], 3, st);
  DIFF(13, 0x05a4, FAR_CDECL, 0, 4, 4, 1, 1); /* hdc 0: every call is made with the invalid handle */
}

static void t_053e(void) {
  uint16_t dc = bar_setup();
  static const uint16_t flags[] = {0x0000, 0xffff, 0x0003, 0x1c70, 0x0040, 0x0f80};
  for (unsigned f = 0; f < sizeof flags / sizeof flags[0]; f++) {
    DS_W(0x055e) = flags[f];
    DS_W(0x4c76) = (f & 1) ? 0x000f : 0x0001;
    DIFF(13, 0x053e, FAR_CDECL, dc, SCR);
  }
  DS_W(0x055e) = 0x0043;
  DS_W(0x54f8) = GetStockObject(15); /* DEFAULT_PALETTE */
  DIFF(13, 0x053e, FAR_CDECL, dc, 0);
  DS_W(0x267a) = 0x000f;
  DIFF(13, 0x053e, FAR_CDECL, dc, 0);
  DS_W(0x54f8) = 0;
  DIFF(13, 0x053e, FAR_CDECL, 0, 0);
}

/* ---- 13:0x0fb8: zoom box, read or set the percentage ---- */

/* A NUL-terminated string in far memory. */
static uint32_t fstr(const char *t) {
  uint32_t p = gblock((uint16_t)(strlen(t) + 1));
  memcpy(LIN(p), t, strlen(t) + 1);
  return p;
}
/* The windows the bar talks to: DS:0x18be the zoom combo box, 0x15a the main window, 0x150 the map
 * window, 0x2806 the frame window that gets the focus. */
static uint16_t g_w1, g_w2, g_w3, g_w4;
static void bar_windows(void) {
  g_w1 = mkwin(0x90000000, 10, 10, 100, 20, 0);
  g_w2 = mkwin(0x90000000, 20, 20, 100, 20, 0);
  g_w3 = mkwin(0x90000000, 30, 30, 100, 20, 0);
  g_w4 = mkwin(0x90000000, 40, 40, 100, 20, 0);
  DS_W(0x18be) = g_w1;
  DS_W(0x015a) = g_w2;
  DS_W(0x0150) = g_w3;
  DS_W(0x2806) = g_w4;
}

static void t_0fb8(void) {
  bar_windows();
  /* mode 0 parses the box text */
  static const char *const texts[] = {"50%", "50", "100%", "7%", "abc", "", "12x", "150", "0", "2", "99%", "3%", "100",
                                      "101", "1000", "65535", "%", "5 %", "0%", "99", "12345678901234567890", "70%x",
                                      "1%", "10%%", "-5", "9", "09%", "40%"};
  static const uint16_t cur[] = {100, 50, 3, 12};
  static const uint16_t fl[] = {0x0000, 0x0020, 0x0040};
  for (unsigned t = 0; t < sizeof texts / sizeof texts[0]; t++) {
    SetWindowText(g_w1, fstr(texts[t]));
    for (unsigned c = 0; c < sizeof cur / sizeof cur[0]; c++) {
      DS_W(0x0560) = cur[c];
      DS_W(0x055e) = fl[c % 3];
      DS_B(0x0794) = (uint8_t)(c & 1 ? 0x01 : 0x00);
      DIFF(13, 0x0fb8, FAR_CDECL, 0);
    }
  }
  /* any other mode reads DS:0x0560, clamped */
  static const uint16_t vals[] = {0, 1, 2, 3, 4, 50, 99, 100, 101, 0x7fff, 0x8000, 0xffff, 0xfffd, 12};
  for (unsigned v = 0; v < sizeof vals / sizeof vals[0]; v++) {
    for (unsigned f = 0; f < 2; f++) {
      DS_W(0x0560) = vals[v];
      DS_W(0x055e) = f ? 0x0020 : 0x0000;
      DIFF(13, 0x0fb8, FAR_CDECL, 1);
      DIFF(13, 0x0fb8, FAR_CDECL, 0x8000);
    }
  }
  DS_W(0x0560) = 64;
  DIFF(13, 0x0fb8, FAR_CDECL, 0xffff);
  DS_W(0x055e) = 0;
}

/* ---- doubles of callees outside segment 13 that cannot run in isolation ---- */

/* 24:0x0e24, the pop-up menu: 8 argument words (hwnd, x, y, count, flags pointer, string-pointer array,
 * 0xfffe, 0), the pointers relative to the stack segment. The double returns the choice stored with
 * stub_ret(STUB_E24, ...) and, at every call, stores in the stub block a hash of the dword flags and the
 * pointer array (count entries) and of the 0x20 bytes each pointer names in DGROUP, i.e. everything the
 * real menu would read. */
#define STUB_E24 20
#define STUB_6_1B0 21
#define STUB_PUMP 23
static void h_e24(HCall *c) {
  uint16_t cnt = hcall_word(c, 3), fl = hcall_word(c, 4), pp = hcall_word(c, 5);
  uint16_t seg = FP_SEG(c->args);
  uint32_t h = 2166136261u;
  if (cnt > 64) cnt = 64;
  const uint8_t *f = LIN(MK_FP(seg, fl)), *p = LIN(MK_FP(seg, pp));
  for (unsigned k = 0; k < 4u * cnt; k++) h = (h ^ f[k]) * 16777619u;
  for (unsigned k = 0; k < 2u * cnt; k++) h = (h ^ p[k]) * 16777619u;
  for (unsigned i = 0; i < cnt; i++) {
    uint16_t sp;
    memcpy(&sp, p + 2 * i, 2);
    const uint8_t *s = LINN(sp);
    for (unsigned k = 0; k < 0x20; k++) h = (h ^ s[k]) * 16777619u;
  }
  stub_hash_to_block(h);
  stub_handler(c);
}

/* 8:0xa2c6, the message pump of the click loop: one argument, a near pointer to a POINT it fills. The
 * double follows a script (g_pump) indexed by the number of earlier calls: it writes the point and
 * returns 1 (button still down) or 0 (released); beyond the script it returns 0. */
typedef struct PumpStep {
  uint16_t ret;
  int16_t x, y;
} PumpStep;
static PumpStep g_pump[16];
static unsigned g_npump;
static void pump_script(unsigned n, const PumpStep *s) {
  CHECK(n <= 16);
  memcpy(g_pump, s, n * sizeof *s);
  g_npump = n;
}
static void h_pump(HCall *c) {
  unsigned i = stub_call_index(STUB_PUMP);
  uint16_t pt = hcall_word(c, 0);
  if (i < g_npump) {
    int16_t xy[2] = {g_pump[i].x, g_pump[i].y};
    memcpy(LIN(MK_FP(FP_SEG(c->args), pt)), xy, 4);
    c->ret = g_pump[i].ret;
  } else {
    c->ret = 0;
  }
}

static void doubles_init(void) {
  stub_install_h(24, 0x0e24, STUB_E24, 8, h_e24);
  stub_install(31, 0x61b0, STUB_6_1B0, 0);
  stub_install_h(8, 0xa2c6, STUB_PUMP, 1, h_pump);
}

/* ---- 13:0x09ae: toggle a toolbar item or choose from its pop-up menu ---- */

/* Fills 16 rows of the layer table at DS:0x41f5 (stride 0x8d): row k is hidden (bit 1 of the byte at +1)
 * when bit k of `hidden` is set, and gets a distinct name in the 0x20 bytes before the pointer it yields. */
static void layer_table(uint16_t hidden) {
  for (unsigned k = 0; k < 16; k++) {
    uint16_t e = (uint16_t)(0x41f5 + 0x8d * k);
    DS_B(e + 1) = (hidden >> k) & 1 ? 2 : 0;
    for (unsigned j = 0; j < 8; j++) DS_B(e - 0x73 + j) = (uint8_t)('A' + k + j);
  }
}

static void t_09ae(void) {
  bar_setup();
  bar_windows();
  static const uint16_t flagsets[] = {0x0000, 0xffff, 0x1234, 0x0040, 0x2a5a};
  /* items 0 to 5: the view mode (low nibble) is set only when the flag is not 0 */
  for (unsigned f = 0; f < sizeof flagsets / sizeof flagsets[0]; f++) {
    DS_W(0x055e) = flagsets[f];
    for (uint16_t it = 0; it <= 5; it++) {
      DIFF(13, 0x09ae, FAR_CDECL, it, 0);
      DIFF(13, 0x09ae, FAR_CDECL, it, 1);
      DIFF(13, 0x09ae, FAR_CDECL, it, 0x100);
    }
  }
  /* the single-bit items; 31:0x61b0 follows when bit 3 of DS:0x0791 is set */
  static const uint16_t bit_items[] = {6, 7, 9, 10, 11, 12, 14, 17};
  for (unsigned i = 0; i < sizeof bit_items / sizeof bit_items[0]; i++) {
    for (unsigned f = 0; f < sizeof flagsets / sizeof flagsets[0]; f++) {
      DS_W(0x055e) = flagsets[f];
      DS_B(0x0791) = (uint8_t)((f & 1) ? 0x08 : 0x00);
      DS_B(0x0794) = (uint8_t)(f & 2);
      DIFF(13, 0x09ae, FAR_CDECL, bit_items[i], 0);
      DIFF(13, 0x09ae, FAR_CDECL, bit_items[i], 1);
      DIFF(13, 0x09ae, FAR_CDECL, bit_items[i], 0xffff);
    }
  }
  DS_B(0x0791) = 0x07; /* bit 3 clear, the others set */
  DIFF(13, 0x09ae, FAR_CDECL, 6, 1);
  DIFF(13, 0x09ae, FAR_CDECL, 7, 1);
  DS_B(0x0791) = 0;
  /* beyond the table: the dirty bit is still set, nothing else happens */
  DS_W(0x055e) = 0x1234;
  DS_B(0x0794) = 0;
  static const uint16_t beyond[] = {0x12, 0x13, 0x100, 0x7fff, 0x8000, 0xffff};
  for (unsigned i = 0; i < sizeof beyond / sizeof beyond[0]; i++) DIFF(13, 0x09ae, FAR_CDECL, beyond[i], 1);

  /* pop-up menus: the choice comes from the double of 24:0x0e24 (-1 = dismissed) */
  static const uint16_t sels[] = {0xffff, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 19, 20, 0x8000, 0x7fff, 0xfffe};
  /* 8: the sets (DS:0x4c76) */
  static const uint16_t sets[] = {0x0000, 0x000f, 0x0003, 0x0001, 0x000c};
  for (unsigned f = 0; f < 2; f++) {
    for (unsigned s = 0; s < sizeof sets / sizeof sets[0]; s++) {
      for (unsigned c = 0; c < sizeof sels / sizeof sels[0]; c++) {
        DS_W(0x055e) = f ? 0x0040 : 0x0000;
        DS_W(0x4c76) = sets[s];
        DS_B(0x0791) = (uint8_t)(c & 1 ? 8 : 0);
        stub_ret(STUB_E24, sels[c], 0);
        DIFF(13, 0x09ae, FAR_CDECL, 8, c & 1);
      }
    }
  }
  /* 13: the layers (DS:0x54ec, table rows with their hidden flags) */
  static const uint16_t hid[] = {0x0000, 0xffff, 0x0005, 0x8001};
  static const uint16_t lay[] = {0x0000, 0xffff, 0x0001, 0x8000, 0x5555};
  for (unsigned h = 0; h < sizeof hid / sizeof hid[0]; h++) {
    layer_table(hid[h]);
    for (unsigned l = 0; l < sizeof lay / sizeof lay[0]; l++) {
      for (unsigned c = 0; c < sizeof sels / sizeof sels[0]; c++) {
        for (unsigned f = 0; f < 2; f++) {
          DS_W(0x54ec) = lay[l];
          DS_W(0x055e) = f ? 0x0200 : 0x0000;
          stub_ret(STUB_E24, sels[c], 0);
          DIFF(13, 0x09ae, FAR_CDECL, 13, 1);
        }
      }
    }
  }
  /* sel 4 + n picks the n-th visible row; walk every position with an all-visible table too */
  layer_table(0);
  for (uint16_t sel = 4; sel < 24; sel++) {
    DS_W(0x54ec) = 0x00f0;
    DS_W(0x055e) = 0;
    stub_ret(STUB_E24, sel, 0);
    DIFF(13, 0x09ae, FAR_CDECL, 13, 1);
    DS_W(0x055e) = 0x0200;
    DIFF(13, 0x09ae, FAR_CDECL, 13, 1);
  }
  /* 15: the 8 bits of DS:0x538e */
  static const uint16_t bits[] = {0x0000, 0x00ff, 0x0001, 0xff00, 0x0a5a};
  for (unsigned b = 0; b < sizeof bits / sizeof bits[0]; b++) {
    for (unsigned c = 0; c < sizeof sels / sizeof sels[0]; c++) {
      for (unsigned f = 0; f < 2; f++) {
        DS_W(0x538e) = bits[b];
        DS_W(0x055e) = f ? 0x0800 : 0x0000;
        stub_ret(STUB_E24, sels[c], 0);
        DIFF(13, 0x09ae, FAR_CDECL, 15, 0);
      }
    }
  }
  for (uint16_t sel = 4; sel < 14; sel++) {
    DS_W(0x538e) = 0x0030;
    DS_W(0x055e) = 0;
    stub_ret(STUB_E24, sel, 0);
    DIFF(13, 0x09ae, FAR_CDECL, 15, 1);
  }
  /* 16: the zoom presets (DS:0x0576 marks one), the choice goes to 4:0x1f40 */
  static const uint16_t cur[] = {0, 4, 0xfffc, 0xfff8, 5, 0xffff};
  for (unsigned z = 0; z < sizeof cur / sizeof cur[0]; z++) {
    for (unsigned c = 0; c < sizeof sels / sizeof sels[0]; c++) {
      DS_W(0x0576) = cur[z];
      stub_ret(STUB_E24, sels[c], 0);
      DIFF(13, 0x09ae, FAR_CDECL, 16, 0);
    }
  }
  /* the mouse position handed to the menu */
  rt_input_mouse(123, 77, 0, 0);
  stub_ret(STUB_E24, 3, 0);
  DIFF(13, 0x09ae, FAR_CDECL, 8, 1);
  DIFF(13, 0x09ae, FAR_CDECL, 13, 1);
  DIFF(13, 0x09ae, FAR_CDECL, 15, 1);
  DIFF(13, 0x09ae, FAR_CDECL, 16, 1);
  DS_W(0x055e) = 0;
}

/* ---- the tooltip: 13:0x109e (show or hide) and 13:0x1224 TOOLTIPWNDPROC ---- */

/* The runtime clock has one-second resolution and is part of the arena (restored between the two
 * runs): g_t0 is GetTickCount() at the 2000-01-01 00:00:00 reference, tick_at(n) the value n seconds later. */
static uint32_t g_t0;
static void clock_init(void) {
  rt_clock_set(2000, 1, 1, 0, 0, 0);
  g_t0 = GetTickCount();
}
static void set_clock(unsigned sec) { rt_clock_set(2000, 1, 1, (int)((sec / 3600) % 24), (int)((sec / 60) % 60), (int)(sec % 60)); }
static uint32_t tick_at(unsigned sec) { return g_t0 + sec * 1000u; }
static void ds_rect(uint16_t at, int16_t l, int16_t t, int16_t r, int16_t b) {
  DS_W(at) = (uint16_t)l;
  DS_W(at + 2) = (uint16_t)t;
  DS_W(at + 4) = (uint16_t)r;
  DS_W(at + 6) = (uint16_t)b;
}

/* The tooltip look: font DS:0x292e, frame brush DS:0x1c, background DS:0x29d4, text colour DS:0x4ac0. */
static void tip_setup(void) {
  bar_setup();
  bar_windows();
  clock_init();
  DS_W(0x292e) = GetStockObject(13); /* SYSTEM_FONT */
  DS_W(0x001c) = GetStockObject(4);
  DS_W(0x29d4) = GetStockObject(0);
  ds_w32(0x4ac0, 0x00112233);
  DS_W(0x54f4) = 0x1234;
  DS_W(0x267a) = 0x0010;
  DS_W(0x264a) = 640;
  DS_W(0x18ce) = 0;
  DS_W(0x18d0) = 0xffff;
  DS_W(0x4da6) = 0xffff;
  ds_w32(0x18d2, 0);
  ds_w32(0x5a44, 0);
  ds_rect(0x29c8, 0, 0, 0, 0);
}

static void t_109e(void) {
  tip_setup();
  const uint16_t rect = SCR + 0x20;
  ds_rect(rect, 10, 20, 50, 60);
  uint16_t hid = mkwin(0x80000000, 5, 5, 60, 20, 0);  /* a tooltip window that is not visible */
  uint16_t shown = mkwin(0x90000000, 6, 6, 60, 20, 0); /* ... and one that is visible */
  static const struct { uint16_t id, rect; } hide[] = {{0xffff, 0}, {0xffff, rect}, {5, 0}, {0x8000, rect}, {0x8001, 0}};
  for (unsigned s = 0; s < 3; s++) {
    for (unsigned h = 0; h < sizeof hide / sizeof hide[0]; h++) {
      DS_W(0x18ce) = s == 0 ? 0 : s == 1 ? hid : shown;
      ds_w32(0x18d2, 7);
      set_clock(10 + h);
      DIFF(13, 0x109e, FAR_CDECL, hide[h].id, hide[h].rect ? hide[h].rect : 0);
    }
  }
  DS_W(0x18ce) = 0;
  /* the same id and rectangle: nothing happens; a rectangle that differs in one word: it does */
  DS_W(0x4da6) = 5;
  ds_rect(0x29c8, 10, 20, 50, 60);
  DS_W(0x18ce) = shown;
  DIFF(13, 0x109e, FAR_CDECL, 5, rect);
  DIFF(13, 0x109e, FAR_CDECL, 6, rect); /* another id, same rectangle */
  for (unsigned w = 0; w < 4; w++) {
    DS_W(0x4da6) = 5;
    ds_rect(0x29c8, 10, 20, 50, 60);
    DS_W(0x29c8 + 2 * w) = (uint16_t)(DS_W(0x29c8 + 2 * w) + 1);
    DIFF(13, 0x109e, FAR_CDECL, 5, rect);
  }
  /* a new id and no tooltip window yet: it is created (class "starstt", ORIGINAL TOOLTIPWNDPROC), then the
   * 0x5f3 message tells it to show at once (1) or after the delay (0): time since the last one, and
   * whether a tooltip was visible */
  static const struct { unsigned now; uint32_t last_delta; } times[] = {
      {100, 1000}, {100, 100}, {100, 400}, {100, 401}, {100, 0}, {100, 0xffffff00u}, {100, 0}};
  for (unsigned vis = 0; vis < 2; vis++) {
    for (unsigned t = 0; t < sizeof times / sizeof times[0]; t++) {
      DS_W(0x18ce) = vis ? shown : 0;
      DS_W(0x4da6) = 0xffff;
      ds_rect(0x29c8, 0, 0, 0, 0);
      set_clock(times[t].now);
      ds_w32(0x18d2, tick_at(times[t].now) - times[t].last_delta);
      DIFF(13, 0x109e, FAR_CDECL, 0x166 + t, rect);
    }
  }
  /* the tooltip window exists and is hidden: invalidated, then shown */
  DS_W(0x18ce) = hid;
  DS_W(0x4da6) = 0xffff;
  set_clock(50);
  ds_w32(0x18d2, tick_at(50) - 5000);
  DIFF(13, 0x109e, FAR_CDECL, 0x170, rect);
  DS_W(0x18ce) = 0;
}

static void t_1224(void) {
  tip_setup();
  uint16_t tw = mkwin(0x90000000, 100, 100, 80, 24, 0); /* stands for the tooltip window */
  uint16_t tw2 = mkwin(0x80000000, 100, 100, 80, 24, 0); /* a hidden one */
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  /* WM_CREATE keeps the window; WM_DESTROY clears it and the timer */
  DIFF(13, 0x1224, FAR_PASCAL, tw, 1, 0, 0, 0);
  DS_W(0x18d0) = 0xffff;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 2, 0, 0, 0);
  DS_W(0x18d0) = 0x039e;
  DS_W(0x18ce) = tw;
  DS_W(0x4da6) = 7;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 2, 0, 0, 0);
  /* WM_ERASEBKGND and WM_PAINT */
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x14, dc, 0, 0);
  DS_W(0x4da6) = 5;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x0f, 0, 0, 0);
  DS_W(0x4da6) = 0x166;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x0f, 0, 0, 0);
  /* the three mouse messages destroy the window; 0x202 and the neighbours go to DefWindowProc */
  static const uint16_t msgs[] = {0x200, 0x201, 0x203, 0x202, 0x204, 0x1ff, 0x0, 0x3, 0x10, 0x13, 0x15, 0x30, 0x111,
                                  0x112, 0x114, 0x5f2, 0x5f4, 0xffff, 0x8000};
  for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
    uint16_t w = mkwin(0x90000000, 100, 100, 80, 24, 0);
    DIFF(13, 0x1224, FAR_PASCAL, w, msgs[i], 0x039e, 0x1234, 0x5678);
  }
  /* 0x5f3 with wParam 0: restart the 700 ms timer */
  DS_W(0x18d0) = 0xffff;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 0, 0, 0);
  DS_W(0x18d0) = 0x039e;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 0, 0, 0);
  /* 0x5f3 with another wParam and WM_TIMER 0x39e: cursor against the item rectangle DS:0x29c8 (screen) */
  ds_rect(0x29c8, 200, 200, 260, 230);
  DS_W(0x2806) = g_w4;
  static const int16_t cur[][2] = {{210, 210}, {199, 210}, {260, 210}, {259, 229}, {230, 230}, {0, 0}, {600, 100}};
  for (unsigned c = 0; c < sizeof cur / sizeof cur[0]; c++) {
    rt_input_mouse(cur[c][0], cur[c][1], 0, 0);
    for (unsigned t = 0; t < 2; t++) {
      DS_W(0x18d0) = t ? 0xffff : 0x039e;
      DS_W(0x4d86) = 40;
      DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 1, 0, 0);
      DIFF(13, 0x1224, FAR_PASCAL, tw2, 0x5f3, 0x039e, 0, 0);
    }
  }
  /* keep the tooltip on the screen: the left edge plus its width over DS:0x264a moves it left */
  rt_input_mouse(210, 210, 0, 0);
  DS_W(0x4d86) = 700;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 1, 0, 0);
  DS_W(0x4d86) = 40;
  DS_W(0x264a) = 100;
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 1, 0, 0);
  DS_W(0x264a) = 640;
  DS_W(0x267a) = 0x0011; /* odd height: 3 * h / 2 rounds toward zero */
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 1, 0, 0);
  DS_W(0x267a) = 0xfff1; /* negative height (the divide rounds toward zero too) */
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x5f3, 1, 0, 0);
  DS_W(0x267a) = 0x0010;
  /* WM_TIMER: only wParam 0x39e counts; a visible tooltip checks the cursor and the 10 s limit */
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x113, 0x0001, 0, 0);
  for (unsigned c = 0; c < sizeof cur / sizeof cur[0]; c++) {
    rt_input_mouse(cur[c][0], cur[c][1], 0, 0);
    /* DS:0x5a44 (when the tooltip was last placed) + 10000 against the tick: equal and later keep it */
    static const int32_t back[] = {0, -1, -10000, -10001, -20000};
    for (unsigned l = 0; l < sizeof back / sizeof back[0]; l++) {
      set_clock(60);
      ds_w32(0x5a44, tick_at(60) + (uint32_t)back[l]);
      DS_W(0x18d0) = 0x039e;
      DIFF(13, 0x1224, FAR_PASCAL, tw, 0x113, 0x039e, 0, 0);   /* visible */
      DIFF(13, 0x1224, FAR_PASCAL, tw2, 0x113, 0x039e, 0, 0);  /* hidden: takes the show path */
    }
  }
  ds_w32(0x5a44, 0xffffd8f0u); /* + 10000 wraps to 0 */
  set_clock(60);
  DIFF(13, 0x1224, FAR_PASCAL, tw, 0x113, 0x039e, 0, 0);
}

/* ---- 13:0x14bc FAKECOMBOPROC and 13:0x1508 FAKECEPROC: subclass procedures of the zoom combo box ---- */

static void t_fake(void) {
  tip_setup();
  ds_w32(0x2668, MK_FP(g_proc_sel, 0)); /* the previous procedures: the combo box's, then the edit control's */
  ds_w32(0x31b6, MK_FP(g_proc_sel2, 0));
  DS_W(0x15a) = g_w2;
  static const uint16_t msgs[] = {0x200, 0x201, 0x202, 0x203, 0x204, 0x100, 0, 0xffff, 0x1ff, 0x5f4};
  static const uint32_t pts[] = {0x00080010, 0x00200010, 0x00080001, 0xffff0000, 0x0008000a};
  for (unsigned off = 0; off < 2; off++) {
    for (unsigned m = 0; m < sizeof msgs / sizeof msgs[0]; m++) {
      for (unsigned p = 0; p < sizeof pts / sizeof pts[0]; p++) {
        DS_W(0x18ce) = (p & 1) ? g_w3 : 0;
        DS_W(0x18c0) = 0; /* the last mouse position: forget it so the move is processed */
        DS_W(0x18c2) = 0;
        if (off) DIFF(13, 0x1508, FAR_PASCAL, g_w1, msgs[m], 0x0001, HI(pts[p]), LO(pts[p]));
        else DIFF(13, 0x14bc, FAR_PASCAL, g_w1, msgs[m], 0x0001, HI(pts[p]), LO(pts[p]));
      }
    }
  }
}

/* ---- 13:0x0000 TBWNDPROC: the toolbar window procedure ---- */

/* Left edge (client x) of table entry idx of the bar, and its width, from ORIGINAL 13:0x08b0. */
static int16_t bar_left(unsigned idx) {
  int16_t x = 4;
  for (unsigned i = 0; i < idx; i++) x = (int16_t)(x + (int16_t)orig_08b0(tb_item(i)));
  return x;
}
static int16_t bar_width(unsigned idx) { return (int16_t)orig_08b0(tb_item(idx)); }
static unsigned bar_index(int8_t item) {
  for (unsigned i = 0; i < 0x1d; i++)
    if (tb_item(i) == item) return i;
  CHECK(0);
  return 0;
}
/* lParam (dword) of a mouse message: x low word, y high word. */
#define MPT(x, y) (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
/* A mouse message to TBWNDPROC: hwnd, msg, wParam, lParam high, lParam low. */
#define TB(h, m, wp, lp) DIFF(13, 0x0000, FAR_PASCAL, (h), (m), (wp), HI(lp), LO(lp))

/* Writes `w` over the stack area below the entry frame: the click loop reads one local ([BP-2]) that
 * the original leaves uninitialised when the message pump ends at once. */
static void fill_stack(uint16_t w) {
  uint16_t top = harness_stack_top();
  for (uint16_t a = (uint16_t)(top - 0x300); a < (uint16_t)(top - 0x10); a = (uint16_t)(a + 2)) DS_W(a) = w;
}

static void t_0000(void) {
  tip_setup();
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t tb = mkwin(0x90000000, 10, 10, 400, 40, 0); /* the toolbar window */
  DS_W(0x15a) = tb;                                   /* the main window: no mapping of the mouse position */
  DS_W(0x18be) = g_w1;
  ds_w32(0x27be, MK_FP(SEL13, 0x14bc));
  ds_w32(0x29c2, MK_FP(SEL13, 0x1508));
  DS_W(0x2930) = GetStockObject(13);

  /* WM_SETCURSOR, WM_CTLCOLOR, WM_ERASEBKGND */
  TB(tb, 0x20, 0, 0);
  TB(tb, 0x20, 0x1234, 0x56789abc);
  ds_w32(0x253a, 0x00334455);
  TB(tb, 0x19, dc, MPT(1, 2));
  TB(tb, 0x19, 0, 0);
  TB(tb, 0x14, dc, 0);
  TB(tb, 0x14, 0, 0x00ff00ff);
  /* WM_PAINT: the frame (the left edge only when DS:0x255e is 0) and the bar */
  for (unsigned f = 0; f < 2; f++) {
    DS_W(0x255e) = f;
    TB(tb, 0x0f, 0, 0);
  }
  /* every message the procedure leaves to DefWindowProc, and the neighbours of those it handles */
  static const uint16_t defs[] = {0x0, 0x2, 0x3, 0x10, 0x13, 0x15, 0x18, 0x1a, 0x1f, 0x21, 0x30, 0x110, 0x112, 0x202,
                                  0x204, 0x1ff, 0x5f3, 0x5f5, 0x7fff, 0x8000, 0xffff, 0x0e, 0x11};
  for (unsigned i = 0; i < sizeof defs / sizeof defs[0]; i++) TB(tb, defs[i], 0x0001, MPT(5, 6));
  /* WM_COMMAND: the combo box notification 8 posts the private message 0x5f4 */
  TB(tb, 0x111, 0, MPT(g_w1, 8));
  TB(tb, 0x111, 0, MPT(g_w1, 7));
  TB(tb, 0x111, 0, MPT(g_w2, 8));
  TB(tb, 0x111, 0x5f4, MPT(g_w1, 0));
  /* the private message 0x5f4 reads the zoom box */
  SetWindowText(g_w1, fstr("40%"));
  TB(tb, 0x5f4, 0, 0);

  /* WM_CREATE builds the zoom combo box and subclasses it and its edit control */
  static const uint16_t heights[] = {0x10, 0x0f, 0x11, 0x14, 0x0, 0xffff, 0x15, 0x7fff};
  static const uint16_t pcts[] = {100, 50, 33, 10, 3, 5, 0x8000};
  for (unsigned h = 0; h < sizeof heights / sizeof heights[0]; h++) {
    DS_W(0x267a) = heights[h];
    DS_W(0x0560) = pcts[h % (sizeof pcts / sizeof pcts[0])];
    uint16_t host = mkwin(0x90000000, 20, 20, 400, 40, 0);
    TB(host, 1, 0, 0);
  }
  for (unsigned p = 0; p < sizeof pcts / sizeof pcts[0]; p++) {
    DS_W(0x267a) = 0x10;
    DS_W(0x0560) = pcts[p];
    uint16_t host = mkwin(0x90000000, 20, 20, 400, 40, 0);
    TB(host, 1, 0, 0);
  }
  DS_W(0x267a) = 0x10;
  DS_W(0x0560) = 100;

  /* WM_MOUSEMOVE: the item under the mouse selects the tooltip; the same position again does nothing */
  DS_W(0x18ce) = 0;
  for (unsigned mapped = 0; mapped < 2; mapped++) {
    DS_W(0x15a) = mapped ? g_w2 : tb;
    for (unsigned i = 0; i < 0x1d; i++) {
      int16_t x = (int16_t)(bar_left(i) + bar_width(i) / 2);
      ds_w32(0x18c0, 0);
      TB(tb, 0x200, 0, MPT(x, 8));
    }
  }
  DS_W(0x15a) = tb;
  ds_w32(0x18c0, MPT(40, 8));
  TB(tb, 0x200, 0, MPT(40, 8));        /* same position as the last one */
  TB(tb, 0x200, 0, MPT(40, 9));        /* y changed only */
  TB(tb, 0x200, 0, MPT(41, 9));
  static const int16_t outside[][2] = {{3, 8}, {40, 3}, {40, 0x20}, {1000, 8}, {-5, -5}, {40, -1}};
  for (unsigned o = 0; o < sizeof outside / sizeof outside[0]; o++) {
    ds_w32(0x18c0, 0);
    TB(tb, 0x200, 0, MPT(outside[o][0], outside[o][1]));
  }
  DS_W(0x18ce) = 0;

  /* WM_LBUTTONDOWN and WM_LBUTTONDBLCLK: hide the tooltip, find the item, and track the click */
  static const int8_t clicks[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 0xa, 0xb, 0xc, 0xe, 0x11, 8, 0xd, 0xf, 0x10, -1, -2, -3};
  /* pump scripts: (ret, x, y); the rectangle of the item is built from the item's left edge */
  for (unsigned c = 0; c < sizeof clicks / sizeof clicks[0]; c++) {
    unsigned idx = bar_index(clicks[c]);
    int16_t lx = bar_left(idx), w = bar_width(idx);
    int16_t in_x = (int16_t)(lx + w / 2), in_y = 10;
    DS_W(0x055e) = (c & 1) ? 0x1c70 : 0x0001;   /* some items on, some off */
    DS_W(0x4c76) = (c & 2) ? 0x000f : 0x0000;
    DS_B(0x0791) = (uint8_t)(c & 4 ? 8 : 0);
    stub_ret(STUB_E24, (uint16_t)(c % 5), 0);
    for (unsigned msg = 0; msg < 2; msg++) {
      uint16_t m = msg ? 0x203 : 0x201;
      /* the pump ends at once (the local [BP-2] is stack garbage: zero, then non-zero) */
      pump_script(0, NULL);
      fill_stack(0);
      TB(tb, m, 1, MPT(in_x, in_y));
      fill_stack(0x5a5a);
      TB(tb, m, 1, MPT(in_x, in_y));
      fill_stack(0);
      /* inside, then released inside: the item toggles */
      PumpStep s1[] = {{1, in_x, in_y}, {0, in_x, in_y}};
      pump_script(2, s1);
      TB(tb, m, 1, MPT(in_x, in_y));
      /* inside, outside, inside again, released outside */
      PumpStep s2[] = {{1, in_x, in_y}, {1, 1000, 1000}, {1, in_x, in_y}, {1, in_x, in_y}, {0, 2, 2}};
      pump_script(5, s2);
      TB(tb, m, 1, MPT(in_x, in_y));
      /* outside only */
      PumpStep s3[] = {{1, 700, 5}, {1, 701, 5}, {0, 702, 5}};
      pump_script(3, s3);
      TB(tb, m, 1, MPT(in_x, in_y));
      /* the click itself outside the item row */
      pump_script(2, s1);
      TB(tb, m, 1, MPT(in_x, 0x40));
    }
  }
  pump_script(0, NULL);
  /* view modes that are already on only beep */
  for (uint16_t mode = 0; mode <= 5; mode++) {
    DS_W(0x055e) = mode;
    for (int8_t it = 0; it <= 5; it++) {
      unsigned idx = bar_index(it);
      TB(tb, 0x201, 0, MPT(bar_left(idx) + 3, 8));
    }
  }
  /* clicks before the bar, between items, past the end */
  static const int16_t xs[] = {0, 3, 4, 5, 200, 500, 700, 0x7fff, -1};
  for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
    TB(tb, 0x201, 0, MPT(xs[i], 8));
    TB(tb, 0x203, 0, MPT(xs[i], 8));
  }
  DS_W(0x055e) = 0;
}

int main(void) {
  g_only = getenv("SEG13_ONLY");
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
  register_classes();
  stubs_init();
  doubles_init();
  if (want("08b0")) t_08b0();
  if (want("0902")) t_0902();
  if (want("0844")) t_0844();
  if (want("05a4")) t_05a4();
  if (want("053e")) t_053e();
  if (want("0fb8")) t_0fb8();
  if (want("09ae")) t_09ae();
  if (want("109e")) t_109e();
  if (want("1224")) t_1224();
  if (want("fake")) t_fake();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
