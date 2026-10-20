/* Windows 3.1 look, part 1: non-client structure and behaviour (task stars-0z4.9).
 *
 * TEST-FIRST. These tests check STRUCTURE only: rectangles from GetSystemMetrics, hit-test
 * regions, message routing, state changes, scroll bar bookkeeping, cursor and icon handles,
 * MessageBox flow. No Windows 3.11 file and no reference capture exists (owner amendment to
 * decision 8), so nothing here is verified against Windows 3.1:
 *   - every METRIC comes from runtime/user_misc.c (itself UNVERIFIED, docs/inventory.md 13);
 *   - the way those metrics combine (frame + caption, button sizes, tick positions, the icon
 *     rectangle, the sizing-corner length) is this runtime's reading of the Windows 3.1 look and is
 *     marked UNVERIFIED in docs/inventory.md section 13 ("Windows 3.1 look");
 *   - the hit-test code values (HTCAPTION ... HTBORDER) and the WM_SYSCOMMAND / SB_* numbers are
 *     the Win16 SDK values as recalled (Wine's include/wine/winuser16.h agrees).
 * Pixel expectations are in test_visual_chrome.c.
 *
 * Window used throughout: overlapped, 200x150 at (100,80); window coordinates below are relative
 * to that rectangle. */
#include "test.h"

#include "user_test_common.h"
#include "runtime/kernel.h"
#include "runtime/user_internal.h"

#define CLS "NCCLS"
#define TOPW (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define WX 100
#define WY 80
#define WW 200
#define WH 150
#define VK_F4_ 0x73

/* ---- scripted pointer input, fed one event at a time when GetMessage has nothing ---- */

typedef struct Step {
  char kind;        /* 'M' mouse (x, y, buttons), 'K' key down (x = vk), 'T' timer message to hwnd */
  int x, y, b;
  HWND16 hwnd;
} Step;
static Step g_script[32];
static int g_nscript, g_iscript;
static uint32_t g_t = 5000;

static void script_clear(void) { g_nscript = g_iscript = 0; }
static void sm(int x, int y, int b) { g_script[g_nscript++] = (Step){'M', x, y, b, 0}; }
static void sk(int vk) { g_script[g_nscript++] = (Step){'K', vk, 0, 0, 0}; }
static void st(HWND16 h) { g_script[g_nscript++] = (Step){'T', 0, 0, 0, h}; }

static bool feeder(void) {
  const Step *s;
  if (g_iscript >= g_nscript) return false;
  s = &g_script[g_iscript++];
  g_t += 10;
  if (s->kind == 'M') rt_input_mouse((int16_t)s->x, (int16_t)s->y, (uint16_t)s->b, g_t);
  else if (s->kind == 'K') {
    rt_input_key((uint16_t)s->x, 0x01, 1, g_t);
    rt_input_key((uint16_t)s->x, 0x01, 0, g_t + 1);
  } else PostMessage(s->hwnd, WM_TIMER, USER_SB_TIMER_ID, 0);
  return true;
}

/* Runs the application loop until the script is used up. */
static void run_script(void) {
  FARPTR p = far_alloc(sizeof(MSG16));
  int guard = 0;
  g_iscript = 0;
  user_wait_hook = feeder;
  while (guard++ < 400 && GetMessage(p, 0, 0, 0)) {
    TranslateMessage(p);
    DispatchMessage(p);
  }
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  script_clear();
}

/* ---- helpers ---- */

static HWND16 g_top;

static HWND16 make_top(uint32_t style) {
  HWND16 h = mk("T", CLS, "Main", style, WX, WY, WW, WH, 0, 0);
  CHECK(h != 0);
  return h;
}
static int16_t HT(HWND16 h, int wx, int wy) { /* hit test at window coordinates */
  RECT16 r = get_rect(h, 0);
  return (int16_t)SendMessage(h, WM_NCHITTEST, 0, pt32(r.left + wx, r.top + wy));
}
static void check_layout_rect(RECT16 r, int l, int t, int rr, int b) { CHECK_RECT(r, l, t, rr, b); }

static uint8_t px(int x, int y) { return fb_pixels()[y * GDI_SCREEN_W + x]; }

/* ---- tests ---- */

static void test_constants(void) {
  CHECK_EQ(WM_SYSCOMMAND, 0x0112);
  CHECK_EQ(WM_HSCROLL, 0x0114);
  CHECK_EQ(WM_VSCROLL, 0x0115);
  CHECK_EQ(HTSYSMENU, 3);
  CHECK_EQ(HTGROWBOX, 4);
  CHECK_EQ(HTMENU, 5);
  CHECK_EQ(HTHSCROLL, 6);
  CHECK_EQ(HTVSCROLL, 7);
  CHECK_EQ(HTREDUCE, 8);
  CHECK_EQ(HTZOOM, 9);
  CHECK_EQ(HTBORDER, 18);
  CHECK_EQ(SC_SIZE, 0xF000);
  CHECK_EQ(SC_MOVE, 0xF010);
  CHECK_EQ(SC_MINIMIZE, 0xF020);
  CHECK_EQ(SC_MAXIMIZE, 0xF030);
  CHECK_EQ(SC_CLOSE, 0xF060);
  CHECK_EQ(SC_RESTORE, 0xF120);
  CHECK_EQ(SC_TASKLIST, 0xF130);
  CHECK_EQ(SB_LINEUP, 0);
  CHECK_EQ(SB_LINEDOWN, 1);
  CHECK_EQ(SB_PAGEUP, 2);
  CHECK_EQ(SB_PAGEDOWN, 3);
  CHECK_EQ(SB_THUMBPOSITION, 4);
  CHECK_EQ(SB_THUMBTRACK, 5);
  CHECK_EQ(SB_TOP, 6);
  CHECK_EQ(SB_BOTTOM, 7);
  CHECK_EQ(SB_ENDSCROLL, 8);
  CHECK_EQ(MB_YESNOCANCEL, 3);
  CHECK_EQ(MB_ICONQUESTION, 0x20);
  CHECK_EQ(IDYES, 6);
  CHECK_EQ(IDNO, 7);
}

/* Layout of the overlapped window from the metrics (UNVERIFIED combination, see the file comment). */
static void test_metrics_layout(void) {
  UNcLayout L;
  int F = GetSystemMetrics(32), CAP = GetSystemMetrics(4), SZ = GetSystemMetrics(30);
  CHECK_EQ(F, 4);
  CHECK_EQ(CAP, 20);
  CHECK_EQ(SZ, 18);
  user_nc_layout_for(WS_OVERLAPPEDWINDOW, 0, WW, WH, &L);
  CHECK_EQ(L.frame, F);                      /* frame width == SM_CXFRAME */
  CHECK(L.thick && L.caption && !L.dlg && !L.iconic);
  CHECK_EQ(L.client.top - L.frame, CAP);     /* caption height == SM_CYCAPTION (bar + separator line) */
  CHECK_EQ(L.capbar.bottom - L.capbar.top, CAP - 1);
  check_layout_rect(L.capbar, 4, 4, 196, 23);
  check_layout_rect(L.sysbox, 4, 4, 22, 23);   /* width SM_CXSIZE */
  check_layout_rect(L.minbtn, 160, 4, 178, 23);
  check_layout_rect(L.maxbtn, 178, 4, 196, 23);
  check_layout_rect(L.title, 22, 4, 160, 23);
  check_layout_rect(L.client, 4, 24, 196, 146);
  CHECK(r_empty(L.hscroll) && r_empty(L.vscroll) && r_empty(L.sizebox));
  /* window scroll bars: SM_CXVSCROLL wide / SM_CYHSCROLL high, a size box where they meet */
  user_nc_layout_for(WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL, 0, WW, WH, &L);
  check_layout_rect(L.client, 4, 24, 180, 130);
  check_layout_rect(L.vscroll, 180, 24, 196, 130);
  check_layout_rect(L.hscroll, 4, 130, 180, 146);
  check_layout_rect(L.sizebox, 180, 130, 196, 146);
  CHECK_EQ(L.vscroll.right - L.vscroll.left, GetSystemMetrics(2));
  CHECK_EQ(L.hscroll.bottom - L.hscroll.top, GetSystemMetrics(3));
  user_nc_layout_for(WS_OVERLAPPEDWINDOW | WS_VSCROLL, 0, WW, WH, &L);
  check_layout_rect(L.vscroll, 180, 24, 196, 146);   /* no horizontal bar: the vertical one runs to the frame */
  check_layout_rect(L.client, 4, 24, 180, 146);
  /* only the system menu box */
  user_nc_layout_for(WS_CAPTION | WS_SYSMENU | WS_POPUP, 0, WW, WH, &L);
  CHECK(!r_empty(L.sysbox) && r_empty(L.minbtn) && r_empty(L.maxbtn));
  check_layout_rect(L.title, 1 + 18, 1, WW - 1, 1 + 19);
}

static void test_frames(void) {
  UNcLayout L;
  /* WS_BORDER: SM_CXBORDER */
  user_nc_layout_for(WS_POPUP | WS_BORDER, 0, 100, 50, &L);
  CHECK_EQ(L.frame, GetSystemMetrics(5));
  CHECK(L.border && !L.thick && !L.caption);
  check_layout_rect(L.client, 1, 1, 99, 49);
  /* WS_DLGFRAME: SM_CXDLGFRAME */
  user_nc_layout_for(WS_POPUP | WS_DLGFRAME, 0, 100, 50, &L);
  CHECK_EQ(L.frame, GetSystemMetrics(7));
  CHECK(L.dlg && !L.thick);
  check_layout_rect(L.client, 4, 4, 96, 46);
  /* DS_MODALFRAME reaches the window as WS_EX_DLGMODALFRAME: a dialog frame around a caption */
  user_nc_layout_for(WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, 100, 80, &L);
  CHECK_EQ(L.frame, GetSystemMetrics(7));
  CHECK(L.dlg && L.caption);
  check_layout_rect(L.client, 4, 24, 96, 76);
  CHECK_EQ(DS_MODALFRAME, 0x80);
  /* WS_CAPTION alone (no thick frame): a one pixel border around the caption */
  user_nc_layout_for(WS_POPUP | WS_CAPTION, 0, 100, 80, &L);
  CHECK_EQ(L.frame, 1);
  CHECK(L.caption && !L.thick);
  check_layout_rect(L.client, 1, 21, 99, 79);
  /* WS_THICKFRAME without a caption */
  user_nc_layout_for(WS_POPUP | WS_THICKFRAME, 0, 100, 80, &L);
  CHECK_EQ(L.frame, GetSystemMetrics(32));
  CHECK(L.thick && !L.caption);
  check_layout_rect(L.client, 4, 4, 96, 76);
  /* no frame style at all (WS_OVERLAPPED with no bits, WS_CHILD) */
  user_nc_layout_for(WS_CHILD, 0, 100, 80, &L);
  CHECK_EQ(L.frame, 0);
  check_layout_rect(L.client, 0, 0, 100, 80);
  /* the same through a real window: a style change plus SWP_FRAMECHANGED re-runs WM_NCCALCSIZE */
  {
    HWND16 h = mk("B", CLS, "b", WS_POPUP | WS_VISIBLE, 10, 10, 100, 60, 0, 0);
    check_layout_rect(get_rect(h, 1), 0, 0, 100, 60);
    SetWindowLong(h, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_BORDER);
    SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    check_layout_rect(get_rect(h, 1), 0, 0, 98, 58);
    DestroyWindow(h);
  }
  /* a user_nc_layout of a live window equals the computation for its style */
  g_top = make_top(TOPW);
  CHECK(user_nc_layout(g_top, &L));
  check_layout_rect(L.client, 4, 24, 196, 146);
  CHECK(!user_nc_layout(0x7777, &L));
  DestroyWindow(g_top);
}

static void test_calcsize_adjust(void) {
  FARPTR rp = far_alloc(sizeof(RECT16));
  RECT16 *r = LIN(rp);
  /* WM_NCCALCSIZE with wParam 0: the RECT goes from window to client rectangle. */
  g_top = make_top(TOPW);
  *r = (RECT16){100, 80, 300, 230};
  SendMessage(g_top, WM_NCCALCSIZE, 0, rp);
  check_layout_rect(*(RECT16 *)LIN(rp), 104, 104, 296, 226);
  /* the client area of the live window (client coordinates start at 0,0) */
  check_layout_rect(get_rect(g_top, 1), 0, 0, 192, 122);
  check_layout_rect(get_rect(g_top, 0), 100, 80, 300, 230);
  {
    POINT16 o = c2s(g_top, 0, 0);
    CHECK_EQ(o.x, 104);
    CHECK_EQ(o.y, 104);
  }
  DestroyWindow(g_top);
  /* WM_SIZE at creation carries the CLIENT size now (test_user_win expected the window size
   * while there was no non-client area). */
  log_clear();
  g_top = make_top(TOPW);
  {
    LogEnt *e = log_find("T", WM_SIZE, 0);
    CHECK(e != NULL);
    if (e) CHECK_EQ(e->lp, 192u | (122u << 16));
  }
  DestroyWindow(g_top);
  /* AdjustWindowRect: client rectangle -> window rectangle */
  *r = (RECT16){0, 0, 192, 122};
  AdjustWindowRect(rp, WS_OVERLAPPEDWINDOW, 0);
  check_layout_rect(*(RECT16 *)LIN(rp), -4, -24, 196, 126);
  *r = (RECT16){10, 10, 110, 60};
  AdjustWindowRect(rp, WS_POPUP | WS_BORDER, 0);
  check_layout_rect(*(RECT16 *)LIN(rp), 9, 9, 111, 61);
  *r = (RECT16){0, 0, 50, 50};
  AdjustWindowRect(rp, WS_CHILD, 0);
  check_layout_rect(*(RECT16 *)LIN(rp), 0, 0, 50, 50);
}

static void test_hittest_map(void) {
  /* The map for the 200x150 overlapped window (window coordinates); sizing-corner length is
   * SM_CXSIZE along each edge (UNVERIFIED). */
  g_top = make_top(TOPW);
  CHECK_EQ(HT(g_top, 100, 100), HTCLIENT);
  CHECK_EQ(HT(g_top, 4, 24), HTCLIENT);
  CHECK_EQ(HT(g_top, 195, 145), HTCLIENT);
  CHECK_EQ(HT(g_top, 100, 10), HTCAPTION);
  CHECK_EQ(HT(g_top, 50, 22), HTCAPTION);
  CHECK_EQ(HT(g_top, 100, 23), HTCAPTION);     /* the separator line belongs to the caption */
  CHECK_EQ(HT(g_top, 10, 10), HTSYSMENU);
  CHECK_EQ(HT(g_top, 4, 4), HTSYSMENU);
  CHECK_EQ(HT(g_top, 21, 22), HTSYSMENU);
  CHECK_EQ(HT(g_top, 22, 10), HTCAPTION);
  CHECK_EQ(HT(g_top, 159, 10), HTCAPTION);
  CHECK_EQ(HT(g_top, 160, 10), HTREDUCE);
  CHECK_EQ(HT(g_top, 177, 10), HTREDUCE);
  CHECK_EQ(HT(g_top, 178, 10), HTZOOM);
  CHECK_EQ(HT(g_top, 195, 10), HTZOOM);
  /* frame */
  CHECK_EQ(HT(g_top, 0, 60), HTLEFT);
  CHECK_EQ(HT(g_top, 3, 60), HTLEFT);
  CHECK_EQ(HT(g_top, 199, 60), HTRIGHT);
  CHECK_EQ(HT(g_top, 100, 0), HTTOP);
  CHECK_EQ(HT(g_top, 100, 3), HTTOP);
  CHECK_EQ(HT(g_top, 100, 149), HTBOTTOM);
  CHECK_EQ(HT(g_top, 0, 0), HTTOPLEFT);
  CHECK_EQ(HT(g_top, 17, 0), HTTOPLEFT);
  CHECK_EQ(HT(g_top, 18, 0), HTTOP);
  CHECK_EQ(HT(g_top, 1, 17), HTTOPLEFT);
  CHECK_EQ(HT(g_top, 1, 18), HTLEFT);
  CHECK_EQ(HT(g_top, 199, 0), HTTOPRIGHT);
  CHECK_EQ(HT(g_top, 182, 1), HTTOPRIGHT);
  CHECK_EQ(HT(g_top, 181, 1), HTTOP);
  CHECK_EQ(HT(g_top, 0, 149), HTBOTTOMLEFT);
  CHECK_EQ(HT(g_top, 199, 149), HTBOTTOMRIGHT);
  CHECK_EQ(HT(g_top, 198, 132), HTBOTTOMRIGHT);   /* corner zones are 18 pixels long: 150 - 18 = 132 */
  CHECK_EQ(HT(g_top, 198, 131), HTRIGHT);
  /* outside the rectangle */
  CHECK_EQ(HT(g_top, -1, 50), HTNOWHERE);
  CHECK_EQ(HT(g_top, 200, 50), HTNOWHERE);
  CHECK_EQ(HT(g_top, 100, 150), HTNOWHERE);
  DestroyWindow(g_top);

  /* scroll bars and the size box */
  g_top = make_top(TOPW | WS_VSCROLL | WS_HSCROLL);
  CHECK_EQ(HT(g_top, 100, 100), HTCLIENT);
  CHECK_EQ(HT(g_top, 188, 60), HTVSCROLL);
  CHECK_EQ(HT(g_top, 180, 24), HTVSCROLL);
  CHECK_EQ(HT(g_top, 195, 129), HTVSCROLL);
  CHECK_EQ(HT(g_top, 60, 138), HTHSCROLL);
  CHECK_EQ(HT(g_top, 4, 130), HTHSCROLL);
  CHECK_EQ(HT(g_top, 179, 145), HTHSCROLL);
  CHECK_EQ(HT(g_top, 188, 138), HTGROWBOX);
  DestroyWindow(g_top);

  /* border, dialog frame, no frame */
  g_top = mk("P", CLS, "p", WS_POPUP | WS_BORDER | WS_VISIBLE, 300, 300, 100, 50, 0, 0);
  CHECK_EQ(HT(g_top, 0, 25), HTBORDER);
  CHECK_EQ(HT(g_top, 50, 0), HTBORDER);
  CHECK_EQ(HT(g_top, 50, 25), HTCLIENT);
  DestroyWindow(g_top);
  g_top = mk("P", CLS, "p", WS_POPUP | WS_DLGFRAME | WS_VISIBLE, 300, 300, 100, 50, 0, 0);
  CHECK_EQ(HT(g_top, 2, 25), HTBORDER);   /* not sizable */
  CHECK_EQ(HT(g_top, 50, 25), HTCLIENT);
  DestroyWindow(g_top);
  g_top = mk("P", CLS, "p", WS_POPUP | WS_VISIBLE, 300, 300, 100, 50, 0, 0);
  CHECK_EQ(HT(g_top, 0, 0), HTCLIENT);
  DestroyWindow(g_top);
  /* a child window with a caption-less border is hit like any other window */
  {
    HWND16 p = mk("P", CLS, "p", WS_POPUP | WS_VISIBLE, 300, 300, 150, 100, 0, 0);
    HWND16 c = mk("C", CLS, "c", WS_CHILD | WS_BORDER | WS_VISIBLE, 10, 10, 60, 40, p, 1);
    CHECK_EQ(HT(c, 0, 20), HTBORDER);
    CHECK_EQ(HT(c, 30, 20), HTCLIENT);
    DestroyWindow(p);
  }
}

/* A hardware mouse event over the non-client area becomes WM_NC* with the hit code in wParam
 * and the SCREEN point in lParam (message.c 2724-2728). */
static void test_mouse_routing(void) {
  FARPTR p = far_alloc(sizeof(MSG16));
  const MSG16 *m = LIN(p);
  g_top = make_top(TOPW);
  rt_input_mouse(210, 90, 0, g_t += 10);          /* caption */
  CHECK_EQ(PeekMessage(p, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(m->message, WM_NCMOUSEMOVE);
  CHECK_EQ(m->wParam, HTCAPTION);
  CHECK_EQ(m->lParam, pt32(210, 90));
  rt_input_mouse(210, 90, RT_BTN_LEFT, g_t += 10);
  CHECK_EQ(PeekMessage(p, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(m->message, WM_NCLBUTTONDOWN);
  CHECK_EQ(m->wParam, HTCAPTION);
  CHECK_EQ(m->lParam, pt32(210, 90));
  rt_input_mouse(210, 90, 0, g_t += 10);
  CHECK_EQ(PeekMessage(p, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(m->message, WM_NCLBUTTONUP);
  rt_input_mouse(250, 150, 0, g_t += 10);         /* client */
  CHECK_EQ(PeekMessage(p, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(m->message, WM_MOUSEMOVE);
  CHECK_EQ(m->lParam, pt32(250 - 104, 150 - 104)); /* client coordinates */
  /* WM_SETCURSOR carries the hit code: sizing cursors on the frame */
  log_clear();
  rt_input_mouse(101, 150, 0, g_t += 10);         /* left edge */
  CHECK_EQ(PeekMessage(p, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(m->wParam, HTLEFT);
  CHECK_EQ(user_current_cursor(), LoadCursor(0, (FARPTR)IDC_SIZEWE_ID));
  DestroyWindow(g_top);
}

static void test_zoom_iconic(void) {
  WINDOWPLACEMENT16 *wp;
  FARPTR f = far_alloc(sizeof(WINDOWPLACEMENT16));
  HWND16 a, b;
  UNcLayout L;
  wp = LIN(f);
  a = make_top(TOPW);
  /* maximise: origin (-F,-F), size screen + 2F; the client area is screen wide and
   * SM_CYFULLSCREEN high (800x580 here) */
  ShowWindow(a, SW_SHOWMAXIMIZED);
  CHECK_EQ(IsZoomed(a), 1);
  check_layout_rect(get_rect(a, 0), -4, -4, 804, 604);
  check_layout_rect(get_rect(a, 1), 0, 0, 800, 580);
  CHECK_EQ(GetSystemMetrics(17), 580);
  CHECK_EQ(HT(a, 400, 10), HTCAPTION);
  CHECK_EQ(HT(a, 2, 100), HTLEFT);       /* the frame is still there (off screen) */
  wp->length = sizeof *wp;
  GetWindowPlacement(a, f);
  wp = LIN(f);
  CHECK_EQ(wp->showCmd, SW_SHOWMAXIMIZED);
  check_layout_rect(wp->rcNormalPosition, 100, 80, 300, 230);
  /* restore */
  ShowWindow(a, SW_RESTORE);
  CHECK_EQ(IsZoomed(a), 0);
  check_layout_rect(get_rect(a, 0), 100, 80, 300, 230);
  check_layout_rect(get_rect(a, 1), 0, 0, 192, 122);
  /* minimise: the window becomes an icon, 75 wide (SM_CXICONSPACING), along the bottom row */
  ShowWindow(a, SW_SHOWMINIMIZED);
  CHECK_EQ(IsIconic(a), 1);
  check_layout_rect(get_rect(a, 0), 0, 548, 75, 600);
  /* CLS has no class icon, so the application paints the icon itself and the iconic window has an
   * icon-sized client area (item 2; the old expectation here was an empty 0,0,0,0 client). The
   * 36x36 size is UNVERIFIED (32x32 icon plus the 2 px margin the game draws at, DrawIcon (2,2)). */
  check_layout_rect(get_rect(a, 1), 0, 0, 36, 36);
  CHECK_EQ(HT(a, 30, 10), HTCAPTION);
  CHECK(user_nc_layout(a, &L) && L.iconic);
  b = mk("U", CLS, "Second", TOPW, 400, 300, 150, 100, 0, 0);
  ShowWindow(b, SW_SHOWMINIMIZED);
  check_layout_rect(get_rect(b, 0), 75, 548, 150, 600);   /* the next slot */
  GetWindowPlacement(a, f);
  wp = LIN(f);
  CHECK_EQ(wp->showCmd, SW_SHOWMINIMIZED);
  check_layout_rect(wp->rcNormalPosition, 100, 80, 300, 230);
  ShowWindow(a, SW_RESTORE);
  check_layout_rect(get_rect(a, 0), 100, 80, 300, 230);
  CHECK_EQ(IsIconic(a), 0);
  /* restoring a minimised window that was maximised goes back to normal, not maximised: Win 3.1
   * keeps no "restore to maximised" flag (UNVERIFIED) */
  ShowWindow(b, SW_SHOWMAXIMIZED);
  ShowWindow(b, SW_SHOWMINIMIZED);
  ShowWindow(b, SW_RESTORE);
  CHECK_EQ(IsZoomed(b), 0);
  check_layout_rect(get_rect(b, 0), 400, 300, 550, 400);
  DestroyWindow(b);
  DestroyWindow(a);
}

static int g_cmds;
static uint16_t g_last_cmd;
static int hook_syscmd(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp) {
  (void)h; (void)lp;
  if (m == WM_SYSCOMMAND) { g_cmds++; g_last_cmd = wp; }
  if (m == WM_CLOSE) return 1;      /* observe, do not destroy */
  return 0;
}

static void test_syscommand(void) {
  g_top = make_top(TOPW);
  g_hook = hook_syscmd;
  log_clear();
  SendMessage(g_top, WM_SYSCOMMAND, SC_CLOSE, 0);
  CHECK(log_find("T", WM_CLOSE, 0) != NULL);
  SendMessage(g_top, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
  CHECK_EQ(IsZoomed(g_top), 1);
  SendMessage(g_top, WM_SYSCOMMAND, SC_RESTORE, 0);
  CHECK_EQ(IsZoomed(g_top), 0);
  SendMessage(g_top, WM_SYSCOMMAND, SC_MINIMIZE, 0);
  CHECK_EQ(IsIconic(g_top), 1);
  SendMessage(g_top, WM_SYSCOMMAND, SC_RESTORE, 0);
  CHECK_EQ(IsIconic(g_top), 0);
  /* the low four bits are not part of the command */
  SendMessage(g_top, WM_SYSCOMMAND, SC_MAXIMIZE | 0x0F, 0);
  CHECK_EQ(IsZoomed(g_top), 1);
  SendMessage(g_top, WM_SYSCOMMAND, SC_RESTORE, 0);
  /* Alt+F4 */
  log_clear();
  SendMessage(g_top, WM_SYSKEYDOWN, VK_F4_, 0x20000001UL);
  { /* SC_CLOSE is POSTED to the top-level window (Wine defwnd.c 2766-2771): process the queue first */
    FARPTR pm = far_alloc(sizeof(MSG16));
    for (int guard = 0; guard < 50 && PeekMessage(pm, 0, 0, 0, PM_REMOVE); guard++) DispatchMessage(pm);
  }
  CHECK(log_find("T", WM_SYSCOMMAND, 0) != NULL || log_find("T", WM_CLOSE, 0) != NULL);
  g_hook = NULL;
  DestroyWindow(g_top);
}

static int g_sysmenu_calls;
static HWND16 g_sysmenu_hwnd;
static bool sysmenu_hook(HWND16 h, int16_t x, int16_t y) {
  (void)x; (void)y;
  g_sysmenu_calls++;
  g_sysmenu_hwnd = h;
  return true;
}

static void test_sysmenu(void) {
  USysItem it[10];
  unsigned n;
  g_top = make_top(TOPW);
  n = user_sysmenu_items(g_top, it);
  /* Restore, Move, Size, Minimize, Maximize, -, Close, -, Switch To... */
  CHECK_EQ(n, 9);
  CHECK_EQ(it[0].id, SC_RESTORE);  CHECK_EQ(it[0].enabled, 0);   /* normal window: nothing to restore */
  CHECK_EQ(it[1].id, SC_MOVE);     CHECK_EQ(it[1].enabled, 1);
  CHECK_EQ(it[2].id, SC_SIZE);     CHECK_EQ(it[2].enabled, 1);
  CHECK_EQ(it[3].id, SC_MINIMIZE); CHECK_EQ(it[3].enabled, 1);
  CHECK_EQ(it[4].id, SC_MAXIMIZE); CHECK_EQ(it[4].enabled, 1);
  CHECK_EQ(it[5].id, 0);
  CHECK_EQ(it[6].id, SC_CLOSE);    CHECK_EQ(it[6].enabled, 1);
  CHECK_EQ(it[7].id, 0);
  CHECK_EQ(it[8].id, SC_TASKLIST); CHECK_EQ(it[8].enabled, 1);
  CHECK(strstr(it[8].text, "Switch To") != NULL);
  CHECK(strstr(it[6].text, "Close") != NULL);
  ShowWindow(g_top, SW_SHOWMAXIMIZED);
  n = user_sysmenu_items(g_top, it);
  CHECK_EQ(it[0].enabled, 1);      /* Restore */
  CHECK_EQ(it[1].enabled, 0);      /* Move */
  CHECK_EQ(it[2].enabled, 0);      /* Size */
  CHECK_EQ(it[3].enabled, 1);
  CHECK_EQ(it[4].enabled, 0);      /* Maximize */
  ShowWindow(g_top, SW_SHOWMINIMIZED);
  n = user_sysmenu_items(g_top, it);
  CHECK_EQ(it[0].enabled, 1);
  CHECK_EQ(it[3].enabled, 0);      /* Minimize */
  CHECK_EQ(it[4].enabled, 1);
  ShowWindow(g_top, SW_RESTORE);
  DestroyWindow(g_top);
  /* no min/max box, no thick frame */
  g_top = mk("D", CLS, "d", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 200, 200, 200, 100, 0, 0);
  n = user_sysmenu_items(g_top, it);
  CHECK_EQ(it[2].enabled, 0);
  CHECK_EQ(it[3].enabled, 0);
  CHECK_EQ(it[4].enabled, 0);
  CHECK_EQ(it[6].enabled, 1);
  DestroyWindow(g_top);

  /* a click on the box offers the pop-up to the menu task's hook; double click closes */
  g_top = make_top(TOPW);
  user_sysmenu_hook = sysmenu_hook;
  g_sysmenu_calls = 0;
  sm(110, 90, RT_BTN_LEFT); sm(110, 90, 0);
  run_script();
  CHECK_EQ(g_sysmenu_calls, 1);
  CHECK_EQ(g_sysmenu_hwnd, g_top);
  user_sysmenu_hook = NULL;
  g_hook = hook_syscmd;
  log_clear();
  sm(110, 90, RT_BTN_LEFT); sm(110, 90, 0); sm(110, 90, RT_BTN_LEFT); sm(110, 90, 0);
  run_script();
  CHECK(log_find("T", WM_NCLBUTTONDBLCLK, 0) != NULL);
  CHECK(log_find("T", WM_CLOSE, 0) != NULL);
  g_hook = NULL;
  DestroyWindow(g_top);
}

static void test_caption_buttons(void) {
  g_top = make_top(TOPW);
  /* press and release on the maximise button */
  sm(285, 90, RT_BTN_LEFT); sm(285, 90, 0);
  run_script();
  CHECK_EQ(IsZoomed(g_top), 1);
  /* in the maximised window the same button restores: it is at the right end of the caption */
  sm(800 - 4 - 9, -4 + 4 + 5, RT_BTN_LEFT); sm(800 - 4 - 9, -4 + 4 + 5, 0);
  run_script();
  CHECK_EQ(IsZoomed(g_top), 0);
  check_layout_rect(get_rect(g_top, 0), 100, 80, 300, 230);
  /* press on minimise, drag off the button, release: nothing happens */
  sm(269, 90, RT_BTN_LEFT); sm(400, 300, RT_BTN_LEFT); sm(400, 300, 0);
  run_script();
  CHECK_EQ(IsIconic(g_top), 0);
  /* press on minimise, release on it: iconic */
  sm(269, 90, RT_BTN_LEFT); sm(269, 90, 0);
  run_script();
  CHECK_EQ(IsIconic(g_top), 1);
  /* double click on the icon restores it */
  {
    RECT16 r = get_rect(g_top, 0);
    int x = r.left + 20, y = r.top + 10;
    sm(x, y, RT_BTN_LEFT); sm(x, y, 0); sm(x, y, RT_BTN_LEFT); sm(x, y, 0);
    run_script();
  }
  CHECK_EQ(IsIconic(g_top), 0);
  /* double click on the caption maximises, again restores */
  sm(210, 90, RT_BTN_LEFT); sm(210, 90, 0); sm(210, 90, RT_BTN_LEFT); sm(210, 90, 0);
  run_script();
  CHECK_EQ(IsZoomed(g_top), 1);
  ShowWindow(g_top, SW_RESTORE);
  DestroyWindow(g_top);
}

static void test_move_size_loops(void) {
  static uint8_t before[300 * 180];
  g_top = make_top(TOPW);
  /* move: drag the caption by (+20,+10) */
  sm(210, 90, RT_BTN_LEFT); sm(230, 100, RT_BTN_LEFT); sm(230, 100, 0);
  run_script();
  check_layout_rect(get_rect(g_top, 0), 120, 90, 320, 240);
  /* the outline is drawn with an XOR raster op and erased with the same: Escape cancels and
   * leaves the screen as it was */
  for (int y = 0; y < 180; y++)
    for (int x = 0; x < 300; x++) before[y * 300 + x] = px(100 + x, 80 + y);
  sm(230, 100, RT_BTN_LEFT);          /* press on the caption of the moved window (120,90): (230,100) */
  sm(300, 120, RT_BTN_LEFT);          /* drag */
  sk(VK_ESCAPE);
  run_script();
  check_layout_rect(get_rect(g_top, 0), 120, 90, 320, 240);    /* not moved */
  for (int y = 0; y < 180; y++)
    for (int x = 0; x < 300; x++) CHECK_EQ(px(100 + x, 80 + y), before[y * 300 + x]);
  sm(300, 120, 0);
  run_script();
  /* a window can be moved (without losing the loop) by the caption of a different window only
   * when it is the hit target: moving does not change the size */
  check_layout_rect(get_rect(g_top, 0), 120, 90, 320, 240);

  /* size: bottom-right corner by (+50,+30) */
  sm(319, 239, RT_BTN_LEFT); sm(369, 269, RT_BTN_LEFT); sm(369, 269, 0);
  run_script();
  check_layout_rect(get_rect(g_top, 0), 120, 90, 370, 270);
  /* size: right edge dragged far to the left stops at SM_CXMINTRACK */
  sm(369, 180, RT_BTN_LEFT); sm(0, 180, RT_BTN_LEFT); sm(0, 180, 0);
  run_script();
  {
    RECT16 r = get_rect(g_top, 0);
    CHECK_EQ(r.right - r.left, GetSystemMetrics(34));
    CHECK_EQ(r.left, 120);
  }
  /* size: top-left corner changes position and size, the opposite corner stays */
  {
    RECT16 r0 = get_rect(g_top, 0);
    sm(r0.left + 1, r0.top + 1, RT_BTN_LEFT); sm(r0.left - 9, r0.top - 19, RT_BTN_LEFT); sm(r0.left - 9, r0.top - 19, 0);
    run_script();
    RECT16 r1 = get_rect(g_top, 0);
    CHECK_EQ(r1.right, r0.right);
    CHECK_EQ(r1.bottom, r0.bottom);
    CHECK_EQ(r1.left, r0.left - 10);
    CHECK_EQ(r1.top, r0.top - 20);
  }
  DestroyWindow(g_top);
}

/* ---- scroll bars ---- */

typedef struct ScRec { uint16_t msg, wp; uint32_t lp; } ScRec;
static ScRec g_sc[64];
static int g_nsc;
static int g_follow;   /* the application stores THUMBTRACK positions with SetScrollPos */
static int hook_scroll(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp) {
  if (m == WM_VSCROLL || m == WM_HSCROLL) {
    if (g_nsc < 64) g_sc[g_nsc++] = (ScRec){m, wp, lp};
    if (g_follow && wp == SB_THUMBTRACK) SetScrollPos(h, m == WM_VSCROLL ? SB_VERT : SB_HORZ, (int16_t)(lp & 0xFFFF), 1);
    return 1;
  }
  return 0;
}

static void test_scroll_api(void) {
  FARPTR pm = far_alloc(4), px_ = far_alloc(4);
  int16_t *lo = LIN(pm), *hi = LIN(px_);
  g_top = make_top(TOPW | WS_VSCROLL | WS_HSCROLL);
  /* default range 0..100, position 0 (UNVERIFIED: Wine's default) */
  GetScrollRange(g_top, SB_VERT, pm, px_);
  CHECK_EQ(*lo, 0); CHECK_EQ(*hi, 100);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 0);
  SetScrollRange(g_top, SB_VERT, 10, 50, 1);
  GetScrollRange(g_top, SB_VERT, pm, px_);
  CHECK_EQ(*lo, 10); CHECK_EQ(*hi, 50);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 10);       /* the position is clamped into the new range */
  CHECK_EQ(SetScrollPos(g_top, SB_VERT, 30, 1), 10); /* returns the previous position */
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 30);
  CHECK_EQ(SetScrollPos(g_top, SB_VERT, 500, 1), 30);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 50);        /* clamped to the maximum */
  CHECK_EQ(SetScrollPos(g_top, SB_VERT, -9, 0), 50);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 10);        /* clamped to the minimum */
  /* the horizontal bar is independent */
  CHECK_EQ(GetScrollPos(g_top, SB_HORZ), 0);
  SetScrollRange(g_top, SB_HORZ, 0, 255, 0);
  GetScrollRange(g_top, SB_HORZ, pm, px_);
  CHECK_EQ(*hi, 255);
  CHECK_EQ(SetScrollPos(g_top, SB_HORZ, 128, 1), 0);
  /* a range wider than 32767 is refused (Windows 3.1 documents the limit) */
  SetScrollRange(g_top, SB_VERT, -30000, 30000, 0);
  GetScrollRange(g_top, SB_VERT, pm, px_);
  CHECK_EQ(*lo, 10); CHECK_EQ(*hi, 50);
  /* the state belongs to the window slot: a new window starts with the default */
  DestroyWindow(g_top);
  g_top = make_top(TOPW | WS_VSCROLL);
  GetScrollRange(g_top, SB_VERT, pm, px_);
  CHECK_EQ(*lo, 0); CHECK_EQ(*hi, 100);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 0);
  /* ShowScrollBar toggles the style bit and re-runs the non-client calculation */
  ShowScrollBar(g_top, SB_VERT, 0);
  CHECK_EQ(GetWindowLong(g_top, GWL_STYLE) & WS_VSCROLL, 0);
  check_layout_rect(get_rect(g_top, 1), 0, 0, 192, 122);
  ShowScrollBar(g_top, SB_VERT, 1);
  CHECK(GetWindowLong(g_top, GWL_STYLE) & WS_VSCROLL);
  check_layout_rect(get_rect(g_top, 1), 0, 0, 176, 122);
  DestroyWindow(g_top);
}

/* Item 1 (stars-0z4.9 review): SetScrollRange on a window bar (SB_HORZ / SB_VERT) shows the bar when
 * min < max and hides it when min == max, then re-runs the non-client calculation (Wine
 * SCROLL_SetScrollInfo, bars other than SB_CTL; UNVERIFIED against Windows 3.1). The game's scanner
 * window is WS_CHILD|WS_VISIBLE (0x50000000) with no scroll styles and calls
 * SetScrollRange(SB_HORZ, 1000, N, FALSE) then SetScrollRange(SB_VERT, 1000, N, TRUE). */
static void test_scroll_range_shows_bar(void) {
  HWND16 c;
  g_top = make_top(TOPW);
  c = mk("C", CLS, "scan", 0x50000000u, 5, 6, 100, 80, g_top, 9);
  CHECK(c != 0);
  CHECK_RECT(get_rect(c, 1), 0, 0, 100, 80);
  CHECK_EQ(GetWindowLong(c, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL), 0);
  SetScrollRange(c, SB_HORZ, 1000, 1600, 0);
  CHECK(GetWindowLong(c, GWL_STYLE) & WS_HSCROLL);
  CHECK_RECT(get_rect(c, 1), 0, 0, 100, 64);          /* the horizontal bar takes 16 px of height */
  SetScrollRange(c, SB_VERT, 1000, 1600, 1);
  CHECK(GetWindowLong(c, GWL_STYLE) & WS_VSCROLL);
  CHECK_RECT(get_rect(c, 1), 0, 0, 84, 64);           /* the vertical bar takes 16 px of width */
  /* min == max hides the bar again and gives the client area back */
  SetScrollRange(c, SB_VERT, 1000, 1000, 1);
  CHECK_EQ(GetWindowLong(c, GWL_STYLE) & WS_VSCROLL, 0);
  CHECK_RECT(get_rect(c, 1), 0, 0, 100, 64);
  SetScrollRange(c, SB_HORZ, 7, 7, 0);
  CHECK_EQ(GetWindowLong(c, GWL_STYLE) & WS_HSCROLL, 0);
  CHECK_RECT(get_rect(c, 1), 0, 0, 100, 80);
  /* a refused range (wider than 32767) changes nothing */
  SetScrollRange(c, SB_VERT, -30000, 30000, 0);
  CHECK_EQ(GetWindowLong(c, GWL_STYLE) & WS_VSCROLL, 0);
  DestroyWindow(g_top);
}

/* Vertical bar of the 200x150 window at (100,80): bar x 180..196, y 24..130 in window coordinates.
 * Up arrow y 24..40, down arrow y 114..130, trough 40..114 (74 px), thumb 16 tall: travel 58. */
#define VX (WX + 188)
#define VY(wy) (WY + (wy))

static void test_scroll_tracking(void) {
  g_top = make_top(TOPW | WS_VSCROLL | WS_HSCROLL);
  g_hook = hook_scroll;
  SetScrollPos(g_top, SB_VERT, 50, 0);
  /* up arrow: SB_LINEUP, then SB_ENDSCROLL on release; lParam low word = position, high = 0 */
  g_nsc = 0;
  sm(VX, VY(30), RT_BTN_LEFT); sm(VX, VY(30), 0);
  run_script();
  CHECK_EQ(g_nsc, 2);
  CHECK_EQ(g_sc[0].msg, WM_VSCROLL); CHECK_EQ(g_sc[0].wp, SB_LINEUP);
  CHECK_EQ(g_sc[1].wp, SB_ENDSCROLL);
  CHECK_EQ(g_sc[0].lp >> 16, 0);
  /* down arrow with auto-repeat: the first message at once, one more per timer message */
  g_nsc = 0;
  sm(VX, VY(120), RT_BTN_LEFT); st(g_top); st(g_top); sm(VX, VY(120), 0);
  run_script();
  CHECK_EQ(g_nsc, 4);
  CHECK_EQ(g_sc[0].wp, SB_LINEDOWN); CHECK_EQ(g_sc[1].wp, SB_LINEDOWN); CHECK_EQ(g_sc[2].wp, SB_LINEDOWN);
  CHECK_EQ(g_sc[3].wp, SB_ENDSCROLL);
  /* the repeat stops while the pointer is off the arrow */
  g_nsc = 0;
  sm(VX, VY(120), RT_BTN_LEFT); sm(VX - 100, VY(60), RT_BTN_LEFT); st(g_top); sm(VX, VY(120), RT_BTN_LEFT); st(g_top); sm(VX, VY(120), 0);
  run_script();
  CHECK_EQ(g_nsc, 3);       /* press, repeat after coming back, end */
  /* page clicks: position 50 puts the thumb at y 69..85; above it is page-up, below page-down */
  g_nsc = 0;
  sm(VX, VY(50), RT_BTN_LEFT); sm(VX, VY(50), 0);
  run_script();
  CHECK_EQ(g_sc[0].wp, SB_PAGEUP);
  g_nsc = 0;
  sm(VX, VY(100), RT_BTN_LEFT); sm(VX, VY(100), 0);
  run_script();
  CHECK_EQ(g_sc[0].wp, SB_PAGEDOWN);
  /* thumb drag: SB_THUMBTRACK for each move, SB_THUMBPOSITION on release, SB_ENDSCROLL; the
   * position is range * (thumb offset) / travel; the pointer offset inside the thumb is kept */
  g_nsc = 0;
  g_follow = 1;
  sm(VX, VY(77), RT_BTN_LEFT);           /* thumb centre of position 50 (y 69..85) */
  sm(VX, VY(48), RT_BTN_LEFT);           /* 29 up: thumb top 40 = minimum */
  sm(VX, VY(106), RT_BTN_LEFT);          /* thumb top 98 = maximum */
  sm(VX, VY(77), 0);                     /* back to the middle, released */
  run_script();
  g_follow = 0;
  CHECK_EQ(g_nsc, 5);
  CHECK_EQ(g_sc[0].wp, SB_THUMBTRACK); CHECK_EQ(g_sc[0].lp & 0xFFFF, 0);
  CHECK_EQ(g_sc[1].wp, SB_THUMBTRACK); CHECK_EQ(g_sc[1].lp & 0xFFFF, 100);
  CHECK_EQ(g_sc[2].wp, SB_THUMBTRACK); CHECK_EQ(g_sc[2].lp & 0xFFFF, 50);
  CHECK_EQ(g_sc[3].wp, SB_THUMBPOSITION); CHECK_EQ(g_sc[3].lp & 0xFFFF, 50);
  CHECK_EQ(g_sc[4].wp, SB_ENDSCROLL);
  CHECK_EQ(GetScrollPos(g_top, SB_VERT), 50);
  /* horizontal bar: same codes in WM_HSCROLL; bar y 130..146, arrows x 4..20 and 164..180 */
  g_nsc = 0;
  sm(WX + 10, WY + 138, RT_BTN_LEFT); sm(WX + 10, WY + 138, 0);
  run_script();
  CHECK_EQ(g_sc[0].msg, WM_HSCROLL); CHECK_EQ(g_sc[0].wp, SB_LINEUP);   /* left arrow = LINEUP */
  g_nsc = 0;
  sm(WX + 170, WY + 138, RT_BTN_LEFT); sm(WX + 170, WY + 138, 0);
  run_script();
  CHECK_EQ(g_sc[0].msg, WM_HSCROLL); CHECK_EQ(g_sc[0].wp, SB_LINEDOWN);
  /* minimum == maximum HIDES a window bar (it is not merely disabled: Wine SCROLL_SetScrollInfo,
   * UNVERIFIED); the old expectation treated it as a disabled bar. A hidden bar sends nothing. */
  SetScrollRange(g_top, SB_VERT, 5, 5, 0);
  CHECK_EQ(GetWindowLong(g_top, GWL_STYLE) & WS_VSCROLL, 0);
  g_nsc = 0;
  sm(VX, VY(30), RT_BTN_LEFT); sm(VX, VY(30), 0);
  run_script();
  CHECK_EQ(g_nsc, 0);
  /* shown again by ShowScrollBar it is a disabled bar and still sends nothing */
  ShowScrollBar(g_top, SB_VERT, 1);
  CHECK(GetWindowLong(g_top, GWL_STYLE) & WS_VSCROLL);
  g_nsc = 0;
  sm(VX, VY(30), RT_BTN_LEFT); sm(VX, VY(30), 0);
  run_script();
  CHECK_EQ(g_nsc, 0);
  g_hook = NULL;
  DestroyWindow(g_top);
}

/* ---- cursors and icons ---- */

static int count_set(const uint8_t *m, int n) {
  int c = 0;
  for (int i = 0; i < n; i++) c += m[i] != 0;
  return c;
}

static void test_cursors(void) {
  static const char *names[] = {"SCANNERCUR", "OPENGRABCUR", "CLOSEGRABCUR", "SCANNERADD"};
  static const int ids[] = {121, 122, 258, 260, 263, 264, 265};
  static const int sys[] = {IDC_ARROW_ID, IDC_IBEAM_ID, IDC_WAIT_ID, IDC_CROSS_ID, IDC_UPARROW_ID, IDC_SIZE_ID,
                            IDC_ICON_ID, IDC_SIZENWSE_ID, IDC_SIZENESW_ID, IDC_SIZEWE_ID, IDC_SIZENS_ID};
  HCURSOR16 seen[32];
  int n = 0;
  uint8_t am[1024], xm[1024];
  int16_t hx, hy;
  const uint16_t inst = MEM_SEL_DGROUP;
  /* the game's cursors come from its GROUP_CURSOR resources */
  for (unsigned i = 0; i < 4; i++) {
    HCURSOR16 c = LoadCursor(inst, fstr(names[i]));
    CHECK(c != 0);
    CHECK_EQ(LoadCursor(inst, fstr(names[i])), c);        /* stable handle per name */
    seen[n++] = c;
    CHECK(user_cursor_masks(c, am, xm, &hx, &hy));
    CHECK(hx >= 0 && hx < 32 && hy >= 0 && hy < 32);
    CHECK(count_set(am, 1024) < 1024);                     /* some pixel is not transparent */
  }
  for (unsigned i = 0; i < 7; i++) {
    HCURSOR16 c = LoadCursor(inst, (FARPTR)ids[i]);
    CHECK(c != 0);
    seen[n++] = c;
    CHECK(user_cursor_masks(c, am, xm, &hx, &hy));
  }
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) CHECK(seen[i] != seen[j]);
  CHECK_EQ(LoadCursor(inst, fstr("NOSUCHCURSOR")), 0);
  /* the system cursors */
  for (unsigned i = 0; i < sizeof sys / sizeof sys[0]; i++) {
    HCURSOR16 c = LoadCursor(0, (FARPTR)sys[i]);
    CHECK(c != 0);
    CHECK(user_cursor_masks(c, am, xm, &hx, &hy));
    {
      int visible = 0;       /* a pixel is drawn when AND is 0 or XOR is 1 */
      for (int k = 0; k < 1024; k++) visible += (!am[k] || xm[k]);
      CHECK(visible > 10);
    }
  }
  /* arrow: hot spot at the tip */
  user_cursor_masks(LoadCursor(0, (FARPTR)IDC_ARROW_ID), am, xm, &hx, &hy);
  CHECK_EQ(hx, 0);
  CHECK_EQ(hy, 0);
  CHECK_EQ(am[0], 0);       /* the tip pixel is drawn (AND 0) */
  CHECK_EQ(xm[0], 0);       /* black */
  CHECK_EQ(am[31 * 32 + 31], 1);   /* the far corner is transparent */
  /* the I-beam inverts what is under it: AND 1, XOR 1 */
  user_cursor_masks(LoadCursor(0, (FARPTR)IDC_IBEAM_ID), am, xm, &hx, &hy);
  {
    int inv = 0;
    for (int i = 0; i < 1024; i++) inv += (am[i] && xm[i]);
    CHECK(inv > 0);
  }
  /* SetCursor / GetCursor bookkeeping */
  {
    HCURSOR16 a = LoadCursor(0, (FARPTR)IDC_CROSS_ID), prev = SetCursor(a);
    CHECK_EQ(user_current_cursor(), a);
    CHECK_EQ(SetCursor(prev), a);
  }
  /* DestroyCursor frees an application cursor; the handle no longer has masks */
  {
    HCURSOR16 c = LoadCursor(inst, fstr("SCANNERCUR"));
    CHECK_EQ(DestroyCursor(c), 1);
    CHECK(!user_cursor_masks(c, am, xm, &hx, &hy));
    CHECK(LoadCursor(inst, fstr("SCANNERCUR")) != 0);      /* can be loaded again */
  }
}

static void test_cursor_present(void) {
  /* The software cursor is composited at present time: AND first, then XOR, at pointer - hot spot. */
  static uint32_t out[GDI_SCREEN_W * GDI_SCREEN_H], plain[GDI_SCREEN_W * GDI_SCREEN_H];
  HCURSOR16 c = LoadCursor(0, (FARPTR)IDC_IBEAM_ID), prev;
  uint8_t am[1024], xm[1024];
  int16_t hx, hy;
  int px0 = 300, py0 = 400, bad = 0, inv = 0, blk = 0, wht = 0, keep = 0;
  static uint8_t fbsnap[GDI_SCREEN_W * GDI_SCREEN_H];
  HDC16 dc = GetDC(0);
  PatBlt(dc, 280, 380, 80, 80, WHITENESS);
  SelectObject(dc, GetStockObject(GRAY_BRUSH));
  PatBlt(dc, 290, 390, 30, 40, PATCOPY);     /* a 128 gray patch under part of the cursor */
  ReleaseDC(0, dc);
  prev = SetCursor(c);
  memcpy(fbsnap, fb_pixels(), sizeof fbsnap);
  rt_input_mouse((int16_t)px0, (int16_t)py0, 0, g_t += 10);
  user_cursor_masks(c, am, xm, &hx, &hy);
  user_present(out);
  fb_present(plain);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      int sx = px0 - hx + x, sy = py0 - hy + y;
      uint32_t bg = plain[sy * GDI_SCREEN_W + sx], got = out[sy * GDI_SCREEN_W + sx], want;
      uint8_t a = am[y * 32 + x], b = xm[y * 32 + x];
      if (a && !b) { want = bg; keep++; }
      else if (!a && !b) { want = 0xFF000000u; blk++; }
      else if (!a && b) { want = 0xFFFFFFFFu; wht++; }
      else { want = 0xFF000000u | (~bg & 0x00FFFFFFu); inv++; }
      if (got != want) bad++;
    }
  (void)blk;
  (void)wht;
  CHECK_EQ(bad, 0);
  CHECK(inv > 0);                      /* XOR=1 and AND=1 inverts */
  CHECK(keep > 0);                     /* AND=1, XOR=0 leaves the pixel */
  /* outside the 32x32 box nothing changes */
  CHECK_EQ(out[(py0 - hy - 1) * GDI_SCREEN_W + px0], plain[(py0 - hy - 1) * GDI_SCREEN_W + px0]);
  /* the framebuffer is untouched */
  CHECK_EQ(memcmp(fbsnap, fb_pixels(), sizeof fbsnap), 0);
  /* ShowCursor(FALSE) hides it; the display counter goes up and down */
  CHECK_EQ(ShowCursor(0), -1);
  user_present(out);
  CHECK_EQ(memcmp(out, plain, sizeof out), 0);
  CHECK_EQ(ShowCursor(1), 0);
  user_present(out);
  CHECK(memcmp(out, plain, sizeof out) != 0);
  SetCursor(prev);
}

/* The 20 static colours are all a plain RGB can map to (runtime/framebuffer.c). */
static uint32_t nearest_static(uint32_t rgb) {
  static const int idx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
  long best = 1L << 30;
  uint32_t bc = 0;
  for (int k = 0; k < 20; k++) {
    uint32_t c = fb_palette_entry((unsigned)idx[k]);
    long dr = (long)(c & 255) - (long)(rgb & 255), dg = (long)((c >> 8) & 255) - (long)((rgb >> 8) & 255),
         db = (long)((c >> 16) & 255) - (long)((rgb >> 16) & 255);
    long d = dr * dr + dg * dg + db * db;
    if (d < best) { best = d; bc = c; }
  }
  return bc;
}

static void test_icons(void) {
  static const char *names[] = {"STARSICO", "HOSTICO", "WAITICO", "BANG1ICO", "BANG2ICO", "BANG3ICO",
                                "TORP1ICO", "TORP2ICO", "TORP3ICO", "TORP4ICO"};
  static const int sys[] = {IDI_APPLICATION_ID, IDI_HAND_ID, IDI_QUESTION_ID, IDI_EXCLAMATION_ID, IDI_ASTERISK_ID};
  const uint16_t inst = MEM_SEL_DGROUP;
  HICON16 seen[16];
  int n = 0;
  uint32_t rgb[1024];
  uint8_t am[1024];
  for (unsigned i = 0; i < 10; i++) {
    HICON16 h = LoadIcon(inst, fstr(names[i]));
    CHECK(h != 0);
    CHECK_EQ(LoadIcon(inst, fstr(names[i])), h);
    seen[n++] = h;
    CHECK(user_icon_pixels(h, rgb, am));
    CHECK(count_set(am, 1024) < 1024);
  }
  for (unsigned i = 0; i < 5; i++) {
    HICON16 h = LoadIcon(0, (FARPTR)sys[i]);
    CHECK(h != 0);
    seen[n++] = h;
    CHECK(user_icon_pixels(h, rgb, am));
    CHECK(count_set(am, 1024) > 100 && count_set(am, 1024) < 1000);
  }
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) CHECK(seen[i] != seen[j]);
  CHECK_EQ(LoadIcon(inst, fstr("NOSUCHICON")), 0);
  /* DrawIcon: opaque pixels take the icon colour, transparent ones keep the background */
  {
    HICON16 h = LoadIcon(inst, fstr("STARSICO"));
    HDC16 dc = GetDC(0);
    int bad = 0, opaque = 0;
    SelectObject(dc, GetStockObject(LTGRAY_BRUSH));
    PatBlt(dc, 500, 400, 40, 40, PATCOPY);
    CHECK_EQ(DrawIcon(dc, 504, 404, h), 1);
    ReleaseDC(0, dc);
    CHECK(user_icon_pixels(h, rgb, am));
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++) {
        uint8_t got = px(504 + x, 404 + y);
        if (am[y * 32 + x]) {
          if (fb_palette_entry(got) != fb_palette_entry(px(500, 400))) bad++;
        } else {
          opaque++;
          if (fb_palette_entry(got) != nearest_static(rgb[y * 32 + x])) bad++;
        }
      }
    CHECK_EQ(bad, 0);
    CHECK(opaque > 0);
  }
  /* DestroyIcon */
  {
    HICON16 h = LoadIcon(inst, fstr("HOSTICO"));
    CHECK_EQ(DestroyIcon(h), 1);
    CHECK(!user_icon_pixels(h, rgb, am));
  }
}

/* Item 2: an iconic window whose class has no icon (the game's "starsframe" class, hIcon 0,
 * FUN_1018_0000) paints its iconic state itself (FRAMEWNDPROC: BeginPaint + DrawIcon(hdc, 2, 2,
 * STARSICO / HOSTICO / WAITICO)). So it gets an icon-sized client area (36x36, UNVERIFIED: a 32x32
 * icon plus the 2 px margin) and WM_PAINT; the title is drawn below that client. A class WITH an
 * icon keeps an empty client and the runtime draws the icon. */
static int g_npaint_icon;
static HICON16 g_paint_icon;
static int hook_icon_paint(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp) {
  (void)wp; (void)lp;
  if (m == WM_PAINT) {
    FARPTR ps = far_alloc(sizeof(PAINTSTRUCT16));
    HDC16 dc = BeginPaint(h, ps);
    DrawIcon(dc, 2, 2, g_paint_icon);
    EndPaint(h, ps);
    g_npaint_icon++;
    return 1;
  }
  return 0;
}

static void test_iconic_no_class_icon(void) {
  const uint16_t inst = MEM_SEL_DGROUP;
  HWND16 a, b;
  RECT16 wr;
  POINT16 o;
  uint32_t rgb[1024];
  uint8_t am[1024];
  int bad = 0, opaque = 0, title = 0;
  const uint32_t bg = nearest_static(GetSysColor(1));
  g_paint_icon = LoadIcon(inst, fstr("STARSICO"));
  CHECK(g_paint_icon != 0 && user_icon_pixels(g_paint_icon, rgb, am));
  user_paint_desktop();
  a = make_top(TOPW);
  UpdateWindow(a);
  g_hook = hook_icon_paint;
  g_npaint_icon = 0;
  ShowWindow(a, SW_SHOWMINIMIZED);
  UpdateWindow(a);
  g_hook = NULL;
  CHECK_EQ(IsIconic(a), 1);
  CHECK_EQ(g_npaint_icon, 1);                         /* WM_PAINT reached the application */
  check_layout_rect(get_rect(a, 1), 0, 0, 36, 36);
  wr = get_rect(a, 0);
  o = c2s(a, 0, 0);
  CHECK_EQ(o.x, wr.left + (75 - 36) / 2);             /* client centred horizontally, top aligned */
  CHECK_EQ(o.y, wr.top);
  /* every opaque pixel of the icon is where DrawIcon(hdc, 2, 2) put it, in client coordinates */
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      if (!am[y * 32 + x]) {
        opaque++;
        if (fb_palette_entry(px(o.x + 2 + x, o.y + 2 + y)) != nearest_static(rgb[y * 32 + x])) bad++;
      }
  CHECK(opaque > 100);
  CHECK_EQ(bad, 0);
  /* the title lies below the client (rows 36..51 of the 75x52 icon area) */
  for (int y = 36; y < 52; y++)
    for (int x = 0; x < 75; x++)
      if (fb_palette_entry(px(wr.left + x, wr.top + y)) != bg) title++;
  CHECK(title > 10);
  /* a caption repaint while minimised (title change, activation change) must not erase the icon the
   * application drew in its client; no WM_PAINT follows */
  SetWindowText(a, fstr("x"));
  SendMessage(a, WM_NCACTIVATE, 0, 0);
  bad = 0;
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      if (!am[y * 32 + x] && fb_palette_entry(px(o.x + 2 + x, o.y + 2 + y)) != nearest_static(rgb[y * 32 + x])) bad++;
  CHECK_EQ(bad, 0);
  ShowWindow(a, SW_RESTORE);
  CHECK_EQ(IsIconic(a), 0);
  DestroyWindow(a);
  /* a class with an icon: empty client, the runtime draws the icon (no WM_PAINT to the application) */
  {
    FARPTR pw = far_alloc(sizeof(WNDCLASS16));
    WNDCLASS16 *w = LIN(pw);
    w->lpfnWndProc = FP_PROC_A;
    w->hIcon = g_paint_icon;
    w->lpszClassName = fstr("NCICONCLS");
    w->hInstance = 0x1234;
    CHECK(RegisterClass(pw) != 0);
  }
  b = mk("B", "NCICONCLS", "Icon", TOPW, WX, WY, WW, WH, 0, 0);
  ShowWindow(b, SW_SHOWMINIMIZED);
  check_layout_rect(get_rect(b, 1), 0, 0, 0, 0);
  DestroyWindow(b);
}

/* ---- MessageBox flow ---- */

static int g_mb_stage;
static void (*g_mb_step)(int stage);
static bool mb_wait(void) {
  if (!g_mb_step) return false;
  g_mb_step(g_mb_stage++);
  return feeder();
}

static int16_t run_mb(uint16_t type, const char *text, const char *cap, void (*step)(int), HWND16 owner) {
  int16_t r;
  g_mb_stage = 0;
  g_mb_step = step;
  script_clear();
  user_wait_hook = mb_wait;
  r = MessageBox(owner, fstr(text), fstr(cap), type);
  user_wait_hook = NULL;
  g_mb_step = NULL;
  user_input_exhausted = 0;
  script_clear();
  return r;
}

typedef struct MbInfo {
  HWND16 hwnd;
  int nbtn, def;
  RECT16 btn[3], icon, text;
  uint16_t ids[3];
} MbInfo;
static MbInfo g_mbi;

static void mb_capture(void) { CHECK(user_msgbox_info(&g_mbi.hwnd, &g_mbi.nbtn, g_mbi.btn, g_mbi.ids, &g_mbi.def, &g_mbi.icon, &g_mbi.text)); }

static void step_enter(int s) { if (s == 0) { mb_capture(); sk(VK_RETURN); } }
static void step_esc(int s) { if (s == 0) { mb_capture(); sk(VK_ESCAPE); } }
static void step_y(int s) { if (s == 0) { mb_capture(); sk('Y'); } }
static void step_tab_enter(int s) { if (s == 0) { mb_capture(); sk(VK_TAB); sk(VK_RETURN); } }
static void step_esc_then_click_no(int s) {
  if (s == 0) { mb_capture(); sk(VK_ESCAPE); }
  else if (s == 1) {
    /* Escape did nothing (no Cancel button): the box is still there; click "No" */
    CHECK(IsWindow(g_mbi.hwnd));
    sm((g_mbi.btn[1].left + g_mbi.btn[1].right) / 2, (g_mbi.btn[1].top + g_mbi.btn[1].bottom) / 2, RT_BTN_LEFT);
    sm((g_mbi.btn[1].left + g_mbi.btn[1].right) / 2, (g_mbi.btn[1].top + g_mbi.btn[1].bottom) / 2, 0);
  }
}
static void step_click_second(int s) {
  if (s == 0) {
    int cx, cy;
    mb_capture();
    cx = (g_mbi.btn[1].left + g_mbi.btn[1].right) / 2;
    cy = (g_mbi.btn[1].top + g_mbi.btn[1].bottom) / 2;
    sm(cx, cy, RT_BTN_LEFT);
    sm(cx, cy, 0);
  }
}
static void step_press_drag_off(int s) {
  if (s == 0) {
    int cx, cy;
    mb_capture();
    cx = (g_mbi.btn[0].left + g_mbi.btn[0].right) / 2;
    cy = (g_mbi.btn[0].top + g_mbi.btn[0].bottom) / 2;
    sm(cx, cy, RT_BTN_LEFT);
    sm(cx, cy - 80, RT_BTN_LEFT);   /* off the button */
    sm(cx, cy - 80, 0);             /* released outside: no result yet */
    sk(VK_RETURN);                  /* the default button then answers */
  }
}

static void test_msgbox(void) {
  HWND16 owner = mk("O", CLS, "Owner", TOPW, 50, 50, 300, 200, 0, 0);
  unsigned wins = user_window_count();
  uint16_t ownerid_before = (uint16_t)IsWindowEnabled(owner);

  CHECK_EQ(run_mb(MB_OK, "Hello", "Caption", step_enter, owner), IDOK);
  CHECK_EQ(g_mbi.nbtn, 1);
  CHECK_EQ(g_mbi.ids[0], IDOK);
  CHECK_EQ(g_mbi.def, 0);
  CHECK_EQ(user_window_count(), wins);               /* the box is gone */
  CHECK_EQ(IsWindowEnabled(owner), ownerid_before);  /* the owner is enabled again */
  CHECK_EQ(GetActiveWindow(), owner);

  /* structure of OK/Cancel with a question icon: two equal buttons, centred under the text, the
   * icon left of the text, the whole box on screen and centred (UNVERIFIED layout) */
  CHECK_EQ(run_mb(MB_OKCANCEL | MB_ICONQUESTION, "Really quit this game now?", "Stars!", step_enter, owner), IDOK);
  CHECK_EQ(g_mbi.nbtn, 2);
  CHECK_EQ(g_mbi.ids[0], IDOK);
  CHECK_EQ(g_mbi.ids[1], IDCANCEL);
  CHECK_EQ(g_mbi.btn[0].right - g_mbi.btn[0].left, g_mbi.btn[1].right - g_mbi.btn[1].left);
  CHECK_EQ(g_mbi.btn[0].bottom - g_mbi.btn[0].top, g_mbi.btn[1].bottom - g_mbi.btn[1].top);
  CHECK_EQ(g_mbi.btn[0].top, g_mbi.btn[1].top);
  CHECK(g_mbi.btn[1].left > g_mbi.btn[0].right);
  CHECK_EQ(g_mbi.icon.right - g_mbi.icon.left, 32);
  CHECK_EQ(g_mbi.icon.bottom - g_mbi.icon.top, 32);
  CHECK(g_mbi.icon.right <= g_mbi.text.left);
  CHECK(g_mbi.text.bottom <= g_mbi.btn[0].top);
  CHECK(g_mbi.btn[0].left >= 0 && g_mbi.btn[1].right <= GDI_SCREEN_W);
  {
    int bl = g_mbi.btn[0].left, br = g_mbi.btn[1].right;
    CHECK(abs((bl + br) / 2 - GDI_SCREEN_W / 2) <= 1);   /* centred on the screen */
  }
  /* default button 2: Enter answers with the second */
  CHECK_EQ(run_mb(MB_YESNOCANCEL | MB_DEFBUTTON2, "Save?", "Stars!", step_enter, owner), IDNO);
  CHECK_EQ(g_mbi.nbtn, 3);
  CHECK_EQ(g_mbi.ids[0], IDYES);
  CHECK_EQ(g_mbi.ids[1], IDNO);
  CHECK_EQ(g_mbi.ids[2], IDCANCEL);
  CHECK_EQ(g_mbi.def, 1);
  /* the other sets */
  CHECK_EQ(run_mb(MB_ABORTRETRYIGNORE, "x", "x", step_enter, owner), IDABORT);
  CHECK_EQ(g_mbi.ids[1], IDRETRY);
  CHECK_EQ(g_mbi.ids[2], IDIGNORE);
  CHECK_EQ(run_mb(MB_RETRYCANCEL, "x", "x", step_enter, owner), IDRETRY);
  CHECK_EQ(run_mb(MB_YESNO, "x", "x", step_enter, owner), IDYES);
  /* Escape: Cancel when there is one, OK for a lone OK button, nothing for Yes/No */
  CHECK_EQ(run_mb(MB_OKCANCEL, "x", "x", step_esc, owner), IDCANCEL);
  CHECK_EQ(run_mb(MB_OK, "x", "x", step_esc, owner), IDOK);
  CHECK_EQ(run_mb(MB_YESNO, "x", "x", step_esc_then_click_no, owner), IDNO);
  /* mnemonics and Tab */
  CHECK_EQ(run_mb(MB_YESNO, "x", "x", step_y, owner), IDYES);
  CHECK_EQ(run_mb(MB_OKCANCEL, "x", "x", step_tab_enter, owner), IDCANCEL);
  /* mouse: click the second button; releasing outside a pressed button does nothing */
  CHECK_EQ(run_mb(MB_YESNO, "x", "x", step_click_second, owner), IDNO);
  CHECK_EQ(run_mb(MB_OKCANCEL, "x", "x", step_press_drag_off, owner), IDOK);
  /* ownerless box */
  CHECK_EQ(run_mb(MB_OK | MB_ICONEXCLAMATION, "no owner", "x", step_enter, 0), IDOK);
  /* the input ends while the box is up: 0, and the exhausted flag is left for the caller */
  CHECK_EQ(run_mb(MB_OK, "x", "x", NULL, owner), 0);
  CHECK_EQ(user_window_count(), wins);
  CHECK_EQ(IsWindowEnabled(owner), ownerid_before);
  DestroyWindow(owner);
}

/* Item 3: MessageBox resolves its owner to the top-level window. The game passes GetFocus(), often
 * a child window, and uses MB_TASKMODAL (0x2023, 0x2024, 0x2034). Wine's DIALOG_CreateIndirect walks
 * WS_CHILD parents up to the top-level window and disables that (UNVERIFIED against Windows 3.1).
 * MB_TASKMODAL with hwnd 0: Windows 3.1 disables the task's top-level windows; this runtime disables
 * nothing for it (UNVERIFIED, documented in docs/inventory.md section 13). */
static HWND16 g_probe_top, g_probe_child, g_probe_owner_seen;
static int g_probe_top_en, g_probe_child_en;
static void step_probe_enter(int s) {
  if (s == 0) {
    g_probe_top_en = IsWindowEnabled(g_probe_top);
    g_probe_child_en = IsWindowEnabled(g_probe_child);
    mb_capture();
    g_probe_owner_seen = GetParent(g_mbi.hwnd);
    sk(VK_RETURN);
  }
}

static void test_msgbox_toplevel_owner(void) {
  HWND16 top = mk("F", CLS, "Frame", TOPW, 50, 50, 300, 200, 0, 0);
  HWND16 kid = mk("K", CLS, "Kid", WS_CHILD | WS_VISIBLE, 5, 5, 100, 80, top, 5);
  unsigned wins = user_window_count();
  g_probe_top = top;
  g_probe_child = kid;
  SetFocus(kid);
  CHECK_EQ(GetFocus(), kid);
  /* a child as hwnd: the top-level frame is disabled while the box is up, not the child */
  g_probe_top_en = g_probe_child_en = -1;
  CHECK_EQ(run_mb(MB_OK | MB_ICONEXCLAMATION | MB_TASKMODAL, "text", "cap", step_probe_enter, kid), IDOK);
  CHECK_EQ(g_probe_top_en, 0);
  CHECK_EQ(g_probe_child_en, 1);
  CHECK_EQ(g_probe_owner_seen, top);                  /* the box is owned by the top-level window */
  CHECK_EQ(user_window_count(), wins);
  CHECK_EQ(IsWindowEnabled(top), 1);                  /* enabled again afterwards */
  CHECK_EQ(IsWindowEnabled(kid), 1);
  CHECK_EQ(GetActiveWindow(), top);
  /* the focus window before the box gets the focus back (UNVERIFIED: the owner's activation alone
   * would leave it on the frame) */
  CHECK_EQ(GetFocus(), kid);
  SetFocus(top);
  SetFocus(kid);                                      /* and it can be moved there again */
  CHECK_EQ(GetFocus(), kid);
  CHECK_EQ(GetActiveWindow(), top);
  /* the same through a grandchild */
  {
    HWND16 gk = mk("G", CLS, "Grand", WS_CHILD | WS_VISIBLE, 1, 1, 20, 20, kid, 6);
    g_probe_child = gk;
    CHECK_EQ(run_mb(MB_YESNO | MB_TASKMODAL, "text", "cap", step_probe_enter, gk), IDYES);
    CHECK_EQ(g_probe_top_en, 0);
    CHECK_EQ(g_probe_owner_seen, top);
    CHECK_EQ(IsWindowEnabled(top), 1);
  }
  /* MB_TASKMODAL with hwnd 0: nothing is disabled by itself (UNVERIFIED) */
  g_probe_child = kid;
  g_probe_top_en = -1;
  CHECK_EQ(run_mb(MB_OK | MB_TASKMODAL, "text", "cap", step_probe_enter, 0), IDOK);
  CHECK_EQ(g_probe_top_en, 1);
  CHECK_EQ(g_probe_owner_seen, 0);
  DestroyWindow(top);
}

/* Item 5: the box text is not cut at 255 characters (the old UMsgBox.text[256]); Windows has no such
 * limit. A text of 600 characters needs more lines than the first 255 characters do. */
static void test_msgbox_long_text(void) {
  char longt[640], shortt[640];
  int h_short, h_long;
  longt[0] = 0;
  for (int i = 0; i < 120; i++) strcat(longt, "word ");
  strncpy(shortt, longt, 255);
  shortt[255] = 0;
  CHECK_EQ(run_mb(MB_OK, shortt, "cap", step_enter, 0), IDOK);
  h_short = g_mbi.text.bottom - g_mbi.text.top;
  CHECK_EQ(run_mb(MB_OK, longt, "cap", step_enter, 0), IDOK);
  h_long = g_mbi.text.bottom - g_mbi.text.top;
  CHECK(h_long > h_short + 16);
}

/* Item 4: the game's frame is exactly 0x00CF0000 = WS_OVERLAPPEDWINDOW. The message-order checks of
 * test_user_win.c (create visible, create hidden then show/hide, destroy) were written with
 * WS_OVERLAPPED (= 0, a window without caption or border: this runtime's artefact; the SDK says an
 * overlapped window has a caption and border, and Wine's create_window corrects the style); this
 * repeats them for WS_OVERLAPPEDWINDOW. Same rules (Wine window.c, UNVERIFIED for Windows 3.1);
 * the difference is the non-client area: WM_SIZE carries the CLIENT size, and WM_NCPAINT comes
 * before WM_PAINT and WM_ERASEBKGND. */
static void test_overlappedwindow_message_order(void) {
  HWND16 p;
  LogEnt *e;
  RECT16 c;
  CHECK_EQ(WS_OVERLAPPEDWINDOW, 0x00CF0000u);
  CHECK_EQ(WS_OVERLAPPEDWINDOW, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
  /* created visible */
  log_clear();
  p = mk("P", CLS, "Main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 20, 300, 200, 0, 0);
  CHECK(p != 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCCALCSIZE P:CREATE P:SHOWWINDOW P:WINDOWPOSCHANGING "
            "P:NCACTIVATE P:ACTIVATE P:SETFOCUS P:WINDOWPOSCHANGED P:SIZE P:MOVE");
  e = log_find("P", WM_NCCREATE, 0);
  if (e) CHECK_EQ(e->cs.style, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
  c = get_rect(p, 1);
  CHECK_RECT(c, 0, 0, 292, 172);                       /* 300x200 minus 4 px frames and the 20 px caption */
  e = log_find("P", WM_SIZE, 0);
  if (e) {
    CHECK_EQ(e->wp, SIZE_RESTORED);
    CHECK_EQ(e->lp, 292u | (172u << 16));              /* the client size, not the window size */
  }
  e = log_find("P", WM_MOVE, 0);
  if (e) CHECK_EQ(e->lp, 14u | (44u << 16));           /* client origin: window (10,20) + frame 4, caption 20 */
  /* the non-client area is painted before the client: WM_NCPAINT, then WM_PAINT, whose BeginPaint
   * sends WM_ERASEBKGND */
  log_clear();
  UpdateWindow(p);
  CHECK_LOG("P:NCPAINT P:PAINT P:ERASEBKGND");
  CHECK_EQ(IsWindowVisible(p), 1);
  log_clear();
  CHECK_EQ(DestroyWindow(p), 1);
  CHECK_LOG("P:WINDOWPOSCHANGING P:WINDOWPOSCHANGED P:NCACTIVATE P:ACTIVATE P:KILLFOCUS P:DESTROY P:NCDESTROY");
  CHECK_EQ(IsWindow(p), 0);

  /* created hidden, then shown and hidden */
  log_clear();
  p = mk("P", CLS, "Hid", WS_OVERLAPPEDWINDOW, 0, 0, 100, 80, 0, 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCCALCSIZE P:CREATE");
  CHECK_EQ(IsWindowVisible(p), 0);
  log_clear();
  CHECK_EQ(ShowWindow(p, SW_SHOW), 0);
  CHECK_LOG("P:SHOWWINDOW P:WINDOWPOSCHANGING P:NCACTIVATE P:ACTIVATE P:SETFOCUS P:WINDOWPOSCHANGED P:SIZE P:MOVE");
  e = log_find("P", WM_SIZE, 0);
  if (e) CHECK_EQ(e->lp, 92u | (52u << 16));           /* 100x80 minus the frame and caption */
  log_clear();
  UpdateWindow(p);
  CHECK_LOG("P:NCPAINT P:PAINT P:ERASEBKGND");
  log_clear();
  CHECK_EQ(ShowWindow(p, SW_SHOW), 1);
  CHECK_LOG("");
  log_clear();
  CHECK_EQ(ShowWindow(p, SW_HIDE), 1);
  CHECK_LOG("P:SHOWWINDOW P:WINDOWPOSCHANGING P:WINDOWPOSCHANGED");   /* as for WS_OVERLAPPED */
  DestroyWindow(p);
}

/* Item 6 (inventory rule 4): WM_NCPAINT is sent before WM_PAINT. Both come out of the message loop
 * (PeekMessage / GetMessage, WM_PAINT range), and from UpdateWindow. */
static void test_ncpaint_before_paint(void) {
  HWND16 p = mk("P", CLS, "Main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 20, 300, 200, 0, 0);
  FARPTR m = far_alloc(sizeof(MSG16));
  int guard = 0;
  CHECK(p != 0);
  log_clear();
  while (guard++ < 10 && PeekMessage(m, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessage(m);
  CHECK_LOG("P:NCPAINT P:PAINT P:ERASEBKGND");
  /* a frame change on a clean window: WM_NCPAINT precedes the first WM_PAINT in the log */
  SetWindowPos(p, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  log_clear();
  UpdateWindow(p);
  {
    int inc = -1, ipt = -1;
    for (int i = 0; i < g_nlog; i++) {
      if (g_log[i].msg == WM_NCPAINT && inc < 0) inc = i;
      if (g_log[i].msg == WM_PAINT && ipt < 0) ipt = i;
    }
    CHECK(inc >= 0 && ipt >= 0);
    CHECK(inc < ipt);
  }
  DestroyWindow(p);
}

static void test_desktop(void) {
  /* user_init painted the desktop with COLOR_BACKGROUND (192,192,192 = static colour 7) */
  user_paint_desktop();
  CHECK_EQ(fb_palette_entry(px(1, 1)), GetSysColor(1));
  CHECK_EQ(fb_palette_entry(px(799, 599)), GetSysColor(1));
  /* hiding a top-level window shows the desktop under it again */
  g_top = make_top(TOPW);
  {
    HWND16 other = mk("V", CLS, "v", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, 40, 40, 0, 0);
    DestroyWindow(other);
  }
  ShowWindow(g_top, SW_HIDE);
  CHECK_EQ(fb_palette_entry(px(WX + 100, WY + 100)), GetSysColor(1));
  CHECK_EQ(fb_palette_entry(px(WX, WY)), GetSysColor(1));
  DestroyWindow(g_top);
}

static uint8_t *g_img;

int main(void) {
  FILE *fp;
  long len;
  (void)dump;
  if (fixture_open() != 0) return 1;
  /* the game's cursors and icons are read from the loaded module's resources (KERNEL) */
  fp = fopen(EXE, "rb");
  if (!fp) return 1;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  g_img = malloc((size_t)len);
  if (!g_img || fread(g_img, 1, (size_t)len, fp) != (size_t)len) return 1;
  fclose(fp);
  CHECK_EQ(kernel_load(g_img, (size_t)len), 0);
  CHECK(register_class(CLS, FP_PROC_A, 0, 0) != 0);
  test_constants();
  test_metrics_layout();
  test_frames();
  test_calcsize_adjust();
  test_hittest_map();
  test_mouse_routing();
  test_zoom_iconic();
  test_syscommand();
  test_sysmenu();
  test_caption_buttons();
  test_move_size_loops();
  test_scroll_api();
  test_scroll_range_shows_bar();
  test_scroll_tracking();
  test_cursors();
  test_cursor_present();
  test_icons();
  test_iconic_no_class_icon();
  test_msgbox();
  test_msgbox_toplevel_owner();
  test_msgbox_long_text();
  test_overlappedwindow_message_order();
  test_ncpaint_before_paint();
  test_desktop();
  kernel_shutdown();
  fixture_close();
  free(g_img);
  return TEST_DONE();
}
