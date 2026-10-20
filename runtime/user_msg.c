/* USER part 1 (task stars-0z4.6): the message queue, SendMessage, GetMessage, PeekMessage,
 * PostMessage, DispatchMessage and PostQuitMessage. Priorities follow Wine's queue
 * (server/queue.c DECL_HANDLER(get_message) 3336-3379): posted messages in FIFO order, then
 * WM_QUIT, then hardware input (mouse, keyboard: user_input.c, USER part 2), then synthesised
 * WM_PAINT, then timers (user_input.c). Plain C, no harness dependency. */
#include <string.h>

#include "runtime/user_internal.h"

/* ---- SendMessage ---- */

uint32_t SendMessage(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  UserState *st = user_state();
  UWnd *w;
  if (!st) return 0;
  if (hwnd == UHWND_BROADCAST) {
    HWND16 tops[UW_MAX];
    unsigned n = 0;
    for (UWnd *c = user_wnd(st->w[0].child); c && n < UW_MAX; c = user_wnd(c->next)) tops[n++] = user_hwnd(c);
    for (unsigned i = 0; i < n; i++)
      if (user_wnd(tops[i])) SendMessage(tops[i], msg, wParam, lParam);
    return 0;
  }
  w = user_wnd(hwnd);
  if (!w) return 0;
  return user_call_wndproc(w->wndproc, hwnd, msg, wParam, lParam);
}

/* ---- queue ---- */

uint16_t user_post_ex(HWND16 hwnd, uint16_t msg, uint16_t wp, uint32_t lp, uint32_t time, int16_t x, int16_t y) {
  UserState *st = user_state();
  UMsg *m;
  if (st->qcount >= UQ_MAX) return 0;
  m = &st->q[(st->qhead + st->qcount) % UQ_MAX];
  st->qcount++;
  m->hwnd = hwnd;
  m->msg = msg;
  m->wp = wp;
  m->lp = lp;
  m->time = time;
  m->x = x;
  m->y = y;
  return 1;
}

static uint16_t post_one(HWND16 hwnd, uint16_t msg, uint16_t wp, uint32_t lp) {
  UserState *st = user_state();
  return user_post_ex(hwnd, msg, wp, lp, st->clock++, 0, 0);
}

uint16_t PostMessage(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  UserState *st = user_state();
  if (!st) return 0;
  if (hwnd == UHWND_BROADCAST) {
    for (UWnd *c = user_wnd(st->w[0].child); c; c = user_wnd(c->next)) post_one(user_hwnd(c), msg, wParam, lParam);
    return 1;
  }
  if (hwnd && !user_wnd(hwnd)) return 0;
  return post_one(hwnd, msg, wParam, lParam);
}

/* Removes every queued message of a destroyed window (server/window.c:584 -> queue.c:2812). */
void user_queue_purge(HWND16 hwnd) {
  UserState *st = user_state();
  unsigned k = 0;
  UMsg keep[UQ_MAX];
  if (!st) return;
  user_input_window_gone(hwnd);
  for (unsigned i = 0; i < st->qcount; i++) {
    UMsg *m = &st->q[(st->qhead + i) % UQ_MAX];
    if (m->hwnd != hwnd) keep[k++] = *m;
  }
  st->qhead = 0;
  st->qcount = (uint16_t)k;
  for (unsigned i = 0; i < k; i++) st->q[i] = keep[i];
}

void PostQuitMessage(int16_t nExitCode) {
  UserState *st = user_state();
  if (!st) return;
  st->quit_pending = 1;
  st->quit_code = nExitCode;
}

bool user_in_range(uint16_t msg, uint16_t min, uint16_t max) {
  return (min == 0 && max == 0) || (msg >= min && msg <= max);
}
#define in_range user_in_range

static void fill_msg(MSG16 *out, const UMsg *m) {
  out->hwnd = m->hwnd;
  out->message = m->msg;
  out->wParam = m->wp;
  out->lParam = m->lp;
  out->time = m->time;
  out->pt.x = m->x;
  out->pt.y = m->y;
}

/* Finds the next message for the filter. Returns true and fills *out when there is one. */
static bool fetch(MSG16 *out, HWND16 hwnd, uint16_t min, uint16_t max, bool remove) {
  UserState *st = user_state();
  /* 1. posted messages, first in first out (W-queue 3336) */
  for (unsigned i = 0; i < st->qcount; i++) {
    UMsg *m = &st->q[(st->qhead + i) % UQ_MAX];
    if (hwnd && m->hwnd != hwnd) continue;
    if (!in_range(m->msg, min, max)) continue;
    fill_msg(out, m);
    if (remove) {
      for (unsigned j = i; j + 1 < st->qcount; j++)
        st->q[(st->qhead + j) % UQ_MAX] = st->q[(st->qhead + j + 1) % UQ_MAX];
      st->qcount--;
    }
    return true;
  }
  /* 2. WM_QUIT, only when no posted message matched (3346-3347; PM_REMOVE consumes it) */
  if (st->quit_pending && in_range(WM_QUIT, min, max)) {
    UMsg q = {0, WM_QUIT, (uint16_t)st->quit_code, 0, st->clock, 0, 0};
    fill_msg(out, &q);
    if (remove) st->quit_pending = 0;
    return true;
  }
  /* 2b. hardware input: mouse and keyboard, translated now (3350; user_input.c) */
  if (user_input_fetch(out, hwnd, min, max, remove)) return true;
  /* 3. WM_PAINT for the first window with an update region (3356) */
  if (in_range(WM_PAINT, min, max)) {
    for (unsigned guard = 0; guard < UW_MAX + 4; guard++) {
      HWND16 p = user_first_paint(hwnd);
      UWnd *pw = user_wnd(p);
      if (!p) break;
      if (pw->ncdirty) { /* WM_NCPAINT goes out first (Wine: send_ncpaint before the paint message) */
        pw->ncdirty = 0;
        SendMessage(p, WM_NCPAINT, 1, 0);
        pw = user_wnd(p);
        if (!pw) continue;
      }
      if (pw->nupd) {
        UMsg q = {p, WM_PAINT, 0, 0, st->clock, 0, 0};
        fill_msg(out, &q);
        return true;
      }
    }
  }
  /* 4. timers last (3370; user_input.c) */
  return user_timer_fetch(out, hwnd, min, max, remove);
}

uint16_t PeekMessage(FARPTR lpMsg, HWND16 hwnd, uint16_t min, uint16_t max, uint16_t wRemoveMsg) {
  MSG16 m;
  if (!user_state() || !lpMsg) return 0;
  if (!fetch(&m, hwnd, min, max, (wRemoveMsg & PM_REMOVE) != 0)) return 0;
  *(MSG16 *)LIN(lpMsg) = m;
  return 1;
}

/* GetMessage blocks. When nothing is available the host decides: user_wait_hook (the browser's
 * yield) may produce input and return true; without one, "no more input" ends the run: the
 * result is a WM_QUIT with FALSE returned, and user_input_exhausted is set. */
uint16_t GetMessage(FARPTR lpMsg, HWND16 hwnd, uint16_t min, uint16_t max) {
  MSG16 m;
  if (!user_state() || !lpMsg) return 0;
  for (;;) {
    if (fetch(&m, hwnd, min, max, true)) {
      *(MSG16 *)LIN(lpMsg) = m;
      return m.message != WM_QUIT ? 1 : 0;
    }
    if (user_wait_hook && user_wait_hook()) continue;
    break;
  }
  memset(&m, 0, sizeof m);
  m.message = WM_QUIT;
  *(MSG16 *)LIN(lpMsg) = m;
  user_input_exhausted = 1;
  return 0;
}

uint16_t TranslateMessage(FARPTR lpMsg) {
  if (!user_state() || !lpMsg) return 0;
  return user_translate(LIN(lpMsg)); /* user_input.c */
}

/* W-dispatch: dlls/user.exe16/message.c DispatchMessage16 1722-1744. */
uint32_t DispatchMessage(FARPTR lpMsg) {
  UserState *st = user_state();
  const MSG16 *m;
  UWnd *w;
  if (!st || !lpMsg) return 0;
  m = LIN(lpMsg);
  if ((m->message == WM_TIMER || m->message == 0x0118 /* WM_SYSTIMER */) && m->lParam)
    return user_call_wndproc(m->lParam, m->hwnd, m->message, m->wParam, user_tick());
  w = user_wnd(m->hwnd);
  if (!w) return 0;
  return user_call_wndproc(w->wndproc, m->hwnd, m->message, m->wParam, m->lParam);
}
