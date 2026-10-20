/* Expectations for the STATIC control class (runtime/ctl_static.c). Task stars-0z4.10, written
 * BEFORE the implementation. Do not edit an expectation to match the code: a disagreement is
 * reported with both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here and no reference capture exists):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h lines 451-452: STM_SETICON16 WM_USER+0,
 *       STM_GETICON16 WM_USER+1 (the Win32 values 0x0170/0x0171 must NOT be used).
 *   SS_* styles            include/winuser.rh 1264-1276 (SS_LEFT 0 ... SS_LEFTNOWORDWRAP 0xC),
 *       SS_NOPREFIX 0x80 (1286), SS_NOTIFY 0x100 (1287).
 *   STN_CLICKED 0, STN_DBLCLK 1, STN_ENABLE 2, STN_DISABLE 3: include/winuser.h 1831-1834.
 *   Text formats           dlls/user32/static.c STATIC_PaintTextfn 603-632: SS_LEFT/CENTER/RIGHT use
 *       DT_EXPANDTABS | DT_WORDBREAK with the matching alignment; SS_SIMPLE DT_LEFT | DT_SINGLELINE;
 *       SS_LEFTNOWORDWRAP DT_LEFT | DT_EXPANDTABS.
 *   WM_NCHITTEST           static.c 517-521: HTTRANSPARENT unless SS_NOTIFY; 528-537: STN_CLICKED on
 *       button down and STN_DBLCLK on double click with SS_NOTIFY.
 *   WM_CTLCOLOR            the control asks its parent for the background brush and text colour with
 *       HIWORD(lParam) = CTLCOLOR_STATIC (6), LOWORD(lParam) = control: winuser.h 930-937 and
 *       dlls/user.exe16/message.c 1193-1201. Default brush: win32u/defwnd.c 2335-2366.
 *
 * UNVERIFIED:
 *   - Every pixel expectation (text origin, the colours of the SS_*RECT / SS_*FRAME styles, icon
 *     placement, line wrapping): no reference capture exists. The colours follow the Windows 3.1
 *     SDK description recalled from memory: SS_BLACKRECT/FRAME COLOR_WINDOWFRAME, SS_GRAYRECT/FRAME
 *     COLOR_BACKGROUND, SS_WHITERECT/FRAME COLOR_WINDOW. Wine's static.c 701-730 uses its 3-D colours
 *     instead (Win95 look); Wine is NOT followed here. Each pixel test names the PNG it will be
 *     compared against (PIXEL_TEST). The checks are of structure: where text, fill and frame land
 *     relative to each other.
 *   - Not implemented (documented omissions): SS_OWNERDRAW, SS_BITMAP, SS_ETCHED*, SS_CENTERIMAGE
 *     (Win32 only), a disabled look for static text (an SS_ICON icon from the window text IS loaded).
 */
#include "test.h"

#include "ctl_test_common.h"
#include "runtime/kernel.h"

/* bounding box of COLOR_WINDOWTEXT pixels in a parent-client rectangle; false when none */
typedef struct BBox {
  int l, t, r, b, n;
} BBox;
static BBox bbox(int x0, int y0, int x1, int y1) {
  BBox bb = {1 << 20, 1 << 20, -1, -1, 0};
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++)
      if (pc(x, y) == BLACK) {
        if (x < bb.l) bb.l = x;
        if (x > bb.r) bb.r = x;
        if (y < bb.t) bb.t = y;
        if (y > bb.b) bb.b = y;
        bb.n++;
      }
  return bb;
}

static HWND16 S(const char *label, const char *text, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "STATIC", text, style, x, y, w, h, id);
}

static void test_constants(void) {
  CHECK_EQ(STM_SETICON, 0x0400);
  CHECK_EQ(STM_GETICON, 0x0401);
  CHECK_EQ(SS_LEFT, 0);
  CHECK_EQ(SS_CENTER, 1);
  CHECK_EQ(SS_RIGHT, 2);
  CHECK_EQ(SS_ICON, 3);
  CHECK_EQ(SS_BLACKRECT, 4);
  CHECK_EQ(SS_GRAYRECT, 5);
  CHECK_EQ(SS_WHITERECT, 6);
  CHECK_EQ(SS_BLACKFRAME, 7);
  CHECK_EQ(SS_GRAYFRAME, 8);
  CHECK_EQ(SS_WHITEFRAME, 9);
  CHECK_EQ(SS_USERITEM, 0xA);
  CHECK_EQ(SS_SIMPLE, 0xB);
  CHECK_EQ(SS_LEFTNOWORDWRAP, 0xC);
  CHECK_EQ(SS_TYPEMASK, 0x1F);
  CHECK_EQ(SS_NOPREFIX, 0x80);
  CHECK_EQ(SS_NOTIFY, 0x100);
  CHECK_EQ(STN_CLICKED, 0);
  CHECK_EQ(STN_DBLCLK, 1);
  CHECK_EQ(STN_ENABLE, 2);
  CHECK_EQ(STN_DISABLE, 3);
}

static void test_class_and_text(void) {
  HWND16 s;
  FARPTR p;
  begin();
  s = S("S", "Hello", SS_LEFT, 10, 10, 150, 20, 11);
  CHECK_EQ(FP_SEG(GetWindowLong(s, GWL_WNDPROC)), MEM_SEL_CALLBACK);
  CHECK_EQ(GetWindowLong(s, GWL_WNDPROC), USER_CB_STATIC);
  p = far_alloc(32);
  CHECK_EQ(GetWindowText(s, p, 32), 5);
  CHECK_EQ(strcmp(LIN(p), "Hello"), 0);
  CHECK_EQ(SendMessage(s, WM_SETTEXT, 0, fstr("Bye now")), 1);
  CHECK_EQ(GetWindowText(s, p, 32), 7);
  CHECK_EQ(strcmp(LIN(p), "Bye now"), 0);
  CHECK_EQ(SendMessage(s, WM_GETTEXTLENGTH, 0, 0), 7);
  CHECK_EQ(SendMessage(s, WM_GETDLGCODE, 0, 0), DLGC_STATIC);
  end();
}

static void test_font_message(void) {
  HWND16 s;
  begin();
  s = S("S", "x", SS_LEFT, 10, 10, 50, 16, 12);
  CHECK_EQ(SendMessage(s, WM_GETFONT, 0, 0), 0);
  SendMessage(s, WM_SETFONT, GetStockObject(SYSTEM_FONT), 1);
  CHECK_EQ(SendMessage(s, WM_GETFONT, 0, 0), GetStockObject(SYSTEM_FONT));
  end();
}

static void test_text_alignment(void) {
  HWND16 l, c, r;
  BBox bl, bc, br;
  begin();
  underlay(0, 0, 170, 100);
  l = S("L", "Hello", SS_LEFT, 10, 10, 150, 20, 21);
  c = S("C", "Hello", SS_CENTER, 10, 40, 150, 20, 22);
  r = S("R", "Hello", SS_RIGHT, 10, 70, 150, 20, 23);
  pump();
  PIXEL_TEST("ctl_static_text");
  PIXEL_EQ(10, 10, FACE); /* the background is the parent's WM_CTLCOLOR brush: COLOR_BTNFACE by default */
  PIXEL_EQ(10 + 149, 10 + 19, FACE);
  bl = bbox(10, 10, 160, 30);
  bc = bbox(10, 40, 160, 60);
  br = bbox(10, 70, 160, 90);
  CHECK(bl.n > 10 && bc.n > 10 && br.n > 10);
  CHECK(bl.l >= 10 && bl.l <= 13);              /* left aligned */
  CHECK(br.r >= 10 + 146 && br.r <= 10 + 149);  /* right aligned */
  {
    int gl = bc.l - 10, gr = (10 + 149) - bc.r;  /* centred: equal gaps */
    CHECK(gl - gr <= 3 && gr - gl <= 3);
    CHECK(gl > 30);
  }
  CHECK_EQ(bl.r - bl.l, br.r - br.l);           /* same text, same width */
  CHECK(bl.t >= 10 && bl.b < 10 + 20);
  dump_frame("ctl_static_text", PX0 + 0, PY0 + 0, 170, 100);
  /* SetWindowText repaints: an empty text leaves only the background */
  SendMessage(l, WM_SETTEXT, 0, fstr(""));
  pump();
  CHECK_EQ(bbox(10, 10, 160, 30).n, 0);
  CHECK_EQ(pc(30, 20), FACE);
  (void)c;
  (void)r;
  end();
}

static void test_wrap_and_nowrap(void) {
  BBox wrap, nowrap, simple;
  begin();
  S("W", "Hello World again", SS_LEFT, 10, 10, 60, 60, 31);
  S("N", "Hello World again", SS_LEFTNOWORDWRAP, 110, 10, 60, 60, 32);
  S("M", "Hello World again", SS_SIMPLE, 210, 10, 60, 60, 33);
  pump();
  wrap = bbox(10, 10, 70, 70);
  nowrap = bbox(110, 10, 170, 70);
  simple = bbox(210, 10, 270, 70);
  CHECK(wrap.n > 0 && nowrap.n > 0 && simple.n > 0);
  /* SS_LEFT wraps inside the 60 pixel width: more than one line (static.c 615-617) */
  CHECK(wrap.b - wrap.t > nowrap.b - nowrap.t + 8);
  /* the wrapped text stays inside the control */
  CHECK(wrap.r < 70);
  /* SS_LEFTNOWORDWRAP and SS_SIMPLE are a single line, clipped by the client area */
  CHECK(nowrap.b - nowrap.t <= 16);
  CHECK(simple.b - simple.t <= 16);
  CHECK_EQ(bbox(170, 10, 200, 70).n, 0); /* nothing leaked to the right of the narrow control */
  end();
}

static void test_prefix(void) {
  BBox with, without;
  begin();
  S("A", "&Hello", SS_LEFT, 10, 10, 150, 20, 41);
  S("B", "&Hello", SS_LEFT | SS_NOPREFIX, 10, 40, 150, 20, 42);
  pump();
  with = bbox(10, 10, 160, 30);
  without = bbox(10, 40, 160, 60);
  /* without SS_NOPREFIX the '&' is consumed (it underlines the next letter); with SS_NOPREFIX it is drawn */
  CHECK(without.r - without.l > with.r - with.l + 3);
  /* the underlined letter has an extra row of text-coloured pixels below the baseline */
  CHECK(with.b - 10 > without.b - 40);
  {
    int run = 0, best = 0;
    for (int x = 10; x < 160; x++) {
      run = pc(x, with.b) == BLACK ? run + 1 : 0;
      if (run > best) best = run;
    }
    CHECK(best >= 6); /* the underline: a solid run as wide as the letter */
  }
  end();
}

static void test_rects_and_frames(void) {
  begin();
  underlay(0, 0, 150, 100);
  S("a", "", SS_BLACKRECT, 10, 10, 40, 20, 51);
  S("b", "", SS_GRAYRECT, 10, 40, 40, 20, 52);
  S("c", "", SS_WHITERECT, 10, 70, 40, 20, 53);
  S("d", "", SS_BLACKFRAME, 100, 10, 40, 20, 54);
  S("e", "", SS_GRAYFRAME, 100, 40, 40, 20, 55);
  S("f", "", SS_WHITEFRAME, 100, 70, 40, 20, 56);
  pump();
  PIXEL_TEST("ctl_static_rects");
  /* filled: the whole client area, corners included */
  PIXEL_EQ(10, 10, BLACK);
  PIXEL_EQ(10 + 39, 10 + 19, BLACK);
  PIXEL_EQ(10 + 20, 10 + 10, BLACK);
  PIXEL_EQ(10, 40, DESK);          /* COLOR_BACKGROUND (equal to COLOR_BTNFACE in this scheme) */
  PIXEL_NE(10, 40, WHITE_);
  PIXEL_EQ(10 + 20, 40 + 10, DESK);
  PIXEL_EQ(10 + 39, 40 + 19, DESK);
  PIXEL_EQ(10, 70, WHITE_);        /* COLOR_WINDOW */
  PIXEL_EQ(10 + 20, 70 + 10, WHITE_);
  PIXEL_EQ(10 + 39, 70 + 19, WHITE_);
  /* frames: a one pixel outline, interior untouched (the background brush) */
  PIXEL_TEST("ctl_static_frames");
  PIXEL_EQ(100, 10, BLACK);
  PIXEL_EQ(100 + 39, 10, BLACK);
  PIXEL_EQ(100, 10 + 19, BLACK);
  PIXEL_EQ(100 + 20, 10, BLACK);
  PIXEL_EQ(100 + 20, 10 + 10, FACE);
  PIXEL_EQ(100 + 1, 10 + 1, FACE);
  PIXEL_EQ(100, 40, DESK);
  PIXEL_EQ(100 + 39, 40 + 19, DESK);
  PIXEL_EQ(100 + 20, 40 + 10, FACE);
  PIXEL_EQ(100, 70, WHITE_);
  PIXEL_EQ(100 + 20, 70 + 19, WHITE_);
  PIXEL_EQ(100 + 20, 70 + 10, FACE);
  dump_frame("ctl_static_rects", PX0 + 0, PY0 + 0, 150, 100);
  end();
}

static void test_icon(void) {
  HWND16 s;
  uint16_t ic;
  begin();
  underlay(0, 0, 80, 60);
  s = S("I", "", SS_ICON, 10, 10, 32, 32, 61);
  ic = LoadIcon(0, 32512 /* IDI_APPLICATION */);
  CHECK(ic != 0);
  pump();
  PIXEL_TEST("ctl_static_icon");
  CHECK_EQ(SendMessage(s, STM_GETICON, 0, 0), 0);
  CHECK_EQ(count_in(10, 10, 42, 42, FACE), 32 * 32); /* nothing yet */
  CHECK_EQ(SendMessage(s, STM_SETICON, ic, 0), 0);    /* returns the previous icon */
  CHECK_EQ(SendMessage(s, STM_GETICON, 0, 0), ic);
  pump();
  CHECK(count_in(10, 10, 42, 42, FACE) < 32 * 32 - 30); /* the icon is drawn into the control */
  CHECK_EQ(count_in(42, 10, 60, 42, WHITE_), 18 * 32); /* and not past it */
  CHECK_EQ(SendMessage(s, STM_SETICON, 0, 0), ic);
  pump();
  CHECK_EQ(count_in(10, 10, 42, 42, FACE), 32 * 32);  /* removed: the background again */
  end();
}

/* SS_ICON with window text: the icon is the resource of that name in the window's hInstance
 * (the game's dialogs 90 "StarsIco" and 115 "HostIco" never send STM_SETICON). Wine static.c
 * WM_CREATE loads it via STATIC_LoadIconW; the control size is left as the template gives. */
static uint8_t *g_img;
static HWND16 mk_icon(const char *text, uint16_t hinst, int x, int y, int w, int h, uint16_t id) {
  HWND16 s = CreateWindow(fstr("STATIC"), fstr(text), WS_CHILD | WS_VISIBLE | SS_ICON, (int16_t)x, (int16_t)y,
                          (int16_t)w, (int16_t)h, g_top, id, hinst, 0);
  CHECK(s != 0);
  return s;
}
static void test_icon_from_text(void) {
  HWND16 s;
  uint16_t ic;
  begin();
  underlay(0, 0, 80, 60);
  s = mk_icon("StarsIco", MEM_SEL_DGROUP, 10, 10, 32, 32, 62);
  pump();
  ic = (uint16_t)SendMessage(s, STM_GETICON, 0, 0);
  CHECK(ic != 0);
  CHECK_EQ(ic, LoadIcon(MEM_SEL_DGROUP, fstr("StarsIco")));
  CHECK(count_in(10, 10, 42, 42, FACE) < 32 * 32 - 30); /* opaque pixels are drawn */
  CHECK_EQ(count_in(42, 10, 60, 42, WHITE_), 18 * 32);
  DestroyWindow(s);
  /* a name that is no resource: icon 0, no crash, nothing drawn */
  s = mk_icon("NoSuchIcon", MEM_SEL_DGROUP, 10, 10, 32, 32, 63);
  pump();
  CHECK_EQ(SendMessage(s, STM_GETICON, 0, 0), 0);
  CHECK_EQ(count_in(10, 10, 42, 42, FACE), 32 * 32);
  end();
}

/* ---- notification and mouse behaviour ---- */

static void test_transparent_and_notify(void) {
  HWND16 plain, notify, under;
  begin();
  /* a static over a button: without SS_NOTIFY the click goes to the button below */
  under = B("PB", "Go", BS_PUSHBUTTON, 10, 10, 80, 30, 71);
  plain = S("S", "", SS_LEFT, 10, 10, 80, 30, 72);
  CHECK_EQ((int16_t)SendMessage(plain, WM_NCHITTEST, 0, 0), HTTRANSPARENT);
  click_at(30, 20);
  CHECK_EQ(ncmd(), 1);
  CHECK(cmd(0) != NULL && cmd(0)->wp == 71);
  /* with SS_NOTIFY: STN_CLICKED to the parent, Win16 packing */
  log_clear();
  notify = S("N", "Click me", SS_LEFT | SS_NOTIFY, 150, 10, 80, 30, 73);
  CHECK((int16_t)SendMessage(notify, WM_NCHITTEST, 0, 0) != HTTRANSPARENT);
  click_at(160, 20);
  CHECK_EQ(ncmd(), 1);
  if (ncmd() == 1) {
    CHECK_EQ(cmd(0)->wp, 73);
    CHECK_EQ(cmd(0)->lp & 0xFFFF, notify);
    CHECK_EQ(cmd(0)->lp >> 16, STN_CLICKED);
  }
  log_clear();
  dblclick_at(160, 20);
  {
    int dbl = 0;
    for (int i = 0; i < ncmd(); i++)
      if ((cmd(i)->lp >> 16) == STN_DBLCLK && (cmd(i)->lp & 0xFFFF) == notify && cmd(i)->wp == 73) dbl = 1;
    CHECK(dbl);
  }
  /* WM_ENABLE: STN_DISABLE / STN_ENABLE to the parent (static.c 400-407) */
  log_clear();
  EnableWindow(notify, 0);
  CHECK_EQ(ncmd(), 1);
  if (ncmd() == 1) {
    CHECK_EQ(cmd(0)->wp, 73);
    CHECK_EQ(cmd(0)->lp & 0xFFFF, notify);
    CHECK_EQ(cmd(0)->lp >> 16, STN_DISABLE);
  }
  log_clear();
  EnableWindow(notify, 1);
  CHECK_EQ(ncmd(), 1);
  if (ncmd() == 1) CHECK_EQ(cmd(0)->lp >> 16, STN_ENABLE);
  (void)under;
  end();
}

/* ---- WM_CTLCOLOR: the parent supplies the brush ---- */

static struct {
  int n;
  uint16_t wp;
  uint32_t lp;
  uint16_t brush;
} g_cc;
static int hook_ctlcolor(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)h;
  if (msg != WM_CTLCOLOR) return 0;
  g_cc.n++;
  g_cc.wp = wp;
  g_cc.lp = lp;
  SetBkColor(wp, GetSysColor(16));
  g_hook_ret = g_cc.brush;
  return 1;
}

static void test_ctlcolor_from_parent(void) {
  HWND16 s;
  begin();
  g_cc.n = 0;
  g_cc.brush = CreateSolidBrush(GetSysColor(16)); /* COLOR_BTNSHADOW */
  g_hook = hook_ctlcolor;
  s = S("S", "", SS_LEFT, 10, 10, 60, 20, 81);
  pump();
  CHECK(g_cc.n >= 1);
  CHECK(g_cc.wp != 0);                        /* wParam = the control's device context */
  CHECK_EQ(g_cc.lp & 0xFFFF, s);              /* LOWORD(lParam) = the control */
  CHECK_EQ(g_cc.lp >> 16, CTLCOLOR_STATIC);   /* HIWORD(lParam) = type (Win16 WM_CTLCOLOR) */
  PIXEL_TEST("ctl_static_parent_brush");
  PIXEL_EQ(10, 10, SHADOW);                   /* painted with the brush the parent returned */
  PIXEL_EQ(10 + 59, 10 + 19, SHADOW);
  g_hook = NULL;
  DeleteObject(g_cc.brush);
  end();
}

static void test_subclass(void) {
  HWND16 s;
  FARPTR old;
  begin();
  s = S("S", "Hello", SS_LEFT, 10, 10, 100, 20, 91);
  old = SetWindowLong(s, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_STATIC);
  g_old = old;
  log_clear();
  SendMessage(s, WM_SETTEXT, 0, fstr("Other"));
  CHECK(log_find("S", WM_SETTEXT, 0) != NULL);
  CHECK_EQ(log_find("S", WM_SETTEXT, 0)->tag, 'B');
  {
    FARPTR p = far_alloc(16);
    CHECK_EQ(GetWindowText(s, p, 16), 5);
    CHECK_EQ(strcmp(LIN(p), "Other"), 0);
  }
  SetWindowLong(s, GWL_WNDPROC, old);
  end();
}

int main(void) {
  (void)dump;
  if (fixture_open() != 0) return 1;
  {
    FILE *fp = fopen(EXE, "rb");
    long len;
    if (!fp) return 1;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    g_img = malloc((size_t)len);
    if (!g_img || fread(g_img, 1, (size_t)len, fp) != (size_t)len) return 1;
    fclose(fp);
    CHECK_EQ(kernel_load(g_img, (size_t)len), 0);
  }
  g_msg = far_alloc(sizeof(MSG16));
  CHECK(register_class(PCLS, FP_PROC_A, 0, 0) != 0);
  test_constants();
  test_class_and_text();
  test_font_message();
  test_text_alignment();
  test_wrap_and_nowrap();
  test_prefix();
  test_rects_and_frames();
  test_icon();
  test_icon_from_text();
  test_transparent_and_notify();
  test_ctlcolor_from_parent();
  test_subclass();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
