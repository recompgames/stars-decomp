/* The LISTBOX control class (task stars-0z4.11): single, multiple and extended selection, LBS_SORT,
 * owner-draw (fixed and variable heights, WM_MEASUREITEM / WM_DRAWITEM / WM_DELETEITEM /
 * WM_COMPAREITEM with the Win16 structures), tab stops, LB_DIR over the VFS, the vertical window scroll
 * bar (WS_VSCROLL) and the LB_* messages with Win16 numbering (include/wine/winuser16.h at commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0 lines 195-231, cited in runtime/tests/test_ctl_listbox.c).
 * The drop-down list of a combo box (ctl_combo.c) is a listbox with a link to its combo box.
 *
 * State: the control's extra bytes (LbX) and, in the arena, an array of items (LbItem) and one block
 * per item string. Behaviour follows Wine's listbox and the issue text; all of it is UNVERIFIED for
 * Windows 3.1 (docs/inventory.md section 16). Plain C, no harness dependency. */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "runtime/ctl.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"
#include "runtime/vfs.h"

#define COL(i) user_syscolor(i)

#define LB_LM 1 /* left margin of the item text */
#define LB_MAXTABS 16

#define LF_FOCUS 1
#define LF_NOREDRAW 2
#define LF_CAPTURED 4
#define LF_CARETOFF 8
#define LF_SIZING 16
#define LF_VSCROLL 32 /* created with WS_VSCROLL: SetScrollRange hides and shows the bar, the style bit comes and goes */

typedef struct LbItem {
  uint32_t str;  /* arena offset of the NUL-terminated string, 0 when the list has no strings */
  uint32_t data; /* LB_SETITEMDATA value, or lParam of LB_ADDSTRING for a list without strings */
  uint16_t h;    /* LBS_OWNERDRAWVARIABLE item height */
  uint8_t sel, pad;
} LbItem;

typedef struct LbX {
  uint32_t items; /* arena offset of LbItem[cap] */
  uint16_t count, cap;
  int16_t cur;    /* the selected item of a single-selection list, -1 none */
  int16_t caret, anchor;
  uint16_t top, font, itemh, flags, hext, colw;
  HWND16 combo;   /* the combo box this list belongs to, 0 for a plain list box */
  uint16_t ntabs;
  uint16_t tabs[LB_MAXTABS]; /* dialog units */
} LbX;
_Static_assert(sizeof(LbX) <= USER_LISTBOX_EXTRA, "LbX does not fit the LISTBOX cbWndExtra");

static LbX *lx(HWND16 h) { return user_ctl_extra(h, sizeof(LbX)); }
static LbItem *its(const LbX *x) { return x->items ? (LbItem *)(mem_arena() + x->items) : NULL; }
static const char *str_of(const LbItem *i) { return i->str ? (const char *)(mem_arena() + i->str) : ""; }

static bool is_od(const UWnd *w) { return (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0; }
static bool is_var(const UWnd *w) { return (w->style & LBS_OWNERDRAWVARIABLE) != 0; }
static bool has_strings(const UWnd *w) { return !is_od(w) || (w->style & LBS_HASSTRINGS); }
static bool is_multi(const UWnd *w) { return (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0; }
static bool is_ext(const UWnd *w) { return (w->style & LBS_EXTENDEDSEL) != 0; }

static int cwidth(const UWnd *w) { return w->crect.right - w->crect.left; }
static int cheight(const UWnd *w) { return w->crect.bottom - w->crect.top; }

/* ---- owner-draw context ---- */

typedef struct OdCtx {
  uint16_t type, id;
  HWND16 item, target;
} OdCtx;

static bool od_ctx(HWND16 h, const UWnd *w, const LbX *x, OdCtx *c) {
  if (x->combo) {
    const UWnd *cw = user_wnd(x->combo);
    if (!cw) return false;
    c->type = ODT_COMBOBOX;
    c->id = cw->id;
    c->item = x->combo;
    c->target = GetParent(x->combo);
  } else {
    c->type = ODT_LISTBOX;
    c->id = w->id;
    c->item = h;
    c->target = GetParent(h);
  }
  return c->target && user_wnd(c->target) && c->target != user_desktop();
}

static uint16_t send_measure(HWND16 h, unsigned idx, uint32_t data, uint16_t defh) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  OdCtx c;
  uint16_t hs = 0, r = defh;
  FARPTR p;
  if (!w || !x || !od_ctx(h, w, x, &c)) return defh;
  p = user_scratch(sizeof(MEASUREITEMSTRUCT16), &hs);
  if (!p) return defh;
  {
    MEASUREITEMSTRUCT16 *m = LIN(p);
    m->CtlType = c.type;
    m->CtlID = c.id;
    m->itemID = (uint16_t)idx;
    m->itemWidth = (uint16_t)cwidth(w);
    m->itemHeight = defh;
    m->itemData = data;
  }
  SendMessage(c.target, WM_MEASUREITEM, c.id, p);
  r = ((MEASUREITEMSTRUCT16 *)LIN(p))->itemHeight;
  user_scratch_free(hs);
  return r ? r : defh;
}

static void send_delete(HWND16 h, unsigned idx, uint32_t data) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  OdCtx c;
  uint16_t hs = 0;
  FARPTR p;
  if (!w || !x || !is_od(w) || !od_ctx(h, w, x, &c)) return;
  p = user_scratch(sizeof(DELETEITEMSTRUCT16), &hs);
  if (!p) return;
  {
    DELETEITEMSTRUCT16 *d = LIN(p);
    d->CtlType = c.type;
    d->CtlID = c.id;
    d->itemID = (uint16_t)idx;
    d->hwndItem = c.item;
    d->itemData = data;
  }
  SendMessage(c.target, WM_DELETEITEM, c.id, p);
  user_scratch_free(hs);
}

/* ---- geometry ---- */

static int item_h(const UWnd *w, const LbX *x, unsigned i) {
  if (is_var(w) && i < x->count) return its(x)[i].h ? its(x)[i].h : x->itemh;
  return x->itemh ? x->itemh : 1;
}

/* y of the top of item i relative to the client area (negative above the first visible item) */
static int item_y(const UWnd *w, const LbX *x, unsigned i) {
  int y = 0;
  if (!is_var(w)) return ((int)i - (int)x->top) * item_h(w, x, 0);
  if (i >= x->top)
    for (unsigned k = x->top; k < i; k++) y += item_h(w, x, k);
  else
    for (unsigned k = i; k < x->top; k++) y -= item_h(w, x, k);
  return y;
}

/* number of items fully visible from the top (at least 1 when there are items) */
static int vis_count(const UWnd *w, const LbX *x) {
  int ch = cheight(w), n = 0, y = 0;
  if (!is_var(w)) {
    n = ch / item_h(w, x, 0);
    return n < 1 ? 1 : n;
  }
  for (unsigned i = x->top; i < x->count; i++) {
    int ih = item_h(w, x, i);
    if (y + ih > ch) break;
    y += ih;
    n++;
  }
  return n < 1 ? 1 : n;
}

static int max_top(const UWnd *w, const LbX *x) {
  int ch = cheight(w), y = 0, n = 0;
  if (!x->count) return 0;
  if (!is_var(w)) {
    int m = (int)x->count - vis_count(w, x);
    return m < 0 ? 0 : m;
  }
  for (int i = (int)x->count - 1; i >= 0; i--) {
    int ih = item_h(w, x, (unsigned)i);
    if (y + ih > ch) break;
    y += ih;
    n++;
  }
  return (int)x->count - (n ? n : 1);
}

static RECT16 item_rect(const UWnd *w, const LbX *x, unsigned i) {
  int y = item_y(w, x, i);
  return r_make(0, y, cwidth(w), y + item_h(w, x, i));
}

/* index of the item at client y; -1 when outside the items */
static int item_at(const UWnd *w, const LbX *x, int y) {
  if (y < 0) return -1;
  for (unsigned i = x->top; i < x->count; i++) {
    int top = item_y(w, x, i);
    if (y >= top && y < top + item_h(w, x, i)) return (int)i;
    if (top > y) break;
  }
  return -1;
}

static bool can_draw(HWND16 h, const LbX *x) {
  return user_wnd(h) && x && !(x->flags & LF_NOREDRAW) && IsWindowVisible(h);
}

static void sb_update(HWND16 h) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  bool vis;
  if (!w || !x || !(x->flags & LF_VSCROLL)) return;
  vis = can_draw(h, x);
  if (w->style & LBS_DISABLENOSCROLL) {
    /* This style keeps the bar visible, disabled, when the list fits; SetScrollRange would hide it
     * for an empty range, so the empty range is stored directly (UNVERIFIED for Windows 3.1). */
    UScroll *s = user_sb_get(h, SB_VERT);
    int mt = (int)max_top(w, x);
    if (s && mt <= 0) {
      s->min = s->max = s->pos = 0;
      s->arrows = ESB_DISABLE_BOTH;
      if (!(w->style & WS_VSCROLL)) ShowScrollBar(h, SB_VERT, 1);
      return;
    }
    if (s) s->arrows = 0;
  }
  SetScrollRange(h, SB_VERT, 0, (int16_t)max_top(w, x), 0);
  SetScrollPos(h, SB_VERT, (int16_t)x->top, vis ? 1 : 0);
}

/* ---- drawing ---- */

static int avg_char_w(HDC16 dc) {
  return (user_ctl_text_width(dc, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 52) + 26) / 52;
}

/* x offset of the k-th tab stop (k from 0) in pixels */
static int tab_px(const LbX *x, unsigned k, int avg) {
  int du;
  if (x->ntabs == 0) du = 32 * ((int)k + 1);
  else if (x->ntabs == 1) du = x->tabs[0] * ((int)k + 1);
  else if (k < x->ntabs) du = x->tabs[k];
  else du = x->tabs[x->ntabs - 1] + 32 * ((int)(k - x->ntabs) + 1);
  return LB_LM + du * avg / 4;
}

static void draw_text(const UWnd *w, const LbX *x, HDC16 dc, const char *s, int y) {
  if (!(w->style & LBS_USETABSTOPS) || !strchr(s, '\t')) {
    user_ctl_text_out(dc, LB_LM, y, s, (int)strlen(s));
    return;
  }
  {
    int avg = avg_char_w(dc), px = LB_LM;
    unsigned k = 0;
    const char *seg = s;
    for (;;) {
      const char *e = strchr(seg, '\t');
      int n = e ? (int)(e - seg) : (int)strlen(seg);
      user_ctl_text_out(dc, px, y, seg, n);
      if (!e) break;
      px = tab_px(x, k++, avg);
      seg = e + 1;
    }
  }
}

static void draw_item(HWND16 h, HDC16 dc, unsigned idx, unsigned action, bool empty_focus) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  RECT16 r;
  bool sel, focus, enabled;
  if (!w || !x) return;
  if (empty_focus) r = r_make(0, 0, cwidth(w), item_h(w, x, 0));
  else r = item_rect(w, x, idx);
  sel = !empty_focus && its(x)[idx].sel;
  focus = (x->flags & LF_FOCUS) && !(x->flags & LF_CARETOFF) && (empty_focus || (int)idx == x->caret);
  enabled = !(w->style & WS_DISABLED);
  if (is_od(w)) {
    OdCtx c;
    uint16_t hs = 0;
    FARPTR p;
    if (!od_ctx(h, w, x, &c)) return;
    p = user_scratch(sizeof(DRAWITEMSTRUCT16), &hs);
    if (!p) return;
    {
      DRAWITEMSTRUCT16 *d = LIN(p);
      d->CtlType = c.type;
      d->CtlID = c.id;
      d->itemID = empty_focus ? 0xFFFF : (uint16_t)idx;
      d->itemAction = (uint16_t)action;
      d->itemState = (uint16_t)((sel ? ODS_SELECTED : 0) | (focus ? ODS_FOCUS : 0) | (enabled ? 0 : ODS_DISABLED));
      d->hwndItem = c.item;
      d->hDC = dc;
      d->rcItem = r;
      d->itemData = empty_focus ? 0 : its(x)[idx].data;
    }
    SendMessage(c.target, WM_DRAWITEM, c.id, p);
    user_scratch_free(hs);
    return;
  }
  if (empty_focus) return;
  {
    uint16_t brush = user_ctl_color(h, dc, CTLCOLOR_LISTBOX);
    int th = user_ctl_text_height(dc), y = r.top + (r.bottom - r.top - th) / 2;
    uint32_t textcol = enabled ? COL(8) : COL(17);
    user_ctl_fill_brush(dc, r, brush);
    if (sel) {
      user_fill(dc, r, COL(13));
      textcol = COL(14);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textcol);
    draw_text(w, x, dc, str_of(&its(x)[idx]), y);
    if (focus) user_ctl_focus_rect(dc, r, sel ? COL(14) : COL(8));
  }
}

static void list_draw(HWND16 h, HDC16 dc) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  RECT16 rc;
  uint16_t oldfont, brush;
  if (!w || !x) return;
  rc = r_make(0, 0, cwidth(w), cheight(w));
  brush = user_ctl_color(h, dc, CTLCOLOR_LISTBOX);
  user_ctl_fill_brush(dc, rc, brush);
  oldfont = SelectObject(dc, user_ctl_pick_font(x->font));
  if (!x->count) {
    if ((x->flags & LF_FOCUS) && is_od(w)) draw_item(h, dc, 0, ODA_FOCUS, true);
  }
  for (unsigned i = x->top; i < x->count; i++) {
    RECT16 r = item_rect(w, x, i);
    if (r.top >= rc.bottom) break;
    draw_item(h, dc, i, ODA_DRAWENTIRE, false);
    if (!user_wnd(h)) break;
  }
  SelectObject(dc, oldfont);
}

static void redraw_all(HWND16 h) {
  HDC16 dc;
  const LbX *x = lx(h);
  if (!can_draw(h, x)) return;
  dc = GetDC(h);
  if (!dc) return;
  list_draw(h, dc);
  ReleaseDC(h, dc);
}

/* Redraws one item (selection or focus changed). */
static void redraw_item(HWND16 h, int idx, unsigned action) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  HDC16 dc;
  uint16_t oldfont;
  RECT16 r;
  if (!can_draw(h, x) || idx < 0 || idx >= x->count) return;
  r = item_rect(w, x, (unsigned)idx);
  if (r.bottom <= 0 || r.top >= cheight(w)) return;
  dc = GetDC(h);
  if (!dc) return;
  oldfont = SelectObject(dc, user_ctl_pick_font(x->font));
  IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
  draw_item(h, dc, (unsigned)idx, action, false);
  SelectObject(dc, oldfont);
  ReleaseDC(h, dc);
}

/* ---- size and font ---- */

static int font_height(HWND16 h, const LbX *x) {
  HDC16 dc = GetDC(h);
  uint16_t old;
  int th = 13;
  if (!dc) return th;
  old = SelectObject(dc, user_ctl_pick_font(x->font));
  th = user_ctl_text_height(dc);
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return th < 1 ? 1 : th;
}

/* Without LBS_NOINTEGRALHEIGHT the window is cut to a whole number of items (Wine
 * LISTBOX_UpdateSize; UNVERIFIED for Windows 3.1). */
static void fit_height(HWND16 h) {
  UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  int ch, ih, n, delta;
  if (!w || !x || (x->flags & LF_SIZING) || (w->style & LBS_NOINTEGRALHEIGHT) || is_var(w)) return;
  ch = cheight(w);
  ih = item_h(w, x, 0);
  n = ch / ih;
  if (n < 1) return;
  delta = ch - n * ih;
  if (delta <= 0) return;
  x->flags |= LF_SIZING;
  SetWindowPos(h, 0, 0, 0, (int16_t)(w->wrect.right - w->wrect.left), (int16_t)(w->wrect.bottom - w->wrect.top - delta),
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (lx(h)) lx(h)->flags &= ~(uint16_t)LF_SIZING;
}

/* ---- scrolling ---- */

static void set_top(HWND16 h, int top) {
  const UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  int mt;
  if (!w || !x) return;
  mt = max_top(w, x);
  if (top > mt) top = mt;
  if (top < 0) top = 0;
  if (top == x->top) {
    sb_update(h);
    return;
  }
  x->top = (uint16_t)top;
  sb_update(h);
  redraw_all(h);
}

/* total height of items from..to inclusive */
static int span_h(const UWnd *w, const LbX *x, unsigned from, unsigned to) {
  int y = 0;
  for (unsigned k = from; k <= to; k++) y += item_h(w, x, k);
  return y;
}

static void make_visible(HWND16 h, int idx) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  int top;
  if (!w || !x || idx < 0 || idx >= x->count) return;
  if (idx < x->top) {
    set_top(h, idx);
    return;
  }
  top = x->top; /* the smallest top that shows the whole item */
  while (top < idx && span_h(w, x, (unsigned)top, (unsigned)idx) > cheight(w)) top++;
  set_top(h, top);
}

/* ---- the item array ---- */

static bool grow(LbX *x) {
  unsigned cap;
  LbItem *n;
  if (x->count < x->cap) return true;
  cap = x->cap ? x->cap * 2u : 16u;
  if (cap > 0x7FFF) cap = 0x7FFF;
  if (cap <= x->count) return false;
  n = rt_alloc(cap * sizeof(LbItem));
  if (!n) return false;
  memset(n, 0, cap * sizeof(LbItem));
  if (x->items) {
    memcpy(n, its(x), x->count * sizeof(LbItem));
    rt_free(its(x));
  }
  x->items = mem_off(n);
  x->cap = (uint16_t)cap;
  return true;
}

static void free_item_str(LbItem *i) {
  if (i->str) rt_free(mem_arena() + i->str);
  i->str = 0;
}

void user_listbox_release(HWND16 h) {
  LbX *x = lx(h);
  if (!x) return;
  for (unsigned i = 0; i < x->count; i++) free_item_str(&its(x)[i]);
  if (x->items) rt_free(its(x));
  x->items = 0;
  x->count = x->cap = 0;
}

static void reset_content(HWND16 h, bool send_delete_msgs) {
  LbX *x = lx(h);
  const UWnd *w = user_wnd(h);
  if (!x || !w) return;
  if (send_delete_msgs && is_od(w)) {
    unsigned n = x->count;
    for (unsigned i = 0; i < n; i++) {
      if (!lx(h)) return;
      send_delete(h, i, its(lx(h))[i].data);
    }
    x = lx(h);
    if (!x) return;
  }
  user_listbox_release(h);
  x->cur = x->caret = x->anchor = -1;
  x->top = 0;
}

/* comparison of a new item (str / data) with the item at position i for a sorted list */
static int compare_new(HWND16 h, const UWnd *w, const LbX *x, const char *str, uint32_t data, unsigned i) {
  if (has_strings(w)) return strcasecmp(str, str_of(&its(x)[i]));
  {
    OdCtx c;
    uint16_t hs = 0;
    FARPTR p;
    int r = 0;
    if (!od_ctx(h, w, x, &c)) return 1;
    p = user_scratch(sizeof(COMPAREITEMSTRUCT16), &hs);
    if (!p) return 1;
    {
      COMPAREITEMSTRUCT16 *cs = LIN(p);
      cs->CtlType = c.type;
      cs->CtlID = c.id;
      cs->hwndItem = c.item;
      cs->itemID1 = 0xFFFF;
      cs->itemData1 = data;
      cs->itemID2 = (uint16_t)i;
      cs->itemData2 = its(x)[i].data;
    }
    r = (int16_t)(SendMessage(c.target, WM_COMPAREITEM, c.id, p) & 0xFFFF);
    user_scratch_free(hs);
    return r;
  }
}

/* Inserts at idx (idx == count appends). Returns the index or LB_ERR / LB_ERRSPACE. */
static uint32_t insert_item(HWND16 h, int idx, const char *str, unsigned slen, uint32_t data, bool sorted) {
  const UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  LbItem *it;
  char *copy = NULL;
  if (!w || !x) return (uint32_t)LB_ERR;
  if (has_strings(w)) {
    copy = rt_alloc(slen + 1u);
    if (!copy) return (uint32_t)LB_ERRSPACE;
    memcpy(copy, str, slen);
    copy[slen] = 0;
  }
  if (sorted && (w->style & LBS_SORT)) {
    unsigned lo = 0, hi = x->count;
    while (lo < hi) {
      unsigned mid = (lo + hi) / 2;
      int c = compare_new(h, w, x, copy, data, mid);
      if (!user_wnd(h) || !lx(h)) return (uint32_t)LB_ERR;
      if (c > 0) lo = mid + 1;
      else hi = mid;
    }
    idx = (int)lo;
  }
  x = lx(h);
  if (!grow(x)) {
    if (copy) rt_free(copy);
    return (uint32_t)LB_ERRSPACE;
  }
  it = its(x);
  memmove(&it[idx + 1], &it[idx], (x->count - (unsigned)idx) * sizeof(LbItem));
  memset(&it[idx], 0, sizeof it[idx]);
  it[idx].str = copy ? mem_off(copy) : 0;
  it[idx].data = data;
  if (is_var(w)) it[idx].h = send_measure(h, (unsigned)idx, data, x->itemh);
  x->count++;
  if (x->cur >= idx) x->cur++;
  if (x->caret >= idx) x->caret++;
  if (x->anchor >= idx) x->anchor++;
  sb_update(h);
  if (idx < x->top + vis_count(w, x)) redraw_all(h);
  return (uint32_t)idx;
}

static uint32_t delete_item(HWND16 h, unsigned idx) {
  const UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  LbItem *it;
  if (!w || !x || idx >= x->count) return (uint32_t)LB_ERR;
  send_delete(h, idx, its(x)[idx].data);
  x = lx(h);
  if (!x || idx >= x->count) return (uint32_t)LB_ERR;
  it = its(x);
  free_item_str(&it[idx]);
  memmove(&it[idx], &it[idx + 1], (x->count - idx - 1u) * sizeof(LbItem));
  memset(&it[x->count - 1], 0, sizeof it[0]);
  x->count--;
  if (x->cur == (int)idx) x->cur = -1;
  else if (x->cur > (int)idx) x->cur--;
  if (x->caret > (int)idx) x->caret--;
  if (x->caret >= x->count) x->caret = (int16_t)x->count - 1;
  if (x->anchor > (int)idx) x->anchor--;
  if (x->anchor >= x->count) x->anchor = (int16_t)x->count - 1;
  if (x->top > max_top(w, x)) x->top = (uint16_t)max_top(w, x);
  sb_update(h);
  redraw_all(h);
  return x->count;
}

/* ---- notification ---- */

static void notify(HWND16 h, uint16_t code) {
  const UWnd *w = user_wnd(h);
  const LbX *x = lx(h);
  if (!w || !x) return;
  if (x->combo) user_ctl_notify_to(x->combo, h, w->id, code);
  else if (w->style & LBS_NOTIFY) user_ctl_notify(h, code);
}

/* ---- selection ---- */

static void set_cur(HWND16 h, int idx) { /* single-selection list */
  LbX *x = lx(h);
  int old;
  if (!x) return;
  old = x->cur;
  if (old == idx) {
    x->caret = (int16_t)idx;
    return;
  }
  if (old >= 0 && old < x->count) its(x)[old].sel = 0;
  x->cur = (int16_t)idx;
  x->caret = (int16_t)(idx >= 0 ? idx : x->caret);
  if (idx >= 0) its(x)[idx].sel = 1;
  redraw_item(h, old, ODA_SELECT);
  redraw_item(h, idx, ODA_SELECT);
}

static void set_caret_index(HWND16 h, int idx) {
  LbX *x = lx(h);
  int old;
  if (!x) return;
  old = x->caret;
  x->caret = (int16_t)idx;
  if (old != idx && (x->flags & LF_FOCUS)) {
    redraw_item(h, old, ODA_FOCUS);
    redraw_item(h, idx, ODA_FOCUS);
  }
}

static void set_item_sel(HWND16 h, int idx, bool on) {
  LbX *x = lx(h);
  if (!x || idx < 0 || idx >= x->count) return;
  if (its(x)[idx].sel == (on ? 1 : 0)) return;
  its(x)[idx].sel = on ? 1 : 0;
  redraw_item(h, idx, ODA_SELECT);
}

static void select_range(HWND16 h, int a, int b, bool on) {
  if (a > b) {
    int t = a;
    a = b;
    b = t;
  }
  for (int i = a; i <= b; i++) set_item_sel(h, i, on);
}

/* Moves the caret and applies the selection rule of the list type for a click or a key; returns true
 * when the selection changed. */
static bool pick(HWND16 h, int idx, bool shift, bool ctrl, bool toggle_multi) {
  const UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  bool changed = false;
  if (!w || !x || idx < 0 || idx >= x->count) return false;
  if (!is_multi(w)) {
    changed = x->cur != idx;
    set_cur(h, idx);
    set_caret_index(h, idx);
  } else if (!is_ext(w)) { /* LBS_MULTIPLESEL: a click toggles */
    if (toggle_multi) {
      set_item_sel(h, idx, !its(x)[idx].sel);
      changed = true;
    }
    set_caret_index(h, idx);
    lx(h)->anchor = (int16_t)idx;
  } else if (shift) { /* extended: range from the anchor */
    int an = x->anchor >= 0 ? x->anchor : idx;
    if (!ctrl)
      for (unsigned i = 0; i < x->count; i++)
        if (its(x)[i].sel && ((int)i < (an < idx ? an : idx) || (int)i > (an > idx ? an : idx))) set_item_sel(h, (int)i, false);
    select_range(h, an, idx, true);
    set_caret_index(h, idx);
    changed = true;
  } else if (ctrl) { /* toggle one item, the anchor moves */
    set_item_sel(h, idx, !its(x)[idx].sel);
    set_caret_index(h, idx);
    lx(h)->anchor = (int16_t)idx;
    changed = true;
  } else { /* plain: only this item */
    for (unsigned i = 0; i < x->count; i++)
      if ((int)i != idx && its(x)[i].sel) set_item_sel(h, (int)i, false);
    set_item_sel(h, idx, true);
    set_caret_index(h, idx);
    lx(h)->anchor = (int16_t)idx;
    changed = true;
  }
  make_visible(h, idx);
  return changed;
}

/* ---- searching ---- */

static int find_string(const UWnd *w, const LbX *x, int start, const char *s, unsigned slen, bool exact, uint32_t data) {
  if (!x->count) return -1;
  for (unsigned k = 1; k <= x->count; k++) {
    unsigned i = (unsigned)((start + (int)k + (int)x->count) % (int)x->count);
    if (start < 0) i = k - 1;
    if (has_strings(w)) {
      const char *t = str_of(&its(x)[i]);
      if (exact ? strcasecmp(t, s) == 0 : strncasecmp(t, s, slen) == 0) return (int)i;
    } else if (its(x)[i].data == data) {
      return (int)i;
    }
  }
  return -1;
}

/* ---- LB_DIR ---- */

static uint32_t add_name(HWND16 h, const char *name, uint32_t last) {
  uint32_t r = insert_item(h, (int)lx(h)->count, name, (unsigned)strlen(name), 0, true);
  return (int32_t)r < 0 ? last : r;
}

static uint32_t list_dir(HWND16 h, uint16_t attr, const char *spec) {
  VfsSearch se;
  VfsEnt e;
  char full[VFS_PATH_MAX], dirspec[VFS_PATH_MAX + 4];
  uint32_t last = (uint32_t)LB_ERR;
  bool excl = (attr & DDL_EXCLUSIVE) != 0;
  unsigned want = attr & (DDL_READONLY | DDL_HIDDEN | DDL_SYSTEM | DDL_DIRECTORY | DDL_ARCHIVE);
  int rc;
  if (vfs_resolve(spec, full, 1) != 0) return (uint32_t)LB_ERR;
  /* files (and, under DDL_EXCLUSIVE with DDL_DIRECTORY, nothing of the plain ones) */
  rc = vfs_find_first(spec, (uint8_t)(want & (DDL_HIDDEN | DDL_SYSTEM)), &se, &e);
  if (rc == 3) return (uint32_t)LB_ERR;
  while (rc == 0) {
    unsigned a = e.attr;
    bool take;
    if (a & VFS_ATTR_DIR) take = false;
    else if (excl) take = want ? (a & want) != 0 : true;
    else take = true;
    if (take) last = add_name(h, e.name, last);
    rc = vfs_find_next(&se, &e);
  }
  if (attr & DDL_DIRECTORY) {
    const char *slash = strrchr(spec, '\\');
    const char *slash2 = strrchr(spec, '/');
    const char *colon = strrchr(spec, ':');
    size_t cut = 0;
    if (slash2 && (!slash || slash2 > slash)) slash = slash2;
    if (slash) cut = (size_t)(slash - spec) + 1;
    else if (colon) cut = (size_t)(colon - spec) + 1;
    memcpy(dirspec, spec, cut);
    strcpy(dirspec + cut, "*");
    rc = vfs_find_first(dirspec, VFS_ATTR_DIR, &se, &e);
    while (rc == 0) {
      if (e.attr & VFS_ATTR_DIR) {
        char b[20];
        snprintf(b, sizeof b, "[%s]", e.name);
        last = add_name(h, b, last);
      }
      rc = vfs_find_next(&se, &e);
    }
    {
      size_t n = strlen(full);
      while (n > 0 && full[n - 1] != '\\') n--; /* up to and including the last separator */
      if (n > 3) last = add_name(h, "[..]", last); /* not the root: "C:\" */
    }
  }
  if (attr & DDL_DRIVES) last = add_name(h, "[-c-]", last);
  if ((int32_t)last < 0 && lx(h) && lx(h)->count) last = (uint32_t)lx(h)->count - 1u;
  return last;
}

/* ---- the message handlers ---- */

static uint32_t lb_message(HWND16 h, UWnd *w, LbX *x, uint16_t msg, uint16_t wp, uint32_t lp) {
  int idx = (int16_t)wp;
  switch (msg) {
    case LB_ADDSTRING:
    case LB_INSERTSTRING: {
      unsigned n = 0;
      const char *s = "";
      int at = msg == LB_ADDSTRING ? (int)x->count : idx;
      if (msg == LB_INSERTSTRING && idx == -1) at = (int)x->count;
      if (at < 0 || at > (int)x->count) return (uint32_t)LB_ERR;
      if (has_strings(w)) {
        s = user_ctl_far_str((FARPTR)lp, 0xFFFF, &n);
        if (!s) return (uint32_t)LB_ERR;
        return insert_item(h, at, s, n, 0, msg == LB_ADDSTRING);
      }
      return insert_item(h, at, "", 0, lp, msg == LB_ADDSTRING);
    }
    case LB_DELETESTRING: return delete_item(h, wp);
    case LB_RESETCONTENT:
      reset_content(h, true);
      sb_update(h);
      redraw_all(h);
      return 0;
    case LB_SELITEMRANGEEX: {
      int a = (int16_t)wp, b = (int16_t)(lp & 0xFFFF);
      if (!is_multi(w) || x->count == 0) return (uint32_t)LB_ERR;
      if (a < 0) a = 0;
      if (b >= x->count) b = x->count - 1;
      if (a <= b) select_range(h, a, b, true);
      else select_range(h, b, a, false);
      return 0;
    }
    case LB_SETSEL: {
      int i = (int16_t)(lp & 0xFFFF);
      if (!is_multi(w)) return (uint32_t)LB_ERR;
      if (i == -1) {
        for (unsigned k = 0; k < x->count; k++) set_item_sel(h, (int)k, wp != 0);
      } else if (i >= 0 && i < x->count) {
        set_item_sel(h, i, wp != 0);
      } else {
        return (uint32_t)LB_ERR;
      }
      return 0;
    }
    case LB_SETCURSEL:
      if (is_multi(w)) return (uint32_t)LB_ERR;
      if (idx == -1) {
        set_cur(h, -1);
        return (uint32_t)LB_ERR;
      }
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      set_cur(h, idx);
      set_caret_index(h, idx);
      make_visible(h, idx);
      return (uint32_t)idx;
    case LB_GETSEL:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      return its(x)[idx].sel ? 1 : 0;
    case LB_GETCURSEL:
      if (is_multi(w)) return x->count ? (uint32_t)(int32_t)x->caret : (uint32_t)LB_ERR;
      return x->cur >= 0 ? (uint32_t)x->cur : (uint32_t)LB_ERR;
    case LB_GETTEXT:
    case LB_GETTEXTLEN: {
      unsigned n;
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      if (has_strings(w)) {
        const char *s = str_of(&its(x)[idx]);
        n = (unsigned)strlen(s);
        if (msg == LB_GETTEXT) {
          if (!lp || !mem_check(lp, n + 1u)) return (uint32_t)LB_ERR;
          memcpy(LIN(lp), s, n + 1u);
        }
        return n;
      }
      if (msg == LB_GETTEXT) {
        if (!lp || !mem_check(lp, 4)) return (uint32_t)LB_ERR;
        memcpy(LIN(lp), &its(x)[idx].data, 4);
      }
      return 4;
    }
    case LB_GETCOUNT: return x->count;
    case LB_SELECTSTRING: {
      unsigned n = 0;
      const char *s = has_strings(w) ? user_ctl_far_str((FARPTR)lp, 0xFFFF, &n) : "";
      int f;
      if (!s) return (uint32_t)LB_ERR;
      f = find_string(w, x, idx, s, n, false, lp);
      if (f < 0) return (uint32_t)LB_ERR;
      if (is_multi(w)) {
        set_item_sel(h, f, true);
        set_caret_index(h, f);
      } else {
        set_cur(h, f);
        set_caret_index(h, f);
      }
      make_visible(h, f);
      return (uint32_t)f;
    }
    case LB_DIR: {
      unsigned n = 0;
      const char *s = user_ctl_far_str((FARPTR)lp, VFS_PATH_MAX - 1u, &n);
      char spec[VFS_PATH_MAX];
      if (!s) return (uint32_t)LB_ERR;
      memcpy(spec, s, n);
      spec[n] = 0;
      return list_dir(h, wp, spec);
    }
    case LB_GETTOPINDEX: return x->top;
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT: {
      unsigned n = 0;
      const char *s = has_strings(w) ? user_ctl_far_str((FARPTR)lp, 0xFFFF, &n) : "";
      int f;
      if (!s) return (uint32_t)LB_ERR;
      f = find_string(w, x, idx, s, n, msg == LB_FINDSTRINGEXACT, lp);
      return f < 0 ? (uint32_t)LB_ERR : (uint32_t)f;
    }
    case LB_GETSELCOUNT: {
      unsigned n = 0;
      if (!is_multi(w)) return (uint32_t)LB_ERR;
      for (unsigned i = 0; i < x->count; i++) n += its(x)[i].sel;
      return n;
    }
    case LB_GETSELITEMS: {
      unsigned n = 0;
      if (!is_multi(w)) return (uint32_t)LB_ERR;
      if (wp && (!lp || !mem_check(lp, (uint32_t)wp * 2u))) return (uint32_t)LB_ERR;
      for (unsigned i = 0; i < x->count && n < wp; i++)
        if (its(x)[i].sel) ((int16_t *)LIN(lp))[n++] = (int16_t)i;
      return n;
    }
    case LB_SETTABSTOPS: {
      unsigned n = wp > LB_MAXTABS ? LB_MAXTABS : wp;
      if (n && (!lp || !mem_check(lp, n * 2u))) return 0;
      x->ntabs = (uint16_t)n;
      for (unsigned i = 0; i < n; i++) x->tabs[i] = (uint16_t)((int16_t *)LIN(lp))[i];
      redraw_all(h);
      return 1;
    }
    case LB_GETHORIZONTALEXTENT: return x->hext;
    case LB_SETHORIZONTALEXTENT:
      x->hext = wp;
      return 0;
    case LB_SETCOLUMNWIDTH:
      x->colw = wp;
      return 0;
    case LB_ADDFILE: return (uint32_t)LB_ERR; /* not implemented */
    case LB_SETTOPINDEX:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      set_top(h, idx);
      return 0;
    case LB_GETITEMRECT: {
      RECT16 r;
      if (idx < 0 || idx >= x->count || !lp || !mem_check(lp, sizeof r)) return (uint32_t)LB_ERR;
      r = item_rect(w, x, (unsigned)idx);
      *(RECT16 *)LIN(lp) = r;
      return 0;
    }
    case LB_GETITEMDATA:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      return its(x)[idx].data;
    case LB_SETITEMDATA:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      its(x)[idx].data = lp;
      return 0;
    case LB_SELITEMRANGE: {
      int a = (int16_t)(lp & 0xFFFF), b = (int16_t)(lp >> 16);
      if (!is_multi(w)) return (uint32_t)LB_ERR;
      if (a < 0) a = 0;
      if (b >= x->count) b = x->count - 1;
      if (a <= b) select_range(h, a, b, wp != 0);
      return 0;
    }
    case LB_SETANCHORINDEX:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      x->anchor = (int16_t)idx;
      return 0;
    case LB_GETANCHORINDEX: return (uint32_t)(int32_t)x->anchor;
    case LB_SETCARETINDEX:
      if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
      set_caret_index(h, idx);
      make_visible(h, idx);
      return 0;
    case LB_GETCARETINDEX: return (uint32_t)(int32_t)x->caret;
    case LB_SETITEMHEIGHT: {
      unsigned hh = lp & 0xFFFF;
      if (hh < 1 || hh > 255) return (uint32_t)LB_ERR;
      if (is_var(w)) {
        if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
        its(x)[idx].h = (uint16_t)hh;
      } else {
        x->itemh = (uint16_t)hh;
        fit_height(h);
      }
      sb_update(h);
      redraw_all(h);
      return 0;
    }
    case LB_GETITEMHEIGHT:
      if (is_var(w)) {
        if (idx < 0 || idx >= x->count) return (uint32_t)LB_ERR;
        return item_h(w, x, (unsigned)idx);
      }
      return x->itemh;
    case LB_CARETON:
      x->flags &= ~(uint16_t)LF_CARETOFF;
      redraw_item(h, x->caret, ODA_FOCUS);
      return 0;
    case LB_CARETOFF:
      x->flags |= LF_CARETOFF;
      redraw_item(h, x->caret, ODA_FOCUS);
      return 0;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}

static void vscroll(HWND16 h, const UWnd *w, const LbX *x, uint16_t code, uint16_t pos) {
  int top = x->top, page = vis_count(w, x) - 1;
  if (page < 1) page = 1;
  switch (code) {
    case SB_LINEUP: top--; break;
    case SB_LINEDOWN: top++; break;
    case SB_PAGEUP: top -= page; break;
    case SB_PAGEDOWN: top += page; break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: top = (int16_t)pos; break;
    case SB_TOP: top = 0; break;
    case SB_BOTTOM: top = max_top(w, x); break;
    default: return;
  }
  set_top(h, top);
}

static int hit_y(uint32_t lp) { return (int16_t)(lp >> 16); }

static bool in_client(const UWnd *w, uint32_t lp) {
  int px = (int16_t)(lp & 0xFFFF), py = (int16_t)(lp >> 16);
  return px >= 0 && py >= 0 && px < cwidth(w) && py < cheight(w);
}

static void key_down(HWND16 h, UWnd *w, LbX *x, uint16_t vk) {
  bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
  int cur, ni, page;
  if (!x->count) return;
  cur = x->caret >= 0 ? x->caret : (x->cur >= 0 ? x->cur : 0);
  page = vis_count(w, x) - 1;
  if (page < 1) page = 1;
  switch (vk) {
    case VK_UP: ni = cur - 1; break;
    case VK_DOWN: ni = cur + 1; break;
    case VK_PRIOR: ni = cur - page; break;
    case VK_NEXT: ni = cur + page; break;
    case VK_HOME: ni = 0; break;
    case VK_END: ni = (int)x->count - 1; break;
    case VK_SPACE:
      if (is_multi(w)) {
        bool ch;
        if (is_ext(w)) ch = pick(h, cur, shift, ctrl, true);
        else ch = pick(h, cur, false, false, true);
        if (ch && !x->combo) notify(h, LBN_SELCHANGE);
      }
      return;
    default: return;
  }
  if (ni < 0) ni = 0;
  if (ni >= x->count) ni = (int)x->count - 1;
  if (is_multi(w) && !is_ext(w)) { /* LBS_MULTIPLESEL: the arrows move only the caret */
    set_caret_index(h, ni);
    make_visible(h, ni);
    return;
  }
  if (is_ext(w) && ctrl && !shift) { /* Ctrl+arrow moves only the caret */
    set_caret_index(h, ni);
    make_visible(h, ni);
    return;
  }
  {
    int old = is_multi(w) ? -2 : x->cur;
    bool ch = pick(h, ni, shift, false, false);
    if (!user_wnd(h)) return;
    if (ch && !x->combo && (is_multi(w) || old != ni)) notify(h, LBN_SELCHANGE);
  }
}

static void char_in(HWND16 h, UWnd *w, LbX *x, uint16_t ch) {
  int start;
  char c;
  if (ch <= ' ' || !x->count || !has_strings(w)) return;
  c = (char)ch;
  start = x->caret >= 0 ? x->caret : -1;
  for (unsigned k = 1; k <= x->count; k++) {
    unsigned i = (unsigned)((start + (int)k + (int)x->count) % (int)x->count);
    if (start < 0) i = k - 1;
    if (strncasecmp(str_of(&its(x)[i]), &c, 1) == 0) {
      int old = x->cur;
      if (is_multi(w) && !is_ext(w)) {
        set_caret_index(h, (int)i);
        make_visible(h, (int)i);
        return;
      }
      pick(h, (int)i, false, false, false);
      if (user_wnd(h) && !x->combo && (is_multi(w) || old != (int)i)) notify(h, LBN_SELCHANGE);
      return;
    }
  }
}

uint32_t user_listbox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  LbX *x = lx(h);
  if (!w || !x) return user_def_window_proc(h, msg, wp, lp);
  switch (msg) {
    case WM_CREATE:
      x->cur = x->caret = x->anchor = -1;
      if ((w->style & LBS_COMBOBOX) && lp && mem_check(lp, sizeof(CREATESTRUCT16))) { /* the drop-down list of a combo box */
        HWND16 cb = (HWND16)((const CREATESTRUCT16 *)LIN(lp))->lpCreateParams;
        if (user_wnd(cb) && cb != user_desktop()) x->combo = cb;
      }
      if (w->style & WS_VSCROLL) x->flags |= LF_VSCROLL;
      x->itemh = (uint16_t)font_height(h, x);
      if (w->style & LBS_OWNERDRAWFIXED) x->itemh = send_measure(h, 0, 0, x->itemh);
      if (!user_wnd(h)) return (uint32_t)-1;
      sb_update(h);
      return 0;
    case WM_NCDESTROY:
      user_listbox_release(h);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_DESTROY:
      reset_content(h, true);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      uint16_t hp = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp);
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc) list_draw(h, dc);
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_SIZE:
      fit_height(h);
      if (user_wnd(h)) sb_update(h);
      return 0;
    case WM_ENABLE:
    case WM_SYSCOLORCHANGE:
      redraw_all(h);
      return 0;
    case WM_SETREDRAW:
      if (wp) {
        x->flags &= ~(uint16_t)LF_NOREDRAW;
        InvalidateRect(h, 0, 1);
      } else {
        x->flags |= LF_NOREDRAW;
      }
      return 0;
    case WM_SETFONT:
      x->font = wp;
      if (!is_od(w)) {
        x->itemh = (uint16_t)font_height(h, x);
        fit_height(h);
      }
      sb_update(h);
      if (lp & 0xFFFF) InvalidateRect(h, 0, 1);
      return 0;
    case WM_GETFONT: return x->font;
    case WM_VSCROLL:
      vscroll(h, w, x, wp, (uint16_t)(lp & 0xFFFF));
      return 0;
    case WM_SETFOCUS:
      x->flags |= LF_FOCUS;
      if (x->caret < 0 && x->count && !is_multi(w) && x->cur < 0) x->caret = 0;
      if (x->count) redraw_item(h, x->caret >= 0 ? x->caret : 0, ODA_FOCUS);
      else if (is_od(w)) redraw_all(h);
      notify(h, LBN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      x->flags &= ~(uint16_t)LF_FOCUS;
      if (x->flags & LF_CAPTURED) {
        x->flags &= ~(uint16_t)LF_CAPTURED;
        if (!x->combo && GetCapture() == h) ReleaseCapture();
      }
      if (x->count) redraw_item(h, x->caret >= 0 ? x->caret : 0, ODA_FOCUS);
      notify(h, LBN_KILLFOCUS);
      return 0;
    case WM_LBUTTONDOWN: {
      int idx;
      if (x->combo && !in_client(w, lp)) { /* a click outside the dropped list closes it */
        notify(h, LBN_SELCANCEL);
        return 0;
      }
      if (!x->combo) SetFocus(h);
      if (!user_wnd(h)) return 0;
      idx = item_at(w, x, hit_y(lp));
      if (idx < 0) return 0;
      if (!x->combo) SetCapture(h);
      x->flags |= LF_CAPTURED;
      pick(h, idx, (wp & MK_SHIFT) != 0, (wp & MK_CONTROL) != 0, true);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      int idx = item_at(w, x, hit_y(lp));
      if (!x->combo) SetFocus(h);
      if (idx >= 0 && !x->combo) pick(h, idx, false, false, false);
      if (user_wnd(h)) notify(h, LBN_DBLCLK);
      return 0;
    }
    case WM_MOUSEMOVE: {
      int idx, y = hit_y(lp);
      if (x->combo && !(wp & MK_LBUTTON)) { /* hover highlight in a dropped list */
        idx = in_client(w, lp) ? item_at(w, x, y) : -1;
        if (idx >= 0 && x->cur != idx) {
          set_cur(h, idx);
          set_caret_index(h, idx);
        }
        return 0;
      }
      if (!(x->flags & LF_CAPTURED) || !(wp & MK_LBUTTON)) return 0;
      if (y < 0) { /* above: scroll up one line per move */
        set_top(h, x->top - 1);
        idx = x->top;
      } else if (y >= cheight(w)) {
        set_top(h, x->top + 1);
        idx = x->top + vis_count(w, x) - 1;
      } else {
        idx = item_at(w, x, y);
      }
      if (idx >= 0 && idx < x->count && !(is_multi(w) && !is_ext(w))) pick(h, idx, is_ext(w), false, false);
      return 0;
    }
    case WM_LBUTTONUP:
      if (x->flags & LF_CAPTURED) {
        x->flags &= ~(uint16_t)LF_CAPTURED;
        if (!x->combo && GetCapture() == h) ReleaseCapture();
        if (x->combo && !in_client(w, lp)) return 0;
        if (x->count) notify(h, LBN_SELCHANGE);
      }
      return 0;
    case WM_CANCELMODE:
      x->flags &= ~(uint16_t)LF_CAPTURED;
      return user_def_window_proc(h, msg, wp, lp);
    case WM_KEYDOWN:
      key_down(h, w, x, wp);
      return 0;
    case WM_CHAR:
      char_in(h, w, x, wp);
      return 0;
    default:
      if (msg >= LB_ADDSTRING && msg <= LB_CARETOFF) return lb_message(h, w, x, msg, wp, lp);
      return user_def_window_proc(h, msg, wp, lp);
  }
}
