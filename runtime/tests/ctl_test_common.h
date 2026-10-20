/* Shared support for the standard-control tests (task stars-0z4.10): test_ctl_button.c,
 * test_ctl_static.c, test_ctl_scroll.c. Message pump, simulated mouse and keyboard, a parent
 * window that logs WM_COMMAND / scroll messages, palette-index helpers and frame dumps.
 * Include after tests/test.h; each test file includes it once (the functions are static). */
#ifndef STARS_RUNTIME_TESTS_CTL_TEST_COMMON_H
#define STARS_RUNTIME_TESTS_CTL_TEST_COMMON_H

#include <sys/stat.h>

#include "user_test_common.h"
#include "runtime/ctl.h"
#include "runtime/user_internal.h"

#define CTL_U static __attribute__((unused))

#define PCLS "CTLPARENT"
#define TOP (WS_OVERLAPPED | WS_VISIBLE)
#define PX0 100 /* parent client origin on the screen */
#define PY0 80
#define VK_SPACE_ 0x20

CTL_U FARPTR g_msg;
CTL_U HWND16 g_top;
CTL_U uint32_t g_t = 100000;
CTL_U int g_pixel_tests;
CTL_U const char *g_ref;

#define PIXEL_TEST(png)                       \
  do {                                        \
    g_ref = "tests/ref/" png ".png (absent)"; \
    g_pixel_tests++;                          \
  } while (0)

/* ---- helpers ---- */

CTL_U void pump(void) {
  int guard = 0;
  while (guard++ < 400 && PeekMessage(g_msg, 0, 0, 0, PM_REMOVE)) {
    TranslateMessage(g_msg);
    DispatchMessage(g_msg);
  }
}
CTL_U void mouse(int x, int y, int b) {
  g_t += 1000;
  rt_input_mouse((int16_t)x, (int16_t)y, (uint16_t)b, g_t);
  pump();
}
/* a click at parent-client coordinates */
CTL_U void click_at(int cx, int cy) {
  mouse(PX0 + cx, PY0 + cy, 1);
  mouse(PX0 + cx, PY0 + cy, 0);
}
CTL_U void dblclick_at(int cx, int cy) {
  g_t += 1000;
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 1, g_t);
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 0, g_t + 20);
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 1, g_t + 40);
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 0, g_t + 60);
  pump();
}
CTL_U void key(unsigned vk, unsigned sc, int down) {
  g_t += 1000;
  rt_input_key((uint16_t)vk, (uint16_t)sc, down, g_t);
  pump();
}

CTL_U void begin(void) {
  user_paint_desktop();
  g_top = mk("P", PCLS, "", TOP, PX0, PY0, 400, 300, 0, 0);
  CHECK(g_top != 0);
  log_clear();
  mouse(790, 590, 0); /* park the pointer over nothing */
  pump();
  log_clear();
}
CTL_U void end(void) {
  pump();
  DestroyWindow(g_top);
  pump();
  log_clear();
  g_hook = NULL;
}

CTL_U HWND16 btn(const char *label, const char *cls, const char *text, uint32_t style, int x, int y, int w, int h,
                  uint16_t id) {
  HWND16 b = mk(label, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_top, id);
  CHECK(b != 0);
  return b;
}
CTL_U HWND16 B(const char *label, const char *text, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "BUTTON", text, style, x, y, w, h, id);
}

CTL_U int ncmd(void) {
  int n = 0;
  for (int i = 0; i < g_nlog; i++)
    if (g_log[i].msg == WM_COMMAND && strcmp(label_of(g_log[i].hwnd), "P") == 0) n++;
  return n;
}
CTL_U const LogEnt *cmd(int nth) { return log_find("P", WM_COMMAND, nth); }

/* ---- palette helpers (as in test_visual_chrome.c) ---- */

CTL_U uint8_t PX(int x, int y) { return fb_pixels()[y * GDI_SCREEN_W + x]; }
CTL_U uint8_t CI(unsigned color) {
  static const int idx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
  uint32_t want = GetSysColor((uint16_t)color);
  for (int k = 0; k < 20; k++)
    if (fb_palette_entry((unsigned)idx[k]) == want) return (uint8_t)idx[k];
  return 0;
}
#define BLACK CI(6)  /* COLOR_WINDOWFRAME */
#define FACE CI(15)  /* COLOR_BTNFACE */
#define HILITE CI(20)
#define SHADOW CI(16)
#define WHITE_ CI(5) /* COLOR_WINDOW */
#define BTNTXT CI(18)
#define GRAYTXT CI(17)
#define DESK CI(1)

/* pixel at parent-client coordinates */
CTL_U uint8_t pc(int x, int y) { return PX(PX0 + x, PY0 + y); }
#define PIXEL_EQ(x, y, want)                                                                         \
  do {                                                                                               \
    int got_ = pc((x), (y));                                                                         \
    test_checks_run++;                                                                               \
    if (got_ != (int)(want)) {                                                                       \
      test_checks_failed++;                                                                          \
      fprintf(stderr, "%s:%d: UNVERIFIED pixel c(%d,%d) = %d, want %d (%s)\n", __FILE__, __LINE__,   \
              (x), (y), got_, (int)(want), g_ref ? g_ref : "?");                                     \
    }                                                                                                \
  } while (0)
#define PIXEL_NE(x, y, bad)                                                                          \
  do {                                                                                               \
    int got_ = pc((x), (y));                                                                         \
    test_checks_run++;                                                                               \
    if (got_ == (int)(bad)) {                                                                        \
      test_checks_failed++;                                                                          \
      fprintf(stderr, "%s:%d: UNVERIFIED pixel c(%d,%d) = %d, must differ (%s)\n", __FILE__, __LINE__, \
              (x), (y), got_, g_ref ? g_ref : "?");                                                  \
    }                                                                                                \
  } while (0)
/* pixels of colour c in the parent-client rectangle [x0,x1) x [y0,y1) */
CTL_U int count_in(int x0, int y0, int x1, int y1, int c) {
  int n = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) n += pc(x, y) == c;
  return n;
}

/* Paints a COLOR_WINDOW (white) rectangle straight onto the parent's client area (parent-client
 * coordinates). Note that in this system colour scheme COLOR_BACKGROUND, COLOR_SCROLLBAR and
 * COLOR_BTNFACE are all (192,192,192), so a control that did not fill its background would be
 * indistinguishable from the desktop: tests put white underneath first. */
CTL_U void underlay(int x, int y, int w, int h) {
  HDC16 dc = GetDC(g_top);
  CHECK(dc != 0);
  user_fill(dc, r_make(x, y, x + w, y + h), GetSysColor(5));
  ReleaseDC(g_top, dc);
}

CTL_U void dump_frame(const char *name, int x, int y, int w, int h) {
  static uint32_t rgba[GDI_SCREEN_W * GDI_SCREEN_H];
  char path[160];
  FILE *f;
  mkdir("re", 0755);
  mkdir("re/frames", 0755);
  snprintf(path, sizeof path, "re/frames/%s.ppm", name);
  f = fopen(path, "wb");
  if (!f) return;
  fb_present(rgba);
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint32_t c = rgba[(y + j) * GDI_SCREEN_W + x + i];
      unsigned char px3[3] = {(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255)};
      fwrite(px3, 1, 3, f);
    }
  fclose(f);
}


#endif /* STARS_RUNTIME_TESTS_CTL_TEST_COMMON_H */
