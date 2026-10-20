/* The SCROLLBAR control class (task stars-0z4.10): SBS_HORZ and SBS_VERT bars that fill their
 * client area. The drawing (user_sb_draw) and the mouse tracking loop (user_sb_track) are the ones
 * of the window scroll bars (user_scroll.c, stars-0z4.9) with bar = SB_CTL; position and range are
 * the SB_CTL entry of the window's scroll table in UserState (arena), so SetScrollPos,
 * GetScrollPos, SetScrollRange and GetScrollRange work on the control handle, and the SBM_*
 * messages are the same operations (Win16 packing: Wine dlls/user.exe16/message.c
 * scrollbar_proc16 2500-2527). WM_HSCROLL / WM_VSCROLL go to the parent with wParam = SB_* code,
 * LOWORD(lParam) = position, HIWORD(lParam) = the control. Keyboard handling is Wine's
 * (dlls/win32u/scroll.c handle_kbd_event 1179-1203). All UNVERIFIED for Windows 3.1
 * (docs/inventory.md section 15, "Standard controls part 1").
 *
 * Not implemented: SBS_SIZEBOX, the SBS_*ALIGN styles. WM_CTLCOLOR (CTLCOLOR_SCROLLBAR) is sent to the
 * parent before every paint (dialog manager task stars-0z4.12, so that a dialog function sees
 * every control kind), but the brush it answers is NOT used: the trough stays the dither of
 * user_sb_draw (UNVERIFIED for Windows 3.1, which fills the trough with that brush). */
#include "runtime/ctl.h"
#include "runtime/user_internal.h"

#define VK_PRIOR_ 0x21
#define VK_NEXT_ 0x22
#define VK_END_ 0x23
#define VK_HOME_ 0x24
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28

static void sb_paint(HWND16 h, HDC16 dc) {
  const UWnd *w = user_wnd(h);
  const UScroll *s = user_sb_get(h, SB_CTL);
  RECT16 bar;
  int ox, oy;
  if (!w || !s) return;
  (void)user_ctl_color(h, dc, CTLCOLOR_SCROLLBAR); /* offered to the parent, answer unused (see above) */
  w = user_wnd(h);
  if (!w) return;
  bar = r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top);
  user_client_origin(w, &ox, &oy);
  user_sb_draw(dc, bar, (w->style & SBS_VERT) != 0, s, !(w->style & WS_DISABLED) && s->max > s->min, 0, s->pos, (ox + oy) & 1);
}

void user_ctl_scroll_redraw(HWND16 h) {
  HDC16 dc;
  if (!user_wnd(h) || !IsWindowVisible(h)) return;
  dc = GetDC(h);
  if (!dc) return;
  sb_paint(h, dc);
  ReleaseDC(h, dc);
}

uint32_t user_scrollbar_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  UScroll *s = user_sb_get(h, SB_CTL);
  bool vert;
  if (!w || !s) return user_def_window_proc(h, msg, wp, lp);
  vert = (w->style & SBS_VERT) != 0;
  switch (msg) {
    case WM_CREATE: return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS; /* scroll.c 1352 */
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      uint16_t hp = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp);
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc) sb_paint(h, dc);
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_ENABLE:
    case WM_SYSCOLORCHANGE:
      user_ctl_scroll_redraw(h);
      return 0;
    case WM_LBUTTONDOWN: { /* scroll.c 1260-1275 */
      int ox, oy;
      HWND16 parent = GetParent(h);
      if (w->style & WS_TABSTOP) SetFocus(h);
      if (!user_wnd(h) || !parent) return 0;
      w = user_wnd(h);
      user_client_origin(w, &ox, &oy);
      user_sb_track(parent, h, SB_CTL, r_make(ox, oy, ox + (w->crect.right - w->crect.left), oy + (w->crect.bottom - w->crect.top)), vert,
                    ox + (int16_t)(lp & 0xFFFF), oy + (int16_t)(lp >> 16));
      return 0;
    }
    case WM_KEYDOWN: { /* scroll.c 1179-1203 */
      HWND16 parent = GetParent(h);
      uint16_t code;
      switch (wp) {
        case VK_PRIOR_: code = SB_PAGEUP; break;
        case VK_NEXT_: code = SB_PAGEDOWN; break;
        case VK_HOME_: code = SB_TOP; break;
        case VK_END_: code = SB_BOTTOM; break;
        case VK_UP_:
        case VK_LEFT_: code = SB_LINEUP; break;
        case VK_DOWN_:
        case VK_RIGHT_: code = SB_LINEDOWN; break;
        default: return 0;
      }
      if (parent && user_wnd(parent)) SendMessage(parent, vert ? WM_VSCROLL : WM_HSCROLL, code, make_long((uint16_t)s->pos, h));
      return 0;
    }
    case SBM_SETPOS: {
      int16_t old = s->pos;
      SetScrollPos(h, SB_CTL, (int16_t)wp, (uint16_t)(lp & 0xFFFF));
      return (uint16_t)old;
    }
    case SBM_GETPOS: return (uint16_t)s->pos;
    case SBM_SETRANGE: { /* Win16: wParam = redraw flag, lParam = MAKELONG(min, max) */
      int16_t old = s->pos;
      SetScrollRange(h, SB_CTL, (int16_t)(lp & 0xFFFF), (int16_t)(lp >> 16), wp);
      s = user_sb_get(h, SB_CTL);
      return (s && s->pos != old) ? (uint16_t)old : 0;
    }
    case SBM_GETRANGE: return make_long((uint16_t)s->min, (uint16_t)s->max);
    case SBM_ENABLE_ARROWS: {
      uint8_t mask = (uint8_t)(wp & 3);
      if (s->arrows == mask) return 0;
      s->arrows = mask;
      user_ctl_scroll_redraw(h);
      return 1;
    }
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}
