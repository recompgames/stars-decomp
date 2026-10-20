/* Shared support for the standard-control tests, part 2 (task stars-0z4.11): test_ctl_edit.c,
 * test_ctl_listbox.c, test_ctl_combo.c. Builds on ctl_test_common.h: loads the original image
 * (the fixture needs it), helpers to talk to a control, to read the WM_COMMAND notifications the
 * parent logged, and keyboard helpers with modifier keys. Each test file includes it once. */
#ifndef STARS_RUNTIME_TESTS_CTL2_TEST_COMMON_H
#define STARS_RUNTIME_TESTS_CTL2_TEST_COMMON_H

#include "ctl_test_common.h"
#include "runtime/kernel.h"
#include "runtime/text.h"
#include "runtime/vfs.h"

CTL_U uint8_t *g_img;

CTL_U int load_fixture(void) {
  FILE *fp;
  long len;
  if (fixture_open() != 0) return 1;
  fp = fopen(EXE, "rb");
  if (!fp) return 1;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  g_img = malloc((size_t)len);
  if (!g_img || fread(g_img, 1, (size_t)len, fp) != (size_t)len) return 1;
  fclose(fp);
  CHECK_EQ(kernel_load(g_img, (size_t)len), 0);
  g_msg = far_alloc(sizeof(MSG16));
  CHECK(register_class(PCLS, FP_PROC_A, 0, 0) != 0);
  return 0;
}

#define ERR32 0xFFFFFFFFu /* LB_ERR / CB_ERR as a 32-bit message result */
#define MKL(lo, hi) ((uint32_t)(uint16_t)(lo) | ((uint32_t)(uint16_t)(hi) << 16))

CTL_U uint32_t SM(HWND16 h, uint16_t m, uint16_t w, uint32_t l) { return SendMessage(h, m, w, l); }
CTL_U uint32_t SMS(HWND16 h, uint16_t m, uint16_t w, const char *s) { return SendMessage(h, m, w, fstr(s)); }

/* WM_GETTEXT into a static buffer */
CTL_U const char *text_of(HWND16 h) {
  static char out[4][512];
  static int k;
  FARPTR p = far_alloc(512);
  k = (k + 1) & 3;
  GetWindowText(h, p, 511);
  snprintf(out[k], sizeof out[k], "%s", (const char *)LIN(p));
  return out[k];
}
/* text of item i through message m (LB_GETTEXT / CB_GETLBTEXT) */
CTL_U const char *item_text(HWND16 h, uint16_t m, int i) {
  static char out[4][512];
  static int k;
  FARPTR p = far_alloc(512);
  k = (k + 1) & 3;
  ((char *)LIN(p))[0] = 0;
  if (SendMessage(h, m, (uint16_t)i, p) == ERR32) return "<ERR>";
  snprintf(out[k], sizeof out[k], "%s", (const char *)LIN(p));
  return out[k];
}

/* WM_COMMAND notifications the parent "P" received: HIWORD(lParam) codes */
CTL_U int ncode(int nth) {
  const LogEnt *e = cmd(nth);
  return e ? (int)(e->lp >> 16) : -1;
}
CTL_U int count_code(int code) {
  int n = 0;
  for (int i = 0; i < g_nlog; i++)
    if (g_log[i].msg == WM_COMMAND && strcmp(label_of(g_log[i].hwnd), "P") == 0 && (int)(g_log[i].lp >> 16) == code) n++;
  return n;
}
/* the codes in order as "0300,0400" */
CTL_U const char *codes(void) {
  static char out[256];
  size_t n = 0;
  out[0] = 0;
  for (int i = 0, k = 0; i < ncmd(); i++, k++) {
    const LogEnt *e = cmd(i);
    n += (size_t)snprintf(out + n, sizeof out - n, "%s%04X", k ? "," : "", (unsigned)(e->lp >> 16));
  }
  return out;
}
#define CHECK_CODES(want)                                                                          \
  do {                                                                                             \
    const char *got_ = codes();                                                                    \
    test_checks_run++;                                                                             \
    if (strcmp(got_, (want)) != 0) {                                                               \
      test_checks_failed++;                                                                        \
      fprintf(stderr, "%s:%d: notifications differ\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, got_, (want)); \
    }                                                                                              \
  } while (0)

/* keyboard: a key press and release, optionally under Shift or Ctrl */
CTL_U void tap(unsigned vk) {
  key(vk, 0x10, 1);
  key(vk, 0x10, 0);
}
CTL_U void tap_with(unsigned mod, unsigned vk) {
  key(mod, 0x2A, 1);
  tap(vk);
  key(mod, 0x2A, 0);
}

/* width in pixels of the first n characters of s in the control's DC (system font unless set) */
CTL_U int text_w(HWND16 h, const char *s, int n) {
  HDC16 dc = GetDC(h);
  uint32_t ext;
  if (n <= 0) {
    ReleaseDC(h, dc);
    return 0;
  }
  ext = GetTextExtent(dc, fstr(s), (int16_t)n);
  ReleaseDC(h, dc);
  return (int)(ext & 0xFFFF);
}
CTL_U int text_h(HWND16 h) {
  HDC16 dc = GetDC(h);
  uint32_t ext = GetTextExtent(dc, fstr("Ag"), 2);
  ReleaseDC(h, dc);
  return (int)(ext >> 16);
}

/* bounding box of pixels with colour c in a parent-client rectangle */
typedef struct CBox {
  int l, t, r, b, n;
} CBox;
CTL_U CBox cbox(int x0, int y0, int x1, int y1, int c) {
  CBox bb = {1 << 20, 1 << 20, -1, -1, 0};
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++)
      if (pc(x, y) == c) {
        if (x < bb.l) bb.l = x;
        if (x > bb.r) bb.r = x;
        if (y < bb.t) bb.t = y;
        if (y > bb.b) bb.b = y;
        bb.n++;
      }
  return bb;
}
#define HIGHLIGHT_ CI(13)     /* COLOR_HIGHLIGHT */
#define HIGHLIGHTTXT_ CI(14)  /* COLOR_HIGHLIGHTTEXT */
#define WINTEXT_ CI(8)        /* COLOR_WINDOWTEXT */

/* the pixels of a parent-client rectangle, to compare two renderings */
CTL_U int same_pixels(int ax, int ay, int bx, int by, int w, int h) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (pc(ax + x, ay + y) != pc(bx + x, by + y)) return 0;
  return 1;
}

#endif
