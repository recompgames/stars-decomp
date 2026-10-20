/* Expectations for the LISTBOX control class (runtime/ctl_listbox.c). Task stars-0z4.11, written
 * BEFORE the implementation. Do not edit an expectation to match the code: a disagreement is
 * reported with both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here and no reference capture exists):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h lines 195-231: LB_ADDSTRING16 WM_USER+1,
 *       LB_INSERTSTRING16 +2, LB_DELETESTRING16 +3, LB_SELITEMRANGEEX16 +4, LB_RESETCONTENT16 +5,
 *       LB_SETSEL16 +6, LB_SETCURSEL16 +7, LB_GETSEL16 +8, LB_GETCURSEL16 +9, LB_GETTEXT16 +10,
 *       LB_GETTEXTLEN16 +11, LB_GETCOUNT16 +12, LB_SELECTSTRING16 +13, LB_DIR16 +14, LB_GETTOPINDEX16
 *       +15, LB_FINDSTRING16 +16, LB_GETSELCOUNT16 +17, LB_GETSELITEMS16 +18, LB_SETTABSTOPS16 +19,
 *       LB_GETHORIZONTALEXTENT16 +20, LB_SETHORIZONTALEXTENT16 +21, LB_SETCOLUMNWIDTH16 +22,
 *       LB_ADDFILE16 +23, LB_SETTOPINDEX16 +24, LB_GETITEMRECT16 +25, LB_GETITEMDATA16 +26,
 *       LB_SETITEMDATA16 +27, LB_SELITEMRANGE16 +28, LB_SETANCHORINDEX16 +29,
 *       LB_GETANCHORINDEX16 +30, LB_SETCARETINDEX16 +31, LB_GETCARETINDEX16 +32,
 *       LB_SETITEMHEIGHT16 +33, LB_GETITEMHEIGHT16 +34, LB_FINDSTRINGEXACT16 +35, LB_CARETON16 +36,
 *       LB_CARETOFF16 +37.  The Win32 values (LB_ADDSTRING 0x0180 ...) must NOT be used.
 *   Owner-draw structs     winuser16.h 487-528: DRAWITEMSTRUCT16 (26 bytes), MEASUREITEMSTRUCT16
 *       (CtlType, CtlID, itemID, itemWidth, itemHeight, itemData), DELETEITEMSTRUCT16 (CtlType,
 *       CtlID, itemID, hwndItem, itemData), COMPAREITEMSTRUCT16 (CtlType, CtlID, hwndItem, itemID1,
 *       itemData1, itemID2, itemData2).  ODT_LISTBOX = 2 (include/winuser.h 861-866).
 *   Behaviour (Wine dlls/user32/listbox.c, not re-read offline; UNVERIFIED for Windows 3.1):
 *       LBN_SELCHANGE is sent on the button up with LBS_NOTIFY (HandleLButtonUp), LBN_DBLCLK on the
 *       double click, keyboard moves send LBN_SELCHANGE when the selection changes; LB_SETCURSEL on
 *       a multiple-selection list returns LB_ERR; LBS_SORT inserts only for LB_ADDSTRING, not for
 *       LB_INSERTSTRING; WM_MEASUREITEM once at creation for LBS_OWNERDRAWFIXED and per item for
 *       LBS_OWNERDRAWVARIABLE; WM_DELETEITEM per item of an owner-draw list.
 *
 * UNVERIFIED (this runtime's choices): item height = font height; text 1 pixel from the left;
 * selection in COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT; dotted focus rectangle on the caret item;
 * without LBS_NOINTEGRALHEIGHT the window height is cut to a multiple of the item height; LB_DIR
 * lists directories as [name], drives as [-c-] and ".." as [..] outside the root; tab stops in
 * dialog units of 1/4 of the average character width.
 * Not implemented (documented): LBS_MULTICOLUMN (LB_SETCOLUMNWIDTH is stored only), LB_ADDFILE,
 * LBS_WANTKEYBOARDINPUT (WM_VKEYTOITEM, WM_CHARTOITEM), LBS_NODATA, drag-and-scroll timers.
 */
#include "test.h"

#include "ctl2_test_common.h"

static HWND16 L(const char *label, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "LISTBOX", "", style | WS_TABSTOP, x, y, w, h, id);
}
static void add3(HWND16 l) {
  SMS(l, LB_ADDSTRING, 0, "alpha");
  SMS(l, LB_ADDSTRING, 0, "beta");
  SMS(l, LB_ADDSTRING, 0, "gamma");
}
static int ih(HWND16 l) { return (int)SM(l, LB_GETITEMHEIGHT, 0, 0); }
/* click on item i (the list's client origin is (x, y) in parent coordinates) */
static void click_item(int x, int y, int ihh, int i) { click_at(x + 20, y + 1 + i * ihh + ihh / 2); }

static void test_disablenoscroll(void) {
  HWND16 l, l2;
  begin();
  /* LBS_DISABLENOSCROLL (0x1000, winuser.h): the bar stays visible, disabled, when the list fits
   * (PLANETWNDPROC 9:0x0232 creates LISTBOX style 0x40a01001). */
  CHECK_EQ(LBS_DISABLENOSCROLL, 0x1000);
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_DISABLENOSCROLL | WS_BORDER | WS_VSCROLL, 20, 20, 120, 100, 230);
  SMS(l, LB_ADDSTRING, 0, "alpha");
  SMS(l, LB_ADDSTRING, 0, "beta");
  CHECK(user_wnd(l)->style & WS_VSCROLL);
  CHECK_EQ(user_sb_get(l, SB_VERT)->arrows, ESB_DISABLE_BOTH);
  /* once the items overflow the bar is enabled again */
  for (int i = 0; i < 20; i++) SMS(l, LB_ADDSTRING, 0, "more");
  CHECK(user_wnd(l)->style & WS_VSCROLL);
  CHECK_EQ(user_sb_get(l, SB_VERT)->arrows, 0);
  /* without the style the bar is hidden when the list fits */
  l2 = L("L2", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER | WS_VSCROLL, 160, 20, 120, 100, 231);
  SMS(l2, LB_ADDSTRING, 0, "alpha");
  SMS(l2, LB_ADDSTRING, 0, "beta");
  CHECK(!(user_wnd(l2)->style & WS_VSCROLL));
  end();
}

static void test_constants(void) {
  CHECK_EQ(LB_ADDSTRING, 0x0401);
  CHECK_EQ(LB_INSERTSTRING, 0x0402);
  CHECK_EQ(LB_DELETESTRING, 0x0403);
  CHECK_EQ(LB_SELITEMRANGEEX, 0x0404);
  CHECK_EQ(LB_RESETCONTENT, 0x0405);
  CHECK_EQ(LB_SETSEL, 0x0406);
  CHECK_EQ(LB_SETCURSEL, 0x0407);
  CHECK_EQ(LB_GETSEL, 0x0408);
  CHECK_EQ(LB_GETCURSEL, 0x0409);
  CHECK_EQ(LB_GETTEXT, 0x040A);
  CHECK_EQ(LB_GETTEXTLEN, 0x040B);
  CHECK_EQ(LB_GETCOUNT, 0x040C);
  CHECK_EQ(LB_SELECTSTRING, 0x040D);
  CHECK_EQ(LB_DIR, 0x040E);
  CHECK_EQ(LB_GETTOPINDEX, 0x040F);
  CHECK_EQ(LB_FINDSTRING, 0x0410);
  CHECK_EQ(LB_GETSELCOUNT, 0x0411);
  CHECK_EQ(LB_GETSELITEMS, 0x0412);
  CHECK_EQ(LB_SETTABSTOPS, 0x0413);
  CHECK_EQ(LB_GETHORIZONTALEXTENT, 0x0414);
  CHECK_EQ(LB_SETHORIZONTALEXTENT, 0x0415);
  CHECK_EQ(LB_SETCOLUMNWIDTH, 0x0416);
  CHECK_EQ(LB_ADDFILE, 0x0417);
  CHECK_EQ(LB_SETTOPINDEX, 0x0418);
  CHECK_EQ(LB_GETITEMRECT, 0x0419);
  CHECK_EQ(LB_GETITEMDATA, 0x041A);
  CHECK_EQ(LB_SETITEMDATA, 0x041B);
  CHECK_EQ(LB_SELITEMRANGE, 0x041C);
  CHECK_EQ(LB_SETANCHORINDEX, 0x041D);
  CHECK_EQ(LB_GETANCHORINDEX, 0x041E);
  CHECK_EQ(LB_SETCARETINDEX, 0x041F);
  CHECK_EQ(LB_GETCARETINDEX, 0x0420);
  CHECK_EQ(LB_SETITEMHEIGHT, 0x0421);
  CHECK_EQ(LB_GETITEMHEIGHT, 0x0422);
  CHECK_EQ(LB_FINDSTRINGEXACT, 0x0423);
  CHECK_EQ(LB_CARETON, 0x0424);
  CHECK_EQ(LB_CARETOFF, 0x0425);
  CHECK_EQ(LBS_NOTIFY, 1);
  CHECK_EQ(LBS_SORT, 2);
  CHECK_EQ(LBS_MULTIPLESEL, 8);
  CHECK_EQ(LBS_OWNERDRAWFIXED, 0x10);
  CHECK_EQ(LBS_OWNERDRAWVARIABLE, 0x20);
  CHECK_EQ(LBS_HASSTRINGS, 0x40);
  CHECK_EQ(LBS_USETABSTOPS, 0x80);
  CHECK_EQ(LBS_NOINTEGRALHEIGHT, 0x100);
  CHECK_EQ(LBS_EXTENDEDSEL, 0x800);
  CHECK_EQ(LBN_SELCHANGE, 1);
  CHECK_EQ(LBN_DBLCLK, 2);
  CHECK_EQ(WM_MEASUREITEM, 0x2C);
  CHECK_EQ(WM_DELETEITEM, 0x2D);
  CHECK_EQ(WM_COMPAREITEM, 0x39);
  CHECK_EQ(sizeof(MEASUREITEMSTRUCT16), 14u); /* 5 words + DWORD (winuser16.h 500-508) */
  CHECK_EQ(sizeof(DELETEITEMSTRUCT16), 12u); /* 4 words + DWORD (winuser16.h 510-517) */
  CHECK_EQ(sizeof(COMPAREITEMSTRUCT16), 18u);
  CHECK_EQ(sizeof(DRAWITEMSTRUCT16), 26u);
  CHECK_EQ(USER_CB_LISTBOX, MK_FP(MEM_SEL_CALLBACK, 0x60));
}

/* The literal Win16 numbers are what the class answers; Win32's LB_ADDSTRING (0x0180) is not. */
static void test_win16_numbers_literal(void) {
  HWND16 l;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 10, 10, 100, 80, 201);
  CHECK_EQ(SMS(l, 0x0401, 0, "one"), 0);  /* LB_ADDSTRING */
  CHECK_EQ(SMS(l, 0x0401, 0, "two"), 1);
  CHECK_EQ(SM(l, 0x040C, 0, 0), 2);       /* LB_GETCOUNT */
  CHECK_EQ(SM(l, 0x0407, 1, 0), 1);       /* LB_SETCURSEL */
  CHECK_EQ(SM(l, 0x0409, 0, 0), 1);       /* LB_GETCURSEL */
  CHECK_EQ(SMS(l, 0x0180, 0, "three"), 0); /* the Win32 number is not a listbox message here */
  CHECK_EQ(SM(l, 0x040C, 0, 0), 2);
  end();
}

static void test_class_and_basic(void) {
  HWND16 l;
  FARPTR p;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 10, 10, 100, 80, 202);
  CHECK_EQ(GetWindowLong(l, GWL_WNDPROC), USER_CB_LISTBOX);
  CHECK_EQ(SM(l, WM_GETDLGCODE, 0, 0), DLGC_WANTARROWS | DLGC_WANTCHARS);
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), ERR32);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "alpha"), 0);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "beta"), 1);
  CHECK_EQ(SMS(l, LB_INSERTSTRING, 1, "mid"), 1);
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 3);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 0), "alpha"), 0);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 1), "mid"), 0);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 2), "beta"), 0);
  CHECK_EQ(SM(l, LB_GETTEXTLEN, 2, 0), 4);
  CHECK_EQ(SM(l, LB_GETTEXTLEN, 9, 0), ERR32);
  p = far_alloc(32);
  CHECK_EQ(SM(l, LB_GETTEXT, 0, p), 5);
  CHECK_EQ(SM(l, LB_GETTEXT, 9, p), ERR32);
  CHECK_EQ(SMS(l, LB_INSERTSTRING, 9, "bad"), ERR32);
  CHECK_EQ(SMS(l, LB_INSERTSTRING, 0xFFFF, "last"), 3); /* index -1 appends */
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 3), "last"), 0);
  CHECK_EQ(SM(l, LB_DELETESTRING, 1, 0), 3); /* returns the new count */
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 1), "beta"), 0);
  CHECK_EQ(SM(l, LB_DELETESTRING, 9, 0), ERR32);
  CHECK_EQ(SM(l, LB_RESETCONTENT, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 0);
  /* item data */
  SMS(l, LB_ADDSTRING, 0, "a");
  CHECK_EQ(SM(l, LB_SETITEMDATA, 0, 0x12345678), 0);
  CHECK_EQ(SM(l, LB_GETITEMDATA, 0, 0), 0x12345678);
  CHECK_EQ(SM(l, LB_SETITEMDATA, 5, 1), ERR32);
  CHECK_EQ(SM(l, LB_GETITEMDATA, 5, 0), ERR32);
  end();
}

static void test_sort(void) {
  HWND16 l;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_SORT, 10, 10, 100, 80, 203);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "b"), 0);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "a"), 0); /* sorts before "b" */
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 0), "a"), 0);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 1), "b"), 0);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "c"), 2);
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "B2"), 2); /* case-insensitive order: a b B2 c */
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 2), "B2"), 0);
  /* LB_INSERTSTRING ignores LBS_SORT */
  CHECK_EQ(SMS(l, LB_INSERTSTRING, 0, "zz"), 0);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 0), "zz"), 0);
  end();
}

static void test_find_and_select_string(void) {
  HWND16 l;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS, 10, 10, 100, 80, 204);
  SMS(l, LB_ADDSTRING, 0, "Apple");
  SMS(l, LB_ADDSTRING, 0, "apricot");
  SMS(l, LB_ADDSTRING, 0, "Banana");
  SMS(l, LB_ADDSTRING, 0, "ap");
  CHECK_EQ(SMS(l, LB_FINDSTRING, 0xFFFF, "ap"), 0);   /* prefix, case-insensitive, from the top */
  CHECK_EQ(SMS(l, LB_FINDSTRING, 0, "ap"), 1);        /* search starts after index 0 */
  CHECK_EQ(SMS(l, LB_FINDSTRING, 2, "ap"), 3);
  CHECK_EQ(SMS(l, LB_FINDSTRING, 3, "ap"), 0);        /* wraps */
  CHECK_EQ(SMS(l, LB_FINDSTRING, 0xFFFF, "zz"), ERR32);
  CHECK_EQ(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "ap"), 3);
  CHECK_EQ(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "APPLE"), 0);
  CHECK_EQ(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "app"), ERR32);
  CHECK_EQ(SMS(l, LB_SELECTSTRING, 0xFFFF, "ban"), 2);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 2);
  log_clear();
  CHECK_EQ(SMS(l, LB_SELECTSTRING, 0xFFFF, "nothing"), ERR32);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 2);
  CHECK_EQ(ncmd(), 0); /* messages never notify */
  end();
}

static void test_selection_single(void) {
  HWND16 l;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS, 10, 10, 100, 80, 205);
  add3(l);
  log_clear();
  CHECK_EQ(SM(l, LB_SETCURSEL, 1, 0), 1);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  CHECK_EQ(SM(l, LB_GETSEL, 1, 0), 1);
  CHECK_EQ(SM(l, LB_GETSEL, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETSEL, 9, 0), ERR32);
  CHECK_EQ(SM(l, LB_SETCURSEL, 0xFFFF, 0), ERR32); /* -1 removes the selection and returns LB_ERR */
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), ERR32);
  CHECK_EQ(SM(l, LB_SETCURSEL, 9, 0), ERR32);
  CHECK_EQ(ncmd(), 0); /* LB_SETCURSEL never notifies */
  /* LB_SETSEL is for multiple-selection lists */
  CHECK_EQ(SM(l, LB_SETSEL, 1, 0), ERR32);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), ERR32);
  end();
}

static void test_mouse_select_notify(void) {
  HWND16 l;
  int h;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 20, 20, 120, 80, 206);
  add3(l);
  h = ih(l);
  CHECK(h >= 8 && h <= 30);
  log_clear();
  click_item(20, 20, h, 1);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  CHECK_EQ(count_code(LBN_SELCHANGE), 1);
  CHECK_EQ(cmd(0)->wp, 206);
  CHECK_EQ((uint16_t)cmd(0)->lp, l);
  CHECK_EQ(GetFocus(), l);
  click_item(20, 20, h, 2);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 2);
  CHECK_EQ(count_code(LBN_SELCHANGE), 2);
  log_clear();
  dblclick_at(20 + 20, 20 + 1 + h / 2);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 0);
  CHECK_EQ(count_code(LBN_DBLCLK), 1);
  /* below the last item: nothing is selected there */
  click_at(20 + 20, 20 + 70);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 0);
  /* without LBS_NOTIFY there is no notification at all */
  {
    HWND16 q = L("Q", LBS_HASSTRINGS | WS_BORDER, 200, 20, 120, 80, 207);
    add3(q);
    log_clear();
    click_at(200 + 20, 20 + 1 + h / 2);
    CHECK_EQ(SM(q, LB_GETCURSEL, 0, 0), 0);
    for (int i = 0; i < ncmd(); i++) CHECK(cmd(i)->wp != 207); /* (the first list reports LBN_KILLFOCUS) */
  }
  /* dragging with the button down moves the selection; the notification comes on the button up */
  log_clear();
  mouse(PX0 + 20 + 20, PY0 + 20 + 1 + h / 2, 1);
  mouse(PX0 + 20 + 20, PY0 + 20 + 1 + h + h / 2, 1);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  CHECK_EQ(count_code(LBN_SELCHANGE), 0);
  mouse(PX0 + 20 + 20, PY0 + 20 + 1 + h + h / 2, 0);
  CHECK_EQ(count_code(LBN_SELCHANGE), 1);
  end();
}

static void test_keyboard(void) {
  HWND16 l;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 20, 20, 120, 80, 208);
  add3(l);
  SMS(l, LB_ADDSTRING, 0, "delta");
  SMS(l, LB_ADDSTRING, 0, "echo");
  SetFocus(l);
  SM(l, LB_SETCURSEL, 0, 0);
  log_clear();
  tap(VK_DOWN);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  CHECK_EQ(count_code(LBN_SELCHANGE), 1);
  tap(VK_DOWN);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 2);
  tap(VK_UP);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  tap(VK_END);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 4);
  log_clear();
  tap(VK_DOWN); /* at the end: no change, no notification */
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 4);
  CHECK_EQ(ncmd(), 0);
  tap(VK_HOME);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 0);
  /* type-ahead: a character selects the next item starting with it */
  log_clear();
  tap('G');
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 2);
  CHECK_EQ(count_code(LBN_SELCHANGE), 1);
  tap('E');
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 4);
  tap('B'); /* wraps round to "beta" */
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 1);
  /* Next / Prior move by a page */
  SM(l, LB_SETCURSEL, 0, 0);
  tap(VK_NEXT);
  CHECK(SM(l, LB_GETCURSEL, 0, 0) > 0);
  end();
}

static void test_multiple_selection(void) {
  HWND16 l;
  FARPTR p;
  int h;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_MULTIPLESEL | WS_BORDER, 20, 20, 120, 80, 209);
  add3(l);
  SMS(l, LB_ADDSTRING, 0, "delta");
  h = ih(l);
  CHECK_EQ(SM(l, LB_SETCURSEL, 1, 0), ERR32);
  CHECK_EQ(SM(l, LB_SETSEL, 1, 1), 0);
  CHECK_EQ(SM(l, LB_SETSEL, 1, 3), 0);
  CHECK_EQ(SM(l, LB_GETSEL, 1, 0), 1);
  CHECK_EQ(SM(l, LB_GETSEL, 3, 0), 1);
  CHECK_EQ(SM(l, LB_GETSEL, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 2);
  p = far_alloc(16 * sizeof(int16_t));
  CHECK_EQ(SM(l, LB_GETSELITEMS, 8, p), 2);
  CHECK_EQ(((int16_t *)LIN(p))[0], 1);
  CHECK_EQ(((int16_t *)LIN(p))[1], 3);
  CHECK_EQ(SM(l, LB_GETSELITEMS, 1, p), 1); /* at most wParam entries */
  SM(l, LB_SETSEL, 0, 1);                   /* deselect */
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 1);
  SM(l, LB_SETSEL, 1, 0xFFFF); /* index -1: every item */
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 4);
  SM(l, LB_SETSEL, 0, 0xFFFF);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 0);
  CHECK_EQ(SM(l, LB_SELITEMRANGE, 1, MKL(1, 2)), 0); /* select 1..2 */
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 2);
  CHECK_EQ(SM(l, LB_SELITEMRANGE, 0, MKL(0, 3)), 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 0);
  CHECK_EQ(SM(l, LB_SELITEMRANGEEX, 3, 1), 0); /* first > last: deselects the range */
  CHECK_EQ(SM(l, LB_SELITEMRANGEEX, 1, 2), 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 2);
  /* a click toggles an item, the others stay */
  SM(l, LB_SETSEL, 0, 0xFFFF);
  log_clear();
  click_item(20, 20, h, 0);
  click_item(20, 20, h, 2);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 2);
  CHECK_EQ(SM(l, LB_GETSEL, 0, 0), 1);
  CHECK_EQ(SM(l, LB_GETSEL, 2, 0), 1);
  click_item(20, 20, h, 0);
  CHECK_EQ(SM(l, LB_GETSEL, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 1);
  CHECK_EQ(count_code(LBN_SELCHANGE), 3);
  /* Space toggles the caret item */
  SetFocus(l);
  SM(l, LB_SETCARETINDEX, 3, 0);
  tap(VK_SPACE);
  CHECK_EQ(SM(l, LB_GETSEL, 3, 0), 1);
  tap(VK_SPACE);
  CHECK_EQ(SM(l, LB_GETSEL, 3, 0), 0);
  /* LB_GETCURSEL returns the caret item */
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 3);
  CHECK_EQ(SM(l, LB_GETCARETINDEX, 0, 0), 3);
  end();
}

static void test_extended_selection(void) {
  HWND16 l;
  int h;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_EXTENDEDSEL | WS_BORDER, 20, 20, 120, 100, 210);
  add3(l);
  SMS(l, LB_ADDSTRING, 0, "delta");
  SMS(l, LB_ADDSTRING, 0, "echo");
  h = ih(l);
  click_item(20, 20, h, 1);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 1);
  click_item(20, 20, h, 3); /* a plain click replaces the selection */
  CHECK_EQ(SM(l, LB_GETSEL, 1, 0), 0);
  CHECK_EQ(SM(l, LB_GETSEL, 3, 0), 1);
  key(VK_CONTROL, 0x1D, 1);
  click_item(20, 20, h, 0); /* Ctrl+click toggles */
  key(VK_CONTROL, 0x1D, 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 2);
  click_item(20, 20, h, 1); /* anchor 1 */
  key(VK_SHIFT, 0x2A, 1);
  click_item(20, 20, h, 4); /* Shift+click selects the range 1..4 */
  key(VK_SHIFT, 0x2A, 0);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 4);
  CHECK_EQ(SM(l, LB_GETSEL, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETANCHORINDEX, 0, 0), 1);
  CHECK_EQ(SM(l, LB_SETANCHORINDEX, 2, 0), 0);
  CHECK_EQ(SM(l, LB_GETANCHORINDEX, 0, 0), 2);
  /* Shift+Down extends from the anchor */
  SetFocus(l);
  click_item(20, 20, h, 0);
  tap_with(VK_SHIFT, VK_DOWN);
  tap_with(VK_SHIFT, VK_DOWN);
  CHECK_EQ(SM(l, LB_GETSELCOUNT, 0, 0), 3);
  end();
}

static struct {
  int measure, draw, del, cmp;
  MEASUREITEMSTRUCT16 m[8];
  DRAWITEMSTRUCT16 d[16];
  DELETEITEMSTRUCT16 x[8];
  COMPAREITEMSTRUCT16 c[16];
  uint16_t mh;
  int cmp_ret;
} g_od;
static int hook_od(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)h;
  (void)wp;
  if (msg == WM_MEASUREITEM && g_od.measure < 8) {
    MEASUREITEMSTRUCT16 *m = LIN(lp);
    g_od.m[g_od.measure++] = *m;
    if (g_od.mh) m->itemHeight = g_od.mh;
    return 1;
  }
  if (msg == WM_DRAWITEM && g_od.draw < 16) {
    g_od.d[g_od.draw++] = *(const DRAWITEMSTRUCT16 *)LIN(lp);
    return 1;
  }
  if (msg == WM_DELETEITEM && g_od.del < 8) {
    g_od.x[g_od.del++] = *(const DELETEITEMSTRUCT16 *)LIN(lp);
    return 1;
  }
  if (msg == WM_COMPAREITEM && g_od.cmp < 16) {
    g_od.c[g_od.cmp++] = *(const COMPAREITEMSTRUCT16 *)LIN(lp);
    g_hook_ret = (uint32_t)(int32_t)g_od.cmp_ret;
    return 1;
  }
  return 0;
}

static void test_owner_draw_fixed(void) {
  HWND16 l;
  int n;
  begin();
  memset(&g_od, 0, sizeof g_od);
  g_od.mh = 20;
  g_hook = hook_od;
  g_hook_ret = 20;
  l = L("L", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_BORDER, 20, 20, 120, 50, 301);
  /* WM_MEASUREITEM once, at creation */
  CHECK_EQ(g_od.measure, 1);
  CHECK_EQ(g_od.m[0].CtlType, ODT_LISTBOX);
  CHECK_EQ(g_od.m[0].CtlID, 301);
  CHECK_EQ(g_od.m[0].itemID, 0);
  CHECK(g_od.m[0].itemWidth > 0);
  CHECK_EQ(SM(l, LB_GETITEMHEIGHT, 0, 0), 20); /* the parent's answer is the item height */
  SMS(l, LB_ADDSTRING, 0, "one");
  SMS(l, LB_ADDSTRING, 0, "two");
  SMS(l, LB_ADDSTRING, 0, "three");
  SMS(l, LB_ADDSTRING, 0, "four");
  CHECK_EQ(g_od.measure, 1); /* still once */
  g_od.draw = 0;
  InvalidateRect(l, 0, 1);
  pump();
  /* client height 48 (50 less the border) shows 2 whole items and part of the third */
  n = g_od.draw;
  CHECK(n >= 2 && n <= 3);
  for (int i = 0; i < n; i++) {
    const DRAWITEMSTRUCT16 *d = &g_od.d[i];
    CHECK_EQ(d->CtlType, ODT_LISTBOX);
    CHECK_EQ(d->CtlID, 301);
    CHECK_EQ(d->itemID, i);
    CHECK_EQ(d->itemAction, ODA_DRAWENTIRE);
    CHECK_EQ(d->hwndItem, l);
    CHECK(d->hDC != 0);
    CHECK_EQ(d->rcItem.left, 0);
    CHECK_EQ(d->rcItem.top, i * 20);
    CHECK_EQ(d->rcItem.bottom, i * 20 + 20);
    CHECK(d->rcItem.right >= 100);
    CHECK_EQ(d->itemState & ODS_SELECTED, 0);
  }
  /* itemData is the LB_SETITEMDATA value (0 until set) */
  CHECK_EQ(g_od.d[0].itemData, 0);
  SM(l, LB_SETITEMDATA, 0, 0x4242);
  g_od.draw = 0;
  InvalidateRect(l, 0, 1);
  pump();
  CHECK(g_od.draw >= 1);
  CHECK_EQ(g_od.d[0].itemData, 0x4242);
  /* selecting an item redraws it with ODA_SELECT and ODS_SELECTED */
  g_od.draw = 0;
  SM(l, LB_SETCURSEL, 1, 0);
  {
    int sel = 0;
    for (int i = 0; i < g_od.draw; i++)
      if (g_od.d[i].itemID == 1 && (g_od.d[i].itemState & ODS_SELECTED) && g_od.d[i].itemAction == ODA_SELECT) sel = 1;
    CHECK(sel);
  }
  /* WM_DELETEITEM for each deleted item, and for each item of LB_RESETCONTENT */
  g_od.del = 0;
  SM(l, LB_DELETESTRING, 0, 0);
  CHECK_EQ(g_od.del, 1);
  CHECK_EQ(g_od.x[0].CtlType, ODT_LISTBOX);
  CHECK_EQ(g_od.x[0].CtlID, 301);
  CHECK_EQ(g_od.x[0].itemID, 0);
  CHECK_EQ(g_od.x[0].hwndItem, l);
  g_od.del = 0;
  SM(l, LB_RESETCONTENT, 0, 0);
  CHECK_EQ(g_od.del, 3);
  end();
}

static void test_owner_draw_no_strings_and_compare(void) {
  HWND16 l;
  begin();
  memset(&g_od, 0, sizeof g_od);
  g_od.mh = 16;
  g_hook = hook_od;
  l = L("L", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_SORT | LBS_NOINTEGRALHEIGHT | WS_BORDER, 20, 20, 120, 80, 302);
  /* without LBS_HASSTRINGS lParam is the item data, WM_COMPAREITEM orders a sorted list */
  g_od.cmp_ret = 1;
  CHECK_EQ(SM(l, LB_ADDSTRING, 0, 0x1111), 0);
  g_od.cmp_ret = -1; /* new item sorts before the existing one */
  CHECK_EQ(SM(l, LB_ADDSTRING, 0, 0x2222), 0);
  CHECK_EQ(g_od.cmp, 1);
  CHECK_EQ(g_od.c[0].CtlType, ODT_LISTBOX);
  CHECK_EQ(g_od.c[0].CtlID, 302);
  CHECK_EQ(g_od.c[0].hwndItem, l);
  CHECK_EQ(g_od.c[0].itemData1, 0x2222); /* the new item first */
  CHECK_EQ(g_od.c[0].itemData2, 0x1111);
  CHECK_EQ(g_od.c[0].itemID1, 0xFFFF); /* not in the list yet */
  CHECK_EQ(g_od.c[0].itemID2, 0);
  CHECK_EQ(SM(l, LB_GETITEMDATA, 0, 0), 0x2222);
  CHECK_EQ(SM(l, LB_GETITEMDATA, 1, 0), 0x1111);
  CHECK_EQ(SM(l, LB_GETTEXTLEN, 0, 0), 4); /* no strings: LB_GETTEXT returns the 4 bytes of data */
  g_od.draw = 0;
  InvalidateRect(l, 0, 1);
  pump();
  CHECK_EQ(g_od.draw, 2);
  CHECK_EQ(g_od.d[0].itemData, 0x2222);
  CHECK_EQ(g_od.d[1].itemData, 0x1111);
  end();
}

static void test_owner_draw_variable(void) {
  HWND16 l;
  begin();
  memset(&g_od, 0, sizeof g_od);
  g_od.mh = 14;
  g_hook = hook_od;
  l = L("L", LBS_NOTIFY | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_BORDER, 20, 20, 120, 80, 303);
  CHECK_EQ(g_od.measure, 0); /* variable: per item, not at creation */
  SMS(l, LB_ADDSTRING, 0, "a");
  g_od.mh = 24;
  SMS(l, LB_ADDSTRING, 0, "b");
  CHECK_EQ(g_od.measure, 2);
  CHECK_EQ(g_od.m[0].itemID, 0);
  CHECK_EQ(g_od.m[1].itemID, 1);
  CHECK_EQ(SM(l, LB_GETITEMHEIGHT, 0, 0), 14);
  CHECK_EQ(SM(l, LB_GETITEMHEIGHT, 1, 0), 24);
  g_od.draw = 0;
  InvalidateRect(l, 0, 1);
  pump();
  CHECK_EQ(g_od.draw, 2);
  CHECK_EQ(g_od.d[1].rcItem.top, 14);
  CHECK_EQ(g_od.d[1].rcItem.bottom, 38);
  end();
}

static void test_scroll_and_item_rect(void) {
  HWND16 l;
  int h, vis;
  FARPTR r;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_BORDER | WS_VSCROLL, 20, 20, 120, 60, 211);
  for (int i = 0; i < 20; i++) {
    char b[16];
    snprintf(b, sizeof b, "item%02d", i);
    SMS(l, LB_ADDSTRING, 0, b);
  }
  h = ih(l);
  r = far_alloc(sizeof(RECT16));
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETITEMRECT, 0, r), 0);
  {
    RECT16 *q = LIN(r);
    CHECK_EQ(q->left, 0);
    CHECK_EQ(q->top, 0);
    CHECK_EQ(q->bottom, h);
    CHECK(q->right > 50);
  }
  CHECK_EQ(SM(l, LB_GETITEMRECT, 2, r), 0);
  CHECK_EQ(((RECT16 *)LIN(r))->top, 2 * h);
  CHECK_EQ(SM(l, LB_GETITEMRECT, 99, r), ERR32);
  CHECK_EQ(SM(l, LB_SETTOPINDEX, 5, 0), 0);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 5);
  CHECK_EQ(SM(l, LB_GETITEMRECT, 5, r), 0);
  CHECK_EQ(((RECT16 *)LIN(r))->top, 0);
  CHECK_EQ(SM(l, LB_SETTOPINDEX, 99, 0), ERR32);
  /* the scroll bar position follows */
  CHECK_EQ(GetScrollPos(l, SB_VERT), 5);
  {
    FARPTR lo = far_alloc(4), hi = far_alloc(4);
    GetScrollRange(l, SB_VERT, lo, hi);
    vis = 20 - *(int16_t *)LIN(hi);
    CHECK(vis >= 1 && vis <= 4); /* range max = count - visible items */
  }
  /* the scroll bar messages */
  SM(l, WM_VSCROLL, SB_LINEDOWN, 0);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 6);
  SM(l, WM_VSCROLL, SB_LINEUP, 0);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 5);
  SM(l, WM_VSCROLL, SB_TOP, 0);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 0);
  SM(l, WM_VSCROLL, SB_THUMBPOSITION, MKL(7, 0));
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 7);
  SM(l, WM_VSCROLL, SB_BOTTOM, 0);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 20 - vis);
  SM(l, WM_VSCROLL, SB_PAGEUP, 0);
  CHECK(SM(l, LB_GETTOPINDEX, 0, 0) < (uint32_t)(20 - vis));
  /* LB_SETCURSEL scrolls the item into view */
  SM(l, LB_SETTOPINDEX, 0, 0);
  SM(l, LB_SETCURSEL, 15, 0);
  CHECK(SM(l, LB_GETTOPINDEX, 0, 0) > 0);
  CHECK(SM(l, LB_GETTOPINDEX, 0, 0) <= 15);
  CHECK(SM(l, LB_GETTOPINDEX, 0, 0) + (uint32_t)vis > 15);
  /* keyboard keeps the caret visible */
  SetFocus(l);
  tap(VK_HOME);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 0);
  tap(VK_END);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 19);
  CHECK_EQ(SM(l, LB_GETTOPINDEX, 0, 0), 20 - vis);
  /* a click on the scroll bar arrow scrolls (non-client area tracking sends WM_VSCROLL) */
  SM(l, LB_SETTOPINDEX, 0, 0);
  {
    UNcLayout nc;
    CHECK(user_nc_layout(l, &nc));
    CHECK(nc.vscroll.right > nc.vscroll.left);
    click_at(20 + nc.vscroll.left + 3, 20 + nc.vscroll.bottom - 4); /* the down arrow */
    CHECK(SM(l, LB_GETTOPINDEX, 0, 0) >= 1);
  }
  end();
}

static void test_integral_height_and_set_item_height(void) {
  HWND16 l;
  FARPTR r;
  int h, ch;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 20, 20, 120, 75, 212);
  h = ih(l);
  r = far_alloc(sizeof(RECT16));
  GetClientRect(l, r);
  ch = ((RECT16 *)LIN(r))->bottom;
  CHECK_EQ(ch % h, 0); /* without LBS_NOINTEGRALHEIGHT the height holds whole items only */
  CHECK(ch > 0 && ch <= 73);
  {
    HWND16 n = L("N", LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_BORDER, 200, 20, 120, 75, 213);
    GetClientRect(n, r);
    CHECK_EQ(((RECT16 *)LIN(r))->bottom, 73);
  }
  CHECK_EQ(SM(l, LB_SETITEMHEIGHT, 0, 20), 0);
  CHECK_EQ(SM(l, LB_GETITEMHEIGHT, 0, 0), 20);
  CHECK_EQ(SM(l, LB_SETITEMHEIGHT, 0, 0x1000), ERR32);
  end();
}

static void test_dir(void) {
  HWND16 l;
  begin();
  vfs_init();
  vfs_put("C:\\STARS\\GAME.TXT", "x", 1);
  vfs_put("C:\\STARS\\ABC.TXT", "x", 1);
  vfs_put("C:\\STARS\\DATA.XY", "x", 1);
  vfs_mkdir("C:\\STARS\\SUB");
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_SORT | WS_BORDER | WS_VSCROLL, 20, 20, 140, 100, 214);
  /* files only: DDL_READWRITE (0), the spec filters */
  CHECK(SMS(l, LB_DIR, DDL_READWRITE, "C:\\STARS\\*.TXT") != ERR32); /* index of the last item added */
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 2);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 0), "ABC.TXT"), 0);
  CHECK_EQ(strcmp(item_text(l, LB_GETTEXT, 1), "GAME.TXT"), 0);
  SM(l, LB_RESETCONTENT, 0, 0);
  /* directories in brackets, ".." outside the root, drives as [-c-] */
  CHECK(SMS(l, LB_DIR, DDL_DIRECTORY | DDL_DRIVES, "C:\\STARS\\*.XY") != ERR32);
  CHECK(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "DATA.XY") != ERR32);
  CHECK(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "[SUB]") != ERR32);
  CHECK(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "[..]") != ERR32);
  CHECK(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "[-c-]") != ERR32);
  CHECK_EQ(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "ABC.TXT"), ERR32); /* the spec filtered it out */
  SM(l, LB_RESETCONTENT, 0, 0);
  /* DDL_EXCLUSIVE: only the directories */
  CHECK(SMS(l, LB_DIR, DDL_DIRECTORY | DDL_EXCLUSIVE, "C:\\STARS\\*.*") != ERR32);
  CHECK(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "[SUB]") != ERR32);
  CHECK_EQ(SMS(l, LB_FINDSTRINGEXACT, 0xFFFF, "DATA.XY"), ERR32);
  /* a path that does not exist */
  CHECK_EQ(SMS(l, LB_DIR, DDL_READWRITE, "C:\\NOPE\\*.*"), ERR32);
  /* a relative spec uses the current directory */
  SM(l, LB_RESETCONTENT, 0, 0);
  SMS(l, LB_DIR, DDL_READWRITE, "*.XY");
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 1);
  vfs_init();
  end();
}

static void test_tabs_hextent_misc(void) {
  HWND16 l;
  FARPTR t;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_USETABSTOPS | LBS_NOINTEGRALHEIGHT | WS_BORDER, 20, 20, 160, 60, 215);
  SMS(l, LB_ADDSTRING, 0, "a\tb");
  t = far_alloc(4);
  *(int16_t *)LIN(t) = 40; /* dialog units */
  CHECK_EQ(SM(l, LB_SETTABSTOPS, 1, t), 1);
  pump();
  {
    CBox b = cbox(20, 20, 180, 80, BLACK);
    CHECK(b.n > 4);
    CHECK(b.r > 20 + 40); /* the second field starts at about 40 dialog units = 40 * avg width / 4 pixels */
  }
  CHECK_EQ(SM(l, LB_SETTABSTOPS, 0, 0), 1); /* default tab stops */
  CHECK_EQ(SM(l, LB_GETHORIZONTALEXTENT, 0, 0), 0);
  CHECK_EQ(SM(l, LB_SETHORIZONTALEXTENT, 300, 0), 0);
  CHECK_EQ(SM(l, LB_GETHORIZONTALEXTENT, 0, 0), 300);
  CHECK_EQ(SM(l, LB_SETCOLUMNWIDTH, 50, 0), 0);
  CHECK_EQ(SM(l, LB_CARETOFF, 0, 0), 0);
  CHECK_EQ(SM(l, LB_CARETON, 0, 0), 0);
  CHECK_EQ(SM(l, LB_SETCARETINDEX, 0, 0), 0);
  CHECK_EQ(SM(l, LB_GETCARETINDEX, 0, 0), 0);
  CHECK_EQ(SM(l, LB_SETCARETINDEX, 9, 0), ERR32);
  end();
}

static void test_pixels(void) {
  HWND16 l;
  int h;
  CBox b;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 20, 20, 120, 60, 216);
  add3(l);
  SM(l, LB_SETCURSEL, 1, 0);
  h = ih(l);
  pump();
  PIXEL_TEST("ctl_listbox_basic");
  PIXEL_EQ(100, 70, WHITE_); /* below the items: the CTLCOLOR_LISTBOX brush, COLOR_WINDOW */
  b = cbox(20, 20, 140, 80, BLACK);
  CHECK(b.n > 10);
  /* the selected item: a COLOR_HIGHLIGHT bar across the whole width, h pixels high */
  b = cbox(20, 20, 140, 80, HIGHLIGHT_);
  CHECK(b.n > 100);
  CHECK_EQ(b.l, 20);
  CHECK_EQ(b.r, 20 + 119);
  CHECK_EQ(b.t, 20 + h);
  CHECK_EQ(b.b, 20 + 2 * h - 1);
  CHECK(cbox(20, 20, 140, 80, HIGHLIGHTTXT_).n > 5);
  dump_frame("ctl_listbox_basic", PX0 + 10, PY0 + 10, 140, 80);
  /* the focus rectangle is dotted on the caret item when focused: on the highlight bar it is drawn in
   * COLOR_HIGHLIGHTTEXT, so the bar gains such pixels */
  {
    int before = cbox(20, 20 + h, 140, 20 + 2 * h, HIGHLIGHTTXT_).n;
    SetFocus(l);
    pump();
    CHECK(cbox(20, 20 + h, 140, 20 + 2 * h, HIGHLIGHTTXT_).n > before + 10);
  }
  /* disabled: gray text */
  EnableWindow(l, 0);
  pump();
  CHECK(cbox(20, 20, 140, 80, GRAYTXT).n > 5);
  end();
}

static void test_subclass(void) {
  HWND16 l;
  FARPTR old;
  begin();
  l = L("L", LBS_NOTIFY | LBS_HASSTRINGS | WS_BORDER, 20, 20, 120, 80, 217);
  old = SetWindowLong(l, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_LISTBOX);
  g_old = old;
  log_clear();
  CHECK_EQ(SMS(l, LB_ADDSTRING, 0, "x"), 0); /* the subclass forwards to the class procedure */
  CHECK(log_find("L", LB_ADDSTRING, 0) != NULL);
  CHECK_EQ(log_find("L", LB_ADDSTRING, 0)->tag, 'B');
  CHECK_EQ(SM(l, LB_GETCOUNT, 0, 0), 1);
  /* a mouse click reaches the subclass first, then the class */
  log_clear();
  click_at(20 + 20, 20 + 5);
  CHECK(log_find("L", WM_LBUTTONDOWN, 0) != NULL);
  CHECK_EQ(SM(l, LB_GETCURSEL, 0, 0), 0);
  CHECK_EQ(count_code(LBN_SELCHANGE), 1);
  SetWindowLong(l, GWL_WNDPROC, old);
  end();
}

static void test_destroy_frees(void) {
  HWND16 l;
  begin();
  memset(&g_od, 0, sizeof g_od);
  g_hook = hook_od;
  g_od.mh = 16;
  l = L("L", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 20, 20, 120, 80, 218);
  add3(l);
  g_od.del = 0;
  DestroyWindow(l); /* WM_DELETEITEM for the items of an owner-draw list at destruction */
  CHECK_EQ(g_od.del, 3);
  end();
}

int main(void) {
  (void)dump;
  if (load_fixture() != 0) return 1;
  test_constants();
  test_disablenoscroll();
  test_win16_numbers_literal();
  test_class_and_basic();
  test_sort();
  test_find_and_select_string();
  test_selection_single();
  test_mouse_select_notify();
  test_keyboard();
  test_multiple_selection();
  test_extended_selection();
  test_owner_draw_fixed();
  test_owner_draw_no_strings_and_compare();
  test_owner_draw_variable();
  test_scroll_and_item_rect();
  test_integral_height_and_set_item_height();
  test_dir();
  test_tabs_hextent_misc();
  test_pixels();
  test_subclass();
  test_destroy_frees();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
