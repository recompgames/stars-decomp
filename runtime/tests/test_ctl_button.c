/* Expectations for the BUTTON control class (runtime/ctl_button.c) and the dialog button helpers
 * CheckDlgButton, CheckRadioButton, IsDlgButtonChecked. Task stars-0z4.10, written BEFORE the
 * implementation. Do not edit an expectation to match the code: a disagreement is reported with
 * both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here, and no reference capture exists, so
 * nothing below is confirmed from the SDK):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h lines 444-448:
 *       BM_GETCHECK16 WM_USER+0, BM_SETCHECK16 +1, BM_GETSTATE16 +2, BM_SETSTATE16 +3,
 *       BM_SETSTYLE16 +4. (BM_CLICK, BM_SETIMAGE are Win32 only and do not exist here.)
 *   BS_* styles, BN_* codes  include/winuser.rh 1200-1212 (BS_PUSHBUTTON 0 ... BS_OWNERDRAW 0xB,
 *       BS_LEFTTEXT 0x20) and 1232-1240 (BN_CLICKED 0, BN_PAINT 1, BN_HILITE 2, BN_UNHILITE 3,
 *       BN_DISABLE 4, BN_DOUBLECLICKED 5). The Win32-only BN_SETFOCUS (6) and BN_KILLFOCUS (7)
 *       are NOT implemented. These values are shared by Win16 and Win32 for the low types
 *       (UNVERIFIED for 3.1: winuser16.h has no BS_/BN_ block).
 *   WM_COMMAND (Win16)    wParam = control id, LOWORD(lParam) = control handle, HIWORD(lParam) =
 *       notification code: dlls/user.exe16/message.c 1193-1201 shows the 16-bit message built from
 *       the Win32 one that way for WM_CTLCOLOR; for WM_COMMAND the layout is the one the issue
 *       states. The Win32 packing (code in the high word of wParam, handle in lParam) must NOT be
 *       used: that is the mutation of this task.
 *   Button behaviour     dlls/user32/button.c (Wine's 32-bit control, the behavioural reference):
 *       BUTTON_NOTIFY_PARENT 60-66, WM_GETDLGCODE 188-198, WM_KEYDOWN/VK_SPACE 245-251,
 *       WM_LBUTTONDBLCLK 254-263, WM_LBUTTONDOWN 264-269, WM_KEYUP/WM_LBUTTONUP 271-310,
 *       WM_MOUSEMOVE 324-330, BM_SETSTYLE 416-426, BM_GETCHECK 453, BM_SETCHECK 456-471,
 *       BM_GETSTATE 473, BM_SETSTATE 476-486, WM_NCHITTEST 488, BUTTON_CheckAutoRadioButton
 *       951-962, UB_Paint 1025-1066.
 *   WM_DRAWITEM / DRAWITEMSTRUCT16  winuser16.h 487-498 (26 bytes); ODT_BUTTON 4, ODA_* 1/2/4,
 *       ODS_* include/winuser.h 865-884; WM_DRAWITEM 0x002B, WM_SETFONT 0x0030, WM_GETFONT 0x0031,
 *       WM_GETDLGCODE 0x0087 include/winuser.rh 347-353, 416.
 *   WM_CTLCOLOR (0x0019): wParam = hdc, LOWORD(lParam) = control, HIWORD(lParam) = CTLCOLOR_xxx
 *       (BTN is 3): include/winuser.h 930-937, message.c 819-821. Default brushes: win32u/defwnd.c
 *       handle_control_color 2335-2366 (a plain DefWindowProc gives buttons the COLOR_BTNFACE brush).
 *
 * UNVERIFIED (also listed in docs/inventory.md section 15, "Standard controls part 1"):
 *   - Every PIXEL expectation (bevel widths, 13x13 check box and radio button, focus ring at inset 4,
 *     group box frame and label gap, X glyph): no reference capture exists (tests/ref is empty).
 *     Those checks test STRUCTURE and internal consistency: which colour class sits where, that a
 *     pressed button differs from a normal one, that a checked box differs from an unchecked one.
 *     Each pixel test names the PNG it will be compared against (PIXEL_TEST).
 *   - Windows 3.1 itself, for every behaviour taken from Wine's Win32 control.
 */
#include "test.h"

#include "ctl_test_common.h"

/* ---- tests: numbering and class ---- */

static void test_constants(void) {
  /* winuser16.h 444-448 */
  CHECK_EQ(BM_GETCHECK, 0x0400);
  CHECK_EQ(BM_SETCHECK, 0x0401);
  CHECK_EQ(BM_GETSTATE, 0x0402);
  CHECK_EQ(BM_SETSTATE, 0x0403);
  CHECK_EQ(BM_SETSTYLE, 0x0404);
  /* winuser.rh 1200-1212 */
  CHECK_EQ(BS_PUSHBUTTON, 0);
  CHECK_EQ(BS_DEFPUSHBUTTON, 1);
  CHECK_EQ(BS_CHECKBOX, 2);
  CHECK_EQ(BS_AUTOCHECKBOX, 3);
  CHECK_EQ(BS_RADIOBUTTON, 4);
  CHECK_EQ(BS_3STATE, 5);
  CHECK_EQ(BS_AUTO3STATE, 6);
  CHECK_EQ(BS_GROUPBOX, 7);
  CHECK_EQ(BS_USERBUTTON, 8);
  CHECK_EQ(BS_AUTORADIOBUTTON, 9);
  CHECK_EQ(BS_OWNERDRAW, 0xB);
  CHECK_EQ(BS_LEFTTEXT, 0x20);
  /* winuser.rh 1232-1240 */
  CHECK_EQ(BN_CLICKED, 0);
  CHECK_EQ(BN_PAINT, 1);
  CHECK_EQ(BN_HILITE, 2);
  CHECK_EQ(BN_UNHILITE, 3);
  CHECK_EQ(BN_DISABLE, 4);
  CHECK_EQ(BN_DOUBLECLICKED, 5);
  CHECK_EQ(WM_COMMAND, 0x0111);
  CHECK_EQ(WM_DRAWITEM, 0x002B);
  CHECK_EQ(WM_SETFONT, 0x0030);
  CHECK_EQ(WM_GETFONT, 0x0031);
  CHECK_EQ(WM_GETDLGCODE, 0x0087);
  CHECK_EQ(sizeof(DRAWITEMSTRUCT16), 26); /* 5 words + hwnd + hdc + RECT + DWORD (winuser16.h 487-498) */
  /* the Win32 BM_CLICK (0x00F5) must not be a Win16 button message */
  CHECK(BM_SETSTYLE < 0x0405);
}

static void test_class_registered(void) {
  HWND16 b, c;
  begin();
  b = B("B1", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  CHECK(b != 0);
  /* the class name is matched case-insensitively and the procedure is a runtime far pointer in
   * the callback segment, so GetWindowLong(GWL_WNDPROC) can hand it to a subclass */
  c = mk("B2", "button", "x", WS_CHILD | WS_VISIBLE, 100, 10, 60, 24, g_top, 102);
  CHECK(c != 0);
  CHECK_EQ(FP_SEG(GetWindowLong(b, GWL_WNDPROC)), MEM_SEL_CALLBACK);
  CHECK_EQ(GetWindowLong(b, GWL_WNDPROC), GetWindowLong(c, GWL_WNDPROC));
  CHECK_EQ(GetWindowLong(b, GWL_WNDPROC), USER_CB_BUTTON);
  CHECK(GetWindowLong(b, GWL_WNDPROC) != USER_CB_DEFWNDPROC);
  CHECK_EQ(GetWindowLong(b, GWL_ID), 101);
  /* window text */
  {
    char buf[16];
    FARPTR p = far_alloc(16);
    CHECK_EQ(GetWindowText(b, p, 16), 2);
    memcpy(buf, LIN(p), 3);
    CHECK_EQ(strcmp(buf, "OK"), 0);
    SetWindowText(b, fstr("Cancel"));
    CHECK_EQ(GetWindowText(b, p, 16), 6);
    CHECK_EQ(strcmp(LIN(p), "Cancel"), 0);
    CHECK_EQ(SendMessage(b, WM_GETTEXTLENGTH, 0, 0), 6);
  }
  end();
}

/* ---- BM_SETCHECK / BM_GETCHECK / BM_GETSTATE ---- */

static void test_check_roundtrip(void) {
  HWND16 cb, tri, pb, rd;
  begin();
  cb = B("C", "Check", BS_CHECKBOX, 10, 10, 100, 16, 201);
  tri = B("T", "Tri", BS_3STATE, 10, 30, 100, 16, 202);
  pb = B("PB", "Push", BS_PUSHBUTTON, 10, 50, 60, 24, 203);
  rd = B("R", "Radio", BS_RADIOBUTTON, 10, 80, 100, 16, 204);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 0);
  SendMessage(cb, BM_SETCHECK, 1, 0);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 1);
  SendMessage(cb, BM_SETCHECK, 0, 0);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 0);
  /* a two-state box cannot be indeterminate: Wine clamps to the type's maximum (button.c 456-458) */
  SendMessage(cb, BM_SETCHECK, 2, 0);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 1);
  /* a three-state box takes 0, 1, 2 */
  for (int v = 0; v <= 2; v++) {
    SendMessage(tri, BM_SETCHECK, (uint16_t)v, 0);
    CHECK_EQ(SendMessage(tri, BM_GETCHECK, 0, 0), v);
  }
  /* a push button has no check state */
  SendMessage(pb, BM_SETCHECK, 1, 0);
  CHECK_EQ(SendMessage(pb, BM_GETCHECK, 0, 0), 0);
  SendMessage(rd, BM_SETCHECK, 1, 0);
  CHECK_EQ(SendMessage(rd, BM_GETCHECK, 0, 0), 1);
  /* BM_GETSTATE: check in bits 0-1; bit 2 pushed; bit 3 focus (SDK layout; Wine adds an internal
   * bit 6 that this runtime masks off) */
  SendMessage(tri, BM_SETCHECK, 2, 0);
  CHECK_EQ(SendMessage(tri, BM_GETSTATE, 0, 0), 2);
  SendMessage(tri, BM_SETSTATE, 1, 0);
  CHECK_EQ(SendMessage(tri, BM_GETSTATE, 0, 0), 2 | 4);
  SendMessage(tri, BM_SETSTATE, 0, 0);
  CHECK_EQ(SendMessage(tri, BM_GETSTATE, 0, 0), 2);
  SetFocus(tri);
  CHECK_EQ(SendMessage(tri, BM_GETSTATE, 0, 0), 2 | 8);
  SetFocus(cb);
  CHECK_EQ(SendMessage(tri, BM_GETSTATE, 0, 0), 2);
  CHECK_EQ(SendMessage(cb, BM_GETSTATE, 0, 0), 1 | 8);
  end();
}

/* ---- notifications: the Win16 WM_COMMAND packing ---- */

static void test_click_wm_command(void) {
  HWND16 pb;
  const LogEnt *e;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  click_at(30, 20);
  CHECK_EQ(ncmd(), 1);
  e = cmd(0);
  CHECK(e != NULL);
  if (e) {
    CHECK_EQ(e->wp, 101);               /* wParam = control id */
    CHECK_EQ(e->lp & 0xFFFF, pb);       /* LOWORD(lParam) = control handle (Win16) */
    CHECK_EQ(e->lp >> 16, BN_CLICKED);  /* HIWORD(lParam) = notification code (Win16) */
  }
  /* the click gave the button the focus (button.c 264-269) */
  CHECK_EQ(GetFocus(), pb);
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 0);
  end();
}

/* A notification other than BN_CLICKED shows the code is in the HIGH word of lParam and the
 * handle in the LOW word: the mutation (swapping them) fails this test and the one above. */
static void test_double_click_notification(void) {
  HWND16 rd;
  const LogEnt *e;
  begin();
  rd = B("R", "Radio", BS_RADIOBUTTON, 10, 10, 100, 16, 301);
  dblclick_at(20, 18);
  CHECK(ncmd() >= 1);
  e = NULL;
  for (int i = 0; i < ncmd(); i++)
    if ((cmd(i)->lp >> 16) == BN_DOUBLECLICKED) e = cmd(i);
  CHECK(e != NULL);
  if (e) {
    CHECK_EQ(e->wp, 301);
    CHECK_EQ(e->lp & 0xFFFF, rd);
    CHECK_EQ(e->lp >> 16, BN_DOUBLECLICKED);
  }
  end();
}

static void test_press_and_drag_off(void) {
  HWND16 pb;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  mouse(PX0 + 30, PY0 + 20, 1);          /* press */
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 4);
  CHECK_EQ(GetCapture(), pb);
  mouse(PX0 + 200, PY0 + 200, 1);        /* drag out: the button un-pushes (button.c 324-330) */
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 0);
  mouse(PX0 + 30, PY0 + 20, 1);          /* back in: pushed again */
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 4);
  mouse(PX0 + 200, PY0 + 200, 1);
  mouse(PX0 + 200, PY0 + 200, 0);        /* release outside: no click */
  CHECK_EQ(ncmd(), 0);
  CHECK_EQ(GetCapture(), 0);
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 0);
  end();
}

static void test_disabled_button(void) {
  HWND16 pb;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  EnableWindow(pb, 0);
  click_at(30, 20);
  CHECK_EQ(ncmd(), 0);
  EnableWindow(pb, 1);
  click_at(30, 20);
  CHECK_EQ(ncmd(), 1);
  end();
}

static void test_space_key(void) {
  HWND16 pb;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  SetFocus(pb);
  CHECK_EQ(GetFocus(), pb);
  key(VK_SPACE_, 0x39, 1);
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 4); /* pushed while the key is down */
  CHECK_EQ(ncmd(), 0);
  key(VK_SPACE_, 0x39, 0);
  CHECK_EQ(SendMessage(pb, BM_GETSTATE, 0, 0) & 4, 0);
  CHECK_EQ(ncmd(), 1);
  CHECK(cmd(0) != NULL && cmd(0)->wp == 101 && (cmd(0)->lp >> 16) == BN_CLICKED);
  end();
}

/* ---- automatic styles ---- */

static void test_auto_checkbox_and_3state(void) {
  HWND16 cb, tri;
  begin();
  cb = B("C", "Auto", BS_AUTOCHECKBOX, 10, 10, 100, 16, 401);
  tri = B("T", "Auto3", BS_AUTO3STATE, 10, 40, 100, 16, 402);
  click_at(20, 18);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 1);
  click_at(20, 18);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(ncmd(), 2);
  /* 0 -> 1 -> 2 -> 0 (button.c 292-297) */
  click_at(20, 48);
  CHECK_EQ(SendMessage(tri, BM_GETCHECK, 0, 0), 1);
  click_at(20, 48);
  CHECK_EQ(SendMessage(tri, BM_GETCHECK, 0, 0), 2);
  click_at(20, 48);
  CHECK_EQ(SendMessage(tri, BM_GETCHECK, 0, 0), 0);
  /* a non-auto check box does not toggle by itself */
  {
    HWND16 plain = B("X", "Plain", BS_CHECKBOX, 10, 70, 100, 16, 403);
    click_at(20, 78);
    CHECK_EQ(SendMessage(plain, BM_GETCHECK, 0, 0), 0);
  }
  end();
}

static void test_auto_radio_group(void) {
  HWND16 r1, r2, r3, r4, r5;
  begin();
  /* group 1: r1 (WS_GROUP), r2, r3. group 2: r4 (WS_GROUP), r5 */
  r1 = B("R1", "One", BS_AUTORADIOBUTTON | WS_GROUP, 10, 10, 100, 16, 501);
  r2 = B("R2", "Two", BS_AUTORADIOBUTTON, 10, 30, 100, 16, 502);
  r3 = B("R3", "Three", BS_AUTORADIOBUTTON, 10, 50, 100, 16, 503);
  r4 = B("R4", "Four", BS_AUTORADIOBUTTON | WS_GROUP, 10, 80, 100, 16, 504);
  r5 = B("R5", "Five", BS_AUTORADIOBUTTON, 10, 100, 100, 16, 505);
  SendMessage(r1, BM_SETCHECK, 1, 0);
  SendMessage(r4, BM_SETCHECK, 1, 0);
  click_at(20, 38); /* r2 */
  CHECK_EQ(SendMessage(r1, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(r2, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(SendMessage(r3, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(r4, BM_GETCHECK, 0, 0), 1); /* the other group is untouched */
  CHECK_EQ(SendMessage(r5, BM_GETCHECK, 0, 0), 0);
  click_at(20, 108); /* r5 */
  CHECK_EQ(SendMessage(r4, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(r5, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(SendMessage(r2, BM_GETCHECK, 0, 0), 1); /* group 1 untouched */
  click_at(20, 58); /* r3 */
  CHECK_EQ(SendMessage(r2, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(r3, BM_GETCHECK, 0, 0), 1);
  /* clicking the checked one keeps it checked */
  click_at(20, 58);
  CHECK_EQ(SendMessage(r3, BM_GETCHECK, 0, 0), 1);
  /* a click sends BN_CLICKED each time */
  CHECK_EQ(ncmd(), 4);
  end();
}

/* ---- subclassing ---- */

static void test_subclass(void) {
  HWND16 cb;
  FARPTR old;
  int sawdown = 0, sawup = 0;
  begin();
  cb = B("C", "Auto", BS_AUTOCHECKBOX, 10, 10, 100, 16, 601);
  old = SetWindowLong(cb, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_BUTTON);
  CHECK_EQ(GetWindowLong(cb, GWL_WNDPROC), FP_PROC_B);
  g_old = old;
  log_clear();
  click_at(20, 18);
  for (int i = 0; i < g_nlog; i++) {
    if (g_log[i].tag == 'B' && g_log[i].hwnd == cb && g_log[i].msg == WM_LBUTTONDOWN) sawdown = 1;
    if (g_log[i].tag == 'B' && g_log[i].hwnd == cb && g_log[i].msg == WM_LBUTTONUP) sawup = 1;
  }
  CHECK(sawdown);   /* the subclass procedure saw the mouse message ... */
  CHECK(sawup);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 1); /* ... and forwarding through CallWindowProc still toggled */
  CHECK_EQ(ncmd(), 1);
  /* calling the old procedure directly works too */
  CHECK_EQ(CallWindowProc(old, cb, BM_SETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 0);
  SetWindowLong(cb, GWL_WNDPROC, old);
  end();
}

/* ---- styles ---- */

static void test_setstyle(void) {
  HWND16 b;
  begin();
  b = B("B", "Btn", BS_PUSHBUTTON, 10, 10, 100, 24, 701);
  SendMessage(b, BM_SETCHECK, 1, 0);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 0);
  SendMessage(b, BM_SETSTYLE, BS_AUTOCHECKBOX, 1);
  CHECK_EQ(GetWindowLong(b, GWL_STYLE) & BS_TYPEMASK, BS_AUTOCHECKBOX);
  CHECK(GetWindowLong(b, GWL_STYLE) & WS_CHILD); /* only the type changes (button.c 416-419) */
  SendMessage(b, BM_SETCHECK, 1, 0);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 1);
  SendMessage(b, BM_SETSTYLE, BS_DEFPUSHBUTTON, 1);
  CHECK_EQ(GetWindowLong(b, GWL_STYLE) & BS_TYPEMASK, BS_DEFPUSHBUTTON);
  end();
}

static void test_dlgcode(void) {
  /* button.c 188-198 */
  static const struct {
    uint32_t style;
    uint32_t want;
  } T[] = {{BS_PUSHBUTTON, DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON},
           {BS_DEFPUSHBUTTON, DLGC_BUTTON | DLGC_DEFPUSHBUTTON},
           {BS_RADIOBUTTON, DLGC_BUTTON | DLGC_RADIOBUTTON},
           {BS_AUTORADIOBUTTON, DLGC_BUTTON | DLGC_RADIOBUTTON},
           {BS_GROUPBOX, DLGC_STATIC},
           {BS_CHECKBOX, DLGC_BUTTON},
           {BS_AUTO3STATE, DLGC_BUTTON}};
  begin();
  for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
    HWND16 b = B("D", "x", T[i].style, 10, 10, 60, 24, 800);
    CHECK_EQ(SendMessage(b, WM_GETDLGCODE, 0, 0), T[i].want);
    DestroyWindow(b);
  }
  end();
}

static void test_font_message(void) {
  HWND16 b;
  begin();
  b = B("B", "Btn", BS_PUSHBUTTON, 10, 10, 60, 24, 901);
  CHECK_EQ(SendMessage(b, WM_GETFONT, 0, 0), 0); /* no font set: the system font is used */
  SendMessage(b, WM_SETFONT, GetStockObject(SYSTEM_FONT), 1);
  CHECK_EQ(SendMessage(b, WM_GETFONT, 0, 0), GetStockObject(SYSTEM_FONT));
  SendMessage(b, WM_SETFONT, 0, 0);
  CHECK_EQ(SendMessage(b, WM_GETFONT, 0, 0), 0);
  end();
}

/* ---- owner draw ---- */

static struct {
  int n;
  DRAWITEMSTRUCT16 d[16];
} g_di;
static int hook_drawitem(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)h;
  if (msg == WM_DRAWITEM && g_di.n < 16) {
    g_di.d[g_di.n++] = *(const DRAWITEMSTRUCT16 *)LIN(lp);
    CHECK_EQ(wp, 1001); /* wParam = control id */
    return 1;
  }
  return 0;
}

static void test_owner_draw(void) {
  HWND16 ob;
  begin();
  g_di.n = 0;
  g_hook = hook_drawitem;
  g_hook_ret = 1;
  ob = B("O", "Owner", BS_OWNERDRAW, 10, 10, 70, 30, 1001);
  pump(); /* first WM_PAINT */
  CHECK(g_di.n >= 1);
  if (g_di.n >= 1) {
    const DRAWITEMSTRUCT16 *d = &g_di.d[0];
    CHECK_EQ(d->CtlType, ODT_BUTTON);
    CHECK_EQ(d->CtlID, 1001);
    CHECK_EQ(d->itemAction, ODA_DRAWENTIRE);
    CHECK_EQ(d->hwndItem, ob);
    CHECK(d->hDC != 0);
    CHECK_EQ(d->rcItem.left, 0);
    CHECK_EQ(d->rcItem.top, 0);
    CHECK_EQ(d->rcItem.right, 70);
    CHECK_EQ(d->rcItem.bottom, 30);
    CHECK_EQ(d->itemState & ODS_SELECTED, 0);
  }
  g_di.n = 0;
  mouse(PX0 + 20, PY0 + 20, 1); /* press: ODA_SELECT with ODS_SELECTED (button.c 476-486) */
  {
    int sel = 0;
    for (int i = 0; i < g_di.n; i++)
      if (g_di.d[i].itemAction == ODA_SELECT && (g_di.d[i].itemState & ODS_SELECTED)) sel = 1;
    CHECK(sel);
  }
  mouse(PX0 + 20, PY0 + 20, 0);
  CHECK_EQ(ncmd(), 1);
  end();
}

/* ---- group box ---- */

static void test_groupbox_transparent_to_clicks(void) {
  HWND16 gb, pb;
  begin();
  gb = B("G", "Group", BS_GROUPBOX, 10, 10, 200, 100, 1100);
  pb = B("PB", "In", BS_PUSHBUTTON, 30, 40, 60, 24, 1101);
  /* a group box is created first, so it is above the button in the z-order; it must not take the click */
  CHECK_EQ((int16_t)SendMessage(gb, WM_NCHITTEST, 0, 0), HTTRANSPARENT);
  click_at(50, 50);
  CHECK_EQ(ncmd(), 1);
  CHECK(cmd(0) != NULL && cmd(0)->wp == 1101);
  CHECK_EQ(GetFocus(), pb);
  end();
}

/* ---- BS_USERBUTTON notifications (button.c 1025-1066) ---- */

static void test_userbutton(void) {
  begin();
  B("U", "User", BS_USERBUTTON, 10, 10, 70, 30, 1201);
  pump();
  log_clear();
  click_at(20, 20);
  {
    int codes[8], n = 0;
    for (int i = 0; i < ncmd(); i++) {
      unsigned c = cmd(i)->lp >> 16;
      if (c != BN_PAINT && n < 8) codes[n++] = (int)c;
    }
    CHECK_EQ(n, 3);
    if (n == 3) {
      CHECK_EQ(codes[0], BN_HILITE);
      CHECK_EQ(codes[1], BN_UNHILITE);
      CHECK_EQ(codes[2], BN_CLICKED);
    }
  }
  end();
}

/* ---- dialog button helpers ---- */

static void test_dlg_helpers(void) {
  HWND16 a, b, c, cb;
  begin();
  a = B("A", "A", BS_RADIOBUTTON, 10, 10, 60, 16, 1301);
  b = B("B", "B", BS_RADIOBUTTON, 10, 30, 60, 16, 1302);
  c = B("C", "C", BS_RADIOBUTTON, 10, 50, 60, 16, 1303);
  cb = B("K", "K", BS_3STATE, 10, 70, 60, 16, 1304);
  CHECK_EQ(user_dlg_item(g_top, 1302), b);
  CHECK_EQ(user_dlg_item(g_top, 9999), 0);
  /* CheckDlgButton / IsDlgButtonChecked are BM_SETCHECK / BM_GETCHECK to the control */
  CHECK_EQ(IsDlgButtonChecked(g_top, 1304), 0);
  CheckDlgButton(g_top, 1304, 2);
  CHECK_EQ(IsDlgButtonChecked(g_top, 1304), 2);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 2);
  CheckDlgButton(g_top, 1304, 1);
  CHECK_EQ(IsDlgButtonChecked(g_top, 1304), 1);
  CHECK_EQ(IsDlgButtonChecked(g_top, 7777), 0); /* no such control */
  CheckDlgButton(g_top, 7777, 1);               /* no such control: nothing happens */
  /* CheckRadioButton(first, last, check): checks one and unchecks the rest of the range */
  SendMessage(a, BM_SETCHECK, 1, 0);
  SendMessage(c, BM_SETCHECK, 1, 0);
  CheckRadioButton(g_top, 1301, 1303, 1302);
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(SendMessage(c, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(cb, BM_GETCHECK, 0, 0), 1); /* outside the range: untouched */
  CheckRadioButton(g_top, 1301, 1302, 1301);       /* c is outside this range */
  SendMessage(c, BM_SETCHECK, 1, 0);
  CheckRadioButton(g_top, 1301, 1302, 1301);
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(c, BM_GETCHECK, 0, 0), 1);
  /* Wine walks all children and returns TRUE even when the first or last id has no control */
  SendMessage(a, BM_SETCHECK, 1, 0);
  SendMessage(b, BM_SETCHECK, 1, 0);
  CHECK_EQ(CheckRadioButton(g_top, 1290, 1302, 1302), 1); /* 1290 does not exist */
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(CheckRadioButton(g_top, 1301, 1399, 1301), 1); /* 1399 does not exist */
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 1);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 0);
  end();
}

/* The PASCAL thunks (harness/user_thunks.c): arguments are pushed left to right, so word 0 of the
 * array is the first parameter. */
static uint32_t call_user(uint16_t ord, const uint16_t *args, int n) {
  uint32_t fp = harness_thunk_far("USER", ord);
  CHECK(fp != 0);
  if (!fp) return 0;
  return harness_call_far(fp, HCONV_PASCAL, args, n);
}

static void test_dlg_helper_thunks(void) {
  HWND16 a, b;
  begin();
  a = B("A", "A", BS_RADIOBUTTON, 10, 10, 60, 16, 1401);
  b = B("B", "B", BS_RADIOBUTTON, 10, 30, 60, 16, 1402);
  { uint16_t v[3] = {g_top, 1401, 1}; CHECK_EQ(call_user(97, v, 3) & 0xFFFF, 1); } /* CheckDlgButton */
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 1);
  { uint16_t v[2] = {g_top, 1401}; CHECK_EQ(call_user(98, v, 2) & 0xFFFF, 1); }    /* IsDlgButtonChecked */
  { uint16_t v[2] = {g_top, 1402}; CHECK_EQ(call_user(98, v, 2) & 0xFFFF, 0); }
  { uint16_t v[4] = {g_top, 1401, 1402, 1402}; CHECK_EQ(call_user(96, v, 4) & 0xFFFF, 1); } /* CheckRadioButton */
  CHECK_EQ(SendMessage(a, BM_GETCHECK, 0, 0), 0);
  CHECK_EQ(SendMessage(b, BM_GETCHECK, 0, 0), 1);
  end();
}

/* ---- WM_CTLCOLOR: inventory.md section 13 rule 15 ---- */

static void test_ctlcolor_default(void) {
  HDC16 dc;
  begin();
  dc = GetDC(g_top);
  CHECK(dc != 0);
  /* A plain DefWindowProc answers WM_CTLCOLOR with a brush (win32u/defwnd.c 2335-2366). */
  for (unsigned type = 0; type <= 6; type++) {
    uint32_t r = DefWindowProc(g_top, WM_CTLCOLOR, dc, make_long(g_top, (uint16_t)type));
    CHECK((r & 0xFFFF) != 0);
  }
  DefWindowProc(g_top, WM_CTLCOLOR, dc, make_long(g_top, CTLCOLOR_BTN));
  CHECK_EQ(GetBkColor(dc), GetSysColor(15)); /* COLOR_BTNFACE */
  DefWindowProc(g_top, WM_CTLCOLOR, dc, make_long(g_top, CTLCOLOR_EDIT));
  CHECK_EQ(GetBkColor(dc), GetSysColor(5));  /* COLOR_WINDOW */
  ReleaseDC(g_top, dc);
  end();
}

/* ---- pixels (all UNVERIFIED against Windows 3.1) ---- */

static void test_pushbutton_pixels(void) {
  HWND16 pb, dp;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  dp = B("DP", "OK", BS_DEFPUSHBUTTON, 100, 10, 60, 24, 102);
  pump();
  PIXEL_TEST("ctl_pushbutton_normal");
  /* outline: COLOR_WINDOWFRAME, one pixel; rounded corners show the parent's face colour */
  PIXEL_EQ(10 + 30, 10, BLACK);
  PIXEL_EQ(10, 10 + 12, BLACK);
  PIXEL_EQ(10 + 59, 10 + 12, BLACK);
  PIXEL_EQ(10 + 30, 10 + 23, BLACK);
  PIXEL_EQ(10, 10, FACE);
  PIXEL_EQ(10 + 59, 10 + 23, FACE);
  /* bevel inside the outline: white top and left, COLOR_BTNSHADOW bottom and right */
  PIXEL_EQ(10 + 30, 10 + 1, HILITE);
  PIXEL_EQ(10 + 1, 10 + 12, HILITE);
  PIXEL_EQ(10 + 30, 10 + 22, SHADOW);
  PIXEL_EQ(10 + 58, 10 + 12, SHADOW);
  PIXEL_EQ(10 + 5, 10 + 5, FACE);
  /* the label "OK" is drawn: COLOR_BTNTEXT pixels in the middle of the face */
  CHECK(count_in(10 + 15, 10 + 4, 10 + 45, 10 + 20, BTNTXT) > 8);
  /* the default button has a two pixel outline */
  PIXEL_TEST("ctl_pushbutton_default");
  PIXEL_EQ(100 + 30, 10, BLACK);
  PIXEL_EQ(100 + 30, 10 + 1, BLACK);
  PIXEL_EQ(100 + 1, 10 + 12, BLACK);
  PIXEL_EQ(100 + 30, 10 + 2, HILITE);
  PIXEL_NE(10 + 30, 10 + 1, BLACK);
  dump_frame("ctl_pushbuttons", PX0 + 0, PY0 + 0, 180, 50);
  /* normal and default differ */
  CHECK(pc(30 + 70, 11) != pc(30, 11));
  (void)pb;
  (void)dp;
  end();
}

static void test_pushbutton_pressed_and_focus(void) {
  HWND16 pb;
  int ring_normal;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  pump();
  PIXEL_TEST("ctl_pushbutton_focused");
  ring_normal = count_in(10 + 5, 10 + 4, 10 + 55, 10 + 5, BLACK);
  CHECK_EQ(ring_normal, 0);
  SetFocus(pb);
  pump();
  /* dotted focus rectangle: alternate pixels of COLOR_WINDOWFRAME at inset 4 */
  {
    int ring = count_in(10 + 5, 10 + 4, 10 + 55, 10 + 5, BLACK);
    CHECK(ring >= 20 && ring <= 30);
    CHECK(count_in(10 + 5, 10 + 4, 10 + 55, 10 + 5, FACE) >= 20);
  }
  PIXEL_TEST("ctl_pushbutton_pressed");
  mouse(PX0 + 30, PY0 + 20, 1);
  /* pressed: shadow inside top and left only, no highlight, label shifted by one pixel */
  PIXEL_EQ(10 + 30, 10 + 1, SHADOW);
  PIXEL_EQ(10 + 1, 10 + 12, SHADOW);
  PIXEL_NE(10 + 30, 10 + 22, SHADOW);
  PIXEL_EQ(10 + 30, 10, BLACK);
  mouse(PX0 + 30, PY0 + 20, 0);
  PIXEL_EQ(10 + 30, 10 + 1, HILITE); /* released: normal bevel again */
  end();
}

static void test_pushbutton_disabled_pixels(void) {
  HWND16 pb;
  begin();
  pb = B("PB", "OK", BS_PUSHBUTTON, 10, 10, 60, 24, 101);
  pump();
  CHECK(count_in(10 + 15, 10 + 4, 10 + 45, 10 + 20, BTNTXT) > 8);
  EnableWindow(pb, 0);
  pump();
  PIXEL_TEST("ctl_pushbutton_disabled");
  /* disabled: label in COLOR_GRAYTEXT, none in COLOR_BTNTEXT */
  CHECK_EQ(count_in(10 + 15, 10 + 4, 10 + 45, 10 + 20, BTNTXT), 0);
  CHECK(count_in(10 + 15, 10 + 4, 10 + 45, 10 + 20, GRAYTXT) > 8);
  end();
}

static void test_checkbox_pixels(void) {
  HWND16 cb, tri;
  begin();
  cb = B("C", "Check", BS_CHECKBOX, 10, 50, 100, 16, 201);
  tri = B("T", "Tri", BS_3STATE, 10, 90, 100, 16, 202);
  pump();
  PIXEL_TEST("ctl_checkbox_unchecked");
  /* 13x13 box, vertically centred: rows 1..13 of the 16 high control; black frame, white interior */
  PIXEL_EQ(10 + 0, 50 + 7, BLACK);
  PIXEL_EQ(10 + 12, 50 + 7, BLACK);
  PIXEL_EQ(10 + 6, 50 + 1, BLACK);
  PIXEL_EQ(10 + 6, 50 + 13, BLACK);
  PIXEL_EQ(10 + 1, 50 + 2, WHITE_);
  PIXEL_EQ(10 + 6, 50 + 7, WHITE_);
  PIXEL_EQ(10 + 11, 50 + 12, WHITE_);
  PIXEL_EQ(10 + 13, 50 + 7, FACE); /* beyond the box: the parent's face colour */
  CHECK_EQ(count_in(10 + 1, 50 + 2, 10 + 12, 50 + 13, BLACK), 0);
  /* the label sits to the right and is drawn in COLOR_WINDOWTEXT (black) */
  CHECK(count_in(10 + 17, 50, 10 + 60, 50 + 16, BLACK) > 8);
  SendMessage(cb, BM_SETCHECK, 1, 0);
  pump();
  PIXEL_TEST("ctl_checkbox_checked");
  /* a check mark (X) fills part of the interior; the centre is on the cross */
  PIXEL_EQ(10 + 6, 50 + 7, BLACK);
  CHECK(count_in(10 + 1, 50 + 2, 10 + 12, 50 + 13, BLACK) >= 10);
  PIXEL_EQ(10 + 0, 50 + 7, BLACK); /* frame unchanged */
  PIXEL_EQ(10 + 1, 50 + 2, WHITE_);
  /* indeterminate: a gray (COLOR_BTNSHADOW) dither in the interior, no X */
  SendMessage(tri, BM_SETCHECK, 2, 0);
  pump();
  PIXEL_TEST("ctl_checkbox_indeterminate");
  CHECK(count_in(10 + 1, 90 + 2, 10 + 12, 90 + 13, SHADOW) >= 20);
  CHECK_EQ(count_in(10 + 1, 90 + 2, 10 + 12, 90 + 13, BLACK), 0);
  /* pressed (mouse down on the box): interior turns COLOR_BTNFACE */
  SendMessage(cb, BM_SETCHECK, 0, 0);
  SendMessage(cb, BM_SETSTATE, 1, 0);
  pump();
  PIXEL_TEST("ctl_checkbox_pressed");
  PIXEL_EQ(10 + 6, 50 + 7, FACE);
  SendMessage(cb, BM_SETSTATE, 0, 0);
  pump();
  PIXEL_EQ(10 + 6, 50 + 7, WHITE_);
  /* BS_LEFTTEXT puts the box on the right-hand side */
  {
    HWND16 lt = B("L", "Left", BS_CHECKBOX | BS_LEFTTEXT, 150, 50, 100, 16, 203);
    pump();
    PIXEL_EQ(150 + 100 - 13, 50 + 7, BLACK);
    PIXEL_EQ(150 + 100 - 1, 50 + 7, BLACK);
    PIXEL_EQ(150 + 100 - 7, 50 + 7, WHITE_);
    (void)lt;
  }
  dump_frame("ctl_checkboxes", PX0 + 0, PY0 + 40, 260, 70);
  end();
}

static void test_radio_pixels(void) {
  HWND16 rd;
  begin();
  rd = B("R", "Radio", BS_RADIOBUTTON, 10, 70, 100, 16, 204);
  pump();
  PIXEL_TEST("ctl_radio_unchecked");
  /* 13 pixel round button: black outline touches the middle of each side, the corners of the
   * 13x13 cell stay the parent's face colour, the interior is white */
  PIXEL_EQ(10 + 0, 70 + 7, BLACK);
  PIXEL_EQ(10 + 12, 70 + 7, BLACK);
  PIXEL_EQ(10 + 6, 70 + 1, BLACK);
  PIXEL_EQ(10 + 6, 70 + 13, BLACK);
  PIXEL_EQ(10 + 0, 70 + 1, FACE);
  PIXEL_EQ(10 + 12, 70 + 13, FACE);
  PIXEL_EQ(10 + 6, 70 + 7, WHITE_);
  PIXEL_EQ(10 + 4, 70 + 5, WHITE_);
  SendMessage(rd, BM_SETCHECK, 1, 0);
  pump();
  PIXEL_TEST("ctl_radio_checked");
  PIXEL_EQ(10 + 6, 70 + 7, BLACK); /* the dot */
  PIXEL_EQ(10 + 0, 70 + 7, BLACK);
  PIXEL_EQ(10 + 3, 70 + 4, WHITE_);
  {
    int dot = count_in(10 + 3, 70 + 4, 10 + 10, 70 + 11, BLACK);
    CHECK(dot >= 9 && dot <= 25);
  }
  dump_frame("ctl_radio", PX0 + 0, PY0 + 60, 120, 40);
  end();
}

static void test_groupbox_pixels(void) {
  HWND16 gb;
  int top = -1, bottom = -1, ybase = 120;
  uint8_t before;
  begin();
  underlay(0, ybase - 10, 200, 90);
  before = pc(60, ybase + 30);
  CHECK_EQ(before, WHITE_);
  gb = B("G", "Grp", BS_GROUPBOX, 10, ybase, 160, 60, 1100);
  pump();
  PIXEL_TEST("ctl_groupbox");
  /* a group box draws a one pixel COLOR_WINDOWFRAME frame whose top edge runs through the middle
   * of the label; it does NOT fill its interior (the controls it holds are separate windows) */
  for (int y = ybase; y < ybase + 20 && top < 0; y++)
    if (pc(10 + 2, y) == BLACK) top = y;
  for (int y = ybase + 59; y > ybase + 30 && bottom < 0; y--)
    if (pc(10 + 60, y) == BLACK) bottom = y;
  CHECK(top >= ybase + 3 && top <= ybase + 9); /* about half a text line below the top */
  CHECK_EQ(bottom, ybase + 59);
  PIXEL_EQ(10, ybase + 40, BLACK);             /* left edge */
  PIXEL_EQ(10 + 159, ybase + 40, BLACK);       /* right edge */
  PIXEL_EQ(10 + 159, top, BLACK);              /* top edge reaches the right corner */
  PIXEL_EQ(10 + 60, top, BLACK);               /* ... and passes the label further right */
  PIXEL_NE(10 + 6, top, BLACK);                /* the label gap: the frame does not run through the text */
  CHECK_EQ(pc(60, ybase + 30), before);        /* interior untouched */
  /* the label is drawn in COLOR_WINDOWTEXT */
  CHECK(count_in(10 + 8, ybase, 10 + 40, ybase + 14, BLACK) > 5);
  dump_frame("ctl_groupbox", PX0 + 0, PY0 + ybase - 4, 180, 70);
  (void)gb;
  end();
}

/* Mutation anchor (issue): in runtime/ctl.c, change the packing in user_ctl_notify to the
 * Win32 form (code in the LOW word of lParam, handle in the high word) ->
 * test_click_wm_command and test_double_click_notification fail. Revert. */

int main(void) {
  (void)dump;
  (void)log_find;
  if (fixture_open() != 0) return 1;
  g_msg = far_alloc(sizeof(MSG16));
  CHECK(register_class(PCLS, FP_PROC_A, 0, 0) != 0);
  test_constants();
  test_class_registered();
  test_check_roundtrip();
  test_click_wm_command();
  test_double_click_notification();
  test_press_and_drag_off();
  test_disabled_button();
  test_space_key();
  test_auto_checkbox_and_3state();
  test_auto_radio_group();
  test_subclass();
  test_setstyle();
  test_dlgcode();
  test_font_message();
  test_owner_draw();
  test_groupbox_transparent_to_clicks();
  test_userbutton();
  test_dlg_helpers();
  test_dlg_helper_thunks();
  test_ctlcolor_default();
  test_pushbutton_pixels();
  test_pushbutton_pressed_and_focus();
  test_pushbutton_disabled_pixels();
  test_checkbox_pixels();
  test_radio_pixels();
  test_groupbox_pixels();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
