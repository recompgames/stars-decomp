/* Windows 3.1 look, non-client area (task stars-0z4.9): WM_NCCALCSIZE, WM_NCHITTEST,
 * WM_NCPAINT, WM_NCACTIVATE, WM_NCLBUTTONDOWN, WM_SYSCOMMAND, the caption with its control-menu
 * box and minimise / maximise / restore buttons, the frames (WS_BORDER, WS_DLGFRAME, DS_MODALFRAME,
 * WS_THICKFRAME), the XOR-outline move and size loops, minimise / maximise geometry, window DCs
 * and the desktop background. Plain C, no harness dependency.
 *
 * Source status (owner amendment to decision 8, 2026-10-17): no Windows 3.11 file exists, so
 * everything is drawn in code to the Windows 3.1 look from GetSystemMetrics and GetSysColor.
 * Structure follows the metrics; pixel details (bevel, tick marks, glyph shapes) are this
 * runtime's reading of the Windows 3.1 look and every one is UNVERIFIED against Windows 3.1
 * (docs/inventory.md section 13, "Windows 3.1 look"). Wine (dlls/win32u/defwnd.c, nonclient.c
 * commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0) is the behavioural reference for the hit-test
 * order and the frame predicates (HAS_THICKFRAME / HAS_DLGFRAME).
 *
 * Window layout (window coordinates, F = frame width, CAP = SM_CYCAPTION):
 *   outline          1 pixel, COLOR_WINDOWFRAME, around the window
 *   frame            F pixels in all: outline + (F-1) pixels COLOR_ACTIVEBORDER / INACTIVEBORDER,
 *                    with sizing tick marks SM_CXSIZE from each corner for WS_THICKFRAME
 *   caption          bar (F,F)-(W-F,F+CAP-1) + a black separator line below it
 *   control-menu box SM_CXSIZE wide at the left, minimise and maximise buttons SM_CXSIZE wide at
 *                    the right, the title centred in what is left
 *   client           inside the frame and below the caption, minus the window scroll bars */
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define SM(i) GetSystemMetrics(i)
#define COL(i) user_syscolor(i)

bool (*user_sysmenu_hook)(HWND16 hwnd, int16_t x, int16_t y);

/* ---- layout ---- */

void user_nc_layout_for(uint32_t style, uint32_t ex, int w, int h, UNcLayout *L) { user_nc_layout_for_menu(style, ex, false, w, h, L); }

void user_nc_layout_for_menu(uint32_t style, uint32_t ex, bool has_menu, int w, int h, UNcLayout *L) {
  int F, cap = 0, cx, cy;
  bool vsb, hsb;
  memset(L, 0, sizeof *L);
  L->window = r_make(0, 0, w, h);
  if (style & WS_MINIMIZE) {
    L->iconic = 1;
    L->client = r_make(0, 0, 0, 0);
    return;
  }
  /* Wine's predicates: dlgframe.c HAS_DLGFRAME / HAS_THICKFRAME */
  L->thick = (style & WS_THICKFRAME) && ((style & (WS_DLGFRAME | WS_BORDER)) != WS_DLGFRAME);
  L->dlg = (((style & WS_DLGFRAME) && !(style & WS_BORDER)) || (ex & WS_EX_DLGMODALFRAME)) && !L->thick;
  L->border = (style & WS_BORDER) != 0;
  L->caption = (style & WS_CAPTION) == WS_CAPTION;
  F = L->thick ? SM(32) : L->dlg ? SM(7) : L->border ? SM(5) : 0;
  L->frame = (int16_t)F;
  if (L->caption) {
    cap = SM(4);
    L->capbar = r_make(F, F, w - F, F + cap - 1);
    if (style & WS_SYSMENU) L->sysbox = r_make(F, F, F + SM(30), F + cap - 1);
    {
      int x = L->capbar.right, bw = SM(30);
      if (style & WS_MAXIMIZEBOX) {
        L->maxbtn = r_make(x - bw, F, x, F + cap - 1);
        x -= bw;
      }
      if (style & WS_MINIMIZEBOX) {
        L->minbtn = r_make(x - bw, F, x, F + cap - 1);
        x -= bw;
      }
      L->title = r_make(L->sysbox.right ? L->sysbox.right : L->capbar.left, F, x, F + cap - 1);
    }
  }
  L->client = r_make(F, F + cap, w - F, h - F);
  if (has_menu && !(style & WS_CHILD)) { /* the menu bar sits between the caption and the client area; SM_CYMENU high (UNVERIFIED: 19) */
    int mb = SM(15);
    L->menubar = r_make(F, F + cap, w - F, F + cap + mb);
    L->client.top = (int16_t)(F + cap + mb);
  }
  vsb = (style & WS_VSCROLL) != 0;
  hsb = (style & WS_HSCROLL) != 0;
  cx = SM(2);
  cy = SM(3);
  if (vsb) L->vscroll = r_make(L->client.right - cx, L->client.top, L->client.right, L->client.bottom - (hsb ? cy : 0));
  if (hsb) L->hscroll = r_make(L->client.left, L->client.bottom - cy, L->client.right - (vsb ? cx : 0), L->client.bottom);
  if (vsb && hsb) L->sizebox = r_make(L->client.right - cx, L->client.bottom - cy, L->client.right, L->client.bottom);
  if (vsb) L->client.right = (int16_t)(L->client.right - cx);
  if (hsb) L->client.bottom = (int16_t)(L->client.bottom - cy);
  if (L->client.right < L->client.left) L->client.right = L->client.left;
  if (L->client.bottom < L->client.top) L->client.bottom = L->client.top;
}

/* An iconic window whose class has no icon paints its iconic state itself (the game's "starsframe":
 * BeginPaint + DrawIcon(hdc, 2, 2, ...)), so it gets an icon-sized client area at the top centre of
 * the icon slot. ICON_CLIENT = 32x32 icon + 2 px margin: UNVERIFIED size (stars-0z4.9 item 2). */
#define ICON_CLIENT 36

static bool class_has_icon(const UWnd *w) {
  UserState *st = user_state();
  return st && st->c[w->cls].hicon != 0;
}

static void icon_client(const UWnd *w, int W, int H, UNcLayout *L) {
  int x;
  if (!L->iconic || class_has_icon(w)) return;
  x = W > ICON_CLIENT ? (W - ICON_CLIENT) / 2 : 0;
  L->client = r_make(x, 0, x + (W < ICON_CLIENT ? W : ICON_CLIENT), H < ICON_CLIENT ? H : ICON_CLIENT);
}

static void layout_of(const UWnd *w, UNcLayout *L) {
  int W = w->wrect.right - w->wrect.left, H = w->wrect.bottom - w->wrect.top;
  user_nc_layout_for_menu(w->style, w->exstyle, w->hmenu != 0, W, H, L);
  icon_client(w, W, H, L);
}

bool user_nc_layout(HWND16 hwnd, UNcLayout *out) {
  const UWnd *w = user_wnd(hwnd);
  if (!w || hwnd == user_desktop()) return false;
  layout_of(w, out);
  return true;
}

/* ---- WM_NCCALCSIZE ---- */

uint32_t user_nc_calcsize(HWND16 h, uint16_t wp, uint32_t lp) {
  const UWnd *w = user_wnd(h);
  RECT16 *r;
  UNcLayout L;
  if (!w || !lp) return 0;
  r = (RECT16 *)LIN(lp); /* NCCALCSIZE_PARAMS16 starts with rgrc[0] */
  (void)wp;
  user_nc_layout_for_menu(w->style, w->exstyle, w->hmenu != 0, r->right - r->left, r->bottom - r->top, &L);
  icon_client(w, r->right - r->left, r->bottom - r->top, &L);
  *r = r_make(r->left + L.client.left, r->top + L.client.top, r->left + L.client.right, r->top + L.client.bottom);
  return 0;
}

void AdjustWindowRect(FARPTR lpRect, uint32_t dwStyle, uint16_t bMenu) {
  RECT16 *r;
  UNcLayout L;
  if (!lpRect || !mem_check(lpRect, sizeof(RECT16))) return;
  r = LIN(lpRect);
  /* scroll bars are not part of the adjustment (SDK); the menu bar is (SM_CYMENU, UNVERIFIED), and
   * WM_NCCALCSIZE takes the same height out of the client area (user_nc_layout_for_menu) */
  user_nc_layout_for(dwStyle & ~(uint32_t)(WS_HSCROLL | WS_VSCROLL | WS_MINIMIZE), 0, 1000, 1000, &L);
  r->left = (int16_t)(r->left - L.client.left);
  r->top = (int16_t)(r->top - L.client.top - (bMenu ? SM(15) : 0));
  r->right = (int16_t)(r->right + 1000 - L.client.right);
  r->bottom = (int16_t)(r->bottom + 1000 - L.client.bottom);
}

/* ---- hit test ---- */

int user_nc_hittest(HWND16 h, int sx, int sy) {
  const UWnd *w = user_wnd(h);
  UNcLayout L;
  int ox, oy, x, y, W, H, F, SZ;
  if (!w || h == user_desktop()) return HTNOWHERE;
  user_window_origin(w, &ox, &oy);
  W = w->wrect.right - w->wrect.left;
  H = w->wrect.bottom - w->wrect.top;
  x = sx - ox;
  y = sy - oy;
  if (x < 0 || y < 0 || x >= W || y >= H) return HTNOWHERE;
  layout_of(w, &L);
  if (L.iconic) return HTCAPTION;
  if (r_contains(L.client, x, y)) return HTCLIENT;
  F = L.frame;
  SZ = SM(30);
  if (L.caption && x >= F && x < W - F && y >= F && y < F + SM(4)) {
    if (r_contains(L.sysbox, x, y)) return HTSYSMENU;
    if (r_contains(L.minbtn, x, y)) return HTREDUCE;
    if (r_contains(L.maxbtn, x, y)) return HTZOOM;
    return HTCAPTION;
  }
  if (r_contains(L.menubar, x, y)) return HTMENU;
  if (r_contains(L.vscroll, x, y)) return HTVSCROLL;
  if (r_contains(L.hscroll, x, y)) return HTHSCROLL;
  if (r_contains(L.sizebox, x, y)) return L.thick ? HTGROWBOX : HTNOWHERE;
  if (F == 0) return HTCLIENT;
  if (L.thick) {
    if (x < F) return y < SZ ? HTTOPLEFT : y >= H - SZ ? HTBOTTOMLEFT : HTLEFT;
    if (x >= W - F) return y < SZ ? HTTOPRIGHT : y >= H - SZ ? HTBOTTOMRIGHT : HTRIGHT;
    if (y < F) return x < SZ ? HTTOPLEFT : x >= W - SZ ? HTTOPRIGHT : HTTOP;
    if (y >= H - F) return x < SZ ? HTBOTTOMLEFT : x >= W - SZ ? HTBOTTOMRIGHT : HTBOTTOM;
  }
  return HTBORDER;
}

/* ---- drawing helpers ---- */

void user_fill(HDC16 dc, RECT16 r, uint32_t color) {
  HBRUSH16 b, old;
  if (r_empty(r)) return;
  b = CreateSolidBrush(color);
  if (!b) return;
  old = SelectObject(dc, b);
  PatBlt(dc, r.left, r.top, (int16_t)(r.right - r.left), (int16_t)(r.bottom - r.top), PATCOPY);
  SelectObject(dc, old);
  DeleteObject(b);
}

static void fillr(HDC16 dc, int l, int t, int r, int b, uint32_t c) { user_fill(dc, r_make(l, t, r, b), c); }
static void hline(HDC16 dc, int x0, int x1, int y, uint32_t c) { fillr(dc, x0, y, x1 + 1, y + 1, c); }  /* x1 inclusive */
static void vline(HDC16 dc, int x, int y0, int y1, uint32_t c) { fillr(dc, x, y0, x + 1, y1 + 1, c); }  /* y1 inclusive */

int user_text_width(HDC16 dc, const char *s) {
  uint16_t hh = 0;
  FARPTR p = user_scratch((uint32_t)strlen(s) + 1, &hh);
  uint32_t e;
  if (!p) return 0;
  strcpy(LIN(p), s);
  e = GetTextExtent(dc, p, (int16_t)strlen(s));
  user_scratch_free(hh);
  return (int)(e & 0xFFFF);
}

int user_text_height(HDC16 dc);
static int text_height(HDC16 dc) {
  uint16_t hh = 0;
  FARPTR p = user_scratch(2, &hh);
  uint32_t e;
  if (!p) return 8;
  strcpy(LIN(p), "H");
  e = GetTextExtent(dc, p, 1);
  user_scratch_free(hh);
  return (int)(e >> 16);
}

int user_text_height(HDC16 dc) { return text_height(dc); }

void user_draw_text(HDC16 dc, int x, int y, const char *s) {
  uint16_t hh = 0;
  FARPTR p;
  if (!s[0]) return;
  p = user_scratch((uint32_t)strlen(s) + 1, &hh);
  if (!p) return;
  strcpy(LIN(p), s);
  TextOut(dc, (int16_t)x, (int16_t)y, p, (int16_t)strlen(s));
  user_scratch_free(hh);
}

/* Triangle glyph helpers (black, or BTNSHADOW when disabled). Rows k = 0..3. */
static void tri_up(HDC16 dc, int x0, int y0, uint32_t c) {
  for (int k = 0; k < 4; k++) hline(dc, x0 + 3 - k, x0 + 3 + k, y0 + k, c);
}
static void tri_down(HDC16 dc, int x0, int y0, uint32_t c) {
  for (int k = 0; k < 4; k++) hline(dc, x0 + k, x0 + 6 - k, y0 + k, c);
}
void user_tri_left(HDC16 dc, int x0, int y0, uint32_t c) {
  for (int k = 0; k < 4; k++) vline(dc, x0 + k, y0 + 3 - k, y0 + 3 + k, c);
}
void user_tri_right(HDC16 dc, int x0, int y0, uint32_t c) {
  for (int k = 0; k < 4; k++) vline(dc, x0 + k, y0 + k, y0 + 6 - k, c);
}
void user_tri_up(HDC16 dc, int x0, int y0, uint32_t c) { tri_up(dc, x0, y0, c); }
void user_tri_down(HDC16 dc, int x0, int y0, uint32_t c) { tri_down(dc, x0, y0, c); }

/* Caption button (minimise / maximise / restore): black left edge, white highlight along the top
 * and left, COLOR_BTNSHADOW along the bottom and right; pressed: shadow on top and left only. */
static void caption_button(HDC16 dc, RECT16 b, int kind, bool pressed) {
  int l = b.left, t = b.top, r = b.right, bt = b.bottom;
  uint32_t face = COL(15), hi = COL(20), sh = COL(16), blk = COL(6);
  if (r_empty(b)) return;
  user_fill(dc, b, face);
  vline(dc, l, t, bt - 1, blk);
  if (!pressed) {
    hline(dc, l + 1, r - 2, t, hi);
    vline(dc, l + 1, t + 1, bt - 2, hi);
    hline(dc, l + 1, r - 1, bt - 1, sh);
    vline(dc, r - 1, t, bt - 2, sh);
  } else {
    hline(dc, l + 1, r - 1, t, sh);
    vline(dc, l + 1, t + 1, bt - 1, sh);
  }
  switch (kind) {
    case 0: tri_down(dc, l + 5, t + 7, COL(18)); break;             /* minimise */
    case 1: tri_up(dc, l + 5, t + 7, COL(18)); break;               /* maximise */
    default:                                                         /* restore: up over down */
      tri_up(dc, l + 5, t + 4, COL(18));
      tri_down(dc, l + 5, t + 9, COL(18));
      break;
  }
}

/* Control-menu box: black right edge, a white bar with a black outline. */
static void sys_box(HDC16 dc, RECT16 b, bool pressed) {
  int l = b.left, t = b.top, r = b.right, bt = b.bottom;
  uint32_t face = COL(15), hi = COL(20), sh = COL(16), blk = COL(6);
  if (r_empty(b)) return;
  user_fill(dc, b, face);
  vline(dc, r - 1, t, bt - 1, blk);
  if (!pressed) {
    hline(dc, l, r - 3, t, hi);
    vline(dc, l, t + 1, bt - 2, hi);
    hline(dc, l, r - 2, bt - 1, sh);
    vline(dc, r - 2, t, bt - 2, sh);
  } else {
    hline(dc, l, r - 2, t, sh);
    vline(dc, l, t + 1, bt - 1, sh);
  }
  fillr(dc, l + 3, t + 6, l + 13, t + 11, blk);
  fillr(dc, l + 4, t + 7, l + 12, t + 10, COL(20));
}

/* ---- window DC ---- */

HDC16 GetWindowDC(HWND16 hwnd) { return user_get_window_dc(hwnd); }

/* ---- painting ---- */

static HICON16 default_icon(void) { return LoadIcon(0, (FARPTR)IDI_APPLICATION_ID); }

HICON16 user_class_icon_or_default(HWND16 h) {
  UserState *st = user_state();
  const UWnd *w = user_wnd(h);
  if (w && st->c[w->cls].hicon) return st->c[w->cls].hicon;
  return default_icon();
}

#define ICON_H (32 + 4 + 16)

static void paint_iconic(HWND16 h, HDC16 dc, const UWnd *w) {
  int W = w->wrect.right - w->wrect.left, H = w->wrect.bottom - w->wrect.top;
  HICON16 ic = user_class_icon_or_default(h);
  int tw, th, x;
  uint16_t old;
  RECT16 clip;
  if (!class_has_icon(w)) {  /* the application drew its icon in the client; a caption repaint sends no WM_PAINT */
    UNcLayout L;
    layout_of(w, &L);
    ExcludeClipRect(dc, L.client.left, L.client.top, L.client.right, L.client.bottom);
  }
  user_fill(dc, r_make(0, 0, W, H), COL(1));
  if (class_has_icon(w)) user_paint_icon_at(dc, (W - 32) / 2, 2, ic);  /* else the application paints it */
  old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, COL(8));
  tw = user_text_width(dc, w->text);
  th = text_height(dc);
  x = (W - tw) / 2;
  if (x < 0) x = 0;
  clip = r_make(0, 36, W, H);
  IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
  user_draw_text(dc, x, 36 + (H - 36 - th) / 2, w->text);
  SelectObject(dc, old);
}

static void paint_caption(HDC16 dc, const UWnd *w, const UNcLayout *L, bool active, int pressed_hit) {
  uint32_t capc = COL(active ? 2 : 3), txt = COL(active ? 9 : 19);
  if (!L->caption) return;
  user_fill(dc, L->capbar, capc);
  hline(dc, L->capbar.left, L->capbar.right - 1, L->capbar.bottom, COL(6));
  sys_box(dc, L->sysbox, pressed_hit == HTSYSMENU);
  caption_button(dc, L->minbtn, 0, pressed_hit == HTREDUCE);
  caption_button(dc, L->maxbtn, (w->style & WS_MAXIMIZE) ? 2 : 1, pressed_hit == HTZOOM);
  /* title last: it narrows the clip rectangle of the DC */
  if (w->text[0] && !r_empty(L->title)) {
    uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    int tw = user_text_width(dc, w->text), th = text_height(dc), avail = L->title.right - L->title.left, x;
    x = L->title.left + (avail > tw ? (avail - tw) / 2 : 0);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, txt);
    IntersectClipRect(dc, L->title.left, L->title.top, L->title.right, L->title.bottom);
    user_draw_text(dc, x, L->capbar.top + (L->capbar.bottom - L->capbar.top - th) / 2, w->text);
    SelectObject(dc, old);
  }
}

static void paint_frame(HDC16 dc, const UNcLayout *L, bool active) {
  int W = L->window.right, H = L->window.bottom, F = L->frame, SZ = SM(30);
  uint32_t blk = COL(6), band = COL(active ? 10 : 11);
  if (F <= 0) return;
  hline(dc, 0, W - 1, 0, blk);
  hline(dc, 0, W - 1, H - 1, blk);
  vline(dc, 0, 1, H - 2, blk);
  vline(dc, W - 1, 1, H - 2, blk);
  if (F > 1) {
    fillr(dc, 1, 1, W - 1, F, band);                 /* top */
    fillr(dc, 1, H - F, W - 1, H - 1, band);         /* bottom */
    fillr(dc, 1, F, F, H - F, band);                 /* left */
    fillr(dc, W - F, F, W - 1, H - F, band);         /* right */
  }
  if (L->thick) {                                    /* sizing tick marks */
    int xs[2] = {SZ, W - SZ}, ys[2] = {SZ, H - SZ};
    for (int i = 0; i < 2; i++) {
      vline(dc, xs[i], 1, F - 1, blk);
      vline(dc, xs[i], H - F, H - 2, blk);
      hline(dc, 1, F - 1, ys[i], blk);
      hline(dc, W - F, W - 2, ys[i], blk);
    }
  }
}

static void paint_window(HWND16 h, bool caption_only, int pressed_hit) {
  const UWnd *w = user_wnd(h);
  UNcLayout L;
  HDC16 dc;
  bool active;
  if (!w || h == user_desktop() || !user_visible(h)) return;
  dc = user_get_window_dc(h);
  if (!dc) return;
  layout_of(w, &L);
  active = w->ncact != 0;
  if (L.iconic) {
    paint_iconic(h, dc, w);
    ReleaseDC(h, dc);
    return;
  }
  if (!caption_only) {
    paint_frame(dc, &L, active);
    for (int bar = 0; bar < 2; bar++) {
      RECT16 r = bar ? L.vscroll : L.hscroll;
      const UScroll *s = user_sb_get(h, bar ? SB_VERT : SB_HORZ);
      if (!r_empty(r) && s) {
        int ox, oy;
        user_window_origin(w, &ox, &oy);
        user_sb_draw(dc, r, bar != 0, s, s->max > s->min, 0, s->pos, (ox + oy) & 1);
      }
    }
    user_fill(dc, L.sizebox, COL(15));
    if (!r_empty(L.menubar)) user_menu_paint_bar(h, dc, L.menubar);   /* the menu bar (menu_ui.c) */
  }
  paint_caption(dc, w, &L, active, pressed_hit);
  ReleaseDC(h, dc);
}

void user_nc_paint(HWND16 h) { paint_window(h, false, 0); }
void user_nc_paint_caption(HWND16 h) {
  const UWnd *w = user_wnd(h);
  if (!w) return;
  if (w->style & WS_MINIMIZE) paint_window(h, false, 0);
  else paint_window(h, true, 0);
}

static void paint_pressed(HWND16 h, int hit) { paint_window(h, true, hit); }

/* ---- desktop ---- */

static void desktop_fill(RECT16 r) {
  UserState *st = user_state();
  HDC16 dc;
  if (!fb_pixels()) return;
  r = r_isect(r, r_make(0, 0, GDI_SCREEN_W, GDI_SCREEN_H));
  if (r_empty(r)) return;
  dc = gdi_create_screen_dc();
  if (!dc) return;
  gdi_dc_set_hard_clip(dc, r.left, r.top, r.right, r.bottom);
  for (const UWnd *c = user_wnd(st->w[0].child); c; c = user_wnd(c->next))
    if (c->style & WS_VISIBLE)
      ExcludeClipRect(dc, c->wrect.left, c->wrect.top, c->wrect.right, c->wrect.bottom);
  user_fill(dc, r, COL(1));
  DeleteDC(dc);
}

void user_nc_expose(HWND16 h, RECT16 old_wrect) {
  (void)h;
  desktop_fill(old_wrect);
}

void user_paint_desktop(void) {
  UserState *st = user_state();
  if (!st) return;
  desktop_fill(r_make(0, 0, GDI_SCREEN_W, GDI_SCREEN_H));
  for (const UWnd *c = user_wnd(st->w[0].child); c; c = user_wnd(c->next))
    if ((c->style & WS_VISIBLE) && (c->style & WS_MINIMIZE)) paint_window(user_hwnd(c), false, 0);
}

/* ---- minimise / maximise geometry ---- */

static int frame_of(const UWnd *w) {
  UNcLayout L;
  user_nc_layout_for(w->style & ~(uint32_t)(WS_MINIMIZE | WS_MAXIMIZE), w->exstyle, 100, 100, &L);
  return L.frame;
}

/* Rectangle (parent client coordinates) for mode 1 (maximised) or 2 (minimised). */
void user_nc_place(HWND16 h, int mode, RECT16 *out) {
  UserState *st = user_state();
  UWnd *w = user_wnd(h);
  const UWnd *p;
  int pw, ph, F;
  if (!w) return;
  p = user_wnd(w->parent);
  if (!p || w->parent == user_desktop()) {
    pw = GDI_SCREEN_W;
    ph = GDI_SCREEN_H;
  } else {
    pw = p->crect.right - p->crect.left;
    ph = p->crect.bottom - p->crect.top;
  }
  if (mode == 1) {
    uint16_t hm = 0;
    FARPTR mp = user_scratch(sizeof(MINMAXINFO16), &hm);
    int x, y, cx, cy;
    F = frame_of(w);
    x = -F; y = -F; cx = pw + 2 * F; cy = ph + 2 * F;
    if (mp) {
      MINMAXINFO16 *mm = LIN(mp);
      memset(mm, 0, sizeof *mm);
      mm->ptMaxSize = (POINT16){(int16_t)cx, (int16_t)cy};
      mm->ptMaxPosition = (POINT16){(int16_t)x, (int16_t)y};
      mm->ptMinTrackSize = (POINT16){(int16_t)SM(34), (int16_t)SM(35)};
      mm->ptMaxTrackSize = mm->ptMaxSize;
      SendMessage(h, WM_GETMINMAXINFO, 0, mp);
      if (user_wnd(h)) {
        mm = LIN(mp);
        x = mm->ptMaxPosition.x; y = mm->ptMaxPosition.y; cx = mm->ptMaxSize.x; cy = mm->ptMaxSize.y;
      }
      user_scratch_free(hm);
    }
    *out = r_make(x, y, x + cx, y + cy);
    return;
  }
  /* minimised: the first free icon slot along the bottom row (UNVERIFIED arrangement) */
  {
    int iw = SM(38), ih = ICON_H;
    for (int slot = 0; slot < 64; slot++) {
      RECT16 c = r_make(slot * iw, ph - ih, slot * iw + iw, ph);
      bool used = false;
      for (const UWnd *o = user_wnd(st->w[0].child); o; o = user_wnd(o->next)) {
        if (o == w || !(o->style & WS_MINIMIZE)) continue;
        if (!r_empty(r_isect(o->wrect, c))) used = true;
      }
      if (!used) {
        *out = c;
        return;
      }
    }
    *out = r_make(0, ph - ih, iw, ph);
  }
}

/* ---- the modal loops ---- */

bool user_pump_open(UPump *pu) {
  pu->p = user_scratch(sizeof(MSG16), &pu->hnd);
  return pu->p != 0;
}
void user_pump_close(UPump *pu) { user_scratch_free(pu->hnd); }

/* 1: a message is in *m. 0: the loop must end; a real WM_QUIT is posted again for the
 * application's own loop (running out of input is not a quit). */
int user_pump_get(UPump *pu, MSG16 *m) {
  int was = user_input_exhausted;
  uint16_t r = GetMessage(pu->p, 0, 0, 0);
  *m = *(MSG16 *)LIN(pu->p);
  if (r) return 1;
  if (!(user_input_exhausted && !was)) PostQuitMessage((int16_t)m->wParam);
  return 0;
}

void user_pump_dispatch(UPump *pu) {
  TranslateMessage(pu->p);
  DispatchMessage(pu->p);
}

static void track_button(HWND16 h, int hit, uint32_t lp) {
  UPump pu;
  MSG16 m;
  bool inside = true, click = false;
  if (!user_pump_open(&pu)) return;
  SetCapture(h);
  paint_pressed(h, hit);
  while (user_pump_get(&pu, &m)) {
    if (!user_wnd(h)) break;
    if (m.message == WM_MOUSEMOVE) {
      bool in = user_nc_hittest(h, m.pt.x, m.pt.y) == hit;
      if (in != inside) {
        inside = in;
        paint_pressed(h, in ? hit : 0);
      }
    } else if (m.message == WM_LBUTTONUP) {
      click = inside;
      break;
    } else if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) {
      if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) break;
    } else {
      user_pump_dispatch(&pu);
    }
  }
  user_pump_close(&pu);
  if (GetCapture() == h) ReleaseCapture();
  if (!user_wnd(h)) return;
  paint_pressed(h, 0);
  if (click) {
    UWnd *w = user_wnd(h);
    uint16_t cmd = hit == HTREDUCE ? SC_MINIMIZE : ((w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE);
    SendMessage(h, WM_SYSCOMMAND, cmd, lp);
  }
}

static void xor_outline(HDC16 dc, RECT16 r, int th) {
  int w = r.right - r.left, h = r.bottom - r.top;
  if (w <= 0 || h <= 0) return;
  if (th * 2 > h) th = h / 2;
  if (th * 2 > w) th = w / 2;
  if (th < 1) th = 1;
  PatBlt(dc, r.left, r.top, (int16_t)w, (int16_t)th, DSTINVERT);
  PatBlt(dc, r.left, (int16_t)(r.bottom - th), (int16_t)w, (int16_t)th, DSTINVERT);
  PatBlt(dc, r.left, (int16_t)(r.top + th), (int16_t)th, (int16_t)(h - 2 * th), DSTINVERT);
  PatBlt(dc, (int16_t)(r.right - th), (int16_t)(r.top + th), (int16_t)th, (int16_t)(h - 2 * th), DSTINVERT);
}

static void commit_rect(HWND16 h, RECT16 scr, bool size) {
  UWnd *w = user_wnd(h);
  int ox = 0, oy = 0;
  uint16_t flags = SWP_NOZORDER | SWP_NOACTIVATE;
  if (!w) return;
  if (w->parent != user_desktop()) {
    const UWnd *p = user_wnd(w->parent);
    if (p) user_client_origin(p, &ox, &oy);
  }
  if (!size) flags |= SWP_NOSIZE;
  SetWindowPos(h, 0, (int16_t)(scr.left - ox), (int16_t)(scr.top - oy), (int16_t)(scr.right - scr.left),
               (int16_t)(scr.bottom - scr.top), flags);
}

/* Move (size == 0) or size (wmsz 1..8) with an XOR outline. lp is the start point (screen). */
static void drag_loop(HWND16 h, uint32_t lp, int wmsz, bool size) {
  UPump pu;
  MSG16 m;
  UWnd *w = user_wnd(h);
  int sx = (int16_t)(lp & 0xFFFF), sy = (int16_t)(lp >> 16), ox, oy, th, minw = 0, minh = 0;
  RECT16 start, cur;
  HDC16 dc;
  bool commit = false, drawn = false;
  if (!w || !lp) return;
  if (!user_pump_open(&pu)) return;
  user_window_origin(w, &ox, &oy);
  start = r_make(ox, oy, ox + (w->wrect.right - w->wrect.left), oy + (w->wrect.bottom - w->wrect.top));
  cur = start;
  th = (w->style & WS_THICKFRAME) ? SM(32) - 1 : 1;
  if (size) {
    uint16_t hm = 0;
    FARPTR mp = user_scratch(sizeof(MINMAXINFO16), &hm);
    minw = SM(34);
    minh = SM(35);
    if (mp) {
      MINMAXINFO16 *mm = LIN(mp);
      memset(mm, 0, sizeof *mm);
      mm->ptMinTrackSize = (POINT16){(int16_t)minw, (int16_t)minh};
      mm->ptMaxTrackSize = (POINT16){GDI_SCREEN_W + 8, GDI_SCREEN_H + 8};
      SendMessage(h, WM_GETMINMAXINFO, 0, mp);
      mm = LIN(mp);
      minw = mm->ptMinTrackSize.x;
      minh = mm->ptMinTrackSize.y;
      user_scratch_free(hm);
    }
  }
  dc = GetDC(0);
  SetCapture(h);
  xor_outline(dc, cur, th);
  drawn = true;
  while (user_pump_get(&pu, &m)) {
    if (!user_wnd(h)) break;
    if (m.message == WM_MOUSEMOVE || m.message == WM_LBUTTONUP) {
      int dx = m.pt.x - sx, dy = m.pt.y - sy;
      RECT16 n = start;
      if (!size) {
        n = r_offset(start, dx, dy);
      } else {
        if (wmsz == WMSZ_LEFT || wmsz == WMSZ_TOPLEFT || wmsz == WMSZ_BOTTOMLEFT) n.left = (int16_t)(start.left + dx);
        if (wmsz == WMSZ_RIGHT || wmsz == WMSZ_TOPRIGHT || wmsz == WMSZ_BOTTOMRIGHT) n.right = (int16_t)(start.right + dx);
        if (wmsz == WMSZ_TOP || wmsz == WMSZ_TOPLEFT || wmsz == WMSZ_TOPRIGHT) n.top = (int16_t)(start.top + dy);
        if (wmsz == WMSZ_BOTTOM || wmsz == WMSZ_BOTTOMLEFT || wmsz == WMSZ_BOTTOMRIGHT) n.bottom = (int16_t)(start.bottom + dy);
        if (n.right - n.left < minw) {
          if (n.left != start.left) n.left = (int16_t)(n.right - minw);
          else n.right = (int16_t)(n.left + minw);
        }
        if (n.bottom - n.top < minh) {
          if (n.top != start.top) n.top = (int16_t)(n.bottom - minh);
          else n.bottom = (int16_t)(n.top + minh);
        }
      }
      if (n.left != cur.left || n.top != cur.top || n.right != cur.right || n.bottom != cur.bottom) {
        xor_outline(dc, cur, th);
        cur = n;
        xor_outline(dc, cur, th);
      }
      if (m.message == WM_LBUTTONUP) {
        commit = true;
        break;
      }
    } else if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) {
      if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) break;
      if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
        commit = true;
        break;
      }
    } else {
      user_pump_dispatch(&pu);
    }
  }
  if (drawn) xor_outline(dc, cur, th);   /* erase: the same raster operation again */
  DeleteDC(dc);
  user_pump_close(&pu);
  if (GetCapture() == h) ReleaseCapture();
  if (commit && user_wnd(h) && (cur.left != start.left || cur.top != start.top || cur.right != start.right || cur.bottom != start.bottom))
    commit_rect(h, cur, size);
}

/* ---- system menu ---- */

unsigned user_sysmenu_items(HWND16 hwnd, USysItem out[10]) {
  const UWnd *w = user_wnd(hwnd);
  bool zoomed, iconic;
  static const struct {
    uint16_t id;
    const char *text;
  } T[9] = {{SC_RESTORE, "&Restore"}, {SC_MOVE, "&Move"}, {SC_SIZE, "&Size"}, {SC_MINIMIZE, "Mi&nimize"},
            {SC_MAXIMIZE, "Ma&ximize"}, {0, ""}, {SC_CLOSE, "&Close\tAlt+F4"}, {0, ""},
            {SC_TASKLIST, "&Switch To...\tCtrl+Esc"}};
  if (!w) return 0;
  zoomed = (w->style & WS_MAXIMIZE) != 0;
  iconic = (w->style & WS_MINIMIZE) != 0;
  for (unsigned i = 0; i < 9; i++) {
    out[i].id = T[i].id;
    strncpy(out[i].text, T[i].text, sizeof out[i].text - 1);
    out[i].text[sizeof out[i].text - 1] = 0;
    switch (T[i].id) {
      case SC_RESTORE: out[i].enabled = (zoomed || iconic); break;
      case SC_MOVE: out[i].enabled = !zoomed; break;
      case SC_SIZE: out[i].enabled = (w->style & WS_THICKFRAME) && !zoomed && !iconic; break;
      case SC_MINIMIZE: out[i].enabled = (w->style & WS_MINIMIZEBOX) && !iconic; break;
      case SC_MAXIMIZE: out[i].enabled = (w->style & WS_MAXIMIZEBOX) && !zoomed; break;
      default: out[i].enabled = T[i].id != 0; break;
    }
    out[i].enabled = out[i].enabled ? 1 : 0;
  }
  return 9;
}

static void open_sysmenu(HWND16 h) {
  UNcLayout L;
  int ox, oy;
  const UWnd *w = user_wnd(h);
  if (!w || !user_sysmenu_hook) return;
  layout_of(w, &L);
  user_window_origin(w, &ox, &oy);
  /* a hook (a host or a test) takes the click; without one WM_SYSCOMMAND SC_MOUSEMENU + HTSYSMENU runs the tracker (menu_ui.c) */
  user_sysmenu_hook(h, (int16_t)(ox + L.sysbox.left), (int16_t)(oy + L.sysbox.bottom));
}

/* ---- default handling ---- */

static void activate_for_click(HWND16 h) {
  const UWnd *w = user_wnd(h);
  if (!w || (w->style & WS_CHILD)) return;
  if (GetActiveWindow() != h) SetActiveWindow(h);
  SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void sys_command(HWND16 h, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  uint16_t cmd = (uint16_t)(wp & 0xFFF0);
  if (!w) return;
  switch (cmd) {
    case SC_CLOSE: SendMessage(h, WM_CLOSE, 0, 0); break;
    case SC_MINIMIZE: ShowWindow(h, SW_MINIMIZE); break;
    case SC_MAXIMIZE: ShowWindow(h, SW_SHOWMAXIMIZED); break;
    case SC_RESTORE: ShowWindow(h, SW_RESTORE); break;
    case SC_MOVE:
      if (!(w->style & WS_MAXIMIZE)) drag_loop(h, lp, 0, false); /* keyboard moving (lp 0): not implemented */
      break;
    case SC_SIZE:
      if ((w->style & WS_THICKFRAME) && !(w->style & (WS_MAXIMIZE | WS_MINIMIZE)) && (wp & 0xF) >= 1 && (wp & 0xF) <= 8)
        drag_loop(h, lp, wp & 0xF, true);
      break;
    case SC_MOUSEMENU: /* defwnd.c 1016-1018 */
      if ((wp & 0xF) == HTSYSMENU && user_sysmenu_hook) open_sysmenu(h);
      else user_menu_track_mouse(h, wp & 0xF, (int16_t)(lp & 0xFFFF), (int16_t)(lp >> 16));
      break;
    case SC_KEYMENU: /* defwnd.c 1020-1022: lp is the character, 0 for a bare Alt or F10, ' ' for Alt+Space */
      user_menu_track_keyboard(h, wp, (uint16_t)(lp & 0xFFFF));
      break;
    default: break;
  }
}

uint32_t user_nc_def_message(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  if (!w) return 0;
  switch (msg) {
    case WM_NCCALCSIZE: return user_nc_calcsize(h, wp, lp);
    case WM_NCHITTEST: return (uint32_t)(uint16_t)(int16_t)user_nc_hittest(h, (int16_t)(lp & 0xFFFF), (int16_t)(lp >> 16));
    case WM_NCPAINT:
      w->ncdirty = 0;
      user_nc_paint(h);
      return 0;
    case WM_NCACTIVATE:
      w->ncact = wp ? 1 : 0;
      if (w->style & WS_VISIBLE) user_nc_paint_caption(h);
      return 1;
    case WM_NCLBUTTONDOWN: {
      int hit = (int16_t)wp;
      switch (hit) {
        case HTCAPTION:
          activate_for_click(h);
          SendMessage(h, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, lp);
          break;
        case HTSYSMENU:
          activate_for_click(h);
          if (w->style & WS_SYSMENU) SendMessage(h, WM_SYSCOMMAND, SC_MOUSEMENU + HTSYSMENU, lp); /* defwnd.c 2223 */
          break;
        case HTMENU:
          activate_for_click(h);
          SendMessage(h, WM_SYSCOMMAND, SC_MOUSEMENU, lp); /* defwnd.c 2227-2228 */
          break;
        case HTREDUCE:
        case HTZOOM:
          activate_for_click(h);
          track_button(h, hit, lp);
          break;
        case HTHSCROLL:
        case HTVSCROLL: {
          UNcLayout L;
          int ox, oy;
          RECT16 bar;
          layout_of(w, &L);
          user_window_origin(w, &ox, &oy);
          bar = r_offset(hit == HTVSCROLL ? L.vscroll : L.hscroll, ox, oy);
          user_sb_track(h, 0, hit == HTVSCROLL ? SB_VERT : SB_HORZ, bar, hit == HTVSCROLL, (int16_t)(lp & 0xFFFF), (int16_t)(lp >> 16));
          break;
        }
        default:
          if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT) {
            activate_for_click(h);
            SendMessage(h, WM_SYSCOMMAND, (uint16_t)(SC_SIZE + (hit - HTLEFT + 1)), lp);
          }
          break;
      }
      return 0;
    }
    case WM_NCLBUTTONDBLCLK: {
      int hit = (int16_t)wp;
      if (w->style & WS_MINIMIZE) {
        SendMessage(h, WM_SYSCOMMAND, SC_RESTORE, lp);
      } else if (hit == HTCAPTION) {
        if (w->style & WS_MAXIMIZE) SendMessage(h, WM_SYSCOMMAND, SC_RESTORE, lp);
        else if (w->style & WS_MAXIMIZEBOX) SendMessage(h, WM_SYSCOMMAND, SC_MAXIMIZE, lp);
      } else if (hit == HTSYSMENU) {
        if (w->style & WS_SYSMENU) SendMessage(h, WM_SYSCOMMAND, SC_CLOSE, lp);
      } else {
        /* any other part (buttons, frame, scroll bars): a double click acts as a second button
         * press (Wine NC_HandleNCLButtonDblClk falls through to NC_HandleNCLButtonDown) */
        return user_nc_def_message(h, WM_NCLBUTTONDOWN, wp, lp);
      }
      return 0;
    }
    case WM_SYSCOMMAND: sys_command(h, wp, lp); return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
      /* Alt / F10 state, Alt+F4, Alt+letter, Alt+Space: defwnd.c 2756-2815 (menu_ui.c keeps menu_sys_key and f10_key) */
      return user_menu_key_event(h, msg, wp, lp);
    default: return 0;
  }
}
