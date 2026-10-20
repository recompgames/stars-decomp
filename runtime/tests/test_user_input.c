/* Expectations for USER part 2: input, focus, activation, capture, timers, cursors, system
 * metrics and colours, rectangle helpers (runtime/user_input.c, user_misc.c), issue stars-0z4.7.
 * Written before the implementation. Do not edit an expectation to match the code: a
 * disagreement is reported with both values.
 *
 * Window procedures are C functions in the diff-test registry (user_test_common.h). Messages
 * are logged as "<label>:<NAME>" in delivery order.
 *
 * Sources. The Windows 3.1 SDK text is NOT available on this machine and tests/ref/ holds no
 * reference capture, so NOTHING below is confirmed against Windows 3.1. Every rule cites Wine
 * (WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0) or the issue text;
 * a rule with neither is this runtime's own choice. All of them are UNVERIFIED for Windows 3.1
 * and are listed again in docs/inventory.md section 13.
 *
 *   W-hw        server/queue.c get_message 3336-3379: posted messages, WM_QUIT, hardware input,
 *               WM_PAINT, timers, in that order (hardware input = mouse and keyboard).
 *   W-hit       dlls/win32u/window.c window_from_point 2850-2902: a disabled window gets hittest
 *               HTERROR; otherwise WM_NCHITTEST (wParam 0, lParam = screen point) decides.
 *   W-mouse     dlls/win32u/message.c process_mouse_message 2598-2870: capture window and
 *               HTCLIENT when a capture is set (2612-2617); hittest other than HTCLIENT turns
 *               WM_xBUTTONyyy into WM_NCxBUTTONyyy with wParam = hittest and a screen-coordinate
 *               lParam (2724-2728); client lParam is client-relative (2729-2735); HTERROR and
 *               HTNOWHERE send WM_SETCURSOR and eat the message (2800-2804); WM_SETCURSOR
 *               (wParam hwnd, LOWORD(lParam) hittest, HIWORD the mouse message) is sent when the
 *               message is removed (2860-2864); PM_NOREMOVE (and a captured mouse) sends no WM_SETCURSOR and
 *               PM_NOREMOVE keeps the double-click state (2745, 2809-2812).
 *   W-dbl       message.c 2745-2775: a button-down becomes the DBLCLK message when the window
 *               class has CS_DBLCLKS (or the hittest is not HTCLIENT), the message, window and
 *               button equal the previous click's, the time difference is < the double-click
 *               time (500 ms here) and |dx| < SM_CXDOUBLECLK/2, |dy| < SM_CYDOUBLECLK/2 (strict).
 *               A DBLCLK clears the stored click (no triple click).
 *   W-key       server/queue.c queue_keyboard_message 2319-2400: WM_SYSKEYDOWN/UP for Alt (unless
 *               Ctrl is down), for any key while Alt is down and Ctrl is not, and for F10;
 *               Alt released with no key in between is WM_SYSKEYUP, otherwise WM_KEYUP.
 *               find_hardware_message_window 1761-1790: keyboard input goes to the focus
 *               window, else to the active window as the SYS message; no window: dropped.
 *   W-xlate     dlls/win32u/message.c NtUserTranslateMessage 4830-4880: WM_KEYDOWN ->
 *               WM_CHAR, WM_SYSKEYDOWN -> WM_SYSCHAR, posted (PostMessage) with the keydown's
 *               lParam; key-up messages are not translated.
 *   W-focus     dlls/win32u/input.c NtUserSetFocus 2266-2320, set_focus_window 2077-2114
 *               (WM_KILLFOCUS wParam = new window, then WM_SETFOCUS wParam = old window),
 *               set_active_window 2120-2230 (WM_NCACTIVATE(FALSE) and WM_ACTIVATE to the old
 *               window, WM_NCACTIVATE(TRUE) and WM_ACTIVATE to the new one, then focus moves to
 *               the new window if the focus is not already inside it), NtUserSetActiveWindow
 *               2236-2262 (a child window: no change, returns the active window).
 *   W-destroy   win32u/window.c send_destroy_message 5398-5410 and activate_other_window
 *               4508-4540: destroying the active window activates the next window below it
 *               in z-order that is visible and enabled; EnableWindow(FALSE) on the focus window
 *               moves the focus to 0 between WM_CANCELMODE and WM_ENABLE (window.c 1150-1161).
 *   W-setcursor dlls/win32u/defwnd.c handle_set_cursor 436-483 and the WM_SETCURSOR case
 *               2690-2703 (a child window offers the message to its parent first).
 *   W-timer     dlls/user.exe16/window.c SetTimer16 101, KillTimer16 121; server/queue.c
 *               get_message 3370-3379 (timers last, a removed timer is re-armed).
 *   W-rect      dlls/user.exe16/user.c PtInRect16 667, OffsetRect16 677, InflateRect16 689,
 *               IntersectRect16 701, UnionRect16 722, FillRect16 756, FrameRect16 785.
 *
 * Limitations that tests record rather than hide:
 *   - CLOCK RESOLUTION. GetTickCount and the timers read the runtime clock (rt_clock_get,
 *     runtime/vfs.h), which has ONE-SECOND resolution: GetTickCount is always a multiple of
 *     1000 and a timer can fire at most once per clock second. Windows 3.1 ticks at about
 *     55 ms (18.2 Hz). No finer timing is pretended: test_timers shows a 100 ms timer firing
 *     only when the second changes and a 1500 ms timer firing two seconds after it is set.
 *   - Not implemented (the dialog / look tasks own them): WM_MOUSEACTIVATE and activation by
 *     clicking, WM_PARENTNOTIFY on button-down, z-order
 *     change on activation, moving activation when the active window is hidden.
 */
#include "test.h"

#include "user_test_common.h"
#include "runtime/user_internal.h"
#include "runtime/vfs.h"

#define CLS "TESTCLS"
#define CLS_DBL "TESTDBL"
#define CLS_IB "TESTIB"
/* stars-0z4.9: these tests pin message order and plain geometry, so their top-level windows carry no
 * caption, frame or scroll bars (WS_OVERLAPPED = 0). WS_OVERLAPPEDWINDOW windows now have a non-client
 * area; that is covered by test_user_nc.c and test_visual_chrome.c. */
#define TOP (WS_OVERLAPPED | WS_VISIBLE)
#define HID WS_OVERLAPPED

/* Win16 / USER constants used by the tests, written out so a wrong header value is caught. */
#define T_WM_NCMOUSEMOVE 0x00A0
#define T_WM_NCLBUTTONDOWN 0x00A1
#define T_WM_NCLBUTTONUP 0x00A2
#define T_WM_NCLBUTTONDBLCLK 0x00A3
#define T_WM_KEYDOWN 0x0100
#define T_WM_KEYUP 0x0101
#define T_WM_CHAR 0x0102
#define T_WM_SYSKEYDOWN 0x0104
#define T_WM_SYSKEYUP 0x0105
#define T_WM_SYSCHAR 0x0106
#define T_WM_MOUSEMOVE 0x0200
#define T_WM_LBUTTONDOWN 0x0201
#define T_WM_LBUTTONUP 0x0202
#define T_WM_LBUTTONDBLCLK 0x0203
#define T_WM_RBUTTONDOWN 0x0204
#define T_WM_RBUTTONUP 0x0205
#define T_WM_MBUTTONDOWN 0x0207
#define T_WM_MBUTTONUP 0x0208

static uint32_t make_long_(uint16_t lo, uint16_t hi) { return (uint32_t)lo | ((uint32_t)hi << 16); }
/* MAKEINTRESOURCE: a far pointer with a zero selector */
#define MAKE_INTRES(i) ((FARPTR)(uint16_t)(i))

static FARPTR g_msgp;
static MSG16 *M(void) { return LIN(g_msgp); }

static int pix(int x, int y) { return fb_pixels()[y * fb_width() + x]; }
static void screen_white(void) {
  HDC16 dc = GetDC(0);
  PatBlt(dc, 0, 0, GDI_SCREEN_W, GDI_SCREEN_H, WHITENESS);
  ReleaseDC(0, dc);
}

/* ---- input helpers ---- */

static uint32_t g_t = 10000; /* host time, ms */

/* Next input message (non-client mouse 0x00A0 .. last mouse 0x0209, including keys and
 * WM_TIMER), removed. Returns 0 when there is none. */
static int get_in(MSG16 *o) {
  if (!PeekMessage(g_msgp, 0, 0x00A0, 0x0209, PM_REMOVE)) return 0;
  *o = *M();
  return 1;
}
/* Same, skipping mouse moves. */
static int get_btn(MSG16 *o) {
  while (get_in(o))
    if (o->message != T_WM_MOUSEMOVE && o->message != T_WM_NCMOUSEMOVE) return 1;
  return 0;
}
static void drain(void) {
  MSG16 m;
  while (get_in(&m)) {}
}
static void mouse(int x, int y, int b, uint32_t t) { rt_input_mouse((int16_t)x, (int16_t)y, (uint16_t)b, t); }
static void key(unsigned vk, unsigned sc, int down) {
  g_t += 5;
  rt_input_key((uint16_t)vk, (uint16_t)sc, down, g_t);
}
/* Parks the pointer over nothing and forgets pending input. */
static void reset_input(void) {
  g_t += 1000;
  mouse(790, 590, 0, g_t);
  drain();
}

#define EXPECT_MSG(m, h, msg_, wp_, lp_)                                         \
  do {                                                                           \
    CHECK_EQ((m).hwnd, (h));                                                     \
    CHECK_EQ((m).message, (msg_));                                               \
    CHECK_EQ((m).wParam, (wp_));                                                 \
    CHECK_EQ((m).lParam, (lp_));                                                 \
  } while (0)

/* ================= rectangles (W-rect) ================= */

static FARPTR rect(int l, int t, int r, int b) {
  FARPTR p = far_alloc(sizeof(RECT16));
  SetRect(p, (int16_t)l, (int16_t)t, (int16_t)r, (int16_t)b);
  return p;
}
static RECT16 R_(FARPTR p) { return *(RECT16 *)LIN(p); }

static void test_rects(void) {
  FARPTR a = rect(10, 20, 30, 40), b = far_alloc(8), d = far_alloc(8);
  RECT16 r = R_(a);
  CHECK_EQ(r.left, 10);
  CHECK_EQ(r.top, 20);
  CHECK_EQ(r.right, 30);
  CHECK_EQ(r.bottom, 40);
  CHECK_EQ(IsRectEmpty(a), 0);
  SetRect(b, 5, 5, 5, 9);
  CHECK_EQ(IsRectEmpty(b), 1); /* zero width */
  SetRect(b, 9, 9, 3, 3);
  CHECK_EQ(IsRectEmpty(b), 1); /* inverted */
  SetRectEmpty(b);
  CHECK_RECT(R_(b), 0, 0, 0, 0);
  CHECK(CopyRect(d, a) != 0);
  CHECK_RECT(R_(d), 10, 20, 30, 40);
  CHECK_EQ(EqualRect(a, d), 1);
  SetRect(d, 10, 20, 30, 41);
  CHECK_EQ(EqualRect(a, d), 0);

  /* PtInRect: left and top edges are inside, right and bottom edges are outside. */
  CHECK_EQ(PtInRect(a, pt32(10, 20)), 1);
  CHECK_EQ(PtInRect(a, pt32(29, 39)), 1);
  CHECK_EQ(PtInRect(a, pt32(30, 25)), 0); /* right edge */
  CHECK_EQ(PtInRect(a, pt32(15, 40)), 0); /* bottom edge */
  CHECK_EQ(PtInRect(a, pt32(9, 25)), 0);
  CHECK_EQ(PtInRect(a, pt32(15, 19)), 0);
  { FARPTR n = rect(-10, -10, 0, 0); CHECK_EQ(PtInRect(n, pt32(-5, -5)), 1); CHECK_EQ(PtInRect(n, pt32(0, -5)), 0); }

  OffsetRect(a, 5, -5);
  CHECK_RECT(R_(a), 15, 15, 35, 35);
  InflateRect(a, 2, 3);
  CHECK_RECT(R_(a), 13, 12, 37, 38);
  InflateRect(a, -2, -3);
  CHECK_RECT(R_(a), 15, 15, 35, 35);

  /* IntersectRect */
  SetRect(a, 10, 20, 30, 40);
  SetRect(b, 20, 30, 50, 60);
  SetRect(d, 99, 99, 99, 99);
  CHECK_EQ(IntersectRect(d, a, b), 1);
  CHECK_RECT(R_(d), 20, 30, 30, 40);
  SetRect(b, 30, 20, 50, 40); /* touching edges: no overlap */
  SetRect(d, 99, 99, 99, 99);
  CHECK_EQ(IntersectRect(d, a, b), 0);
  CHECK_RECT(R_(d), 0, 0, 0, 0); /* the destination is emptied (W-rect 701) */
  SetRect(b, 25, 25, 25, 40); /* empty source */
  CHECK_EQ(IntersectRect(d, a, b), 0);

  /* UnionRect */
  SetRect(b, 20, 30, 50, 60);
  CHECK_EQ(UnionRect(d, a, b), 1);
  CHECK_RECT(R_(d), 10, 20, 50, 60);
  SetRect(b, 7, 7, 7, 7); /* empty: the other rectangle is the result */
  CHECK_EQ(UnionRect(d, a, b), 1);
  CHECK_RECT(R_(d), 10, 20, 30, 40);
  SetRect(a, 1, 1, 1, 1);
  CHECK_EQ(UnionRect(d, a, b), 0); /* both empty */
  CHECK_RECT(R_(d), 0, 0, 0, 0);
}

static void test_fill_frame(void) {
  HDC16 dc;
  HGDIOBJ16 br = GetStockObject(BLACK_BRUSH);
  FARPTR r = rect(10, 10, 20, 20), f = rect(30, 30, 40, 40);
  screen_white();
  dc = GetDC(0);
  CHECK(FillRect(dc, r, br) != 0);
  CHECK_EQ(pix(10, 10), 0);
  CHECK_EQ(pix(19, 19), 0);
  CHECK_EQ(pix(20, 20), 255); /* right and bottom edges are not painted */
  CHECK_EQ(pix(20, 10), 255);
  CHECK_EQ(pix(10, 20), 255);
  CHECK_EQ(pix(9, 10), 255);
  FrameRect(dc, f, br);
  CHECK_EQ(pix(30, 30), 0);
  CHECK_EQ(pix(39, 39), 0);
  CHECK_EQ(pix(30, 35), 0); /* left */
  CHECK_EQ(pix(39, 35), 0); /* right */
  CHECK_EQ(pix(35, 30), 0); /* top */
  CHECK_EQ(pix(35, 39), 0); /* bottom */
  CHECK_EQ(pix(35, 35), 255); /* interior untouched */
  CHECK_EQ(pix(40, 35), 255);
  CHECK_EQ(pix(35, 40), 255);
  ReleaseDC(0, dc);
}

/* ================= system metrics and colours ================= */

/* UNVERIFIED: the values are the Windows 3.1 defaults as remembered, adjusted to the decided
 * display mode (800x600, 256 colours: docs/PLAN.md decision 8). Neither the SDK nor a reference
 * capture is available. Sources are listed per row in docs/inventory.md section 13. */
static const struct {
  int idx;
  int value;
  const char *name;
} METRICS[] = {
    {0, GDI_SCREEN_W, "SM_CXSCREEN"}, {1, GDI_SCREEN_H, "SM_CYSCREEN"}, {2, 16, "SM_CXVSCROLL"},
    {3, 16, "SM_CYHSCROLL"}, {4, 20, "SM_CYCAPTION"}, {5, 1, "SM_CXBORDER"}, {6, 1, "SM_CYBORDER"},
    {7, 4, "SM_CXDLGFRAME"}, {8, 4, "SM_CYDLGFRAME"}, {9, 16, "SM_CYVTHUMB"}, {10, 16, "SM_CXHTHUMB"},
    {11, 32, "SM_CXICON"}, {12, 32, "SM_CYICON"}, {13, 32, "SM_CXCURSOR"}, {14, 32, "SM_CYCURSOR"},
    {15, 19, "SM_CYMENU"}, {16, GDI_SCREEN_W, "SM_CXFULLSCREEN"}, {17, GDI_SCREEN_H - 20, "SM_CYFULLSCREEN"},
    {18, 0, "SM_CYKANJIWINDOW"}, {19, 1, "SM_MOUSEPRESENT"}, {20, 16, "SM_CYVSCROLL"},
    {21, 16, "SM_CXHSCROLL"}, {22, 0, "SM_DEBUG"}, {23, 0, "SM_SWAPBUTTON"}, {28, 112, "SM_CXMIN"},
    {29, 27, "SM_CYMIN"}, {30, 18, "SM_CXSIZE"}, {31, 18, "SM_CYSIZE"}, {32, 4, "SM_CXFRAME"},
    {33, 4, "SM_CYFRAME"}, {34, 112, "SM_CXMINTRACK"}, {35, 27, "SM_CYMINTRACK"},
    {36, 4, "SM_CXDOUBLECLK"}, {37, 4, "SM_CYDOUBLECLK"}, {38, 75, "SM_CXICONSPACING"},
    {39, 75, "SM_CYICONSPACING"}, {40, 0, "SM_MENUDROPALIGNMENT"}, {41, 0, "SM_PENWINDOWS"},
    {42, 0, "SM_DBCSENABLED"}, {43, 2, "SM_CMOUSEBUTTONS"}, {44, 0, "SM_SECURE"},
};

static void test_metrics_colors(void) {
  for (size_t i = 0; i < sizeof METRICS / sizeof METRICS[0]; i++) {
    int16_t v = GetSystemMetrics((int16_t)METRICS[i].idx);
    if (v != METRICS[i].value)
      fprintf(stderr, "%s (%d): got %d, want %d\n", METRICS[i].name, METRICS[i].idx, v, METRICS[i].value);
    CHECK_EQ(v, METRICS[i].value);
  }
  CHECK_EQ(GetSystemMetrics(200), 0);  /* unknown index */
  CHECK_EQ(GetSystemMetrics(-1), 0);

  /* Default Windows 3.1 scheme, COLOR_xxx 0..20. COLOR_WINDOW is white: required by the issue
   * ("confirmed against a reference capture"; UNVERIFIED here, the capture does not exist). */
  CHECK_EQ(GetSysColor(5), GDI_RGB(255, 255, 255)); /* COLOR_WINDOW */
  CHECK_EQ(GetSysColor(6), GDI_RGB(0, 0, 0));       /* COLOR_WINDOWFRAME */
  CHECK_EQ(GetSysColor(8), GDI_RGB(0, 0, 0));       /* COLOR_WINDOWTEXT */
  CHECK_EQ(GetSysColor(2), GDI_RGB(0, 0, 128));     /* COLOR_ACTIVECAPTION */
  CHECK_EQ(GetSysColor(9), GDI_RGB(255, 255, 255)); /* COLOR_CAPTIONTEXT */
  CHECK_EQ(GetSysColor(13), GDI_RGB(0, 0, 128));    /* COLOR_HIGHLIGHT */
  CHECK_EQ(GetSysColor(14), GDI_RGB(255, 255, 255)); /* COLOR_HIGHLIGHTTEXT */
  CHECK_EQ(GetSysColor(15), GDI_RGB(192, 192, 192)); /* COLOR_BTNFACE */
  CHECK_EQ(GetSysColor(16), GDI_RGB(128, 128, 128)); /* COLOR_BTNSHADOW */
  CHECK_EQ(GetSysColor(18), GDI_RGB(0, 0, 0));       /* COLOR_BTNTEXT */
  CHECK_EQ(GetSysColor(20), GDI_RGB(255, 255, 255)); /* COLOR_BTNHIGHLIGHT */
  CHECK_EQ(GetSysColor(21), 0);                      /* out of range */
  CHECK_EQ(GetSysColor(0xFFFF), 0);
}

/* ================= clock and timers (W-timer) ================= */

static void clock_s(int day, int h, int m, int s) { rt_clock_set(1980, 1, day, h, m, s); }

/* GetTickCount is boot-relative: 0 when user_init ran, then the elapsed runtime-clock time
 * (one-second resolution), whatever the calendar date. */
static void test_tick(void) {
  /* user_init calls user_input_init, which fixes the origin; re-run it under other dates */
  rt_clock_set(2031, 6, 15, 12, 0, 0);
  user_input_init();
  CHECK(GetTickCount() <= 1000);
  CHECK_EQ(GetTickCount(), 0);
  rt_clock_set(2031, 6, 15, 12, 0, 5);
  CHECK_EQ(GetTickCount(), 5000); /* one-second resolution: always a multiple of 1000 */
  CHECK_EQ(GetCurrentTime(), 5000);
  rt_clock_set(2031, 6, 15, 13, 2, 3);
  CHECK_EQ(GetTickCount(), 3723000);
  rt_clock_set(2031, 6, 16, 12, 0, 0);
  CHECK_EQ(GetTickCount(), 86400000);
  /* another date at init gives the same start value, even one whose milliseconds since 1980
   * would be past the sign bit (2030: about 1.6e12 ms, modulo 2^32 arbitrary) */
  rt_clock_set(2030, 1, 1, 0, 0, 0);
  user_input_init();
  CHECK_EQ(GetTickCount(), 0);
  rt_clock_set(2030, 1, 1, 0, 0, 7);
  CHECK_EQ(GetTickCount(), 7000);
  /* later tests use the 1980 epoch with the origin at 1980-01-01 00:00:00 */
  rt_clock_set(1980, 1, 1, 0, 0, 0);
  user_input_init();
  CHECK_EQ(GetTickCount(), 0);
}

static int g_adv_calls;
static bool advance_hook(void) {
  if (g_adv_calls++ > 0) return false;
  clock_s(1, 0, 0, 1);
  return true;
}

static void test_timers(void) {
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 50, 50, 0, 0);
  HWND16 w2 = mk("X", CLS, "x", HID, 60, 0, 50, 50, 0, 0);
  MSG16 m;
  clock_s(1, 0, 0, 0);
  CHECK_EQ(SetTimer(w, 7, 100, 0), 7);
  /* A 100 ms timer is not due until the one-second clock changes (resolution limitation). */
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);
  clock_s(1, 0, 0, 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  m = *M();
  CHECK_EQ(m.hwnd, w);
  CHECK_EQ(m.message, WM_TIMER);
  CHECK_EQ(m.wParam, 7);
  CHECK_EQ(m.lParam, 0);
  CHECK_EQ(m.time, 1000);
  /* re-armed: not due again within the same second */
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);
  clock_s(1, 0, 0, 2);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(M()->time, 2000);
  /* a missed period is not made up: one message per check */
  clock_s(1, 0, 0, 9);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);

  /* PM_NOREMOVE does not consume or re-arm */
  clock_s(1, 0, 0, 10);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_NOREMOVE), 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_NOREMOVE), 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);

  /* window filter */
  clock_s(1, 0, 0, 11);
  CHECK_EQ(PeekMessage(g_msgp, w2, WM_TIMER, WM_TIMER, PM_REMOVE), 0);
  CHECK_EQ(PeekMessage(g_msgp, w, WM_TIMER, WM_TIMER, PM_REMOVE), 1);

  /* KillTimer */
  CHECK_EQ(KillTimer(w, 7), 1);
  CHECK_EQ(KillTimer(w, 7), 0);
  CHECK_EQ(KillTimer(w, 99), 0);
  clock_s(1, 0, 0, 12);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);

  /* a 1500 ms timer set at second 12 fires at second 14, not 13 (due = 13500), then re-arms */
  CHECK_EQ(SetTimer(w, 8, 1500, 0), 8);
  clock_s(1, 0, 0, 13);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);
  clock_s(1, 0, 0, 14);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(M()->wParam, 8);
  clock_s(1, 0, 0, 15);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0); /* due 15500 */
  clock_s(1, 0, 0, 16);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  KillTimer(w, 8);

  /* SetTimer on an existing (window, id) replaces it */
  CHECK_EQ(SetTimer(w, 3, 100, 0), 3);
  CHECK_EQ(SetTimer(w, 3, 5000, 0), 3);
  clock_s(1, 0, 0, 17);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);
  clock_s(1, 0, 0, 22);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  KillTimer(w, 3);

  /* a window handle that does not exist */
  CHECK_EQ(SetTimer(0x7770, 1, 100, 0), 0);

  /* hwnd 0: the runtime picks the id; the message has hwnd 0 */
  {
    uint16_t id = SetTimer(0, 0, 100, 0);
    CHECK(id != 0);
    clock_s(1, 0, 0, 23);
    CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
    CHECK_EQ(M()->hwnd, 0);
    CHECK_EQ(M()->wParam, id);
    CHECK_EQ(KillTimer(0, id), 1);
  }

  /* timer callback: lParam carries the procedure; DispatchMessage calls it with the tick */
  CHECK_EQ(SetTimer(w, 9, 100, FP_PROC_C), 9);
  clock_s(1, 0, 0, 24);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(M()->lParam, FP_PROC_C);
  log_clear();
  DispatchMessage(g_msgp);
  CHECK_LOG("C/W:TIMER");
  {
    LogEnt *e = log_find("W", WM_TIMER, 0);
    if (e) {
      CHECK_EQ(e->wp, 9);
      CHECK_EQ(e->lp, GetTickCount()); /* GetTickCount() at dispatch (W-dispatch) */
    }
  }
  KillTimer(w, 9);

  /* timers die with their window */
  CHECK_EQ(SetTimer(w2, 4, 100, 0), 4);
  DestroyWindow(w2);
  clock_s(1, 0, 0, 25);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 0);

  /* GetMessage: the host's wait hook may advance the clock; the timer then arrives */
  clock_s(1, 0, 0, 0);
  CHECK_EQ(SetTimer(w, 5, 100, 0), 5);
  g_adv_calls = 0;
  user_wait_hook = advance_hook;
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 1);
  CHECK_EQ(M()->message, WM_TIMER);
  CHECK_EQ(M()->wParam, 5);
  user_wait_hook = NULL;
  KillTimer(w, 5);

  /* the table is finite (32 timers: Windows 3.1's system limit, UNVERIFIED) */
  {
    uint16_t ids[40];
    int ok = 0;
    for (int i = 0; i < 33; i++) {
      ids[i] = SetTimer(0, 0, 1000, 0);
      if (ids[i]) ok++;
    }
    CHECK_EQ(ok, 32);
    CHECK_EQ(ids[32], 0);
    for (int i = 0; i < 32; i++) KillTimer(0, ids[i]);
  }
  DestroyWindow(w);
}

static void test_timer_priority(void) {
  /* W-hw: posted messages, then WM_PAINT, then timers. */
  HWND16 v = mk("V", CLS, "v", TOP, 0, 0, 40, 40, 0, 0);
  clock_s(1, 0, 0, 0);
  SetTimer(v, 1, 100, 0);
  clock_s(1, 0, 0, 1);
  PostMessage(v, WM_USER, 1, 0);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(M()->message, WM_USER);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_NOREMOVE), 1);
  CHECK_EQ(M()->message, WM_PAINT); /* before the due timer */
  UpdateWindow(v);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(M()->message, WM_TIMER);
  KillTimer(v, 1);
  DestroyWindow(v);
}

/* ================= focus and activation (W-focus, W-destroy) ================= */

/* ShowWindow / SetWindowPos activate a top-level window that becomes visible (window.c
 * show_window ~4190 -> set_active_window(hwnd, NULL, FALSE, TRUE); order UNVERIFIED for 3.1).
 * The game's startup only calls CreateWindow, LoadAccelerators and ShowWindow. */
static void test_auto_activation(void) {
  HWND16 a, b, c;
  MSG16 m;
  SetActiveWindow(0);
  SetFocus(0);
  reset_input();
  a = mk("A", CLS, "a", HID, 0, 0, 100, 100, 0, 0);
  CHECK_EQ(GetActiveWindow(), 0);
  log_clear();
  ShowWindow(a, SW_SHOW);
  CHECK_EQ(GetActiveWindow(), a);
  CHECK_EQ(GetFocus(), a);
  CHECK(log_find("A", WM_NCACTIVATE, 0) != NULL);
  CHECK(log_find("A", WM_ACTIVATE, 0) != NULL);
  CHECK(log_find("A", WM_SETFOCUS, 0) != NULL);
  /* a keyboard event now reaches the window instead of being dropped */
  key(0x41, 0x1E, 1);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, a, T_WM_KEYDOWN, 0x41, 0x001E0001u);
  key(0x41, 0x1E, 0);
  drain();

  /* no activation: SW_SHOWNA, SW_SHOWNOACTIVATE, SWP_NOACTIVATE, hidden windows, children */
  b = mk("B", CLS, "b", HID, 120, 0, 100, 100, 0, 0);
  ShowWindow(b, SW_SHOWNA);
  CHECK_EQ(GetActiveWindow(), a);
  ShowWindow(b, SW_HIDE);
  ShowWindow(b, SW_SHOWNOACTIVATE);
  CHECK_EQ(GetActiveWindow(), a);
  ShowWindow(b, SW_HIDE);
  CHECK_EQ(SetWindowPos(b, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE), 1);
  CHECK_EQ(GetActiveWindow(), a);
  ShowWindow(b, SW_HIDE);
  c = mk("C", CLS, "c", WS_CHILD, 10, 10, 30, 30, a, 1);
  ShowWindow(c, SW_SHOW);
  CHECK_EQ(GetActiveWindow(), a);
  /* SetWindowPos with SWP_SHOWWINDOW activates */
  CHECK_EQ(SetWindowPos(b, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW), 1);
  CHECK_EQ(GetActiveWindow(), b);
  CHECK_EQ(GetFocus(), b);
  /* showing an already visible window does not move the activation */
  SetActiveWindow(a);
  ShowWindow(b, SW_SHOW);
  CHECK_EQ(GetActiveWindow(), a);
  /* a DISABLED visible-to-be window shown with SW_SHOW is not activated (can_activate) */
  ShowWindow(b, SW_HIDE);
  SetActiveWindow(a);
  EnableWindow(b, 0);
  ShowWindow(b, SW_SHOW);
  CHECK_EQ(GetActiveWindow(), a);
  CHECK_EQ(IsWindowVisible(b), 1);
  EnableWindow(b, 1);
  /* SetWindowPos on a HIDDEN window (move only, no show) does not activate */
  ShowWindow(b, SW_HIDE);
  CHECK_EQ(SetWindowPos(b, 0, 50, 50, 0, 0, SWP_NOSIZE | SWP_NOZORDER), 1);
  CHECK_EQ(IsWindowVisible(b), 0);
  CHECK_EQ(GetActiveWindow(), a);
  /* CreateWindow with WS_VISIBLE shows with SW_SHOW and so activates */
  c = mk("D", CLS, "d", TOP, 0, 100, 50, 50, 0, 0);
  CHECK_EQ(GetActiveWindow(), c);
  DestroyWindow(c);
  DestroyWindow(b);
  DestroyWindow(a);
  SetActiveWindow(0);
  SetFocus(0);
  reset_input();
}

static void test_activation_focus(void) {
  HWND16 a, b, c;
  a = mk("A", CLS, "a", TOP, 0, 0, 100, 100, 0, 0);
  b = mk("B", CLS, "b", TOP, 120, 0, 100, 100, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 10, 10, 30, 30, a, 1);
  /* creating visible top-level windows activated them (test_auto_activation): start clean */
  SetActiveWindow(0);
  SetFocus(0);
  CHECK_EQ(GetActiveWindow(), 0);
  CHECK_EQ(GetFocus(), 0);

  /* 1. first activation: focus follows into the window */
  log_clear();
  CHECK_EQ(SetActiveWindow(a), 0);
  CHECK_LOG("A:NCACTIVATE A:ACTIVATE A:SETFOCUS");
  { LogEnt *e = log_find("A", WM_NCACTIVATE, 0); if (e) { CHECK_EQ(e->wp, 1); CHECK_EQ(e->lp, 0); } }
  { LogEnt *e = log_find("A", WM_ACTIVATE, 0); if (e) { CHECK_EQ(e->wp, 1); CHECK_EQ(e->lp, 0); } } /* WA_ACTIVE, not minimised, no other window */
  { LogEnt *e = log_find("A", WM_SETFOCUS, 0); if (e) { CHECK_EQ(e->wp, 0); CHECK_EQ(e->lp, 0); } }
  CHECK_EQ(GetActiveWindow(), a);
  CHECK_EQ(GetFocus(), a);

  /* 2. switching */
  log_clear();
  CHECK_EQ(SetActiveWindow(b), a);
  CHECK_LOG("A:NCACTIVATE A:ACTIVATE B:NCACTIVATE B:ACTIVATE A:KILLFOCUS B:SETFOCUS");
  { LogEnt *e = log_find("A", WM_NCACTIVATE, 0); if (e) CHECK_EQ(e->wp, 0); }
  /* Win16 WM_ACTIVATE: wParam WA_INACTIVE=0 / WA_ACTIVE=1, LOWORD(lParam) minimised flag,
   * HIWORD(lParam) the other window (UNVERIFIED; Wine's Win32 layout differs). */
  { LogEnt *e = log_find("A", WM_ACTIVATE, 0); if (e) { CHECK_EQ(e->wp, 0); CHECK_EQ(e->lp, make_long_(0, b)); } }
  { LogEnt *e = log_find("B", WM_NCACTIVATE, 0); if (e) CHECK_EQ(e->wp, 1); }
  { LogEnt *e = log_find("B", WM_ACTIVATE, 0); if (e) { CHECK_EQ(e->wp, 1); CHECK_EQ(e->lp, make_long_(0, a)); } }
  { LogEnt *e = log_find("A", WM_KILLFOCUS, 0); if (e) CHECK_EQ(e->wp, b); }
  { LogEnt *e = log_find("B", WM_SETFOCUS, 0); if (e) CHECK_EQ(e->wp, a); }
  CHECK_EQ(GetActiveWindow(), b);
  CHECK_EQ(GetFocus(), b);

  /* 3. SetFocus on a child of an inactive top-level window activates the top-level first,
   * without moving the focus there (W-focus 2300-2310). */
  log_clear();
  CHECK_EQ(SetFocus(c), b);
  CHECK_LOG("B:NCACTIVATE B:ACTIVATE A:NCACTIVATE A:ACTIVATE B:KILLFOCUS C:SETFOCUS");
  { LogEnt *e = log_find("B", WM_KILLFOCUS, 0); if (e) CHECK_EQ(e->wp, c); }
  { LogEnt *e = log_find("C", WM_SETFOCUS, 0); if (e) CHECK_EQ(e->wp, b); }
  CHECK_EQ(GetActiveWindow(), a);
  CHECK_EQ(GetFocus(), c);

  /* 4. already focused: nothing happens, the previous focus is returned */
  log_clear();
  CHECK_EQ(SetFocus(c), c);
  CHECK_LOG("");

  /* 5. SetActiveWindow on a child window changes nothing and returns the active window */
  log_clear();
  CHECK_EQ(SetActiveWindow(c), a);
  CHECK_LOG("");
  CHECK_EQ(GetActiveWindow(), a);

  /* 6. SetFocus(0) */
  log_clear();
  CHECK_EQ(SetFocus(0), c);
  CHECK_LOG("C:KILLFOCUS");
  { LogEnt *e = log_find("C", WM_KILLFOCUS, 0); if (e) CHECK_EQ(e->wp, 0); }
  CHECK_EQ(GetFocus(), 0);
  log_clear();
  CHECK_EQ(SetFocus(0), 0);
  CHECK_LOG("");

  /* 7. a window that does not exist */
  CHECK_EQ(SetFocus(0x7770), 0);
  CHECK_EQ(SetActiveWindow(0x7770), 0);

  /* 8. disabled windows, and children of disabled windows, cannot take the focus */
  EnableWindow(b, 0);
  log_clear();
  CHECK_EQ(SetFocus(b), 0);
  CHECK_LOG("");
  CHECK_EQ(GetFocus(), 0);
  EnableWindow(b, 1);
  SetFocus(a);
  log_clear();
  EnableWindow(a, 0); /* W-destroy: the focus moves to 0 between CANCELMODE and ENABLE */
  CHECK_EQ(GetFocus(), 0);
  CHECK(strstr(dump(), "A:CANCELMODE A:KILLFOCUS A:ENABLE") != NULL);
  CHECK_EQ(SetFocus(c), 0); /* c is a child of the disabled window a */
  EnableWindow(a, 1);

  /* 9. destroying the active window activates the next one below it (W-destroy) and the
   * focus follows. */
  SetActiveWindow(b);
  SetFocus(b);
  log_clear();
  DestroyWindow(b);
  {
    const char *want = "B:WINDOWPOSCHANGING B:WINDOWPOSCHANGED B:NCACTIVATE B:ACTIVATE A:NCACTIVATE A:ACTIVATE B:KILLFOCUS A:SETFOCUS B:DESTROY";
    CHECK(strncmp(dump(), want, strlen(want)) == 0);
  }
  CHECK_EQ(GetActiveWindow(), a);
  CHECK_EQ(GetFocus(), a);

  /* 10. destroying the focus window (a child) hands the focus to its parent */
  SetFocus(c);
  DestroyWindow(c);
  CHECK_EQ(GetFocus(), a);

  /* 11. destroying the last window leaves nothing active or focused */
  DestroyWindow(a);
  CHECK_EQ(GetActiveWindow(), 0);
  CHECK_EQ(GetFocus(), 0);
}

/* ================= mouse (W-hit, W-mouse) ================= */

static int hit_hook_on;
static uint32_t g_hit;
static int hit_hook(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)h; (void)wp; (void)lp;
  if (msg == WM_NCHITTEST && hit_hook_on) {
    g_hook_ret = g_hit;
    return 1;
  }
  return 0;
}

static int g_beeps;
static uint16_t g_beep_type;
static void beep_hook(uint16_t type) { g_beeps++; g_beep_type = type; }

static void test_mouse_basic(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 100, 100, 200, 150, 0, 0);
  HWND16 c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 120, 10, 40, 40, w, 1); /* screen (220,110)-(260,150) */
  MSG16 m;
  reset_input();

  /* first move: hit test, then the message; PM_NOREMOVE sends WM_NCHITTEST only */
  mouse(150, 150, 0, 11000);
  log_clear();
  CHECK_EQ(PeekMessage(g_msgp, 0, 0x00A0, 0x0209, PM_NOREMOVE), 1);
  CHECK_LOG("W:NCHITTEST");
  { LogEnt *e = log_find("W", WM_NCHITTEST, 0); if (e) { CHECK_EQ(e->wp, 0); CHECK_EQ(e->lp, pt32(150, 150)); } }
  EXPECT_MSG(*M(), w, T_WM_MOUSEMOVE, 0, pt32(50, 50));
  CHECK_EQ(M()->time, 11000);
  CHECK_EQ(M()->pt.x, 150);
  CHECK_EQ(M()->pt.y, 150);
  log_clear();
  CHECK_EQ(PeekMessage(g_msgp, 0, 0x00A0, 0x0209, PM_REMOVE), 1);
  CHECK_LOG("W:NCHITTEST W:SETCURSOR");
  { LogEnt *e = log_find("W", WM_SETCURSOR, 0); if (e) { CHECK_EQ(e->wp, w); CHECK_EQ(e->lp, make_long_(HTCLIENT, T_WM_MOUSEMOVE)); } }
  CHECK_EQ(get_in(&m), 0);

  /* consecutive moves collapse into the last one */
  mouse(151, 150, 0, 11010);
  mouse(152, 150, 0, 11020);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_MOUSEMOVE, 0, pt32(52, 50));
  CHECK_EQ(m.time, 11020);
  CHECK_EQ(get_in(&m), 0);

  /* no change, no message */
  mouse(152, 150, 0, 11030);
  CHECK_EQ(get_in(&m), 0);

  /* over a child: the child, with child-client coordinates */
  mouse(230, 125, 0, 11040);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, c, T_WM_MOUSEMOVE, 0, pt32(10, 15));

  /* over the desktop: the application never sees it (UNVERIFIED) */
  mouse(700, 500, 0, 11050);
  CHECK_EQ(get_in(&m), 0);

  /* buttons: MK_ flags, coordinates relative to the window */
  mouse(150, 150, 0, 11060);
  drain();
  mouse(150, 150, 1, 11070);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONDOWN, 1 /* MK_LBUTTON */, pt32(50, 50));
  CHECK_EQ(m.time, 11070);
  mouse(150, 150, 0, 11080);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONUP, 0, pt32(50, 50));
  mouse(150, 150, 2, 11090);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_RBUTTONDOWN, 2 /* MK_RBUTTON */, pt32(50, 50));
  mouse(150, 150, 0, 11100);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_RBUTTONUP, 0, pt32(50, 50));
  mouse(150, 150, 4, 11110);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_MBUTTONDOWN, 0x10 /* MK_MBUTTON */, pt32(50, 50));
  mouse(150, 150, 0, 11120);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_MBUTTONUP, 0, pt32(50, 50));
  /* two buttons at once: left then right, and the right one carries both flags */
  mouse(150, 150, 3, 11130);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONDOWN, 1, pt32(50, 50));
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_RBUTTONDOWN, 3, pt32(50, 50));
  /* a packet with a move and a button change: the move comes first, with the buttons as they
   * were (L and R), then the release (right button up, only L left) */
  mouse(160, 150, 1, 11140);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_MOUSEMOVE, 3, pt32(60, 50));
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_RBUTTONUP, 1, pt32(60, 50));
  mouse(160, 150, 0, 11150);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONUP, 0, pt32(60, 50));

  /* Shift and Ctrl state in the flags: the key messages must be retrieved first (the key state
   * seen by the application follows the message queue) */
  SetFocus(w);
  reset_input();
  key(0x10, 0x2A, 1);
  key(0x11, 0x1D, 1);
  drain();
  mouse(150, 150, 0, 12000);
  drain();
  mouse(150, 150, 1, 12010);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONDOWN, 1 | 4 | 8, pt32(50, 50)); /* MK_LBUTTON | MK_SHIFT | MK_CONTROL */
  mouse(150, 150, 0, 12020);
  drain();
  key(0x10, 0x2A, 0);
  key(0x11, 0x1D, 0);
  drain();

  DestroyWindow(w);
}

static void test_mouse_hit_and_disabled(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 100, 100, 200, 150, 0, 0);
  MSG16 m;
  reset_input();
  g_hook = hit_hook;
  g_beeps = 0;
  user_beep_hook = beep_hook;

  /* Disabled window: hittest HTERROR, WM_SETCURSOR is sent, the message is eaten, and a
   * button-down beeps through DefWindowProc (W-hit 2874-2878; defwnd.c 436-452). */
  EnableWindow(w, 0);
  log_clear();
  mouse(150, 150, 0, 13000);
  CHECK_EQ(get_in(&m), 0);
  { LogEnt *e = log_find("W", WM_SETCURSOR, 0); CHECK(e != NULL); if (e) CHECK_EQ(e->lp, make_long_((uint16_t)HTERROR, T_WM_MOUSEMOVE)); }
  CHECK_EQ(log_find("W", WM_NCHITTEST, 0) == NULL, 1); /* a disabled window is not asked */
  CHECK_EQ(g_beeps, 0);
  mouse(150, 150, 1, 13010);
  CHECK_EQ(get_in(&m), 0);
  CHECK_EQ(g_beeps, 1);
  mouse(150, 150, 0, 13020);
  drain();
  EnableWindow(w, 1);

  /* stars-q2c: a child of a disabled window gets no mouse input either (EnableWindow: "all its
   * child windows are also disabled"). The child is itself enabled; the point lies on it. It is
   * the situation behind the scrambled serial-dialog text: the owner of a modal dialog is
   * disabled and a click on a button inside the owner used to reach the button. */
  { HWND16 kid = mk("K", CLS, "k", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, w, 7); /* screen (110,110)-(160,160) */
    EnableWindow(w, 0);
    log_clear();
    mouse(130, 130, 0, 13040);
    CHECK_EQ(get_in(&m), 0);
    CHECK_EQ(log_find("K", WM_NCHITTEST, 0) == NULL, 1); /* the child is not asked */
    CHECK_EQ(log_find("K", WM_SETCURSOR, 0) != NULL || log_find("W", WM_SETCURSOR, 0) != NULL, 1);
    mouse(130, 130, 1, 13050);
    CHECK_EQ(get_in(&m), 0); /* no WM_LBUTTONDOWN reaches the child */
    mouse(130, 130, 0, 13060);
    drain();
    EnableWindow(w, 1);
    mouse(130, 130, 1, 13070);
    CHECK_EQ(get_in(&m), 1); /* enabled again: it does */
    mouse(130, 130, 0, 13080);
    drain();
    DestroyWindow(kid);
  }

  /* HTNOWHERE (0): eaten, WM_SETCURSOR sent, no beep */
  hit_hook_on = 1;
  g_hit = HTNOWHERE;
  g_beeps = 0;
  log_clear();
  mouse(151, 150, 0, 13030);
  CHECK_EQ(get_in(&m), 0);
  { LogEnt *e = log_find("W", WM_SETCURSOR, 0); CHECK(e != NULL); if (e) CHECK_EQ(e->lp, make_long_(HTNOWHERE, T_WM_MOUSEMOVE)); }
  CHECK_EQ(g_beeps, 0);

  /* Non-client hit (HTCAPTION = 2): WM_NCxxx with wParam = hittest and a SCREEN lParam; the
   * double click needs no CS_DBLCLKS for the non-client area (W-dbl 2753). */
  g_hit = 2;
  mouse(152, 151, 0, 13040);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_NCMOUSEMOVE, 2, pt32(152, 151));
  mouse(152, 151, 1, 13050);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_NCLBUTTONDOWN, 2, pt32(152, 151));
  mouse(152, 151, 0, 13060);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_NCLBUTTONUP, 2, pt32(152, 151));
  mouse(152, 151, 1, 13100);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_NCLBUTTONDBLCLK, 2, pt32(152, 151));
  mouse(152, 151, 0, 13110);
  drain();

  /* a sizing hit selects a sizing cursor via DefWindowProc's WM_SETCURSOR (defwnd.c 455-478) */
  g_hit = 10; /* HTLEFT */
  mouse(153, 151, 0, 14000);
  drain();
  CHECK_EQ(user_current_cursor(), LoadCursor(0, MAKE_INTRES(32644))); /* IDC_SIZEWE */

  hit_hook_on = 0;
  g_hook = NULL;
  user_beep_hook = NULL;
  DestroyWindow(w);
}

static void click(int x, int y, uint32_t t_down, uint32_t t_up) {
  mouse(x, y, 1, t_down);
  mouse(x, y, 0, t_up);
}

static int seq(uint16_t *out, int max) {
  MSG16 m;
  int n = 0;
  while (n < max && get_btn(&m)) out[n++] = m.message;
  return n;
}

static void test_double_click(void) {
  HWND16 d = mk("D", CLS_DBL, "d", TOP, 100, 100, 200, 150, 0, 0);
  HWND16 p = mk("P", CLS, "p", TOP, 400, 100, 200, 150, 0, 0);
  HWND16 dc = mk("E", CLS_DBL, "e", WS_CHILD | WS_VISIBLE, 150, 0, 40, 40, d, 1);
  uint16_t s[8];
  int n;
  reset_input();
  mouse(150, 150, 0, 20000);
  drain();

  /* two clicks 100 ms apart, same point, CS_DBLCLKS: DOWN UP DBLCLK UP */
  click(150, 150, 21000, 21030);
  click(150, 150, 21100, 21130);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[0], T_WM_LBUTTONDOWN);
  CHECK_EQ(s[1], T_WM_LBUTTONUP);
  CHECK_EQ(s[2], T_WM_LBUTTONDBLCLK);
  CHECK_EQ(s[3], T_WM_LBUTTONUP);
  /* the DBLCLK cleared the stored click: a third quick click is a plain DOWN */
  click(150, 150, 21200, 21230);
  n = seq(s, 8);
  CHECK_EQ(n, 2);
  CHECK_EQ(s[0], T_WM_LBUTTONDOWN);

  /* 600 ms apart: no DBLCLK */
  click(150, 150, 30000, 30030);
  click(150, 150, 30600, 30630);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDOWN);

  /* boundary: 499 ms is a double click, 500 ms is not (strict, W-dbl) */
  click(150, 150, 40000, 40010);
  click(150, 150, 40499, 40510);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDBLCLK);
  click(150, 150, 50000, 50010);
  click(150, 150, 50500, 50510);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDOWN);

  /* a class without CS_DBLCLKS never gets one */
  mouse(450, 150, 0, 60000);
  drain();
  click(450, 150, 61000, 61030);
  click(450, 150, 61100, 61130);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[0], T_WM_LBUTTONDOWN);
  CHECK_EQ(s[2], T_WM_LBUTTONDOWN);

  /* distance: SM_CXDOUBLECLK = 4, the second click must be within < 2 pixels (W-dbl) */
  mouse(150, 150, 0, 70000);
  drain();
  click(150, 150, 71000, 71010);
  click(151, 150, 71100, 71110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDBLCLK);
  click(150, 150, 72000, 72010);
  click(152, 150, 72100, 72110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDOWN);
  click(150, 150, 73000, 73010);
  mouse(150, 152, 0, 73050);
  click(150, 152, 73100, 73110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDOWN);

  /* a different button */
  mouse(150, 150, 0, 74000);
  drain();
  click(150, 150, 75000, 75010);
  mouse(150, 150, 2, 75100);
  mouse(150, 150, 0, 75110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[0], T_WM_LBUTTONDOWN);
  CHECK_EQ(s[2], T_WM_RBUTTONDOWN);
  /* right button double click */
  mouse(150, 150, 2, 76000);
  mouse(150, 150, 0, 76010);
  mouse(150, 150, 2, 76100);
  mouse(150, 150, 0, 76110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], 0x0206 /* WM_RBUTTONDBLCLK */);

  /* a different window (parent then child at the child's position) */
  mouse(150, 150, 0, 77000);
  drain();
  mouse(260, 110, 0, 77010); /* inside child dc: (250..290) x (100..140) is dc */
  drain();
  click(260, 110, 78000, 78010);
  click(260, 110, 78100, 78110);
  n = seq(s, 8);
  CHECK_EQ(n, 4);
  CHECK_EQ(s[2], T_WM_LBUTTONDBLCLK); /* the child's class has CS_DBLCLKS too */
  (void)dc;

  /* PM_NOREMOVE peeks do not disturb the stored click (W-mouse 2745) */
  mouse(150, 150, 0, 79000);
  drain();
  click(150, 150, 80000, 80010);
  drain();
  mouse(150, 150, 1, 80100);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0x00A0, 0x0209, PM_NOREMOVE), 1);
  CHECK_EQ(M()->message, T_WM_LBUTTONDBLCLK);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0x00A0, 0x0209, PM_NOREMOVE), 1);
  CHECK_EQ(M()->message, T_WM_LBUTTONDBLCLK);
  mouse(150, 150, 0, 80110);
  drain();

  DestroyWindow(d);
  DestroyWindow(p);
}

static void test_capture(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 100, 100, 200, 150, 0, 0);
  HWND16 o = mk("O", CLS, "o", TOP, 400, 100, 200, 150, 0, 0);
  MSG16 m;
  reset_input();
  mouse(150, 150, 0, 90000);
  drain();

  CHECK_EQ(GetCapture(), 0);
  CHECK_EQ(SetCapture(w), 0);
  CHECK_EQ(GetCapture(), w);
  /* outside the window, over another window: still routed to the capturing window, in its
   * client coordinates (negative values are two's complement words), without WM_NCHITTEST
   * (W-mouse 2612-2617) */
  log_clear();
  mouse(450, 150, 1, 90010);
  CHECK_EQ(get_btn(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONDOWN, 1, pt32(350, 50));
  CHECK_EQ(log_find("O", WM_NCHITTEST, 0) == NULL, 1);
  CHECK_EQ(log_find("W", WM_NCHITTEST, 0) == NULL, 1);
  mouse(50, 40, 1, 90020);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_MOUSEMOVE, 1, pt32(-50, -60));
  /* over the desktop too */
  mouse(750, 580, 0, 90030);
  CHECK_EQ(get_btn(&m), 1);
  EXPECT_MSG(m, w, T_WM_LBUTTONUP, 0, pt32(650, 480));
  CHECK_EQ(SetCapture(o), w); /* returns the previous capture */
  mouse(150, 150, 0, 90040);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.hwnd, o);
  ReleaseCapture();
  CHECK_EQ(GetCapture(), 0);
  mouse(450, 150, 0, 90050);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.hwnd, o); /* normal routing again */
  CHECK_EQ(log_find("O", WM_NCHITTEST, 0) != NULL, 1);

  /* capture is released when its window is destroyed */
  SetCapture(w);
  DestroyWindow(w);
  CHECK_EQ(GetCapture(), 0);
  /* and by WM_CANCELMODE (DefWindowProc) */
  SetCapture(o);
  SendMessage(o, WM_CANCELMODE, 0, 0);
  CHECK_EQ(GetCapture(), 0);
  /* a window handle that does not exist */
  CHECK_EQ(SetCapture(0x7770), 0);
  CHECK_EQ(GetCapture(), 0);
  DestroyWindow(o);
}

static void test_cursor_pos(void) {
  FARPTR p = far_alloc(4);
  POINT16 pt;
  mouse(321, 123, 0, 100000);
  GetCursorPos(p);
  memcpy(&pt, LIN(p), 4);
  CHECK_EQ(pt.x, 321); /* the pointer position is the latest the host reported, not queue-synchronised */
  CHECK_EQ(pt.y, 123);
  drain();
}

/* ================= cursors (W-setcursor) ================= */

static void test_cursors(void) {
  uint16_t arrow = LoadCursor(0, MAKE_INTRES(32512)), ibeam = LoadCursor(0, MAKE_INTRES(32513));
  uint16_t wait = LoadCursor(0, MAKE_INTRES(32514));
  HWND16 w, c;
  MSG16 m;
  CHECK(arrow != 0);
  CHECK(ibeam != 0);
  CHECK(wait != 0);
  CHECK(arrow != ibeam);
  CHECK(ibeam != wait);
  CHECK_EQ(LoadCursor(0, MAKE_INTRES(32512)), arrow); /* the same cursor, the same handle */
  /* application cursors: a stable non-zero handle per (instance, name) */
  {
    uint16_t a1 = LoadCursor(0x1234, fstr("MYCUR")), a2 = LoadCursor(0x1234, fstr("MYCUR")), a3 = LoadCursor(0x1234, fstr("OTHER"));
    CHECK(a1 != 0);
    CHECK_EQ(a1, a2);
    CHECK(a3 != a1);
    CHECK(a1 != arrow);
    CHECK_EQ(LoadCursor(0x1234, MAKE_INTRES(5)), LoadCursor(0x1234, MAKE_INTRES(5)));
  }
  /* SetCursor returns the previous cursor */
  SetCursor(arrow);
  CHECK_EQ(SetCursor(wait), arrow);
  CHECK_EQ(SetCursor(arrow), wait);
  CHECK_EQ(user_current_cursor(), arrow);

  /* DefWindowProc(WM_SETCURSOR) with HTCLIENT sets the class cursor */
  next_label("-");
  {
    FARPTR p = far_alloc(sizeof(WNDCLASS16));
    WNDCLASS16 *wc = LIN(p);
    wc->lpfnWndProc = FP_PROC_A;
    wc->hCursor = ibeam;
    wc->lpszClassName = fstr(CLS_IB);
    wc->hInstance = 0x1234;
    CHECK(RegisterClass(p) != 0);
  }
  w = mk("W", CLS_IB, "w", TOP, 100, 100, 200, 150, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, w, 1); /* class without a cursor */
  reset_input();
  SetCursor(arrow);
  mouse(250, 200, 0, 110000); /* inside w, outside c */
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(user_current_cursor(), ibeam);
  /* A child offers WM_SETCURSOR to its parent first (defwnd.c 2692-2700), but the cursor that
   * DefWindowProc sets is the class cursor of the window named in wParam (handle_set_cursor 436-
   * 453: hwnd = wParam): the child. Its class has no cursor, so nothing changes, although the
   * parent's DefWindowProc ran. Wine behaviour; Windows 3.1 UNVERIFIED. */
  SetCursor(arrow);
  log_clear();
  mouse(120, 130, 0, 110010);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.hwnd, c);
  CHECK_LOG("C:NCHITTEST C:SETCURSOR W:SETCURSOR");
  CHECK_EQ(user_current_cursor(), arrow);
  /* a window without a class cursor and without a parent that sets one leaves it alone */
  DestroyWindow(c);
  DestroyWindow(w);
  w = mk("W", CLS, "w", TOP, 100, 100, 200, 150, 0, 0);
  SetCursor(wait);
  mouse(250, 200, 0, 110020);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(user_current_cursor(), wait);
  DestroyWindow(w);
  SetCursor(arrow);
}

/* ================= keyboard (W-key, W-xlate) ================= */

static void test_key_messages(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 0, 0, 100, 100, 0, 0);
  MSG16 m;
  SetFocus(w);
  reset_input();

  key(0x41, 0x1E, 1);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_KEYDOWN, 0x41, 0x001E0001u); /* repeat 1, scan code 0x1E */
  CHECK(m.time != 0);
  key(0x41, 0x1E, 1); /* auto-repeat: previous-state bit 30 */
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_KEYDOWN, 0x41, 0x401E0001u);
  key(0x41, 0x1E, 0);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_KEYUP, 0x41, 0xC01E0001u); /* bits 30 and 31 */
  /* time is the host's */
  rt_input_key(0x42, 0x30, 1, 987654);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.time, 987654);
  rt_input_key(0x42, 0x30, 0, 987660);
  drain();
  /* extended key (scancode bit 8 = extended flag -> lParam bit 24) */
  key(0x25, 0x14B, 1);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_KEYDOWN, 0x25, 0x014B0001u);
  key(0x25, 0x14B, 0);
  drain();

  /* Alt: SYSKEYDOWN, context bit 29 while Alt is down */
  key(0x12, 0x38, 1);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_SYSKEYDOWN, 0x12, 0x20380001u);
  key(0x46, 0x21, 1);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_SYSKEYDOWN, 0x46, 0x20210001u);
  key(0x46, 0x21, 0);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_SYSKEYUP, 0x46, 0xE0210001u);
  key(0x12, 0x38, 0); /* a key came in between: a plain WM_KEYUP */
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_KEYUP, 0x12, 0xC0380001u);
  /* Alt tapped alone: SYSKEYDOWN, SYSKEYUP */
  key(0x12, 0x38, 1);
  key(0x12, 0x38, 0);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.message, T_WM_SYSKEYDOWN);
  CHECK_EQ(get_in(&m), 1);
  EXPECT_MSG(m, w, T_WM_SYSKEYUP, 0x12, 0xC0380001u);
  /* F10 is a system key without Alt */
  key(0x79, 0x44, 1);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.message, T_WM_SYSKEYDOWN);
  key(0x79, 0x44, 0);
  drain();
  /* Ctrl+Alt: ordinary key messages */
  key(0x11, 0x1D, 1);
  key(0x12, 0x38, 1);
  key(0x41, 0x1E, 1);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.message, T_WM_KEYDOWN);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.message, T_WM_KEYDOWN);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.message, T_WM_KEYDOWN);
  key(0x41, 0x1E, 0);
  key(0x12, 0x38, 0);
  key(0x11, 0x1D, 0);
  drain();

  /* no focus: the active window gets the SYS form (W-key 1778-1780) */
  SetFocus(0);
  CHECK_EQ(GetActiveWindow(), w);
  key(0x41, 0x1E, 1);
  CHECK_EQ(get_in(&m), 1);
  CHECK_EQ(m.hwnd, w);
  CHECK_EQ(m.message, T_WM_SYSKEYDOWN);
  key(0x41, 0x1E, 0);
  drain();
  /* no focus and no active window: dropped, but the key state still follows */
  SetActiveWindow(0);
  key(0x10, 0x2A, 1);
  CHECK_EQ(get_in(&m), 0);
  CHECK(GetKeyState(0x10) < 0);
  key(0x10, 0x2A, 0);
  CHECK_EQ(get_in(&m), 0);
  CHECK(GetKeyState(0x10) >= 0);
  DestroyWindow(w);
}

/* Presses `vk` with the modifiers already held, translates it, and returns the character
 * posted by TranslateMessage (-1 if none). `msg_out` receives the WM_CHAR message. */
static int translate_vk(unsigned vk, MSG16 *char_msg, int *ret) {
  MSG16 m;
  int r, got = -1;
  key(vk, 0x20, 1);
  if (!get_in(&m)) return -2;
  *(MSG16 *)LIN(g_msgp) = m;
  r = TranslateMessage(g_msgp);
  if (ret) *ret = r;
  if (PeekMessage(g_msgp, 0, T_WM_CHAR, T_WM_SYSCHAR + 1, PM_REMOVE)) {
    if (char_msg) *char_msg = *M();
    got = M()->wParam;
  }
  key(vk, 0x20, 0);
  drain();
  return got;
}
#define XL(vk) translate_vk((vk), NULL, NULL)

static void hold(unsigned vk, int down) {
  key(vk, 0x2A, down);
  drain();
}

static void test_translate(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 0, 0, 100, 100, 0, 0);
  MSG16 cm;
  int r;
  SetFocus(w);
  reset_input();

  /* the issue's pair */
  CHECK_EQ(XL(0x41), 'a');
  hold(0x10, 1);
  CHECK_EQ(XL(0x41), 'A');
  hold(0x10, 0);

  /* the returned value and the WM_CHAR message */
  CHECK_EQ(translate_vk(0x41, &cm, &r), 'a');
  CHECK_EQ(r, 1);
  CHECK_EQ(cm.hwnd, w);
  CHECK_EQ(cm.message, T_WM_CHAR);
  CHECK_EQ(cm.lParam, 0x00200001u); /* the keydown's lParam (scan code 0x20) */
  /* keys that produce no character: 0 and no message */
  CHECK_EQ(translate_vk(0x70 /* F1 */, NULL, &r), -1);
  CHECK_EQ(r, 0);
  CHECK_EQ(translate_vk(0x25 /* LEFT */, NULL, &r), -1);
  CHECK_EQ(r, 0);
  CHECK_EQ(translate_vk(0x10 /* SHIFT itself */, NULL, &r), -1);
  CHECK_EQ(r, 0);

  /* Caps Lock (toggle): letters only, Shift cancels it */
  key(0x14, 0x3A, 1);
  key(0x14, 0x3A, 0);
  drain();
  CHECK_EQ(XL(0x41), 'A');
  CHECK_EQ(XL(0x31), '1');
  hold(0x10, 1);
  CHECK_EQ(XL(0x41), 'a');
  CHECK_EQ(XL(0x31), '!');
  hold(0x10, 0);
  key(0x14, 0x3A, 1);
  key(0x14, 0x3A, 0);
  drain();
  CHECK_EQ(XL(0x41), 'a');

  /* digits and shifted digits */
  CHECK_EQ(XL(0x30), '0');
  CHECK_EQ(XL(0x39), '9');
  hold(0x10, 1);
  CHECK_EQ(XL(0x30), ')');
  CHECK_EQ(XL(0x32), '@');
  CHECK_EQ(XL(0x33), '#');
  CHECK_EQ(XL(0x34), '$');
  CHECK_EQ(XL(0x35), '%');
  CHECK_EQ(XL(0x36), '^');
  CHECK_EQ(XL(0x37), '&');
  CHECK_EQ(XL(0x38), '*');
  CHECK_EQ(XL(0x39), '(');
  CHECK_EQ(XL(0xBA), ':');
  CHECK_EQ(XL(0xBB), '+');
  CHECK_EQ(XL(0xBC), '<');
  CHECK_EQ(XL(0xBD), '_');
  CHECK_EQ(XL(0xBE), '>');
  CHECK_EQ(XL(0xBF), '?');
  CHECK_EQ(XL(0xC0), '~');
  CHECK_EQ(XL(0xDB), '{');
  CHECK_EQ(XL(0xDC), '|');
  CHECK_EQ(XL(0xDD), '}');
  CHECK_EQ(XL(0xDE), '"');
  hold(0x10, 0);
  CHECK_EQ(XL(0xBA), ';');
  CHECK_EQ(XL(0xBB), '=');
  CHECK_EQ(XL(0xBC), ',');
  CHECK_EQ(XL(0xBD), '-');
  CHECK_EQ(XL(0xBE), '.');
  CHECK_EQ(XL(0xBF), '/');
  CHECK_EQ(XL(0xC0), '`');
  CHECK_EQ(XL(0xDB), '[');
  CHECK_EQ(XL(0xDC), '\\');
  CHECK_EQ(XL(0xDD), ']');
  CHECK_EQ(XL(0xDE), '\'');

  /* control characters and space */
  CHECK_EQ(XL(0x20), ' ');
  CHECK_EQ(XL(0x0D), 13);
  CHECK_EQ(XL(0x09), 9);
  CHECK_EQ(XL(0x08), 8);
  CHECK_EQ(XL(0x1B), 27);
  /* Ctrl+letter is the control code */
  hold(0x11, 1);
  CHECK_EQ(XL(0x41), 1);
  CHECK_EQ(XL(0x5A), 26);
  /* Ctrl+digit / Ctrl+punctuation: a US layout produces no character, so no WM_CHAR is posted
   * (ToUnicodeEx under Ctrl, win32u/input.c; UNVERIFIED for Windows 3.1). The exceptions are the
   * ASCII control codes Ctrl+[ \ ] = 1B 1C 1D and Ctrl+Shift+6 / Ctrl+Shift+- = 1E 1F. */
  CHECK_EQ(XL(0x30), -1);
  CHECK_EQ(XL(0x31), -1);
  CHECK_EQ(XL(0x39), -1);
  CHECK_EQ(XL(0xBA), -1); /* ; */
  CHECK_EQ(XL(0xBB), -1); /* = */
  CHECK_EQ(XL(0xBC), -1); /* , */
  CHECK_EQ(XL(0xBD), -1); /* - */
  CHECK_EQ(XL(0xBE), -1); /* . */
  CHECK_EQ(XL(0xBF), -1); /* / */
  CHECK_EQ(XL(0xC0), -1); /* ` */
  CHECK_EQ(XL(0xDE), -1); /* ' */
  CHECK_EQ(XL(0x60), -1); /* keypad 0 */
  CHECK_EQ(XL(0x6A), -1); /* keypad * */
  CHECK_EQ(XL(0xDB), 0x1B);
  CHECK_EQ(XL(0xDC), 0x1C);
  CHECK_EQ(XL(0xDD), 0x1D);
  hold(0x10, 1);
  CHECK_EQ(XL(0x36), 0x1E);
  CHECK_EQ(XL(0xBD), 0x1F);
  CHECK_EQ(XL(0x32), -1); /* Ctrl+Shift+2 is NUL: nothing posted */
  hold(0x10, 0);
  hold(0x11, 0);
  CHECK_EQ(XL(0x31), '1'); /* without Ctrl nothing changed */

  /* the numeric keypad: digits and operators */
  CHECK_EQ(XL(0x60), '0');
  CHECK_EQ(XL(0x65), '5');
  CHECK_EQ(XL(0x69), '9');
  CHECK_EQ(XL(0x6A), '*');
  CHECK_EQ(XL(0x6B), '+');
  CHECK_EQ(XL(0x6D), '-');
  CHECK_EQ(XL(0x6E), '.');
  CHECK_EQ(XL(0x6F), '/');

  /* Alt+letter: WM_SYSKEYDOWN -> WM_SYSCHAR (W-xlate), lParam of the keydown */
  hold(0x12, 1);
  CHECK_EQ(translate_vk(0x46, &cm, &r), 'f');
  CHECK_EQ(r, 1);
  CHECK_EQ(cm.message, T_WM_SYSCHAR);
  CHECK_EQ(cm.lParam, 0x20200001u);
  hold(0x12, 0);

  /* key-up and non-key messages are not translated */
  {
    MSG16 *p = LIN(g_msgp);
    memset(p, 0, sizeof *p);
    p->hwnd = w;
    p->message = T_WM_KEYUP;
    p->wParam = 0x41;
    CHECK_EQ(TranslateMessage(g_msgp), 0);
    p->message = WM_USER;
    CHECK_EQ(TranslateMessage(g_msgp), 0);
    CHECK_EQ(PeekMessage(g_msgp, 0, T_WM_CHAR, T_WM_SYSCHAR + 1, PM_REMOVE), 0);
  }

  /* WM_CHAR goes ahead of key events that are already waiting (it is a posted message) */
  key(0x41, 0x1E, 1);
  key(0x42, 0x30, 1);
  {
    MSG16 m;
    CHECK_EQ(get_in(&m), 1);
    CHECK_EQ(m.wParam, 0x41);
    *(MSG16 *)LIN(g_msgp) = m;
    CHECK_EQ(TranslateMessage(g_msgp), 1);
    CHECK_EQ(get_in(&m), 1);
    CHECK_EQ(m.message, T_WM_CHAR);
    CHECK_EQ(m.wParam, 'a');
    CHECK_EQ(get_in(&m), 1);
    CHECK_EQ(m.message, T_WM_KEYDOWN);
    CHECK_EQ(m.wParam, 0x42);
  }
  key(0x41, 0x1E, 0);
  key(0x42, 0x30, 0);
  drain();
  DestroyWindow(w);
}

static void test_key_state(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 0, 0, 100, 100, 0, 0);
  MSG16 m;
  SetFocus(w);
  reset_input();
  drain();

  /* GetKeyState follows the retrieved messages; GetAsyncKeyState is the hardware state */
  key(0x10, 0x2A, 1);
  CHECK(GetKeyState(0x10) >= 0);
  CHECK((GetAsyncKeyState(0x10) & 0x8000) != 0);
  CHECK_EQ(get_in(&m), 1);
  CHECK(GetKeyState(0x10) < 0);
  key(0x10, 0x2A, 0);
  CHECK(GetKeyState(0x10) < 0);
  drain();
  CHECK(GetKeyState(0x10) >= 0);
  CHECK_EQ(GetAsyncKeyState(0x10) & 0x8000, 0);

  /* toggle keys: bit 0 flips on every key-down */
  CHECK_EQ(GetKeyState(0x14) & 1, 0);
  key(0x14, 0x3A, 1);
  key(0x14, 0x3A, 0);
  drain();
  CHECK_EQ(GetKeyState(0x14) & 1, 1);
  CHECK(GetKeyState(0x14) >= 0); /* not down */
  key(0x14, 0x3A, 1);
  key(0x14, 0x3A, 0);
  drain();
  CHECK_EQ(GetKeyState(0x14) & 1, 0);
  key(0x90, 0x45, 1);
  key(0x90, 0x45, 0);
  drain();
  CHECK_EQ(GetKeyState(0x90) & 1, 1); /* VK_NUMLOCK */
  key(0x90, 0x45, 1);
  key(0x90, 0x45, 0);
  drain();

  /* GetAsyncKeyState bit 0: pressed since the previous call */
  GetAsyncKeyState(0x41);
  key(0x41, 0x1E, 1);
  CHECK_EQ(GetAsyncKeyState(0x41), (int16_t)0x8001);
  CHECK_EQ(GetAsyncKeyState(0x41), (int16_t)0x8000);
  key(0x41, 0x1E, 0);
  CHECK_EQ(GetAsyncKeyState(0x41), 0);
  drain();

  /* mouse buttons are virtual keys 1, 2, 4 */
  reset_input();
  mouse(40, 40, 1, 130000); /* over w */
  CHECK((GetAsyncKeyState(1) & 0x8000) != 0);
  CHECK(GetKeyState(1) >= 0);
  drain();
  CHECK(GetKeyState(1) < 0);
  mouse(40, 40, 0, 130010);
  drain();
  CHECK(GetKeyState(1) >= 0);
  mouse(40, 40, 2, 130020);
  drain();
  CHECK(GetKeyState(2) < 0);
  mouse(40, 40, 0, 130030);
  drain();
  CHECK_EQ(GetKeyState(300), 0); /* out of range */
  DestroyWindow(w);
}

/* ================= miscellaneous ================= */

static void test_misc(void) {
  HWND16 w = mk("W", CLS, "w", TOP, 0, 0, 100, 100, 0, 0);
  g_beeps = 0;
  user_beep_hook = beep_hook;
  MessageBeep(0x10);
  CHECK_EQ(g_beeps, 1);
  CHECK_EQ(g_beep_type, 0x10);
  MessageBeep(0xFFFF);
  CHECK_EQ(g_beeps, 2);
  CHECK_EQ(g_beep_type, 0xFFFF);
  user_beep_hook = NULL;
  MessageBeep(0); /* no host callback: nothing happens */

  CHECK_EQ(InitApp(0x1234), 1);

  /* FlashWindow: toggles the active look through WM_NCACTIVATE, returns the previous state
   * (UNVERIFIED rule; no non-client drawing yet) */
  SetActiveWindow(w);
  log_clear();
  CHECK_EQ(FlashWindow(w, 1), 1);
  CHECK_LOG("W:NCACTIVATE");
  { LogEnt *e = log_find("W", WM_NCACTIVATE, 0); if (e) CHECK_EQ(e->wp, 0); }
  CHECK_EQ(FlashWindow(w, 1), 0);
  { LogEnt *e = log_find("W", WM_NCACTIVATE, 1); if (e) CHECK_EQ(e->wp, 1); }
  CHECK_EQ(FlashWindow(w, 1), 1);
  CHECK_EQ(FlashWindow(w, 0), 0); /* back to the real state: active */
  { LogEnt *e = log_find("W", WM_NCACTIVATE, 3); if (e) CHECK_EQ(e->wp, 1); }
  CHECK_EQ(FlashWindow(0x7770, 1), 0);

  /* ExitWindows ends the program: WM_QUIT with the low word of the return code (UNVERIFIED:
   * the real call also sends WM_QUERYENDSESSION / WM_ENDSESSION, which are not generated) */
  CHECK_EQ(user_exit_requested(), 0);
  CHECK_EQ(ExitWindows(3, 0), 1);
  CHECK_EQ(user_exit_requested(), 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_QUIT, WM_QUIT, PM_REMOVE), 1);
  CHECK_EQ(M()->message, WM_QUIT);
  CHECK_EQ(M()->wParam, 3);
  DestroyWindow(w);
}

/* ================= PASCAL thunks (harness/user_thunks.c) ================= */

/* Thunks exist only for the imports of the loaded exe: an ordinal the game does not import has a
 * registered handler but no thunk to call, and is reported as skipped. */
static int g_thunk_skipped;
static uint32_t call_user(uint16_t ord, const uint16_t *args, int n) {
  uint32_t fp = harness_thunk_far("USER", ord);
  if (!fp) {
    g_thunk_skipped++;
    fprintf(stderr, "note: USER.%u is not imported by %s: thunk test skipped\n", ord, EXE);
    return 0xDEADBEEFu;
  }
  return harness_call_far(fp, HCONV_PASCAL, args, n);
}
#define HI(x) ((uint16_t)((uint32_t)(x) >> 16))
#define LO(x) ((uint16_t)((uint32_t)(x) & 0xFFFF))

static void test_thunks(void) {
  FARPTR r = far_alloc(8), r2 = far_alloc(8);
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 50, 50, 0, 0);
  uint32_t v;
  /* SetRect(ptr, s_word x4): the pointer is pushed first, the coordinates keep their sign */
  { uint16_t a[6] = {HI(r), LO(r), 10, (uint16_t)-20, 30, 40}; call_user(72, a, 6); }
  CHECK_RECT(R_(r), 10, -20, 30, 40);
  /* PtInRect(ptr, long): the POINT is one long, y in the high word */
  { uint16_t a[4] = {HI(r), LO(r), (uint16_t)-5 /* y */, 29 /* x */}; CHECK_EQ(call_user(76, a, 4) & 0xFFFF, 1); }
  { uint16_t a[4] = {HI(r), LO(r), 5 /* y */, 30 /* x */}; CHECK_EQ(call_user(76, a, 4) & 0xFFFF, 0); }
  /* IntersectRect(dest, a, b) */
  SetRect(r2, 20, 0, 50, 10);
  { FARPTR dst = far_alloc(8); uint16_t a[6] = {HI(dst), LO(dst), HI(r), LO(r), HI(r2), LO(r2)}; CHECK_EQ(call_user(79, a, 6) & 0xFFFF, 1); CHECK_RECT(R_(dst), 20, 0, 30, 10); }
  /* GetSysColor returns the whole COLORREF in DX:AX */
  { uint16_t a[1] = {2}; CHECK_EQ(call_user(180, a, 1), GDI_RGB(0, 0, 128)); }
  /* GetSystemMetrics(s_word) */
  { uint16_t a[1] = {0}; CHECK_EQ(call_user(179, a, 1) & 0xFFFF, GDI_SCREEN_W); }
  { uint16_t a[1] = {(uint16_t)-3}; CHECK_EQ(call_user(179, a, 1) & 0xFFFF, 0); }
  /* GetTickCount: DX:AX */
  clock_s(1, 0, 1, 40);
  CHECK_EQ(call_user(13, NULL, 0), 100000);
  /* SetTimer(hwnd, id, timeout, proc) / KillTimer(hwnd, id) */
  clock_s(1, 0, 0, 0);
  { uint16_t a[5] = {w, 21, 100, 0, 0}; CHECK_EQ(call_user(10, a, 5) & 0xFFFF, 21); }
  clock_s(1, 0, 0, 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_TIMER, WM_TIMER, PM_REMOVE), 1);
  CHECK_EQ(M()->wParam, 21);
  { uint16_t a[2] = {w, 21}; CHECK_EQ(call_user(12, a, 2) & 0xFFFF, 1); }
  /* SetFocus / GetFocus / SetActiveWindow / GetActiveWindow / SetCapture / GetCapture */
  SetActiveWindow(w); /* USER.59 is not imported by the game: no thunk to call */
  CHECK_EQ(call_user(60, NULL, 0) & 0xFFFF, w);
  { uint16_t a[1] = {w}; call_user(22, a, 1); }
  CHECK_EQ(call_user(23, NULL, 0) & 0xFFFF, w);
  { uint16_t a[1] = {w}; call_user(18, a, 1); }
  CHECK_EQ(GetCapture(), w); /* USER.236 is not imported either */
  call_user(19, NULL, 0);
  CHECK_EQ(GetCapture(), 0);
  /* GetKeyState returns a signed 16-bit value in AX */
  reset_input();
  SetFocus(w);
  key(0x10, 0x2A, 1);
  drain();
  { uint16_t a[1] = {0x10}; CHECK_EQ(call_user(106, a, 1) & 0x8000, 0x8000); }
  { uint16_t a[1] = {0x10}; CHECK((call_user(249, a, 1) & 0x8000) != 0); }
  key(0x10, 0x2A, 0);
  drain();
  /* LoadCursor(hinst, name): the name is a far pointer or MAKEINTRESOURCE (high word 0) */
  { uint16_t a[3] = {0, 0, 32512}; v = call_user(173, a, 3) & 0xFFFF; CHECK(v != 0); CHECK_EQ(v, LoadCursor(0, MAKE_INTRES(32512))); }
  /* ExitWindows(long, word): the long is pushed first */
  { uint16_t a[3] = {0, 9, 0}; CHECK_EQ(call_user(7, a, 3) & 0xFFFF, 1); }
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_QUIT, WM_QUIT, PM_REMOVE), 1);
  CHECK_EQ(M()->wParam, 9);
  { uint16_t a[1] = {0x1234}; CHECK_EQ(call_user(5, a, 1) & 0xFFFF, 1); }
  DestroyWindow(w);
}

int main(void) {
  if (fixture_open() != 0) return 1;
  g_msgp = far_alloc(sizeof(MSG16));
  register_class(CLS, FP_PROC_A, 0, 0);
  register_class(CLS_DBL, FP_PROC_A, 0x0008 /* CS_DBLCLKS */, 0);
  register_class("TESTCLSC", FP_PROC_C, 0, 0);
  test_rects();
  test_fill_frame();
  test_metrics_colors();
  test_tick();
  test_timers();
  test_auto_activation();
  test_timer_priority();
  test_activation_focus();
  test_mouse_basic();
  test_mouse_hit_and_disabled();
  test_double_click();
  test_capture();
  test_cursor_pos();
  test_cursors();
  test_key_messages();
  test_translate();
  test_key_state();
  test_misc();
  test_thunks();
  fixture_close();
  return TEST_DONE();
}
