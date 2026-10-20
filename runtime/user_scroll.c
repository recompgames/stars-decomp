/* Windows 3.1 look, scroll bars (task stars-0z4.9): the state behind SetScrollPos /
 * GetScrollPos / SetScrollRange / GetScrollRange / ShowScrollBar, the drawing of a bar (arrows,
 * dithered trough, thumb) and the tracking loop that sends WM_HSCROLL / WM_VSCROLL with the SB_*
 * codes (arrow auto-repeat, page clicks, thumb tracking). The SCROLLBAR control (stars-0z4.10)
 * reuses user_sb_draw and user_sb_track with SB_CTL.
 *
 * Plain C. State lives in UserState.sb[slot][SB_HORZ / SB_VERT / SB_CTL] (arena). Defaults and
 * pixel details are UNVERIFIED against Windows 3.1 (docs/inventory.md section 13, "Windows 3.1
 * look"): range 0..100 and position 0 (Wine's default), thumb rounding (n * travel + d / 2) / d,
 * thumb size SM_CYVTHUMB / SM_CXHTHUMB (16), trough = dither of COLOR_SCROLLBAR and
 * COLOR_BTNHIGHLIGHT with white where (screen x + y) is even, auto-repeat 200 ms then 50 ms,
 * the thumb snaps back when the pointer is more than 64 pixels away from the bar. */
#include <string.h>

#include "runtime/user_internal.h"

#define SM(i) GetSystemMetrics(i)
#define COL(i) user_syscolor(i)

/* ---- state ---- */

static int slot_of(HWND16 h) {
  UserState *st = user_state();
  if (!st || h < UHWND_BASE || ((h - UHWND_BASE) % UHWND_STEP)) return -1;
  unsigned i = (h - UHWND_BASE) / UHWND_STEP;
  return i < UW_MAX ? (int)i : -1;
}

UScroll *user_sb_get(HWND16 h, int bar) {
  UserState *st = user_state();
  int i = slot_of(h);
  UScroll *s;
  if (!st || i < 0 || !user_wnd(h) || bar < 0 || bar > 2) return NULL;
  s = &st->sb[i][bar];
  if (!s->inited) {
    s->min = 0;
    s->max = 100;
    s->pos = 0;
    s->inited = 1;
  }
  return s;
}

void user_sb_reset(HWND16 h) {
  UserState *st = user_state();
  int i = slot_of(h);
  if (st && i >= 0) memset(st->sb[i], 0, sizeof st->sb[i]);
}

/* ---- geometry ---- */

typedef struct SbG {
  int len, arrow, trough_len, thumb, travel;
  bool enabled;
} SbG;

static void geom(RECT16 bar, bool vert, const UScroll *s, SbG *g) {
  g->len = vert ? bar.bottom - bar.top : bar.right - bar.left;
  g->arrow = SM(vert ? 20 : 21);
  g->thumb = SM(vert ? 9 : 10);
  if (g->len < 2 * g->arrow) g->arrow = g->len / 2;
  g->trough_len = g->len - 2 * g->arrow;
  g->travel = g->trough_len - g->thumb;
  g->enabled = s->max > s->min;
}

static int thumb_off(const SbG *g, const UScroll *s, int pos) {
  int d = s->max - s->min;
  if (g->travel <= 0 || d <= 0) return 0;
  return ((pos - s->min) * g->travel + d / 2) / d;
}

static int pos_from_off(const SbG *g, const UScroll *s, int off) {
  int d = s->max - s->min;
  if (g->travel <= 0) return s->min;
  if (off < 0) off = 0;
  if (off > g->travel) off = g->travel;
  return s->min + (off * d + g->travel / 2) / g->travel;
}

static RECT16 axis_rect(RECT16 bar, bool vert, int a, int b) {
  return vert ? r_make(bar.left, bar.top + a, bar.right, bar.top + b) : r_make(bar.left + a, bar.top, bar.left + b, bar.bottom);
}

/* ---- drawing ---- */

static void hline(HDC16 dc, int x0, int x1, int y, uint32_t c) { user_fill(dc, r_make(x0, y, x1 + 1, y + 1), c); }
static void vline(HDC16 dc, int x, int y0, int y1, uint32_t c) { user_fill(dc, r_make(x, y0, x + 1, y1 + 1), c); }

/* Arrow / thumb button: black outline, white highlight inside top and left, shadow inside bottom
 * and right, COLOR_BTNFACE; pressed: shadow inside top and left only. */
static void sb_button(HDC16 dc, RECT16 r, bool pressed) {
  int l = r.left, t = r.top, rr = r.right, b = r.bottom;
  if (r_empty(r)) return;
  user_fill(dc, r, COL(15));
  hline(dc, l, rr - 1, t, COL(6));
  hline(dc, l, rr - 1, b - 1, COL(6));
  vline(dc, l, t, b - 1, COL(6));
  vline(dc, rr - 1, t, b - 1, COL(6));
  if (!pressed) {
    hline(dc, l + 1, rr - 3, t + 1, COL(20));
    vline(dc, l + 1, t + 1, b - 3, COL(20));
    hline(dc, l + 1, rr - 2, b - 2, COL(16));
    vline(dc, rr - 2, t + 1, b - 2, COL(16));
  } else {
    hline(dc, l + 1, rr - 2, t + 1, COL(16));
    vline(dc, l + 1, t + 1, b - 2, COL(16));
  }
}

static void dither(HDC16 dc, RECT16 r, int parity) {
  for (int y = r.top; y < r.bottom; y++)
    for (int x = r.left; x < r.right; x++) SetPixel(dc, (int16_t)x, (int16_t)y, ((x + y + parity) & 1) == 0 ? COL(20) : COL(0));
}

/* pressed_part: 1 = first arrow, 2 = second arrow. thumb_pos is the position the thumb is drawn
 * at (the stored one, or the tracked one while dragging). */
void user_sb_draw(HDC16 dc, RECT16 bar, bool vert, const UScroll *s, bool enabled, int pressed_part, int thumb_pos,
                  int parity) {
  SbG g;
  RECT16 a1, a2, tr;
  uint32_t gc1 = (enabled && !(s->arrows & 1)) ? COL(18) : COL(16), gc2 = (enabled && !(s->arrows & 2)) ? COL(18) : COL(16);
  int off;
  geom(bar, vert, s, &g);
  a1 = axis_rect(bar, vert, 0, g.arrow);
  a2 = axis_rect(bar, vert, g.len - g.arrow, g.len);
  tr = axis_rect(bar, vert, g.arrow, g.arrow + g.trough_len);
  sb_button(dc, a1, pressed_part == 1);
  sb_button(dc, a2, pressed_part == 2);
  if (g.arrow >= 16) {
    if (vert) {
      user_tri_up(dc, a1.left + 5, a1.top + 6, gc1);
      user_tri_down(dc, a2.left + 5, a2.top + 6, gc2);
    } else {
      user_tri_left(dc, a1.left + 6, a1.top + 5, gc1);
      user_tri_right(dc, a2.left + 6, a2.top + 5, gc2);
    }
  }
  if (enabled && g.travel >= 0 && g.trough_len >= g.thumb) {
    RECT16 th, before, after;
    off = thumb_off(&g, s, thumb_pos < s->min ? s->min : thumb_pos > s->max ? s->max : thumb_pos);
    th = axis_rect(bar, vert, g.arrow + off, g.arrow + off + g.thumb);
    before = axis_rect(bar, vert, g.arrow, g.arrow + off);
    after = axis_rect(bar, vert, g.arrow + off + g.thumb, g.arrow + g.trough_len);
    dither(dc, before, parity);
    dither(dc, after, parity);
    sb_button(dc, th, false);
  } else {
    dither(dc, tr, parity);
  }
}

/* Window scroll bar redraw (SetScrollPos, SetScrollRange with bRedraw). Control bars redraw
 * themselves. */
void user_sb_draw_window(HWND16 h, int bar) {
  const UWnd *w = user_wnd(h);
  UNcLayout L;
  const UScroll *s;
  HDC16 dc;
  RECT16 r;
  int ox, oy;
  if (w && bar == SB_CTL) { /* the SCROLLBAR control draws itself (ctl_scroll.c, stars-0z4.10) */
    user_ctl_scroll_redraw(h);
    return;
  }
  if (!w || bar < 0 || bar > 1 || !(w->style & WS_VISIBLE) || !user_visible(h)) return;
  if (!(w->style & (bar ? WS_VSCROLL : WS_HSCROLL))) return;
  user_nc_layout(h, &L);
  r = bar ? L.vscroll : L.hscroll;
  s = user_sb_get(h, bar);
  if (!s || r_empty(r)) return;
  dc = user_get_window_dc(h);
  if (!dc) return;
  user_window_origin(w, &ox, &oy);
  user_sb_draw(dc, r, bar != 0, s, s->max > s->min, 0, s->pos, (ox + oy) & 1);
  ReleaseDC(h, dc);
}

/* ---- API ---- */

int16_t SetScrollPos(HWND16 hwnd, int16_t nBar, int16_t nPos, uint16_t bRedraw) {
  UScroll *s = user_sb_get(hwnd, nBar);
  int16_t old;
  if (!s) return 0;
  old = s->pos;
  if (nPos < s->min) nPos = s->min;
  if (nPos > s->max) nPos = s->max;
  s->pos = nPos;
  if (bRedraw) user_sb_draw_window(hwnd, nBar);
  return old;
}

int16_t GetScrollPos(HWND16 hwnd, int16_t nBar) {
  UScroll *s = user_sb_get(hwnd, nBar);
  return s ? s->pos : 0;
}

void ShowScrollBar(HWND16 hwnd, int16_t wBar, uint16_t bShow);

void SetScrollRange(HWND16 hwnd, int16_t nBar, int16_t nMinPos, int16_t nMaxPos, uint16_t bRedraw) {
  UScroll *s = user_sb_get(hwnd, nBar);
  if (!s) return;
  if ((int)nMaxPos - (int)nMinPos > 32767) return; /* the SDK documents this limit */
  if (nMaxPos < nMinPos) return;
  s->min = nMinPos;
  s->max = nMaxPos;
  if (s->pos < s->min) s->pos = s->min;
  if (s->pos > s->max) s->pos = s->max;
  /* A window bar is shown when min < max and hidden when min == max (Wine SCROLL_SetScrollInfo, bars
   * other than SB_CTL; UNVERIFIED against Windows 3.1). A change of the style bit re-runs the
   * non-client calculation, so the client area changes. */
  if (nBar == SB_HORZ || nBar == SB_VERT) {
    const uint32_t bit = nBar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
    const UWnd *w = user_wnd(hwnd);
    const bool want = s->min < s->max;
    if (w && !!(w->style & bit) != want) ShowScrollBar(hwnd, nBar, want);
  }
  if (bRedraw) user_sb_draw_window(hwnd, nBar);
}

void GetScrollRange(HWND16 hwnd, int16_t nBar, FARPTR lpMinPos, FARPTR lpMaxPos) {
  UScroll *s = user_sb_get(hwnd, nBar);
  if (lpMinPos && mem_check(lpMinPos, 2)) *(int16_t *)LIN(lpMinPos) = s ? s->min : 0;
  if (lpMaxPos && mem_check(lpMaxPos, 2)) *(int16_t *)LIN(lpMaxPos) = s ? s->max : 0;
}

void ShowScrollBar(HWND16 hwnd, int16_t wBar, uint16_t bShow) {
  UWnd *w = user_wnd(hwnd);
  uint32_t bits = 0;
  if (!w) return;
  if (wBar == SB_CTL) {
    ShowWindow(hwnd, bShow ? SW_SHOWNA : SW_HIDE);
    return;
  }
  if (wBar == SB_HORZ || wBar == SB_BOTH) bits |= WS_HSCROLL;
  if (wBar == SB_VERT || wBar == SB_BOTH) bits |= WS_VSCROLL;
  if (!bits) return;
  w->style = bShow ? (w->style | bits) : (w->style & ~bits);
  SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

/* ---- tracking ---- */

enum { P_NONE = 0, P_UP, P_DOWN, P_PAGEUP, P_PAGEDOWN, P_THUMB };

typedef struct Trk {
  HWND16 notify, ctl;
  int bar;
  bool vert;
  RECT16 bar_screen;
} Trk;

static int coord(const Trk *t, int x, int y, int *perp_dist) {
  int c = t->vert ? y - t->bar_screen.top : x - t->bar_screen.left;
  int p = t->vert ? x : y, p0 = t->vert ? t->bar_screen.left : t->bar_screen.top, p1 = t->vert ? t->bar_screen.right : t->bar_screen.bottom;
  *perp_dist = p < p0 ? p0 - p : p >= p1 ? p - p1 + 1 : 0;
  return c;
}

static int part_at(const SbG *g, const UScroll *s, int c, int perp, int pos) {
  int off;
  if (!g->enabled || perp > 0 || c < 0 || c >= g->len) return P_NONE;
  if (c < g->arrow) return (s->arrows & 1) ? P_NONE : P_UP; /* a disabled arrow (SBM_ENABLE_ARROWS) does nothing */
  if (c >= g->len - g->arrow) return (s->arrows & 2) ? P_NONE : P_DOWN;
  if (g->travel <= 0) return P_NONE;
  off = g->arrow + thumb_off(g, s, pos);
  if (c >= off && c < off + g->thumb) return P_THUMB;
  return c < off ? P_PAGEUP : P_PAGEDOWN;
}

static void send_sb(const Trk *t, uint16_t code, int pos) {
  if (!user_wnd(t->notify)) return;
  SendMessage(t->notify, t->vert ? WM_VSCROLL : WM_HSCROLL, code, make_long((uint16_t)pos, t->ctl));
}

static void redraw(const Trk *t, int pressed, int thumb_pos) {
  HWND16 dh = t->ctl ? t->ctl : t->notify;
  const UWnd *w = user_wnd(dh);
  const UScroll *s = user_sb_get(dh, t->bar);
  HDC16 dc;
  int ox, oy;
  if (!w || !s) return;
  dc = t->ctl ? user_get_dc(dh, NULL) : user_get_window_dc(dh);
  if (!dc) return;
  if (t->ctl) user_client_origin(w, &ox, &oy);
  else user_window_origin(w, &ox, &oy);
  user_sb_draw(dc, r_offset(t->bar_screen, -ox, -oy), t->vert, s, s->max > s->min, pressed, thumb_pos, (ox + oy) & 1);
  ReleaseDC(dh, dc);
}

void user_sb_track(HWND16 notify, HWND16 ctl, int bar, RECT16 bar_screen, bool vert, int sx, int sy) {
  Trk t = {notify, ctl, bar, vert, bar_screen};
  HWND16 dh = ctl ? ctl : notify;
  UScroll *s = user_sb_get(dh, bar);
  SbG g;
  UPump pu;
  MSG16 m;
  int perp, c, part0, grab = 0, tpos, last, pos0;
  bool active = true, thumb_moved = false;
  uint16_t code = 0;
  if (!s) return;
  geom(bar_screen, vert, s, &g);
  c = coord(&t, sx, sy, &perp);
  pos0 = tpos = last = s->pos;
  part0 = part_at(&g, s, c, perp, s->pos);
  if (part0 == P_NONE) return;
  if (!user_pump_open(&pu)) return;
  SetCapture(dh);
  if (part0 == P_THUMB) {
    grab = c - (g.arrow + thumb_off(&g, s, s->pos));
  } else {
    code = part0 == P_UP ? SB_LINEUP : part0 == P_DOWN ? SB_LINEDOWN : part0 == P_PAGEUP ? SB_PAGEUP : SB_PAGEDOWN;
    send_sb(&t, code, s->pos);
    if (user_wnd(dh)) redraw(&t, part0 == P_UP ? 1 : part0 == P_DOWN ? 2 : 0, s->pos);
    SetTimer(notify, USER_SB_TIMER_ID, 200, 0);
  }
  while (user_wnd(notify) && user_wnd(dh) && user_pump_get(&pu, &m)) {
    if (m.message == WM_MOUSEMOVE || m.message == WM_LBUTTONUP) {
      c = coord(&t, m.pt.x, m.pt.y, &perp);
      if (part0 == P_THUMB) {
        bool snap = perp > 4 * SM(2);
        tpos = snap ? pos0 : pos_from_off(&g, s, c - grab - g.arrow);
        if (tpos != last) {
          last = tpos;
          thumb_moved = true;
          send_sb(&t, SB_THUMBTRACK, tpos);
          if (user_wnd(dh)) redraw(&t, 0, s->pos == tpos ? tpos : tpos);
        }
        if (m.message == WM_LBUTTONUP) {
          send_sb(&t, SB_THUMBPOSITION, tpos);
          break;
        }
      } else {
        bool on = part_at(&g, s, c, perp, s->pos) == part0;
        if (on != active) {
          active = on;
          redraw(&t, on ? (part0 == P_UP ? 1 : part0 == P_DOWN ? 2 : 0) : 0, s->pos);
        }
        if (m.message == WM_LBUTTONUP) break;
      }
    } else if (m.message == WM_TIMER && m.wParam == USER_SB_TIMER_ID && m.hwnd == notify) {
      if (part0 != P_THUMB && active) {
        send_sb(&t, code, s->pos);
        geom(bar_screen, vert, s, &g);
        SetTimer(notify, USER_SB_TIMER_ID, 50, 0);
      }
    } else if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) {
      if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) break;
    } else {
      user_pump_dispatch(&pu);
    }
  }
  user_pump_close(&pu);
  KillTimer(notify, USER_SB_TIMER_ID);
  if (GetCapture() == dh) ReleaseCapture();
  (void)thumb_moved;
  if (user_wnd(notify) && user_wnd(dh)) {
    s = user_sb_get(dh, bar);
    send_sb(&t, SB_ENDSCROLL, s ? s->pos : 0);
    if (user_wnd(dh)) redraw(&t, 0, s ? s->pos : 0);
  }
}
