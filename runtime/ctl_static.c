/* The STATIC control class (task stars-0z4.10): left, centred and right aligned text, simple and
 * non-wrapping text, icons, and filled rectangles and frames. Wine's static control
 * (dlls/user32/static.c at commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0, lines cited in
 * runtime/tests/test_ctl_static.c) is the behavioural reference; all of it is UNVERIFIED for
 * Windows 3.1 (docs/inventory.md section 15, "Standard controls part 1").
 *
 * The control paints its whole client area itself: the background is the brush the parent returns
 * for WM_CTLCOLOR (CTLCOLOR_STATIC), then the content. State: extra bytes (font, icon).
 * SS_BLACKRECT/FRAME use COLOR_WINDOWFRAME, SS_GRAYRECT/FRAME COLOR_BACKGROUND and
 * SS_WHITERECT/FRAME COLOR_WINDOW (the Windows 3.1 SDK wording as recalled; Wine uses its 3-D
 * colours instead). Not implemented: SS_OWNERDRAW, SS_BITMAP, SS_ETCHED*, a disabled look. */
#include <string.h>

#include "runtime/ctl.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define COL(i) user_syscolor(i)

typedef struct StaX {
  uint16_t font, icon;
} StaX;

static StaX *sx(HWND16 h) { return user_ctl_extra(h, sizeof(StaX)); }

/* SS_ICON with window text: the icon is the resource of that name in the window's hInstance (the
 * game's About and Host dialogs use "StarsIco" / "HostIco" and never send STM_SETICON). Wine
 * static.c loads it in WM_CREATE (and WM_SETTEXT) with STATIC_LoadIconW. A name that is no
 * resource leaves the icon 0. The control size is left as the template gives (UNVERIFIED: Wine
 * resizes to the icon; not done here). */
static void icon_from_text(const UWnd *w, StaX *x) {
  uint16_t hs = 0;
  FARPTR ps;
  if ((w->style & SS_TYPEMASK) != SS_ICON || !w->text[0]) return;
  ps = user_scratch((uint32_t)strlen(w->text) + 1, &hs);
  if (!ps) return;
  strcpy(LIN(ps), w->text);
  x->icon = LoadIcon(w->hinst, ps);
  user_scratch_free(hs);
}

static void draw_frame(HDC16 dc, RECT16 r, uint32_t c) {
  user_fill(dc, r_make(r.left, r.top, r.right, r.top + 1), c);
  user_fill(dc, r_make(r.left, r.bottom - 1, r.right, r.bottom), c);
  user_fill(dc, r_make(r.left, r.top, r.left + 1, r.bottom), c);
  user_fill(dc, r_make(r.right - 1, r.top, r.right, r.bottom), c);
}

static void static_draw(HWND16 h, HDC16 dc) {
  const UWnd *w = user_wnd(h);
  const StaX *x = sx(h);
  unsigned type;
  uint16_t brush, oldfont;
  RECT16 rc;
  if (!w || !x) return;
  type = w->style & SS_TYPEMASK;
  rc = r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top);
  brush = user_ctl_color(h, dc, CTLCOLOR_STATIC);
  user_ctl_fill_brush(dc, rc, brush);
  oldfont = SelectObject(dc, user_ctl_pick_font(x->font));
  switch (type) {
    case SS_LEFT:
    case SS_CENTER:
    case SS_RIGHT:
    case SS_SIMPLE:
    case SS_LEFTNOWORDWRAP: { /* static.c 603-632 */
      uint16_t fmt;
      if (type == SS_LEFT) fmt = DT_LEFT | DT_EXPANDTABS | DT_WORDBREAK;
      else if (type == SS_CENTER) fmt = DT_CENTER | DT_EXPANDTABS | DT_WORDBREAK;
      else if (type == SS_RIGHT) fmt = DT_RIGHT | DT_EXPANDTABS | DT_WORDBREAK;
      else if (type == SS_SIMPLE) fmt = DT_LEFT | DT_SINGLELINE;
      else fmt = DT_LEFT | DT_EXPANDTABS;
      if (w->style & SS_NOPREFIX) fmt |= DT_NOPREFIX;
      SetBkMode(dc, TRANSPARENT);
      user_ctl_draw_text(dc, rc, w->text, fmt);
      break;
    }
    case SS_ICON:
      if (x->icon) DrawIcon(dc, 0, 0, x->icon);
      break;
    case SS_BLACKRECT: user_fill(dc, rc, COL(6)); break;
    case SS_GRAYRECT: user_fill(dc, rc, COL(1)); break;
    case SS_WHITERECT: user_fill(dc, rc, COL(5)); break;
    case SS_BLACKFRAME: draw_frame(dc, rc, COL(6)); break;
    case SS_GRAYFRAME: draw_frame(dc, rc, COL(1)); break;
    case SS_WHITEFRAME: draw_frame(dc, rc, COL(5)); break;
    default: break; /* SS_USERITEM and the styles that are not implemented: background only */
  }
  SelectObject(dc, oldfont);
}

static void paint_now(HWND16 h) {
  HDC16 dc;
  if (!user_wnd(h) || !IsWindowVisible(h)) return;
  dc = GetDC(h);
  if (!dc) return;
  static_draw(h, dc);
  ReleaseDC(h, dc);
}

uint32_t user_static_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  StaX *x = sx(h);
  if (!w || !x) return user_def_window_proc(h, msg, wp, lp);
  switch (msg) {
    case WM_CREATE: icon_from_text(w, x); return 0;
    case WM_GETDLGCODE: return DLGC_STATIC;
    case WM_ERASEBKGND: return 1; /* everything is painted in WM_PAINT (static.c 374) */
    case WM_PAINT: {
      uint16_t hp = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp);
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc) static_draw(h, dc);
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_NCHITTEST: /* static.c 517-521: clicks go to the window below unless SS_NOTIFY */
      if (w->style & SS_NOTIFY) return user_def_window_proc(h, msg, wp, lp);
      return (uint16_t)HTTRANSPARENT;
    case WM_LBUTTONDOWN:
      if (w->style & SS_NOTIFY) user_ctl_notify(h, STN_CLICKED);
      return 0;
    case WM_LBUTTONDBLCLK:
      if (w->style & SS_NOTIFY) user_ctl_notify(h, STN_DBLCLK);
      return 0;
    case WM_ENABLE:
      if (w->style & SS_NOTIFY) user_ctl_notify(h, wp ? STN_ENABLE : STN_DISABLE);
      return 0;
    case WM_SETTEXT: {
      uint32_t r = user_def_window_proc(h, msg, wp, lp);
      icon_from_text(w, x);
      paint_now(h);
      return r;
    }
    case WM_SETFONT:
      x->font = wp;
      if (lp & 0xFFFF) InvalidateRect(h, 0, 1);
      return 0;
    case WM_GETFONT: return x->font;
    case STM_SETICON: {
      uint16_t old = x->icon;
      x->icon = wp;
      paint_now(h);
      return old;
    }
    case STM_GETICON: return x->icon;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}
