/* Menus (task stars-0z4.13), part 2: the Windows 3.1 look of the menu bar and the pop-up menus,
 * and the tracking loop (mouse and keyboard), TrackPopupMenu and the control-menu pop-up. Plain C,
 * no harness dependency.
 *
 * Model. A tracking (one at a time) is a modal loop on the message pump (user_pump_get, like the
 * move and size loops of user_nc.c). Its state is the MTrack record in the arena block of
 * menu.c: level 0 is the menu bar, the control-menu box or the TrackPopupMenu menu, the other
 * levels are the open pop-ups. Every pop-up is a real window of the runtime class "#32768"
 * (USER_CB_MENUPOPUP), created hidden-activation-free (SW_SHOWNOACTIVATE) and destroyed when its
 * level closes, so the window system repaints what it covered. The loop follows Wine's
 * dlls/win32u/menu.c track_menu (4097-4376) message by message; every function below names the
 * Wine function it ports. Win16 packing of the messages is in runtime/menu.h.
 *
 * Look (drawn in code, no Microsoft file; every pixel rule is UNVERIFIED, M1-M4 in
 * runtime/tests/test_menu.c and docs/inventory.md section 18):
 *   bar     SM_CYMENU rows below the caption: 18 rows of items on COLOR_MENU, text in COLOR_MENUTEXT,
 *           item = text width + 16, the selected item on COLOR_HIGHLIGHT with COLOR_HIGHLIGHTTEXT,
 *           and a 1-pixel COLOR_WINDOWFRAME line as the last row.
 *   pop-up  1-pixel COLOR_WINDOWFRAME border, no shadow, items 18 high, separators 9 high with a
 *           COLOR_WINDOWFRAME line, a 16-pixel check column, text after a tab in a second column,
 *           a right-pointing triangle on items that open a sub-menu, grayed text in COLOR_GRAYTEXT. */
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/menu_internal.h"

#define SM(i) GetSystemMetrics(i)
#define COL(i) user_syscolor(i)

#define ITEM_H 18
#define SEP_H 9
#define CHECK_W 16
#define BAR_PAD 8
#define TAB_GAP 16
#define POP_RPAD 8
#define ARROW_W 14
#define POPUP_CLASS "#32768"
#define DBLCLK_MS 500

#define VK_LEFT_ 0x25
#define VK_RIGHT_ 0x27

enum { C_BKG = 4, C_FRAME = 6, C_TEXT = 7, C_HI = 13, C_HITEXT = 14, C_GRAY = 17 };

void user_menu_ui_init(void) {
  UserState *st = user_state();
  if (!st) return;
  for (unsigned i = 0; i < UC_MAX; i++) {
    UClass *c = &st->c[i];
    if (c->used) continue;
    memset(c, 0, sizeof *c);
    c->used = 1;
    c->wndproc = USER_CB_MENUPOPUP;
    strcpy(c->name, POPUP_CLASS);
    return;
  }
}

void user_menu_ui_shutdown(void) {}

/* ---- text ---- */

typedef struct MText {
  char left[64], right[64];
  int mnem; /* index in left of the mnemonic character, -1 */
} MText;

static void split_text(const char *s, MText *t) {
  int n = 0, rn = 0;
  bool inright = false;
  t->mnem = -1;
  t->left[0] = t->right[0] = 0;
  for (const char *p = s; *p; p++) {
    if (*p == '\t' && !inright) {
      inright = true;
      continue;
    }
    if (inright) {
      if (rn < 63) t->right[rn++] = *p;
      continue;
    }
    if (*p == '&') {
      if (p[1] == '&') {
        if (n < 63) t->left[n++] = '&';
        p++;
      } else if (p[1] && t->mnem < 0) {
        t->mnem = n;
      }
      continue;
    }
    if (n < 63) t->left[n++] = *p;
  }
  t->left[n] = 0;
  t->right[rn] = 0;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static bool item_sep(const UMenuItem *it) { return (it->flags & MF_SEPARATOR) != 0; }
static bool item_disabled(const UMenuItem *it) { return (it->flags & (MF_GRAYED | MF_DISABLED)) != 0; }
static int item_h(const UMenuItem *it) { return item_sep(it) ? SEP_H : ITEM_H; }

/* ---- DC with the system font, for measuring ---- */

static HDC16 mdc_open(uint16_t *old) {
  HDC16 dc = GetDC(0);
  *old = 0;
  if (dc) *old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
  return dc;
}
static void mdc_close(HDC16 dc, uint16_t old) {
  if (!dc) return;
  SelectObject(dc, old);
  ReleaseDC(0, dc);
}

/* ---- geometry ---- */

typedef struct PMetrics {
  int w, h, leftw, rightw;
} PMetrics;

static void popup_metrics(HDC16 dc, HMENU16 m, PMetrics *pm) {
  UMenuObj *o = user_menu_obj(m);
  bool arrow = false;
  int hh = 2;
  pm->leftw = pm->rightw = 0;
  if (o)
    for (unsigned i = 0; i < o->count; i++) {
      const UMenuItem *it = user_menu_item(m, i);
      MText t;
      int w;
      hh += item_h(it);
      if (item_sep(it)) continue;
      split_text(it->text, &t);
      w = user_text_width(dc, t.left);
      if (w > pm->leftw) pm->leftw = w;
      if (t.right[0]) {
        w = user_text_width(dc, t.right);
        if (w > pm->rightw) pm->rightw = w;
      }
      if (it->flags & MF_POPUP) arrow = true;
    }
  pm->h = hh;
  pm->w = 2 + CHECK_W + pm->leftw + (pm->rightw ? TAB_GAP + pm->rightw : 0) + (arrow ? ARROW_W : POP_RPAD);
}

bool user_menu_popup_size(HMENU16 m, int *w, int *h) {
  PMetrics pm;
  uint16_t old;
  HDC16 dc;
  if (!user_menu_valid(m)) return false;
  dc = mdc_open(&old);
  if (!dc) return false;
  popup_metrics(dc, m, &pm);
  mdc_close(dc, old);
  *w = pm.w;
  *h = pm.h;
  return true;
}

/* item rectangle inside a pop-up whose client origin is (0,0) */
static bool popup_item_rect(HMENU16 m, const PMetrics *pm, unsigned pos, RECT16 *r) {
  UMenuObj *o = user_menu_obj(m);
  int y = 1;
  if (!o || pos >= o->count) return false;
  for (unsigned i = 0; i < pos; i++) y += item_h(user_menu_item(m, i));
  *r = r_make(1, y, pm->w - 1, y + item_h(user_menu_item(m, pos)));
  return true;
}

/* bar item rectangles; the bar's top-left is (left, top) in whatever coordinates the caller uses */
static bool bar_item_rect(HDC16 dc, HMENU16 m, unsigned pos, int left, int top, RECT16 *r) {
  UMenuObj *o = user_menu_obj(m);
  int x = left;
  if (!o || pos >= o->count) return false;
  for (unsigned i = 0; i <= pos; i++) {
    const UMenuItem *it = user_menu_item(m, i);
    MText t;
    int w;
    if (item_sep(it)) {
      w = BAR_PAD;
    } else {
      split_text(it->text, &t);
      w = user_text_width(dc, t.left) + 2 * BAR_PAD;
    }
    if (i == pos) {
      *r = r_make(x, top, x + w, top + ITEM_H);
      return true;
    }
    x += w;
  }
  return false;
}

/* ---- drawing ---- */

static void hline(HDC16 dc, int x0, int x1, int y, uint32_t c) { user_fill(dc, r_make(x0, y, x1 + 1, y + 1), c); }

static void draw_label(HDC16 dc, int x, int y, int th, const MText *t, uint32_t color) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  user_draw_text(dc, x, y, t->left);
  if (t->mnem >= 0) {
    char pre[64];
    int cw, px;
    char ch[2] = {t->left[t->mnem], 0};
    memcpy(pre, t->left, (size_t)t->mnem);
    pre[t->mnem] = 0;
    px = user_text_width(dc, pre);
    cw = user_text_width(dc, ch);
    user_fill(dc, r_make(x + px, y + th - 1, x + px + cw, y + th), color);
  }
}

static void draw_check(HDC16 dc, int x, int y, uint32_t c) {
  /* a tick 9 pixels wide and 7 high, two rows thick */
  static const int8_t P[9][2] = {{0, 3}, {1, 4}, {2, 5}, {3, 4}, {4, 3}, {5, 2}, {6, 1}, {7, 0}, {8, -1}};
  for (int i = 0; i < 9; i++) {
    user_fill(dc, r_make(x + P[i][0], y + P[i][1] + 1, x + P[i][0] + 1, y + P[i][1] + 3), c);
  }
}

/* One item of a pop-up. r is the item rectangle in dc coordinates. */
static void draw_popup_item(HDC16 dc, const UMenuItem *it, RECT16 r, bool sel, const PMetrics *pm, int th) {
  MText t;
  uint32_t bg = COL(sel ? C_HI : C_BKG), fg;
  user_fill(dc, r, bg);
  if (item_sep(it)) {
    hline(dc, r.left, r.right - 1, (r.top + r.bottom) / 2, COL(C_FRAME));
    return;
  }
  fg = item_disabled(it) ? COL(C_GRAY) : COL(sel ? C_HITEXT : C_TEXT);
  split_text(it->text, &t);
  {
    int y = r.top + (ITEM_H - th) / 2;
    draw_label(dc, r.left + CHECK_W, y, th, &t, fg);
    if (t.right[0]) {
      MText rt = {{0}, {0}, -1};
      strcpy(rt.left, t.right);
      draw_label(dc, r.left + CHECK_W + pm->leftw + TAB_GAP, y, th, &rt, fg);
    }
  }
  if (it->flags & MF_CHECKED) draw_check(dc, r.left + 3, r.top + (ITEM_H - 7) / 2, fg);
  if (it->flags & MF_POPUP) user_tri_right(dc, r.right - 11, r.top + (ITEM_H - 7) / 2, fg);
}

/* The whole pop-up, client origin (0,0). */
static void draw_popup(HDC16 dc, HMENU16 m, int sel) {
  UMenuObj *o = user_menu_obj(m);
  PMetrics pm;
  uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
  int th = user_text_height(dc);
  uint32_t fr = COL(C_FRAME);
  popup_metrics(dc, m, &pm);
  user_fill(dc, r_make(0, 0, pm.w, pm.h), COL(C_BKG));
  hline(dc, 0, pm.w - 1, 0, fr);
  hline(dc, 0, pm.w - 1, pm.h - 1, fr);
  user_fill(dc, r_make(0, 1, 1, pm.h - 1), fr);
  user_fill(dc, r_make(pm.w - 1, 1, pm.w, pm.h - 1), fr);
  if (o)
    for (unsigned i = 0; i < o->count; i++) {
      RECT16 r;
      if (popup_item_rect(m, &pm, i, &r)) draw_popup_item(dc, user_menu_item(m, i), r, (int)i == sel, &pm, th);
    }
  SelectObject(dc, old);
}

static MTrack *T(void) {
  UMenuState *st = user_menu_state();
  return st ? &st->trk : NULL;
}

/* the level shown by pop-up window h, or -1 */
static int level_of_hwnd(const MTrack *t, HWND16 h) {
  if (!t || !t->active) return -1;
  for (int k = 0; k < t->depth; k++)
    if (t->lv[k].hwnd == h) return k;
  return -1;
}

static void popup_draw_dc(HWND16 h, HDC16 dc) {
  MTrack *t = T();
  int k = level_of_hwnd(t, h);
  if (k >= 0) draw_popup(dc, t->lv[k].menu, t->lv[k].sel);
}

static void popup_paint_now(HWND16 h) {
  HDC16 dc;
  if (!user_wnd(h)) return;
  dc = GetDC(h);
  if (!dc) return;
  popup_draw_dc(h, dc);
  ReleaseDC(h, dc);
  ValidateRect(h, 0);
}

uint32_t user_menu_popup_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  switch (msg) {
    case WM_ERASEBKGND: return 1; /* the pop-up paints every pixel itself */
    case WM_PAINT: {
      uint16_t hh = 0;
      FARPTR ps = user_scratch(sizeof(PAINTSTRUCT16), &hh);
      if (ps) {
        HDC16 dc = BeginPaint(h, ps);
        if (dc) popup_draw_dc(h, dc);
        EndPaint(h, ps);
        user_scratch_free(hh);
      }
      return 0;
    }
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}

/* The bar. bar is its rectangle in the coordinates of dc (the window DC of hwnd). */
void user_menu_paint_bar(HWND16 hwnd, HDC16 dc, RECT16 bar) {
  UWnd *w = user_wnd(hwnd);
  MTrack *t = T();
  HMENU16 m;
  UMenuObj *o;
  uint16_t old;
  int th, sel = -1;
  if (!w || !w->hmenu) return;
  m = w->hmenu;
  o = user_menu_obj(m);
  old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
  th = user_text_height(dc);
  if (t && t->active && t->owner == hwnd && t->kind == MK_BAR && t->lv[0].menu == m) sel = t->lv[0].sel;
  user_fill(dc, bar, COL(C_BKG));
  hline(dc, bar.left, bar.right - 1, bar.top + ITEM_H, COL(C_FRAME));
  if (o)
    for (unsigned i = 0; i < o->count; i++) {
      const UMenuItem *it = user_menu_item(m, i);
      RECT16 r;
      MText tx;
      bool s = (int)i == sel;
      uint32_t fg;
      if (!bar_item_rect(dc, m, i, bar.left, bar.top, &r) || r.left >= bar.right) break;
      if (r.right > bar.right) r.right = bar.right;
      if (item_sep(it)) continue;
      user_fill(dc, r, COL(s ? C_HI : C_BKG));
      split_text(it->text, &tx);
      fg = item_disabled(it) ? COL(C_GRAY) : COL(s ? C_HITEXT : C_TEXT);
      draw_label(dc, r.left + BAR_PAD, r.top + (ITEM_H - th) / 2, th, &tx, fg);
    }
  SelectObject(dc, old);
}

void user_menu_redraw_bar(HWND16 hwnd) {
  UNcLayout L;
  HDC16 dc;
  const UWnd *w = user_wnd(hwnd);
  if (!w || !w->hmenu || !(w->style & WS_VISIBLE) || !user_nc_layout(hwnd, &L) || r_empty(L.menubar)) return;
  dc = GetWindowDC(hwnd);
  if (!dc) return;
  user_menu_paint_bar(hwnd, dc, L.menubar);
  ReleaseDC(hwnd, dc);
}

void DrawMenuBar(HWND16 hwnd) { user_menu_redraw_bar(hwnd); }

/* ---- levels ---- */

static bool lvl_popup(const MTrack *t, int k) { return k > 0 || t->kind == MK_POPUP; }

/* screen rectangle of the bar (owner's window) or of the control-menu box */
static bool owner_rects(HWND16 hwnd, RECT16 *bar, RECT16 *sysbox) {
  const UWnd *w = user_wnd(hwnd);
  UNcLayout L;
  int ox, oy;
  if (!w || !user_nc_layout(hwnd, &L)) return false;
  user_window_origin(w, &ox, &oy);
  if (bar) *bar = r_offset(L.menubar, ox, oy);
  if (sysbox) *sysbox = r_offset(L.sysbox, ox, oy);
  return true;
}

static bool level_item_rect(MTrack *t, int k, unsigned pos, RECT16 *r) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  if (!o || pos >= o->count) return false;
  if (!lvl_popup(t, k)) {
    if (t->kind == MK_SYSBAR) {
      if (pos) return false;
      *r = L->rect;
      return true;
    } else {
      uint16_t old;
      HDC16 dc = mdc_open(&old);
      bool ok;
      if (!dc) return false;
      ok = bar_item_rect(dc, L->menu, pos, L->rect.left, L->rect.top, r);
      mdc_close(dc, old);
      return ok;
    }
  } else {
    PMetrics pm;
    uint16_t old;
    HDC16 dc = mdc_open(&old);
    RECT16 ir;
    if (!dc) return false;
    popup_metrics(dc, L->menu, &pm);
    mdc_close(dc, old);
    if (!popup_item_rect(L->menu, &pm, pos, &ir)) return false;
    *r = r_offset(ir, L->rect.left, L->rect.top);
    return true;
  }
}

enum { HT_NOWHERE = 0, HT_BORDER = 1, HT_ITEM = 2 };

/* Wine find_item_by_coords */
static int level_hit(MTrack *t, int k, int x, int y, int *pos) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  *pos = -1;
  if (!o || !r_contains(L->rect, x, y)) return HT_NOWHERE;
  if (!lvl_popup(t, k) && t->kind == MK_SYSBAR) {
    *pos = 0;
    return HT_ITEM;
  }
  {
    /* measure once: the text widths dominate the cost of this function */
    uint16_t old;
    HDC16 dc = mdc_open(&old);
    PMetrics pm;
    int found = -1;
    if (!dc) return HT_BORDER;
    if (lvl_popup(t, k)) popup_metrics(dc, L->menu, &pm);
    for (unsigned i = 0; i < o->count && found < 0; i++) {
      RECT16 r;
      if (lvl_popup(t, k)) {
        if (!popup_item_rect(L->menu, &pm, i, &r)) break;
        r = r_offset(r, L->rect.left, L->rect.top);
      } else if (!bar_item_rect(dc, L->menu, i, L->rect.left, L->rect.top, &r)) {
        break;
      }
      if (r_contains(r, x, y)) found = (int)i;
    }
    mdc_close(dc, old);
    if (found >= 0) {
      *pos = found;
      return HT_ITEM;
    }
  }
  return HT_BORDER;
}

static void redraw_level(MTrack *t, int k) {
  if (lvl_popup(t, k)) popup_paint_now(t->lv[k].hwnd);
  else if (t->kind == MK_BAR) user_menu_redraw_bar(t->owner);
}

/* ---- WM_MENUSELECT ---- */

static void send_menuselect(MTrack *t, int k, int idx) {
  UMenuObj *o = user_menu_obj(t->lv[k].menu);
  const UMenuItem *it = (idx >= 0) ? user_menu_item(t->lv[k].menu, (unsigned)idx) : NULL;
  uint16_t flags, wp;
  if (!o || !it) return;
  flags = (uint16_t)(it->flags | MF_HILITE | (o->sys ? MF_SYSMENU : 0)); /* M8 */
  wp = (it->flags & MF_POPUP) ? it->sub : it->id;
  SendMessage(t->owner, WM_MENUSELECT, wp, make_long(flags, t->lv[k].menu));
}

static bool still_active(MTrack *t) { return t->active && user_wnd(t->owner); }

/* Wine select_item (3179-3240) */
static void select_item(MTrack *t, int k, int idx, bool send) {
  MLevel *L = &t->lv[k];
  if (L->sel == idx) return;
  L->sel = (int16_t)idx;
  redraw_level(t, k);
  if (idx >= 0 && send) send_menuselect(t, k, idx);
}

static void hide_sub_popups(MTrack *t, int k) {
  while (t->depth > k + 1) {
    MLevel *L = &t->lv[t->depth - 1];
    if (L->hwnd) DestroyWindow(L->hwnd);
    memset(L, 0, sizeof *L);
    L->sel = -1;
    t->depth--;
  }
}

/* Wine move_selection (3253-3277): separators are skipped, the selection wraps */
static void move_selection(MTrack *t, int k, int offset) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  int n;
  if (!o) return;
  n = (int)o->count;
  if (L->sel >= 0) {
    if (n == 1) return;
    for (int i = L->sel + offset; i >= 0 && i < n; i += offset) {
      if (item_sep(user_menu_item(L->menu, (unsigned)i))) continue;
      select_item(t, k, i, true);
      return;
    }
  }
  for (int i = (offset > 0) ? 0 : n - 1; i >= 0 && i < n; i += offset) {
    if (item_sep(user_menu_item(L->menu, (unsigned)i))) continue;
    select_item(t, k, i, true);
    return;
  }
}

static HWND16 popup_create(RECT16 r) {
  uint16_t hs = 0;
  FARPTR s = user_scratch(16, &hs);
  HWND16 h;
  if (!s) return 0;
  strcpy(LIN(s), POPUP_CLASS);
  ((char *)LIN(s))[sizeof POPUP_CLASS] = 0; /* an empty window name follows the class name */
  h = CreateWindow(s, s + sizeof POPUP_CLASS, WS_POPUP, r.left, r.top, (int16_t)(r.right - r.left), (int16_t)(r.bottom - r.top), 0, 0, 0, 0);
  user_scratch_free(hs);
  if (h) ShowWindow(h, SW_SHOWNOACTIVATE);
  return h;
}

static RECT16 clamp_popup(int x, int y, int w, int h) {
  if (x + w > GDI_SCREEN_W) x = GDI_SCREEN_W - w;
  if (x < 0) x = 0;
  if (y + h > GDI_SCREEN_H) y = GDI_SCREEN_H - h;
  if (y < 0) y = 0;
  return r_make(x, y, x + w, y + h);
}

/* Wine show_sub_popup (3370-3420) */
static bool show_sub_popup(MTrack *t, int k, bool select_first) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  const UMenuItem *it;
  HMENU16 sub;
  RECT16 ir, pr;
  PMetrics pm;
  uint16_t old;
  HDC16 dc;
  int x, y, sys;
  if (!o || L->sel < 0 || k + 1 >= UM_MAX_LEVELS) return false;
  it = user_menu_item(L->menu, (unsigned)L->sel);
  if (!it || !(it->flags & MF_POPUP) || item_disabled(it)) return false;
  if (t->depth > k + 1) return true; /* already open */
  sub = it->sub;
  sys = o->sys;
  if (!(t->flags & TPM_NONOTIFY)) {
    SendMessage(t->owner, WM_INITMENUPOPUP, sub, make_long((uint16_t)L->sel, (uint16_t)sys));
    if (!still_active(t)) return false;
    it = (L->sel >= 0) ? user_menu_item(L->menu, (unsigned)L->sel) : NULL; /* the application may have changed it */
    if (!it || !(it->flags & MF_POPUP) || !user_menu_valid(it->sub)) return false;
    sub = it->sub;
  }
  if (sys) user_menu_sys_popup(t->owner); /* Wine init_sys_menu_popup: gray what the window cannot do */
  if (!level_item_rect(t, k, (unsigned)L->sel, &ir)) return false;
  dc = mdc_open(&old);
  if (!dc) return false;
  popup_metrics(dc, sub, &pm);
  mdc_close(dc, old);
  if (lvl_popup(t, k)) {
    x = L->rect.right - 1; /* overlaps the parent's border by one pixel (Wine 3398-3410) */
    y = ir.top - 1;        /* the first item lines up with the selected one */
    if (x + pm.w > GDI_SCREEN_W) x = L->rect.left - pm.w + 1; /* M5: UNVERIFIED flip to the left */
  } else {
    x = ir.left;
    y = ir.bottom; /* the pop-up's top border is the row of the bar's bottom line */
  }
  pr = clamp_popup(x, y, pm.w, pm.h);
  {
    HWND16 h = popup_create(pr);
    if (!h) return false;
    t->lv[k + 1].menu = sub;
    t->lv[k + 1].hwnd = h;
    t->lv[k + 1].rect = pr;
    t->lv[k + 1].sel = -1;
    t->depth = (int16_t)(k + 2);
    popup_paint_now(h);
  }
  if (select_first) move_selection(t, k + 1, 1);
  return true;
}

/* Wine exec_focused_item (3472-3512): the id, -1 when nothing ran, -2 when a pop-up opened */
static int exec_focused(MTrack *t, int k) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  const UMenuItem *it;
  if (!o || L->sel < 0) return -1;
  it = user_menu_item(L->menu, (unsigned)L->sel);
  if (!it) return -1;
  if (it->flags & MF_POPUP) {
    show_sub_popup(t, k, true);
    return -2;
  }
  if (item_disabled(it) || item_sep(it)) return -1;
  if (!(t->flags & TPM_RETURNCMD)) {
    if (o->sys) PostMessage(t->owner, WM_SYSCOMMAND, it->id, make_long((uint16_t)t->px, (uint16_t)t->py));
    else PostMessage(t->owner, WM_COMMAND, it->id, 0);
  }
  t->result = it->id;
  return it->id;
}

/* Wine switch_tracking (3522-3541) */
static void switch_tracking(MTrack *t, int k, int pos) {
  hide_sub_popups(t, k);
  select_item(t, k, pos, true);
}

/* ---- keyboard ---- */

/* Wine find_item_by_key (4056-4094): the item index, -1 none, -2 close */
static int find_item_by_key(MTrack *t, int k, int ch) {
  MLevel *L = &t->lv[k];
  UMenuObj *o = user_menu_obj(L->menu);
  uint32_t r;
  if (!o) return -1;
  for (unsigned i = 0; i < o->count; i++) {
    const UMenuItem *it = user_menu_item(L->menu, i);
    MText tx;
    if (item_sep(it)) continue;
    split_text(it->text, &tx);
    if (tx.mnem >= 0 && lower((unsigned char)tx.left[tx.mnem]) == lower(ch)) return (int)i;
  }
  r = SendMessage(t->owner, WM_MENUCHAR, (uint16_t)ch,
                  make_long((uint16_t)((lvl_popup(t, k) ? MF_POPUP : 0) | (o->sys ? MF_SYSMENU : 0)), L->menu));
  if (!still_active(t)) return -2;
  if ((r >> 16) == 2) return (int)(r & 0xFFFF); /* MNC_EXECUTE */
  if ((r >> 16) == 1) return -2;                /* MNC_CLOSE */
  return -1;
}

/* Wine do_next_menu (3678-3790) for the bar and the control menu: Left from the first bar item and Right
 * from the last go to the control menu, and back. */
static bool next_menu(MTrack *t, int vk) {
  const UWnd *w = user_wnd(t->owner);
  UMenuObj *o = user_menu_obj(t->lv[0].menu);
  bool at_end = false;
  int id = 0;
  if (!w || !o || t->kind == MK_POPUP) return false;
  if (vk == VK_LEFT_ && t->lv[0].sel == 0) at_end = true;
  else if (vk == VK_RIGHT_ && t->kind == MK_BAR && t->lv[0].sel == (int)o->count - 1) at_end = true;
  else if (vk == VK_RIGHT_ && t->kind == MK_SYSBAR) at_end = true;
  if (!at_end) return false;
  {
    RECT16 bar, box;
    HMENU16 nm;
    int nk;
    if (!owner_rects(t->owner, &bar, &box)) return false;
    if (t->kind == MK_SYSBAR) {
      if ((w->style & WS_CHILD) || !w->hmenu) return false;
      nm = w->hmenu;
      nk = MK_BAR;
      if (vk == VK_LEFT_) {
        UMenuObj *bo = user_menu_obj(nm);
        id = bo && bo->count ? (int)bo->count - 1 : 0;
      }
    } else {
      if (!(w->style & WS_SYSMENU)) return false;
      nm = user_menu_sys_outer(t->owner);
      nk = MK_SYSBAR;
    }
    if (!nm) return false;
    select_item(t, 0, -1, false);
    hide_sub_popups(t, 0);
    t->kind = (uint8_t)nk;
    t->lv[0].menu = nm;
    t->lv[0].rect = (nk == MK_BAR) ? bar : box;
    t->lv[0].sel = -1;
    select_item(t, 0, id, true);
    return true;
  }
}

/* Wine menu_key_left (3930-3970) */
static void key_left(MTrack *t) {
  int prev = t->depth - 1;
  bool had_sub = t->depth > 1;
  if (prev > 0) prev--;
  hide_sub_popups(t, prev);
  if (prev == 0 && t->kind != MK_POPUP) {
    if (!next_menu(t, VK_LEFT_)) move_selection(t, 0, -1);
    if (had_sub) show_sub_popup(t, 0, true);
  }
}

/* Wine menu_right_key (3972-4018) */
static void key_right(MTrack *t) {
  int cur = t->depth - 1;
  bool opened = false;
  if (t->kind == MK_POPUP || cur > 0) {
    int before = t->depth;
    show_sub_popup(t, cur, true);
    if (t->depth != before) return;
  }
  if (t->kind != MK_POPUP) {
    if (cur > 0) {
      hide_sub_popups(t, 0);
      opened = true;
    }
    if (!next_menu(t, VK_RIGHT_)) move_selection(t, 0, 1);
    if (opened) show_sub_popup(t, 0, true);
  }
}

/* Wine menu_key_escape (3820-3845): true when the whole menu ends */
static bool key_escape(MTrack *t) {
  if (t->depth > 1) {
    hide_sub_popups(t, t->depth - 2);
    return false;
  }
  return true;
}

static void on_key_down(MTrack *t, UPump *pu, const MSG16 *m) {
  int cur = t->depth - 1;
  switch (m->wParam) {
    case VK_MENU:
    case VK_F10:
      t->exit = 1;
      break;
    case 0x24: /* VK_HOME */
    case 0x23: /* VK_END */
      select_item(t, cur, -1, false);
      move_selection(t, cur, m->wParam == 0x24 ? 1 : -1);
      break;
    case 0x26: /* VK_UP */
    case 0x28: /* VK_DOWN */
      if (!lvl_popup(t, cur)) show_sub_popup(t, cur, true); /* on the bar: pull the menu down */
      else move_selection(t, cur, m->wParam == 0x26 ? -1 : 1);
      break;
    case VK_LEFT_: key_left(t); break;
    case VK_RIGHT_: key_right(t); break;
    case VK_ESCAPE:
      if (key_escape(t)) t->exit = 1;
      break;
    case VK_RETURN:
    case VK_SPACE: {
      int r = exec_focused(t, cur); /* Wine WM_CHAR '\r' / ' ': anything but "a pop-up opened" ends the menu */
      if (r != -2) t->exit = 1;
      break;
    }
    default:
      TranslateMessage(pu->p); /* posts WM_CHAR, handled by on_char (Wine NtUserTranslateMessage, 4294) */
      break;
  }
}

static void on_char(MTrack *t, const MSG16 *m) {
  int cur = t->depth - 1, pos;
  if (m->wParam < 32) return;
  pos = find_item_by_key(t, cur, m->wParam);
  if (pos == -2) {
    t->exit = 1;
  } else if (pos == -1) {
    MessageBeep(0);
  } else {
    int r;
    select_item(t, cur, pos, true);
    if (!still_active(t)) return;
    r = exec_focused(t, cur);
    if (r != -2) t->exit = 1;
  }
}

/* ---- mouse ---- */

/* deepest level that contains the point, -1 if none (Wine menu_from_point) */
static int level_from_point(MTrack *t, int x, int y) {
  for (int k = t->depth - 1; k >= 0; k--) {
    int pos;
    if (level_hit(t, k, x, y, &pos) != HT_NOWHERE) return k;
  }
  return -1;
}

/* Wine menu_button_down (3545-3586): false ends the menu */
static bool button_down(MTrack *t, int k, int x, int y, bool dblclk) {
  int pos, ht;
  if (k < 0) return false;
  ht = level_hit(t, k, x, y, &pos);
  if (!lvl_popup(t, k) && t->kind == MK_SYSBAR && dblclk) return false;
  if (pos >= 0) {
    if (t->lv[k].sel != pos) switch_tracking(t, k, pos);
    if (still_active(t) && t->depth <= k + 1) show_sub_popup(t, k, false);
  }
  return ht == HT_ITEM || (lvl_popup(t, k) && ht != HT_NOWHERE);
}

/* Wine menu_button_up (3588-3630): the id, -1 to go on */
static int button_up(MTrack *t, int k, int x, int y) {
  MLevel *L = &t->lv[k];
  int pos = -1;
  bool top = !lvl_popup(t, k);
  if (level_hit(t, k, x, y, &pos) != HT_ITEM) pos = -1;
  if (pos >= 0 && L->sel == pos) {
    const UMenuItem *it = user_menu_item(L->menu, (unsigned)pos);
    if (it && !(it->flags & MF_POPUP)) {
      int r = exec_focused(t, k);
      return (r == -1 || r == -2) ? -1 : r;
    }
    if (top && t->rcvd_btn_up) return 0; /* a click on an item that is already open ends the menu */
  }
  if (top) {
    if (pos < 0) return 0;
    t->rcvd_btn_up = 1;
  }
  return -1;
}

/* Wine menu_mouse_move (3652-3676) */
static void mouse_move(MTrack *t, int k, int x, int y) {
  int pos = -1, cur = t->depth - 1;
  if (level_hit(t, k, x, y, &pos) != HT_ITEM) pos = -1;
  if (pos < 0) {
    /* no item under the pointer: nothing is selected in the current menu; the parent item is announced again */
    MLevel *L = &t->lv[cur];
    if (L->sel >= 0) {
      L->sel = -1;
      redraw_level(t, cur);
      if (cur > 0 && t->lv[cur - 1].sel >= 0) send_menuselect(t, cur - 1, t->lv[cur - 1].sel);
    }
  } else if (t->lv[k].sel != pos) {
    switch_tracking(t, k, pos);
    if (still_active(t)) show_sub_popup(t, k, false);
  }
}

static void on_mouse(MTrack *t, const MSG16 *m) {
  int x = m->pt.x, y = m->pt.y;
  int k = level_from_point(t, x, y);
  t->px = (int16_t)x;
  t->py = (int16_t)y;
  switch (m->message) {
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
      if (!(t->flags & TPM_RIGHTBUTTON)) break;
      /* fall through */
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      bool dbl = m->message == WM_LBUTTONDBLCLK;
      /* M7: a second press on the control-menu box within the double-click time counts as a double click */
      if (t->kind == MK_SYSBAR && k == 0 && (uint32_t)(m->time - t->start_time) < DBLCLK_MS) dbl = true;
      if (!button_down(t, k, x, y, dbl)) {
        t->exit = 1;
        if (dbl && t->kind == MK_SYSBAR && k == 0) t->result = -3; /* the caller sends WM_NCLBUTTONDBLCLK */
      }
      break;
    }
    case WM_RBUTTONUP:
      if (!(t->flags & TPM_RIGHTBUTTON)) break;
      /* fall through */
    case WM_LBUTTONUP:
      if (k >= 0) {
        int r = button_up(t, k, x, y);
        if (r != -1) t->exit = 1;
      } else {
        t->exit = t->kind != MK_POPUP; /* M9 */
      }
      break;
    case WM_MOUSEMOVE:
      if (k >= 0) mouse_move(t, k, x, y);
      break;
    default: break;
  }
}

/* ---- the tracking loop ---- */

static void tracker_loop(MTrack *t) {
  UPump pu;
  MSG16 m;
  if (!user_pump_open(&pu)) return;
  SetCapture(t->owner);
  if (t->pending_return && !t->exit) {
    int r;
    t->pending_return = 0;
    r = exec_focused(t, t->depth - 1);
    if (r != -2) t->exit = 1;
  }
  while (!t->exit && user_pump_get(&pu, &m)) {
    if (!still_active(t)) break;
    if (m.message == WM_CANCELMODE) {
      t->exit = 1;
      continue;
    }
    if (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) {
      on_mouse(t, &m);
    } else if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
      on_key_down(t, &pu, &m);
    } else if (m.message == WM_CHAR || m.message == WM_SYSCHAR) {
      on_char(t, &m);
    } else if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) {
      /* key up and the other key messages: keyboard messages are always removed (Wine 4276) */
    } else {
      user_pump_dispatch(&pu);
    }
  }
  user_pump_close(&pu);
  if (GetCapture() == t->owner) ReleaseCapture();
}

static void tracker_begin(MTrack *t, HWND16 owner, int kind, HMENU16 menu, RECT16 rect, uint16_t flags) {
  memset(t, 0, sizeof *t);
  t->active = 1;
  t->kind = (uint8_t)kind;
  t->owner = owner;
  t->flags = flags;
  t->depth = 1;
  t->result = -1;
  t->lv[0].menu = menu;
  t->lv[0].rect = rect;
  t->lv[0].sel = -1;
}

/* Wine exit_tracking and the end of track_menu (4338-4376) */
static void tracker_end(MTrack *t) {
  hide_sub_popups(t, 0);
  if (t->kind == MK_POPUP && t->lv[0].hwnd) DestroyWindow(t->lv[0].hwnd);
  t->lv[0].hwnd = 0;
  t->lv[0].sel = -1;
  if (t->kind == MK_BAR) user_menu_redraw_bar(t->owner);
  t->active = 0;
  if (user_wnd(t->owner)) SendMessage(t->owner, WM_MENUSELECT, 0, make_long(0xFFFF, 0));
}

bool user_menu_track_info(UMenuTrackInfo *out) {
  MTrack *t = T();
  memset(out, 0, sizeof *out);
  if (!t) return false;
  out->active = t->active;
  out->depth = t->depth;
  out->owner = t->owner;
  for (int k = 0; k < t->depth && k < UM_MAX_LEVELS; k++) {
    out->lv[k].menu = t->lv[k].menu;
    out->lv[k].hwnd = t->lv[k].hwnd;
    out->lv[k].rect = t->lv[k].rect;
    out->lv[k].sel = t->lv[k].sel;
  }
  return t->active != 0;
}

bool user_menu_item_rect(HMENU16 m, unsigned pos, RECT16 *out) {
  MTrack *t = T();
  if (t && t->active) {
    for (int k = 0; k < t->depth; k++)
      if (t->lv[k].menu == m) return level_item_rect(t, k, pos, out);
  }
  if (!user_menu_valid(m)) return false;
  /* the menu bar of a visible window */
  {
    UserState *st = user_state();
    for (unsigned i = 1; i < UW_MAX; i++) {
      const UWnd *w = &st->w[i];
      RECT16 bar;
      uint16_t old;
      HDC16 dc;
      bool ok;
      if (!w->used || w->hmenu != m || !(w->style & WS_VISIBLE) || !owner_rects(user_hwnd(w), &bar, NULL)) continue;
      dc = mdc_open(&old);
      if (!dc) return false;
      ok = bar_item_rect(dc, m, pos, bar.left, bar.top, out);
      mdc_close(dc, old);
      return ok;
    }
  }
  return false;
}

/* ---- entry points ---- */

/* DefWindowProc SC_MOUSEMENU (defwnd.c 1016-1018; Wine track_mouse_menu_bar 4428-4445) */
void user_menu_track_mouse(HWND16 hwnd, int ht, int sx, int sy) {
  MTrack *t = T();
  const UWnd *w = user_wnd(hwnd);
  HMENU16 menu;
  RECT16 bar, box;
  int kind = (ht == HTSYSMENU) ? MK_SYSBAR : MK_BAR;
  if (!t || t->active || !w || !owner_rects(hwnd, &bar, &box)) return;
  menu = (kind == MK_SYSBAR) ? user_menu_sys_outer(hwnd) : GetMenu(hwnd);
  if (!menu || !user_menu_valid(menu)) return;
  if (kind == MK_BAR && r_empty(bar)) return;
  SendMessage(hwnd, WM_INITMENU, menu, 0); /* init_tracking */
  if (!user_wnd(hwnd)) return;
  menu = (kind == MK_SYSBAR) ? user_menu_sys_outer(hwnd) : GetMenu(hwnd); /* it may have changed */
  tracker_begin(t, hwnd, kind, menu, kind == MK_SYSBAR ? box : bar, TPM_LEFTBUTTON);
  t->px = (int16_t)sx;
  t->py = (int16_t)sy;
  t->start_time = user_state()->in.clk_time;
  if (kind == MK_SYSBAR) user_menu_sys_popup(hwnd);
  if (!button_down(t, 0, sx, sy, false)) t->exit = 1;
  tracker_loop(t);
  {
    bool dbl = t->result == -3;
    tracker_end(t);
    if (dbl && user_wnd(hwnd)) SendMessage(hwnd, WM_NCLBUTTONDBLCLK, HTSYSMENU, make_long((uint16_t)sx, (uint16_t)sy)); /* closes the window */
  }
}

/* DefWindowProc SC_KEYMENU (defwnd.c 1020-1022; Wine track_keyboard_menu_bar 4447-4503) */
void user_menu_track_keyboard(HWND16 hwnd, uint16_t wparam, uint16_t ch) {
  MTrack *t = T();
  const UWnd *w = user_wnd(hwnd);
  HMENU16 menu;
  RECT16 bar, box;
  bool sys = false;
  int item = -1;
  (void)wparam;
  while (w && (w->style & WS_CHILD) && w->parent != user_desktop()) { /* find the window that has a menu */
    hwnd = w->parent;
    w = user_wnd(hwnd);
  }
  if (!t || t->active || !w || (w->style & WS_CHILD) || !owner_rects(hwnd, &bar, &box)) return;
  menu = w->hmenu;
  if (!menu || (w->style & WS_MINIMIZE) || ch == ' ') {
    if (!(w->style & WS_SYSMENU)) return;
    menu = user_menu_sys_outer(hwnd);
    item = 0;
    sys = true;
  }
  if (!menu || !user_menu_valid(menu)) return;
  SendMessage(hwnd, WM_INITMENU, menu, 0);
  if (!user_wnd(hwnd)) return;
  w = user_wnd(hwnd);
  if (sys) menu = user_menu_sys_outer(hwnd);
  else menu = w->hmenu;
  if (!menu) return;
  tracker_begin(t, hwnd, sys ? MK_SYSBAR : MK_BAR, menu, sys ? box : bar, TPM_LEFTBUTTON);
  if (sys) user_menu_sys_popup(hwnd);
  if (ch && ch != ' ') {
    int r = find_item_by_key(t, 0, ch);
    if (r < 0) {
      if (r == -1) MessageBeep(0);
      t->exit = 1; /* TF_ENDMENU */
    } else {
      select_item(t, 0, r, true);
      t->pending_return = 1;
    }
  } else {
    select_item(t, 0, item, true);
    if (!sys || ch == ' ') {
      if (item < 0) move_selection(t, 0, 1);
      else t->pending_return = 1;
    }
  }
  tracker_loop(t);
  tracker_end(t);
}

/* Wine NtUserTrackPopupMenuEx (4505-4570) */
uint16_t TrackPopupMenu(HMENU16 hMenu, uint16_t fuFlags, int16_t x, int16_t y, int16_t nReserved, HWND16 hwnd, FARPTR lpRect) {
  MTrack *t = T();
  UMenuObj *o;
  PMetrics pm;
  uint16_t old;
  HDC16 dc;
  RECT16 r;
  HWND16 pw;
  int px = x, py = y;
  (void)nReserved;
  (void)lpRect;
  if (!t || t->active || !user_menu_valid(hMenu) || !user_wnd(hwnd)) return 0;
  if (!(fuFlags & TPM_NONOTIFY)) {
    SendMessage(hwnd, WM_INITMENU, hMenu, 0);
    if (!user_wnd(hwnd) || !user_menu_valid(hMenu)) return 0;
    SendMessage(hwnd, WM_INITMENUPOPUP, hMenu, 0);
    if (!user_wnd(hwnd) || !user_menu_valid(hMenu)) return 0;
  }
  o = user_menu_obj(hMenu);
  if (!o || o->count == 0) return 0;
  dc = mdc_open(&old);
  if (!dc) return 0;
  popup_metrics(dc, hMenu, &pm);
  mdc_close(dc, old);
  if (fuFlags & TPM_CENTERALIGN) px -= pm.w / 2;
  else if (fuFlags & TPM_RIGHTALIGN) px -= pm.w;
  if (fuFlags & TPM_VCENTERALIGN) py -= pm.h / 2;
  else if (fuFlags & TPM_BOTTOMALIGN) py -= pm.h;
  r = clamp_popup(px, py, pm.w, pm.h); /* M5: UNVERIFIED, moved to stay on the screen */
  pw = popup_create(r);
  if (!pw) return 0;
  tracker_begin(t, hwnd, MK_POPUP, hMenu, r, fuFlags);
  t->lv[0].hwnd = pw;
  t->px = x;
  t->py = y;
  popup_paint_now(pw);
  tracker_loop(t);
  {
    int res = t->result;
    tracker_end(t);
    if (fuFlags & TPM_RETURNCMD) return (uint16_t)(res < 0 ? 0 : res);
  }
  return 1;
}

/* ---- DefWindowProc keyboard handling (defwnd.c 2756-2815) ---- */

static HWND16 root_of(HWND16 h) {
  const UWnd *w = user_wnd(h);
  while (w && (w->style & WS_CHILD) && w->parent != user_desktop()) {
    h = w->parent;
    w = user_wnd(h);
  }
  return h;
}

uint32_t user_menu_key_event(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UMenuState *st = user_menu_state();
  const UWnd *w = user_wnd(h);
  if (!st || !w) return 0;
  switch (msg) {
    case WM_KEYDOWN:
      if (wp == VK_F10) st->f10_key = 1;
      break;
    case WM_SYSKEYDOWN:
      if (lp & 0x20000000UL) { /* KEYDATA_ALT */
        st->sys_key = (uint8_t)((wp == VK_MENU && !st->sys_key) ? 1 : 0);
        st->f10_key = 0;
        if (wp == 0x73 /* VK_F4 */) {
          /* defwnd.c 2766-2771: SC_CLOSE is POSTED to the top-level (root) window, unless its class has CS_NOCLOSE */
          HWND16 top = root_of(h);
          const UWnd *tw = user_wnd(top);
          if (tw && !(user_state()->c[tw->cls].style & CS_NOCLOSE)) PostMessage(top, WM_SYSCOMMAND, SC_CLOSE, 0);
        }
      } else if (wp == VK_F10) {
        st->f10_key = 1;
      } else if (wp == VK_ESCAPE && GetKeyState(VK_SHIFT) < 0) {
        SendMessage(h, WM_SYSCOMMAND, SC_KEYMENU, ' ');
      }
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      /* press and release F10 or Alt */
      if ((wp == VK_MENU && st->sys_key) || (wp == VK_F10 && st->f10_key)) {
        st->sys_key = st->f10_key = 0;
        SendMessage(root_of(h), WM_SYSCOMMAND, SC_KEYMENU, 0);
      }
      st = user_menu_state();
      if (st) st->sys_key = st->f10_key = 0;
      break;
    case WM_SYSCHAR:
      st->sys_key = 0;
      if (wp == '\r' && (w->style & WS_MINIMIZE)) {
        PostMessage(h, WM_SYSCOMMAND, SC_RESTORE, 0);
      } else if ((lp & 0x20000000UL) && wp) {
        if (wp == '\t' || wp == 0x1B) break;
        if (wp == ' ' && (w->style & WS_CHILD)) SendMessage(w->parent, msg, wp, lp);
        else SendMessage(h, WM_SYSCOMMAND, SC_KEYMENU, wp);
      } else if (wp != 0x1B) {
        MessageBeep(0);
      }
      break;
    default: break;
  }
  return 0;
}
