/* USER part 2 (task stars-0z4.7): input pipeline, focus, activation, capture, key state,
 * TranslateMessage and timers. Plain C, no harness dependency.
 *
 * Model. The host calls rt_input_mouse / rt_input_key; they only append RAW events to a hardware
 * queue in the arena (UInput.hq). Everything that depends on application state happens when the
 * application retrieves the message (user_input_fetch, called by GetMessage / PeekMessage after
 * posted messages and WM_QUIT and before WM_PAINT): hit testing (WM_NCHITTEST), capture routing,
 * double-click detection, WM_SETCURSOR, MK_* flags and the key state seen by GetKeyState.
 * The same order as Wine's server (queue.c get_message 3336-3379).
 *
 * Sources: the Windows 3.1 SDK text is NOT available and no reference capture exists, so every
 * rule here is UNVERIFIED for Windows 3.1. Wine (wine-mirror/wine commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0) is the reference and is cited by file and line next
 * to each rule; the tests in runtime/tests/test_user_input.c carry the same citations and
 * docs/inventory.md section 13 lists the rules.
 *
 * Clock. GetTickCount and the timers use the runtime clock (rt_clock_ms, one-second
 * resolution) relative to the moment user_init ran (0 at start): a timer fires at most once per clock second. Windows 3.1 ticks at about 55 ms
 * (18.2 Hz). This is a recorded limitation, not an approximation to be refined here. */
#include <stdlib.h>
#include <string.h>

#include "runtime/user_internal.h"
#include "runtime/vfs.h"

#define DBLCLK_TIME 500u  /* Windows 3.1 default double-click time, ms (issue text) */

static UInput *IN(void) {
  UserState *st = user_state();
  return st ? &st->in : NULL;
}

static uint32_t pt32(int x, int y) { return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16); }

/* Boot-relative: the runtime clock's elapsed milliseconds since user_input_init (user_init).
 * rt_clock_ms stays the date clock (milliseconds since 1980), so the start value no longer
 * depends on the calendar date. One-second resolution is kept (recorded limitation). */
uint32_t user_tick(void) {
  UInput *in = IN();
  return rt_clock_ms() - (in ? in->tick_base : 0);
}

void user_input_init(void) {
  UInput *in = IN();
  if (!in) return;
  in->next_timer_id = 1;
  in->tick_base = rt_clock_ms();
  in->cursor = LoadCursor(0, (FARPTR)32512); /* IDC_ARROW */
}

/* ---- tree helpers ---- */

static bool in_subtree(HWND16 h, HWND16 root) {
  for (int guard = 0; h && guard < UW_MAX + 2; guard++) {
    const UWnd *w;
    if (h == root) return true;
    w = user_wnd(h);
    if (!w) return false;
    h = w->parent;
  }
  return false;
}

static HWND16 root_of(HWND16 h) {
  HWND16 d = user_desktop();
  for (int guard = 0; guard < UW_MAX + 2; guard++) {
    const UWnd *w = user_wnd(h);
    if (!w) return 0;
    if (w->parent == d || w->parent == 0) return h;
    h = w->parent;
  }
  return 0;
}

static bool can_activate(HWND16 h) {
  const UWnd *w = user_wnd(h);
  return w && h != user_desktop() && (w->style & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE;
}

/* A window gets no mouse input when it or any of its ancestors is disabled (EnableWindow: disabling
 * a window disables its child windows too). stars-q2c. */
static bool input_disabled(const UWnd *w) {
  for (int guard = 0; w && guard < UW_MAX + 2; guard++) {
    if (w->style & WS_DISABLED) return true;
    if (!(w->style & WS_CHILD)) break;
    w = user_wnd(w->parent);
  }
  return false;
}

static uint32_t snd(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  const UWnd *w = user_wnd(h);
  if (!w) return 0;
  return user_call_wndproc(w->wndproc, h, msg, wp, lp);
}

/* ---- focus and activation (W-focus: win32u/input.c 2073-2320) ---- */

/* set_focus_window 2077-2114: WM_KILLFOCUS(wParam = new) to the old window, then
 * WM_SETFOCUS(wParam = old) to the new one. */
static HWND16 set_focus_internal(HWND16 hwnd) {
  UInput *in = IN();
  HWND16 prev = in->focus;
  if (prev && !user_wnd(prev)) prev = 0;
  if (prev == hwnd) return prev;
  in->focus = hwnd;
  if (prev) {
    snd(prev, WM_KILLFOCUS, hwnd, 0);
    in = IN();
    if (in->focus != hwnd) return prev; /* changed by the message */
  }
  if (hwnd && user_wnd(hwnd)) snd(hwnd, WM_SETFOCUS, prev, 0);
  return prev;
}

/* set_active_window 2120-2230 (Win16 WM_ACTIVATE layout: LOWORD(lParam) = minimised flag,
 * HIWORD(lParam) = the other window; UNVERIFIED, Wine builds the Win32 layout). */
static HWND16 set_active_internal(HWND16 hwnd, bool mouse, bool focus) {
  UInput *in = IN();
  HWND16 prev = in->active;
  if (prev && !user_wnd(prev)) prev = 0;
  if (prev == hwnd) return prev;
  if (prev) {
    snd(prev, WM_NCACTIVATE, 0, 0);
    if (user_wnd(prev)) snd(prev, WM_ACTIVATE, WA_INACTIVE, make_long(user_wnd(prev)->style & WS_MINIMIZE ? 1 : 0, hwnd));
  }
  in = IN();
  in->active = hwnd;
  if (hwnd && !user_wnd(hwnd)) return prev;
  if (hwnd) {
    snd(hwnd, WM_NCACTIVATE, 1, 0);
    if (user_wnd(hwnd))
      snd(hwnd, WM_ACTIVATE, mouse ? WA_CLICKACTIVE : WA_ACTIVE,
          make_long(user_wnd(hwnd)->style & WS_MINIMIZE ? 1 : 0, prev));
  }
  in = IN();
  if (focus && hwnd == in->active) { /* focus follows unless it is already inside the new window */
    HWND16 f = in->focus;
    if (f && !user_wnd(f)) f = 0;
    if (!f || !hwnd || root_of(f) != hwnd) set_focus_internal(hwnd);
  }
  return prev;
}

/* ShowWindow / SetWindowPos made a top-level window visible without SWP_NOACTIVATE: activate it
 * and move the focus in (window.c show_window ~4190: set_active_window(hwnd, NULL, FALSE, TRUE);
 * the z-order step Wine's set_active_window does is not done here). UNVERIFIED for 3.1. */
bool user_activate_shown(HWND16 hwnd) {
  if (!IN() || !can_activate(hwnd)) return false;
  set_active_internal(hwnd, false, true);
  return true;
}

HWND16 GetActiveWindow(void) {
  UInput *in = IN();
  return in && user_wnd(in->active) ? in->active : 0;
}

HWND16 GetFocus(void) {
  UInput *in = IN();
  return in && user_wnd(in->focus) ? in->focus : 0;
}

/* NtUserSetActiveWindow 2236-2262: a child window changes nothing and the active window is
 * returned. */
HWND16 SetActiveWindow(HWND16 hwnd) {
  UWnd *w;
  if (!IN()) return 0;
  if (hwnd) {
    w = user_wnd(hwnd);
    if (!w || hwnd == user_desktop()) return 0;
    if ((w->style & (WS_POPUP | WS_CHILD)) == WS_CHILD) return GetActiveWindow();
  }
  return set_active_internal(hwnd, false, true);
}

/* NtUserSetFocus 2266-2320 */
HWND16 SetFocus(HWND16 hwnd) {
  UInput *in = IN();
  HWND16 prev, top, active;
  if (!in) return 0;
  prev = GetFocus();
  if (hwnd) {
    if (!user_wnd(hwnd) || hwnd == user_desktop()) return 0;
    if (hwnd == prev) return prev;
    top = hwnd;
    for (;;) { /* a minimised or disabled window, or an ancestor of one, cannot take the focus */
      const UWnd *w = user_wnd(top);
      if (w->style & (WS_MINIMIZE | WS_DISABLED)) return 0;
      if (!(w->style & WS_CHILD)) break;
      if (w->parent == user_desktop() || !user_wnd(w->parent)) {
        if ((w->style & (WS_POPUP | WS_CHILD)) == WS_CHILD) return 0;
        break;
      }
      top = w->parent;
    }
    active = GetActiveWindow();
    if (top != active) {
      set_active_internal(top, false, false);
      if (!user_wnd(hwnd)) return 0;
      if (top != GetActiveWindow()) return 0;
    }
  } else if (!prev) {
    return 0;
  }
  return set_focus_internal(hwnd);
}

/* activate_other_window 4508-4540 */
static void activate_other(HWND16 hwnd) {
  UserState *st = user_state();
  const UWnd *w = user_wnd(hwnd);
  HWND16 to = 0;
  if (!w) return;
  if ((w->style & WS_POPUP) && w->owner) {
    HWND16 r = root_of(w->owner);
    if (r && can_activate(r)) to = r;
  }
  if (!to) {
    for (HWND16 n = w->next; n; n = user_wnd(n) ? user_wnd(n)->next : 0)
      if (can_activate(n)) {
        to = n;
        break;
      }
  }
  if (!to) {
    for (HWND16 n = st->w[0].child; n && n != hwnd; n = user_wnd(n) ? user_wnd(n)->next : 0)
      if (can_activate(n)) {
        to = n;
        break;
      }
  }
  set_active_internal(to, false, true);
}

/* DestroyWindow (window.c 5546-5621): capture is released first, before the window is hidden. */
void user_input_pre_destroy(HWND16 hwnd) {
  UInput *in = IN();
  if (!in || !user_wnd(hwnd)) return;
  if (in->capture && in_subtree(in->capture, hwnd)) in->capture = 0;
}

/* DestroyWindow, after the hide and after the owned windows are gone, just before WM_DESTROY:
 * the active window passes to the next one (send_destroy_message 5398-5410 calls
 * activate_other_window), and the focus goes to the parent of a destroyed child (hide step,
 * 5113-5124). ORDER UNVERIFIED against Windows 3.1. */
void user_input_leave_destroy(HWND16 hwnd) {
  UInput *in = IN();
  const UWnd *w = user_wnd(hwnd);
  HWND16 parent;
  if (!in || !w) return;
  if (in->active == hwnd) activate_other(hwnd);
  in = IN();
  w = user_wnd(hwnd);
  if (!w) return;
  if (in->focus && user_wnd(in->focus) && in_subtree(in->focus, hwnd)) {
    parent = (w->style & WS_CHILD) ? w->parent : 0;
    if (parent == user_desktop()) parent = 0;
    SetFocus(parent);
  }
}

/* EnableWindow(FALSE) on the focus window: the focus moves to 0 (window.c 1150-1161). */
void user_input_disabled(HWND16 hwnd) {
  UInput *in = IN();
  if (in && in->focus == hwnd) SetFocus(0);
}

/* The window slot is freed: nothing may keep pointing at it. */
void user_input_window_gone(HWND16 hwnd) {
  UInput *in = IN();
  if (!in) return;
  if (in->focus == hwnd) in->focus = 0;
  if (in->active == hwnd) in->active = 0;
  if (in->capture == hwnd) in->capture = 0;
  for (unsigned i = 0; i < UT_MAX; i++)
    if (in->timers[i].used && in->timers[i].hwnd == hwnd) in->timers[i].used = 0;
}

/* ---- capture ---- */

HWND16 SetCapture(HWND16 hwnd) {
  UInput *in = IN();
  HWND16 prev;
  if (!in) return 0;
  prev = in->capture && user_wnd(in->capture) ? in->capture : 0;
  if (hwnd && (!user_wnd(hwnd) || hwnd == user_desktop())) return 0;
  in->capture = hwnd;
  return prev;
}

void ReleaseCapture(void) {
  UInput *in = IN();
  if (in) in->capture = 0;
}

HWND16 GetCapture(void) {
  UInput *in = IN();
  return in && user_wnd(in->capture) ? in->capture : 0;
}

/* ---- key state ---- */

int16_t GetKeyState(int16_t vk) {
  UInput *in = IN();
  uint8_t v;
  if (!in || vk < 0 || vk > 255) return 0;
  v = in->keysync[vk];
  return (int16_t)((v & 0x80 ? 0x8000 : 0) | (v & 1));
}

int16_t GetAsyncKeyState(int16_t vk) {
  UInput *in = IN();
  uint16_t r;
  if (!in || vk < 0 || vk > 255) return 0;
  r = (uint16_t)((in->keyasync[vk] & 0x80 ? 0x8000 : 0) | (in->keypressed[vk] ? 1 : 0));
  in->keypressed[vk] = 0;
  return (int16_t)r;
}

void GetCursorPos(FARPTR lpPoint) {
  UInput *in = IN();
  POINT16 *p;
  if (!in || !lpPoint || !mem_check(lpPoint, sizeof(POINT16))) return;
  p = LIN(lpPoint);
  p->x = in->px;
  p->y = in->py;
}

/* ---- host entry points ---- */

static UHw *hq_at(UInput *in, unsigned i) { return &in->hq[(in->hhead + i) % UH_MAX]; }

static UHw *hq_push(UInput *in, uint16_t msg, uint16_t wp, uint32_t lp, int16_t x, int16_t y, uint32_t time) {
  UHw *e;
  if (in->hcount >= UH_MAX) return NULL; /* full: the newest event is lost */
  e = hq_at(in, in->hcount++);
  e->m.hwnd = 0;
  e->m.msg = msg;
  e->m.wp = wp;
  e->m.lp = lp;
  e->m.time = time;
  e->m.x = x;
  e->m.y = y;
  e->seq = ++in->hseq;
  return e;
}

static void hq_remove(UInput *in, unsigned i) {
  for (unsigned j = i; j + 1 < in->hcount; j++) *hq_at(in, j) = *hq_at(in, j + 1);
  if (in->hcount) in->hcount--;
}

static int hq_find(UInput *in, uint32_t seq) {
  for (unsigned i = 0; i < in->hcount; i++)
    if (hq_at(in, i)->seq == seq) return (int)i;
  return -1;
}

void rt_input_mouse(int16_t x, int16_t y, uint16_t buttons, uint32_t time_ms) {
  UInput *in = IN();
  uint16_t changed;
  static const struct {
    uint16_t bit, down, up, vk;
  } B[3] = {{RT_BTN_LEFT, WM_LBUTTONDOWN, WM_LBUTTONUP, VK_LBUTTON},
            {RT_BTN_RIGHT, WM_RBUTTONDOWN, WM_RBUTTONUP, VK_RBUTTON},
            {RT_BTN_MIDDLE, WM_MBUTTONDOWN, WM_MBUTTONUP, VK_MBUTTON}};
  if (!in) return;
  if (!in->has_pos || x != in->px || y != in->py) {
    UHw *tail = in->hcount ? hq_at(in, in->hcount - 1) : NULL;
    if (tail && tail->m.msg == WM_MOUSEMOVE) { /* consecutive moves collapse into the last one */
      tail->m.x = x;
      tail->m.y = y;
      tail->m.time = time_ms;
    } else {
      hq_push(in, WM_MOUSEMOVE, 0, 0, x, y, time_ms);
    }
    in->px = x;
    in->py = y;
    in->has_pos = 1;
  }
  changed = (uint16_t)(buttons ^ in->btn_async);
  for (int i = 0; i < 3; i++) {
    if (!(changed & B[i].bit)) continue;
    if (buttons & B[i].bit) {
      hq_push(in, B[i].down, 0, 0, x, y, time_ms);
      in->keyasync[B[i].vk] |= 0x80;
      in->keypressed[B[i].vk] = 1;
    } else {
      hq_push(in, B[i].up, 0, 0, x, y, time_ms);
      in->keyasync[B[i].vk] &= (uint8_t)~0x80;
    }
  }
  in->btn_async = (uint16_t)(buttons & 7);
}

/* queue_keyboard_message (server/queue.c 2319-2400): the message code is decided from the
 * hardware key state at the time of the event. */
void rt_input_key(uint16_t vk, uint16_t scancode, int down, uint32_t time_ms) {
  UInput *in = IN();
  uint16_t msg = down ? WM_KEYDOWN : WM_KEYUP;
  bool prev_down, alt, ctrl;
  uint32_t lp;
  if (!in || vk > 255) return;
  alt = (in->keyasync[VK_MENU] & 0x80) != 0;
  ctrl = (in->keyasync[VK_CONTROL] & 0x80) != 0;
  prev_down = (in->keyasync[vk] & 0x80) != 0;
  switch (vk) {
    case VK_MENU:
      if (!down) {
        if (alt && in->alt_pressed) { /* Alt released with no key in between */
          msg = WM_SYSKEYUP;
          in->alt_pressed = 0;
        }
      } else if (!ctrl) {
        msg = WM_SYSKEYDOWN;
        in->alt_pressed = 1;
      }
      break;
    case VK_CONTROL:
      if (!down && alt) {
        msg = WM_SYSKEYUP;
        in->alt_pressed = 0;
      }
      break;
    default:
      if (vk != VK_F10 && (ctrl || !alt)) break;
      msg = down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
      in->alt_pressed = 0;
      break;
  }
  if (down) {
    in->keyasync[vk] |= 0x80;
    if (!prev_down) in->keypressed[vk] = 1;
  } else {
    in->keyasync[vk] &= (uint8_t)~0x80;
  }
  alt = (in->keyasync[VK_MENU] & 0x80) != 0;
  lp = 1u | ((uint32_t)(scancode & 0xFF) << 16) | ((scancode & 0x100) ? 1u << 24 : 0);
  if ((msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && alt) lp |= 1u << 29; /* context code */
  if (!down || prev_down) lp |= 1u << 30;                                  /* previous state */
  if (!down) lp |= 1u << 31;                                               /* transition */
  hq_push(in, msg, vk, lp, in->px, in->py, time_ms);
}

/* ---- retrieval ---- */

enum { H_SKIP, H_ACCEPT, H_DROP /* only when removing */, H_EAT /* always */ };

static bool is_toggle(unsigned vk) { return vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL; }

static void key_apply(UInput *in, uint16_t msg, uint16_t vk) {
  if (vk > 255) return;
  if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
    bool was = (in->keysync[vk] & 0x80) != 0;
    in->keysync[vk] |= 0x80;
    if (!was && is_toggle(vk)) in->keysync[vk] ^= 1;
  } else {
    in->keysync[vk] &= (uint8_t)~0x80;
  }
}

static bool hwnd_filter_ok(HWND16 target, HWND16 filter) {
  return !filter || filter == UHWND_BROADCAST || target == filter || (user_wnd(target) && in_subtree(user_wnd(target)->parent, filter));
}

static void btn_apply(UInput *in, uint16_t b) {
  in->btn_sync = b;
  in->keysync[VK_LBUTTON] = (b & RT_BTN_LEFT) ? 0x80 : 0;
  in->keysync[VK_RBUTTON] = (b & RT_BTN_RIGHT) ? 0x80 : 0;
  in->keysync[VK_MBUTTON] = (b & RT_BTN_MIDDLE) ? 0x80 : 0;
}

/* process_mouse_message (win32u/message.c 2598-2870) */
static int process_mouse(const UMsg *e, HWND16 filter, uint16_t min, uint16_t max, bool remove, MSG16 *out) {
  UInput *in = IN();
  bool cap = in->capture && user_wnd(in->capture);
  HWND16 target;
  int hit = HTCLIENT;
  uint16_t msg = e->msg, wp, b = in->btn_sync;
  uint32_t lp;
  const UWnd *w;
  int ox, oy;

  /* the button state as it will be after this event (the key state follows retrieval) */
  if (e->msg == WM_LBUTTONDOWN) b |= RT_BTN_LEFT;
  else if (e->msg == WM_LBUTTONUP) b &= (uint16_t)~RT_BTN_LEFT;
  else if (e->msg == WM_RBUTTONDOWN) b |= RT_BTN_RIGHT;
  else if (e->msg == WM_RBUTTONUP) b &= (uint16_t)~RT_BTN_RIGHT;
  else if (e->msg == WM_MBUTTONDOWN) b |= RT_BTN_MIDDLE;
  else if (e->msg == WM_MBUTTONUP) b &= (uint16_t)~RT_BTN_MIDDLE;

  if (cap) {
    target = in->capture;
  } else {
    HWND16 skip[16];
    unsigned nskip = 0;
    for (;;) {
      target = nskip ? user_window_from_point_skip(pt32(e->x, e->y), skip, nskip) : WindowFromPoint(pt32(e->x, e->y));
      if (!target || target == user_desktop()) { /* the desktop belongs to no application */
        if (remove) btn_apply(in, b);
        return H_DROP;
      }
      w = user_wnd(target);
      if (input_disabled(w)) {
        hit = HTERROR;
        break;
      }
      hit = (int16_t)snd(target, WM_NCHITTEST, 0, pt32(e->x, e->y)); /* window.c 2886 */
      in = IN();
      if (!user_wnd(target)) {
        if (remove) btn_apply(in, b);
        return H_DROP;
      }
      if (hit != HTTRANSPARENT) break;
      /* HTTRANSPARENT (stars-0z4.10: group boxes, static text): Wine tries the window below */
      if (nskip >= 16) {
        if (remove) btn_apply(in, b);
        return H_DROP;
      }
      skip[nskip++] = target;
    }
  }
  w = user_wnd(target);
  if (!w) return H_DROP;
  in = IN();
  if (!hwnd_filter_ok(target, filter)) return H_SKIP;

  user_client_origin(w, &ox, &oy);
  if (hit != HTCLIENT) { /* 2724-2728 */
    msg = (uint16_t)(msg - (WM_MOUSEFIRST - WM_NCMOUSEMOVE));
    wp = (uint16_t)hit;
    lp = pt32(e->x, e->y);
  } else { /* 2729-2735 */
    wp = (uint16_t)(((b & RT_BTN_LEFT) ? MK_LBUTTON : 0) | ((b & RT_BTN_RIGHT) ? MK_RBUTTON : 0) |
                    ((b & RT_BTN_MIDDLE) ? MK_MBUTTON : 0) | ((in->keysync[VK_SHIFT] & 0x80) ? MK_SHIFT : 0) |
                    ((in->keysync[VK_CONTROL] & 0x80) ? MK_CONTROL : 0));
    lp = pt32(e->x - ox, e->y - oy);
  }

  /* double clicks: 2739-2775 */
  if (e->msg == WM_LBUTTONDOWN || e->msg == WM_RBUTTONDOWN || e->msg == WM_MBUTTONDOWN) {
    bool update = remove;
    UserState *st = user_state();
    if (hit != HTCLIENT || (st->c[w->cls].style & CS_DBLCLKS)) {
      if (e->msg == in->clk_msg && target == in->clk_hwnd && (uint32_t)(e->time - in->clk_time) < DBLCLK_TIME &&
          abs(e->x - in->clk_x) < GetSystemMetrics(36) / 2 && abs(e->y - in->clk_y) < GetSystemMetrics(37) / 2) {
        msg = (uint16_t)(msg + (WM_LBUTTONDBLCLK - WM_LBUTTONDOWN));
        if (update) {
          in->clk_msg = 0; /* clear the double-click conditions */
          update = false;
        }
      }
    }
    if (!user_in_range(msg, min, max)) return H_SKIP;
    if (update) {
      in->clk_msg = e->msg;
      in->clk_hwnd = target;
      in->clk_time = e->time;
      in->clk_x = e->x;
      in->clk_y = e->y;
    }
  } else if (!user_in_range(msg, min, max)) {
    return H_SKIP;
  }

  if (hit == HTERROR || hit == HTNOWHERE) { /* 2800-2804: eaten even by PM_NOREMOVE */
    btn_apply(in, b);
    snd(target, WM_SETCURSOR, target, make_long((uint16_t)hit, e->msg));
    return H_EAT;
  }
  if (remove) {
    btn_apply(in, b);
    if (!cap) /* 2860-2864 (no WM_SETCURSOR while the mouse is captured: 2809) */
      snd(target, WM_SETCURSOR, target, make_long((uint16_t)hit, e->msg));
  }
  out->hwnd = target;
  out->message = msg;
  out->wParam = wp;
  out->lParam = lp;
  out->time = e->time;
  out->pt.x = e->x;
  out->pt.y = e->y;
  return H_ACCEPT;
}

/* find_hardware_message_window (server/queue.c 1761-1790): focus, else the active window with
 * the SYS form of the message, else nobody. */
static int process_key(const UMsg *e, HWND16 filter, uint16_t min, uint16_t max, bool remove, MSG16 *out) {
  UInput *in = IN();
  HWND16 target = GetFocus();
  uint16_t msg = e->msg;
  if (!target) {
    target = GetActiveWindow();
    if (target && msg < WM_SYSKEYDOWN) msg = (uint16_t)(msg + (WM_SYSKEYDOWN - WM_KEYDOWN));
  }
  if (!target) {
    if (remove) key_apply(in, e->msg, e->wp);
    return H_DROP;
  }
  if (!hwnd_filter_ok(target, filter) || !user_in_range(msg, min, max)) return H_SKIP;
  if (remove) key_apply(in, e->msg, e->wp);
  out->hwnd = target;
  out->message = msg;
  out->wParam = e->wp;
  out->lParam = e->lp;
  out->time = e->time;
  out->pt.x = in->px;
  out->pt.y = in->py;
  return H_ACCEPT;
}

/* filter_contains_hw_range (server/queue.c 769-781) */
static bool filter_has_hw(uint16_t min, uint16_t max) {
  if (min == 0 && max == 0) return true;
  if (max < WM_NCMOUSEMOVE) return false;
  if (min > 0x00A9 && max < WM_KEYFIRST) return false;
  if (min > WM_KEYLAST && max < WM_MOUSEFIRST) return false;
  if (min > WM_MOUSELAST) return false;
  return true;
}

bool user_input_fetch(MSG16 *out, HWND16 hwnd, uint16_t min, uint16_t max, bool remove) {
  UInput *in = IN();
  unsigned i = 0;
  if (!in || !filter_has_hw(min, max)) return false;
  while (i < in->hcount) {
    UHw e = *hq_at(in, i);
    int r;
    bool mouse_ev = e.m.msg >= WM_MOUSEFIRST && e.m.msg <= WM_MOUSELAST;
    int idx;
    r = mouse_ev ? process_mouse(&e.m, hwnd, min, max, remove, out) : process_key(&e.m, hwnd, min, max, remove, out);
    in = IN();
    idx = hq_find(in, e.seq); /* window procedures ran: find the entry again */
    if (r == H_ACCEPT) {
      if (remove && idx >= 0) hq_remove(in, (unsigned)idx);
      return true;
    }
    if (idx < 0) {
      i = 0; /* somebody else took it: start again */
      continue;
    }
    if (r == H_EAT || (r == H_DROP && remove)) {
      hq_remove(in, (unsigned)idx);
      i = (unsigned)idx;
      continue;
    }
    i = (unsigned)idx + 1;
  }
  return false;
}

/* ---- TranslateMessage (W-xlate: win32u/message.c 4830-4880), US keyboard layout ---- */

static int vk_char(unsigned vk, bool shift, bool caps, bool ctrl) {
  static const char SHIFT_DIGITS[] = ")!@#$%^&*(";
  if (vk >= 0x41 && vk <= 0x5A) {
    int c = (int)(vk - 0x41);
    if (ctrl) return c + 1;
    return (shift != caps) ? 'A' + c : 'a' + c;
  }
  if (vk >= 0x30 && vk <= 0x39) return shift ? SHIFT_DIGITS[vk - 0x30] : (int)vk;
  if (vk >= 0x60 && vk <= 0x69) return '0' + (int)(vk - 0x60);
  switch (vk) {
    case VK_SPACE: return ' ';
    case VK_RETURN: return 13;
    case VK_TAB: return 9;
    case VK_BACK: return 8;
    case VK_ESCAPE: return 27;
    case 0x6A: return '*';
    case 0x6B: return '+';
    case 0x6D: return '-';
    case 0x6E: return '.';
    case 0x6F: return '/';
    case 0xBA: return shift ? ':' : ';';
    case 0xBB: return shift ? '+' : '=';
    case 0xBC: return shift ? '<' : ',';
    case 0xBD: return shift ? '_' : '-';
    case 0xBE: return shift ? '>' : '.';
    case 0xBF: return shift ? '?' : '/';
    case 0xC0: return shift ? '~' : '`';
    case 0xDB: return shift ? '{' : '[';
    case 0xDC: return shift ? '|' : '\\';
    case 0xDD: return shift ? '}' : ']';
    case 0xDE: return shift ? '"' : '\'';
    default: return -1;
  }
}

/* Returns 1 when a WM_CHAR / WM_SYSCHAR was posted (the SDK's "translated"); Wine returns TRUE for
 * every key message (message.c 4833-4862), this follows the SDK description (UNVERIFIED). */
uint16_t user_translate(const MSG16 *m) {
  UInput *in = IN();
  int ch;
  if (!in) return 0;
  if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return 0;
  bool ctrl = (in->keysync[VK_CONTROL] & 0x80) != 0 && m->message == WM_KEYDOWN;
  ch = vk_char(m->wParam, (in->keysync[VK_SHIFT] & 0x80) != 0, (in->keysync[VK_CAPITAL] & 1) != 0, ctrl);
  /* Under Ctrl a US layout yields a character only for letters (1..26), Space, Enter, Tab,
   * Backspace, Esc and the ASCII control codes [ \ ] ^ _ (= 1B 1C 1D 1E 1F); Ctrl+digit and
   * Ctrl+punctuation post nothing. Follows ToUnicodeEx under Ctrl as TranslateMessage calls it
   * (win32u/message.c 4830-4880 -> win32u/input.c; exact lines not re-read offline; UNVERIFIED
   * against Windows 3.1). */
  if (ctrl && ch >= 0) {
    unsigned vk = m->wParam;
    bool plain = (vk >= 0x41 && vk <= 0x5A) || vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB ||
                 vk == VK_BACK || vk == VK_ESCAPE;
    if (!plain) ch = (ch == '[' || ch == '\\' || ch == ']' || ch == '^' || ch == '_') ? (ch & 0x1F) : -1;
  }
  if (ch < 0) return 0;
  return user_post_ex(m->hwnd, m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR, (uint16_t)ch, m->lParam, m->time,
                      m->pt.x, m->pt.y);
}

/* ---- timers (W-timer) ---- */

uint16_t SetTimer(HWND16 hwnd, uint16_t id, uint16_t elapse, FARPTR proc) {
  UInput *in = IN();
  UTimer *t = NULL;
  if (!in) return 0;
  if (hwnd && !user_wnd(hwnd)) return 0;
  if (!hwnd) { /* the runtime picks an unused id */
    for (unsigned guard = 0; guard < 0x10000u; guard++) {
      bool used = false;
      id = in->next_timer_id ? in->next_timer_id : 1;
      in->next_timer_id = (uint16_t)(id + 1);
      for (unsigned i = 0; i < UT_MAX; i++)
        if (in->timers[i].used && in->timers[i].hwnd == 0 && in->timers[i].id == id) used = true;
      if (!used) break;
    }
  }
  for (unsigned i = 0; i < UT_MAX; i++)
    if (in->timers[i].used && in->timers[i].hwnd == hwnd && in->timers[i].id == id) t = &in->timers[i];
  if (!t)
    for (unsigned i = 0; i < UT_MAX; i++)
      if (!in->timers[i].used) {
        t = &in->timers[i];
        break;
      }
  if (!t) return 0;
  t->used = 1;
  t->hwnd = hwnd;
  t->id = id;
  t->elapse = elapse;
  t->proc = proc;
  t->due = user_tick() + elapse;
  return id;
}

uint16_t KillTimer(HWND16 hwnd, uint16_t id) {
  UInput *in = IN();
  if (!in) return 0;
  for (unsigned i = 0; i < UT_MAX; i++)
    if (in->timers[i].used && in->timers[i].hwnd == hwnd && in->timers[i].id == id) {
      in->timers[i].used = 0;
      return 1;
    }
  return 0;
}

uint32_t GetTickCount(void) { return user_tick(); }
uint32_t GetCurrentTime(void) { return user_tick(); }

/* find_expired_timer: the earliest due timer; a removed timer is re-armed from now (a missed
 * period is not made up). */
bool user_timer_fetch(MSG16 *out, HWND16 hwnd, uint16_t min, uint16_t max, bool remove) {
  UInput *in = IN();
  UTimer *best = NULL;
  uint32_t now;
  if (!in || !user_in_range(WM_TIMER, min, max)) return false;
  now = user_tick();
  for (unsigned i = 0; i < UT_MAX; i++) {
    UTimer *t = &in->timers[i];
    if (!t->used || (hwnd && t->hwnd != hwnd)) continue;
    if ((int32_t)(now - t->due) < 0) continue;
    if (!best || (int32_t)(t->due - best->due) < 0) best = t;
  }
  if (!best) return false;
  out->hwnd = best->hwnd;
  out->message = WM_TIMER;
  out->wParam = best->id;
  out->lParam = best->proc;
  out->time = now;
  out->pt.x = in->px;
  out->pt.y = in->py;
  if (remove) best->due = now + best->elapse;
  return true;
}
