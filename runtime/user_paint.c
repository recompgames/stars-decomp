/* USER part 1 (task stars-0z4.6): update regions, BeginPaint/EndPaint, GetDC/ReleaseDC,
 * InvalidateRect, ValidateRect, UpdateWindow, ScrollWindow and the default WM_ERASEBKGND and
 * WM_PAINT handling. Update regions are lists of at most UPD_MAX rectangles in client
 * coordinates (more are merged into their bounding box). Plain C, no harness dependency. */
#include <string.h>

#include "runtime/user_internal.h"

/* ---- update region ---- */

static RECT16 client_rect(const UWnd *w) {
  return r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top);
}

bool user_upd_bbox(const UWnd *w, RECT16 *out) {
  RECT16 u = r_make(0, 0, 0, 0);
  if (!w->nupd) return false;
  for (unsigned i = 0; i < w->nupd; i++) u = r_union(u, w->upd[i]);
  *out = u;
  return true;
}

void user_upd_add(UWnd *w, RECT16 r, bool erase) {
  r = r_isect(r, client_rect(w));
  if (r_empty(r)) return;
  if (erase) w->erase = 1;
  for (unsigned i = 0; i < w->nupd; i++) {
    RECT16 e = w->upd[i];
    if (r.left >= e.left && r.top >= e.top && r.right <= e.right && r.bottom <= e.bottom) return; /* contained */
  }
  if (w->nupd >= UPD_MAX) {
    RECT16 u = r;
    for (unsigned i = 0; i < w->nupd; i++) u = r_union(u, w->upd[i]);
    w->upd[0] = u;
    w->nupd = 1;
    return;
  }
  w->upd[w->nupd++] = r;
}

void user_upd_sub(UWnd *w, RECT16 r) {
  RECT16 out[UPD_MAX * 4];
  unsigned n = 0;
  for (unsigned i = 0; i < w->nupd; i++) {
    RECT16 piece[4];
    int k = r_subtract(w->upd[i], r, piece);
    for (int j = 0; j < k && n < UPD_MAX * 4; j++) out[n++] = piece[j];
  }
  if (n > UPD_MAX) {
    RECT16 u = r_make(0, 0, 0, 0);
    for (unsigned i = 0; i < n; i++) u = r_union(u, out[i]);
    out[0] = u;
    n = 1;
  }
  for (unsigned i = 0; i < n; i++) w->upd[i] = out[i];
  w->nupd = (uint8_t)n;
  if (!w->nupd) w->erase = 0;
}

void InvalidateRect(HWND16 hwnd, FARPTR lpRect, uint16_t bErase) {
  UWnd *w = user_wnd(hwnd);
  if (!w || hwnd == user_desktop()) return;
  user_upd_add(w, lpRect ? *(RECT16 *)LIN(lpRect) : client_rect(w), bErase != 0);
}

void ValidateRect(HWND16 hwnd, FARPTR lpRect) {
  UWnd *w = user_wnd(hwnd);
  if (!w) return;
  user_upd_sub(w, lpRect ? *(RECT16 *)LIN(lpRect) : client_rect(w));
}

/* Paint a visible window that needs it, then its visible children (RDW_UPDATENOW |
 * RDW_ALLCHILDREN; Wine dlls/user.exe16/window.c ~701, UpdateWindow16 -> RedrawWindow). */
static void update_tree(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  if (!w || !(w->style & WS_VISIBLE)) return;
  if (w->ncdirty) { /* the non-client area first (Windows 3.1 look, stars-0z4.9) */
    w->ncdirty = 0;
    SendMessage(hwnd, WM_NCPAINT, 1, 0);
    w = user_wnd(hwnd);
    if (!w) return;
  }
  if (w->nupd) SendMessage(hwnd, WM_PAINT, 0, 0);
  w = user_wnd(hwnd); /* the procedure may have destroyed it */
  if (!w) return;
  {
    /* Snapshot the children first: handles are reused slots, so a link read after a paint
     * handler ran may lead into an unrelated window (even back into this parent). Visit an
     * entry only if it is still alive and still a child of hwnd. */
    HWND16 kids[UW_MAX];
    unsigned n = 0;
    for (const UWnd *c = user_wnd(w->child); c && n < UW_MAX; c = user_wnd(c->next)) kids[n++] = user_hwnd(c);
    for (unsigned i = 0; i < n; i++) {
      const UWnd *cw = user_wnd(kids[i]);
      if (cw && cw->parent == hwnd) update_tree(kids[i]);
    }
  }
}

/* Hidden windows are left alone (not IsWindowVisible). UNVERIFIED for Windows 3.1: the
 * over-invalidating child recursion follows Wine. */
void UpdateWindow(HWND16 hwnd) {
  if (!user_wnd(hwnd) || !IsWindowVisible(hwnd)) return;
  update_tree(hwnd);
}

/* W-paint: server/window.c find_child_to_repaint 1117-1145. A visible window that needs
 * painting is chosen before its own children; siblings are searched top of the z-order first. */
static HWND16 find_child_to_repaint(const UWnd *parent) {
  for (const UWnd *c = user_wnd(parent->child); c; c = user_wnd(c->next)) {
    HWND16 r;
    if (!(c->style & WS_VISIBLE)) continue;
    if (c->nupd || c->ncdirty) return user_hwnd(c);
    if (!(c->style & WS_MINIMIZE) && (r = find_child_to_repaint(c)) != 0) return r;
  }
  return 0;
}

HWND16 user_first_paint(HWND16 filter) {
  UserState *st = user_state();
  HWND16 w = find_child_to_repaint(&st->w[0]);
  if (w && filter) {
    /* only the filter window or one of its descendants (1147-1161) */
    UWnd *c = user_wnd(w);
    while (c && user_hwnd(c) != filter) c = user_wnd(c->parent);
    if (!c) return 0;
  }
  return w;
}

/* ---- device contexts ---- */

/* The visible region of a window's client area, as a clip rectangle plus excluded rectangles
 * (screen coordinates): the client area clipped by every ancestor's client area; minus visible
 * children for WS_CLIPCHILDREN; minus higher siblings for WS_CLIPSIBLINGS windows and, always,
 * for top-level windows (UNVERIFIED for Windows 3.1; the ancestors' own sibling clipping also
 * applies). A hidden window has an empty region (UNVERIFIED). */
#define MAX_EXCL 128

static RECT16 window_rect_screen(const UWnd *c) {
  int ox, oy;
  user_window_origin(c, &ox, &oy);
  return r_make(ox, oy, ox + (c->wrect.right - c->wrect.left), oy + (c->wrect.bottom - c->wrect.top));
}

static HDC16 get_dc_impl(HWND16 h, const RECT16 *paint_clip, bool whole) {
  UWnd *w;
  HDC16 dc;
  RECT16 clip, excl[MAX_EXCL];
  int ox, oy;
  unsigned n = 0;
  if (h == 0) return gdi_create_screen_dc();
  w = user_wnd(h);
  if (!w) return 0;
  dc = gdi_create_screen_dc();
  if (!dc) return 0;
  if (whole) user_window_origin(w, &ox, &oy);
  else user_client_origin(w, &ox, &oy);
  gdi_dc_set_origin(dc, (int16_t)ox, (int16_t)oy);
  if (!user_visible(h)) {
    gdi_dc_set_hard_clip(dc, 0, 0, 0, 0);
    return dc;
  }
  if (whole) clip = r_make(ox, oy, ox + (w->wrect.right - w->wrect.left), oy + (w->wrect.bottom - w->wrect.top));
  else clip = r_make(ox, oy, ox + (w->crect.right - w->crect.left), oy + (w->crect.bottom - w->crect.top));
  for (UWnd *a = user_wnd(w->parent); a && user_hwnd(a) != user_desktop(); a = user_wnd(a->parent)) {
    int ax, ay;
    user_client_origin(a, &ax, &ay);
    clip = r_isect(clip, r_make(ax, ay, ax + (a->crect.right - a->crect.left), ay + (a->crect.bottom - a->crect.top)));
  }
  clip = r_isect(clip, r_make(0, 0, GDI_SCREEN_W, GDI_SCREEN_H));
  if (paint_clip) clip = r_isect(clip, r_offset(*paint_clip, ox, oy));
  if ((w->style & WS_CLIPCHILDREN) && !whole)
    for (const UWnd *c = user_wnd(w->child); c && n < MAX_EXCL; c = user_wnd(c->next))
      if (c->style & WS_VISIBLE) excl[n++] = window_rect_screen(c);
  for (const UWnd *a = w; a && user_hwnd(a) != user_desktop(); a = user_wnd(a->parent)) {
    const UWnd *p = user_wnd(a->parent);
    if (!p) break;
    if ((a->style & WS_CLIPSIBLINGS) || user_hwnd(p) == user_desktop())
      for (const UWnd *s = user_wnd(p->child); s && s != a && n < MAX_EXCL; s = user_wnd(s->next))
        if (s->style & WS_VISIBLE) excl[n++] = window_rect_screen(s);
  }
  gdi_dc_set_hard_clip(dc, clip.left, clip.top, clip.right, clip.bottom);
  for (unsigned i = 0; i < n; i++) {
    RECT16 e = excl[i];
    ExcludeClipRect(dc, (int16_t)(e.left - ox), (int16_t)(e.top - oy), (int16_t)(e.right - ox), (int16_t)(e.bottom - oy));
  }
  return dc;
}

HDC16 user_get_dc(HWND16 h, const RECT16 *paint_clip) { return get_dc_impl(h, paint_clip, false); }
/* A DC for the whole window, non-client area included (GetWindowDC, WM_NCPAINT). */
HDC16 user_get_window_dc(HWND16 h) { return get_dc_impl(h, NULL, true); }

HDC16 GetDC(HWND16 hwnd) { return user_get_dc(hwnd, NULL); }

uint16_t ReleaseDC(HWND16 hwnd, HDC16 hdc) {
  (void)hwnd;
  return DeleteDC(hdc);
}

/* ---- BeginPaint / EndPaint ---- */

HDC16 BeginPaint(HWND16 hwnd, FARPTR lpPaint) {
  UWnd *w = user_wnd(hwnd);
  PAINTSTRUCT16 *ps;
  RECT16 bb = r_make(0, 0, 0, 0);
  HDC16 dc;
  bool erase;
  if (!lpPaint) return 0;
  ps = LIN(lpPaint);
  memset(ps, 0, sizeof *ps);
  if (!w) return 0;
  erase = w->erase != 0;
  user_upd_bbox(w, &bb);
  dc = user_get_dc(hwnd, &bb);
  /* UNVERIFIED (no Wine source line checked): BeginPaint validates the window, then sends
   * WM_ERASEBKGND with the paint DC. This order is this runtime's choice (docs/inventory.md
   * section 13). */
  w->nupd = 0;
  w->erase = 0;
  ps->hdc = dc;
  ps->rcPaint = bb;
  if (erase) {
    uint32_t r = SendMessage(hwnd, WM_ERASEBKGND, dc, 0);
    ps = LIN(lpPaint);
    ps->fErase = (r & 0xFFFF) ? 0 : 1; /* handled: the application need not erase */
  }
  return dc;
}

void EndPaint(HWND16 hwnd, FARPTR lpPaint) {
  const PAINTSTRUCT16 *ps;
  if (!lpPaint) return;
  ps = LIN(lpPaint);
  ReleaseDC(hwnd, ps->hdc);
}

/* ---- default handling ---- */

uint32_t user_def_erase(HWND16 h, HDC16 hdc) {
  UserState *st = user_state();
  UWnd *w = user_wnd(h);
  uint16_t hbr, brush, old;
  RECT16 c;
  bool temp = false;
  if (!w || !hdc) return 0;
  hbr = st->c[w->cls].hbr;
  if (!hbr) return 0;
  if (hbr < 0x100) { /* COLOR_xxx + 1 */
    unsigned idx = (unsigned)hbr - 1;
    if (idx >= 21) return 0;
    brush = CreateSolidBrush(user_syscolor(idx));
    temp = true;
  } else {
    brush = hbr;
  }
  c = client_rect(w);
  old = SelectObject(hdc, brush);
  PatBlt(hdc, c.left, c.top, (int16_t)(c.right - c.left), (int16_t)(c.bottom - c.top), PATCOPY);
  SelectObject(hdc, old);
  if (temp) DeleteObject(brush);
  return 1;
}

uint32_t user_def_paint(HWND16 h) {
  uint16_t hp = 0;
  FARPTR p = user_scratch(sizeof(PAINTSTRUCT16), &hp);
  if (!p) return 0;
  BeginPaint(h, p);
  EndPaint(h, p);
  user_scratch_free(hp);
  return 0;
}

/* ---- ScrollWindow ---- */

void ScrollWindow(HWND16 hwnd, int16_t dx, int16_t dy, FARPTR lpRect, FARPTR lpClipRect) {
  UWnd *w = user_wnd(hwnd);
  RECT16 area, clip, dst, exposed[4];
  HDC16 dc;
  int n;
  if (!w) return;
  area = r_isect(lpRect ? *(RECT16 *)LIN(lpRect) : client_rect(w), client_rect(w));
  clip = lpClipRect ? r_isect(*(RECT16 *)LIN(lpClipRect), client_rect(w)) : client_rect(w);
  if (r_empty(area)) return;
  dc = user_get_dc(hwnd, NULL);
  if (dc) {
    dst = r_isect(r_offset(area, dx, dy), clip);
    IntersectClipRect(dc, dst.left, dst.top, dst.right, dst.bottom);
    BitBlt(dc, (int16_t)(area.left + dx), (int16_t)(area.top + dy), (int16_t)(area.right - area.left),
           (int16_t)(area.bottom - area.top), dc, area.left, area.top, SRCCOPY);
    DeleteDC(dc);
  }
  if (!lpRect) {
    /* the whole client area: the update region and the child windows move with the contents
     * (UNVERIFIED for Windows 3.1) */
    for (unsigned i = 0; i < w->nupd; i++) w->upd[i] = r_isect(r_offset(w->upd[i], dx, dy), client_rect(w));
    for (UWnd *c = user_wnd(w->child); c; c = user_wnd(c->next)) {
      c->wrect = r_offset(c->wrect, dx, dy);
      c->crect = r_offset(c->crect, dx, dy);
    }
  }
  /* what the move uncovered inside the scrolled area is invalid (exact shape UNVERIFIED) */
  n = r_subtract(r_isect(area, clip), r_offset(area, dx, dy), exposed);
  for (int i = 0; i < n; i++) user_upd_add(w, exposed[i], true);
}
