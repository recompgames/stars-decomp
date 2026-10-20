/* Windows 3.1 look, part 2: pixels of the chrome (task stars-0z4.9).
 *
 * TEST-FIRST. EVERY PIXEL EXPECTATION IN THIS FILE IS UNVERIFIED AGAINST WINDOWS 3.1.
 * Owner amendment to decision 8: no Windows 3.11 file exists and no reference capture was ever
 * made, so tests/ref/NAME.png does not exist. The expectations below are derived from
 *   - the Windows 3.1 metrics as remembered (UNVERIFIED; GetSystemMetrics: caption 20, frame 4,
 *     button 18, scroll bar 16) and the Windows 3.1 default colour scheme (GetSysColor), and
 *   - this runtime's reading of the Windows 3.1 look (button bevel, sizing-frame tick marks, glyph
 *     shapes, dithered scroll trough, MessageBox layout), listed in docs/inventory.md section 13
 *     "Windows 3.1 look".
 * Each pixel test names the reference PNG it WILL be compared against once the owner supplies
 * captures (PIXEL_TEST); until then the owner reviews the frame dumps this test writes to
 * re/frames/NAME.ppm. When a capture arrives the matching expectation is replaced by an exact
 * comparison with it, and wins over the derived value.
 *
 * Coordinates "w(x,y)" are window coordinates of the 200x150 overlapped window at (100,80). */
#include "test.h"

#include <sys/stat.h>

#include "user_test_common.h"
#include "runtime/user_internal.h"

#define CLS "VCCLS"
#define TOPW (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define WX 100
#define WY 80
#define WW 200
#define WH 150

static int g_pixel_tests;
static const char *g_ref;

/* A pixel test: the reference capture it is waiting for. */
#define PIXEL_TEST(png)                          \
  do {                                           \
    g_ref = "tests/ref/" png ".png (absent)";    \
    g_pixel_tests++;                             \
  } while (0)

static uint8_t PX(int x, int y) { return fb_pixels()[y * GDI_SCREEN_W + x]; }
/* pixel at window coordinates of the window whose origin is (ox,oy) */
static int g_ox = WX, g_oy = WY;
static uint8_t w_(int x, int y) { return PX(g_ox + x, g_oy + y); }

/* Palette index of a system colour as drawn: the static colour with the same RGB. */
static uint8_t CI(unsigned color) {
  static const int idx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
  uint32_t want = GetSysColor((uint16_t)color);
  for (int k = 0; k < 20; k++)
    if (fb_palette_entry((unsigned)idx[k]) == want) return (uint8_t)idx[k];
  return 0;
}
#define BLACK CI(6)        /* COLOR_WINDOWFRAME */
#define FACE CI(15)        /* COLOR_BTNFACE */
#define HILITE CI(20)      /* COLOR_BTNHIGHLIGHT */
#define SHADOW CI(16)      /* COLOR_BTNSHADOW */
#define NAVY CI(2)         /* COLOR_ACTIVECAPTION */
#define WHITEC CI(3)       /* COLOR_INACTIVECAPTION */
#define BORDERC CI(10)     /* COLOR_ACTIVEBORDER */
#define DESK CI(1)         /* COLOR_BACKGROUND */

#define PIXEL_EQ(x, y, want)                                                                        \
  do {                                                                                              \
    int got_ = w_((x), (y));                                                                        \
    test_checks_run++;                                                                              \
    if (got_ != (int)(want)) {                                                                      \
      test_checks_failed++;                                                                         \
      fprintf(stderr, "%s:%d: UNVERIFIED pixel w(%d,%d) = %d, want %d (%s)\n", __FILE__, __LINE__,  \
              (x), (y), got_, (int)(want), g_ref ? g_ref : "?");                                    \
    }                                                                                               \
  } while (0)
#define PIXEL_NE(x, y, bad)                                                                         \
  do {                                                                                              \
    int got_ = w_((x), (y));                                                                        \
    test_checks_run++;                                                                              \
    if (got_ == (int)(bad)) {                                                                       \
      test_checks_failed++;                                                                         \
      fprintf(stderr, "%s:%d: UNVERIFIED pixel w(%d,%d) = %d, must differ (%s)\n", __FILE__, __LINE__, \
              (x), (y), got_, g_ref ? g_ref : "?");                                                 \
    }                                                                                               \
  } while (0)

/* Counts pixels of one palette index in a window-coordinate rectangle. */
static int count_in(int l, int t, int r, int b, int idx) {
  int n = 0;
  for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) n += (w_(x, y) == idx);
  return n;
}

/* Frame dumps for the owner's review (re/frames is git-ignored). */
static void dump_frame(const char *name, int x, int y, int w, int h) {
  static uint32_t rgba[GDI_SCREEN_W * GDI_SCREEN_H];
  char path[160];
  FILE *f;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > GDI_SCREEN_W) w = GDI_SCREEN_W - x;
  if (y + h > GDI_SCREEN_H) h = GDI_SCREEN_H - y;
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

static HWND16 make_top(uint32_t style, const char *title) {
  HWND16 h = mk("T", CLS, title, style, WX, WY, WW, WH, 0, 0);
  CHECK(h != 0);
  g_ox = WX;
  g_oy = WY;
  return h;
}

static void paint_all(void) {
  FARPTR p = far_alloc(sizeof(MSG16));
  int guard = 0;
  while (guard++ < 200 && PeekMessage(p, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessage(p);
}

static void clear_desktop(void) { user_paint_desktop(); }

/* ---- frame ---- */

static void test_active_frame_and_caption(void) {
  HWND16 h;
  clear_desktop();
  h = make_top(TOPW, "Main");
  paint_all();
  PIXEL_TEST("chrome_active_window");
  /* outline: COLOR_WINDOWFRAME, one pixel */
  PIXEL_EQ(0, 60, BLACK);
  PIXEL_EQ(199, 60, BLACK);
  PIXEL_EQ(100, 0, BLACK);
  PIXEL_EQ(100, 149, BLACK);
  PIXEL_EQ(0, 0, BLACK);
  PIXEL_EQ(199, 149, BLACK);
  /* the other three pixels of the SM_CXFRAME = 4 wide frame: COLOR_ACTIVEBORDER */
  PIXEL_EQ(1, 60, BORDERC);
  PIXEL_EQ(2, 60, BORDERC);
  PIXEL_EQ(3, 60, BORDERC);
  PIXEL_EQ(196, 60, BORDERC);
  PIXEL_EQ(198, 60, BORDERC);
  PIXEL_EQ(100, 1, BORDERC);
  PIXEL_EQ(100, 3, BORDERC);
  PIXEL_EQ(100, 146, BORDERC);
  PIXEL_EQ(100, 148, BORDERC);
  /* sizing-frame tick marks, one pixel wide across the three inner pixels, SM_CXSIZE (18) from
   * each corner */
  PIXEL_EQ(1, 18, BLACK);  PIXEL_EQ(2, 18, BLACK);  PIXEL_EQ(3, 18, BLACK);
  PIXEL_EQ(196, 18, BLACK); PIXEL_EQ(198, 18, BLACK);
  PIXEL_EQ(1, 132, BLACK); PIXEL_EQ(198, 132, BLACK);
  PIXEL_EQ(18, 1, BLACK);  PIXEL_EQ(18, 3, BLACK);
  PIXEL_EQ(182, 1, BLACK); PIXEL_EQ(182, 148, BLACK);
  PIXEL_EQ(18, 148, BLACK);
  PIXEL_EQ(2, 17, BORDERC);
  PIXEL_EQ(2, 19, BORDERC);
  PIXEL_EQ(17, 2, BORDERC);
  PIXEL_EQ(19, 2, BORDERC);
  /* caption bar: COLOR_ACTIVECAPTION from (4,4) to (196,23), black separator line below it */
  PIXEL_EQ(30, 6, NAVY);
  PIXEL_EQ(150, 6, NAVY);
  PIXEL_EQ(30, 21, NAVY);
  PIXEL_EQ(150, 22, NAVY);
  PIXEL_EQ(5, 23, BLACK);
  PIXEL_EQ(100, 23, BLACK);
  PIXEL_EQ(195, 23, BLACK);
  /* the title is drawn centred in COLOR_CAPTIONTEXT (white) in the System font */
  CHECK(count_in(22, 4, 160, 23, 255) > 10);
  {
    int l = 160, r = 22;
    for (int y = 4; y < 23; y++)
      for (int x = 22; x < 160; x++)
        if (w_(x, y) == 255) {
          if (x < l) l = x;
          if (x > r) r = x;
        }
    CHECK(abs((l + r) / 2 - (22 + 160) / 2) <= 2);     /* centred between the system box and the buttons */
  }
  dump_frame("chrome_active_window", WX - 4, WY - 4, WW + 8, WH + 8);
  DestroyWindow(h);
}

static void test_inactive_caption(void) {
  HWND16 a, b;
  clear_desktop();
  a = make_top(TOPW, "Main");
  b = mk("U", CLS, "Other", TOPW, 400, 80, WW, WH, 0, 0);    /* becomes the active window */
  paint_all();
  PIXEL_TEST("chrome_inactive_window");
  CHECK_EQ(GetActiveWindow(), b);
  g_ox = WX; g_oy = WY;
  PIXEL_EQ(30, 6, WHITEC);                                   /* COLOR_INACTIVECAPTION is white */
  PIXEL_EQ(150, 22, WHITEC);
  PIXEL_EQ(100, 23, BLACK);
  CHECK(count_in(22, 4, 160, 23, 0) > 10);                   /* title in COLOR_INACTIVECAPTIONTEXT (black) */
  CHECK_EQ(count_in(22, 4, 160, 23, 4), 0);                  /* no navy anywhere in it */
  g_ox = 400; g_oy = 80;
  PIXEL_EQ(30, 6, NAVY);
  g_ox = WX; g_oy = WY;
  /* activating the first one swaps the looks */
  SetActiveWindow(a);
  paint_all();
  PIXEL_EQ(30, 6, NAVY);
  g_ox = 400;
  PIXEL_EQ(30, 6, WHITEC);
  dump_frame("chrome_inactive_window", WX - 4, WY - 4, 520, WH + 8);
  g_ox = WX;
  DestroyWindow(b);
  DestroyWindow(a);
}

static void test_caption_buttons(void) {
  HWND16 h;
  clear_desktop();
  h = make_top(TOPW, "Main");
  paint_all();
  PIXEL_TEST("chrome_caption_buttons");
  /* minimise button (160,4)-(178,23): black left edge, white highlight top and left, COLOR_BTNSHADOW
   * bottom and right, COLOR_BTNFACE inside */
  PIXEL_EQ(160, 10, BLACK);
  PIXEL_EQ(160, 4, BLACK);
  PIXEL_EQ(170, 4, HILITE);
  PIXEL_EQ(161, 12, HILITE);
  PIXEL_EQ(170, 22, SHADOW);
  PIXEL_EQ(177, 12, SHADOW);
  PIXEL_EQ(164, 8, FACE);
  PIXEL_EQ(172, 9, FACE);
  /* minimise glyph: a downward triangle 7 wide, 4 high at (165,11) */
  PIXEL_EQ(165, 11, BLACK); PIXEL_EQ(171, 11, BLACK);
  PIXEL_EQ(166, 12, BLACK); PIXEL_EQ(170, 12, BLACK);
  PIXEL_EQ(168, 14, BLACK);
  PIXEL_EQ(165, 12, FACE);
  PIXEL_EQ(168, 15, FACE);
  /* maximise button (178,4)-(196,23); upward triangle at (183,11) */
  PIXEL_EQ(178, 10, BLACK);
  PIXEL_EQ(186, 11, BLACK);
  PIXEL_EQ(185, 12, BLACK); PIXEL_EQ(187, 12, BLACK);
  PIXEL_EQ(183, 14, BLACK); PIXEL_EQ(189, 14, BLACK);
  PIXEL_EQ(183, 11, FACE);
  PIXEL_EQ(195, 12, SHADOW);
  /* system menu box (4,4)-(22,23): black right edge, the control-menu bar glyph */
  PIXEL_EQ(21, 12, BLACK);
  PIXEL_EQ(10, 4, HILITE);
  PIXEL_EQ(4, 12, HILITE);
  PIXEL_EQ(10, 22, SHADOW);
  PIXEL_EQ(20, 12, SHADOW);
  PIXEL_EQ(6, 6, FACE);
  PIXEL_EQ(7, 10, BLACK); PIXEL_EQ(16, 14, BLACK); PIXEL_EQ(12, 10, BLACK);
  PIXEL_EQ(10, 12, HILITE);
  /* a window with no minimise box shows only the maximise button: nothing at 160..177 */
  DestroyWindow(h);
  clear_desktop();
  h = make_top(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_VISIBLE, "x");
  paint_all();
  PIXEL_NE(160, 10, BLACK);
  PIXEL_EQ(160, 10, NAVY);
  PIXEL_EQ(181, 10, BLACK);            /* the maximise button is (181,1)-(199,20) in a one pixel border */
  PIXEL_EQ(198, 10, SHADOW);
  DestroyWindow(h);
}

/* Pressed look while tracking: bevel swaps (shadow on top/left, no highlight). Sampled from a
 * hook that runs inside the button-tracking loop. */
static int g_pressed_seen;
static bool hook_pressed(void) {
  static int step;
  if (step == 0) {
    step++;
    g_pressed_seen = 1;
    PIXEL_EQ(161, 12, SHADOW);
    PIXEL_EQ(170, 4, SHADOW);
    PIXEL_EQ(164, 8, FACE);
    rt_input_mouse(269, 90, 0, 9000);
    return true;
  }
  step = 0;
  return false;
}

static void test_pressed_button(void) {
  HWND16 h;
  clear_desktop();
  h = make_top(TOPW, "Main");
  paint_all();
  PIXEL_TEST("chrome_button_pressed");
  rt_input_mouse(269, 90, RT_BTN_LEFT, 8000);
  user_wait_hook = hook_pressed;
  {
    FARPTR p = far_alloc(sizeof(MSG16));
    int guard = 0;
    while (guard++ < 50 && GetMessage(p, 0, 0, 0)) DispatchMessage(p);
  }
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  CHECK_EQ(g_pressed_seen, 1);
  CHECK_EQ(IsIconic(h), 1);
  ShowWindow(h, SW_RESTORE);
  DestroyWindow(h);
}

static void test_maximized_restore_glyph(void) {
  HWND16 h;
  clear_desktop();
  h = make_top(TOPW, "Main");
  ShowWindow(h, SW_SHOWMAXIMIZED);
  paint_all();
  PIXEL_TEST("chrome_maximized_window");
  g_ox = -4; g_oy = -4;
  /* the maximise button shows the restore glyph: up triangle over a down triangle (UNVERIFIED shape) */
  PIXEL_EQ(794, 8, BLACK);
  PIXEL_EQ(791, 11, BLACK); PIXEL_EQ(797, 11, BLACK);
  PIXEL_EQ(791, 13, BLACK); PIXEL_EQ(797, 13, BLACK);
  PIXEL_EQ(794, 16, BLACK);
  PIXEL_EQ(794, 12, FACE);
  dump_frame("chrome_maximized_window", 0, 0, 800, 60);
  g_ox = WX; g_oy = WY;
  ShowWindow(h, SW_RESTORE);
  DestroyWindow(h);
}

static void test_other_frames(void) {
  HWND16 h;
  clear_desktop();
  PIXEL_TEST("chrome_frames");
  /* WS_BORDER: one black pixel */
  h = mk("B", CLS, "b", WS_POPUP | WS_BORDER | WS_VISIBLE, 300, 300, 100, 50, 0, 0);
  paint_all();
  g_ox = 300; g_oy = 300;
  PIXEL_EQ(0, 25, BLACK);
  PIXEL_EQ(99, 25, BLACK);
  PIXEL_EQ(50, 0, BLACK);
  PIXEL_EQ(50, 49, BLACK);
  DestroyWindow(h);
  clear_desktop();
  /* WS_DLGFRAME: black outline plus three COLOR_ACTIVEBORDER pixels, no tick marks */
  h = mk("D", CLS, "d", WS_POPUP | WS_DLGFRAME | WS_VISIBLE, 300, 300, 100, 50, 0, 0);
  paint_all();
  PIXEL_EQ(0, 25, BLACK);
  PIXEL_EQ(1, 25, BORDERC);
  PIXEL_EQ(3, 25, BORDERC);
  PIXEL_EQ(2, 18, BORDERC);
  PIXEL_EQ(50, 3, BORDERC);
  DestroyWindow(h);
  clear_desktop();
  /* DS_MODALFRAME (WS_EX_DLGMODALFRAME) around a caption */
  h = mk("M", CLS, "Modal", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 300, 300, 150, 80, 0, 0);
  SetWindowLong(h, GWL_EXSTYLE, WS_EX_DLGMODALFRAME);
  SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  paint_all();
  PIXEL_EQ(0, 40, BLACK);
  PIXEL_EQ(2, 40, BORDERC);
  PIXEL_EQ(60, 6, NAVY);
  PIXEL_EQ(60, 23, BLACK);
  PIXEL_EQ(2, 18, BORDERC);       /* no tick marks on a dialog frame */
  DestroyWindow(h);
  clear_desktop();
  /* WS_CAPTION alone: a one pixel border */
  h = mk("C", CLS, "Cap", WS_POPUP | WS_CAPTION | WS_VISIBLE, 300, 300, 150, 80, 0, 0);
  paint_all();
  PIXEL_EQ(0, 40, BLACK);
  PIXEL_EQ(1, 1, NAVY);
  PIXEL_EQ(60, 20, BLACK);        /* separator at y = 1 + 19 */
  DestroyWindow(h);
  g_ox = WX; g_oy = WY;
}

static void test_iconic(void) {
  HWND16 h;
  RECT16 r;
  clear_desktop();
  /* the runtime draws the icon only for a class that has one (a class without an icon paints its
   * iconic state itself: test_user_nc.c test_iconic_no_class_icon); give the class the default icon */
  {
    FARPTR pw = far_alloc(sizeof(WNDCLASS16));
    WNDCLASS16 *w = LIN(pw);
    w->lpfnWndProc = FP_PROC_A;
    w->hIcon = LoadIcon(0, (FARPTR)IDI_APPLICATION_ID);
    w->lpszClassName = fstr("VCICONCLS");
    w->hInstance = 0x1234;
    CHECK(RegisterClass(pw) != 0);
  }
  h = mk("T", "VCICONCLS", "Hello", TOPW, WX, WY, WW, WH, 0, 0);
  CHECK(h != 0);
  g_ox = WX; g_oy = WY;
  paint_all();
  ShowWindow(h, SW_SHOWMINIMIZED);
  paint_all();
  PIXEL_TEST("chrome_iconic_window");
  r = get_rect(h, 0);
  g_ox = r.left; g_oy = r.top;
  CHECK_EQ(r.right - r.left, 75);
  /* the default application icon is centred horizontally at the top, the title is drawn
   * below it (UNVERIFIED layout); the rest of the 75x52 area is desktop */
  {
    int icon = 0, title = 0, stray = 0;
    for (int y = 0; y < 52; y++)
      for (int x = 0; x < 75; x++) {
        int c = w_(x, y);
        if (c == DESK) continue;
        if (x >= 21 && x < 53 && y >= 2 && y < 34) icon++;
        else if (y >= 34) title++;
        else stray++;
      }
    CHECK(icon > 100);
    CHECK(title > 10);
    CHECK_EQ(stray, 0);
  }
  /* the window is no longer drawn at its old place */
  g_ox = WX; g_oy = WY;
  PIXEL_EQ(10, 10, DESK);
  PIXEL_EQ(100, 100, DESK);
  dump_frame("chrome_iconic_window", 0, 520, 200, 80);
  ShowWindow(h, SW_RESTORE);
  DestroyWindow(h);
}

/* ---- scroll bars ---- */

static void check_vbar(int thumb_top, int enabled) {
  /* bar (180,24)-(196,130): arrows 16 high, trough 40..114, thumb 16 high */
  PIXEL_EQ(180, 24, BLACK);                 /* arrow button outline */
  PIXEL_EQ(195, 24, BLACK);
  PIXEL_EQ(180, 39, BLACK);
  PIXEL_EQ(181, 30, HILITE);
  PIXEL_EQ(194, 30, SHADOW);
  PIXEL_EQ(183, 38, SHADOW);
  PIXEL_EQ(182, 30, FACE);
  PIXEL_EQ(180, 114, BLACK);
  PIXEL_EQ(188, 129, BLACK);
  if (enabled) {
    PIXEL_EQ(188, 30, BLACK);                /* up triangle 7 wide, 4 high at (185,30) */
    PIXEL_EQ(185, 33, BLACK); PIXEL_EQ(191, 33, BLACK);
    PIXEL_EQ(185, 30, FACE);
    PIXEL_EQ(185, 120, BLACK); PIXEL_EQ(191, 120, BLACK);   /* down triangle at (185,120) */
    PIXEL_EQ(188, 123, BLACK);
  } else {
    PIXEL_EQ(188, 30, SHADOW);               /* disabled arrows: COLOR_BTNSHADOW glyph */
    PIXEL_EQ(185, 33, SHADOW);
    PIXEL_EQ(188, 123, SHADOW);
  }
  if (thumb_top >= 0) {
    PIXEL_EQ(180, thumb_top, BLACK);         /* thumb 16x16 button */
    PIXEL_EQ(195, thumb_top + 15, BLACK);
    PIXEL_EQ(188, thumb_top + 8, FACE);
    PIXEL_EQ(181, thumb_top + 8, HILITE);
    PIXEL_EQ(194, thumb_top + 8, SHADOW);
  }
  /* trough: dither of COLOR_SCROLLBAR and COLOR_BTNHIGHLIGHT, white where (screen x + y) is even.
   * Checked in a trough band that the thumb never covers in this test: y 99..113 and 41..55 only
   * when the thumb is elsewhere. */
  for (int y = 41; y < 113; y += 7) {
    if (thumb_top >= 0 && y + 1 >= thumb_top && y <= thumb_top + 16) continue;
    for (int x = 181; x < 195; x += 5) {
      int want = ((g_ox + x + g_oy + y) & 1) ? CI(0) : CI(20);
      PIXEL_EQ(x, y, want);
    }
  }
}

static void test_scrollbar_pixels(void) {
  HWND16 h;
  clear_desktop();
  h = make_top(TOPW | WS_VSCROLL | WS_HSCROLL, "Scroll");
  paint_all();
  PIXEL_TEST("chrome_scrollbar_min_mid_max");
  SetScrollRange(h, SB_VERT, 0, 100, 1);
  SetScrollPos(h, SB_VERT, 0, 1);          /* minimum: thumb right under the up arrow (y 40..56) */
  check_vbar(40, 1);
  dump_frame("chrome_scrollbar_min", WX, WY, WW, WH);
  SetScrollPos(h, SB_VERT, 50, 1);         /* middle: 40 + 58 * 50 / 100 = 69 */
  check_vbar(69, 1);
  dump_frame("chrome_scrollbar_mid", WX, WY, WW, WH);
  SetScrollPos(h, SB_VERT, 100, 1);        /* maximum: 98..114 */
  check_vbar(98, 1);
  dump_frame("chrome_scrollbar_max", WX, WY, WW, WH);
  /* the thumb left the old place: it is trough again */
  CHECK(w_(188, 48) == CI(0) || w_(188, 48) == CI(20));
  /* thumb positions follow the rounding (n * range + d/2) / d (UNVERIFIED rounding) */
  SetScrollRange(h, SB_VERT, 0, 7, 1);
  SetScrollPos(h, SB_VERT, 3, 1);          /* 3 * 58 / 7 = 24.86 -> 25 */
  PIXEL_EQ(180, 40 + 25, BLACK);
  /* an empty range (min == max) hides a window bar (Wine SCROLL_SetScrollInfo; UNVERIFIED) ... */
  SetScrollRange(h, SB_VERT, 4, 4, 1);
  CHECK_EQ(GetWindowLong(h, GWL_STYLE) & WS_VSCROLL, 0);
  /* ... and ShowScrollBar brings it back as a disabled bar: no thumb, grey arrows */
  ShowScrollBar(h, SB_VERT, 1);
  paint_all();
  check_vbar(-1, 0);
  /* size box where the two bars meet */
  PIXEL_EQ(188, 138, FACE);
  PIXEL_EQ(181, 131, FACE);
  /* horizontal bar (4,130)-(180,146): left arrow at 4..20, thumb travels from 20 to 164 */
  SetScrollRange(h, SB_HORZ, 0, 100, 1);
  SetScrollPos(h, SB_HORZ, 0, 1);
  PIXEL_EQ(20, 130, BLACK);                /* thumb 16 wide at the minimum */
  PIXEL_EQ(28, 138, FACE);
  PIXEL_EQ(4, 130, BLACK);
  PIXEL_EQ(10, 138, BLACK);                /* left triangle, apex at (10,138): column x=12 is 5 high */
  PIXEL_EQ(12, 136, BLACK); PIXEL_EQ(13, 135, BLACK);
  PIXEL_EQ(10, 137, FACE);
  SetScrollPos(h, SB_HORZ, 100, 1);        /* travel = 160 - 32 = 128: thumb at 4 + 16 + 128 = 148..164 */
  PIXEL_EQ(148, 130, BLACK);
  PIXEL_EQ(156, 138, FACE);
  dump_frame("chrome_scrollbar_horz", WX, WY, WW, WH);
  DestroyWindow(h);
}

/* ---- MessageBox ---- */

typedef struct Mb {
  HWND16 hwnd;
  int nbtn, def;
  RECT16 btn[3], icon, text;
  uint16_t ids[3];
} Mb;
static Mb g_mb;
static int g_mb_ok;

static bool mb_look(void) {
  static int once;
  if (once) { once = 0; return false; }
  once = 1;
  g_mb_ok = user_msgbox_info(&g_mb.hwnd, &g_mb.nbtn, g_mb.btn, g_mb.ids, &g_mb.def, &g_mb.icon, &g_mb.text);
  if (!g_mb_ok) return false;
  {
    RECT16 r = get_rect(g_mb.hwnd, 0);
    int sx0 = r.left, sy0 = r.top;
    g_ox = 0; g_oy = 0;
    /* frame of the box: dialog frame (outline, 3 pixels of COLOR_ACTIVEBORDER), navy caption with
     * the title in white, black separator, then the white (COLOR_WINDOW) client area */
    PIXEL_TEST("chrome_messagebox");
    PIXEL_EQ(sx0, sy0 + 30, BLACK);
    PIXEL_EQ(sx0 + 2, sy0 + 30, BORDERC);
    PIXEL_EQ(sx0 + 40, sy0 + 6, NAVY);
    PIXEL_EQ(sx0 + 40, sy0 + 23, BLACK);
    PIXEL_EQ(sx0 + 5, sy0 + 26, CI(5));
    {
      int white_title = 0;
      for (int y = sy0 + 4; y < sy0 + 23; y++)
        for (int x = sx0 + 22; x < sx0 + (r.right - r.left) - 4; x++) white_title += (PX(x, y) == 255);
      CHECK(white_title > 10);
    }
    /* buttons: COLOR_BTNFACE with a black outline whose corner pixel is cut (rounded); the
     * default button has a two pixel outline */
    for (int i = 0; i < g_mb.nbtn; i++) {
      RECT16 b = g_mb.btn[i];
      int mid = (b.top + b.bottom) / 2;
      PIXEL_EQ(b.left + 6, b.top, BLACK);
      PIXEL_EQ(b.left, mid, BLACK);
      PIXEL_EQ(b.right - 1, mid, BLACK);
      PIXEL_NE(b.left, b.top, BLACK);                 /* rounded corner */
      PIXEL_EQ(b.left + 1, mid, i == g_mb.def ? BLACK : HILITE);
      PIXEL_EQ(b.left + 2, mid, i == g_mb.def ? HILITE : FACE);
      {
        int dark = 0;      /* the label: black pixels inside the button */
        for (int y = b.top + 3; y < b.bottom - 3; y++)
          for (int x = b.left + 3; x < b.right - 3; x++) dark += (PX(x, y) == BLACK);
        CHECK(dark > 8);
      }
    }
    /* icon and text are drawn */
    {
      int ink = 0, nonwhite = 0;
      for (int y = g_mb.text.top; y < g_mb.text.bottom; y++)
        for (int x = g_mb.text.left; x < g_mb.text.right; x++) ink += (PX(x, y) == BLACK);
      for (int y = g_mb.icon.top; y < g_mb.icon.bottom; y++)
        for (int x = g_mb.icon.left; x < g_mb.icon.right; x++) nonwhite += (PX(x, y) != CI(5));
      CHECK(ink > 20);
      CHECK(nonwhite > 100);
    }
    dump_frame("chrome_messagebox", sx0 - 4, sy0 - 4, r.right - r.left + 8, r.bottom - r.top + 8);
    g_ox = WX; g_oy = WY;
  }
  rt_input_key(VK_RETURN, 0x1C, 1, 10000);
  rt_input_key(VK_RETURN, 0x1C, 0, 10001);
  return true;
}

static void test_messagebox_pixels(void) {
  int16_t r;
  clear_desktop();
  user_wait_hook = mb_look;
  r = MessageBox(0, fstr("Save changes to the game?"), fstr("Stars!"), MB_YESNOCANCEL | MB_ICONQUESTION);
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  CHECK(g_mb_ok);
  CHECK_EQ(r, IDYES);
  CHECK_EQ(g_mb.def, 0);
  /* the box is gone and the desktop is back */
  PIXEL_EQ(400 - WX, 300 - WY, DESK);
}

/* ---- icons ---- */

static void test_desktop_pixels(void) {
  clear_desktop();
  PIXEL_TEST("chrome_desktop");
  g_ox = 0; g_oy = 0;
  PIXEL_EQ(0, 0, DESK);
  PIXEL_EQ(799, 599, DESK);
  PIXEL_EQ(400, 300, DESK);
  g_ox = WX; g_oy = WY;
}

/* Mutation anchor (issue acceptance): swapping the active and inactive caption colours in
 * runtime/user_nc.c must make the active/inactive caption pixel checks above fail. */

int main(void) {
  (void)dump;
  (void)log_clear;
  (void)log_find;
  if (fixture_open() != 0) return 1;
  CHECK(register_class(CLS, FP_PROC_A, 0, 0) != 0);
  test_desktop_pixels();
  test_active_frame_and_caption();
  test_inactive_caption();
  test_caption_buttons();
  test_pressed_button();
  test_maximized_restore_glyph();
  test_other_frames();
  test_iconic();
  test_scrollbar_pixels();
  test_messagebox_pixels();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
