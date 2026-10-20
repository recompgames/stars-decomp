/* The BUTTON control class (task stars-0z4.10): push buttons, default push buttons, check boxes,
 * three-state boxes, radio buttons (plain and automatic), group boxes, user buttons and owner-draw
 * buttons.
 *
 * Message handling follows Wine's 32-bit control (dlls/user32/button.c at commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0; the lines are cited in runtime/tests/test_ctl_button.c)
 * with the Win16 differences: BM_* numbers are WM_USER+0..4, there is no BM_CLICK, the
 * notification is WM_COMMAND with wParam = id, LOWORD(lParam) = control and HIWORD(lParam) =
 * BN_*, and BN_SETFOCUS / BN_KILLFOCUS (Win32) are not sent. All of it is UNVERIFIED for Windows
 * 3.1 (docs/inventory.md section 15, "Standard controls part 1").
 *
 * State: the control's extra bytes (BtnX, 8 bytes, allocated in the arena by CreateWindow). The
 * look is drawn in code to the Windows 3.1 style from the system metrics and colours; every pixel
 * detail is this runtime's reading and UNVERIFIED (no reference capture exists): 1 pixel black
 * outline with rounded corners (2 pixels for a default button), white highlight inside top and
 * left, COLOR_BTNSHADOW inside bottom and right, pressed = shadow inside top and left only; 13x13
 * check box and radio button; X check mark; a dotted focus rectangle. */
#include <string.h>

#include "runtime/ctl.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define COL(i) user_syscolor(i)

typedef struct BtnX {
  uint16_t state; /* bits 0-1 check, 2 pushed (BST_PUSHED), 3 focus (BST_FOCUS), 6 mouse/key held (BTNPRESSED) */
  uint16_t font;
  uint16_t pad[2];
} BtnX;

#define ST_CHECK 3
#define ST_PUSHED 4
#define ST_FOCUS 8
#define ST_NSTATES 0x0F
#define ST_BTNPRESSED 0x40

/* Largest check state per button type (button.c 78-92). */
static const uint8_t MAXCHK[16] = {0, 0, 1, 1, 1, 2, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0};

static BtnX *bx(HWND16 h) { return user_ctl_extra(h, sizeof(BtnX)); }
static unsigned btype(const UWnd *w) { return w->style & BS_TYPEMASK; }
static bool is_check_type(unsigned t) {
  return t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_RADIOBUTTON || t == BS_3STATE || t == BS_AUTO3STATE ||
         t == BS_AUTORADIOBUTTON;
}
static bool is_radio_type(unsigned t) { return t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON; }

/* ---- drawing ---- */

static RECT16 client_of(const UWnd *w) { return r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top); }

/* The label rectangle's size and the '&' mnemonic are handled by DrawText (text.c). */
static void measure(HDC16 dc, const char *s, uint16_t flags, int maxw, int *tw, int *th) {
  RECT16 r = r_make(0, 0, maxw, 0);
  uint16_t hs = 0, hr = 0;
  FARPTR ps = user_scratch((uint32_t)strlen(s) + 1, &hs), pr = user_scratch(sizeof(RECT16), &hr);
  *tw = *th = 0;
  if (ps && pr) {
    strcpy(LIN(ps), s);
    *(RECT16 *)LIN(pr) = r;
    DrawText(dc, ps, -1, pr, (uint16_t)(flags | DT_CALCRECT));
    r = *(RECT16 *)LIN(pr);
    *tw = r.right - r.left;
    *th = r.bottom - r.top;
  }
  user_scratch_free(hs);
  user_scratch_free(hr);
}

static void line_h(HDC16 dc, int x0, int x1, int y, uint32_t c) { user_fill(dc, r_make(x0, y, x1 + 1, y + 1), c); }
static void line_v(HDC16 dc, int x, int y0, int y1, uint32_t c) { user_fill(dc, r_make(x, y0, x + 1, y1 + 1), c); }

/* Push button: outline (rounded), face, bevel, label, focus ring. */
static void draw_push(HDC16 dc, RECT16 b, const char *text, bool def, bool pressed, bool enabled, bool focus) {
  int t = def ? 2 : 1, l = b.left, tp = b.top, r = b.right, bt = b.bottom;
  uint32_t bg = GetBkColor(dc);
  RECT16 lab;
  user_fill(dc, b, COL(6));
  user_fill(dc, r_make(l + t, tp + t, r - t, bt - t), COL(15));
  if (!pressed) {
    user_fill(dc, r_make(l + t, tp + t, r - t - 1, tp + t + 1), COL(20));
    user_fill(dc, r_make(l + t, tp + t, l + t + 1, bt - t - 1), COL(20));
    user_fill(dc, r_make(l + t + 1, bt - t - 1, r - t, bt - t), COL(16));
    user_fill(dc, r_make(r - t - 1, tp + t + 1, r - t, bt - t), COL(16));
  } else {
    user_fill(dc, r_make(l + t, tp + t, r - t, tp + t + 1), COL(16));
    user_fill(dc, r_make(l + t, tp + t, l + t + 1, bt - t), COL(16));
  }
  user_fill(dc, r_make(l, tp, l + 1, tp + 1), bg); /* rounded corners show the parent's colour */
  user_fill(dc, r_make(r - 1, tp, r, tp + 1), bg);
  user_fill(dc, r_make(l, bt - 1, l + 1, bt), bg);
  user_fill(dc, r_make(r - 1, bt - 1, r, bt), bg);
  lab = r_make(l + t + 2, tp + t + 1, r - t - 2, bt - t - 1);
  if (pressed) lab = r_offset(lab, 1, 1);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, enabled ? COL(18) : COL(17));
  user_ctl_draw_text(dc, lab, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (focus) user_ctl_focus_rect(dc, r_make(l + 4, tp + 4, r - 4, bt - 4), COL(6));
}

static void draw_check_glyph(HDC16 dc, int bx0, int by0, unsigned type, unsigned check, bool pressed, bool enabled) {
  uint32_t ink = enabled ? COL(6) : COL(17);
  if (is_radio_type(type)) {
    for (int y = 0; y < 13; y++)
      for (int x = 0; x < 13; x++) {
        int dx = x - 6, dy = y - 6, d2 = dx * dx + dy * dy;
        if (d2 > 42) continue;
        if (d2 > 30) SetPixel(dc, (int16_t)(bx0 + x), (int16_t)(by0 + y), COL(6));
        else if (check && d2 <= 5) SetPixel(dc, (int16_t)(bx0 + x), (int16_t)(by0 + y), ink);
        else SetPixel(dc, (int16_t)(bx0 + x), (int16_t)(by0 + y), pressed ? COL(15) : COL(5));
      }
    return;
  }
  user_fill(dc, r_make(bx0, by0, bx0 + 13, by0 + 13), COL(6));
  user_fill(dc, r_make(bx0 + 1, by0 + 1, bx0 + 12, by0 + 12), pressed ? COL(15) : COL(5));
  if (check == 2) { /* indeterminate: dither of COLOR_BTNSHADOW and white */
    for (int y = 1; y < 12; y++)
      for (int x = 1; x < 12; x++)
        if (((x + y) & 1) == 0) SetPixel(dc, (int16_t)(bx0 + x), (int16_t)(by0 + y), COL(16));
  } else if (check == 1) { /* X, two pixels thick */
    for (int i = 0; i < 7; i++) {
      SetPixel(dc, (int16_t)(bx0 + 3 + i), (int16_t)(by0 + 3 + i), ink);
      SetPixel(dc, (int16_t)(bx0 + 4 + i), (int16_t)(by0 + 3 + i), ink);
      SetPixel(dc, (int16_t)(bx0 + 9 - i), (int16_t)(by0 + 3 + i), ink);
      SetPixel(dc, (int16_t)(bx0 + 10 - i), (int16_t)(by0 + 3 + i), ink);
    }
  }
}

static void draw_check(HDC16 dc, RECT16 rc, const char *text, unsigned type, unsigned state, bool left_text, bool enabled) {
  int h = rc.bottom - rc.top, w = rc.right - rc.left;
  int by0 = rc.top + (h - 13) / 2, bx0 = left_text ? rc.left + w - 13 : rc.left;
  int tw, th;
  RECT16 lab;
  draw_check_glyph(dc, bx0, by0, type, state & ST_CHECK, (state & ST_PUSHED) != 0, enabled);
  lab = left_text ? r_make(rc.left, rc.top, rc.left + w - 13 - 4, rc.bottom) : r_make(rc.left + 17, rc.top, rc.right, rc.bottom);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, enabled ? GetTextColor(dc) : COL(17));
  user_ctl_draw_text(dc, lab, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if ((state & ST_FOCUS) && text[0]) {
    measure(dc, text, DT_SINGLELINE, lab.right - lab.left, &tw, &th);
    if (th > 0) {
      int ty = rc.top + (h - th) / 2;
      user_ctl_focus_rect(dc, r_make(lab.left - 1, ty - 1, lab.left + tw + 2, ty + th + 1), COL(6));
    }
  }
}

static void draw_group(HDC16 dc, RECT16 rc, const char *text, bool enabled) {
  int w = rc.right - rc.left, h = rc.bottom - rc.top, tw = 0, th = 0, ytop;
  uint32_t ink = COL(6);
  if (text[0]) measure(dc, text, DT_SINGLELINE | DT_NOCLIP, w, &tw, &th);
  ytop = th / 2;
  line_v(dc, 0, ytop, h - 1, ink);
  line_v(dc, w - 1, ytop, h - 1, ink);
  line_h(dc, 0, w - 1, h - 1, ink);
  if (text[0]) {
    int gap0 = 5, gap1 = 8 + tw + 2;
    line_h(dc, 0, gap0 - 1, ytop, ink);
    if (gap1 <= w - 1) line_h(dc, gap1, w - 1, ytop, ink);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? GetTextColor(dc) : COL(17));
    user_ctl_draw_text(dc, r_make(8, 0, 8 + tw + 2, th), text, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
  } else {
    line_h(dc, 0, w - 1, ytop, ink);
  }
}

static void send_drawitem(HWND16 h, HDC16 dc, unsigned action) {
  const UWnd *w = user_wnd(h);
  const BtnX *x = bx(h);
  uint16_t hh = 0;
  FARPTR p;
  DRAWITEMSTRUCT16 *d;
  HWND16 parent = GetParent(h);
  if (!w || !x || !parent || !user_wnd(parent)) return;
  p = user_scratch(sizeof(DRAWITEMSTRUCT16), &hh);
  if (!p) return;
  d = LIN(p);
  memset(d, 0, sizeof *d);
  d->CtlType = ODT_BUTTON;
  d->CtlID = w->id;
  d->itemAction = (uint16_t)action;
  d->itemState = (uint16_t)(((x->state & ST_PUSHED) ? ODS_SELECTED : 0) | ((w->style & WS_DISABLED) ? ODS_DISABLED : 0) |
                            ((x->state & ST_FOCUS) ? ODS_FOCUS : 0));
  d->hwndItem = h;
  d->hDC = dc;
  d->rcItem = client_of(w);
  SendMessage(parent, WM_DRAWITEM, w->id, p);
  user_scratch_free(hh);
}

/* Draws the whole button on dc. action is ODA_DRAWENTIRE, ODA_SELECT or ODA_FOCUS. */
static void button_draw(HWND16 h, HDC16 dc, unsigned action) {
  const UWnd *w = user_wnd(h);
  BtnX *x = bx(h);
  unsigned type;
  uint16_t brush, oldfont;
  RECT16 rc;
  bool enabled;
  if (!w || !x) return;
  type = btype(w);
  if (type == BS_OWNERDRAW) {
    send_drawitem(h, dc, action);
    return;
  }
  if (type == BS_USERBUTTON) { /* button.c 1025-1066 (without the Win32 focus codes) */
    if (action == ODA_DRAWENTIRE) user_ctl_notify(h, BN_PAINT);
    else if (action == ODA_SELECT) user_ctl_notify(h, (x->state & ST_PUSHED) ? BN_HILITE : BN_UNHILITE);
    return;
  }
  rc = client_of(w);
  enabled = !(w->style & WS_DISABLED);
  brush = user_ctl_color(h, dc, CTLCOLOR_BTN);
  oldfont = SelectObject(dc, user_ctl_pick_font(x->font));
  if (type != BS_GROUPBOX) user_ctl_fill_brush(dc, rc, brush);
  if (is_check_type(type)) draw_check(dc, rc, w->text, type, x->state, (w->style & BS_LEFTTEXT) != 0, enabled);
  else if (type == BS_GROUPBOX) draw_group(dc, rc, w->text, enabled);
  else draw_push(dc, rc, w->text, type == BS_DEFPUSHBUTTON, (x->state & ST_PUSHED) != 0, enabled, (x->state & ST_FOCUS) != 0);
  SelectObject(dc, oldfont);
}

static void paint_now(HWND16 h, unsigned action) {
  HDC16 dc;
  if (!user_wnd(h) || !IsWindowVisible(h)) return;
  dc = GetDC(h);
  if (!dc) return;
  button_draw(h, dc, action);
  ReleaseDC(h, dc);
}

/* A group box has no background of its own: its rectangle is invalidated (with erase) in the parent. */
static void invalidate_in_parent(HWND16 h) {
  const UWnd *w = user_wnd(h);
  uint16_t hr = 0;
  FARPTR pr;
  if (!w || !user_wnd(w->parent)) return;
  pr = user_scratch(sizeof(RECT16), &hr);
  if (!pr) return;
  *(RECT16 *)LIN(pr) = w->wrect;
  InvalidateRect(w->parent, pr, 1);
  user_scratch_free(hr);
}

/* ---- automatic radio buttons ---- */

/* button.c 951-962 BUTTON_CheckAutoRadioButton: check this button, uncheck the other radio buttons
 * of its WS_GROUP group (siblings in z order; hidden and disabled ones are skipped as
 * GetNextDlgGroupItem does). */
static void check_auto_radio(HWND16 h) {
  const UWnd *w = user_wnd(h), *p;
  HWND16 sib[UW_MAX];
  unsigned n = 0, idx = 0, start, end;
  if (!w) return;
  p = user_wnd(w->parent);
  if (!p) return;
  for (const UWnd *c = user_wnd(p->child); c && n < UW_MAX; c = user_wnd(c->next)) {
    if (user_hwnd(c) == h) idx = n;
    sib[n++] = user_hwnd(c);
  }
  start = idx;
  while (start > 0 && !(user_wnd(sib[start])->style & WS_GROUP)) start--;
  end = idx + 1;
  while (end < n && !(user_wnd(sib[end])->style & WS_GROUP)) end++;
  for (unsigned i = start; i < end; i++) {
    const UWnd *s = user_wnd(sib[i]);
    if (!s) continue;
    if (sib[i] != h && (!(s->style & WS_VISIBLE) || (s->style & WS_DISABLED))) continue;
    if ((SendMessage(sib[i], WM_GETDLGCODE, 0, 0) & 0xFFFF) == (DLGC_BUTTON | DLGC_RADIOBUTTON))
      SendMessage(sib[i], BM_SETCHECK, sib[i] == h ? 1 : 0, 0);
  }
}

/* ---- window procedure ---- */

static void set_state_bit(HWND16 h, uint16_t bit, bool on) {
  BtnX *x = bx(h);
  if (x) x->state = (uint16_t)(on ? (x->state | bit) : (x->state & ~bit));
}

static uint32_t hit_client(HWND16 h, uint32_t lp) {
  const UWnd *w = user_wnd(h);
  RECT16 rc;
  if (!w) return 0;
  rc = client_of(w);
  return r_contains(rc, (int16_t)(lp & 0xFFFF), (int16_t)(lp >> 16));
}

uint32_t user_button_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  BtnX *x = bx(h);
  unsigned type;
  if (!w || !x) return user_def_window_proc(h, msg, wp, lp);
  type = btype(w);
  switch (msg) {
    case WM_CREATE: return 0;
    case WM_GETDLGCODE: /* button.c 188-198 */
      switch (type) {
        case BS_USERBUTTON:
        case BS_PUSHBUTTON: return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        case BS_DEFPUSHBUTTON: return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        case BS_RADIOBUTTON:
        case BS_AUTORADIOBUTTON: return DLGC_BUTTON | DLGC_RADIOBUTTON;
        case BS_GROUPBOX: return DLGC_STATIC;
        default: return DLGC_BUTTON;
      }
    case WM_NCHITTEST:
      if (type == BS_GROUPBOX) return (uint16_t)HTTRANSPARENT; /* button.c 488 */
      return user_def_window_proc(h, msg, wp, lp);
    case WM_ERASEBKGND:
      if (type == BS_OWNERDRAW) { /* the owner draws on a background of the parent's brush */
        RECT16 rc = client_of(w);
        user_ctl_fill_brush(wp, rc, user_ctl_color(h, wp, CTLCOLOR_BTN));
      }
      return 1;
    case WM_PAINT: {
      uint16_t hp = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp);
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc) button_draw(h, dc, ODA_DRAWENTIRE);
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_ENABLE:
    case WM_SYSCOLORCHANGE:
      paint_now(h, ODA_DRAWENTIRE);
      return 0;
    case WM_KEYDOWN:
      if (wp == VK_SPACE) {
        SendMessage(h, BM_SETSTATE, 1, 0);
        set_state_bit(h, ST_BTNPRESSED, true);
        SetCapture(h);
      }
      return 0;
    case WM_LBUTTONDBLCLK:
      if (type == BS_RADIOBUTTON || type == BS_USERBUTTON || type == BS_OWNERDRAW) { /* button.c 254-263 */
        user_ctl_notify(h, BN_DOUBLECLICKED);
        return 0;
      }
      /* fall through */
    case WM_LBUTTONDOWN:
      SetCapture(h);
      set_state_bit(h, ST_BTNPRESSED, true);
      SetFocus(h);
      if (user_wnd(h)) SendMessage(h, BM_SETSTATE, 1, 0);
      return 0;
    case WM_KEYUP:
      if (wp != VK_SPACE) return 0;
      /* fall through */
    case WM_LBUTTONUP: {
      uint16_t st = x->state;
      if (!(st & ST_BTNPRESSED)) return 0;
      st &= ST_NSTATES;
      x->state = st;
      if (!(st & ST_PUSHED)) {
        ReleaseCapture();
        return 0;
      }
      SendMessage(h, BM_SETSTATE, 0, 0);
      if (!user_wnd(h)) return 0;
      if (msg == WM_KEYUP || hit_client(h, lp)) {
        st = bx(h)->state;
        switch (type) {
          case BS_AUTOCHECKBOX: SendMessage(h, BM_SETCHECK, !(st & ST_CHECK), 0); break;
          case BS_AUTORADIOBUTTON: check_auto_radio(h); break;
          case BS_AUTO3STATE: SendMessage(h, BM_SETCHECK, (st & 2) ? 0 : ((st & ST_CHECK) + 1), 0); break;
          default: break;
        }
        ReleaseCapture();
        if (user_wnd(h)) user_ctl_notify(h, BN_CLICKED);
      } else {
        ReleaseCapture();
      }
      return 0;
    }
    case WM_CANCELMODE: /* the capture goes away without a click */
      if (x->state & ST_BTNPRESSED) {
        uint16_t st = x->state;
        x->state = (uint16_t)(st & ST_NSTATES);
        if (st & ST_PUSHED) SendMessage(h, BM_SETSTATE, 0, 0);
      }
      return user_def_window_proc(h, msg, wp, lp);
    case WM_MOUSEMOVE:
      if ((wp & MK_LBUTTON) && GetCapture() == h) SendMessage(h, BM_SETSTATE, hit_client(h, lp) ? 1 : 0, 0);
      return 0;
    case WM_SETTEXT: {
      uint32_t r = user_def_window_proc(h, msg, wp, lp);
      if (!user_wnd(h) || !IsWindowVisible(h)) return r;
      if (type == BS_GROUPBOX) invalidate_in_parent(h);
      else paint_now(h, ODA_DRAWENTIRE);
      return r;
    }
    case WM_SETFONT:
      x->font = wp;
      if (lp & 0xFFFF) InvalidateRect(h, 0, 1);
      return 0;
    case WM_GETFONT: return x->font;
    case WM_SETFOCUS:
      x->state |= ST_FOCUS;
      paint_now(h, ODA_FOCUS);
      return 0;
    case WM_KILLFOCUS: {
      uint16_t st = x->state;
      x->state = (uint16_t)(st & ~ST_FOCUS);
      if ((st & ST_BTNPRESSED) && GetCapture() == h) { /* button.c 398-410 and the capture-lost case 312-322 */
        ReleaseCapture();
        x->state = (uint16_t)(x->state & ST_NSTATES);
        if (st & ST_PUSHED) SendMessage(h, BM_SETSTATE, 0, 0);
      }
      paint_now(h, ODA_FOCUS);
      return 0;
    }
    case BM_SETSTYLE: /* button.c 416-426: only the type changes; LOWORD(lParam) asks for a redraw */
      w->style = (w->style & ~(uint32_t)BS_TYPEMASK) | (wp & BS_TYPEMASK);
      if (lp & 0xFFFF) {
        if (type == BS_GROUPBOX || (w->style & BS_TYPEMASK) == BS_GROUPBOX) invalidate_in_parent(h);
        else InvalidateRect(h, 0, 1);
      }
      return 0;
    case BM_GETCHECK: return x->state & ST_CHECK;
    case BM_SETCHECK: {
      unsigned mx = MAXCHK[type & 15];
      if (wp > mx) wp = (uint16_t)mx;
      if ((x->state & ST_CHECK) != wp) {
        x->state = (uint16_t)((x->state & ~ST_CHECK) | wp);
        paint_now(h, ODA_SELECT);
      }
      return 0;
    }
    case BM_GETSTATE: return x->state & ST_NSTATES; /* SDK layout; Wine also leaks bit 6 */
    case BM_SETSTATE:
      x->state = (uint16_t)(wp ? (x->state | ST_PUSHED) : (x->state & ~ST_PUSHED));
      paint_now(h, ODA_SELECT);
      return 0;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}
