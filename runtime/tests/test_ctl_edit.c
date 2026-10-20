/* Expectations for the EDIT control class (runtime/ctl_edit.c). Task stars-0z4.11, written BEFORE
 * the implementation. Do not edit an expectation to match the code: a disagreement is reported with
 * both values.
 *
 * Sources (the Windows 3.1 SDK text is NOT available here and no reference capture exists):
 *   WINE = wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 *   Win16 message numbers  include/wine/winuser16.h lines 455-485: EM_GETSEL16 WM_USER+0, EM_SETSEL16
 *       +1, EM_GETRECT16 +2, EM_SETRECT16 +3, EM_SETRECTNP16 +4, EM_SCROLL16 +5, EM_LINESCROLL16 +6,
 *       EM_SCROLLCARET16 +7, EM_GETMODIFY16 +8, EM_SETMODIFY16 +9, EM_GETLINECOUNT16 +10,
 *       EM_LINEINDEX16 +11, EM_SETHANDLE16 +12, EM_GETHANDLE16 +13, EM_GETTHUMB16 +14,
 *       EM_LINELENGTH16 +17, EM_REPLACESEL16 +18, EM_GETLINE16 +20, EM_LIMITTEXT16 +21,
 *       EM_CANUNDO16 +22, EM_UNDO16 +23, EM_FMTLINES16 +24, EM_LINEFROMCHAR16 +25,
 *       EM_SETTABSTOPS16 +27, EM_SETPASSWORDCHAR16 +28, EM_EMPTYUNDOBUFFER16 +29,
 *       EM_GETFIRSTVISIBLELINE16 +30, EM_SETREADONLY16 +31, EM_SETWORDBREAKPROC16 +32,
 *       EM_GETWORDBREAKPROC16 +33, EM_GETPASSWORDCHAR16 +34.  Win32 uses 0x00B0.. : NOT used.
 *   Win16 packing          EM_GETSEL returns MAKELONG(start, end); EM_SETSEL takes lParam =
 *       MAKELONG(start, end) (issue text); the notification is WM_COMMAND wParam = id, LOWORD(lParam)
 *       = control, HIWORD(lParam) = code (same as part 1).
 *   Styles / codes         include/winuser.rh 1134-1148 (ES_*), include/winuser.h 957-964 (EN_*):
 *       EN_SETFOCUS 0x100, EN_KILLFOCUS 0x200, EN_CHANGE 0x300, EN_UPDATE 0x400, EN_ERRSPACE 0x500,
 *       EN_MAXTEXT 0x501. UNVERIFIED for Windows 3.1 (shared low values).
 *
 * UNVERIFIED (no SDK text, no capture; this runtime's choices):
 *   - Every pixel expectation (text origin, caret width 1 pixel, caret blink 530 ms [WIN.INI
 *     CursorBlinkRate default recalled from memory], selection colours COLOR_HIGHLIGHT /
 *     COLOR_HIGHLIGHTTEXT, a gray label when disabled). Each pixel test names the PNG it waits for.
 *   - EN_UPDATE then EN_CHANGE order per edit (Wine edit.c EDIT_NOTIFY_PARENT after
 *     EDIT_EM_ReplaceSel: EN_UPDATE before the redraw, EN_CHANGE after).
 *   - WM_SETTEXT sends EN_UPDATE and EN_CHANGE to the parent (Wine EDIT_WM_SetText).
 *   - Default text limit 32766 characters; EM_LIMITTEXT(0) restores it.
 *   - Not implemented (documented): ES_CENTER / ES_RIGHT of single-line edits, undo (EM_CANUNDO returns 0), EM_SETWORDBREAKPROC, ES_OEMCONVERT, the I-beam
 *     cursor.
 */
#include "test.h"

#include "ctl2_test_common.h"

#define LM 1 /* left margin of the text in pixels (UNVERIFIED) */

static HWND16 E(const char *label, const char *text, uint32_t style, int x, int y, int w, int h, uint16_t id) {
  return btn(label, "EDIT", text, style | WS_TABSTOP, x, y, w, h, id);
}

static void sel_is(HWND16 e, int s, int t) {
  uint32_t r = SM(e, EM_GETSEL, 0, 0);
  CHECK_EQ(r & 0xFFFF, (uint32_t)s);
  CHECK_EQ(r >> 16, (uint32_t)t);
}

static void test_constants(void) {
  CHECK_EQ(EM_GETSEL, 0x0400);
  CHECK_EQ(EM_SETSEL, 0x0401);
  CHECK_EQ(EM_GETRECT, 0x0402);
  CHECK_EQ(EM_SETRECT, 0x0403);
  CHECK_EQ(EM_SETRECTNP, 0x0404);
  CHECK_EQ(EM_SCROLL, 0x0405);
  CHECK_EQ(EM_LINESCROLL, 0x0406);
  CHECK_EQ(EM_SCROLLCARET, 0x0407);
  CHECK_EQ(EM_GETMODIFY, 0x0408);
  CHECK_EQ(EM_SETMODIFY, 0x0409);
  CHECK_EQ(EM_GETLINECOUNT, 0x040A);
  CHECK_EQ(EM_LINEINDEX, 0x040B);
  CHECK_EQ(EM_SETHANDLE, 0x040C);
  CHECK_EQ(EM_GETHANDLE, 0x040D);
  CHECK_EQ(EM_GETTHUMB, 0x040E);
  CHECK_EQ(EM_LINELENGTH, 0x0411);
  CHECK_EQ(EM_REPLACESEL, 0x0412);
  CHECK_EQ(EM_GETLINE, 0x0414);
  CHECK_EQ(EM_LIMITTEXT, 0x0415);
  CHECK_EQ(EM_CANUNDO, 0x0416);
  CHECK_EQ(EM_UNDO, 0x0417);
  CHECK_EQ(EM_FMTLINES, 0x0418);
  CHECK_EQ(EM_LINEFROMCHAR, 0x0419);
  CHECK_EQ(EM_SETTABSTOPS, 0x041B);
  CHECK_EQ(EM_SETPASSWORDCHAR, 0x041C);
  CHECK_EQ(EM_EMPTYUNDOBUFFER, 0x041D);
  CHECK_EQ(EM_GETFIRSTVISIBLELINE, 0x041E);
  CHECK_EQ(EM_SETREADONLY, 0x041F);
  CHECK_EQ(EM_SETWORDBREAKPROC, 0x0420);
  CHECK_EQ(EM_GETWORDBREAKPROC, 0x0421);
  CHECK_EQ(EM_GETPASSWORDCHAR, 0x0422);
  CHECK_EQ(ES_MULTILINE, 4);
  CHECK_EQ(ES_UPPERCASE, 8);
  CHECK_EQ(ES_LOWERCASE, 0x10);
  CHECK_EQ(ES_PASSWORD, 0x20);
  CHECK_EQ(ES_AUTOHSCROLL, 0x80);
  CHECK_EQ(ES_READONLY, 0x800);
  CHECK_EQ(EN_SETFOCUS, 0x100);
  CHECK_EQ(EN_KILLFOCUS, 0x200);
  CHECK_EQ(EN_CHANGE, 0x300);
  CHECK_EQ(EN_UPDATE, 0x400);
  CHECK_EQ(EN_MAXTEXT, 0x501);
  CHECK_EQ(USER_CB_EDIT, MK_FP(MEM_SEL_CALLBACK, 0x50));
}

static void test_class_and_text(void) {
  HWND16 e;
  FARPTR p;
  begin();
  e = E("E", "Hello", ES_AUTOHSCROLL, 10, 10, 120, 16, 101);
  CHECK_EQ(GetWindowLong(e, GWL_WNDPROC), USER_CB_EDIT);
  p = far_alloc(32);
  CHECK_EQ(GetWindowText(e, p, 32), 5);
  CHECK_EQ(strcmp(LIN(p), "Hello"), 0);
  CHECK_EQ(SM(e, WM_GETTEXTLENGTH, 0, 0), 5);
  /* WM_GETTEXT with a small buffer truncates and returns the count copied */
  CHECK_EQ(GetWindowText(e, p, 4), 3);
  CHECK_EQ(strcmp(LIN(p), "Hel"), 0);
  CHECK_EQ(SM(e, WM_GETDLGCODE, 0, 0), DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS);
  end();
}

static void test_settext_notifies_and_long_text(void) {
  HWND16 e;
  char big[300];
  begin();
  e = E("E", "", ES_AUTOHSCROLL, 10, 10, 120, 16, 102);
  log_clear();
  CHECK_EQ(SMS(e, WM_SETTEXT, 0, "abc"), 1);
  CHECK_EQ(strcmp(text_of(e), "abc"), 0);
  CHECK_CODES("0400,0300"); /* EN_UPDATE then EN_CHANGE */
  CHECK_EQ(cmd(0)->wp, 102);
  CHECK_EQ((uint16_t)cmd(0)->lp, e);
  /* the window text is not limited to the 79 characters of the generic window text */
  memset(big, 'x', 250);
  big[250] = 0;
  SMS(e, WM_SETTEXT, 0, big);
  CHECK_EQ(SM(e, WM_GETTEXTLENGTH, 0, 0), 250);
  CHECK_EQ(strlen(text_of(e)), 250u);
  /* the initial text of CreateWindow is the control text */
  end();
}

static void test_typing(void) {
  HWND16 e;
  begin();
  e = E("E", "", ES_AUTOHSCROLL, 10, 10, 120, 16, 103);
  SM(e, WM_CHAR, 'a', 0);
  SM(e, WM_CHAR, 'b', 0);
  SM(e, WM_CHAR, 'c', 0);
  CHECK_EQ(strcmp(text_of(e), "abc"), 0);
  CHECK_EQ(count_code(EN_CHANGE), 3);
  CHECK_EQ(count_code(EN_UPDATE), 3);
  CHECK_CODES("0400,0300,0400,0300,0400,0300");
  sel_is(e, 3, 3);
  CHECK_EQ(SM(e, EM_GETMODIFY, 0, 0), 1);
  end();
}

static void test_real_keyboard(void) {
  HWND16 e;
  begin();
  e = E("E", "", ES_AUTOHSCROLL, 10, 10, 120, 16, 104);
  SetFocus(e);
  log_clear();
  tap('A'); /* WM_KEYDOWN -> TranslateMessage -> WM_CHAR 'a' (no Shift) */
  tap('B');
  tap_with(VK_SHIFT, 'C'); /* 'C' */
  CHECK_EQ(strcmp(text_of(e), "abC"), 0);
  tap(VK_BACK);
  CHECK_EQ(strcmp(text_of(e), "ab"), 0);
  tap(VK_LEFT);
  sel_is(e, 1, 1);
  tap(VK_DELETE); /* deletes the character after the caret */
  CHECK_EQ(strcmp(text_of(e), "a"), 0);
  tap(VK_HOME);
  sel_is(e, 0, 0);
  tap(VK_END);
  sel_is(e, 1, 1);
  end();
}

static void test_limittext_and_maxtext(void) {
  HWND16 e;
  begin();
  e = E("E", "", ES_AUTOHSCROLL, 10, 10, 120, 16, 105);
  CHECK_EQ(SM(e, EM_LIMITTEXT, 2, 0), 0);
  SM(e, WM_CHAR, 'a', 0);
  SM(e, WM_CHAR, 'b', 0);
  log_clear();
  SM(e, WM_CHAR, 'c', 0); /* rejected */
  CHECK_EQ(strcmp(text_of(e), "ab"), 0);
  CHECK_EQ(count_code(EN_MAXTEXT), 1);
  CHECK_EQ(count_code(EN_CHANGE), 0);
  /* EM_REPLACESEL beyond the limit is cut and reports EN_MAXTEXT too */
  SM(e, EM_SETSEL, 0, MKL(0, 2));
  log_clear();
  SMS(e, EM_REPLACESEL, 0, "xyz");
  CHECK_EQ(strcmp(text_of(e), "xy"), 0);
  CHECK_EQ(count_code(EN_MAXTEXT), 1);
  /* limit 0 means the default (large) limit */
  SM(e, EM_LIMITTEXT, 0, 0);
  SM(e, WM_CHAR, 'z', 0);
  CHECK_EQ(strcmp(text_of(e), "xyz"), 0);
  end();
}

static void test_selection_messages(void) {
  HWND16 e;
  begin();
  e = E("E", "hello", ES_AUTOHSCROLL, 10, 10, 120, 16, 106);
  sel_is(e, 0, 0);
  SM(e, EM_SETSEL, 0, MKL(1, 3));
  sel_is(e, 1, 3);
  SMS(e, EM_REPLACESEL, 0, "XY");
  CHECK_EQ(strcmp(text_of(e), "hXYlo"), 0);
  sel_is(e, 3, 3);
  SM(e, EM_SETSEL, 0, MKL(0, 0xFFFF)); /* end 0xFFFF: to the end of the text */
  sel_is(e, 0, 5);
  SM(e, EM_SETSEL, 0, MKL(4, 1)); /* start after end: the selection is normalised in EM_GETSEL */
  {
    uint32_t r = SM(e, EM_GETSEL, 0, 0);
    CHECK(((r & 0xFFFF) == 1 && (r >> 16) == 4) || ((r & 0xFFFF) == 4 && (r >> 16) == 1));
  }
  SM(e, EM_SETSEL, 0, MKL(2, 99)); /* clamped to the length */
  sel_is(e, 2, 5);
  /* typing replaces the selection */
  SM(e, WM_CHAR, 'Q', 0);
  CHECK_EQ(strcmp(text_of(e), "hXQ"), 0);
  sel_is(e, 3, 3);
  /* Backspace on a selection deletes the selection only */
  SM(e, EM_SETSEL, 0, MKL(0, 2));
  SM(e, WM_CHAR, 8, 0);
  CHECK_EQ(strcmp(text_of(e), "Q"), 0);
  end();
}

static void test_shift_selection_and_words(void) {
  HWND16 e;
  begin();
  e = E("E", "one two three", ES_AUTOHSCROLL, 10, 10, 160, 16, 107);
  SetFocus(e);
  SM(e, EM_SETSEL, 0, MKL(0, 0));
  tap_with(VK_SHIFT, VK_RIGHT);
  tap_with(VK_SHIFT, VK_RIGHT);
  sel_is(e, 0, 2);
  tap_with(VK_SHIFT, VK_END);
  sel_is(e, 0, 13);
  tap(VK_LEFT); /* a plain arrow collapses the selection at the caret and moves it */
  sel_is(e, 12, 12);
  tap(VK_HOME);
  tap_with(VK_CONTROL, VK_RIGHT); /* next word */
  sel_is(e, 4, 4);
  end();
}

static void test_clipboard(void) {
  HWND16 e, f;
  begin();
  e = E("E", "hello", ES_AUTOHSCROLL, 10, 10, 120, 16, 108);
  f = E("F", "", ES_AUTOHSCROLL, 10, 40, 120, 16, 109);
  SetFocus(e);
  SM(e, EM_SETSEL, 0, MKL(1, 3));
  tap_with(VK_CONTROL, VK_INSERT); /* copy "el" */
  SM(e, EM_SETSEL, 0, MKL(5, 5));
  tap_with(VK_SHIFT, VK_INSERT); /* paste */
  CHECK_EQ(strcmp(text_of(e), "helloel"), 0);
  SM(e, EM_SETSEL, 0, MKL(0, 2));
  tap_with(VK_SHIFT, VK_DELETE); /* cut "he" */
  CHECK_EQ(strcmp(text_of(e), "lloel"), 0);
  /* the clipboard is shared between edits */
  SetFocus(f);
  tap_with(VK_SHIFT, VK_INSERT);
  CHECK_EQ(strcmp(text_of(f), "he"), 0);
  /* the messages do the same */
  SM(e, EM_SETSEL, 0, MKL(0, 3));
  SM(e, WM_COPY, 0, 0);
  CHECK_EQ(strcmp(text_of(e), "lloel"), 0);
  SM(e, WM_CUT, 0, 0);
  CHECK_EQ(strcmp(text_of(e), "el"), 0);
  SM(f, WM_PASTE, 0, 0);
  CHECK_EQ(strcmp(text_of(f), "hello"), 0);
  SM(f, EM_SETSEL, 0, MKL(0, 1));
  SM(f, WM_CLEAR, 0, 0);
  CHECK_EQ(strcmp(text_of(f), "ello"), 0);
  end();
}

static void test_password_and_case_and_readonly(void) {
  HWND16 p, q, u, l, r;
  begin();
  p = E("P1", "ab", ES_PASSWORD | ES_AUTOHSCROLL, 10, 10, 100, 16, 110);
  q = E("Q1", "**", ES_AUTOHSCROLL, 10, 40, 100, 16, 111);
  (void)q;
  pump();
  PIXEL_TEST("ctl_edit_password");
  CHECK_EQ(strcmp(text_of(p), "ab"), 0); /* the text is the real one */
  CHECK_EQ(SM(p, EM_GETPASSWORDCHAR, 0, 0), '*');
  CHECK(same_pixels(10, 10, 10, 40, 100, 16)); /* ES_PASSWORD renders '*' characters */
  CHECK(cbox(10, 10, 110, 26, BLACK).n > 4);
  SM(p, EM_SETPASSWORDCHAR, '#', 0);
  CHECK_EQ(SM(p, EM_GETPASSWORDCHAR, 0, 0), '#');
  pump();
  CHECK(!same_pixels(10, 10, 10, 40, 100, 16));
  SM(p, EM_SETPASSWORDCHAR, 0, 0); /* 0 shows the text */
  pump();
  CHECK_EQ(SM(p, EM_GETPASSWORDCHAR, 0, 0), 0);
  u = E("U", "", ES_UPPERCASE | ES_AUTOHSCROLL, 10, 70, 100, 16, 112);
  l = E("L", "", ES_LOWERCASE | ES_AUTOHSCROLL, 10, 100, 100, 16, 113);
  SM(u, WM_CHAR, 'a', 0);
  SM(l, WM_CHAR, 'A', 0);
  CHECK_EQ(strcmp(text_of(u), "A"), 0);
  CHECK_EQ(strcmp(text_of(l), "a"), 0);
  r = E("R", "ro", ES_READONLY | ES_AUTOHSCROLL, 10, 130, 100, 16, 114);
  log_clear();
  SM(r, WM_CHAR, 'x', 0);
  SM(r, WM_CHAR, 8, 0);
  CHECK_EQ(strcmp(text_of(r), "ro"), 0);
  CHECK_EQ(ncmd(), 0);
  CHECK_EQ(SM(r, EM_SETREADONLY, 0, 0), 1);
  SM(r, WM_CHAR, 'x', 0); /* the caret of a new edit is at 0 */
  CHECK_EQ(strcmp(text_of(r), "xro"), 0);
  CHECK_EQ(SM(r, EM_SETREADONLY, 1, 0), 1);
  CHECK(GetWindowLong(r, GWL_STYLE) & ES_READONLY);
  end();
}

static void test_focus_notifications(void) {
  HWND16 a, b;
  begin();
  a = E("A", "x", ES_AUTOHSCROLL, 10, 10, 100, 16, 115);
  b = E("B", "y", ES_AUTOHSCROLL, 10, 40, 100, 16, 116);
  log_clear();
  SetFocus(a);
  CHECK_CODES("0100");
  CHECK_EQ(cmd(0)->wp, 115);
  log_clear();
  SetFocus(b);
  CHECK_CODES("0200,0100"); /* a loses it, b gets it */
  CHECK_EQ(cmd(0)->wp, 115);
  CHECK_EQ(cmd(1)->wp, 116);
  end();
}

static void test_line_messages(void) {
  HWND16 e;
  FARPTR p;
  begin();
  e = E("E", "hello", ES_AUTOHSCROLL, 10, 10, 100, 16, 117);
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 1);
  CHECK_EQ(SM(e, EM_LINEINDEX, 0, 0), 0);
  CHECK_EQ(SM(e, EM_LINEINDEX, 0xFFFF, 0), 0);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0, 0), 5);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0xFFFF, 0), 5);
  CHECK_EQ(SM(e, EM_LINEFROMCHAR, 3, 0), 0);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  p = far_alloc(32);
  *(uint16_t *)LIN(p) = 32; /* EM_GETLINE: the first word of the buffer is its size */
  CHECK_EQ(SM(e, EM_GETLINE, 0, p), 5);
  CHECK_EQ(memcmp(LIN(p), "hello", 5), 0);
  *(uint16_t *)LIN(p) = 3;
  CHECK_EQ(SM(e, EM_GETLINE, 0, p), 3);
  CHECK_EQ(memcmp(LIN(p), "hel", 3), 0);
  CHECK_EQ(SM(e, EM_GETLINE, 1, p), 0); /* single line: line 1 does not exist */
  CHECK_EQ(SM(e, EM_GETMODIFY, 0, 0), 0);
  SM(e, EM_SETMODIFY, 1, 0);
  CHECK_EQ(SM(e, EM_GETMODIFY, 0, 0), 1);
  SM(e, EM_SETMODIFY, 0, 0);
  CHECK_EQ(SM(e, EM_GETMODIFY, 0, 0), 0);
  CHECK_EQ(SM(e, EM_CANUNDO, 0, 0), 0);
  CHECK_EQ(SM(e, EM_FMTLINES, 1, 0), 0);
  CHECK_EQ(SM(e, EM_GETTHUMB, 0, 0), 0);
  CHECK_EQ(SM(e, EM_SCROLL, 0, 0), 0);
  CHECK_EQ(SM(e, EM_SETHANDLE, 0, 0), 0);
  CHECK_EQ(SM(e, EM_GETHANDLE, 0, 0), 0);
  {
    FARPTR r = far_alloc(sizeof(RECT16));
    SM(e, EM_GETRECT, 0, r);
    CHECK(((RECT16 *)LIN(r))->right > ((RECT16 *)LIN(r))->left);
  }
  end();
}

static void test_mouse_selection(void) {
  HWND16 e;
  int x0 = 10;
  begin();
  e = E("E", "hello world", ES_AUTOHSCROLL, x0, 20, 160, 16, 118);
  underlay(0, 0, 200, 60);
  pump();
  click_at(x0 + 150, 28); /* far right of the text: the caret goes to the end */
  sel_is(e, 11, 11);
  CHECK_EQ(GetFocus(), e);
  click_at(x0 + 0, 28); /* at the left edge */
  sel_is(e, 0, 0);
  /* click between 'l' (index 2) and 'l': just right of the boundary at index 3 -> 3 */
  click_at(x0 + LM + text_w(e, "hello world", 3) + 1, 28);
  sel_is(e, 3, 3);
  /* drag from index 2 to index 6 selects 2..6 */
  mouse(PX0 + x0 + LM + text_w(e, "hello world", 2), PY0 + 28, 1);
  mouse(PX0 + x0 + LM + text_w(e, "hello world", 6), PY0 + 28, 1);
  mouse(PX0 + x0 + LM + text_w(e, "hello world", 6), PY0 + 28, 0);
  sel_is(e, 2, 6);
  /* Shift+click extends from the anchor */
  key(VK_SHIFT, 0x2A, 1);
  click_at(x0 + LM + text_w(e, "hello world", 9), 28);
  key(VK_SHIFT, 0x2A, 0);
  sel_is(e, 2, 9);
  /* a double click selects the word under the pointer */
  dblclick_at(x0 + LM + text_w(e, "hello world", 8), 28);
  sel_is(e, 6, 11);
  end();
}

static void test_pixels_selection_caret(void) {
  HWND16 e;
  CBox b;
  int h;
  begin();
  e = E("E", "hello", ES_AUTOHSCROLL, 10, 10, 120, 20, 119);
  h = text_h(e);
  pump();
  PIXEL_TEST("ctl_edit_basic");
  PIXEL_EQ(60, 25, WHITE_); /* background: the parent's CTLCOLOR_EDIT brush, COLOR_WINDOW by default */
  PIXEL_EQ(10 + 119, 10 + 19, WHITE_);
  b = cbox(10, 10, 130, 30, BLACK);
  CHECK(b.n > 10);
  CHECK(b.l >= 10 + LM && b.l <= 10 + LM + 2);
  CHECK(b.r < 10 + LM + text_w(e, "hello", 5) + 2);
  CHECK(b.b - b.t < h + 2);
  CHECK(b.t >= 10 && b.b < 30); /* vertically inside the control */
  /* unfocused, a selection is not shown */
  SM(e, EM_SETSEL, 0, MKL(1, 3));
  pump();
  CHECK_EQ(cbox(10, 10, 130, 30, HIGHLIGHT_).n, 0);
  /* focused: the selection is highlighted (COLOR_HIGHLIGHT) with COLOR_HIGHLIGHTTEXT text */
  SetFocus(e);
  pump();
  b = cbox(10, 10, 130, 30, HIGHLIGHT_);
  CHECK(b.n > 20);
  CHECK(b.l >= 10 + LM + text_w(e, "hello", 1) - 1 && b.l <= 10 + LM + text_w(e, "hello", 1) + 1);
  CHECK(b.r >= 10 + LM + text_w(e, "hello", 3) - 2 && b.r <= 10 + LM + text_w(e, "hello", 3) + 1);
  CHECK(cbox(10, 10, 130, 30, HIGHLIGHTTXT_).n > 5);
  PIXEL_TEST("ctl_edit_selection");
  dump_frame("ctl_edit_selection", PX0, PY0, 140, 40);
  /* the caret: with no selection and the focus, a 1 pixel wide vertical line at the caret in the
   * text colour, as tall as the text; it is also visible while the selection is empty */
  SM(e, EM_SETSEL, 0, MKL(5, 5));
  pump();
  CHECK_EQ(cbox(10, 10, 130, 30, HIGHLIGHT_).n, 0);
  {
    int cx = 10 + LM + text_w(e, "hello", 5), run = 0, best = 0;
    for (int x = cx - 1; x <= cx + 2; x++) {
      run = 0;
      for (int y = 10; y < 30; y++) {
        run = pc(x, y) == BLACK ? run + 1 : 0;
        if (run > best) best = run;
      }
    }
    CHECK(best >= h - 2);
  }
  /* ES_NOHIDESEL keeps the selection visible without the focus */
  {
    HWND16 n = E("N", "world", ES_AUTOHSCROLL | ES_NOHIDESEL, 10, 40, 120, 20, 120);
    SM(n, EM_SETSEL, 0, MKL(0, 3));
    pump();
    CHECK(cbox(10, 40, 130, 60, HIGHLIGHT_).n > 10);
  }
  /* disabled: gray text */
  EnableWindow(e, 0);
  pump();
  CHECK_EQ(cbox(10, 10, 130, 30, BLACK).n, 0);
  CHECK(cbox(10, 10, 130, 30, GRAYTXT).n > 5);
  end();
}

static void test_no_autohscroll_rejects(void) {
  HWND16 e;
  int n = 0;
  begin();
  e = E("E", "", 0, 10, 10, 40, 16, 121); /* 40 pixels wide, no ES_AUTOHSCROLL */
  for (int i = 0; i < 20; i++) SM(e, WM_CHAR, 'W', 0);
  n = (int)strlen(text_of(e));
  CHECK(n >= 1 && n < 8); /* stops when the text no longer fits */
  CHECK(text_w(e, "WWWWWWWWWWWWWWWWWWWW", n) <= 40);
  {
    HWND16 a = E("A", "", ES_AUTOHSCROLL, 10, 40, 40, 16, 122);
    for (int i = 0; i < 20; i++) SM(a, WM_CHAR, 'W', 0);
    CHECK_EQ(strlen(text_of(a)), 20u); /* scrolls instead */
    pump();
    /* the caret is at the end and scrolled into view: text pixels near the right edge */
    SetFocus(a);
    pump();
    /* scrolled so that the last characters and the caret show: the text ends inside the control
     * (an unscrolled run of 20 'W' would fill it to the last pixel column) */
    {
      CBox bb = cbox(10, 40, 50, 56, BLACK);
      CHECK(bb.n > 10);
      CHECK(bb.r >= 10 + 20 && bb.r <= 10 + 38);
    }
  }
  end();
}

static void test_subclass(void) {
  HWND16 e;
  FARPTR old;
  begin();
  e = E("E", "Hello", ES_AUTOHSCROLL, 10, 10, 100, 16, 123);
  old = SetWindowLong(e, GWL_WNDPROC, FP_PROC_B);
  CHECK_EQ(old, USER_CB_EDIT);
  g_old = old;
  log_clear();
  SMS(e, WM_SETTEXT, 0, "Other");
  CHECK(log_find("E", WM_SETTEXT, 0) != NULL);
  CHECK_EQ(log_find("E", WM_SETTEXT, 0)->tag, 'B');
  CHECK_EQ(strcmp(text_of(e), "Other"), 0);
  /* the subclass sees EM_* and forwards: CallWindowProc reaches the class procedure */
  SM(e, EM_SETSEL, 0, MKL(1, 2));
  CHECK(log_find("E", EM_SETSEL, 0) != NULL);
  sel_is(e, 1, 2);
  SM(e, WM_CHAR, 'z', 0);
  CHECK_EQ(strcmp(text_of(e), "Ozher"), 0);
  SetWindowLong(e, GWL_WNDPROC, old);
  end();
}

static void test_win16_numbers_literal(void) {
  /* literal Win16 numbers, not the macros: the class must answer these and nothing else */
  HWND16 e;
  begin();
  e = E("E", "abcdef", ES_AUTOHSCROLL, 10, 10, 100, 16, 124);
  SM(e, 0x0401, 0, MKL(2, 4)); /* EM_SETSEL */
  CHECK_EQ(SM(e, 0x0400, 0, 0), MKL(2, 4)); /* EM_GETSEL */
  SM(e, 0x0415, 3, 0);                      /* EM_LIMITTEXT */
  SM(e, 0x0401, 0, MKL(6, 6));
  SM(e, WM_CHAR, 'x', 0);
  CHECK_EQ(strcmp(text_of(e), "abcdef"), 0);
  /* the Win32 numbers do nothing (EM_SETSEL = 0x00B1, EM_GETSEL = 0x00B0) */
  SM(e, 0x00B1, 0, 3);
  CHECK_EQ(SM(e, 0x00B0, 0, 0), 0);
  end();
}


/* ---- ES_MULTILINE (task stars-8xw), written BEFORE the implementation ----
 * Sources: EM_* numbers as above (winuser16.h lines 455-485). Everything below about multi-line
 * behaviour follows Wine's edit.c and the issue text and is UNVERIFIED for Windows 3.1 (no SDK text,
 * no capture): CRLF is the only line break (a lone CR or LF is not), EM_LINEINDEX(-1) is the line of
 * the caret, EM_LINELENGTH / EM_LINEFROMCHAR take a CHARACTER INDEX (-1 = the caret), EM_GETLINE
 * takes a line number and does not NUL-terminate, EM_SCROLL answers MAKELONG(lines scrolled, 1),
 * Enter without ES_AUTOVSCROLL on the last visible line is refused, a vertical bar of WS_VSCROLL
 * appears only when the text has more lines than fit (SetScrollRange rule, as the list box), no
 * word wrapping (lines are only broken at CRLF), ES_PASSWORD ignored.
 * The two game sites (MESSAGEWNDPROC 6:0x0141 and 6:0x018d) are styles ML_COMPOSE and ML_VIEW. */
#define ML_COMPOSE (ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER)                         /* 0x40800044 with WS_CHILD */
#define ML_VIEW (ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL | WS_BORDER) /* 0x40a00844 with WS_CHILD */

/* an edit that shows exactly n lines (plus the 1 pixel border on each side) */
static HWND16 EL(const char *text, uint32_t style, int n, uint16_t id) {
  return E("M", text, style, 10, 10, 120, n * text_h(g_top) + 2, id);
}
static int vis_lines(HWND16 e) {
  const UWnd *w = user_wnd(e);
  return (w->crect.bottom - w->crect.top) / text_h(e);
}
static int line_of_caret(HWND16 e) { return (int)SM(e, EM_LINEFROMCHAR, 0xFFFF, 0); }
static const char *line_text(HWND16 e, int n) {
  static char out[4][256];
  static int k;
  FARPTR p = far_alloc(256);
  uint32_t r;
  k = (k + 1) & 3;
  *(uint16_t *)LIN(p) = 250;
  r = SM(e, EM_GETLINE, (uint16_t)n, p);
  memcpy(out[k], LIN(p), r);
  out[k][r] = 0;
  return out[k];
}

static void test_ml_game_sites(void) {
  HWND16 c, v;
  char big[1100];
  begin();
  CHECK_EQ(WS_CHILD | ML_COMPOSE, 0x40800044UL);
  CHECK_EQ(WS_CHILD | ML_VIEW, 0x40a00844UL);
  CHECK_EQ(ES_AUTOVSCROLL, 0x40);
  c = E("C", "", ML_COMPOSE, 10, 10, 120, 40, 200);
  CHECK_EQ(GetWindowLong(c, GWL_STYLE) & ~(uint32_t)(WS_VISIBLE | WS_TABSTOP), 0x40800044UL);
  CHECK_EQ(SM(c, EM_LIMITTEXT, 968, 0), 0); /* the game's call right after CreateWindow */
  memset(big, 'x', 1000);
  big[1000] = 0;
  log_clear();
  SMS(c, EM_REPLACESEL, 0, big);
  CHECK_EQ(SM(c, WM_GETTEXTLENGTH, 0, 0), 968);
  CHECK_EQ(count_code(EN_MAXTEXT), 1);
  log_clear();
  SMS(c, WM_SETTEXT, 0, "a\r\nb");
  CHECK_CODES("0400,0300"); /* the notifications are the single-line ones */
  CHECK_EQ(SM(c, EM_GETLINECOUNT, 0, 0), 2);
  v = E("V", "one\r\ntwo", ML_VIEW, 10, 70, 120, 40, 201);
  CHECK_EQ(GetWindowLong(v, GWL_STYLE) & ~(uint32_t)(WS_VISIBLE | WS_TABSTOP | WS_VSCROLL), 0x40800844UL);
  CHECK_EQ(SM(v, EM_GETLINECOUNT, 0, 0), 2);
  end();
}

static void test_ml_line_messages(void) {
  HWND16 e, s;
  begin();
  e = EL("a\r\nb\r\nc", ML_COMPOSE, 3, 210);
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 3); /* regression: a CRLF split must give 3, not 1 */
  CHECK_EQ(SM(e, EM_LINEINDEX, 0, 0), 0);
  CHECK_EQ(SM(e, EM_LINEINDEX, 1, 0), 3);
  CHECK_EQ(SM(e, EM_LINEINDEX, 2, 0), 6);
  CHECK_EQ(SM(e, EM_LINEINDEX, 3, 0), ERR32); /* no such line */
  CHECK_EQ(SM(e, EM_LINEINDEX, 0xFFFF, 0), 0); /* caret on line 0 */
  SM(e, EM_SETSEL, 0, MKL(4, 4));
  CHECK_EQ(SM(e, EM_LINEINDEX, 0xFFFF, 0), 3);
  CHECK_EQ(SM(e, EM_LINEFROMCHAR, 4, 0), 1);
  CHECK_EQ(SM(e, EM_LINEFROMCHAR, 6, 0), 2);
  CHECK_EQ(SM(e, EM_LINEFROMCHAR, 0xFFFF, 0), 1);
  CHECK_EQ(SM(e, EM_LINEFROMCHAR, 0, 0), 0);
  CHECK_EQ(SM(e, WM_GETTEXTLENGTH, 0, 0), 7);
  CHECK_EQ(strcmp(text_of(e), "a\r\nb\r\nc"), 0);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0, 0), 1);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0xFFFF, 0), 1);
  /* lines of different lengths, an empty line in the middle, a trailing break */
  SMS(e, WM_SETTEXT, 0, "abc\r\n\r\nhi\r\n");
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 4);
  CHECK_EQ(SM(e, EM_LINEINDEX, 1, 0), 5);
  CHECK_EQ(SM(e, EM_LINEINDEX, 2, 0), 7);
  CHECK_EQ(SM(e, EM_LINEINDEX, 3, 0), 11);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0, 0), 3);
  CHECK_EQ(SM(e, EM_LINELENGTH, 5, 0), 0); /* index 5 is on the empty line */
  CHECK_EQ(SM(e, EM_LINELENGTH, 7, 0), 2);
  CHECK_EQ(SM(e, EM_LINELENGTH, 11, 0), 0);
  CHECK_EQ(strcmp(line_text(e, 0), "abc"), 0);
  CHECK_EQ(strcmp(line_text(e, 1), ""), 0);
  CHECK_EQ(strcmp(line_text(e, 2), "hi"), 0);
  {
    FARPTR p = far_alloc(16);
    *(uint16_t *)LIN(p) = 2; /* a buffer of 2 truncates, no NUL is written */
    CHECK_EQ(SM(e, EM_GETLINE, 0, p), 2);
    CHECK_EQ(memcmp(LIN(p), "ab", 2), 0);
    *(uint16_t *)LIN(p) = 16;
    CHECK_EQ(SM(e, EM_GETLINE, 4, p), 0); /* line 4 does not exist */
  }
  SMS(e, WM_SETTEXT, 0, "");
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 1);
  CHECK_EQ(SM(e, EM_LINELENGTH, 0, 0), 0);
  /* a lone CR or LF is not a line break */
  SMS(e, WM_SETTEXT, 0, "a\rb\nc");
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 1);
  /* the single-line control still answers 1 for text with CRLF in it (unchanged path) */
  s = E("S", "x\r\ny", ES_AUTOHSCROLL, 10, 80, 120, 16, 211);
  CHECK_EQ(SM(s, EM_GETLINECOUNT, 0, 0), 1);
  CHECK_EQ(SM(s, EM_LINEINDEX, 1, 0), ERR32);
  end();
}

static void test_ml_enter_and_backspace(void) {
  HWND16 e, s;
  begin();
  e = EL("", ML_COMPOSE, 4, 220);
  SM(e, WM_CHAR, 'a', 0);
  SM(e, WM_CHAR, 'b', 0);
  log_clear();
  SM(e, WM_CHAR, 13, 0); /* Enter inserts CR LF */
  CHECK_EQ(strcmp(text_of(e), "ab\r\n"), 0);
  CHECK_CODES("0400,0300");
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 2);
  sel_is(e, 4, 4);
  SM(e, WM_CHAR, 'c', 0);
  CHECK_EQ(strcmp(text_of(e), "ab\r\nc"), 0);
  CHECK_EQ(line_of_caret(e), 1);
  /* Backspace at a line start joins the lines (removes the CR LF pair) */
  SM(e, EM_SETSEL, 0, MKL(4, 4));
  SM(e, WM_CHAR, 8, 0);
  CHECK_EQ(strcmp(text_of(e), "abc"), 0);
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 1);
  sel_is(e, 2, 2);
  /* Enter in the middle of a line splits it */
  SM(e, WM_CHAR, 13, 0);
  CHECK_EQ(strcmp(text_of(e), "ab\r\nc"), 0);
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 2);
  /* Delete at a line end joins as well */
  SM(e, EM_SETSEL, 0, MKL(2, 2));
  SM(e, WM_KEYDOWN, VK_DELETE, 0);
  CHECK_EQ(strcmp(text_of(e), "abc"), 0);
  /* Enter replaces a selection */
  SM(e, EM_SETSEL, 0, MKL(1, 2));
  SM(e, WM_CHAR, 13, 0);
  CHECK_EQ(strcmp(text_of(e), "a\r\nc"), 0);
  /* Backspace inside a line deletes one character; at the very start nothing happens */
  SM(e, EM_SETSEL, 0, MKL(0, 0));
  SM(e, WM_CHAR, 8, 0);
  CHECK_EQ(strcmp(text_of(e), "a\r\nc"), 0);
  /* a limit that leaves room for one character only refuses Enter: never a lone CR */
  SMS(e, WM_SETTEXT, 0, "ab");
  SM(e, EM_LIMITTEXT, 3, 0);
  SM(e, EM_SETSEL, 0, MKL(2, 2));
  log_clear();
  SM(e, WM_CHAR, 13, 0);
  CHECK_EQ(strcmp(text_of(e), "ab"), 0);
  CHECK_EQ(count_code(EN_MAXTEXT), 1);
  /* a single-line edit still ignores Enter */
  s = E("S", "x", ES_AUTOHSCROLL, 10, 80, 120, 16, 221);
  SM(s, WM_CHAR, 13, 0);
  CHECK_EQ(strcmp(text_of(s), "x"), 0);
  end();
}

static void test_ml_keys(void) {
  HWND16 e;
  begin();
  e = EL("abc\r\nde\r\nfghij", ML_COMPOSE, 5, 230);
  SetFocus(e);
  SM(e, EM_SETSEL, 0, MKL(1, 1));
  tap(VK_DOWN);
  CHECK_EQ(line_of_caret(e), 1);
  { /* the same column (the same pixel, within a character) */
    uint32_t r = SM(e, EM_GETSEL, 0, 0);
    CHECK_EQ(r & 0xFFFF, 6u);
  }
  tap(VK_DOWN);
  CHECK_EQ(line_of_caret(e), 2);
  CHECK_EQ(SM(e, EM_GETSEL, 0, 0) & 0xFFFF, 10u);
  tap(VK_DOWN); /* last line: stays */
  CHECK_EQ(line_of_caret(e), 2);
  tap(VK_UP);
  tap(VK_UP);
  CHECK_EQ(line_of_caret(e), 0);
  tap(VK_UP);
  CHECK_EQ(line_of_caret(e), 0);
  /* a short line clamps the column */
  SM(e, EM_SETSEL, 0, MKL(14, 14)); /* the end of "fghij", column 5 */
  tap(VK_UP);
  sel_is(e, 7, 7); /* end of "de" */
  /* Home / End are the line's */
  tap(VK_HOME);
  sel_is(e, 5, 5);
  tap(VK_END);
  sel_is(e, 7, 7);
  /* Right at a line end steps over the CR LF; Left at a line start steps back over it */
  tap(VK_RIGHT);
  sel_is(e, 9, 9);
  tap(VK_LEFT);
  sel_is(e, 7, 7);
  /* Shift+Down extends the selection */
  SM(e, EM_SETSEL, 0, MKL(1, 1));
  tap_with(VK_SHIFT, VK_DOWN);
  {
    uint32_t r = SM(e, EM_GETSEL, 0, 0);
    CHECK_EQ(r & 0xFFFF, 1u);
    CHECK_EQ(r >> 16, 6u);
  }
  /* Ctrl+Home / Ctrl+End: the whole text */
  tap_with(VK_CONTROL, VK_END);
  sel_is(e, 14, 14);
  tap_with(VK_CONTROL, VK_HOME);
  sel_is(e, 0, 0);
  /* real keyboard: Return makes a new line through WM_CHAR */
  tap(VK_END);
  tap(VK_RETURN);
  CHECK_EQ(strcmp(text_of(e), "abc\r\n\r\nde\r\nfghij"), 0);
  CHECK_EQ(SM(e, EM_GETLINECOUNT, 0, 0), 4);
  end();
}

static void test_ml_scrolling(void) {
  HWND16 e, a;
  UScroll *sb;
  int vis;
  char buf[64];
  begin();
  e = EL("0\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9", ML_COMPOSE, 3, 240);
  vis = vis_lines(e);
  CHECK_EQ(vis, 3);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* the caret to the end brings the last line into view */
  SM(e, EM_SETSEL, 0, MKL(28, 28));
  SM(e, EM_SCROLLCARET, 0, 0);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 7);
  /* EM_LINESCROLL: LOWORD lines vertically (HIWORD columns); clamped to the text */
  SM(e, EM_LINESCROLL, 0, MKL((uint16_t)-2, 0));
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 5);
  SM(e, EM_LINESCROLL, 0, MKL(100, 0));
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 7);
  SM(e, EM_LINESCROLL, 0, MKL((uint16_t)-100, 0));
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* EM_SCROLL: SB_LINEDOWN / SB_PAGEDOWN / SB_LINEUP / SB_PAGEUP answer MAKELONG(lines, 1), up negative */
  CHECK_EQ(SM(e, EM_SCROLL, SB_LINEDOWN, 0), MKL(1, 1));
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 1);
  CHECK_EQ(SM(e, EM_SCROLL, SB_PAGEDOWN, 0), MKL(2, 1)); /* a page is vis - 1 lines (UNVERIFIED) */
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 3);
  CHECK_EQ(SM(e, EM_SCROLL, SB_LINEUP, 0), MKL((uint16_t)-1, 1)); /* up is negative (Wine; UNVERIFIED) */
  CHECK_EQ(SM(e, EM_SCROLL, SB_PAGEUP, 0), MKL((uint16_t)-2, 1));
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* the arrow keys keep the caret in view: Down past the last visible line scrolls by one */
  SetFocus(e);
  SM(e, EM_SETSEL, 0, MKL(0, 0));
  tap(VK_DOWN);
  tap(VK_DOWN);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  tap(VK_DOWN);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 1);
  tap(VK_UP);
  tap(VK_UP);
  tap(VK_UP);
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* ES_AUTOVSCROLL: Enter on the last visible line scrolls the text up */
  a = EL("p\r\nq\r\nr", ML_COMPOSE, 3, 241);
  SM(a, EM_SETSEL, 0, MKL(7, 7));
  SM(a, WM_CHAR, 13, 0);
  CHECK_EQ(SM(a, EM_GETLINECOUNT, 0, 0), 4);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 1);
  /* without ES_AUTOVSCROLL Enter on the last visible line is refused (UNVERIFIED) */
  a = EL("p\r\nq\r\nr", ES_MULTILINE | WS_BORDER, 3, 242);
  SM(a, EM_SETSEL, 0, MKL(7, 7));
  SM(a, WM_CHAR, 13, 0);
  CHECK_EQ(SM(a, EM_GETLINECOUNT, 0, 0), 3);
  CHECK_EQ(strcmp(text_of(a), "p\r\nq\r\nr"), 0);
  /* WM_SETTEXT starts at the top again */
  SMS(e, WM_SETTEXT, 0, "x\r\ny");
  CHECK_EQ(SM(e, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* the vertical scroll bar of WS_VSCROLL: shown only when the text has more lines than fit */
  a = EL("0\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 3, 243);
  CHECK(user_wnd(a)->style & WS_VSCROLL);
  sb = user_sb_get(a, SB_VERT);
  CHECK(sb != NULL);
  CHECK_EQ(sb->min, 0);
  CHECK_EQ(sb->max, 7); /* lines - visible lines */
  CHECK_EQ(GetScrollPos(a, SB_VERT), 0);
  SM(a, WM_VSCROLL, SB_LINEDOWN, 0);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 1);
  CHECK_EQ(GetScrollPos(a, SB_VERT), 1);
  SM(a, WM_VSCROLL, SB_PAGEDOWN, 0);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 3);
  SM(a, WM_VSCROLL, SB_THUMBPOSITION, MKL(5, 0));
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 5);
  CHECK_EQ(GetScrollPos(a, SB_VERT), 5);
  SM(a, WM_VSCROLL, SB_LINEUP, 0);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 4);
  SM(a, WM_VSCROLL, SB_BOTTOM, 0);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 7);
  SM(a, WM_VSCROLL, SB_TOP, 0);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 0);
  /* keyboard scrolling moves the thumb */
  SetFocus(a);
  tap_with(VK_CONTROL, VK_END);
  CHECK_EQ(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0), 7);
  CHECK_EQ(GetScrollPos(a, SB_VERT), 7);
  /* a click on the down arrow of the bar scrolls (non-client tracking sends WM_VSCROLL) */
  SM(a, EM_LINESCROLL, 0, MKL((uint16_t)-100, 0));
  MoveWindow(a, 10, 90, 120, (int16_t)(3 * text_h(g_top) + 2), 1); /* clear of the other edits */
  {
    UNcLayout nc;
    CHECK(user_nc_layout(a, &nc));
    CHECK(nc.vscroll.right > nc.vscroll.left);
    click_at(10 + nc.vscroll.left + 3, 90 + nc.vscroll.bottom - 4);
    CHECK(SM(a, EM_GETFIRSTVISIBLELINE, 0, 0) >= 1);
  }
  /* when the text fits the bar goes away, and it comes back with more text */
  SMS(a, WM_SETTEXT, 0, "x\r\ny");
  CHECK(!(user_wnd(a)->style & WS_VSCROLL));
  snprintf(buf, sizeof buf, "1\r\n2\r\n3\r\n4\r\n5\r\n6");
  SMS(a, WM_SETTEXT, 0, buf);
  CHECK(user_wnd(a)->style & WS_VSCROLL);
  CHECK_EQ(user_sb_get(a, SB_VERT)->max, 3);
  /* the compose box has no bar of its own */
  CHECK(!(user_wnd(e)->style & WS_VSCROLL));
  end();
}

static void test_ml_readonly_view(void) {
  HWND16 v;
  int th;
  begin();
  v = EL("l1\r\nl2\r\nl3", ML_VIEW, 5, 250);
  th = text_h(v);
  underlay(0, 0, 200, 100);
  pump();
  PIXEL_TEST("ctl_edit_multiline_view");
  /* the text shows on three rows, nothing below them */
  for (int i = 0; i < 3; i++) CHECK(cbox(11, 11 + i * th, 129, 11 + (i + 1) * th, BLACK).n > 5);
  CHECK_EQ(cbox(11, 11 + 3 * th, 129, 11 + 5 * th, BLACK).n, 0);
  CHECK_EQ(SM(v, EM_GETLINECOUNT, 0, 0), 3);
  /* typing, Enter, Backspace, Delete, paste, cut, clear change nothing and notify nothing */
  log_clear();
  SM(v, EM_SETSEL, 0, MKL(2, 2));
  SM(v, WM_CHAR, 'x', 0);
  SM(v, WM_CHAR, 13, 0);
  SM(v, WM_CHAR, 8, 0);
  SM(v, WM_KEYDOWN, VK_DELETE, 0);
  SM(v, WM_PASTE, 0, 0);
  SM(v, EM_SETSEL, 0, MKL(0, 3));
  SM(v, WM_CUT, 0, 0);
  SM(v, WM_CLEAR, 0, 0);
  CHECK_EQ(ncmd(), 0);
  CHECK_EQ(strcmp(text_of(v), "l1\r\nl2\r\nl3"), 0);
  CHECK_EQ(SM(v, EM_GETLINECOUNT, 0, 0), 3);
  /* the program can still set the text; the view then shows the new lines */
  SMS(v, WM_SETTEXT, 0, "a\r\nb\r\nc\r\nd");
  CHECK_EQ(SM(v, EM_GETLINECOUNT, 0, 0), 4);
  pump();
  CHECK(cbox(11, 11 + 3 * th, 129, 11 + 4 * th, BLACK).n > 3);
  /* the selection highlight spans the rows it covers (focused) */
  SetFocus(v);
  SM(v, EM_SETSEL, 0, MKL(0, 10));
  pump();
  CHECK(cbox(11, 11, 129, 11 + th, HIGHLIGHT_).n > 3);
  CHECK(cbox(11, 11 + th, 129, 11 + 2 * th, HIGHLIGHT_).n > 3);
  end();
}

static void test_ml_mouse_clipboard_dlgcode(void) {
  HWND16 e, f, s;
  int th;
  begin();
  th = text_h(g_top);
  e = E("M", "one\r\ntwo\r\nthree", ML_COMPOSE, 10, 10, 120, 4 * th + 2, 260);
  f = E("M2", "", ML_COMPOSE, 10, 80, 120, 4 * th + 2, 261);
  s = E("S", "", ES_AUTOHSCROLL, 10, 140, 120, 16, 262);
  underlay(0, 0, 200, 160);
  pump();
  /* a click on the second row puts the caret on the second line; far right = the end of that line */
  click_at(10 + 100, 10 + 1 + th + th / 2);
  CHECK_EQ(line_of_caret(e), 1);
  sel_is(e, 8, 8);
  click_at(10 + 1, 10 + 1 + 2 * th + th / 2); /* the third row, left edge */
  sel_is(e, 10, 10);
  click_at(10 + 100, 10 + 1 + 3 * th + th / 2); /* below the last line: the last line */
  sel_is(e, 15, 15);
  /* copy two lines and paste them into another multi-line edit: the line break survives */
  SM(e, EM_SETSEL, 0, MKL(0, 8));
  SM(e, WM_COPY, 0, 0);
  SM(f, WM_PASTE, 0, 0);
  CHECK_EQ(strcmp(text_of(f), "one\r\ntwo"), 0);
  CHECK_EQ(SM(f, EM_GETLINECOUNT, 0, 0), 2);
  /* a single-line edit still takes the clipboard only up to the first break (unchanged) */
  SM(s, WM_PASTE, 0, 0);
  CHECK_EQ(strcmp(text_of(s), "one"), 0);
  /* a multi-line edit wants Enter as a character (UNVERIFIED) */
  CHECK_EQ(SM(e, WM_GETDLGCODE, 0, 0), DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTALLKEYS);
  /* ES_PASSWORD does not apply to a multi-line edit (UNVERIFIED) */
  CHECK_EQ(SM(e, EM_GETPASSWORDCHAR, 0, 0), 0);
  end();
}

int main(void) {
  (void)dump;
  if (load_fixture() != 0) return 1;
  test_constants();
  test_class_and_text();
  test_settext_notifies_and_long_text();
  test_typing();
  test_real_keyboard();
  test_limittext_and_maxtext();
  test_selection_messages();
  test_shift_selection_and_words();
  test_clipboard();
  test_password_and_case_and_readonly();
  test_focus_notifications();
  test_line_messages();
  test_mouse_selection();
  test_pixels_selection_caret();
  test_no_autohscroll_rejects();
  test_subclass();
  test_win16_numbers_literal();
  test_ml_game_sites();
  test_ml_line_messages();
  test_ml_enter_and_backspace();
  test_ml_keys();
  test_ml_scrolling();
  test_ml_readonly_view();
  test_ml_mouse_clipboard_dlgcode();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  return TEST_DONE();
}
