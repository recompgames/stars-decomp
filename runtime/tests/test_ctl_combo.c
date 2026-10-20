/* Expectations for the COMBOBOX control class (runtime/ctl_combo.c). Task stars-0z4.11, written
 * BEFORE the implementation. Do not edit an expectation to match the code: a disagreement is
 * reported with both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here and no reference capture exists):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h lines 234-258: CB_GETEDITSEL16 WM_USER+0,
 *       CB_LIMITTEXT16 +1, CB_SETEDITSEL16 +2, CB_ADDSTRING16 +3, CB_DELETESTRING16 +4, CB_DIR16 +5,
 *       CB_GETCOUNT16 +6, CB_GETCURSEL16 +7, CB_GETLBTEXT16 +8, CB_GETLBTEXTLEN16 +9,
 *       CB_INSERTSTRING16 +10, CB_RESETCONTENT16 +11, CB_FINDSTRING16 +12, CB_SELECTSTRING16 +13,
 *       CB_SETCURSEL16 +14, CB_SHOWDROPDOWN16 +15, CB_GETITEMDATA16 +16, CB_SETITEMDATA16 +17,
 *       CB_GETDROPPEDCONTROLRECT16 +18, CB_SETITEMHEIGHT16 +19, CB_GETITEMHEIGHT16 +20,
 *       CB_SETEXTENDEDUI16 +21, CB_GETEXTENDEDUI16 +22, CB_GETDROPPEDSTATE16 +23,
 *       CB_FINDSTRINGEXACT16 +24.  Win32 uses 0x0140..: NOT used.
 *   Styles / codes         include/winuser.rh 1341-1350 (CBS_*), include/winuser.h 1983-1990 (CBN_*):
 *       CBN_SELCHANGE 1, CBN_DBLCLK 2, CBN_SETFOCUS 3, CBN_KILLFOCUS 4, CBN_EDITCHANGE 5,
 *       CBN_EDITUPDATE 6, CBN_DROPDOWN 7.  CBN_CLOSEUP / SELENDOK / SELENDCANCEL (8-10) are Win32:
 *       never sent.  UNVERIFIED for Windows 3.1.
 *   Owner-draw             WM_MEASUREITEM / WM_DRAWITEM / WM_DELETEITEM / WM_COMPAREITEM with
 *       CtlType ODT_COMBOBOX (3), CtlID = the combo box id, hwndItem = the combo box; the field is
 *       drawn with ODS_COMBOBOXEDIT (0x1000, include/winuser.h 884; UNVERIFIED for Win16).
 *
 * UNVERIFIED (this runtime's choices, Wine's combo design): the drop-down part is a popup LISTBOX
 * (owned by the dialog, shown with SW_SHOWNOACTIVATE, mouse capture while dropped); a CBS_DROPDOWN /
 * CBS_DROPDOWNLIST combo box shrinks to the height of its closed part (the height given at creation
 * less the list is the list height); CBS_SIMPLE keeps its size with the list below the edit; an
 * edit child (id 1001) and a list (id 1000) report to the combo box; the closed height is the item
 * height + 6; a click on the field toggles the list, a click outside closes it, Esc closes it, F4
 * toggles it, Up/Down change the selection (CBN_SELCHANGE); GetWindowText of a CBS_DROPDOWNLIST combo
 * returns the selected item; focus goes to the edit field of a CBS_DROPDOWN / CBS_SIMPLE combo.
 * Not implemented (documented): the extended UI beyond storing the flag, CBS_OEMCONVERT, drag-
 * through selection (press on the field, release on an item).
 */
#include "test.h"

#include "ctl2_test_common.h"

static HWND16 C(const char *label, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "COMBOBOX", "", style | WS_TABSTOP, x, y, w, h, id);
}
static void fill5(HWND16 c) {
  SMS(c, CB_ADDSTRING, 0, "item0");
  SMS(c, CB_ADDSTRING, 0, "item1");
  SMS(c, CB_ADDSTRING, 0, "item2");
  SMS(c, CB_ADDSTRING, 0, "item3");
  SMS(c, CB_ADDSTRING, 0, "item4");
}
static RECT16 wrect(HWND16 c) { return get_rect(c, 0); }
static RECT16 droprect(HWND16 c) {
  FARPTR p = far_alloc(sizeof(RECT16));
  RECT16 r;
  SM(c, CB_GETDROPPEDCONTROLRECT, 0, p);
  memcpy(&r, LIN(p), sizeof r);
  return r;
}
static int dropped(HWND16 c) { return (int)SM(c, CB_GETDROPPEDSTATE, 0, 0); }
/* screen point of item i of the dropped list */
static void item_pt(HWND16 c, int i, int *sx, int *sy) {
  RECT16 cr = wrect(c);
  int h = (int)SM(c, CB_GETITEMHEIGHT, 0, 0);
  *sx = cr.left + 20;
  *sy = cr.bottom + 1 + i * h + h / 2;
}
static void click_screen(int sx, int sy) {
  mouse(sx, sy, 1);
  mouse(sx, sy, 0);
}
static HWND16 child_edit(HWND16 c) {
  for (HWND16 k = GetWindow(c, GW_CHILD); k; k = GetWindow(k, GW_HWNDNEXT))
    if (GetWindowLong(k, GWL_WNDPROC) == USER_CB_EDIT) return k;
  return 0;
}

static void test_constants(void) {
  CHECK_EQ(CB_GETEDITSEL, 0x0400);
  CHECK_EQ(CB_LIMITTEXT, 0x0401);
  CHECK_EQ(CB_SETEDITSEL, 0x0402);
  CHECK_EQ(CB_ADDSTRING, 0x0403);
  CHECK_EQ(CB_DELETESTRING, 0x0404);
  CHECK_EQ(CB_DIR, 0x0405);
  CHECK_EQ(CB_GETCOUNT, 0x0406);
  CHECK_EQ(CB_GETCURSEL, 0x0407);
  CHECK_EQ(CB_GETLBTEXT, 0x0408);
  CHECK_EQ(CB_GETLBTEXTLEN, 0x0409);
  CHECK_EQ(CB_INSERTSTRING, 0x040A);
  CHECK_EQ(CB_RESETCONTENT, 0x040B);
  CHECK_EQ(CB_FINDSTRING, 0x040C);
  CHECK_EQ(CB_SELECTSTRING, 0x040D);
  CHECK_EQ(CB_SETCURSEL, 0x040E);
  CHECK_EQ(CB_SHOWDROPDOWN, 0x040F);
  CHECK_EQ(CB_GETITEMDATA, 0x0410);
  CHECK_EQ(CB_SETITEMDATA, 0x0411);
  CHECK_EQ(CB_GETDROPPEDCONTROLRECT, 0x0412);
  CHECK_EQ(CB_SETITEMHEIGHT, 0x0413);
  CHECK_EQ(CB_GETITEMHEIGHT, 0x0414);
  CHECK_EQ(CB_SETEXTENDEDUI, 0x0415);
  CHECK_EQ(CB_GETEXTENDEDUI, 0x0416);
  CHECK_EQ(CB_GETDROPPEDSTATE, 0x0417);
  CHECK_EQ(CB_FINDSTRINGEXACT, 0x0418);
  CHECK_EQ(CBS_SIMPLE, 1);
  CHECK_EQ(CBS_DROPDOWN, 2);
  CHECK_EQ(CBS_DROPDOWNLIST, 3);
  CHECK_EQ(CBS_OWNERDRAWFIXED, 0x10);
  CHECK_EQ(CBS_SORT, 0x100);
  CHECK_EQ(CBS_HASSTRINGS, 0x200);
  CHECK_EQ(CBN_SELCHANGE, 1);
  CHECK_EQ(CBN_DBLCLK, 2);
  CHECK_EQ(CBN_SETFOCUS, 3);
  CHECK_EQ(CBN_KILLFOCUS, 4);
  CHECK_EQ(CBN_EDITCHANGE, 5);
  CHECK_EQ(CBN_EDITUPDATE, 6);
  CHECK_EQ(CBN_DROPDOWN, 7);
  CHECK_EQ(USER_CB_COMBOBOX, MK_FP(MEM_SEL_CALLBACK, 0x70));
}

static void test_class_and_literal_numbers(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 401);
  CHECK_EQ(GetWindowLong(c, GWL_WNDPROC), USER_CB_COMBOBOX);
  CHECK_EQ(SMS(c, 0x0403, 0, "one"), 0); /* CB_ADDSTRING */
  CHECK_EQ(SMS(c, 0x0403, 0, "two"), 1);
  CHECK_EQ(SM(c, 0x0406, 0, 0), 2);      /* CB_GETCOUNT */
  CHECK_EQ(SM(c, 0x040E, 1, 0), 1);      /* CB_SETCURSEL */
  CHECK_EQ(SM(c, 0x0407, 0, 0), 1);      /* CB_GETCURSEL */
  CHECK_EQ(SMS(c, 0x0140, 0, "win32"), 0); /* the Win32 CB_ADDSTRING (0x0143) / others are nothing here */
  CHECK_EQ(SM(c, 0x0406, 0, 0), 2);
  end();
}

static void test_items(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 402);
  CHECK_EQ(SM(c, CB_GETCOUNT, 0, 0), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), ERR32);
  fill5(c);
  CHECK_EQ(SM(c, CB_GETCOUNT, 0, 0), 5);
  CHECK_EQ(SMS(c, CB_INSERTSTRING, 1, "ins"), 1);
  CHECK_EQ(strcmp(item_text(c, CB_GETLBTEXT, 1), "ins"), 0);
  CHECK_EQ(strcmp(item_text(c, CB_GETLBTEXT, 2), "item1"), 0);
  CHECK_EQ(SM(c, CB_GETLBTEXTLEN, 1, 0), 3);
  CHECK_EQ(SM(c, CB_GETLBTEXTLEN, 99, 0), ERR32);
  CHECK_EQ(SM(c, CB_DELETESTRING, 1, 0), 5);
  CHECK_EQ(SM(c, CB_DELETESTRING, 99, 0), ERR32);
  CHECK_EQ(SMS(c, CB_FINDSTRING, 0xFFFF, "ITEM3"), 3);
  CHECK_EQ(SMS(c, CB_FINDSTRINGEXACT, 0xFFFF, "item3"), 3);
  CHECK_EQ(SMS(c, CB_FINDSTRINGEXACT, 0xFFFF, "item"), ERR32);
  CHECK_EQ(SMS(c, CB_FINDSTRING, 2, "item"), 3);
  CHECK_EQ(SM(c, CB_SETITEMDATA, 2, 0xABCDEF01), 0);
  CHECK_EQ(SM(c, CB_GETITEMDATA, 2, 0), 0xABCDEF01);
  CHECK_EQ(SM(c, CB_SETCURSEL, 3, 0), 3);
  log_clear();
  CHECK_EQ(SM(c, CB_SETCURSEL, 4, 0), 4);
  CHECK_EQ(ncmd(), 0); /* messages never notify */
  CHECK_EQ(SMS(c, CB_SELECTSTRING, 0xFFFF, "item2"), 2);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  CHECK_EQ(SM(c, CB_SETCURSEL, 0xFFFF, 0), ERR32);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), ERR32);
  CHECK_EQ(SM(c, CB_RESETCONTENT, 0, 0), 0);
  CHECK_EQ(SM(c, CB_GETCOUNT, 0, 0), 0);
  /* extended UI flag is stored */
  CHECK_EQ(SM(c, CB_GETEXTENDEDUI, 0, 0), 0);
  CHECK_EQ(SM(c, CB_SETEXTENDEDUI, 1, 0), 0);
  CHECK_EQ(SM(c, CB_GETEXTENDEDUI, 0, 0), 1);
  end();
}

static void test_sort_style(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | CBS_SORT | CBS_HASSTRINGS | WS_VSCROLL, 20, 20, 120, 90, 403);
  SMS(c, CB_ADDSTRING, 0, "b");
  SMS(c, CB_ADDSTRING, 0, "a");
  CHECK_EQ(strcmp(item_text(c, CB_GETLBTEXT, 0), "a"), 0);
  end();
}

static void test_geometry(void) {
  HWND16 c, s;
  RECT16 r, rs;
  int h;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 404);
  r = wrect(c);
  h = (int)SM(c, CB_GETITEMHEIGHT, 0, 0);
  CHECK(h >= 8 && h <= 30);
  CHECK_EQ(r.left, PX0 + 20);
  CHECK_EQ(r.right, PX0 + 140);
  CHECK(r.bottom - r.top < 90);            /* only the closed part is the window */
  CHECK(r.bottom - r.top >= h + 2);
  CHECK_EQ(SM(c, CB_GETITEMHEIGHT, 0xFFFF, 0), r.bottom - r.top - 0); /* index -1: the selection field height */
  s = C("S", CBS_SIMPLE | WS_VSCROLL, 200, 20, 120, 90, 405);
  rs = wrect(s);
  CHECK_EQ(rs.bottom - rs.top, 90); /* a simple combo box keeps its size */
  end();
}

static void test_dropdownlist_open_select(void) {
  HWND16 c;
  int sx, sy;
  RECT16 cr, dr;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 406);
  fill5(c);
  SM(c, CB_SETCURSEL, 0, 0);
  cr = wrect(c);
  CHECK_EQ(dropped(c), 0);
  log_clear();
  click_at(20 + 30, 20 + 5); /* a click on the field opens the list */
  CHECK_EQ(dropped(c), 1);
  CHECK_EQ(GetFocus(), c);
  CHECK_CODES("0003,0007"); /* CBN_SETFOCUS then CBN_DROPDOWN */
  CHECK_EQ(cmd(1)->wp, 406);
  CHECK_EQ((uint16_t)cmd(1)->lp, c);
  dr = droprect(c);
  CHECK_EQ(dr.left, cr.left);
  CHECK_EQ(dr.top, cr.top);
  CHECK_EQ(dr.right, cr.right);
  CHECK(dr.bottom > cr.bottom);
  /* the list is a visible LISTBOX window under the pointer */
  item_pt(c, 2, &sx, &sy);
  {
    HWND16 w = WindowFromPoint(MKL(sx, sy));
    CHECK(w != 0 && w != c && w != g_top);
    CHECK_EQ(GetWindowLong(w, GWL_WNDPROC), USER_CB_LISTBOX);
    CHECK(IsWindowVisible(w));
  }
  log_clear();
  click_screen(sx, sy); /* pick item2 */
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  CHECK_CODES("0001"); /* CBN_SELCHANGE once */
  CHECK_EQ(cmd(0)->wp, 406);
  CHECK_EQ(strcmp(text_of(c), "item2"), 0);
  /* the list is gone from under the pointer */
  CHECK(WindowFromPoint(MKL(sx, sy)) != 0);
  CHECK(GetWindowLong(WindowFromPoint(MKL(sx, sy)), GWL_WNDPROC) != USER_CB_LISTBOX);
  /* a second click on the field opens, another closes without a change */
  click_at(20 + 30, 20 + 5);
  CHECK_EQ(dropped(c), 1);
  log_clear();
  click_at(20 + 30, 20 + 5);
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  CHECK_EQ(count_code(CBN_SELCHANGE), 0);
  end();
}

static void test_close_outside_and_escape(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 407);
  fill5(c);
  SM(c, CB_SETCURSEL, 1, 0);
  click_at(20 + 30, 20 + 5);
  CHECK_EQ(dropped(c), 1);
  log_clear();
  click_at(300, 250); /* outside the list and the field */
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 1);
  CHECK_EQ(count_code(CBN_SELCHANGE), 0);
  SM(c, CB_SHOWDROPDOWN, 1, 0);
  CHECK_EQ(dropped(c), 1);
  tap(VK_ESCAPE);
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 1);
  /* losing the focus closes the list */
  SM(c, CB_SHOWDROPDOWN, 1, 0);
  CHECK_EQ(dropped(c), 1);
  SetFocus(g_top);
  CHECK_EQ(dropped(c), 0);
  end();
}

static void test_keyboard(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 408);
  fill5(c);
  SetFocus(c);
  SM(c, CB_SETCURSEL, 1, 0);
  log_clear();
  tap(VK_DOWN); /* closed: the selection moves and CBN_SELCHANGE is sent */
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  CHECK_CODES("0001");
  tap(VK_UP);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 1);
  tap('I'); /* type-ahead picks the next item starting with the letter */
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  tap(VK_F4); /* opens */
  CHECK_EQ(dropped(c), 1);
  log_clear();
  tap(VK_DOWN); /* open: the list highlight moves, no notification until Enter */
  tap(VK_DOWN);
  CHECK_EQ(count_code(CBN_SELCHANGE), 0);
  tap(VK_RETURN);
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 4);
  CHECK_EQ(count_code(CBN_SELCHANGE), 1);
  tap(VK_F4);
  CHECK_EQ(dropped(c), 1);
  tap(VK_F4);
  CHECK_EQ(dropped(c), 0);
  end();
}

static void test_extended_ui(void) {
  HWND16 c;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 420);
  fill5(c);
  SetFocus(c);
  SM(c, CB_SETCURSEL, 1, 0);
  /* extended UI: F4 does nothing, Down opens the list instead of changing the selection */
  CHECK_EQ(SM(c, CB_SETEXTENDEDUI, 1, 0), 0);
  CHECK_EQ(SM(c, CB_GETEXTENDEDUI, 0, 0), 1);
  tap(VK_F4);
  CHECK_EQ(dropped(c), 0);
  tap(VK_DOWN);
  CHECK_EQ(dropped(c), 1);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 1);
  tap(VK_ESCAPE);
  CHECK_EQ(dropped(c), 0);
  /* default UI: F4 toggles, Down changes the selection */
  CHECK_EQ(SM(c, CB_SETEXTENDEDUI, 0, 0), 0);
  tap(VK_F4);
  CHECK_EQ(dropped(c), 1);
  tap(VK_F4);
  CHECK_EQ(dropped(c), 0);
  tap(VK_DOWN);
  CHECK_EQ(dropped(c), 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 2);
  end();
}

static void test_ctlcolor_forwarded(void) {
  HWND16 c, e;
  const LogEnt *le;
  begin();
  c = C("C", CBS_DROPDOWN | CBS_AUTOHSCROLL, 20, 20, 140, 90, 421);
  e = child_edit(c);
  CHECK(e != 0);
  log_clear();
  SM(e, WM_SETTEXT, 0, 0);
  SM(c, WM_PAINT, 0, 0);
  InvalidateRect(e, 0, 1);
  pump();
  /* the owner sees the child edit's handle in LOWORD(lParam), as Wine forwards lParam unchanged */
  le = NULL;
  for (int i = 0; i < g_nlog; i++)
    if (g_log[i].msg == WM_CTLCOLOR && strcmp(label_of(g_log[i].hwnd), "P") == 0) { le = &g_log[i]; break; }
  CHECK(le != NULL);
  if (le) CHECK_EQ((uint16_t)le->lp, e);
  end();
}

static void test_dropdown_with_edit(void) {
  HWND16 c, e;
  begin();
  c = C("C", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 20, 20, 140, 90, 409);
  fill5(c);
  e = child_edit(c);
  CHECK(e != 0);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), ERR32);
  /* typing in the edit reports CBN_EDITUPDATE then CBN_EDITCHANGE */
  SetFocus(c);
  CHECK_EQ(GetFocus(), e); /* the focus goes to the edit field */
  log_clear();
  SM(e, WM_CHAR, 'z', 0);
  CHECK_CODES("0006,0005");
  CHECK_EQ(cmd(0)->wp, 409);
  CHECK_EQ((uint16_t)cmd(0)->lp, c);
  CHECK_EQ(strcmp(text_of(c), "z"), 0); /* WM_GETTEXT of the combo box is the edit text */
  CHECK_EQ(SMS(c, WM_SETTEXT, 0, "typed"), 1);
  CHECK_EQ(strcmp(text_of(e), "typed"), 0);
  /* CB_SETCURSEL puts the item text into the edit field */
  SM(c, CB_SETCURSEL, 3, 0);
  CHECK_EQ(strcmp(text_of(e), "item3"), 0);
  CHECK_EQ(SM(c, CB_GETEDITSEL, 0, 0), MKL(0, 5)); /* all selected */
  SM(c, CB_SETEDITSEL, 0, MKL(1, 3));
  CHECK_EQ(SM(c, CB_GETEDITSEL, 0, 0), MKL(1, 3));
  CHECK_EQ(SM(e, EM_GETSEL, 0, 0), MKL(1, 3));
  SM(c, CB_LIMITTEXT, 3, 0);
  SM(e, EM_SETSEL, 0, MKL(5, 5));
  SM(e, WM_CHAR, 'x', 0);
  CHECK_EQ(strcmp(text_of(e), "item3"), 0);
  /* a typed text that is not an item leaves CB_GETCURSEL at LB_ERR */
  /* opening the list and picking an item copies it to the edit */
  SM(c, CB_LIMITTEXT, 0, 0);
  {
    RECT16 cr = wrect(c);
    int sx, sy;
    SM(c, CB_SETCURSEL, 0, 0); /* the dropped list scrolls to show the selection: start from the top */
    SM(c, CB_SHOWDROPDOWN, 1, 0);
    CHECK_EQ(dropped(c), 1);
    item_pt(c, 1, &sx, &sy);
    log_clear();
    click_screen(sx, sy);
    CHECK_EQ(dropped(c), 0);
    CHECK_EQ(strcmp(text_of(e), "item1"), 0);
    CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 1);
    CHECK_EQ(count_code(CBN_SELCHANGE), 1);
    /* the arrow button at the right end of the closed part opens the list */
    click_at(20 + (cr.right - cr.left) - 8, 20 + 5);
    CHECK_EQ(dropped(c), 1);
    SM(c, CB_SHOWDROPDOWN, 0, 0);
    CHECK_EQ(dropped(c), 0);
  }
  /* CBN_SETFOCUS / CBN_KILLFOCUS around the combo box (the focus moving between its parts does not count) */
  log_clear();
  SetFocus(g_top);
  CHECK_EQ(count_code(CBN_KILLFOCUS), 1);
  log_clear();
  SetFocus(c);
  CHECK_EQ(count_code(CBN_SETFOCUS), 1);
  end();
}

static void test_simple(void) {
  HWND16 c, e;
  int h, sx, sy;
  RECT16 cr;
  begin();
  c = C("C", CBS_SIMPLE | CBS_AUTOHSCROLL | WS_VSCROLL, 20, 20, 140, 100, 410);
  fill5(c);
  e = child_edit(c);
  CHECK(e != 0);
  h = (int)SM(c, CB_GETITEMHEIGHT, 0, 0);
  cr = wrect(c);
  /* the list is a child below the edit: a click on its first item selects it */
  log_clear();
  sx = cr.left + 20;
  sy = cr.top + (int)SM(c, CB_GETITEMHEIGHT, 0xFFFF, 0) + 2 + h / 2;
  click_screen(sx, sy);
  CHECK_EQ(SM(c, CB_GETCURSEL, 0, 0), 0);
  CHECK_EQ(strcmp(text_of(e), "item0"), 0);
  CHECK_EQ(count_code(CBN_SELCHANGE), 1);
  (void)h;
  end();
}

static void test_dir_and_height_messages(void) {
  HWND16 c;
  begin();
  vfs_init();
  vfs_put("C:\\STARS\\A.XY", "x", 1);
  vfs_put("C:\\STARS\\B.XY", "x", 1);
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 411);
  CHECK(SMS(c, CB_DIR, DDL_READWRITE, "C:\\STARS\\*.XY") != ERR32);
  CHECK_EQ(SM(c, CB_GETCOUNT, 0, 0), 2);
  CHECK_EQ(SM(c, CB_SETITEMHEIGHT, 0, 20), 0);
  CHECK_EQ(SM(c, CB_GETITEMHEIGHT, 0, 0), 20);
  CHECK_EQ(SM(c, CB_SETITEMHEIGHT, 0xFFFF, 24), 0);
  CHECK_EQ(SM(c, CB_GETITEMHEIGHT, 0xFFFF, 0), 24);
  vfs_init();
  end();
}

static struct {
  int measure, draw, del;
  MEASUREITEMSTRUCT16 m[4];
  DRAWITEMSTRUCT16 d[24];
  DELETEITEMSTRUCT16 x[8];
} g_oc;
static int hook_oc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  (void)h;
  (void)wp;
  if (msg == WM_MEASUREITEM && g_oc.measure < 4) {
    MEASUREITEMSTRUCT16 *m = LIN(lp);
    g_oc.m[g_oc.measure++] = *m;
    m->itemHeight = 18;
    return 1;
  }
  if (msg == WM_DRAWITEM && g_oc.draw < 24) {
    g_oc.d[g_oc.draw++] = *(const DRAWITEMSTRUCT16 *)LIN(lp);
    return 1;
  }
  if (msg == WM_DELETEITEM && g_oc.del < 8) {
    g_oc.x[g_oc.del++] = *(const DELETEITEMSTRUCT16 *)LIN(lp);
    return 1;
  }
  return 0;
}

static void test_owner_draw(void) {
  HWND16 c;
  int field = -1, list = 0;
  begin();
  memset(&g_oc, 0, sizeof g_oc);
  g_hook = hook_oc;
  g_hook_ret = 1;
  c = C("C", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, 20, 20, 120, 100, 412);
  CHECK(g_oc.measure >= 1);
  CHECK_EQ(g_oc.m[0].CtlType, ODT_COMBOBOX);
  CHECK_EQ(g_oc.m[0].CtlID, 412);
  CHECK_EQ(SM(c, CB_GETITEMHEIGHT, 0, 0), 18);
  fill5(c);
  SM(c, CB_SETCURSEL, 2, 0);
  g_oc.draw = 0;
  InvalidateRect(c, 0, 1);
  pump();
  /* the selection field is drawn by the owner with ODS_COMBOBOXEDIT */
  for (int i = 0; i < g_oc.draw; i++)
    if (g_oc.d[i].itemState & ODS_COMBOBOXEDIT) field = i;
  CHECK(field >= 0);
  if (field >= 0) {
    const DRAWITEMSTRUCT16 *d = &g_oc.d[field];
    CHECK_EQ(d->CtlType, ODT_COMBOBOX);
    CHECK_EQ(d->CtlID, 412);
    CHECK_EQ(d->hwndItem, c);
    CHECK_EQ(d->itemID, 2);
    CHECK_EQ(d->itemAction, ODA_DRAWENTIRE);
    CHECK(d->hDC != 0);
    CHECK(d->rcItem.right > d->rcItem.left);
  }
  /* the dropped list items are drawn by the owner too, with the combo box as the control */
  g_oc.draw = 0;
  SM(c, CB_SHOWDROPDOWN, 1, 0);
  pump();
  for (int i = 0; i < g_oc.draw; i++)
    if (!(g_oc.d[i].itemState & ODS_COMBOBOXEDIT)) {
      list++;
      CHECK_EQ(g_oc.d[i].CtlType, ODT_COMBOBOX);
      CHECK_EQ(g_oc.d[i].CtlID, 412);
      CHECK_EQ(g_oc.d[i].hwndItem, c);
    }
  CHECK(list >= 3);
  SM(c, CB_SHOWDROPDOWN, 0, 0);
  g_oc.del = 0;
  SM(c, CB_DELETESTRING, 0, 0);
  CHECK_EQ(g_oc.del, 1);
  CHECK_EQ(g_oc.x[0].CtlType, ODT_COMBOBOX);
  CHECK_EQ(g_oc.x[0].CtlID, 412);
  CHECK_EQ(g_oc.x[0].hwndItem, c);
  end();
}

static void test_pixels(void) {
  HWND16 c;
  RECT16 r;
  int w, h;
  CBox b;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 413);
  fill5(c);
  SM(c, CB_SETCURSEL, 1, 0);
  r = wrect(c);
  w = r.right - r.left;
  h = r.bottom - r.top;
  pump();
  PIXEL_TEST("ctl_combo_closed");
  PIXEL_EQ(20, 20, BLACK);        /* 1 pixel frame */
  PIXEL_EQ(20 + w - 1, 20 + h - 1, BLACK);
  PIXEL_EQ(20 + 30, 20 + h - 2, WHITE_); /* the selection field is COLOR_WINDOW */
  b = cbox(20 + 1, 20 + 1, 20 + w - 17, 20 + h - 1, BLACK);
  CHECK(b.n > 8); /* the selected item's text, "item1" */
  /* the arrow button: a down triangle in black on a COLOR_BTNFACE button at the right end */
  PIXEL_EQ(20 + w - 3, 20 + 3, FACE);
  CHECK(cbox(20 + w - 16, 20 + 2, 20 + w - 1, 20 + h - 1, BLACK).n >= 5);
  /* focused: the selection field is highlighted */
  SetFocus(c);
  pump();
  b = cbox(20 + 1, 20 + 1, 20 + w - 17, 20 + h - 1, HIGHLIGHT_);
  CHECK(b.n > 40);
  dump_frame("ctl_combo_closed", PX0 + 10, PY0 + 10, 140, 40);
  /* dropped: the list frame and items under the field */
  SM(c, CB_SHOWDROPDOWN, 1, 0);
  pump();
  {
    RECT16 dr = droprect(c);
    int lh = dr.bottom - dr.top;
    CHECK(lh > h + 20);
    CHECK_EQ(PX(dr.left, dr.bottom - 1), BLACK);                 /* the list's frame */
    CHECK_EQ(PX(dr.left + 30, dr.bottom - 1), BLACK);
    CHECK(PX(dr.left + 40, dr.top + h + 8) == WHITE_ || PX(dr.left + 40, dr.top + h + 8) == HIGHLIGHT_);
    dump_frame("ctl_combo_dropped", dr.left - 5, dr.top - 5, 130, lh + 10);
  }
  SM(c, CB_SHOWDROPDOWN, 0, 0);
  end();
}

static void test_subclass(void) {
  HWND16 c;
  FARPTR old;
  begin();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 414);
  old = SetWindowLong(c, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_COMBOBOX);
  g_old = old;
  log_clear();
  CHECK_EQ(SMS(c, CB_ADDSTRING, 0, "x"), 0);
  CHECK(log_find("C", CB_ADDSTRING, 0) != NULL);
  CHECK_EQ(log_find("C", CB_ADDSTRING, 0)->tag, 'B');
  CHECK_EQ(SM(c, CB_GETCOUNT, 0, 0), 1);
  log_clear();
  click_at(20 + 30, 20 + 5);
  CHECK(log_find("C", WM_LBUTTONDOWN, 0) != NULL);
  CHECK_EQ(dropped(c), 1);
  SM(c, CB_SHOWDROPDOWN, 0, 0);
  SetWindowLong(c, GWL_WNDPROC, old);
  end();
}

static void test_destroy_with_open_list(void) {
  HWND16 c;
  unsigned before;
  begin();
  before = user_window_count();
  c = C("C", CBS_DROPDOWNLIST | WS_VSCROLL, 20, 20, 120, 90, 415);
  fill5(c);
  SM(c, CB_SHOWDROPDOWN, 1, 0);
  CHECK(user_window_count() > before + 1); /* the combo box and its list */
  DestroyWindow(c);
  pump();
  CHECK_EQ(user_window_count(), before);
  /* a combo with edit */
  c = C("D", CBS_DROPDOWN | WS_VSCROLL, 20, 20, 120, 90, 416);
  DestroyWindow(c);
  pump();
  CHECK_EQ(user_window_count(), before);
  end();
}

int main(void) {
  (void)dump;
  if (load_fixture() != 0) return 1;
  test_constants();
  test_class_and_literal_numbers();
  test_items();
  test_sort_style();
  test_geometry();
  test_dropdownlist_open_select();
  test_close_outside_and_escape();
  test_keyboard();
  test_extended_ui();
  test_ctlcolor_forwarded();
  test_dropdown_with_edit();
  test_simple();
  test_dir_and_height_messages();
  test_owner_draw();
  test_pixels();
  test_subclass();
  test_destroy_with_open_list();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
