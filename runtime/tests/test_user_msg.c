/* Expectations for USER part 1, messages and painting (runtime/user_msg.c, user_paint.c),
 * issue stars-0z4.6. Written before the implementation. Do not edit an expectation to match
 * the code: a disagreement is reported with both values.
 *
 * Window procedures are C functions in the diff-test registry under the test-only selector
 * TSEL (see user_test_common.h). Sources (the Windows 3.1 SDK text is NOT available here, so
 * no order below is confirmed from the SDK):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   W-queue    server/queue.c DECL_HANDLER(get_message), 3336-3379: posted messages first, then
 *              WM_QUIT "only if no posted messages pending" (3346-3347), then hardware input
 *              (3350), then WM_PAINT (3356), then timers (3370). get_quit_message 1209-1233:
 *              PM_REMOVE consumes the quit message, PM_NOREMOVE does not.
 *   W-paint    server/window.c find_window_to_repaint / find_child_to_repaint (1117-1161):
 *              visible windows only; children are searched top of the z-order first; a window
 *              that needs repainting is chosen before its own children.
 *   W-cleanup  server/window.c:584 calls queue_cleanup_window (server/queue.c:2812) when a
 *              window is destroyed: its queued messages are discarded.
 *   W-dispatch dlls/user.exe16/message.c DispatchMessage16 (1722-1744): WM_TIMER and
 *              WM_SYSTIMER with a non-zero lParam call that procedure with
 *              (hwnd, message, wParam, GetTickCount()); otherwise the window procedure.
 *   W-quit     dlls/user.exe16/message.c GetMessage16 (1689) -> GetMessage: returns FALSE for
 *              WM_QUIT only.
 *   W-defwnd   dlls/win32u/defwnd.c:2527-2545 DefWindowProc(WM_PAINT) = BeginPaint/EndPaint.
 *
 * UNVERIFIED (also listed in docs/inventory.md section 13):
 *   - Windows 3.1 itself for every order above (no SDK text, no capture).
 *   - BeginPaint itself sends only WM_ERASEBKGND. WM_NCPAINT is sent by the message loop /
 *     UpdateWindow BEFORE WM_PAINT, and only for a window whose non-client area needs drawing
 *     (stars-0z4.9). The WS_OVERLAPPED windows here have no non-client area, so no WM_NCPAINT
 *     appears in these logs; test_user_nc.c pins the WS_OVERLAPPEDWINDOW order.
 *   - rcPaint is the bounding box of the invalid area (the update region is a list of at
 *     most 8 rectangles; more are merged into their bounding box).
 *   - Queue capacity is 256 messages (Windows 3.1's default application queue holds 8).
 *   - ScrollWindow's exact invalidated shape; GetDC on a hidden window draws nothing;
 *     WindowFromPoint skips hidden windows but not disabled ones.
 */
#include "test.h"

#include "user_test_common.h"

#define CLS "TESTCLS"
#define CLS_W "TESTCLSW" /* white class brush */
/* stars-0z4.9: these tests pin message order and plain geometry with WS_OVERLAPPED (= 0), which in this
 * runtime means a top-level window WITHOUT caption, frame or scroll bars. That is an artefact of this
 * runtime: the SDK says an overlapped window has a caption and a border, and Wine's create_window
 * corrects the style (the correction itself is a follow-up). It keeps these tests free of the
 * non-client area. The game's frame is WS_OVERLAPPEDWINDOW (0x00CF0000); that case is covered by
 * test_user_nc.c (test_overlappedwindow_message_order) and test_visual_chrome.c. */
#define TOP (WS_OVERLAPPED | WS_VISIBLE)
/* Hidden windows have no pending WM_PAINT, so the queue tests see only what they post. */
#define HID WS_OVERLAPPED
#define SCR_W 800

static FARPTR g_msgp;
static MSG16 *M(void) { return LIN(g_msgp); }

static int pix(int x, int y) { return fb_pixels()[y * fb_width() + x]; }

static void screen_white(void) {
  HDC16 dc = GetDC(0);
  CHECK(dc != 0);
  CHECK_EQ(PatBlt(dc, 0, 0, GDI_SCREEN_W, GDI_SCREEN_H, WHITENESS), 1);
  ReleaseDC(0, dc);
}

/* ---- posted messages ---- */

static void test_post_get_fifo(void) {
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 100, 100, 0, 0);
  char order[64] = "";
  CHECK_EQ(PostMessage(w, WM_USER, 1, 100), 1);
  CHECK_EQ(PostMessage(w, WM_USER + 1, 2, 200), 1);
  CHECK_EQ(PostMessage(w, WM_USER + 2, 3, 300), 1);
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 1);
    CHECK_EQ(M()->hwnd, w);
    CHECK_EQ(M()->wParam, i + 1);
    CHECK_EQ(M()->lParam, (i + 1) * 100);
    strcat(order, msg_name(M()->message));
    if (i < 2) strcat(order, " ");
  }
  /* Pinned order: first posted, first delivered (W-queue). Delivering LIFO fails this. */
  CHECK(strcmp(order, "USER USER1 USER2") == 0);
  if (strcmp(order, "USER USER1 USER2") != 0) fprintf(stderr, "  got order: %s\n", order);
  DestroyWindow(w);
}

static void test_filters_and_noremove(void) {
  HWND16 a = mk("A", CLS, "a", HID, 0, 0, 50, 50, 0, 0);
  HWND16 b = mk("B", CLS, "b", HID, 60, 0, 50, 50, 0, 0);
  PostMessage(a, WM_USER, 1, 0);
  PostMessage(b, WM_USER + 1, 2, 0);
  PostMessage(a, WM_USER + 2, 3, 0);
  /* Window filter: the first message for b, a's stay queued in order. */
  CHECK_EQ(PeekMessage(g_msgp, b, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(M()->message, WM_USER + 1);
  /* Range filter. */
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_USER + 2, WM_USER + 2, PM_NOREMOVE), 1);
  CHECK_EQ(M()->message, WM_USER + 2);
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_USER + 2, WM_USER + 2, PM_NOREMOVE), 1); /* still there */
  CHECK_EQ(PeekMessage(g_msgp, 0, WM_USER + 5, WM_USER + 6, PM_REMOVE), 0);
  /* PM_NOYIELD does not change what is returned. */
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE | PM_NOYIELD), 1);
  CHECK_EQ(M()->message, WM_USER);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(M()->message, WM_USER + 2);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 0); /* empty */
  DestroyWindow(a);
  DestroyWindow(b);
}

static void test_post_to_destroyed_window(void) {
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 50, 50, 0, 0);
  HWND16 k = mk("K", CLS, "k", HID, 60, 0, 50, 50, 0, 0);
  PostMessage(w, WM_USER, 1, 0);
  PostMessage(k, WM_USER + 1, 2, 0);
  DestroyWindow(w); /* W-cleanup: w's queued messages are dropped */
  CHECK_EQ(PostMessage(w, WM_USER, 3, 0), 0); /* not a window */
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 1);
  CHECK_EQ(M()->hwnd, k);
  CHECK_EQ(M()->message, WM_USER + 1);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 0);
  DestroyWindow(k);
}

/* ---- sent messages ---- */

static int hook_nested(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)wp;
  (void)lp;
  if (msg == WM_USER + 1) {
    /* A nested SendMessage runs synchronously inside the outer one. */
    uint32_t r = SendMessage(h, WM_USER + 2, 0, 0);
    log_add('A', h, WM_USER + 3, 0, r); /* "USER3": marks the point after the inner call returned */
    return 1;
  }
  if (msg == WM_USER + 2) return 1;
  return 0;
}

static void test_sendmessage(void) {
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 50, 50, 0, 0);
  HWND16 c = mk("C", "TESTCLSC", "c", HID, 60, 0, 50, 50, 0, 0);
  /* Calls the procedure at once and returns its value (procedure C answers WM_USER). */
  log_clear();
  CHECK_EQ(SendMessage(c, WM_USER, 7, 8), g_user_ret);
  CHECK_LOG("C/C:USER");
  /* Nothing is queued by SendMessage. */
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 0);
  /* Nesting: the inner message is fully processed before the outer returns. */
  g_hook = hook_nested;
  log_clear();
  SendMessage(w, WM_USER + 1, 0, 0);
  CHECK_LOG("W:USER1 W:USER2 W:USER3");
  g_hook = NULL;
  /* A window procedure may destroy its own window while handling a message. */
  CHECK_EQ(SendMessage(0x7777, WM_USER, 0, 0), 0); /* not a window: 0 */
  DestroyWindow(w);
  DestroyWindow(c);
}

/* ---- quit, idle, dispatch ---- */

static void test_quit(void) {
  PostQuitMessage(0);
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 0); /* W-quit: FALSE only for WM_QUIT */
  CHECK_EQ(M()->message, WM_QUIT);
  CHECK_EQ(M()->wParam, 0);
  PostQuitMessage(7);
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 0);
  CHECK_EQ(M()->message, WM_QUIT);
  CHECK_EQ(M()->wParam, 7); /* the exit code travels in wParam */
  /* PM_NOREMOVE leaves the quit message (W-queue, get_quit_message 1224-1229). */
  PostQuitMessage(9);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_NOREMOVE), 1);
  CHECK_EQ(M()->message, WM_QUIT);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 1);
  CHECK_EQ(M()->wParam, 9);
  CHECK_EQ(PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE), 0);
}

static int g_wait_calls;
static HWND16 g_wait_target;
static bool wait_hook(void) {
  g_wait_calls++;
  if (g_wait_calls == 1) {
    PostMessage(g_wait_target, WM_USER, 42, 0);
    return true; /* the host produced input */
  }
  return false;
}

static void test_idle_and_wait_hook(void) {
  HWND16 w = mk("W", CLS, "w", HID, 0, 0, 50, 50, 0, 0);
  /* Native tests: "no more input" ends the run. GetMessage returns 0 (the game's loop ends)
   * and records why. */
  user_input_exhausted = 0;
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 0);
  CHECK_EQ(user_input_exhausted, 1);
  CHECK_EQ(M()->message, WM_QUIT);
  /* Browser: the host yields. The hook may produce input; GetMessage then returns it. */
  user_input_exhausted = 0;
  g_wait_calls = 0;
  g_wait_target = w;
  user_wait_hook = wait_hook;
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 1);
  CHECK_EQ(M()->message, WM_USER);
  CHECK_EQ(M()->wParam, 42);
  CHECK_EQ(user_input_exhausted, 0);
  CHECK_EQ(GetMessage(g_msgp, 0, 0, 0), 0); /* hook returns false: nothing more */
  CHECK_EQ(user_input_exhausted, 1);
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  DestroyWindow(w);
}

static void test_dispatch(void) {
  HWND16 w = mk("W", CLS_W, "w", HID, 0, 0, 50, 50, 0, 0);
  HWND16 c = mk("C", "TESTCLSC", "c", HID, 60, 0, 50, 50, 0, 0);
  FARPTR mp = far_alloc(sizeof(MSG16));
  MSG16 *m = LIN(mp);
  m->hwnd = c;
  m->message = WM_USER;
  m->wParam = 1;
  m->lParam = 2;
  log_clear();
  CHECK_EQ(DispatchMessage(mp), g_user_ret); /* returns the procedure's value */
  CHECK_LOG("C/C:USER");
  {
    LogEnt *e = log_find("C", WM_USER, 0);
    if (e) {
      CHECK_EQ(e->wp, 1);
      CHECK_EQ(e->lp, 2);
    }
  }
  m->hwnd = 0; /* no window: nothing happens */
  log_clear();
  CHECK_EQ(DispatchMessage(mp), 0);
  CHECK_LOG("");
  /* W-dispatch: a timer message with a procedure in lParam calls that procedure instead. */
  m->hwnd = w;
  m->message = WM_TIMER;
  m->wParam = 5;
  m->lParam = FP_PROC_C;
  log_clear();
  DispatchMessage(mp);
  CHECK_LOG("C/W:TIMER");
  {
    LogEnt *e = log_find("W", WM_TIMER, 0);
    if (e) CHECK_EQ(e->wp, 5);
  }
  /* Without a callback the window procedure gets it. */
  m->lParam = 0;
  log_clear();
  DispatchMessage(mp);
  CHECK_LOG("W:TIMER");
  CHECK_EQ(TranslateMessage(mp), 0); /* not a key message */
  DestroyWindow(w);
  DestroyWindow(c);
}

/* ---- painting ---- */

static struct {
  int n;
  PAINTSTRUCT16 ps;
  FARPTR psp;
  int paint_black;
  int erase_seen;
} g_paint;

static int hook_paint(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)wp;
  (void)lp;
  if (msg != WM_PAINT) return 0;
  g_paint.n++;
  if (!g_paint.psp) g_paint.psp = far_alloc(sizeof(PAINTSTRUCT16));
  CHECK(BeginPaint(h, g_paint.psp) != 0);
  g_paint.ps = *(PAINTSTRUCT16 *)LIN(g_paint.psp);
  if (g_paint.paint_black) PatBlt(g_paint.ps.hdc, -50, -50, 1000, 1000, BLACKNESS);
  EndPaint(h, g_paint.psp);
  return 1;
}

static void paint_reset(int black) {
  memset(&g_paint.ps, 0, sizeof g_paint.ps);
  g_paint.n = 0;
  g_paint.paint_black = black;
  g_hook = hook_paint;
}

static FARPTR rect_p(int l, int t, int r, int b) {
  FARPTR p = far_alloc(sizeof(RECT16));
  RECT16 *q = LIN(p);
  q->left = (int16_t)l;
  q->top = (int16_t)t;
  q->right = (int16_t)r;
  q->bottom = (int16_t)b;
  return p;
}

static void test_invalidate_update_paint(void) {
  HWND16 w;
  w = mk("W", CLS_W, "w", TOP, 100, 100, 200, 150, 0, 0);
  screen_white();
  /* The creation sequence leaves the window with an update region (it was just shown); paint
   * it away first, then test the pinned behaviour on a clean window. */
  paint_reset(0);
  UpdateWindow(w);
  paint_reset(0);

  log_clear();
  InvalidateRect(w, rect_p(10, 20, 60, 70), 1);
  UpdateWindow(w);
  /* Exactly one WM_PAINT; BeginPaint sends WM_ERASEBKGND from inside it. No WM_NCPAINT: this
   * WS_OVERLAPPED window has no non-client area, and its frame is not invalid (UNVERIFIED). */
  CHECK_LOG("W:PAINT W:ERASEBKGND");
  CHECK_EQ(g_paint.n, 1);
  CHECK_EQ(g_paint.ps.rcPaint.left, 10);
  CHECK_EQ(g_paint.ps.rcPaint.top, 20);
  CHECK_EQ(g_paint.ps.rcPaint.right, 60);
  CHECK_EQ(g_paint.ps.rcPaint.bottom, 70);
  CHECK_EQ(g_paint.ps.fErase, 0); /* DefWindowProc erased with the class brush: handled */
  CHECK(g_paint.ps.hdc != 0);

  /* BeginPaint validated the region: a second UpdateWindow paints nothing. */
  log_clear();
  UpdateWindow(w);
  CHECK_LOG("");
  CHECK_EQ(g_paint.n, 1);

  /* InvalidateRect(NULL) = the whole client area. */
  paint_reset(0);
  InvalidateRect(w, 0, 0);
  UpdateWindow(w);
  CHECK_EQ(g_paint.n, 1);
  CHECK_EQ(g_paint.ps.rcPaint.left, 0);
  CHECK_EQ(g_paint.ps.rcPaint.top, 0);
  CHECK_EQ(g_paint.ps.rcPaint.right, 200);
  CHECK_EQ(g_paint.ps.rcPaint.bottom, 150);

  /* bErase = FALSE: no WM_ERASEBKGND. */
  paint_reset(0);
  log_clear();
  InvalidateRect(w, rect_p(0, 0, 5, 5), 0);
  UpdateWindow(w);
  CHECK_LOG("W:PAINT");

  /* Two invalid rectangles are reported as their bounding box. */
  paint_reset(0);
  InvalidateRect(w, rect_p(0, 0, 10, 10), 1);
  InvalidateRect(w, rect_p(20, 20, 30, 30), 1);
  UpdateWindow(w);
  CHECK_EQ(g_paint.n, 1);
  CHECK_EQ(g_paint.ps.rcPaint.right, 30);
  CHECK_EQ(g_paint.ps.rcPaint.bottom, 30);
  CHECK_EQ(g_paint.ps.rcPaint.left, 0);

  /* ValidateRect removes the whole area; a partial validate leaves the rest. */
  paint_reset(0);
  InvalidateRect(w, rect_p(0, 0, 10, 10), 1);
  ValidateRect(w, rect_p(0, 0, 10, 10));
  UpdateWindow(w);
  CHECK_EQ(g_paint.n, 0);
  InvalidateRect(w, rect_p(0, 0, 20, 20), 1);
  ValidateRect(w, rect_p(0, 0, 20, 10));
  UpdateWindow(w);
  CHECK_EQ(g_paint.n, 1);
  CHECK_EQ(g_paint.ps.rcPaint.left, 0);
  CHECK_EQ(g_paint.ps.rcPaint.top, 10);
  CHECK_EQ(g_paint.ps.rcPaint.right, 20);
  CHECK_EQ(g_paint.ps.rcPaint.bottom, 20);
  g_hook = NULL;
  DestroyWindow(w);
}

/* UpdateWindow = RDW_UPDATENOW | RDW_ALLCHILDREN (Wine dlls/user.exe16/window.c ~701): the
 * visible window and its visible children are painted; a hidden window is left alone. */
static void test_update_window_children_and_hidden(void) {
  HWND16 p, c, h;
  p = mk("P", CLS, "p", TOP, 0, 0, 200, 150, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, p, 1);
  paint_reset(0);
  while (PeekMessage(g_msgp, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessage(g_msgp);
  paint_reset(0);
  log_clear();
  InvalidateRect(p, rect_p(0, 0, 5, 5), 0);
  InvalidateRect(c, rect_p(0, 0, 5, 5), 0);
  UpdateWindow(p);
  CHECK_LOG("P:PAINT C:PAINT");
  CHECK_EQ(user_window_nupd(c), 0);
  CHECK_EQ(user_window_nupd(p), 0);
  DestroyWindow(p);

  h = mk("H", CLS, "h", HID, 0, 0, 100, 100, 0, 0);
  paint_reset(0);
  log_clear();
  InvalidateRect(h, rect_p(0, 0, 5, 5), 0);
  UpdateWindow(h);
  CHECK_LOG("");
  CHECK_EQ(g_paint.n, 0);
  CHECK_EQ(user_window_nupd(h), 1); /* still invalid */
  g_hook = NULL;
  DestroyWindow(h);
}

/* update_tree must not follow a stale sibling link (stars-0z4.6). A's WM_PAINT destroys the
 * next sibling B and creates a popup Z that reuses B's handle slot; Z's own sibling link leads
 * back to P (new top-level windows go on top of the desktop's list), so a guard that only
 * checks "handle in use" re-enters P. Expect exactly P, A, C, each once, in that order. */
static HWND16 g_ut_b, g_ut_z;
static int g_ut_done;
static int hook_destroy_sibling(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  if (msg == WM_PAINT && !g_ut_done && strcmp(label_of(h), "A") == 0) {
    g_ut_done = 1;
    DestroyWindow(g_ut_b);
    g_ut_z = mk("Z", CLS, "z", WS_POPUP | WS_VISIBLE, 300, 300, 20, 20, 0, 0);
  }
  return hook_paint(h, msg, wp, lp);
}

static void test_update_tree_stale_sibling(void) {
  HWND16 p, a, b, c;
  p = mk("P", CLS, "p", TOP, 0, 0, 200, 150, 0, 0);
  a = mk("A", CLS, "a", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, p, 1);
  b = mk("B", CLS, "b", WS_CHILD | WS_VISIBLE, 30, 0, 20, 20, p, 2);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 60, 0, 20, 20, p, 3);
  CHECK(p && a && b && c);
  paint_reset(0);
  while (PeekMessage(g_msgp, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessage(g_msgp);
  InvalidateRect(p, rect_p(0, 0, 5, 5), 0);
  InvalidateRect(a, rect_p(0, 0, 5, 5), 0);
  InvalidateRect(b, rect_p(0, 0, 5, 5), 0);
  InvalidateRect(c, rect_p(0, 0, 5, 5), 0);
  g_ut_b = b;
  g_ut_z = 0;
  g_ut_done = 0;
  g_hook = hook_destroy_sibling;
  log_clear();
  UpdateWindow(p);
  CHECK_EQ(g_ut_z, b); /* the handle slot really was reused, or the test proves nothing */
  {
    char paints[128] = "";
    for (int i = 0; i < g_nlog; i++)
      if (g_log[i].msg == WM_PAINT)
        snprintf(paints + strlen(paints), sizeof paints - strlen(paints), "%s%s:PAINT", paints[0] ? " " : "",
                 label_of(g_log[i].hwnd));
    if (strcmp(paints, "P:PAINT A:PAINT C:PAINT") != 0) fprintf(stderr, "paint log: %s\n", paints);
    CHECK(strcmp(paints, "P:PAINT A:PAINT C:PAINT") == 0); /* each exactly once, P not re-entered */
  }
  g_hook = NULL;
  CHECK_EQ(user_window_nupd(c), 0);
  DestroyWindow(p);
  if (IsWindow(g_ut_z)) DestroyWindow(g_ut_z);
}

static void test_erase_flag_without_brush(void) {
  HWND16 w;
  g_hook = NULL;
  w = mk("W", CLS, "w", TOP, 100, 100, 100, 100, 0, 0); /* class without a background brush */
  paint_reset(0);
  UpdateWindow(w);
  paint_reset(0);
  InvalidateRect(w, rect_p(0, 0, 10, 10), 1);
  UpdateWindow(w);
  /* DefWindowProc(WM_ERASEBKGND) returns 0 without a brush, so the application must erase:
   * PAINTSTRUCT.fErase is TRUE. */
  CHECK_EQ(g_paint.ps.fErase, 1);
  g_hook = NULL;
  DestroyWindow(w);
}

static void test_paint_clip_and_erase(void) {
  HWND16 w;
  w = mk("W", "TESTCLSBRUSH", "w", TOP, 100, 100, 200, 150, 0, 0);
  paint_reset(0);
  UpdateWindow(w);
  screen_white();
  paint_reset(1);
  InvalidateRect(w, rect_p(10, 20, 60, 70), 1);
  UpdateWindow(w);
  /* The paint DC is clipped to the update rectangle: the BLACKNESS covering far more than it
   * ends exactly on the rectangle (screen = window origin + client coordinates). */
  CHECK_EQ(pix(100 + 10, 100 + 20), 0);
  CHECK_EQ(pix(100 + 59, 100 + 69), 0);
  CHECK_EQ(pix(100 + 9, 100 + 20), 255);
  CHECK_EQ(pix(100 + 60, 100 + 30), 255);
  CHECK_EQ(pix(100 + 30, 100 + 19), 255);
  CHECK_EQ(pix(100 + 30, 100 + 70), 255);
  g_hook = NULL;
  DestroyWindow(w);
}

/* Order of synthesised WM_PAINT messages (W-paint) and their place after posted messages and
 * the quit message (W-queue). */
static void test_paint_synthesis_order(void) {
  HWND16 a, b, p, c;
  char got[64] = "";
  a = mk("A", CLS, "a", TOP, 0, 0, 40, 40, 0, 0);
  b = mk("B", CLS, "b", TOP, 100, 0, 40, 40, 0, 0); /* created last: topmost */
  p = mk("P", CLS, "p", TOP, 200, 0, 100, 100, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 10, 10, 20, 20, p, 1);
  paint_reset(0);
  /* Clear every pending update region first. */
  while (PeekMessage(g_msgp, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessage(g_msgp);
  while (PeekMessage(g_msgp, 0, 0, 0, PM_REMOVE)) {
  }
  log_clear();
  InvalidateRect(a, 0, 0);
  InvalidateRect(b, 0, 0);
  InvalidateRect(c, 0, 0);
  InvalidateRect(p, 0, 0);
  /* Synthesised only when nothing is posted. Posted first, then quit, then paint. */
  PostQuitMessage(3);
  PostMessage(a, WM_USER, 0, 0);
  g_log_on = 0;
  for (int i = 0; i < 6; i++) {
    uint16_t r = GetMessage(g_msgp, 0, 0, 0);
    const char *nm;
    if (M()->message == WM_PAINT) nm = label_of(M()->hwnd);
    else nm = msg_name(M()->message);
    strcat(got, nm);
    strcat(got, r ? " " : "! ");
    if (M()->message == WM_PAINT) {
      CHECK_EQ(M()->wParam, 0);
      CHECK_EQ(M()->lParam, 0);
      DispatchMessage(g_msgp);
    }
  }
  g_log_on = 1;
  /* USER (posted), QUIT, then paints in z-order, topmost top-level window first: P (created
   * last), and once P is clean the search descends into its child C, then B, then A. A window
   * that needs painting is chosen before its own children (W-paint). */
  CHECK(strcmp(got, "USER QUIT! P C B A ") == 0);
  if (strcmp(got, "USER QUIT! P C B A ") != 0) fprintf(stderr, "  got order: %s\n", got);
  g_hook = NULL;
  DestroyWindow(a);
  DestroyWindow(b);
  DestroyWindow(p);
}

/* ---- GetDC clipping ---- */

static void fill_black(HWND16 w, int x, int y, int cx, int cy) {
  HDC16 dc = GetDC(w);
  CHECK(dc != 0);
  PatBlt(dc, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, BLACKNESS);
  CHECK_EQ(ReleaseDC(w, dc), 1);
}

static void test_getdc_clipping(void) {
  HWND16 p, c, c1, c2, h;
  screen_white();
  /* WS_CLIPCHILDREN: the parent's DC excludes the child; the DC is also limited to the client
   * area. Screen = parent origin (100,100) + client coordinates. */
  p = mk("P", CLS, "p", TOP | WS_CLIPCHILDREN, 100, 100, 200, 200, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 20, 20, 50, 50, p, 1);
  fill_black(p, -20, -20, 300, 300);
  CHECK_EQ(pix(100 + 5, 100 + 5), 0);         /* parent only */
  CHECK_EQ(pix(100 + 30, 100 + 30), 255);     /* under the child: excluded */
  CHECK_EQ(pix(100 + 69, 100 + 69), 255);
  CHECK_EQ(pix(100 + 70, 100 + 70), 0);       /* just past the child */
  CHECK_EQ(pix(90, 90), 255);                 /* outside the client area */
  CHECK_EQ(pix(305, 305), 255);
  DestroyWindow(p);

  /* No WS_CLIPCHILDREN: the parent DC covers the child's area. */
  screen_white();
  p = mk("P", CLS, "p", TOP, 100, 100, 200, 200, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 20, 20, 50, 50, p, 1);
  fill_black(p, 0, 0, 200, 200);
  CHECK_EQ(pix(100 + 30, 100 + 30), 0);
  DestroyWindow(p);

  /* A child DC is limited to the parent's client area. */
  screen_white();
  p = mk("P", CLS, "p", TOP, 100, 100, 100, 100, 0, 0);
  c = mk("C", CLS, "c", WS_CHILD | WS_VISIBLE, 60, 60, 80, 80, p, 1); /* sticks out */
  fill_black(c, 0, 0, 80, 80);
  CHECK_EQ(pix(100 + 70, 100 + 70), 0);
  CHECK_EQ(pix(100 + 99, 100 + 99), 0);
  CHECK_EQ(pix(100 + 100, 100 + 100), 255); /* beyond the parent */
  DestroyWindow(p);

  /* WS_CLIPSIBLINGS: children are created at the bottom of the z-order (window.c:6117-6122),
   * so A (created first) is above B. B's DC leaves A's area alone; without the style A is
   * overdrawn. */
  screen_white();
  p = mk("P", CLS, "p", TOP, 100, 100, 300, 300, 0, 0);
  c1 = mk("A", CLS, "a", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 50, 50, 100, 100, p, 1);
  c2 = mk("B", CLS, "b", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, 100, p, 2);
  fill_black(c2, 0, 0, 100, 100);
  CHECK_EQ(pix(100 + 10, 100 + 10), 0);
  CHECK_EQ(pix(100 + 60, 100 + 60), 255); /* overlap: A on top */
  DestroyWindow(p);
  screen_white();
  p = mk("P", CLS, "p", TOP, 100, 100, 300, 300, 0, 0);
  c1 = mk("A", CLS, "a", WS_CHILD | WS_VISIBLE, 50, 50, 100, 100, p, 1);
  c2 = mk("B", CLS, "b", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, p, 2);
  fill_black(c2, 0, 0, 100, 100);
  CHECK_EQ(pix(100 + 60, 100 + 60), 0);
  DestroyWindow(p);

  /* A hidden window has an empty visible region (UNVERIFIED). */
  screen_white();
  h = mk("H", CLS, "h", WS_OVERLAPPED, 100, 100, 100, 100, 0, 0);
  fill_black(h, 0, 0, 100, 100);
  CHECK_EQ(pix(150, 150), 255);
  DestroyWindow(h);
  (void)c;
  (void)c1;
  (void)c2;
}

static void test_scroll_window(void) {
  HWND16 w;
  w = mk("W", "TESTCLSBRUSH", "s", TOP, 100, 100, 100, 50, 0, 0);
  paint_reset(0);
  UpdateWindow(w);
  paint_reset(0);
  screen_white();
  fill_black(w, 0, 0, 40, 50); /* left 40 columns black */
  ScrollWindow(w, 10, 0, 0, 0);
  /* The pixels moved 10 to the right... */
  CHECK_EQ(pix(100 + 12, 100 + 10), 0);
  CHECK_EQ(pix(100 + 49, 100 + 10), 0);
  CHECK_EQ(pix(100 + 50, 100 + 10), 255);
  /* ...and the uncovered strip [0,10) is invalid (exact shape UNVERIFIED). */
  UpdateWindow(w);
  CHECK_EQ(g_paint.n, 1);
  CHECK_EQ(g_paint.ps.rcPaint.left, 0);
  CHECK_EQ(g_paint.ps.rcPaint.top, 0);
  CHECK_EQ(g_paint.ps.rcPaint.right, 10);
  CHECK_EQ(g_paint.ps.rcPaint.bottom, 50);
  g_hook = NULL;
  DestroyWindow(w);
}


/* ---- PASCAL thunks of harness/user_thunks.c ---- */

static uint32_t call_user(uint16_t ord, const uint16_t *args, int n) {
  uint32_t fp = harness_thunk_far("USER", ord);
  CHECK(fp != 0);
  if (!fp) return 0;
  return harness_call_far(fp, HCONV_PASCAL, args, n);
}
#define HI(x) ((uint16_t)((uint32_t)(x) >> 16))
#define LO(x) ((uint16_t)((uint32_t)(x) & 0xFFFF))

/* Argument order and widths: far pointers and longs are pushed high word first; the WNDCLASS,
 * CREATESTRUCT-bound strings and MSG live in the arena. A decoder that swaps words, drops the
 * sign of a coordinate or reads the wrong word fails one of these. */
static void test_harness_thunks(void) {
  FARPTR wc = far_alloc(sizeof(WNDCLASS16)), name = fstr("THUNKCLS"), title = fstr("Thunk"), mp = far_alloc(sizeof(MSG16));
  WNDCLASS16 *w = LIN(wc);
  uint32_t r;
  uint16_t h;
  w->lpfnWndProc = FP_PROC_C;
  w->lpszClassName = name;
  { uint16_t a[2] = {HI(wc), LO(wc)}; CHECK(call_user(57, a, 2) != 0); }
  /* CreateWindow: class, title, style (long), x, y, cx, cy (signed), parent, menu, instance, param. */
  {
    uint16_t a[15] = {HI(name), LO(name), HI(title), LO(title), (uint16_t)(HID >> 16), (uint16_t)(HID & 0xFFFF),
                      (uint16_t)-20, 30, 111, 77, 0, 0, 0x4321, 0, 0};
    next_label("T");
    h = (uint16_t)call_user(41, a, 15);
    CHECK(h != 0);
  }
  CHECK_EQ(IsWindow(h), 1);
  {
    RECT16 wr = get_rect(h, 0);
    CHECK_EQ(wr.left, -20);                 /* the sign survives the thunk */
    CHECK_EQ(wr.top, 30);
    CHECK_EQ(wr.right, -20 + 111);
    CHECK_EQ(wr.bottom, 30 + 77);
  }
  CHECK_EQ(GetWindowWord(h, GWL_HINSTANCE), 0x4321);
  /* PostMessage / GetMessage / DispatchMessage with a 32-bit lParam pushed high word first. */
  { uint16_t a[5] = {h, WM_USER, 0x1111, 0x0003, 0x0004}; CHECK_EQ(call_user(110, a, 5) & 0xFFFF, 1); }
  { uint16_t a[5] = {HI(mp), LO(mp), 0, 0, 0}; CHECK_EQ(call_user(108, a, 5) & 0xFFFF, 1); }
  CHECK_EQ(((MSG16 *)LIN(mp))->hwnd, h);
  CHECK_EQ(((MSG16 *)LIN(mp))->message, WM_USER);
  CHECK_EQ(((MSG16 *)LIN(mp))->wParam, 0x1111);
  CHECK_EQ(((MSG16 *)LIN(mp))->lParam, 0x00030004u);
  { uint16_t a[2] = {HI(mp), LO(mp)}; r = call_user(114, a, 2); }
  CHECK_EQ(r, g_user_ret); /* DX:AX of the window procedure's LRESULT */
  /* SendMessage returns DX:AX; GetWindowLong / SetWindowLong with GWL_WNDPROC. */
  { uint16_t a[5] = {h, WM_USER, 1, 0, 2}; CHECK_EQ(call_user(111, a, 5), g_user_ret); }
  { uint16_t a[2] = {h, (uint16_t)GWL_WNDPROC}; CHECK_EQ(call_user(135, a, 2), FP_PROC_C); }
  { uint16_t a[4] = {h, (uint16_t)GWL_WNDPROC, HI(FP_PROC_A), LO(FP_PROC_A)}; CHECK_EQ(call_user(136, a, 4), FP_PROC_C); }
  CHECK_EQ(GetWindowLong(h, GWL_WNDPROC), FP_PROC_A);
  /* WindowFromPoint takes a POINT by value: y in the high word, pushed first. */
  { uint16_t a[2] = {0x7777, 0x7777}; CHECK_EQ(call_user(30, a, 2), GetDesktopWindow()); }
  { uint16_t a[1] = {h}; CHECK_EQ(call_user(53, a, 1) & 0xFFFF, 1); }
  CHECK_EQ(IsWindow(h), 0);
  { uint16_t a[1] = {(uint16_t)5}; call_user(6, a, 1); }
  CHECK_EQ(GetMessage(mp, 0, 0, 0), 0);
  CHECK_EQ(((MSG16 *)LIN(mp))->wParam, 5);
}

int main(void) {
  if (fixture_open() != 0) return 1;
  g_msgp = far_alloc(sizeof(MSG16));
  register_class(CLS, FP_PROC_A, 0, 0);
  register_class("TESTCLSC", FP_PROC_C, 0, 0);
  /* hbrBackground = COLOR_WINDOW + 1: the default Windows 3.1 window colour is white. */
  register_class(CLS_W, FP_PROC_A, 0, 6);
  register_class("TESTCLSBRUSH", FP_PROC_A, 0, 6);
  test_post_get_fifo();
  test_filters_and_noremove();
  test_post_to_destroyed_window();
  test_sendmessage();
  test_quit();
  test_idle_and_wait_hook();
  test_dispatch();
  test_invalidate_update_paint();
  test_update_window_children_and_hidden();
  test_update_tree_stale_sibling();
  test_erase_flag_without_brush();
  test_paint_clip_and_erase();
  test_paint_synthesis_order();
  test_getdc_clipping();
  test_scroll_window();
  test_harness_thunks();
  fixture_close();
  return TEST_DONE();
}
