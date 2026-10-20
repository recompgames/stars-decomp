/* Expectations for the SCROLLBAR control class (runtime/ctl_scroll.c). Task stars-0z4.10, written
 * BEFORE the implementation. Do not edit an expectation to match the code: a disagreement is
 * reported with both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here and no reference capture exists):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h 399-403: SBM_SETPOS16 WM_USER+0, SBM_GETPOS16 +1,
 *       SBM_SETRANGE16 +2, SBM_GETRANGE16 +3, SBM_ENABLE_ARROWS16 +4.
 *   Win16 packing          dlls/user.exe16/message.c scrollbar_proc16 2500-2527: SBM_SETRANGE16 takes
 *       wParam = redraw flag and lParam = MAKELONG(min, max); SBM_GETRANGE16 returns
 *       MAKELRESULT(min, max); SBM_SETPOS16 takes wParam = position and lParam = redraw (returns
 *       the previous position); SBM_ENABLE_ARROWS16 takes the ESB_* flags in wParam.
 *   SBS_HORZ 0, SBS_VERT 1: include/winuser.rh 1360-1361. ESB_* flags: include/winuser.h 1810-1822
 *       (ESB_ENABLE_BOTH 0, ESB_DISABLE_LTUP 1, ESB_DISABLE_RTDN 2, ESB_DISABLE_BOTH 3). The
 *       winuser16.h of the pinned commit has no ESB_* block: the Win16 values are UNVERIFIED.
 *   Control behaviour      dlls/win32u/scroll.c: WM_GETDLGCODE 1352-1353 (DLGC_WANTARROWS), WM_KEYDOWN
 *       1251-1252 with handle_kbd_event 1179-1203 (Prior/Next/Home/End/Up/Down/Left/Right ->
 *       SB_PAGEUP/PAGEDOWN/TOP/BOTTOM/LINEUP/LINEDOWN sent to the parent as WM_VSCROLL or
 *       WM_HSCROLL), WM_LBUTTONDOWN -> track_scroll_bar 1260-1275, SBM_SETRANGE 1364-1372,
 *       SBM_SETPOS 1393-1400.
 *   WM_HSCROLL / WM_VSCROLL (Win16): wParam = SB_* code, LOWORD(lParam) = position (thumb codes
 *       only in Windows; this runtime always fills the current position), HIWORD(lParam) = scroll
 *       bar control handle. Same packing as the window scroll bars of stars-0z4.9.
 *
 * UNVERIFIED:
 *   - Every pixel expectation (arrow/thumb geometry comes from user_scroll.c of stars-0z4.9, which
 *     is itself UNVERIFIED). The checks are structural: where the thumb is for a given position,
 *     that a disabled bar shows no thumb, that the arrows have an outline.
 *   - A keyboard scroll sends the current position in the low word of lParam (Wine's 32-bit control
 *     sends only the handle); no SB_ENDSCROLL follows a key (as in Wine).
 *   - Not implemented (documented omissions): SBS_SIZEBOX, SBS_TOPALIGN/LEFTALIGN/BOTTOMALIGN/
 *     RIGHTALIGN (positioning on creation), WM_CTLCOLOR for the trough brush (the trough is the
 *     dither of user_sb_draw).
 */
#include "test.h"

#include "ctl_test_common.h"

#define SM_ARROW_V GetSystemMetrics(20) /* arrow button height of a vertical bar */
#define SM_ARROW_H GetSystemMetrics(21) /* arrow button width of a horizontal bar */
#define SM_THUMB_V GetSystemMetrics(9)
#define SM_THUMB_H GetSystemMetrics(10)

static HWND16 SB(const char *label, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "SCROLLBAR", "", style, x, y, w, h, id);
}

/* codes of the scroll messages the parent got, in order; also returns the lParam of the first */
static int scrolls(uint16_t msg, int out[16], uint32_t *lp0) {
  int n = 0;
  for (int i = 0; i < g_nlog; i++)
    if (g_log[i].msg == msg && strcmp(label_of(g_log[i].hwnd), "P") == 0 && n < 16) {
      if (n == 0 && lp0) *lp0 = g_log[i].lp;
      out[n++] = g_log[i].wp;
    }
  return n;
}

/* queue a click (down, optional moves, up) and run it through the message loop in one go: the
 * tracking loop of the scroll bar runs until the queued input is exhausted */
static void clickq(int cx, int cy) {
  g_t += 1000;
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 1, g_t);
  rt_input_mouse((int16_t)(PX0 + cx), (int16_t)(PY0 + cy), 0, g_t + 10);
  pump();
}

static void test_constants(void) {
  CHECK_EQ(SBS_HORZ, 0);
  CHECK_EQ(SBS_VERT, 1);
  CHECK_EQ(SBM_SETPOS, 0x0400);
  CHECK_EQ(SBM_GETPOS, 0x0401);
  CHECK_EQ(SBM_SETRANGE, 0x0402);
  CHECK_EQ(SBM_GETRANGE, 0x0403);
  CHECK_EQ(SBM_ENABLE_ARROWS, 0x0404);
  CHECK_EQ(ESB_ENABLE_BOTH, 0);
  CHECK_EQ(ESB_DISABLE_LTUP, 1);
  CHECK_EQ(ESB_DISABLE_RTDN, 2);
  CHECK_EQ(ESB_DISABLE_BOTH, 3);
  CHECK_EQ(WM_HSCROLL, 0x0114);
  CHECK_EQ(WM_VSCROLL, 0x0115);
}

static void test_class(void) {
  HWND16 v;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  CHECK(v != 0);
  CHECK_EQ(FP_SEG(GetWindowLong(v, GWL_WNDPROC)), MEM_SEL_CALLBACK);
  CHECK_EQ(GetWindowLong(v, GWL_WNDPROC), USER_CB_SCROLLBAR);
  CHECK_EQ(SendMessage(v, WM_GETDLGCODE, 0, 0), DLGC_WANTARROWS);
  end();
}

static void test_position_api(void) {
  HWND16 v;
  FARPTR pmin, pmax;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  pmin = far_alloc(2);
  pmax = far_alloc(2);
  /* defaults: 0..100, position 0 (user_scroll.c, UNVERIFIED for Windows 3.1) */
  GetScrollRange(v, SB_CTL, pmin, pmax);
  CHECK_EQ(*(int16_t *)LIN(pmin), 0);
  CHECK_EQ(*(int16_t *)LIN(pmax), 100);
  SetScrollRange(v, SB_CTL, 0, 50, 1);
  GetScrollRange(v, SB_CTL, pmin, pmax);
  CHECK_EQ(*(int16_t *)LIN(pmax), 50);
  CHECK_EQ(SetScrollPos(v, SB_CTL, 20, 1), 0);   /* returns the previous position */
  CHECK_EQ(GetScrollPos(v, SB_CTL), 20);
  CHECK_EQ(SetScrollPos(v, SB_CTL, 70, 1), 20);  /* clamped to the range */
  CHECK_EQ(GetScrollPos(v, SB_CTL), 50);
  /* the window scroll bars of the same window are separate */
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 0);
  /* SBM_* messages (Win16 packing, message.c 2506-2525) */
  CHECK_EQ(SendMessage(v, SBM_SETPOS, 10, 1), 50);                      /* previous position */
  CHECK_EQ(SendMessage(v, SBM_GETPOS, 0, 0), 10);
  SendMessage(v, SBM_SETRANGE, 1, make_long(5, 200));                    /* redraw, MAKELONG(min,max) */
  CHECK_EQ(SendMessage(v, SBM_GETRANGE, 0, 0), make_long(5, 200));
  CHECK_EQ(SendMessage(v, SBM_GETPOS, 0, 0), 10);
  /* a range that no longer holds the position moves it, and SBM_SETRANGE returns the old one */
  CHECK_EQ(SendMessage(v, SBM_SETRANGE, 0, make_long(20, 30)), 10);
  CHECK_EQ(SendMessage(v, SBM_GETPOS, 0, 0), 20);
  end();
}

/* ---- mouse: messages to the parent ---- */

static void test_vertical_clicks(void) {
  HWND16 v;
  int codes[16], n, arrow;
  uint32_t lp = 0;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  pump();
  arrow = SM_ARROW_V;
  log_clear();
  clickq(10 + 8, 10 + 100 - arrow / 2);   /* second arrow: down */
  n = scrolls(WM_VSCROLL, codes, &lp);
  CHECK_EQ(n, 2);
  if (n == 2) {
    CHECK_EQ(codes[0], SB_LINEDOWN);
    CHECK_EQ(codes[1], SB_ENDSCROLL);
  }
  CHECK_EQ(lp >> 16, v);                  /* HIWORD(lParam) = the scroll bar control (Win16) */
  CHECK_EQ(lp & 0xFFFF, 0);               /* LOWORD(lParam) = position */
  CHECK_EQ(scrolls(WM_HSCROLL, codes, NULL), 0);
  log_clear();
  clickq(10 + 8, 10 + arrow / 2);         /* first arrow: up */
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK_EQ(n, 2);
  if (n == 2) CHECK_EQ(codes[0], SB_LINEUP);
  /* trough below the thumb (position 0: thumb at the top): page down */
  log_clear();
  clickq(10 + 8, 10 + 100 - arrow - 4);
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK_EQ(n, 2);
  if (n == 2) CHECK_EQ(codes[0], SB_PAGEDOWN);
  /* thumb at the bottom: a click in the trough above it pages up */
  SetScrollPos(v, SB_CTL, 100, 1);
  log_clear();
  clickq(10 + 8, 10 + arrow + 4);
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK_EQ(n, 2);
  if (n == 2) CHECK_EQ(codes[0], SB_PAGEUP);
  end();
}

static void test_horizontal_clicks(void) {
  HWND16 h;
  int codes[16], n, arrow;
  uint32_t lp = 0;
  begin();
  h = SB("H", SBS_HORZ, 10, 10, 100, 16, 2);
  pump();
  arrow = SM_ARROW_H;
  log_clear();
  clickq(10 + 100 - arrow / 2, 10 + 8);
  n = scrolls(WM_HSCROLL, codes, &lp);
  CHECK_EQ(n, 2);
  if (n == 2) CHECK_EQ(codes[0], SB_LINEDOWN); /* the right arrow is SB_LINEDOWN (= SB_LINERIGHT) */
  CHECK_EQ(lp >> 16, h);
  CHECK_EQ(scrolls(WM_VSCROLL, codes, NULL), 0);
  log_clear();
  clickq(10 + arrow / 2, 10 + 8);
  n = scrolls(WM_HSCROLL, codes, NULL);
  CHECK_EQ(n, 2);
  if (n == 2) CHECK_EQ(codes[0], SB_LINEUP);
  end();
}

static void test_thumb_drag(void) {
  HWND16 v;
  int codes[16], n, arrow, thumb, y0;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  pump();
  arrow = SM_ARROW_V;
  thumb = SM_THUMB_V;
  y0 = 10 + arrow + thumb / 2;    /* centre of the thumb at position 0 */
  log_clear();
  g_t += 1000;
  rt_input_mouse((int16_t)(PX0 + 10 + 8), (int16_t)(PY0 + y0), 1, g_t);
  rt_input_mouse((int16_t)(PX0 + 10 + 8), (int16_t)(PY0 + y0 + 12), 1, g_t + 10);
  rt_input_mouse((int16_t)(PX0 + 10 + 8), (int16_t)(PY0 + y0 + 24), 1, g_t + 20);
  rt_input_mouse((int16_t)(PX0 + 10 + 8), (int16_t)(PY0 + y0 + 24), 0, g_t + 30);
  pump();
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK(n >= 3);
  {
    int track = 0, pos = 0, end_ = 0;
    uint32_t lastpos = 0;
    for (int i = 0; i < g_nlog; i++)
      if (g_log[i].msg == WM_VSCROLL) {
        if (g_log[i].wp == SB_THUMBTRACK) track++;
        if (g_log[i].wp == SB_THUMBPOSITION) {
          pos++;
          lastpos = g_log[i].lp & 0xFFFF;
        }
        if (g_log[i].wp == SB_ENDSCROLL) end_++;
      }
    CHECK(track >= 1);
    CHECK_EQ(pos, 1);
    CHECK_EQ(end_, 1);
    CHECK(lastpos > 0 && lastpos <= 100);          /* dragged downwards: a larger position */
    CHECK_EQ(codes[n - 1], SB_ENDSCROLL);          /* the sequence ends with SB_ENDSCROLL */
  }
  (void)v;
  end();
}

/* ---- keyboard ---- */

static void test_keyboard(void) {
  HWND16 v, h;
  int codes[16], n;
  uint32_t lp = 0;
  begin();
  v = SB("V", SBS_VERT | WS_TABSTOP, 10, 10, 16, 100, 1);
  h = SB("H", SBS_HORZ | WS_TABSTOP, 100, 10, 100, 16, 2);
  SetFocus(v);
  CHECK_EQ(GetFocus(), v);
  log_clear();
  key(0x28 /* VK_DOWN */, 0x50, 1);
  key(0x28, 0x50, 0);
  n = scrolls(WM_VSCROLL, codes, &lp);
  CHECK_EQ(n, 1);
  if (n == 1) CHECK_EQ(codes[0], SB_LINEDOWN);
  CHECK_EQ(lp >> 16, v);
  static const struct {
    unsigned vk;
    int code;
  } K[] = {{0x26, SB_LINEUP}, {0x21, SB_PAGEUP}, {0x22, SB_PAGEDOWN}, {0x24, SB_TOP}, {0x23, SB_BOTTOM}};
  for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
    log_clear();
    key(K[i].vk, 0x50, 1);
    key(K[i].vk, 0x50, 0);
    n = scrolls(WM_VSCROLL, codes, NULL);
    CHECK_EQ(n, 1);
    if (n == 1) CHECK_EQ(codes[0], K[i].code);
  }
  /* the horizontal bar answers with WM_HSCROLL, Right = line down */
  SetFocus(h);
  log_clear();
  key(0x27 /* VK_RIGHT */, 0x4D, 1);
  key(0x27, 0x4D, 0);
  n = scrolls(WM_HSCROLL, codes, &lp);
  CHECK_EQ(n, 1);
  if (n == 1) CHECK_EQ(codes[0], SB_LINEDOWN);
  CHECK_EQ(lp >> 16, h);
  CHECK_EQ(scrolls(WM_VSCROLL, codes, NULL), 0);
  end();
}

/* ---- arrows ---- */

static void test_enable_arrows(void) {
  HWND16 v;
  int codes[16], n, arrow;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  pump();
  arrow = SM_ARROW_V;
  CHECK_EQ(SendMessage(v, SBM_ENABLE_ARROWS, ESB_DISABLE_RTDN, 0), 1);  /* the state changed */
  CHECK_EQ(SendMessage(v, SBM_ENABLE_ARROWS, ESB_DISABLE_RTDN, 0), 0);  /* no change */
  log_clear();
  clickq(10 + 8, 10 + 100 - arrow / 2);                                  /* disabled down arrow */
  CHECK_EQ(scrolls(WM_VSCROLL, codes, NULL), 0);
  log_clear();
  clickq(10 + 8, 10 + arrow / 2);                                        /* the up arrow still works */
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK(n >= 1);
  if (n >= 1) CHECK_EQ(codes[0], SB_LINEUP);
  CHECK_EQ(SendMessage(v, SBM_ENABLE_ARROWS, ESB_ENABLE_BOTH, 0), 1);
  log_clear();
  clickq(10 + 8, 10 + 100 - arrow / 2);
  n = scrolls(WM_VSCROLL, codes, NULL);
  CHECK(n >= 1);
  if (n >= 1) CHECK_EQ(codes[0], SB_LINEDOWN);
  end();
}

static void test_disabled_window(void) {
  HWND16 v;
  int codes[16];
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  EnableWindow(v, 0);
  log_clear();
  clickq(10 + 8, 10 + 100 - 8);
  CHECK_EQ(scrolls(WM_VSCROLL, codes, NULL), 0);
  end();
}

/* ---- subclassing ---- */

static void test_subclass(void) {
  HWND16 v;
  FARPTR old;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, 100, 1);
  old = SetWindowLong(v, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_SCROLLBAR);
  g_old = old;
  log_clear();
  CHECK_EQ(SendMessage(v, SBM_SETPOS, 33, 1), 0);
  CHECK(log_find("V", SBM_SETPOS, 0) != NULL);
  CHECK_EQ(GetScrollPos(v, SB_CTL), 33);       /* forwarded through CallWindowProc to the class */
  SetWindowLong(v, GWL_WNDPROC, old);
  end();
}

/* ---- pixels (all UNVERIFIED) ---- */

/* COLOR_BTNFACE pixels in the 9x9 square at parent-client (x,y): the inside of a thumb is all face
 * (81); the trough is a dither of COLOR_BTNHIGHLIGHT and COLOR_SCROLLBAR, and in this colour scheme
 * COLOR_SCROLLBAR equals COLOR_BTNFACE, so about half (40 or 41). */
static int faces(int x, int y) { return count_in(x, y, x + 9, y + 9, FACE); }
#define THUMB_AT(x, y) (faces((x), (y)) == 81)
#define TROUGH_AT(x, y) (faces((x), (y)) >= 38 && faces((x), (y)) <= 43)

static void test_pixels(void) {
  HWND16 v;
  int arrow, thumb, len = 100, tx = 10 + 3;
  begin();
  v = SB("V", SBS_VERT, 10, 10, 16, len, 1);
  SetScrollRange(v, SB_CTL, 0, 100, 0);
  SetScrollPos(v, SB_CTL, 0, 0);
  pump();
  arrow = SM_ARROW_V;
  thumb = SM_THUMB_V;
  PIXEL_TEST("ctl_scrollbar_min");
  /* arrow buttons are outlined in COLOR_WINDOWFRAME with a white highlight inside top and left */
  PIXEL_EQ(10, 10, BLACK);
  PIXEL_EQ(10 + 15, 10, BLACK);
  PIXEL_EQ(10, 10 + arrow - 1, BLACK);
  PIXEL_EQ(10, 10 + len - 1, BLACK);
  PIXEL_EQ(10 + 1, 10 + 5, HILITE);
  PIXEL_EQ(10 + 5, 10 + 1, HILITE);
  /* the arrow glyph: COLOR_BTNTEXT pixels in the middle of the up arrow */
  CHECK(count_in(10 + 3, 10 + 3, 10 + 13, 10 + arrow - 3, BTNTXT) >= 6);
  /* position 0: the thumb sits against the first arrow */
  CHECK(THUMB_AT(tx, 10 + arrow + 3));
  CHECK(TROUGH_AT(tx, 10 + len - arrow - 12));
  /* position 100: the thumb moves to the end; SetScrollPos with bRedraw redraws the control */
  SetScrollPos(v, SB_CTL, 100, 1);
  PIXEL_TEST("ctl_scrollbar_max");
  CHECK(THUMB_AT(tx, 10 + len - arrow - thumb + 3));
  CHECK(TROUGH_AT(tx, 10 + arrow + 3));
  /* without bRedraw the picture stays until the next paint */
  SetScrollPos(v, SB_CTL, 0, 0);
  CHECK(THUMB_AT(tx, 10 + len - arrow - thumb + 3));
  InvalidateRect(v, 0, 0);
  pump();
  CHECK(THUMB_AT(tx, 10 + arrow + 3));
  /* middle */
  SetScrollPos(v, SB_CTL, 50, 1);
  PIXEL_TEST("ctl_scrollbar_mid");
  CHECK(THUMB_AT(tx, 10 + len / 2 - 4));
  CHECK(TROUGH_AT(tx, 10 + arrow + 3));
  CHECK(TROUGH_AT(tx, 10 + len - arrow - 12));
  dump_frame("ctl_scrollbar", PX0 + 0, PY0 + 0, 60, 120);
  /* no thumb when the range is empty, or when the window is disabled */
  SetScrollRange(v, SB_CTL, 10, 10, 1);
  PIXEL_TEST("ctl_scrollbar_empty");
  CHECK(TROUGH_AT(tx, 10 + len / 2 - 4));
  SetScrollRange(v, SB_CTL, 0, 100, 1);
  SetScrollPos(v, SB_CTL, 50, 1);   /* the empty range had clamped the position to 10 */
  CHECK(THUMB_AT(tx, 10 + len / 2 - 4));
  EnableWindow(v, 0);
  pump();
  PIXEL_TEST("ctl_scrollbar_disabled");
  CHECK(TROUGH_AT(tx, 10 + len / 2 - 4));
  CHECK_EQ(count_in(10 + 3, 10 + 3, 10 + 13, 10 + arrow - 3, BTNTXT), 0); /* the glyph is gray */
  end();
}

int main(void) {
  (void)dump;
  if (fixture_open() != 0) return 1;
  g_msg = far_alloc(sizeof(MSG16));
  CHECK(register_class(PCLS, FP_PROC_A, 0, 0) != 0);
  test_constants();
  test_class();
  test_position_api();
  test_vertical_clicks();
  test_horizontal_clicks();
  test_thumb_drag();
  test_keyboard();
  test_enable_arrows();
  test_disabled_window();
  test_subclass();
  test_pixels();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
