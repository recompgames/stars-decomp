/* Standard controls, part 1 (task stars-0z4.10): class registration, the WM_CTLCOLOR default, the
 * parent notification, the dialog button helpers and small drawing helpers shared by BUTTON,
 * STATIC and SCROLLBAR (ctl_button.c, ctl_static.c, ctl_scroll.c). Plain C, no harness
 * dependency. Behaviour is Wine's (commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0) and
 * UNVERIFIED for Windows 3.1: see docs/inventory.md section 15, "Standard controls part 1". */
#include <strings.h>
#include <string.h>

#include "runtime/ctl.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define COL(i) user_syscolor(i)

/* ---- classes ---- */

static void reg_class(const char *name, uint32_t proc, uint16_t style, int16_t cbwnd) {
  UserState *st = user_state();
  for (unsigned i = 0; i < UC_MAX; i++) {
    UClass *c = &st->c[i];
    if (c->used) continue;
    memset(c, 0, sizeof *c);
    c->used = 1;
    c->style = style;
    c->wndproc = proc;
    c->cbwnd = cbwnd;
    strcpy(c->name, name);
    return;
  }
}

void user_ctl_init(void) {
  UserState *st = user_state();
  if (!st) return;
  /* BUTTON keeps 8 bytes of state (check/push/focus bits, font); STATIC 4 (font, icon); the scroll
   * bar control keeps its position and range in the UserState scroll table. */
  reg_class("BUTTON", USER_CB_BUTTON, CS_DBLCLKS | CS_VREDRAW | CS_HREDRAW, 8);
  reg_class("STATIC", USER_CB_STATIC, CS_DBLCLKS, 4);
  reg_class("SCROLLBAR", USER_CB_SCROLLBAR, 0, 0);
  /* part 2 (stars-0z4.11); the state sizes are asserted in ctl_edit.c / ctl_listbox.c / ctl_combo.c */
  reg_class("EDIT", USER_CB_EDIT, CS_DBLCLKS, USER_EDIT_EXTRA);
  reg_class("LISTBOX", USER_CB_LISTBOX, CS_DBLCLKS, USER_LISTBOX_EXTRA);
  reg_class("COMBOBOX", USER_CB_COMBOBOX, CS_DBLCLKS, USER_COMBO_EXTRA);
  st->ctl_brush[0] = CreateSolidBrush(COL(15)); /* COLOR_BTNFACE */
  st->ctl_brush[1] = CreateSolidBrush(COL(5));  /* COLOR_WINDOW */
  st->ctl_brush[2] = CreateSolidBrush(COL(0));  /* COLOR_SCROLLBAR */
}

void user_ctl_shutdown(void) {
  UserState *st = user_state();
  if (!st) return;
  /* the heap blocks of the part 2 controls (text, items) are outside their extra bytes */
  for (unsigned i = 1; i < UW_MAX; i++) {
    const UWnd *w = &st->w[i];
    const char *cn;
    if (!w->used) continue;
    cn = st->c[w->cls].name;
    if (!strcasecmp(cn, "EDIT")) user_edit_release(user_hwnd(w));
    else if (!strcasecmp(cn, "LISTBOX")) user_listbox_release(user_hwnd(w));
  }
  user_edit_clip_release();
  for (unsigned i = 0; i < 3; i++) {
    if (st->ctl_brush[i]) DeleteObject(st->ctl_brush[i]);
    st->ctl_brush[i] = 0;
  }
}

/* ---- WM_CTLCOLOR ---- */

/* win32u/defwnd.c handle_control_color 2335-2366: scroll bars get COLOR_BTNFACE text on a
 * COLOR_BTNHIGHLIGHT background and the COLOR_SCROLLBAR brush; edits and list boxes the window
 * colours; everything else (buttons, static text, dialogs, message boxes) COLOR_WINDOWTEXT on
 * COLOR_BTNFACE with the COLOR_BTNFACE brush. */
uint32_t user_def_ctlcolor(HDC16 hdc, uint16_t type) {
  UserState *st = user_state();
  if (!st || !hdc) return 0;
  if (type == CTLCOLOR_SCROLLBAR) {
    SetTextColor(hdc, COL(15));
    SetBkColor(hdc, COL(20));
    return st->ctl_brush[2];
  }
  SetTextColor(hdc, COL(8));
  if (type == CTLCOLOR_EDIT || type == CTLCOLOR_LISTBOX) {
    SetBkColor(hdc, COL(5));
    return st->ctl_brush[1];
  }
  SetBkColor(hdc, COL(15));
  return st->ctl_brush[0];
}

uint16_t user_ctl_color(HWND16 ctl, HDC16 hdc, uint16_t type) {
  HWND16 parent = GetParent(ctl);
  uint32_t r = 0;
  if (parent && user_wnd(parent) && parent != user_desktop()) r = SendMessage(parent, WM_CTLCOLOR, hdc, make_long(ctl, type)) & 0xFFFF;
  if (!r) r = user_def_ctlcolor(hdc, type) & 0xFFFF;
  return (uint16_t)r;
}

/* ---- notification ---- */

/* Win16 WM_COMMAND: wParam = control id, LOWORD(lParam) = control handle, HIWORD(lParam) = code
 * (the issue text; the Win32 packing puts the code in the high word of wParam). */
void user_ctl_notify(HWND16 ctl, uint16_t code) {
  const UWnd *w = user_wnd(ctl);
  HWND16 parent;
  if (!w) return;
  parent = GetParent(ctl);
  if (!parent || !user_wnd(parent) || parent == user_desktop()) return;
  SendMessage(parent, WM_COMMAND, w->id, make_long(ctl, code));
}

void *user_ctl_extra(HWND16 h, unsigned need) {
  const UWnd *w = user_wnd(h);
  if (!w || !w->extra_off || w->nextra < need) return NULL;
  return mem_arena() + w->extra_off;
}

/* ---- dialog button helpers ---- */

HWND16 user_dlg_item(HWND16 parent, int16_t id) {
  const UWnd *p = user_wnd(parent);
  if (!p) return 0;
  for (const UWnd *c = user_wnd(p->child); c; c = user_wnd(c->next))
    if (c->id == (uint16_t)id) return user_hwnd(c);
  return 0;
}

uint16_t CheckDlgButton(HWND16 hDlg, int16_t nIDButton, uint16_t uCheck) {
  HWND16 c = user_dlg_item(hDlg, nIDButton);
  if (!c) return 0;
  SendMessage(c, BM_SETCHECK, uCheck, 0);
  return 1;
}

/* Walks every direct child whose id is in [first, last] (Wine's behaviour; the old guard that
 * returned 0 when the first or last control was missing is gone) and always returns TRUE. */
uint16_t CheckRadioButton(HWND16 hDlg, int16_t nIDFirstButton, int16_t nIDLastButton, int16_t nIDCheckButton) {
  const UWnd *p = user_wnd(hDlg);
  if (!p) return 1;
  for (const UWnd *c = user_wnd(p->child); c; c = user_wnd(c->next)) {
    int16_t id = (int16_t)c->id;
    if (id >= nIDFirstButton && id <= nIDLastButton)
      SendMessage(user_hwnd(c), BM_SETCHECK, id == nIDCheckButton ? 1 : 0, 0);
  }
  return 1;
}

uint16_t IsDlgButtonChecked(HWND16 hDlg, int16_t nIDButton) {
  HWND16 c = user_dlg_item(hDlg, nIDButton);
  if (!c) return 0;
  return (uint16_t)SendMessage(c, BM_GETCHECK, 0, 0);
}

/* ---- drawing helpers ---- */

void user_ctl_fill_brush(HDC16 hdc, RECT16 r, uint16_t brush) {
  uint16_t old;
  if (r_empty(r) || !brush) return;
  old = SelectObject(hdc, brush);
  PatBlt(hdc, r.left, r.top, (int16_t)(r.right - r.left), (int16_t)(r.bottom - r.top), PATCOPY);
  SelectObject(hdc, old);
}

void user_ctl_draw_text(HDC16 hdc, RECT16 r, const char *text, uint16_t flags) {
  uint16_t hs = 0, hr = 0;
  FARPTR ps, pr;
  if (!text[0]) return;
  ps = user_scratch((uint32_t)strlen(text) + 1, &hs);
  pr = user_scratch(sizeof(RECT16), &hr);
  if (ps && pr) {
    strcpy(LIN(ps), text);
    *(RECT16 *)LIN(pr) = r;
    DrawText(hdc, ps, -1, pr, flags);
  }
  user_scratch_free(hs);
  user_scratch_free(hr);
}

void user_ctl_focus_rect(HDC16 hdc, RECT16 r, uint32_t color) {
  if (r.right - r.left < 2 || r.bottom - r.top < 2) return;
  for (int x = r.left; x < r.right; x++) {
    if (((x + r.top) & 1) == 0) SetPixel(hdc, (int16_t)x, r.top, color);
    if (((x + r.bottom - 1) & 1) == 0) SetPixel(hdc, (int16_t)x, (int16_t)(r.bottom - 1), color);
  }
  for (int y = r.top + 1; y < r.bottom - 1; y++) {
    if (((r.left + y) & 1) == 0) SetPixel(hdc, r.left, (int16_t)y, color);
    if (((r.right - 1 + y) & 1) == 0) SetPixel(hdc, (int16_t)(r.right - 1), (int16_t)y, color);
  }
}

uint16_t user_ctl_pick_font(uint16_t font) { return font ? font : (uint16_t)GetStockObject(SYSTEM_FONT); }

/* ---- part 2 helpers (stars-0z4.11) ---- */

void user_ctl_notify_to(HWND16 target, HWND16 ctl, uint16_t id, uint16_t code) {
  if (!target || !user_wnd(target) || target == user_desktop() || !user_wnd(ctl)) return;
  SendMessage(target, WM_COMMAND, id, make_long(ctl, code));
}

const char *user_ctl_far_str(FARPTR p, unsigned max, unsigned *len) {
  const char *s;
  uint64_t avail;
  if (!p || !mem_check(p, 1)) return NULL;
  avail = (uint64_t)desc_limit(FP_SEG(p)) + 1 - FP_OFF(p);
  if (avail > max) avail = max;
  s = LIN(p);
  for (uint64_t i = 0; i < avail; i++)
    if (!s[i]) {
      if (len) *len = (unsigned)i;
      return s;
    }
  if (len) *len = (unsigned)avail; /* not terminated within the limit: cut there */
  return s;
}

int user_ctl_text_width(HDC16 hdc, const char *s, int n) {
  uint16_t hs = 0;
  FARPTR ps;
  int w = 0;
  if (n <= 0) return 0;
  ps = user_scratch((uint32_t)n + 1, &hs);
  if (!ps) return 0;
  memcpy(LIN(ps), s, (size_t)n);
  ((char *)LIN(ps))[n] = 0;
  w = (int)(GetTextExtent(hdc, ps, (int16_t)n) & 0xFFFF);
  user_scratch_free(hs);
  return w;
}

int user_ctl_text_height(HDC16 hdc) {
  uint16_t hs = 0;
  FARPTR ps = user_scratch(4, &hs);
  int h = 0;
  if (!ps) return 0;
  strcpy(LIN(ps), "Ag");
  h = (int)(GetTextExtent(hdc, ps, 2) >> 16);
  user_scratch_free(hs);
  return h;
}

void user_ctl_text_out(HDC16 hdc, int x, int y, const char *s, int n) {
  uint16_t hs = 0;
  FARPTR ps;
  if (n <= 0) return;
  ps = user_scratch((uint32_t)n + 1, &hs);
  if (!ps) return;
  memcpy(LIN(ps), s, (size_t)n);
  ((char *)LIN(ps))[n] = 0;
  TextOut(hdc, (int16_t)x, (int16_t)y, ps, (int16_t)n);
  user_scratch_free(hs);
}

/* ---- the edit controls' internal clipboard ---- */

void user_edit_clip_set(const char *s, unsigned n) {
  UserState *st = user_state();
  if (!st) return;
  if (n + 1u > st->ctl_clip_cap) {
    void *b = rt_alloc(n + 1u);
    if (!b) return;
    if (st->ctl_clip_off) rt_free(mem_arena() + st->ctl_clip_off);
    st->ctl_clip_off = mem_off(b);
    st->ctl_clip_cap = (uint16_t)(n + 1u);
  }
  memcpy(mem_arena() + st->ctl_clip_off, s, n);
  mem_arena()[st->ctl_clip_off + n] = 0;
  st->ctl_clip_len = (uint16_t)n;
}

unsigned user_edit_clip_get(const char **s) {
  UserState *st = user_state();
  if (!st || !st->ctl_clip_off) {
    *s = "";
    return 0;
  }
  *s = (const char *)(mem_arena() + st->ctl_clip_off);
  return st->ctl_clip_len;
}

void user_edit_clip_release(void) {
  UserState *st = user_state();
  if (st && st->ctl_clip_off) rt_free(mem_arena() + st->ctl_clip_off);
  if (st) st->ctl_clip_off = st->ctl_clip_len = st->ctl_clip_cap = 0;
}
