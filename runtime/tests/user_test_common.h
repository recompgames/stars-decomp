/* Shared support for test_user_win.c and test_user_msg.c (task stars-0z4.6): test window
 * procedures registered in the diff-test registry under a test-only selector, a message log
 * with window labels, and far-string helpers. Include after tests/test.h; each test file
 * includes it once (the functions are static). */
#ifndef STARS_RUNTIME_TESTS_USER_TEST_COMMON_H
#define STARS_RUNTIME_TESTS_USER_TEST_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/mem.h"
#include "runtime/user.h"

#define EXE "original/stars26i.exe"

/* Test-only selector: not in the GDT, never dereferenced; only the registry knows it. */
#define TSEL 0x0300
#define OFF_PROC_A 0x0000  /* class procedure: logs, then DefWindowProc */
#define OFF_PROC_B 0x0010  /* subclass: logs "B", forwards to g_old via CallWindowProc */
#define OFF_PROC_C 0x0020  /* logs and returns a fixed value for WM_USER, else DefWindowProc */
#define FP_PROC_A MK_FP(TSEL, OFF_PROC_A)
#define FP_PROC_B MK_FP(TSEL, OFF_PROC_B)
#define FP_PROC_C MK_FP(TSEL, OFF_PROC_C)

#define MAX_LOG 256
typedef struct LogEnt {
  char tag;       /* 'A', 'B' or 'C': the procedure that saw it */
  HWND16 hwnd;
  uint16_t msg, wp;
  uint32_t lp;    /* raw lParam */
  uint32_t aux;   /* WINDOWPOS flags (message specific) */
  CREATESTRUCT16 cs; /* copy taken when WM_NCCREATE / WM_CREATE is delivered (the block is freed after) */
  char csname[32], cscls[32];
} LogEnt;

static LogEnt g_log[MAX_LOG];
static int g_nlog;
static FARPTR g_old;           /* previous procedure for the subclass B */
static const char *g_next_label = "?";
static struct {
  HWND16 h;
  char label[8];
} g_labels[32];
static int g_nlabels;
static uint32_t g_nccreate_ret = 1;   /* mutation knobs for the creation failure tests */
static uint16_t g_create_ret = 0;
static int g_log_on = 1;
static uint32_t g_user_ret = 0x12340000u; /* WM_USER return value of procedure C */
/* Hook run inside procedure A for every message; returning nonzero means "handled" (the
 * procedure then returns 0 without calling DefWindowProc). */
static int (*g_hook)(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
/* Value the procedure returns when g_hook handled the message (default 0). */
static uint32_t g_hook_ret;

static const struct {
  uint16_t msg;
  const char *name;
} MSG_NAMES[] = {
    {WM_CREATE, "CREATE"}, {WM_DESTROY, "DESTROY"}, {WM_MOVE, "MOVE"}, {WM_SIZE, "SIZE"},
    {WM_ENABLE, "ENABLE"}, {WM_SETTEXT, "SETTEXT"}, {WM_GETTEXT, "GETTEXT"},
    {WM_GETTEXTLENGTH, "GETTEXTLENGTH"}, {WM_PAINT, "PAINT"}, {WM_CLOSE, "CLOSE"},
    {WM_ERASEBKGND, "ERASEBKGND"}, {WM_SHOWWINDOW, "SHOWWINDOW"}, {WM_CANCELMODE, "CANCELMODE"},
    {WM_GETMINMAXINFO, "GETMINMAXINFO"}, {WM_WINDOWPOSCHANGING, "WINDOWPOSCHANGING"},
    {WM_WINDOWPOSCHANGED, "WINDOWPOSCHANGED"}, {WM_NCCREATE, "NCCREATE"},
    {WM_NCDESTROY, "NCDESTROY"}, {WM_NCCALCSIZE, "NCCALCSIZE"}, {WM_NCPAINT, "NCPAINT"},
    {WM_PARENTNOTIFY, "PARENTNOTIFY"}, {WM_USER, "USER"}, {WM_USER + 1, "USER1"},
    {WM_USER + 2, "USER2"}, {WM_USER + 3, "USER3"}, {WM_QUIT, "QUIT"}, {WM_TIMER, "TIMER"},
    /* USER part 2 (task stars-0z4.7) */
    {WM_ACTIVATE, "ACTIVATE"}, {WM_SETFOCUS, "SETFOCUS"}, {WM_KILLFOCUS, "KILLFOCUS"},
    {WM_SETCURSOR, "SETCURSOR"}, {WM_NCHITTEST, "NCHITTEST"}, {WM_NCACTIVATE, "NCACTIVATE"},
    {WM_KEYDOWN, "KEYDOWN"}, {WM_KEYUP, "KEYUP"}, {WM_CHAR, "CHAR"},
};

static const char *msg_name(uint16_t m) {
  static char buf[8][16];
  static int k;
  for (size_t i = 0; i < sizeof MSG_NAMES / sizeof MSG_NAMES[0]; i++)
    if (MSG_NAMES[i].msg == m) return MSG_NAMES[i].name;
  k = (k + 1) & 7;
  snprintf(buf[k], sizeof buf[k], "0x%04X", m);
  return buf[k];
}

static const char *label_of(HWND16 h) {
  for (int i = 0; i < g_nlabels; i++)
    if (g_labels[i].h == h) return g_labels[i].label;
  if (g_nlabels < 32) {
    g_labels[g_nlabels].h = h;
    snprintf(g_labels[g_nlabels].label, sizeof g_labels[0].label, "%s", g_next_label);
    return g_labels[g_nlabels++].label;
  }
  return "?";
}
/* Window labels are bound to the first window handle that sends a message; call before the
 * CreateWindow that creates the window. */
static void next_label(const char *l) {
  int k = 0;
  /* Forget labels of dead windows: handles are recycled. */
  for (int i = 0; i < g_nlabels; i++)
    if (IsWindow(g_labels[i].h)) g_labels[k++] = g_labels[i];
  g_nlabels = k;
  g_next_label = l;
}
static void bind_label(HWND16 h, const char *l) {
  for (int i = 0; i < g_nlabels; i++)
    if (g_labels[i].h == h) {
      snprintf(g_labels[i].label, sizeof g_labels[i].label, "%s", l);
      return;
    }
  if (g_nlabels < 32) {
    g_labels[g_nlabels].h = h;
    snprintf(g_labels[g_nlabels].label, sizeof g_labels[0].label, "%s", l);
    g_nlabels++;
  }
}

static void log_clear(void) { g_nlog = 0; }

/* The log as "P:NCCREATE P:CREATE ..." (procedure tag omitted for A, "B/" prefix for B). */
static const char *dump(void) {
  static char out[4096];
  size_t n = 0;
  out[0] = 0;
  for (int i = 0; i < g_nlog; i++) {
    const LogEnt *e = &g_log[i];
    n += (size_t)snprintf(out + n, sizeof out - n, "%s%s%s%s:%s", i ? " " : "",
                          e->tag == 'A' ? "" : (e->tag == 'B' ? "B/" : "C/"), "", label_of(e->hwnd),
                          msg_name(e->msg));
    if (n >= sizeof out) break;
  }
  return out;
}

static LogEnt *log_find(const char *label, uint16_t msg, int nth) {
  for (int i = 0; i < g_nlog; i++)
    if (g_log[i].msg == msg && strcmp(label_of(g_log[i].hwnd), label) == 0 && nth-- == 0) return &g_log[i];
  return NULL;
}

static void log_add(char tag, HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  LogEnt *e;
  if (!g_log_on || g_nlog >= MAX_LOG) return;
  e = &g_log[g_nlog++];
  e->tag = tag;
  e->hwnd = h;
  e->msg = msg;
  e->wp = wp;
  e->lp = lp;
  e->aux = 0;
  (void)label_of(h);
  if (msg == WM_NCCREATE || msg == WM_CREATE) {
    const CREATESTRUCT16 *c = LIN(lp);
    e->cs = *c;
    snprintf(e->csname, sizeof e->csname, "%s", c->lpszName ? (const char *)LIN(c->lpszName) : "");
    snprintf(e->cscls, sizeof e->cscls, "%s", c->lpszClass ? (const char *)LIN(c->lpszClass) : "");
  }
  if (msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED) {
    const WINDOWPOS16 *w = LIN(lp);
    e->aux = w->flags;
  }
}

#define CHECK_LOG(expected)                                                         \
  do {                                                                              \
    const char *got_ = dump();                                                      \
    test_checks_run++;                                                              \
    if (strcmp(got_, (expected)) != 0) {                                            \
      test_checks_failed++;                                                         \
      fprintf(stderr, "%s:%d: message order differs\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, got_, (expected)); \
    }                                                                               \
  } while (0)

/* ---- procedures ---- */

static uint32_t unpack_ret(uint32_t v) { return v; }

static uint32_t common_proc(char tag, uint16_t ss, uint16_t sp, int forward) {
  uint32_t lp = DT_ARG32(ss, sp, 4);
  uint16_t wp = DT_ARG16(ss, sp, 8), msg = DT_ARG16(ss, sp, 10), h = DT_ARG16(ss, sp, 12);
  log_add(tag, h, msg, wp, lp);
  if (forward) return CallWindowProc(g_old, h, msg, wp, lp);
  if (tag == 'C' && msg == WM_USER) return g_user_ret;
  if (msg == WM_NCCREATE && !g_nccreate_ret) return 0;
  if (msg == WM_CREATE && g_create_ret) return g_create_ret;
  if (g_hook && g_hook(h, msg, wp, lp)) return g_hook_ret;
  return unpack_ret(DefWindowProc(h, msg, wp, lp));
}

static uint32_t ad_a(uint16_t ss, uint16_t sp, Regs16 *r) { (void)r; return common_proc('A', ss, sp, 0); }
static uint32_t ad_b(uint16_t ss, uint16_t sp, Regs16 *r) { (void)r; return common_proc('B', ss, sp, 1); }
static uint32_t ad_c(uint16_t ss, uint16_t sp, Regs16 *r) { (void)r; return common_proc('C', ss, sp, 0); }

static void reg_procs(void) {
  DtEntry a = {TSEL, OFF_PROC_A, "test_proc_a", FAR_PASCAL, ad_a, 2};
  DtEntry b = {TSEL, OFF_PROC_B, "test_proc_b", FAR_PASCAL, ad_b, 2};
  DtEntry c = {TSEL, OFF_PROC_C, "test_proc_c", FAR_PASCAL, ad_c, 2};
  difftest_unregister_all();
  difftest_register(&a);
  difftest_register(&b);
  difftest_register(&c);
  rt_set_mode(RT_MODE_DECOMP);
}

/* ---- far strings and structures in the arena ---- */

static uint16_t g_pool[64];
static int g_npool;

static FARPTR far_alloc(uint32_t bytes) {
  uint16_t h = GlobalAlloc(GHND, bytes);
  CHECK(h != 0);
  if (g_npool < 64) g_pool[g_npool++] = h;
  return GlobalLock(h);
}
static FARPTR fstr(const char *s) {
  FARPTR p = far_alloc((uint32_t)strlen(s) + 1);
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}
static void pool_free(void) {
  while (g_npool > 0) {
    uint16_t h = g_pool[--g_npool];
    GlobalUnlock(h);
    GlobalFree(h);
  }
}

static uint16_t register_class(const char *name, FARPTR proc, uint16_t style, uint16_t brush) {
  FARPTR p = far_alloc(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(p);
  w->style = style;
  w->lpfnWndProc = proc;
  w->hbrBackground = brush;
  w->lpszClassName = fstr(name);
  w->hInstance = 0x1234;
  return RegisterClass(p);
}

static HWND16 mk(const char *label, const char *cls, const char *title, uint32_t style, int x, int y, int cx,
                 int cy, HWND16 parent, uint16_t id) {
  HWND16 h;
  next_label(label);
  h = CreateWindow(fstr(cls), fstr(title), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent,
                   id, 0x1234, 0);
  if (h) bind_label(h, label);
  return h;
}

__attribute__((unused)) static RECT16 get_rect(HWND16 h, int client) {
  FARPTR p = far_alloc(sizeof(RECT16));
  RECT16 r;
  if (client) GetClientRect(h, p);
  else GetWindowRect(h, p);
  memcpy(&r, LIN(p), sizeof r);
  return r;
}
#define CHECK_RECT(r, l, t, rr, b)                                                   \
  do {                                                                               \
    RECT16 r_ = (r);                                                                 \
    CHECK_EQ(r_.left, (l));                                                          \
    CHECK_EQ(r_.top, (t));                                                           \
    CHECK_EQ(r_.right, (rr));                                                        \
    CHECK_EQ(r_.bottom, (b));                                                        \
  } while (0)

__attribute__((unused)) static POINT16 c2s(HWND16 h, int x, int y) {
  FARPTR p = far_alloc(sizeof(POINT16));
  POINT16 *q = LIN(p);
  POINT16 r;
  q->x = (int16_t)x;
  q->y = (int16_t)y;
  ClientToScreen(h, p);
  r = *q;
  return r;
}
__attribute__((unused)) static POINT16 s2c(HWND16 h, int x, int y) {
  FARPTR p = far_alloc(sizeof(POINT16));
  POINT16 *q = LIN(p);
  POINT16 r;
  q->x = (int16_t)x;
  q->y = (int16_t)y;
  ScreenToClient(h, p);
  r = *q;
  return r;
}
__attribute__((unused)) static uint32_t pt32(int x, int y) { return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16); }

/* ---- fixture ---- */

static int fixture_open(void) {
  harness_set_quiet(true);
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "cannot load %s (run from the repo root)\n", EXE);
    return -1;
  }
  CHECK_EQ(gdi_init(), 0);
  gdi_register();
  CHECK_EQ(user_init(), 0);
  user_register();
  reg_procs();
  g_nlabels = 0;
  g_nlog = 0;
  g_log_on = 1;
  g_nccreate_ret = 1;
  g_create_ret = 0;
  g_hook = NULL;
  g_hook_ret = 0;
  return 0;
}

static void fixture_close(void) {
  pool_free();
  user_shutdown();
  gdi_shutdown();
  harness_shutdown();
}

#endif /* STARS_RUNTIME_TESTS_USER_TEST_COMMON_H */
