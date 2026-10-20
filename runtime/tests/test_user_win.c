/* Expectations for USER part 1, windows (runtime/user_win.c), issue stars-0z4.6.
 * Written before the implementation. Do not edit an expectation to match the code: a
 * disagreement is reported with both values.
 *
 * Every window procedure is a C function registered in the diff-test registry under the
 * test-only selector TSEL; messages are logged as "<label>:<NAME>" in delivery order.
 *
 * Sources for the pinned orders (the Windows 3.1 SDK text is NOT available in this
 * environment, so no order below is confirmed from the SDK; each cites Wine instead):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   W-create  dlls/win32u/window.c, NtUserCreateWindowEx: WM_GETMINMAXINFO (line 6070-6081),
 *             WM_NCCREATE (6097-6100), WM_NCCALCSIZE (6115-6125), WM_CREATE (6134-6135),
 *             WM_SIZE + WM_MOVE when the window does not need its first size message
 *             (children and popups, 6146-6152), WM_PARENTNOTIFY (send_parent_notify, 6172;
 *             definition 4551-4561), then NtUserShowWindow (6186-6190).
 *             The user.exe16 entry point CreateWindowEx16 (dlls/user.exe16/window.c:1897)
 *             converts the arguments and calls the same code.
 *   W-show    window.c show_window: WM_SHOWWINDOW (5067), SetWindowPos, then WM_SIZE and WM_MOVE
 *             when the window still needs its first size message (5128-5139), i.e. top-level
 *             windows created without WS_VISIBLE and shown later. The hide case returns before
 *             that block (5099-5120).
 *   W-swp     window.c calc_winpos (3731-3734: WM_WINDOWPOSCHANGING is sent unless
 *             SWP_NOSENDCHANGING, or a pure SWP_SHOWWINDOW with no client change) and
 *             SetWindowPos (4225-4234: WM_WINDOWPOSCHANGED unless nothing changed).
 *             calc_ncsize 3855/3884: WM_NCCALCSIZE (wParam TRUE) unless SWP_NOSIZE.
 *   W-defpos  dlls/win32u/defwnd.c handle_window_pos_changed (504-525): DefWindowProc turns
 *             WM_WINDOWPOSCHANGED into WM_MOVE and then WM_SIZE.
 *   W-destroy window.c user_destroy_window (5546-5621), send_destroy_message (5398-5430),
 *             destroy_window (5467-5503): hide, WM_DESTROY parent then children, then
 *             WM_NCDESTROY children then parent.
 *   W-enable  window.c NtUserEnableWindow (1126-1167): WM_CANCELMODE before WM_ENABLE(FALSE).
 *
 * UNVERIFIED (marked again in docs/inventory.md section 13):
 *   - Windows 3.1 itself is not confirmed for any order here (no SDK text, no capture).
 *   - Activation: a top-level window that becomes visible without SWP_NOACTIVATE is activated
 *     (stars-0z4.7; window.c show_window ~4190, set_active_window(hwnd, NULL, FALSE, TRUE)), so
 *     WM_NCACTIVATE, WM_ACTIVATE and WM_SETFOCUS follow WM_WINDOWPOSCHANGING and precede
 *     WM_WINDOWPOSCHANGED. That order is Wine's and UNVERIFIED for Windows 3.1; WM_ACTIVATEAPP,
 *     WM_CHILDACTIVATE and the z-order step of activation are not generated. WM_NCPAINT and WM_ERASEBKGND that Wine sends from inside ShowWindow
 *     (erase_now, window.c 4212-4216) are deferred to the first BeginPaint here.
 *   - The relative order WM_MOVE, WM_SIZE sent by DefWindowProc(WM_WINDOWPOSCHANGED) (W-defpos)
 *     is Wine's; the creation path in W-create/W-show sends WM_SIZE before WM_MOVE.
 */
#include "test.h"

#include "user_test_common.h"
#include "runtime/user_internal.h"

#define CLS "TESTCLS"
#define CLS_C "TESTCLSC"
/* stars-0z4.9: these tests pin message order and plain geometry with WS_OVERLAPPED (= 0), which in this
 * runtime means a top-level window WITHOUT caption, frame or scroll bars. That is an artefact of this
 * runtime: the SDK says an overlapped window has a caption and a border, and Wine's create_window
 * corrects the style (the correction itself is a follow-up). It keeps these tests free of the
 * non-client area. The game's frame is WS_OVERLAPPEDWINDOW (0x00CF0000); that case is covered by
 * test_user_nc.c (test_overlappedwindow_message_order) and test_visual_chrome.c. */
#define TOP (WS_OVERLAPPED | WS_VISIBLE)

static void test_win16_constants(void) {
  /* Win16 numbering (windows.h of the 3.1 SDK; checked against Wine's include/wine/winuser16.h
   * and the spec files for the Win16 names only). WM_CTLCOLOR exists in Win16; the Win32
   * WM_CTLCOLORBTN (0x0135) etc. do not. */
  CHECK_EQ(WM_CTLCOLOR, 0x0019);
  CHECK_EQ(WM_NCCREATE, 0x0081);
  CHECK_EQ(WM_NCCALCSIZE, 0x0083);
  CHECK_EQ(WM_WINDOWPOSCHANGING, 0x0046);
  CHECK_EQ(WM_PARENTNOTIFY, 0x0210);
  CHECK_EQ(WM_USER, 0x0400);
  CHECK_EQ(WM_TIMER, 0x0113);
  CHECK_EQ(sizeof(MSG16), 18);
  CHECK_EQ(sizeof(PAINTSTRUCT16), 32);
  CHECK_EQ(sizeof(CREATESTRUCT16), 34);
  CHECK_EQ(sizeof(WNDCLASS16), 26);
  CHECK_EQ(sizeof(WINDOWPOS16), 14);
  CHECK_EQ(sizeof(NCCALCSIZE_PARAMS16), 28);
  CHECK_EQ(sizeof(MINMAXINFO16), 20);
  CHECK_EQ(sizeof(WINDOWPLACEMENT16), 22);
  CHECK_EQ(WS_OVERLAPPEDWINDOW, 0x00CF0000UL);
}

static void test_register_class_errors(void) {
  uint16_t a;
  a = register_class(CLS, FP_PROC_A, 0, 0);
  CHECK(a != 0);
  CHECK_EQ(register_class(CLS, FP_PROC_A, 0, 0), 0); /* a class name can be registered once */
  CHECK(register_class(CLS_C, FP_PROC_C, 0, 0) != 0);
  /* Unknown class, WS_CHILD without a parent: no window. */
  next_label("X");
  CHECK_EQ(CreateWindow(fstr("NOSUCHCLASS"), fstr(""), TOP, 0, 0, 10, 10, 0, 0, 0, 0), 0);
  CHECK_EQ(CreateWindow(fstr(CLS), fstr(""), WS_CHILD, 0, 0, 10, 10, 0, 0, 0, 0), 0);
  CHECK_EQ(user_window_count(), 0);
}

/* A visible overlapped window: W-create then W-show (the show happens inside CreateWindow
 * because of WS_VISIBLE; the window needs its first size message, so WM_SIZE and WM_MOVE
 * follow the show). */
static void test_create_visible_overlapped(void) {
  HWND16 p;
  LogEnt *e;
  const CREATESTRUCT16 *cs;
  log_clear();
  p = mk("P", CLS, "Main", TOP, 10, 20, 300, 200, 0, 0);
  CHECK(p != 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCCALCSIZE P:CREATE P:SHOWWINDOW P:WINDOWPOSCHANGING "
            "P:NCACTIVATE P:ACTIVATE P:SETFOCUS P:WINDOWPOSCHANGED P:SIZE P:MOVE");
  e = log_find("P", WM_NCCREATE, 0);
  CHECK(e != NULL);
  if (e) {
    cs = &e->cs;
    CHECK_EQ(e->wp, 0);
    CHECK_EQ(cs->x, 10);
    CHECK_EQ(cs->y, 20);
    CHECK_EQ(cs->cx, 300);
    CHECK_EQ(cs->cy, 200);
    CHECK_EQ(cs->style, TOP); /* the style as passed, WS_VISIBLE included */
    CHECK_EQ(cs->hInstance, 0x1234);
    CHECK(strcmp(e->csname, "Main") == 0);
    CHECK(strcmp(e->cscls, CLS) == 0);
  }
  e = log_find("P", WM_NCCALCSIZE, 0);
  if (e) CHECK_EQ(e->wp, 0); /* W-create: wParam FALSE, lParam a RECT */
  e = log_find("P", WM_SHOWWINDOW, 0);
  if (e) {
    CHECK_EQ(e->wp, 1);
    CHECK_EQ(e->lp, 0);
  }
  e = log_find("P", WM_SIZE, 0);
  if (e) {
    CHECK_EQ(e->wp, SIZE_RESTORED);
    CHECK_EQ(e->lp, 300u | (200u << 16)); /* client size = window size: WS_OVERLAPPED has no non-client area here */
  }
  e = log_find("P", WM_MOVE, 0);
  if (e) CHECK_EQ(e->lp, 10u | (20u << 16));
  CHECK_EQ(IsWindowVisible(p), 1);
  CHECK_EQ(IsWindow(p), 1);
  CHECK_EQ(GetWindowTextLength(p), 4);
  DestroyWindow(p);
  CHECK_EQ(IsWindow(p), 0);
}

/* Created hidden: nothing is shown or sized until ShowWindow (W-create, W-show). */
static void test_create_hidden_then_show_hide(void) {
  HWND16 p;
  log_clear();
  p = mk("P", CLS, "Hid", WS_OVERLAPPED, 0, 0, 100, 80, 0, 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCCALCSIZE P:CREATE");
  CHECK_EQ(IsWindowVisible(p), 0);

  log_clear();
  CHECK_EQ(ShowWindow(p, SW_SHOW), 0); /* previous state: hidden */
  CHECK_LOG("P:SHOWWINDOW P:WINDOWPOSCHANGING P:NCACTIVATE P:ACTIVATE P:SETFOCUS P:WINDOWPOSCHANGED P:SIZE P:MOVE");
  CHECK_EQ(IsWindowVisible(p), 1);
  {
    LogEnt *chg = log_find("P", WM_WINDOWPOSCHANGED, 0);
    if (chg) CHECK((chg->aux & SWP_SHOWWINDOW) != 0);
  }

  log_clear();
  CHECK_EQ(ShowWindow(p, SW_SHOW), 1); /* already visible: nothing is sent */
  CHECK_LOG("");

  log_clear();
  CHECK_EQ(ShowWindow(p, SW_HIDE), 1);
  CHECK_LOG("P:SHOWWINDOW P:WINDOWPOSCHANGING P:WINDOWPOSCHANGED");
  {
    LogEnt *s = log_find("P", WM_SHOWWINDOW, 0);
    if (s) CHECK_EQ(s->wp, 0);
    LogEnt *chg = log_find("P", WM_WINDOWPOSCHANGED, 0);
    if (chg) CHECK((chg->aux & SWP_HIDEWINDOW) != 0);
  }
  log_clear();
  CHECK_EQ(ShowWindow(p, SW_HIDE), 0); /* already hidden: nothing is sent */
  CHECK_LOG("");
  DestroyWindow(p);
}

/* A visible child: W-create sends WM_SIZE and WM_MOVE right after WM_CREATE (a child does not
 * "need size"), then notifies the parent, then shows. */
static void test_create_child(void) {
  HWND16 p, c;
  LogEnt *e;
  p = mk("P", CLS, "Parent", TOP, 0, 0, 300, 200, 0, 0);
  log_clear();
  c = mk("C", CLS, "Child", WS_CHILD | WS_VISIBLE, 5, 6, 50, 40, p, 77);
  CHECK(c != 0);
  CHECK_LOG("C:NCCREATE C:NCCALCSIZE C:CREATE C:SIZE C:MOVE P:PARENTNOTIFY C:SHOWWINDOW "
            "C:WINDOWPOSCHANGING C:WINDOWPOSCHANGED");
  e = log_find("P", WM_PARENTNOTIFY, 0);
  if (e) {
    /* Win16 layout: wParam the event, lParam = LOWORD child handle, HIWORD child id. */
    CHECK_EQ(e->wp, WM_CREATE);
    CHECK_EQ(e->lp, (uint32_t)c | (77u << 16));
  }
  e = log_find("C", WM_NCCREATE, 0);
  if (e) {
    const CREATESTRUCT16 *cs = &e->cs;
    CHECK_EQ(cs->hwndParent, p);
    CHECK_EQ(cs->hMenu, 77);
    CHECK((cs->style & WS_CHILD) != 0);
  }
  e = log_find("C", WM_SIZE, 0);
  if (e) CHECK_EQ(e->lp, 50u | (40u << 16));
  e = log_find("C", WM_MOVE, 0);
  if (e) CHECK_EQ(e->lp, 5u | (6u << 16)); /* position in the parent's client area */
  /* WS_EX_NOPARENTNOTIFY is not reachable through the Win16 CreateWindow; GetWindowLong(GWL_ID). */
  CHECK_EQ(GetWindowWord(c, GWW_ID), 77);
  CHECK_EQ(GetParent(c), p);
  DestroyWindow(p);
}

/* Creation can be vetoed. W-create: a failed WM_NCCREATE or WM_CREATE(-1) goes to "failed:",
 * which calls destroy_window: only WM_NCDESTROY is sent (no WM_DESTROY). */
static void test_create_vetoed(void) {
  HWND16 p;
  g_nccreate_ret = 0;
  log_clear();
  p = mk("P", CLS, "No", TOP, 0, 0, 10, 10, 0, 0);
  CHECK_EQ(p, 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCDESTROY");
  CHECK_EQ(user_window_count(), 0);
  g_nccreate_ret = 1;

  g_create_ret = 0xFFFF; /* Win16 handlers return int -1 in AX */
  log_clear();
  p = mk("P", CLS, "No", TOP, 0, 0, 10, 10, 0, 0);
  CHECK_EQ(p, 0);
  CHECK_LOG("P:GETMINMAXINFO P:NCCREATE P:NCCALCSIZE P:CREATE P:NCDESTROY");
  CHECK_EQ(user_window_count(), 0);
  g_create_ret = 0;
}

/* W-destroy: parent visible with two visible children (C1 is created first and stays above C2). */
static void test_destroy_tree_order(void) {
  HWND16 p, c1, c2, g;
  p = mk("P", CLS, "P", TOP, 0, 0, 300, 200, 0, 0);
  c1 = mk("C1", CLS, "1", WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, p, 1);
  c2 = mk("C2", CLS, "2", WS_CHILD | WS_VISIBLE, 60, 0, 50, 50, p, 2);
  g = mk("G", CLS, "g", WS_CHILD | WS_VISIBLE, 1, 1, 10, 10, c2, 3);
  CHECK(p && c1 && c2 && g);
  CHECK_EQ(user_window_count(), 4);
  log_clear();
  CHECK_EQ(DestroyWindow(p), 1);
  /* The top-level window is hidden with SetWindowPos(SWP_HIDEWINDOW): no WM_SHOWWINDOW
   * (Wine comment at window.c:5580: only child windows receive it). Children are not hidden
   * one by one. New children go to the BOTTOM of the z-order (window.c:6117-6122), so the
   * list is C1, C2 and C2's child G: WM_DESTROY walks it parent first, WM_NCDESTROY children
   * first. */
  CHECK_LOG("P:WINDOWPOSCHANGING P:WINDOWPOSCHANGED P:NCACTIVATE P:ACTIVATE P:KILLFOCUS P:DESTROY C1:DESTROY C2:DESTROY G:DESTROY "
            "C1:NCDESTROY G:NCDESTROY C2:NCDESTROY P:NCDESTROY");
  CHECK_EQ(IsWindow(p), 0);
  CHECK_EQ(IsWindow(c1), 0);
  CHECK_EQ(IsWindow(c2), 0);
  CHECK_EQ(IsWindow(g), 0);
  CHECK_EQ(user_window_count(), 0);
  CHECK_EQ(DestroyWindow(p), 0); /* already gone */
}

static void test_destroy_child_alone(void) {
  HWND16 p, c;
  p = mk("P", CLS, "P", TOP, 0, 0, 300, 200, 0, 0);
  c = mk("C", CLS, "C", WS_CHILD | WS_VISIBLE, 5, 5, 20, 20, p, 9);
  log_clear();
  CHECK_EQ(DestroyWindow(c), 1);
  /* W-destroy: a child notifies its parent (WM_DESTROY event), is hidden with ShowWindow
   * (which sends WM_SHOWWINDOW), then destroyed. */
  CHECK_LOG("P:PARENTNOTIFY C:SHOWWINDOW C:WINDOWPOSCHANGING C:WINDOWPOSCHANGED C:DESTROY C:NCDESTROY");
  {
    LogEnt *n = log_find("P", WM_PARENTNOTIFY, 0);
    if (n) {
      CHECK_EQ(n->wp, WM_DESTROY);
      CHECK_EQ(n->lp, (uint32_t)c | (9u << 16));
    }
  }
  CHECK_EQ(IsWindow(c), 0);
  CHECK_EQ(GetWindow(p, GW_CHILD), 0);
  DestroyWindow(p);
}

/* W-swp, W-defpos. */
static void test_move_and_setwindowpos(void) {
  HWND16 p = mk("P", CLS, "P", TOP, 10, 20, 300, 200, 0, 0);
  log_clear();
  CHECK_EQ(MoveWindow(p, 100, 110, 400, 300, 1), 1);
  /* UNVERIFIED for Windows 3.1: WM_MOVE before WM_SIZE (Wine's DefWindowProc order). */
  CHECK_LOG("P:WINDOWPOSCHANGING P:NCCALCSIZE P:WINDOWPOSCHANGED P:MOVE P:SIZE");
  {
    LogEnt *n = log_find("P", WM_NCCALCSIZE, 0);
    if (n) CHECK_EQ(n->wp, 1); /* TRUE: lParam is an NCCALCSIZE_PARAMS */
    LogEnt *m = log_find("P", WM_MOVE, 0);
    if (m) CHECK_EQ(m->lp, 100u | (110u << 16));
    LogEnt *s = log_find("P", WM_SIZE, 0);
    if (s) {
      CHECK_EQ(s->wp, SIZE_RESTORED);
      CHECK_EQ(s->lp, 400u | (300u << 16));
    }
  }
  CHECK_RECT(get_rect(p, 0), 100, 110, 500, 410);
  CHECK_RECT(get_rect(p, 1), 0, 0, 400, 300);

  /* Nothing changes: only the pre-change notification (W-swp: 3731-3734 sends
   * WM_WINDOWPOSCHANGING, 4225 skips WM_WINDOWPOSCHANGED for a no-change SetWindowPos). */
  log_clear();
  CHECK_EQ(SetWindowPos(p, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), 1);
  CHECK_LOG("P:WINDOWPOSCHANGING");

  /* Size only: SIZE, no MOVE. */
  log_clear();
  SetWindowPos(p, 0, 0, 0, 250, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  CHECK_LOG("P:WINDOWPOSCHANGING P:NCCALCSIZE P:WINDOWPOSCHANGED P:SIZE");
  CHECK_RECT(get_rect(p, 0), 100, 110, 350, 260);
  DestroyWindow(p);
}

static void test_subclass(void) {
  HWND16 w = mk("W", CLS_C, "sub", TOP, 0, 0, 100, 100, 0, 0);
  FARPTR old;
  uint32_t r;
  CHECK_EQ(GetWindowLong(w, GWL_WNDPROC), FP_PROC_C);
  g_old = FP_PROC_C;
  log_clear();
  r = SendMessage(w, WM_USER, 5, 6);
  CHECK_EQ(r, g_user_ret);
  CHECK_LOG("C/W:USER");

  old = SetWindowLong(w, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, FP_PROC_C); /* returns the previous procedure */
  CHECK_EQ(GetWindowLong(w, GWL_WNDPROC), FP_PROC_B);
  g_old = old;
  log_clear();
  r = SendMessage(w, WM_USER, 5, 6);
  /* The subclass sees the message first, forwards through CallWindowProc; both see it. */
  CHECK_LOG("B/W:USER C/W:USER");
  CHECK_EQ(r, g_user_ret);
  {
    LogEnt *b = log_find("W", WM_USER, 0);
    if (b) {
      CHECK_EQ(b->wp, 5);
      CHECK_EQ(b->lp, 6);
    }
  }
  /* Runtime class procedure in the callback segment: CallWindowProc works on it and
   * GetWindowLong can hand it out. */
  SetWindowText(w, fstr("abc"));
  CHECK_EQ(CallWindowProc(USER_CB_DEFWNDPROC, w, WM_GETTEXTLENGTH, 0, 0), 3);
  SetWindowLong(w, GWL_WNDPROC, USER_CB_DEFWNDPROC);
  CHECK_EQ(GetWindowLong(w, GWL_WNDPROC), USER_CB_DEFWNDPROC);
  log_clear();
  CHECK_EQ(SendMessage(w, WM_GETTEXTLENGTH, 0, 0), 3);
  CHECK_LOG("");
  DestroyWindow(w);
}

static void test_coordinates(void) {
  HWND16 p = mk("P", CLS, "P", TOP, 10, 20, 300, 200, 0, 0);
  HWND16 c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 5, 6, 50, 40, p, 1);
  HWND16 g = mk("G", CLS, "g", WS_CHILD | WS_VISIBLE, 3, 4, 10, 10, c, 2);
  POINT16 q;
  FARPTR buf;
  CHECK_RECT(get_rect(p, 0), 10, 20, 310, 220);
  CHECK_RECT(get_rect(c, 0), 15, 26, 65, 66); /* screen coordinates */
  CHECK_RECT(get_rect(c, 1), 0, 0, 50, 40);
  CHECK_RECT(get_rect(g, 0), 18, 30, 28, 40);
  q = c2s(g, 0, 0);
  CHECK_EQ(q.x, 18);
  CHECK_EQ(q.y, 30);
  q = s2c(g, 18, 30);
  CHECK_EQ(q.x, 0);
  CHECK_EQ(q.y, 0);
  /* Round trips, including negative and outside points. */
  for (int i = -3; i < 8; i++) {
    int x = i * 37 - 11, y = 200 - i * 29;
    POINT16 a = c2s(g, x, y), b = s2c(g, a.x, a.y);
    CHECK_EQ(b.x, x);
    CHECK_EQ(b.y, y);
  }
  /* MapWindowPoints: child to parent, child to screen (hwndTo 0), parent to child. */
  buf = far_alloc(sizeof(POINT16) * 2);
  {
    POINT16 *pts = LIN(buf);
    pts[0] = (POINT16){0, 0};
    pts[1] = (POINT16){7, 9};
    MapWindowPoints(g, p, buf, 2);
    CHECK_EQ(pts[0].x, 8);
    CHECK_EQ(pts[0].y, 10);
    CHECK_EQ(pts[1].x, 15);
    CHECK_EQ(pts[1].y, 19);
    MapWindowPoints(p, g, buf, 2);
    CHECK_EQ(pts[0].x, 0);
    CHECK_EQ(pts[1].y, 9);
    MapWindowPoints(g, 0, buf, 1);
    CHECK_EQ(pts[0].x, 18);
    CHECK_EQ(pts[0].y, 30);
  }
  DestroyWindow(p);
}

static void test_window_from_point(void) {
  HWND16 p = mk("P", CLS, "P", TOP, 10, 20, 300, 200, 0, 0);
  HWND16 c1 = mk("C1", CLS, "1", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, p, 1);
  /* Children are created at the bottom of the z-order (window.c:6117-6122): C1 is above C2. */
  HWND16 c2 = mk("C2", CLS, "2", WS_CHILD | WS_VISIBLE, 50, 50, 100, 100, p, 2);
  HWND16 g = mk("G", CLS, "g", WS_CHILD | WS_VISIBLE, 60, 60, 20, 20, c2, 3);
  /* Screen = parent client (10,20) + child coordinates. */
  CHECK_EQ(WindowFromPoint(pt32(15, 25)), c1);     /* only C1 */
  CHECK_EQ(WindowFromPoint(pt32(65, 75)), c1);     /* overlap of C1 and C2 (not G): C1 is above */
  CHECK_EQ(WindowFromPoint(pt32(10 + 50 + 60 + 5, 20 + 50 + 60 + 5)), g); /* deepest child, outside C1 */
  CHECK_EQ(WindowFromPoint(pt32(10 + 250, 20 + 10)), p);    /* inside P, in no child */
  CHECK_EQ(WindowFromPoint(pt32(700, 500)), GetDesktopWindow()); /* outside every window */
  /* Hidden windows do not count. */
  ShowWindow(c1, SW_HIDE);
  CHECK_EQ(WindowFromPoint(pt32(65, 75)), c2);
  ShowWindow(c1, SW_SHOW);
  CHECK_EQ(WindowFromPoint(pt32(65, 75)), c1); /* showing re-raises a top-level only; a child keeps its place */
  /* Raising C2 puts it above C1. */
  SetWindowPos(c2, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  CHECK_EQ(WindowFromPoint(pt32(65, 75)), c2);
  DestroyWindow(p);
}

static void test_text_enable_tree(void) {
  HWND16 p = mk("P", CLS, "Main", TOP, 0, 0, 300, 200, 0, 0);
  HWND16 c1 = mk("C1", CLS, "one", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, p, 1);
  HWND16 c2 = mk("C2", CLS, "two", WS_CHILD, 30, 0, 20, 20, p, 2);
  FARPTR buf = far_alloc(32);
  char *s = LIN(buf);

  CHECK_EQ(GetWindowText(p, buf, 32), 4);
  CHECK(strcmp(s, "Main") == 0);
  CHECK_EQ(GetWindowText(p, buf, 3), 2); /* room for 2 characters and the NUL */
  CHECK(strcmp(s, "Ma") == 0);
  log_clear();
  SetWindowText(p, fstr("Renamed"));
  CHECK_LOG("P:SETTEXT"); /* SetWindowText sends WM_SETTEXT, DefWindowProc stores it */
  CHECK_EQ(GetWindowText(p, buf, 32), 7);
  CHECK(strcmp(s, "Renamed") == 0);
  log_clear();
  CHECK_EQ(GetWindowTextLength(p), 7);
  CHECK_LOG("P:GETTEXTLENGTH");

  /* W-enable. EnableWindow returns whether the window WAS disabled. */
  log_clear();
  CHECK_EQ(EnableWindow(p, 0), 0);
  CHECK_LOG("P:CANCELMODE P:KILLFOCUS P:ENABLE");
  CHECK_EQ(log_find("P", WM_ENABLE, 0)->wp, 0);
  CHECK_EQ(IsWindowEnabled(p), 0);
  CHECK((GetWindowLong(p, GWL_STYLE) & WS_DISABLED) != 0);
  log_clear();
  CHECK_EQ(EnableWindow(p, 1), 1);
  CHECK_LOG("P:ENABLE");
  CHECK_EQ(log_find("P", WM_ENABLE, 0)->wp, 1);
  log_clear();
  CHECK_EQ(EnableWindow(p, 1), 0); /* already enabled: nothing sent */
  CHECK_LOG("");

  /* Tree queries. Creation order c1 then c2: new children go to the bottom (window.c:6117-6122). */
  CHECK_EQ(GetParent(c1), p);
  CHECK_EQ(GetParent(p), 0);
  CHECK_EQ(GetWindow(p, GW_CHILD), c1);
  CHECK_EQ(GetWindow(c1, GW_HWNDNEXT), c2);
  CHECK_EQ(GetWindow(c2, GW_HWNDPREV), c1);
  CHECK_EQ(GetWindow(c2, GW_HWNDNEXT), 0);
  CHECK_EQ(GetWindow(c2, GW_HWNDFIRST), c1);
  CHECK_EQ(GetWindow(c1, GW_HWNDLAST), c2);
  CHECK_EQ(GetWindow(c1, GW_OWNER), 0);

  /* Visibility needs the whole parent chain. */
  CHECK_EQ(IsWindowVisible(c1), 1);
  CHECK_EQ(IsWindowVisible(c2), 0); /* no WS_VISIBLE */
  ShowWindow(p, SW_HIDE);
  CHECK_EQ(IsWindowVisible(c1), 0);
  CHECK((GetWindowLong(c1, GWL_STYLE) & WS_VISIBLE) != 0); /* its own bit is untouched */
  ShowWindow(p, SW_SHOW);
  CHECK_EQ(IsWindowVisible(c1), 1);

  /* Style-only minimise / maximise (geometry is the Windows 3.1 look task's). */
  CHECK_EQ(IsZoomed(p), 0);
  CHECK_EQ(IsIconic(p), 0);
  ShowWindow(p, SW_SHOWMAXIMIZED);
  CHECK_EQ(IsZoomed(p), 1);
  ShowWindow(p, SW_RESTORE);
  CHECK_EQ(IsZoomed(p), 0);
  ShowWindow(p, SW_SHOWMINIMIZED);
  CHECK_EQ(IsIconic(p), 1);
  ShowWindow(p, SW_RESTORE);
  CHECK_EQ(IsIconic(p), 0);

  /* GetWindowLong/Word: id, style, window extra bytes. */
  CHECK_EQ(GetWindowWord(c2, GWW_ID), 2);
  CHECK_EQ(SetWindowWord(c2, GWW_ID, 5), 2);
  CHECK_EQ(GetWindowLong(c2, GWL_ID) & 0xFFFF, 5);
  DestroyWindow(p);
}

static void test_window_placement(void) {
  HWND16 p = mk("P", CLS, "P", TOP, 10, 20, 300, 200, 0, 0);
  FARPTR f = far_alloc(sizeof(WINDOWPLACEMENT16));
  WINDOWPLACEMENT16 *w = LIN(f);
  w->length = sizeof *w;
  CHECK_EQ(GetWindowPlacement(p, f), 1);
  CHECK_EQ(w->showCmd, SW_SHOWNORMAL);
  CHECK_EQ(w->rcNormalPosition.left, 10);
  CHECK_EQ(w->rcNormalPosition.top, 20);
  CHECK_EQ(w->rcNormalPosition.right, 310);
  CHECK_EQ(w->rcNormalPosition.bottom, 220);
  DestroyWindow(p);
}

/* Hiding a window invalidates what it uncovered in EVERY window under it, children included.
 * Wine: server/window.c redraw_window with RDW_ALLCHILDREN (the exposed area is invalidated
 * in the whole subtree). The game's tooltip is a top-level WS_POPUP over child panes. */
static void test_hide_popup_invalidates_child_pane(void) {
  HWND16 f, pane, far_pane, pop;
  const UWnd *pw;
  f = mk("F", CLS, "frame", TOP, 0, 0, 300, 200, 0, 0);
  pane = mk("N", CLS, "pane", WS_CHILD | WS_VISIBLE, 10, 10, 100, 100, f, 1);
  far_pane = mk("M", CLS, "far", WS_CHILD | WS_VISIBLE, 200, 10, 40, 40, f, 2); /* clear of the popup */
  pop = mk("T", CLS, "tip", WS_POPUP | WS_VISIBLE, 20, 20, 50, 50, 0, 0);
  CHECK(f && pane && far_pane && pop);
  ValidateRect(f, 0);
  ValidateRect(pane, 0);
  ValidateRect(far_pane, 0);
  ValidateRect(pop, 0);
  CHECK_EQ(user_window_nupd(pane), 0);
  ShowWindow(pop, SW_HIDE);
  CHECK(user_window_nupd(f) != 0);
  CHECK_EQ(user_window_nupd(far_pane), 0); /* not under the popup: untouched */
  /* The popup covered screen (20,20)-(70,70) = (10,10)-(60,60) in the pane's client coordinates
   * (the pane's origin is screen 10,10). That is exactly the uncovered rectangle. */
  pw = user_wnd(pane);
  CHECK_EQ(user_window_nupd(pane), 1);
  CHECK_RECT(pw->upd[0], 10, 10, 60, 60);
  UpdateWindow(pane);
  CHECK_EQ(user_window_nupd(pane), 0); /* painted (and validated) by the update */
  CHECK_EQ(user_window_nupd(far_pane), 0);
  DestroyWindow(pop);
  DestroyWindow(f);
}

/* A popup whose parent argument is a child window is owned by that child's top-level ancestor
 * (Wine server/window.c create_window 2251-2253). */
static void test_popup_owner_is_toplevel(void) {
  HWND16 f, pane, pop;
  f = mk("F", CLS, "frame", TOP, 0, 0, 300, 200, 0, 0);
  pane = mk("N", CLS, "pane", WS_CHILD | WS_VISIBLE, 10, 10, 100, 100, f, 1);
  pop = mk("T", CLS, "tip", WS_POPUP, 20, 20, 50, 50, pane, 0);
  CHECK(pop != 0);
  CHECK_EQ(GetParent(pop), f);
  CHECK_EQ(GetWindow(pop, GW_OWNER), f);
  DestroyWindow(f);
  CHECK_EQ(IsWindow(pop), 0);
  CHECK_EQ(user_window_count(), 0);
}

/* Wine keeps the owner while an owned window is destroyed with its owner (same thread):
 * inside the popup's WM_DESTROY / WM_NCDESTROY, GW_OWNER and GetParent still name the owner. */
static HWND16 g_own_pop, g_own_in_destroy, g_own_par_destroy, g_own_in_ncdestroy;
static int hook_record_owner(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)wp;
  (void)lp;
  if (h != g_own_pop) return 0;
  if (msg == WM_DESTROY) {
    g_own_in_destroy = GetWindow(h, GW_OWNER);
    g_own_par_destroy = GetParent(h);
  }
  if (msg == WM_NCDESTROY) g_own_in_ncdestroy = GetWindow(h, GW_OWNER);
  return 0;
}

static void test_owner_kept_during_destroy(void) {
  HWND16 f, pop;
  f = mk("F", CLS, "frame", TOP, 0, 0, 300, 200, 0, 0);
  pop = mk("T", CLS, "tip", WS_POPUP, 20, 20, 50, 50, f, 0);
  CHECK(f && pop);
  CHECK_EQ(GetWindow(pop, GW_OWNER), f);
  g_own_pop = pop;
  g_own_in_destroy = g_own_par_destroy = g_own_in_ncdestroy = 0xFFFF;
  g_hook = hook_record_owner;
  DestroyWindow(f);
  g_hook = NULL;
  CHECK_EQ(IsWindow(pop), 0);
  CHECK_EQ(g_own_in_destroy, f);
  CHECK_EQ(g_own_par_destroy, f);
  CHECK_EQ(g_own_in_ncdestroy, f);
  CHECK_EQ(user_window_count(), 0);
}

/* stars-0z4.17: showing a window invalidates its visible children too (Wine redraw_window with
 * RDW_ALLCHILDREN). Before the fix only the shown window got an update region. */
static void test_show_invalidates_children(void) {
  HWND16 p, c, c2;
  p = mk("P", CLS, "popup", WS_POPUP, 0, 0, 200, 100, 0, 0);
  c = mk("C", CLS, "child", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, p, 1);
  c2 = mk("D", CLS, "grandchild", WS_CHILD | WS_VISIBLE, 5, 5, 20, 20, c, 2);
  CHECK(p && c && c2);
  ValidateRect(c, 0);
  ValidateRect(c2, 0);
  CHECK_EQ(user_window_nupd(c), 0);
  ShowWindow(p, SW_SHOW);
  CHECK(user_window_nupd(p) != 0);
  CHECK(user_window_nupd(c) != 0);
  CHECK(user_window_nupd(c2) != 0);
  UpdateWindow(c);
  CHECK_EQ(user_window_nupd(c), 0); /* painted */
  ShowWindow(p, SW_HIDE);
  ValidateRect(c, 0);
  ValidateRect(c2, 0);
  ShowWindow(p, SW_SHOW); /* a second show */
  CHECK(user_window_nupd(c) != 0);
  CHECK(user_window_nupd(c2) != 0);
  DestroyWindow(p);
}

int main(void) {
  if (fixture_open() != 0) return 1;
  test_win16_constants();
  test_register_class_errors();
  test_create_visible_overlapped();
  test_create_hidden_then_show_hide();
  test_create_child();
  test_create_vetoed();
  test_destroy_tree_order();
  test_destroy_child_alone();
  test_move_and_setwindowpos();
  test_subclass();
  test_coordinates();
  test_window_from_point();
  test_text_enable_tree();
  test_window_placement();
  test_hide_popup_invalidates_child_pane();
  test_popup_owner_is_toplevel();
  test_owner_kept_during_destroy();
  test_show_invalidates_children();
  fixture_close();
  return TEST_DONE();
}
