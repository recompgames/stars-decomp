/* The COMBOBOX control class (task stars-0z4.11): CBS_SIMPLE, CBS_DROPDOWN and CBS_DROPDOWNLIST,
 * with the owner-draw variants. Wine's design: the combo box is a window with child controls. A
 * CBS_DROPDOWN / CBS_SIMPLE combo box has an EDIT child (id 1001); the list is a LISTBOX (id 1000), a
 * child under the edit for CBS_SIMPLE and a popup window (owned by the top-level ancestor, shown with
 * SW_SHOWNOACTIVATE, holding the mouse capture while dropped) for the drop-down kinds. Both children
 * report to the combo box with WM_COMMAND (the list through the LBS_COMBOBOX link, so that the
 * owner-draw messages and the notifications are not sent to the dialog under the list's own id).
 * The CB_* messages (Win16 numbering, include/wine/winuser16.h at commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0 lines 234-258) are mapped onto LB_* / EM_* messages.
 *
 * A CBS_DROPDOWN / CBS_DROPDOWNLIST combo box shrinks to the height of its closed part (item height
 * + 6); the height given at creation less that is the height of the dropped list. Everything is
 * UNVERIFIED for Windows 3.1 (docs/inventory.md section 16). Plain C, no harness dependency. */
#include <string.h>

#include "runtime/ctl.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define COL(i) user_syscolor(i)

#define ID_LIST 1000
#define ID_EDIT 1001
#define CB_PAD 6 /* closed height = item height + 6 */

#define CF_DROPPED 1
#define CF_FOCUS 2
#define CF_EXTUI 4

typedef struct CbX {
  HWND16 list, edit;
  uint16_t type;     /* CBS_SIMPLE, CBS_DROPDOWN, CBS_DROPDOWNLIST */
  uint16_t closed_h; /* height of the edit / selection field */
  uint16_t full_h;   /* height given at creation */
  uint16_t flags, font;
  int16_t saved;     /* selection when the list was dropped (Esc restores it) */
  uint16_t btnw;
} CbX;
_Static_assert(sizeof(CbX) <= USER_COMBO_EXTRA, "CbX does not fit the COMBOBOX cbWndExtra");

static CbX *cx_(HWND16 h) { return user_ctl_extra(h, sizeof(CbX)); }
static bool has_edit(const CbX *x) { return x->type != CBS_DROPDOWNLIST; }
static bool is_od(const UWnd *w) { return (w->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) != 0; }
static int cwidth(const UWnd *w) { return w->crect.right - w->crect.left; }

static uint32_t lbmsg(const CbX *x, uint16_t msg, uint16_t wp, uint32_t lp) {
  return x->list ? SendMessage(x->list, msg, wp, lp) : (uint32_t)CB_ERR;
}

/* the text of item i into a malloc-free scratch string (NUL-terminated, at most n-1 characters) */
static unsigned item_text(const CbX *x, int i, char *out, unsigned n) {
  uint16_t hs = 0;
  FARPTR p;
  uint32_t len;
  out[0] = 0;
  if (i < 0) return 0;
  len = lbmsg(x, LB_GETTEXTLEN, (uint16_t)i, 0);
  if ((int32_t)len < 0) return 0;
  p = user_scratch(len + 2u, &hs);
  if (!p) return 0;
  if ((int32_t)lbmsg(x, LB_GETTEXT, (uint16_t)i, p) >= 0) {
    strncpy(out, (const char *)LIN(p), n - 1);
    out[n - 1] = 0;
  }
  user_scratch_free(hs);
  return (unsigned)strlen(out);
}

/* ---- geometry ---- */

static RECT16 button_rect(const UWnd *w, const CbX *x) {
  return r_make(cwidth(w) - 1 - x->btnw, 1, cwidth(w) - 1, x->closed_h - 1);
}

static void layout(HWND16 h) {
  const UWnd *w = user_wnd(h);
  CbX *x = cx_(h);
  int cw, ch;
  if (!w || !x) return;
  cw = cwidth(w);
  ch = w->crect.bottom - w->crect.top;
  if (x->type == CBS_SIMPLE) {
    if (x->edit) MoveWindow(x->edit, 0, 0, (int16_t)cw, (int16_t)x->closed_h, 1);
    if (x->list) MoveWindow(x->list, 0, (int16_t)x->closed_h, (int16_t)cw, (int16_t)(ch - x->closed_h > 0 ? ch - x->closed_h : 0), 1);
  } else if (x->edit) {
    MoveWindow(x->edit, 1, 1, (int16_t)(cw - 2 - x->btnw), (int16_t)(x->closed_h - 2), 1);
  }
}

/* the height of the dropped list window: what is left of the creation height, cut to whole items
 * (2 pixels of border) unless CBS_NOINTEGRALHEIGHT or variable item heights */
static int list_height(HWND16 h, const CbX *x) {
  const UWnd *w = user_wnd(h);
  int ih = (int)lbmsg(x, LB_GETITEMHEIGHT, 0, 0);
  int lh = (int)x->full_h - (int)x->closed_h;
  if (ih < 1) ih = 1;
  if (lh < ih + 2) lh = ih + 2;
  if (w && !(w->style & (CBS_NOINTEGRALHEIGHT | CBS_OWNERDRAWVARIABLE))) lh = ((lh - 2) / ih) * ih + 2;
  return lh;
}

/* ---- children ---- */

static HWND16 make_child(const char *cls, uint32_t style, HWND16 parent, uint16_t id, uint16_t hinst, uint32_t param) {
  uint16_t hs = 0, ht = 0;
  FARPTR pc = user_scratch((uint32_t)strlen(cls) + 1, &hs), pt = user_scratch(2, &ht);
  HWND16 c = 0;
  if (pc && pt) {
    strcpy(LIN(pc), cls);
    ((char *)LIN(pt))[0] = 0;
    c = CreateWindow(pc, pt, style, 0, 0, 0, 0, parent, id, hinst, param);
  }
  user_scratch_free(hs);
  user_scratch_free(ht);
  return c;
}

static void paint_now(HWND16 h) {
  if (user_wnd(h) && IsWindowVisible(h)) {
    InvalidateRect(h, 0, 0);
    UpdateWindow(h);
  }
}

/* ---- the selection field ---- */

static void sync_field(HWND16 h) {
  CbX *x = cx_(h);
  char buf[512];
  int sel;
  if (!x) return;
  if (has_edit(x) && x->edit) {
    sel = (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
    if (sel >= 0) {
      unsigned n = item_text(x, sel, buf, sizeof buf);
      user_edit_set_quiet(x->edit, buf, n, true);
    }
  } else {
    paint_now(h);
  }
}

/* ---- the drop-down list ---- */

static void open_list(HWND16 h) {
  UWnd *w = user_wnd(h);
  CbX *x = cx_(h);
  RECT16 r;
  uint16_t hr = 0;
  FARPTR pr;
  int lh;
  if (!w || !x || x->type == CBS_SIMPLE || (x->flags & CF_DROPPED) || !x->list) return;
  x->saved = (int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
  user_ctl_notify(h, CBN_DROPDOWN);
  x = cx_(h);
  if (!x || !user_wnd(h)) return;
  pr = user_scratch(sizeof(RECT16), &hr);
  if (!pr) return;
  GetWindowRect(h, pr);
  r = *(RECT16 *)LIN(pr);
  user_scratch_free(hr);
  lh = list_height(h, x);
  MoveWindow(x->list, r.left, r.bottom, (int16_t)(r.right - r.left), (int16_t)lh, 1);
  if (x->saved >= 0) lbmsg(x, LB_SETCURSEL, (uint16_t)x->saved, 0);
  ShowWindow(x->list, SW_SHOWNOACTIVATE);
  SetWindowPos(x->list, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  x->flags |= CF_DROPPED;
  SetCapture(x->list);
  paint_now(h);
}

/* Closes the list; accept = false restores the selection of the time it was dropped. */
static void close_list(HWND16 h, bool accept) {
  CbX *x = cx_(h);
  if (!x || !(x->flags & CF_DROPPED)) return;
  x->flags &= ~(uint16_t)CF_DROPPED;
  if (GetCapture() == x->list) ReleaseCapture();
  if (user_wnd(x->list)) ShowWindow(x->list, SW_HIDE);
  if (!accept) {
    lbmsg(x, LB_SETCURSEL, (uint16_t)x->saved, 0);
    sync_field(h);
  }
  paint_now(h);
}

/* a selection the user made: update the field, report CBN_SELCHANGE */
static void user_picked(HWND16 h) {
  CbX *x = cx_(h);
  if (!x) return;
  if (x->flags & CF_DROPPED) close_list(h, true);
  x = cx_(h);
  if (!x) return;
  sync_field(h);
  if (user_wnd(h)) user_ctl_notify(h, CBN_SELCHANGE);
}

/* ---- drawing ---- */

static void draw_field(HWND16 h, HDC16 dc) {
  const UWnd *w = user_wnd(h);
  const CbX *x = cx_(h);
  RECT16 fr, br;
  int sel;
  bool focus, enabled;
  if (!w || !x) return;
  br = button_rect(w, x);
  fr = r_make(1, 1, br.left, x->closed_h - 1);
  focus = (x->flags & CF_FOCUS) != 0;
  enabled = !(w->style & WS_DISABLED);
  sel = (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
  /* frame */
  user_fill(dc, r_make(0, 0, cwidth(w), x->closed_h), COL(6));
  if (x->type == CBS_DROPDOWNLIST) {
    if (is_od(w)) {
      user_ctl_fill_brush(dc, fr, user_ctl_color(h, dc, CTLCOLOR_EDIT));
      {
        HWND16 par = GetParent(h);
        uint16_t hs = 0;
        FARPTR p = user_scratch(sizeof(DRAWITEMSTRUCT16), &hs);
        if (p && par && user_wnd(par)) {
          DRAWITEMSTRUCT16 *d = LIN(p);
          d->CtlType = ODT_COMBOBOX;
          d->CtlID = w->id;
          d->itemID = sel >= 0 ? (uint16_t)sel : 0xFFFF;
          d->itemAction = ODA_DRAWENTIRE;
          d->itemState = (uint16_t)(ODS_COMBOBOXEDIT | (focus ? (ODS_FOCUS | ODS_SELECTED) : 0) | (enabled ? 0 : ODS_DISABLED));
          d->hwndItem = h;
          d->hDC = dc;
          d->rcItem = fr;
          d->itemData = sel >= 0 ? lbmsg(x, LB_GETITEMDATA, (uint16_t)sel, 0) : 0;
          SendMessage(par, WM_DRAWITEM, w->id, p);
        }
        user_scratch_free(hs);
      }
    } else {
      uint16_t brush = user_ctl_color(h, dc, CTLCOLOR_EDIT);
      uint16_t old = SelectObject(dc, user_ctl_pick_font(x->font));
      char buf[512];
      int th = user_ctl_text_height(dc);
      user_ctl_fill_brush(dc, fr, brush);
      if (focus) user_fill(dc, fr, COL(13));
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, focus ? COL(14) : (enabled ? COL(8) : COL(17)));
      if (item_text(x, sel, buf, sizeof buf)) user_ctl_text_out(dc, fr.left + 1, fr.top + (fr.bottom - fr.top - th) / 2, buf, (int)strlen(buf));
      SelectObject(dc, old);
    }
  }
  /* the arrow button: COLOR_BTNFACE with the push button bevel and a black down arrow */
  user_fill(dc, br, COL(15));
  user_fill(dc, r_make(br.left, br.top, br.right - 1, br.top + 1), COL(20));
  user_fill(dc, r_make(br.left, br.top, br.left + 1, br.bottom - 1), COL(20));
  user_fill(dc, r_make(br.left, br.bottom - 1, br.right, br.bottom), COL(16));
  user_fill(dc, r_make(br.right - 1, br.top, br.right, br.bottom), COL(16));
  user_tri_down(dc, br.left + (br.right - br.left) / 2 - 3, br.top + (br.bottom - br.top) / 2 - 1, enabled ? COL(6) : COL(17));
}

/* ---- keys ---- */

static void key_down(HWND16 h, CbX *x, uint16_t vk) {
  /* CB_SETEXTENDEDUI: F4 does nothing and Down opens the list (UNVERIFIED, Wine combo.c CBKeyDown). */
  if ((x->flags & CF_EXTUI) && !(x->flags & CF_DROPPED) && vk == VK_DOWN) {
    open_list(h);
    return;
  }
  if (vk == VK_F4) {
    if (x->flags & CF_EXTUI) return;
    if (x->flags & CF_DROPPED) close_list(h, true);
    else open_list(h);
    return;
  }
  if (x->flags & CF_DROPPED) {
    if (vk == VK_RETURN) {
      int cur = (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
      bool changed = cur != x->saved;
      close_list(h, true);
      x = cx_(h);
      if (x && changed) {
        sync_field(h);
        if (user_wnd(h)) user_ctl_notify(h, CBN_SELCHANGE);
      }
    } else if (vk == VK_ESCAPE) {
      close_list(h, false);
    } else if (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END) {
      lbmsg(x, WM_KEYDOWN, vk, 0);
    }
    return;
  }
  if (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT) {
    int cur = (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
    int n = (int)lbmsg(x, LB_GETCOUNT, 0, 0), ni = cur;
    if (n <= 0) return;
    if (vk == VK_UP || vk == VK_PRIOR) ni = cur <= 0 ? 0 : cur - 1;
    else ni = cur < 0 ? 0 : (cur + 1 < n ? cur + 1 : n - 1);
    if (ni != cur) {
      lbmsg(x, LB_SETCURSEL, (uint16_t)ni, 0);
      user_picked(h);
    }
  }
}

/* ---- WM_COMMAND from the children ---- */

static void child_command(HWND16 h, CbX *x, uint16_t id, uint16_t code) {
  if (id == ID_LIST) {
    switch (code) {
      case LBN_SELCHANGE: user_picked(h); break;
      case LBN_DBLCLK: user_ctl_notify(h, CBN_DBLCLK); break;
      case LBN_SELCANCEL: close_list(h, false); break;
      default: break;
    }
  } else if (id == ID_EDIT) {
    switch (code) {
      case EN_SETFOCUS:
        x->flags |= CF_FOCUS;
        user_ctl_notify(h, CBN_SETFOCUS);
        break;
      case EN_KILLFOCUS:
        x->flags &= ~(uint16_t)CF_FOCUS;
        close_list(h, false);
        if (user_wnd(h)) user_ctl_notify(h, CBN_KILLFOCUS);
        break;
      case EN_UPDATE: user_ctl_notify(h, CBN_EDITUPDATE); break;
      case EN_CHANGE: user_ctl_notify(h, CBN_EDITCHANGE); break;
      default: break;
    }
  }
}

/* ---- messages ---- */

static uint32_t cb_message(HWND16 h, UWnd *w, CbX *x, uint16_t msg, uint16_t wp, uint32_t lp) {
  switch (msg) {
    case CB_GETEDITSEL: return x->edit ? SendMessage(x->edit, EM_GETSEL, 0, 0) : (uint32_t)CB_ERR;
    case CB_LIMITTEXT: return x->edit ? SendMessage(x->edit, EM_LIMITTEXT, wp, 0) : (uint32_t)CB_ERR;
    case CB_SETEDITSEL: return x->edit ? SendMessage(x->edit, EM_SETSEL, 0, lp) : (uint32_t)CB_ERR;
    case CB_ADDSTRING: return lbmsg(x, LB_ADDSTRING, wp, lp);
    case CB_DELETESTRING: return lbmsg(x, LB_DELETESTRING, wp, lp);
    case CB_DIR: return lbmsg(x, LB_DIR, wp, lp);
    case CB_GETCOUNT: return lbmsg(x, LB_GETCOUNT, wp, lp);
    case CB_GETCURSEL: return lbmsg(x, LB_GETCURSEL, wp, lp);
    case CB_GETLBTEXT: return lbmsg(x, LB_GETTEXT, wp, lp);
    case CB_GETLBTEXTLEN: return lbmsg(x, LB_GETTEXTLEN, wp, lp);
    case CB_INSERTSTRING: return lbmsg(x, LB_INSERTSTRING, wp, lp);
    case CB_RESETCONTENT: {
      uint32_t r = lbmsg(x, LB_RESETCONTENT, wp, lp);
      if (x->edit) user_edit_set_quiet(x->edit, "", 0, false);
      paint_now(h);
      return r;
    }
    case CB_FINDSTRING: return lbmsg(x, LB_FINDSTRING, wp, lp);
    case CB_FINDSTRINGEXACT: return lbmsg(x, LB_FINDSTRINGEXACT, wp, lp);
    case CB_SELECTSTRING: {
      uint32_t r = lbmsg(x, LB_SELECTSTRING, wp, lp);
      if ((int32_t)r >= 0) sync_field(h);
      return r;
    }
    case CB_SETCURSEL: {
      uint32_t r = lbmsg(x, LB_SETCURSEL, wp, lp);
      if ((int32_t)r >= 0) sync_field(h);
      else if (x->edit && (int16_t)wp == -1) user_edit_set_quiet(x->edit, "", 0, false);
      else paint_now(h);
      return r;
    }
    case CB_SHOWDROPDOWN:
      if (x->type == CBS_SIMPLE) return 1;
      if (wp) open_list(h);
      else close_list(h, true);
      return 1;
    case CB_GETITEMDATA: return lbmsg(x, LB_GETITEMDATA, wp, lp);
    case CB_SETITEMDATA: return lbmsg(x, LB_SETITEMDATA, wp, lp);
    case CB_GETDROPPEDCONTROLRECT: {
      uint16_t hr = 0;
      FARPTR pr;
      RECT16 r;
      if (!lp || !mem_check(lp, sizeof(RECT16))) return (uint32_t)CB_ERR;
      pr = user_scratch(sizeof(RECT16), &hr);
      if (!pr) return (uint32_t)CB_ERR;
      GetWindowRect(h, pr);
      r = *(RECT16 *)LIN(pr);
      user_scratch_free(hr);
      if (x->type != CBS_SIMPLE) r.bottom = (int16_t)(r.bottom + list_height(h, x));
      *(RECT16 *)LIN(lp) = r;
      return 0;
    }
    case CB_SETITEMHEIGHT:
      if ((int16_t)wp == -1) {
        unsigned hh = lp & 0xFFFF;
        if (hh < 1 || hh > 255) return (uint32_t)CB_ERR;
        x->closed_h = (uint16_t)hh;
        if (x->type != CBS_SIMPLE)
          SetWindowPos(h, 0, 0, 0, (int16_t)(w->wrect.right - w->wrect.left), (int16_t)hh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        layout(h);
        return 0;
      }
      return lbmsg(x, LB_SETITEMHEIGHT, wp, lp);
    case CB_GETITEMHEIGHT:
      if ((int16_t)wp == -1) return x->closed_h;
      return lbmsg(x, LB_GETITEMHEIGHT, wp, lp);
    case CB_SETEXTENDEDUI:
      if (wp) x->flags |= CF_EXTUI;
      else x->flags &= ~(uint16_t)CF_EXTUI;
      return 0;
    case CB_GETEXTENDEDUI: return (x->flags & CF_EXTUI) ? 1 : 0;
    case CB_GETDROPPEDSTATE: return (x->flags & CF_DROPPED) ? 1 : 0;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}

uint32_t user_combobox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  CbX *x = cx_(h);
  if (!w || !x) return user_def_window_proc(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE: /* WS_VSCROLL / WS_HSCROLL in the template are for the drop-down list, not for this window */
      w->style &= ~(uint32_t)(WS_VSCROLL | WS_HSCROLL);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_CREATE: {
      const CREATESTRUCT16 *cs = lp && mem_check(lp, sizeof *cs) ? LIN(lp) : NULL;
      uint32_t lst = LBS_NOTIFY | LBS_COMBOBOX | WS_BORDER;
      HDC16 dc;
      int th = 13;
      x->type = (uint16_t)(w->style & 3);
      if (x->type < CBS_SIMPLE) x->type = CBS_SIMPLE;
      x->full_h = (uint16_t)(cs ? cs->cy : (w->wrect.bottom - w->wrect.top));
      x->btnw = (uint16_t)GetSystemMetrics(2); /* SM_CXVSCROLL */
      x->saved = -1;
      dc = GetDC(h);
      if (dc) {
        uint16_t old = SelectObject(dc, user_ctl_pick_font(0));
        th = user_ctl_text_height(dc);
        SelectObject(dc, old);
        ReleaseDC(h, dc);
      }
      x->closed_h = (uint16_t)(th + CB_PAD);
      if (w->style & CBS_SORT) lst |= LBS_SORT;
      if (w->style & CBS_HASSTRINGS) lst |= LBS_HASSTRINGS;
      if (w->style & CBS_OWNERDRAWFIXED) lst |= LBS_OWNERDRAWFIXED;
      if (w->style & CBS_OWNERDRAWVARIABLE) lst |= LBS_OWNERDRAWVARIABLE;
      if (cs && (cs->style & WS_VSCROLL)) lst |= WS_VSCROLL;
      if (w->style & CBS_NOINTEGRALHEIGHT) lst |= LBS_NOINTEGRALHEIGHT;
      lst |= x->type == CBS_SIMPLE ? (WS_CHILD | WS_VISIBLE) : WS_POPUP;
      x->list = make_child("LISTBOX", lst, h, ID_LIST, w->hinst, h);
      if (!user_wnd(h) || !x->list) return (uint32_t)-1;
      x = cx_(h);
      {
        int ih = (int)lbmsg(x, LB_GETITEMHEIGHT, 0, 0);
        if (ih >= 1) x->closed_h = (uint16_t)(ih + CB_PAD);
      }
      if (x->type != CBS_SIMPLE && x->full_h != x->closed_h)
        SetWindowPos(h, 0, 0, 0, (int16_t)(w->wrect.right - w->wrect.left), (int16_t)x->closed_h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
      if (!user_wnd(h)) return (uint32_t)-1;
      if (has_edit(cx_(h))) {
        uint32_t es = WS_CHILD | WS_VISIBLE | ((w->style & CBS_AUTOHSCROLL) ? ES_AUTOHSCROLL : 0);
        if (x->type == CBS_SIMPLE) es |= WS_BORDER;
        x = cx_(h);
        x->edit = make_child("EDIT", es, h, ID_EDIT, w->hinst, 0);
        if (!user_wnd(h) || !x->edit) return (uint32_t)-1;
        user_edit_set_combo(x->edit);
      }
      layout(h);
      return 0;
    }
    case WM_DESTROY:
      if (x->flags & CF_DROPPED) {
        x->flags &= ~(uint16_t)CF_DROPPED;
        if (GetCapture() == x->list) ReleaseCapture();
      }
      if (x->type != CBS_SIMPLE && user_wnd(x->list)) DestroyWindow(x->list); /* the popup is owned, not a child */
      return user_def_window_proc(h, msg, wp, lp);
    case WM_GETDLGCODE: { /* a dropped list keeps Enter and Esc for itself (dialog manager, stars-0z4.12; Wine combo.c) */
      uint32_t code = DLGC_WANTCHARS | DLGC_WANTARROWS;
      if (lp && (x->flags & CF_DROPPED) && mem_check(lp, sizeof(MSG16))) {
        const MSG16 *km = LIN(lp);
        if (km->message == WM_KEYDOWN && (km->wParam == VK_RETURN || km->wParam == VK_ESCAPE)) code |= DLGC_WANTMESSAGE;
      }
      return code;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      uint16_t hp = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp);
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc && x->type != CBS_SIMPLE) draw_field(h, dc);
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_SIZE:
    case WM_MOVE:
      layout(h);
      return 0;
    case WM_ENABLE:
      if (x->edit) EnableWindow(x->edit, wp);
      if (x->type == CBS_SIMPLE && x->list) EnableWindow(x->list, wp);
      paint_now(h);
      return 0;
    case WM_SETFONT:
      x->font = wp;
      if (x->list) SendMessage(x->list, WM_SETFONT, wp, lp);
      if (x->edit) SendMessage(x->edit, WM_SETFONT, wp, lp);
      paint_now(h);
      return 0;
    case WM_GETFONT: return x->font;
    case WM_GETTEXT:
      if (x->edit) return SendMessage(x->edit, WM_GETTEXT, wp, lp);
      {
        char buf[512];
        unsigned n = item_text(x, (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0), buf, sizeof buf);
        if (!wp || !lp) return 0;
        if (n > (unsigned)wp - 1u) n = (unsigned)wp - 1u;
        if (!mem_check(lp, n + 1u)) return 0;
        memcpy(LIN(lp), buf, n);
        ((char *)LIN(lp))[n] = 0;
        return n;
      }
    case WM_GETTEXTLENGTH:
      if (x->edit) return SendMessage(x->edit, WM_GETTEXTLENGTH, 0, 0);
      {
        char buf[512];
        return item_text(x, (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0), buf, sizeof buf);
      }
    case WM_SETTEXT:
      if (x->edit) return SendMessage(x->edit, WM_SETTEXT, wp, lp);
      return 0;
    case WM_CTLCOLOR: { /* the children ask us; we ask the dialog */
      HWND16 par = GetParent(h);
      uint32_t r = 0;
      if (par && user_wnd(par) && par != user_desktop()) r = SendMessage(par, WM_CTLCOLOR, wp, lp) & 0xFFFF; /* lParam unchanged: LOWORD stays the child (as Wine) */
      if (!r) r = user_def_ctlcolor(wp, (uint16_t)(lp >> 16)) & 0xFFFF;
      return r;
    }
    case WM_COMMAND:
      child_command(h, x, wp, (uint16_t)(lp >> 16));
      return 0;
    case WM_SETFOCUS:
      if (x->edit) {
        SetFocus(x->edit);
      } else {
        x->flags |= CF_FOCUS;
        paint_now(h);
        user_ctl_notify(h, CBN_SETFOCUS);
      }
      return 0;
    case WM_KILLFOCUS:
      if (wp == x->edit || (wp == x->list && x->list)) return 0; /* the focus moves between our own parts */
      if (!x->edit) {
        x->flags &= ~(uint16_t)CF_FOCUS;
        close_list(h, false);
        paint_now(h);
        if (user_wnd(h)) user_ctl_notify(h, CBN_KILLFOCUS);
      }
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      if (x->type == CBS_SIMPLE) return 0;
      SetFocus(h);
      if (!user_wnd(h)) return 0;
      if (x->flags & CF_DROPPED) close_list(h, true);
      else open_list(h);
      return 0;
    case WM_CANCELMODE:
      close_list(h, false);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_SYSKEYDOWN:
      if (wp == VK_DOWN && x->type != CBS_SIMPLE) { /* Alt+Down opens the list */
        open_list(h);
        return 0;
      }
      return user_def_window_proc(h, msg, wp, lp);
    case WM_KEYDOWN:
      key_down(h, x, wp);
      return 0;
    case WM_CHAR:
      if (!x->edit && x->list && !(x->flags & CF_DROPPED)) { /* type-ahead through the list */
        int before = (int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0);
        lbmsg(x, WM_CHAR, wp, lp);
        if ((int)(int16_t)lbmsg(x, LB_GETCURSEL, 0, 0) != before) user_picked(h);
      } else if (!x->edit && (x->flags & CF_DROPPED) && x->list) {
        lbmsg(x, WM_CHAR, wp, lp);
      }
      return 0;
    default:
      if (msg >= WM_USER && msg <= CB_FINDSTRINGEXACT) return cb_message(h, w, x, msg, wp, lp);
      return user_def_window_proc(h, msg, wp, lp);
  }
}
