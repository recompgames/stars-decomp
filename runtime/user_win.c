/* USER part 1 (task stars-0z4.6): window classes, the window tree, creation, destruction,
 * show/hide, size and position, window words. Behaviour from the Windows 3.1 SDK where its
 * text was available (it was not: see docs/inventory.md section 13) and, for message order,
 * from Wine (wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
 * dlls/win32u/window.c and defwnd.c, dlls/user.exe16/window.c). Each pinned order cites its
 * source in runtime/tests/test_user_win.c. Plain C, no harness dependency: the PASCAL stack
 * decoding lives in harness/user_thunks.c. */
#include <stdlib.h>
#include <string.h>

#include "runtime/commdlg.h"
#include "runtime/frame.h"
#include "runtime/user_internal.h"

static uint32_t g_state_off;
int user_input_exhausted;
bool (*user_wait_hook)(void);

UserState *user_state(void) { return g_state_off ? (UserState *)(mem_arena() + g_state_off) : NULL; }

HWND16 user_desktop(void) { return (HWND16)UHWND_BASE; }

HWND16 user_hwnd(const UWnd *w) {
  UserState *st = user_state();
  return (HWND16)(UHWND_BASE + (unsigned)(w - st->w) * UHWND_STEP);
}

UWnd *user_wnd(HWND16 h) {
  UserState *st = user_state();
  unsigned i;
  if (!st || h < UHWND_BASE || ((h - UHWND_BASE) % UHWND_STEP)) return NULL;
  i = (h - UHWND_BASE) / UHWND_STEP;
  if (i >= UW_MAX || !st->w[i].used) return NULL;
  return &st->w[i];
}
#define IS_DESKTOP(h) ((h) == user_desktop())

FARPTR user_scratch(uint32_t bytes, uint16_t *handle) {
  *handle = GlobalAlloc(GHND, bytes);
  if (!*handle) return 0;
  return GlobalLock(*handle);
}
void user_scratch_free(uint16_t handle) {
  if (!handle) return;
  GlobalUnlock(handle);
  GlobalFree(handle);
}

static void far_strcpy(char *dst, size_t n, FARPTR src) {
  size_t i = 0;
  if (!src) {
    dst[0] = 0;
    return;
  }
  while (i + 1 < n) {
    char ch;
    if (!mem_check(src + (uint32_t)i, 1)) break;
    ch = *(const char *)LIN(src + (uint32_t)i);
    if (!ch) break;
    dst[i++] = ch;
  }
  dst[i] = 0;
}

static int strieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return 0;
  }
  return *a == *b;
}

/* ---- lifecycle ---- */

int user_init(void) {
  UserState *st;
  UWnd *d;
  if (g_state_off) return 0;
  st = rt_alloc(sizeof *st);
  if (!st) return -1;
  memset(st, 0, sizeof *st);
  g_state_off = mem_off(st);
  d = &st->w[0];
  d->used = 1;
  d->style = WS_VISIBLE | WS_CLIPCHILDREN;
  d->wndproc = USER_CB_DEFWNDPROC;
  d->wrect = d->crect = r_make(0, 0, GDI_SCREEN_W, GDI_SCREEN_H);
  strcpy(d->text, "");
  user_input_exhausted = 0;
  user_input_init(); /* USER part 2 state (focus, timers, cursor) */
  user_ctl_init();   /* BUTTON, STATIC and SCROLLBAR classes (stars-0z4.10) */
  user_dlg_init();   /* the dialog class (stars-0z4.12) */
  user_menu_init();  /* menus, accelerators, the pop-up window class (stars-0z4.13) */
  user_paint_desktop(); /* Windows 3.1 look: the desktop background (COLOR_BACKGROUND) */
  return 0;
}

void user_shutdown(void) {
  UserState *st = user_state();
  if (!st) return;
  user_ctl_shutdown();
  user_menu_shutdown();
  commdlg_shutdown();
  for (unsigned i = 0; i < UW_MAX; i++)
    if (st->w[i].used && st->w[i].extra_off) rt_free(mem_arena() + st->w[i].extra_off);
  rt_free(st);
  g_state_off = 0;
  user_input_exhausted = 0;
  user_wait_hook = NULL;
}

unsigned user_window_count(void) {
  UserState *st = user_state();
  unsigned n = 0;
  if (!st) return 0;
  for (unsigned i = 1; i < UW_MAX; i++) n += st->w[i].used;
  return n;
}

unsigned user_window_nupd(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  return w ? w->nupd : 0;
}

/* ---- window procedures ---- */

static uint32_t cb_proc(uint16_t off, HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  switch (off) {
    case 0x0000: return user_def_window_proc(h, msg, wp, lp);
    case 0x0010: return user_msgbox_proc(h, msg, wp, lp); /* the MessageBox window class (user_msgbox.c) */
    case 0x0020: return user_button_proc(h, msg, wp, lp);     /* standard controls (stars-0z4.10): ctl_button.c */
    case 0x0030: return user_static_proc(h, msg, wp, lp);     /* ctl_static.c */
    case 0x0040: return user_scrollbar_proc(h, msg, wp, lp);  /* ctl_scroll.c */
    case 0x0080: return user_dialog_proc(h, msg, wp, lp);     /* the dialog class: DefDlgProc (dialog.c, stars-0z4.12) */
    case 0x00a0: return user_filedlg_proc(h, msg, wp, lp);    /* the File Open / Save As dialog function (commdlg.c, stars-0z4.14) */
    case 0x0090: return user_menu_popup_proc(h, msg, wp, lp); /* the pop-up menu class (menu_ui.c, stars-0z4.13) */
    case 0x0050: return user_edit_proc(h, msg, wp, lp);       /* standard controls part 2 (stars-0z4.11): ctl_edit.c */
    case 0x0060: return user_listbox_proc(h, msg, wp, lp);    /* ctl_listbox.c */
    case 0x0070: return user_combobox_proc(h, msg, wp, lp);   /* ctl_combo.c */
    default: return 0;
  }
}

uint32_t user_call_wndproc(FARPTR proc, HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  uint16_t args[5];
  if (!proc) return 0;
  if (FP_SEG(proc) == MEM_SEL_CALLBACK) return cb_proc(FP_OFF(proc), hwnd, msg, wParam, lParam);
  args[0] = hwnd;
  args[1] = msg;
  args[2] = wParam;
  args[3] = (uint16_t)(lParam >> 16);
  args[4] = (uint16_t)lParam;
  return rt_call_far(proc, FAR_PASCAL, args, 5);
}

uint32_t CallWindowProc(FARPTR proc, HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  return user_call_wndproc(proc, hwnd, msg, wParam, lParam);
}

/* ---- geometry ---- */

void user_client_origin(const UWnd *w, int *x, int *y) {
  UserState *st = user_state();
  int ox = 0, oy = 0;
  for (const UWnd *c = w; c && c != &st->w[0]; c = user_wnd(c->parent)) {
    ox += c->crect.left;
    oy += c->crect.top;
  }
  *x = ox;
  *y = oy;
}

void user_window_origin(const UWnd *w, int *x, int *y) {
  UserState *st = user_state();
  int px = 0, py = 0;
  const UWnd *p = user_wnd(w->parent);
  if (p && p != &st->w[0]) user_client_origin(p, &px, &py);
  *x = px + w->wrect.left;
  *y = py + w->wrect.top;
}

bool user_client_rect_screen(HWND16 h, RECT16 *out) {
  UWnd *w = user_wnd(h);
  int ox, oy;
  if (!w) return false;
  user_client_origin(w, &ox, &oy);
  *out = r_make(ox, oy, ox + (w->crect.right - w->crect.left), oy + (w->crect.bottom - w->crect.top));
  return true;
}

bool user_visible(HWND16 h) {
  for (UWnd *w = user_wnd(h); w; w = user_wnd(w->parent)) {
    if (user_hwnd(w) == user_desktop()) return true;
    if (!(w->style & WS_VISIBLE)) return false;
  }
  return true;
}

/* ---- tree ---- */

static void unlink_wnd(UWnd *w) {
  UWnd *p = user_wnd(w->parent);
  HWND16 h = user_hwnd(w);
  if (!p) return;
  if (p->child == h) {
    p->child = w->next;
  } else {
    for (UWnd *s = user_wnd(p->child); s; s = user_wnd(s->next))
      if (s->next == h) {
        s->next = w->next;
        break;
      }
  }
  w->next = 0;
}

/* Inserts below `after` (0 = at the top, 1 = at the bottom, else a sibling handle). */
static void link_wnd(UWnd *w, HWND16 after) {
  UWnd *p = user_wnd(w->parent);
  HWND16 h = user_hwnd(w);
  if (!p) return;
  if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST || !p->child) {
    w->next = p->child;
    p->child = h;
    return;
  }
  if (after != HWND_BOTTOM) {
    UWnd *a = user_wnd(after);
    if (a && a->parent == w->parent && a != w) {
      w->next = a->next;
      a->next = h;
      return;
    }
  }
  {
    UWnd *last = user_wnd(p->child);
    while (last && last->next) last = user_wnd(last->next);
    w->next = 0;
    last->next = h;
  }
}

static unsigned children_of(const UWnd *p, HWND16 *out, unsigned max) {
  unsigned n = 0;
  for (const UWnd *c = user_wnd(p->child); c && n < max; c = user_wnd(c->next)) out[n++] = user_hwnd(c);
  return n;
}

/* ---- classes ---- */

uint16_t RegisterClass(FARPTR lpWndClass) {
  UserState *st = user_state();
  const WNDCLASS16 *wc;
  char name[UCLS_NAME];
  if (!st || !lpWndClass) return 0;
  wc = LIN(lpWndClass);
  if (FP_SEG(wc->lpszClassName) == 0) return 0; /* atoms as class names are not supported */
  far_strcpy(name, sizeof name, wc->lpszClassName);
  if (!name[0]) return 0;
  for (unsigned i = 0; i < UC_MAX; i++)
    if (st->c[i].used && strieq(st->c[i].name, name)) return 0; /* already registered */
  for (unsigned i = 0; i < UC_MAX; i++) {
    UClass *c = &st->c[i];
    if (c->used) continue;
    c->used = 1;
    c->style = wc->style;
    c->wndproc = wc->lpfnWndProc;
    c->cbcls = wc->cbClsExtra;
    c->cbwnd = wc->cbWndExtra;
    c->hinst = wc->hInstance;
    c->hicon = wc->hIcon;
    c->hcursor = wc->hCursor;
    c->hbr = wc->hbrBackground;
    c->menu = wc->lpszMenuName;
    strcpy(c->name, name);
    return (uint16_t)(UATOM_BASE + i);
  }
  return 0;
}

static int find_class(FARPTR name) {
  UserState *st = user_state();
  char buf[UCLS_NAME];
  if (FP_SEG(name) == 0) { /* MAKEINTATOM */
    unsigned a = FP_OFF(name);
    if (a >= UATOM_BASE && a - UATOM_BASE < UC_MAX && st->c[a - UATOM_BASE].used) return (int)(a - UATOM_BASE);
    return -1;
  }
  far_strcpy(buf, sizeof buf, name);
  for (unsigned i = 0; i < UC_MAX; i++)
    if (st->c[i].used && strieq(st->c[i].name, buf)) return (int)i;
  return -1;
}

/* ---- messages used by creation and destruction ---- */

static uint32_t snd(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) { return SendMessage(h, msg, wp, lp); }

static void destroy_window_internal(HWND16 h);

/* ---- creation ---- */

static HWND16 create_window(FARPTR clsname, FARPTR title, uint32_t style, uint32_t exstyle, int16_t x, int16_t y,
                            int16_t cx, int16_t cy, HWND16 parent_arg, HANDLE16 hmenu, HANDLE16 hinst, FARPTR param) {
  UserState *st = user_state();
  int ci = st ? find_class(clsname) : -1;
  UWnd *w;
  HWND16 h, parent, owner = 0;
  unsigned slot = 0;
  uint16_t hcs = 0;
  FARPTR csp;
  CREATESTRUCT16 *cs;
  RECT16 r;
  uint32_t ret;
  int px, py;

  if (ci < 0) return 0;
  if (style & WS_CHILD) {
    if (!user_wnd(parent_arg) || IS_DESKTOP(parent_arg)) return 0; /* a child needs a parent */
    parent = parent_arg;
  } else {
    parent = user_desktop();
    if (parent_arg && user_wnd(parent_arg) && !IS_DESKTOP(parent_arg)) {
      /* The owner is always a top-level window: a child is walked up to its top-level ancestor
       * (Wine server/window.c create_window 2251-2253). */
      UWnd *o = user_wnd(parent_arg);
      while (o && (o->style & WS_CHILD) && !IS_DESKTOP(o->parent)) o = user_wnd(o->parent);
      if (o) owner = user_hwnd(o);
    }
  }
  for (unsigned i = 1; i < UW_MAX; i++)
    if (!st->w[i].used) {
      slot = i;
      break;
    }
  if (!slot) return 0;

  /* CW_USEDEFAULT: origin 0,0 and as large as the screen allows (UNVERIFIED for Windows 3.1,
   * which cascades top-level windows). */
  if (!(style & WS_CHILD)) {
    if (x == CW_USEDEFAULT) x = 0;
    if (y == CW_USEDEFAULT) y = 0;
    if (cx == CW_USEDEFAULT) cx = (int16_t)(GDI_SCREEN_W - x);
    if (cy == CW_USEDEFAULT) cy = (int16_t)(GDI_SCREEN_H - y);
  } else {
    if (x == CW_USEDEFAULT) x = 0;
    if (y == CW_USEDEFAULT) y = 0;
    if (cx == CW_USEDEFAULT) cx = 0;
    if (cy == CW_USEDEFAULT) cy = 0;
  }

  w = &st->w[slot];
  memset(w, 0, sizeof *w);
  w->used = 1;
  w->cls = (uint16_t)ci;
  w->parent = parent;
  w->owner = owner;
  w->style = style & ~(WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE);
  w->exstyle = exstyle;
  w->id = hmenu;
  w->hinst = hinst;
  w->wndproc = st->c[ci].wndproc;
  w->nextra = (uint16_t)(st->c[ci].cbwnd > 0 ? st->c[ci].cbwnd : 0);
  if (w->nextra) {
    void *e = rt_alloc(w->nextra);
    if (e) {
      memset(e, 0, w->nextra);
      w->extra_off = mem_off(e);
    } else {
      w->nextra = 0;
    }
  }
  far_strcpy(w->text, sizeof w->text, title);
  if (!(style & (WS_CHILD | WS_POPUP))) w->need_size = 1;
  h = user_hwnd(w);
  w->minpos = (POINT16){-1, -1};
  w->hmenu = user_menu_for_new_window(h, style, hmenu, st->c[ci].hinst ? st->c[ci].hinst : hinst, st->c[ci].menu); /* the class menu is loaded from the class's module */ /* menu bar: stars-0z4.13 */
  user_sb_reset(h);
  link_wnd(w, (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP); /* window.c:6117-6122: children go to the bottom */

  csp = user_scratch(sizeof(CREATESTRUCT16), &hcs);
  if (!csp) {
    st->w[slot].used = 0;
    return 0;
  }
  cs = LIN(csp);
  cs->lpCreateParams = param;
  cs->hInstance = hinst;
  cs->hMenu = hmenu;
  cs->hwndParent = parent_arg;
  cs->cy = cy;
  cs->cx = cx;
  cs->y = y;
  cs->x = x;
  cs->style = style;
  cs->lpszName = title;
  cs->lpszClass = clsname;
  cs->dwExStyle = exstyle;

  /* W-create (test_user_win.c): top-level and thick-frame windows first ask for their limits
   * (window.c:6070-6081) and are clamped to them. */
  if ((style & WS_THICKFRAME) || !(style & (WS_POPUP | WS_CHILD))) {
    uint16_t hm = 0;
    FARPTR mp = user_scratch(sizeof(MINMAXINFO16), &hm);
    if (mp) {
      MINMAXINFO16 *mm = LIN(mp);
      mm->ptMaxSize = (POINT16){GDI_SCREEN_W + 8, GDI_SCREEN_H + 8}; /* UNVERIFIED defaults */
      mm->ptMaxTrackSize = mm->ptMaxSize;
      mm->ptMinTrackSize = (POINT16){0, 0};
      snd(h, WM_GETMINMAXINFO, 0, mp);
      if (user_wnd(h)) {
        mm = LIN(mp);
        if (cx > mm->ptMaxTrackSize.x) cx = mm->ptMaxTrackSize.x;
        if (cy > mm->ptMaxTrackSize.y) cy = mm->ptMaxTrackSize.y;
        if (cx < mm->ptMinTrackSize.x) cx = mm->ptMinTrackSize.x;
        if (cy < mm->ptMinTrackSize.y) cy = mm->ptMinTrackSize.y;
      }
      user_scratch_free(hm);
    }
  }
  w = user_wnd(h);
  if (!w) {
    user_scratch_free(hcs);
    return 0;
  }
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;
  w->wrect = r_make(x, y, x + cx, y + cy);
  w->crect = w->wrect;

  /* WM_NCCREATE (window.c:6097-6100); a zero result aborts, and "failed:" (6210-6213) calls
   * destroy_window, which sends only WM_NCDESTROY. */
  ret = snd(h, WM_NCCREATE, 0, csp);
  if (!user_wnd(h)) {
    user_scratch_free(hcs);
    return 0;
  }
  if (!(ret & 0xFFFF)) {
    destroy_window_internal(h);
    user_scratch_free(hcs);
    return 0;
  }

  /* WM_NCCALCSIZE with wParam FALSE and a RECT in screen coordinates (6115-6125). */
  {
    uint16_t hr = 0;
    FARPTR rp = user_scratch(sizeof(RECT16), &hr);
    if (rp) {
      w = user_wnd(h);
      user_window_origin(w, &px, &py);
      r = r_make(px, py, px + (w->wrect.right - w->wrect.left), py + (w->wrect.bottom - w->wrect.top));
      *(RECT16 *)LIN(rp) = r;
      snd(h, WM_NCCALCSIZE, 0, rp);
      w = user_wnd(h);
      if (w) {
        int ppx = px - w->wrect.left, ppy = py - w->wrect.top; /* parent client origin */
        r = *(RECT16 *)LIN(rp);
        w->crect = r_offset(r, -ppx, -ppy);
      }
      user_scratch_free(hr);
    }
  }
  if (!user_wnd(h)) {
    user_scratch_free(hcs);
    return 0;
  }

  /* WM_CREATE (6134-6135): -1 in AX (a Win16 int) aborts. */
  ret = snd(h, WM_CREATE, 0, csp);
  if (!user_wnd(h)) {
    user_scratch_free(hcs);
    return 0;
  }
  if ((ret & 0xFFFF) == 0xFFFF) {
    destroy_window_internal(h);
    user_scratch_free(hcs);
    return 0;
  }

  /* Children and popups get WM_SIZE and WM_MOVE now (6146-6152); top-level windows get them
   * from their first ShowWindow (window.c:5128-5139). */
  w = user_wnd(h);
  if (!w->need_size) {
    snd(h, WM_SIZE, SIZE_RESTORED, make_long((uint16_t)(w->crect.right - w->crect.left), (uint16_t)(w->crect.bottom - w->crect.top)));
    w = user_wnd(h);
    if (w) snd(h, WM_MOVE, 0, make_long((uint16_t)w->crect.left, (uint16_t)w->crect.top));
  }

  /* WM_PARENTNOTIFY, Win16 layout (6172; send_parent_notify 4551-4561). */
  w = user_wnd(h);
  if (w && (w->style & (WS_CHILD | WS_POPUP)) == WS_CHILD && !(w->exstyle & WS_EX_NOPARENTNOTIFY) &&
      !IS_DESKTOP(w->parent))
    snd(w->parent, WM_PARENTNOTIFY, WM_CREATE, make_long(h, w->id));
  if (!user_wnd(h)) {
    user_scratch_free(hcs);
    return 0;
  }

  if (style & WS_VISIBLE) {
    int16_t cmd = SW_SHOW;
    if (style & WS_MINIMIZE) cmd = SW_SHOWMINIMIZED;
    else if (style & WS_MAXIMIZE) cmd = SW_SHOWMAXIMIZED;
    ShowWindow(h, cmd);
  }
  user_scratch_free(hcs);
  return user_wnd(h) ? h : 0;
}

HWND16 user_create_window_ex(uint32_t exstyle, FARPTR lpClassName, FARPTR lpWindowName, uint32_t dwStyle, int16_t x, int16_t y,
                             int16_t cx, int16_t cy, HWND16 hwndParent, HANDLE16 hMenu, HANDLE16 hInstance, FARPTR lpParam) {
  return create_window(lpClassName, lpWindowName, dwStyle, exstyle, x, y, cx, cy, hwndParent, hMenu, hInstance, lpParam);
}

HWND16 CreateWindow(FARPTR lpClassName, FARPTR lpWindowName, uint32_t dwStyle, int16_t x, int16_t y, int16_t cx,
                    int16_t cy, HWND16 hwndParent, HANDLE16 hMenu, HANDLE16 hInstance, FARPTR lpParam) {
  return create_window(lpClassName, lpWindowName, dwStyle, 0, x, y, cx, cy, hwndParent, hMenu, hInstance, lpParam);
}

/* ---- SetWindowPos ---- */

/* Invalidate the rectangle r (in p's client coordinates) in every visible descendant of p
 * that it intersects, skipping the window `skip` and its subtree. Wine redraw_window with
 * RDW_ALLCHILDREN (dlls/win32u/window.c, update_window_state / expose paths). */
static void invalidate_tree(const UWnd *p, RECT16 r, HWND16 skip) {
  for (UWnd *s = user_wnd(p->child); s; s = user_wnd(s->next)) {
    RECT16 o;
    if (user_hwnd(s) == skip || !(s->style & WS_VISIBLE)) continue;
    if (!r_empty(r_isect(r, s->wrect)) && (s->crect.left != s->wrect.left || s->crect.top != s->wrect.top ||
                                          s->crect.right != s->wrect.right || s->crect.bottom != s->wrect.bottom))
      s->ncdirty = 1; /* the exposed area reaches the frame, caption or scroll bars: WM_NCPAINT */
    o = r_isect(r, s->crect); /* s's client rectangle is in p's client coordinates, like r */
    if (r_empty(o)) continue;
    o = r_offset(o, -s->crect.left, -s->crect.top);
    user_upd_add(s, o, true);
    invalidate_tree(s, o, skip);
  }
}

static void invalidate_exposed(HWND16 h, const UWnd *w, RECT16 old_wrect) {
  /* Windows invalidates what a moved, resized or hidden window uncovered. Here: the parent's
   * client area under the old rectangle and every visible window under it, children of the
   * siblings included (RDW_ALLCHILDREN). The desktop's children are the top-level windows. */
  UWnd *p = user_wnd(w->parent);
  if (!p) return;
  if (IS_DESKTOP(w->parent)) user_nc_expose(h, old_wrect); /* Windows 3.1 look: the desktop shows through */
  if (!IS_DESKTOP(w->parent)) {
    RECT16 r = r_isect(old_wrect, r_make(0, 0, p->crect.right - p->crect.left, p->crect.bottom - p->crect.top));
    if (!r_empty(r)) user_upd_add(p, r, true);
  }
  invalidate_tree(p, old_wrect, h);
}

static bool set_window_pos(HWND16 h, HWND16 after, int16_t x, int16_t y, int16_t cx, int16_t cy, uint16_t flags) {
  UWnd *w = user_wnd(h);
  uint16_t hwp = 0;
  FARPTR wpp;
  WINDOWPOS16 *wp;
  RECT16 old_w, old_c, nw, nc;
  uint16_t orig = flags;
  bool shown_now = false, hidden_now = false;
  if (!w || IS_DESKTOP(h)) return false;
  wpp = user_scratch(sizeof(WINDOWPOS16), &hwp);
  if (!wpp) return false;
  wp = LIN(wpp);
  wp->hwnd = h;
  wp->hwndInsertAfter = after;
  wp->x = x;
  wp->y = y;
  wp->cx = cx;
  wp->cy = cy;
  wp->flags = flags;

  /* W-swp: window.c calc_winpos 3731-3734. */
  if (!(flags & SWP_NOSENDCHANGING) && !((flags & SWP_AGG_NOCLIENTCHANGE) && (flags & SWP_SHOWWINDOW)))
    snd(h, WM_WINDOWPOSCHANGING, 0, wpp);
  w = user_wnd(h);
  if (!w) {
    user_scratch_free(hwp);
    return false;
  }
  wp = LIN(wpp);
  flags = wp->flags;
  after = wp->hwndInsertAfter;

  old_w = w->wrect;
  old_c = w->crect;
  nw = old_w;
  if (!(flags & SWP_NOSIZE)) {
    nw.right = (int16_t)(nw.left + (wp->cx < 0 ? 0 : wp->cx));
    nw.bottom = (int16_t)(nw.top + (wp->cy < 0 ? 0 : wp->cy));
  }
  if (!(flags & SWP_NOMOVE)) {
    int dx = wp->x - old_w.left, dy = wp->y - old_w.top;
    nw.left = wp->x;
    nw.top = wp->y;
    nw.right = (int16_t)(nw.right + dx);
    nw.bottom = (int16_t)(nw.bottom + dy);
  }
  nc = old_c;
  if (!(flags & SWP_NOMOVE)) nc = r_offset(old_c, nw.left - old_w.left, nw.top - old_w.top);
  flags |= SWP_NOCLIENTMOVE | SWP_NOCLIENTSIZE;

  /* W-swp: calc_ncsize 3855-3884 sends WM_NCCALCSIZE (TRUE) unless only the position changes. */
  if ((flags & (SWP_FRAMECHANGED | SWP_NOSIZE)) != SWP_NOSIZE) {
    uint16_t hp = 0, hq = 0;
    FARPTR pp = user_scratch(sizeof(NCCALCSIZE_PARAMS16), &hp);
    FARPTR qp = user_scratch(sizeof(WINDOWPOS16), &hq);
    if (pp && qp) {
      NCCALCSIZE_PARAMS16 *np = LIN(pp);
      WINDOWPOS16 *cp = LIN(qp);
      *cp = *wp;
      cp->flags = flags;
      if (flags & SWP_NOMOVE) {
        cp->x = old_w.left;
        cp->y = old_w.top;
      }
      if (flags & SWP_NOSIZE) {
        cp->cx = (int16_t)(old_w.right - old_w.left);
        cp->cy = (int16_t)(old_w.bottom - old_w.top);
      }
      np->rgrc[0] = nw;
      np->rgrc[1] = old_w;
      np->rgrc[2] = old_c;
      np->lppos = qp;
      /* the window still has its old rectangles while the handler runs */
      snd(h, WM_NCCALCSIZE, 1, pp);
      w = user_wnd(h);
      if (w) {
        np = LIN(pp);
        nc = np->rgrc[0];
      }
    }
    user_scratch_free(hp);
    user_scratch_free(hq);
    if (!w) {
      user_scratch_free(hwp);
      return false;
    }
  }
  if (nc.left != old_c.left || nc.top != old_c.top) flags = (uint16_t)(flags & ~SWP_NOCLIENTMOVE);
  if ((nc.right - nc.left) != (old_c.right - old_c.left) || (nc.bottom - nc.top) != (old_c.bottom - old_c.top))
    flags = (uint16_t)(flags & ~SWP_NOCLIENTSIZE);

  w->wrect = nw;
  w->crect = nc;
  if ((flags & SWP_SHOWWINDOW) && !(w->style & WS_VISIBLE)) {
    w->style |= WS_VISIBLE;
    shown_now = true;
  }
  if ((flags & SWP_HIDEWINDOW) && (w->style & WS_VISIBLE)) {
    w->style &= ~WS_VISIBLE;
    hidden_now = true;
  }
  if (!(flags & SWP_NOZORDER)) {
    unlink_wnd(w);
    link_wnd(w, after);
  }
  if (!(flags & SWP_NOREDRAW)) {
    bool geometry = (nw.left != old_w.left || nw.top != old_w.top || nw.right != old_w.right || nw.bottom != old_w.bottom);
    bool vis = (w->style & WS_VISIBLE) != 0;
    if (hidden_now || (vis && geometry)) invalidate_exposed(h, w, old_w);
    if (vis && (shown_now || geometry || (flags & SWP_FRAMECHANGED))) {
      RECT16 all = r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top);
      user_upd_add(w, all, true);
      invalidate_tree(w, all, 0); /* stars-0z4.17: visible children too (RDW_ALLCHILDREN) */
      if (w->crect.left != w->wrect.left || w->crect.top != w->wrect.top || w->crect.right != w->wrect.right ||
          w->crect.bottom != w->wrect.bottom)
        w->ncdirty = 1; /* there is a non-client area to draw (Windows 3.1 look) */
    }
  }

  /* A top-level window that was JUST shown (shown_now) is activated unless SWP_NOACTIVATE is set
   * (window.c show_window ~4190 -> set_active_window(hwnd, NULL, FALSE, TRUE), before WM_WINDOWPOSCHANGED
   * goes out). UNVERIFIED against Windows 3.1: the order of the activation messages is Wine's.
   * KNOWN DIFFERENCE: Wine's set_window_pos activates on any SetWindowPos without SWP_NOACTIVATE
   * or SWP_HIDEWINDOW; here only the shown case activates (all the game's startup needs). */
  if (shown_now && !(flags & SWP_NOACTIVATE) && user_wnd(h) && !(user_wnd(h)->style & WS_CHILD))
    user_activate_shown(h);
  if (!user_wnd(h)) {
    user_scratch_free(hwp);
    return false;
  }

  /* W-swp: 4225-4234. WINDOWPOSCHANGED always carries the final position. */
  wp = LIN(wpp);
  wp->x = nw.left;
  wp->y = nw.top;
  wp->cx = (int16_t)(nw.right - nw.left);
  wp->cy = (int16_t)(nw.bottom - nw.top);
  wp->flags = flags;
  if (((flags & SWP_AGG_STATUSFLAGS) != SWP_AGG_NOPOSCHANGE) &&
      !((orig & SWP_AGG_NOCLIENTCHANGE) && (orig & SWP_SHOWWINDOW)))
    snd(h, WM_WINDOWPOSCHANGED, 0, wpp);
  user_scratch_free(hwp);
  return true;
}

uint16_t SetWindowPos(HWND16 hwnd, HWND16 hwndInsertAfter, int16_t x, int16_t y, int16_t cx, int16_t cy, uint16_t flags) {
  return set_window_pos(hwnd, hwndInsertAfter, x, y, cx, cy, flags) ? 1 : 0;
}

uint16_t MoveWindow(HWND16 hwnd, int16_t x, int16_t y, int16_t cx, int16_t cy, uint16_t fRepaint) {
  uint16_t flags = SWP_NOZORDER | SWP_NOACTIVATE;
  if (!fRepaint) flags |= SWP_NOREDRAW;
  return set_window_pos(hwnd, 0, x, y, cx, cy, flags) ? 1 : 0;
}

/* ---- ShowWindow ---- */

uint16_t ShowWindow(HWND16 hwnd, int16_t cmd) {
  UWnd *w = user_wnd(hwnd);
  bool was_visible, show_flag = true, state_change = false;
  uint16_t swp = 0;
  RECT16 tgt = {0, 0, 0, 0};
  bool have_tgt = false;
  if (!w || IS_DESKTOP(hwnd)) return 0;
  was_visible = (w->style & WS_VISIBLE) != 0;

  /* W-show: window.c show_window 5040-5090. Minimising, maximising and restoring also move the
   * window (Windows 3.1 look, stars-0z4.9: user_nc_place; the restored rectangle is kept in
   * UWnd.nrect). */
  switch (cmd) {
    case SW_HIDE:
      if (!was_visible) return 0;
      swp = SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
      show_flag = false;
      break;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
    case SW_SHOWMINNOACTIVE:
      if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE))) w->nrect = w->wrect;
      w->style = (w->style | WS_MINIMIZE) & ~(uint32_t)WS_MAXIMIZE;
      swp = SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_STATECHANGED;
      user_nc_place(hwnd, 2, &tgt);
      have_tgt = true;
      if (!was_visible) swp |= SWP_SHOWWINDOW;
      if (cmd == SW_SHOWMINNOACTIVE) swp |= SWP_NOACTIVATE;
      state_change = true;
      break;
    case SW_SHOWMAXIMIZED:
      if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE))) w->nrect = w->wrect;
      w->style = (w->style | WS_MAXIMIZE) & ~(uint32_t)WS_MINIMIZE;
      swp = SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_STATECHANGED;
      user_nc_place(hwnd, 1, &tgt);
      w = user_wnd(hwnd);
      if (!w) goto done;
      have_tgt = true;
      if (!was_visible) swp |= SWP_SHOWWINDOW;
      state_change = true;
      break;
    case SW_SHOWNA:
      swp = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;
      if (!was_visible) swp |= SWP_SHOWWINDOW;
      break;
    case SW_SHOW:
      if (was_visible) goto done;
      swp = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE;
      break;
    case SW_SHOWNOACTIVATE:
    case SW_RESTORE:
    case SW_SHOWNORMAL:
      if (!was_visible) swp |= SWP_SHOWWINDOW;
      if (cmd == SW_SHOWNOACTIVATE) swp |= SWP_NOACTIVATE | SWP_NOZORDER;
      if (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) {
        w->style &= ~(uint32_t)(WS_MINIMIZE | WS_MAXIMIZE);
        swp |= SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_STATECHANGED;
        state_change = true;
        tgt = w->nrect;
        have_tgt = !r_empty(tgt);
      } else {
        if (was_visible) goto done;
        swp |= SWP_NOSIZE | SWP_NOMOVE;
      }
      break;
    default:
      goto done;
  }
  if ((w->style & WS_CHILD) && !state_change) swp |= SWP_NOACTIVATE | SWP_NOZORDER;

  if ((show_flag != was_visible || cmd == SW_SHOWNA) && !state_change) {
    snd(hwnd, WM_SHOWWINDOW, show_flag, 0);
    if (!user_wnd(hwnd)) goto done;
  }
  w = user_wnd(hwnd);
  if (!IS_DESKTOP(w->parent) && !user_visible(w->parent) && !state_change) {
    /* the parent is hidden: only the style bit changes */
    if (show_flag) w->style |= WS_VISIBLE;
    else w->style &= ~(uint32_t)WS_VISIBLE;
  } else {
    if (have_tgt)
      set_window_pos(hwnd, HWND_TOP, tgt.left, tgt.top, (int16_t)(tgt.right - tgt.left), (int16_t)(tgt.bottom - tgt.top),
                     (uint16_t)(swp & ~(SWP_NOSIZE | SWP_NOMOVE)));
    else
      set_window_pos(hwnd, HWND_TOP, 0, 0, 0, 0, swp);
  }
  if (cmd == SW_HIDE) goto done;
  /* W-show: 5128-5139, the first size message of a window created hidden. */
  w = user_wnd(hwnd);
  if (w && w->need_size) {
    uint16_t sz = SIZE_RESTORED;
    uint32_t lp = make_long((uint16_t)(w->crect.right - w->crect.left), (uint16_t)(w->crect.bottom - w->crect.top));
    w->need_size = 0;
    if (w->style & WS_MAXIMIZE) sz = SIZE_MAXIMIZED;
    else if (w->style & WS_MINIMIZE) {
      sz = SIZE_MINIMIZED;
      lp = 0;
    }
    snd(hwnd, WM_SIZE, sz, lp);
    w = user_wnd(hwnd);
    if (w) snd(hwnd, WM_MOVE, 0, make_long((uint16_t)w->crect.left, (uint16_t)w->crect.top));
  }
done:
  return was_visible ? 1 : 0;
}

/* ---- destruction ---- */

static void send_destroy_message(HWND16 h) {
  UWnd *w = user_wnd(h);
  HWND16 kids[UW_MAX];
  unsigned n;
  if (!w) return;
  snd(h, WM_DESTROY, 0, 0); /* W-destroy: window.c 5414 */
  w = user_wnd(h);
  if (!w) return; /* destroyed itself in WM_DESTROY */
  n = children_of(w, kids, UW_MAX);
  for (unsigned i = 0; i < n; i++)
    if (user_wnd(kids[i])) send_destroy_message(kids[i]);
}

/* Frees the window and, first, its children (window.c destroy_window 5467-5503): children, then
 * WM_NCDESTROY to the window, which is already unlinked from its parent. */
static void destroy_window_internal(HWND16 h) {
  UWnd *w = user_wnd(h);
  HWND16 kids[UW_MAX];
  unsigned n;
  if (!w) return;
  n = children_of(w, kids, UW_MAX);
  for (unsigned i = 0; i < n; i++) destroy_window_internal(kids[i]);
  w = user_wnd(h);
  if (!w) return;
  unlink_wnd(w);
  snd(h, WM_NCDESTROY, 0, 0);
  w = user_wnd(h);
  if (!w) return;
  user_queue_purge(h); /* W-cleanup: server/window.c:584 */
  user_sb_reset(h);
  user_menu_window_gone(h, w->hmenu, w->hsys);
  w = user_wnd(h);
  if (!w) return;
  if (w->extra_off) rt_free(mem_arena() + w->extra_off);
  memset(w, 0, sizeof *w);
}

uint16_t DestroyWindow(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  bool is_child;
  if (!w || IS_DESKTOP(hwnd)) return 0;
  is_child = (w->style & WS_CHILD) != 0;
  /* W-destroy: window.c user_destroy_window 5546-5621. */
  if (is_child) {
    if ((w->style & (WS_CHILD | WS_POPUP)) == WS_CHILD && !(w->exstyle & WS_EX_NOPARENTNOTIFY) && !IS_DESKTOP(w->parent))
      snd(w->parent, WM_PARENTNOTIFY, WM_DESTROY, make_long(hwnd, w->id));
  }
  if (!user_wnd(hwnd)) return 1;
  user_input_pre_destroy(hwnd); /* capture is released early (USER part 2) */
  if (!user_wnd(hwnd)) return 1;
  w = user_wnd(hwnd);
  if (w->style & WS_VISIBLE) {
    /* "Only child windows receive WM_SHOWWINDOW in DestroyWindow()" (5580) */
    if (is_child) ShowWindow(hwnd, SW_HIDE);
    else set_window_pos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
  }
  if (!user_wnd(hwnd)) return 1;
  if (!is_child) {
    /* owned windows go first, with their owner intact (Wine keeps it during same-thread
     * destruction). Snapshot the list; a window that survives its own destruction cannot make
     * this loop forever. */
    UserState *st = user_state();
    HWND16 owned[UW_MAX];
    unsigned n = 0;
    for (unsigned i = 1; i < UW_MAX; i++)
      if (st->w[i].used && st->w[i].owner == hwnd) owned[n++] = user_hwnd(&st->w[i]);
    for (unsigned i = 0; i < n; i++) {
      const UWnd *ow = user_wnd(owned[i]);
      if (ow && ow->owner == hwnd) DestroyWindow(owned[i]); /* still alive and still owned */
    }
  }
  if (!user_wnd(hwnd)) return 1;
  /* activate_other_window runs inside send_destroy_message (5398-5410), after the hide and the
   * owned windows; the focus of a destroyed child moves to its parent after its SW_HIDE. */
  user_input_leave_destroy(hwnd);
  if (!user_wnd(hwnd)) return 1;
  send_destroy_message(hwnd);
  if (!user_wnd(hwnd)) return 1;
  destroy_window_internal(hwnd);
  return 1;
}

/* ---- queries ---- */

void GetWindowRect(HWND16 hwnd, FARPTR lpRect) {
  UWnd *w = user_wnd(hwnd);
  RECT16 *r = LIN(lpRect);
  int ox, oy;
  if (!w) {
    *r = r_make(0, 0, 0, 0);
    return;
  }
  user_window_origin(w, &ox, &oy);
  *r = r_make(ox, oy, ox + (w->wrect.right - w->wrect.left), oy + (w->wrect.bottom - w->wrect.top));
}

void GetClientRect(HWND16 hwnd, FARPTR lpRect) {
  UWnd *w = user_wnd(hwnd);
  RECT16 *r = LIN(lpRect);
  if (!w) {
    *r = r_make(0, 0, 0, 0);
    return;
  }
  *r = r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top);
}

static void origin_of(HWND16 h, int *x, int *y) {
  UWnd *w = h ? user_wnd(h) : NULL;
  *x = *y = 0;
  if (w) user_client_origin(w, x, y);
}

void ClientToScreen(HWND16 hwnd, FARPTR lpPoint) {
  POINT16 *p = LIN(lpPoint);
  int ox, oy;
  origin_of(hwnd, &ox, &oy);
  p->x = (int16_t)(p->x + ox);
  p->y = (int16_t)(p->y + oy);
}

void ScreenToClient(HWND16 hwnd, FARPTR lpPoint) {
  POINT16 *p = LIN(lpPoint);
  int ox, oy;
  origin_of(hwnd, &ox, &oy);
  p->x = (int16_t)(p->x - ox);
  p->y = (int16_t)(p->y - oy);
}

void MapWindowPoints(HWND16 hwndFrom, HWND16 hwndTo, FARPTR lpPoints, uint16_t cPoints) {
  int fx, fy, tx, ty;
  POINT16 *p = LIN(lpPoints);
  origin_of(hwndFrom, &fx, &fy);
  origin_of(hwndTo, &tx, &ty);
  for (unsigned i = 0; i < cPoints; i++) {
    p[i].x = (int16_t)(p[i].x + fx - tx);
    p[i].y = (int16_t)(p[i].y + fy - ty);
  }
}

static HWND16 hit_children(const UWnd *parent, int x, int y, const HWND16 *skip, unsigned nskip) {
  for (const UWnd *c = user_wnd(parent->child); c; c = user_wnd(c->next)) {
    int ox, oy;
    RECT16 wr;
    bool skipped = false;
    if (!(c->style & WS_VISIBLE)) continue;
    for (unsigned i = 0; i < nskip; i++) skipped |= skip[i] == user_hwnd(c);
    if (skipped) continue; /* answered HTTRANSPARENT: the window below gets the point */
    user_window_origin(c, &ox, &oy);
    wr = r_make(ox, oy, ox + (c->wrect.right - c->wrect.left), oy + (c->wrect.bottom - c->wrect.top));
    if (!r_contains(wr, x, y)) continue;
    {
      HWND16 deeper = hit_children(c, x, y, skip, nskip);
      return deeper ? deeper : user_hwnd(c);
    }
  }
  return 0;
}

HWND16 WindowFromPoint(uint32_t pt) {
  UserState *st = user_state();
  HWND16 h = hit_children(&st->w[0], (int16_t)(pt & 0xFFFF), (int16_t)(pt >> 16), NULL, 0);
  return h ? h : user_desktop();
}

/* WindowFromPoint that passes over the windows in skip (those that answered WM_NCHITTEST with
 * HTTRANSPARENT, e.g. group boxes and static text), together with their children. */
HWND16 user_window_from_point_skip(uint32_t pt, const HWND16 *skip, unsigned nskip) {
  UserState *st = user_state();
  HWND16 h = hit_children(&st->w[0], (int16_t)(pt & 0xFFFF), (int16_t)(pt >> 16), skip, nskip);
  return h ? h : user_desktop();
}

HWND16 GetDesktopWindow(void) { return user_desktop(); }

HWND16 GetParent(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  if (!w || IS_DESKTOP(hwnd)) return 0;
  if (w->style & WS_CHILD) return IS_DESKTOP(w->parent) ? 0 : w->parent;
  return w->owner;
}

HWND16 GetWindow(HWND16 hwnd, uint16_t cmd) {
  UWnd *w = user_wnd(hwnd), *p;
  if (!w) return 0;
  p = user_wnd(w->parent);
  switch (cmd) {
    case GW_OWNER: return w->owner;
    case GW_CHILD: return w->child;
    case GW_HWNDNEXT: return w->next;
    case GW_HWNDFIRST: return p ? p->child : 0;
    case GW_HWNDLAST: {
      UWnd *l = p ? user_wnd(p->child) : NULL;
      while (l && l->next) l = user_wnd(l->next);
      return l ? user_hwnd(l) : 0;
    }
    case GW_HWNDPREV: {
      if (!p || p->child == hwnd) return 0;
      for (UWnd *s = user_wnd(p->child); s; s = user_wnd(s->next))
        if (s->next == hwnd) return user_hwnd(s);
      return 0;
    }
    default: return 0;
  }
}

uint16_t IsWindow(HWND16 hwnd) { return user_wnd(hwnd) ? 1 : 0; }
uint16_t IsWindowVisible(HWND16 hwnd) { return (user_wnd(hwnd) && user_visible(hwnd)) ? 1 : 0; }
uint16_t IsIconic(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  return (w && (w->style & WS_MINIMIZE)) ? 1 : 0;
}
uint16_t IsZoomed(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  return (w && (w->style & WS_MAXIMIZE)) ? 1 : 0;
}
uint16_t IsWindowEnabled(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  return (w && !(w->style & WS_DISABLED)) ? 1 : 0;
}

/* W-enable: window.c NtUserEnableWindow 1126-1167. Returns whether the window WAS disabled. */
uint16_t EnableWindow(HWND16 hwnd, uint16_t bEnable) {
  UWnd *w = user_wnd(hwnd);
  bool was_disabled;
  if (!w || IS_DESKTOP(hwnd)) return 0;
  if (bEnable) {
    was_disabled = (w->style & WS_DISABLED) != 0;
    w->style &= ~(uint32_t)WS_DISABLED;
    if (was_disabled) snd(hwnd, WM_ENABLE, 1, 0);
  } else {
    snd(hwnd, WM_CANCELMODE, 0, 0);
    w = user_wnd(hwnd);
    if (!w) return 0;
    was_disabled = (w->style & WS_DISABLED) != 0;
    w->style |= WS_DISABLED;
    if (!was_disabled) {
      user_input_disabled(hwnd); /* a disabled window cannot have the focus (window.c 1157) */
      snd(hwnd, WM_ENABLE, 0, 0);
    }
  }
  return was_disabled ? 1 : 0;
}

int16_t GetWindowText(HWND16 hwnd, FARPTR lpString, int16_t nMaxCount) {
  if (!user_wnd(hwnd) || nMaxCount <= 0) return 0;
  return (int16_t)snd(hwnd, WM_GETTEXT, (uint16_t)nMaxCount, lpString);
}
void SetWindowText(HWND16 hwnd, FARPTR lpString) { snd(hwnd, WM_SETTEXT, 0, lpString); }
int16_t GetWindowTextLength(HWND16 hwnd) { return (int16_t)snd(hwnd, WM_GETTEXTLENGTH, 0, 0); }

static uint32_t extra_get(const UWnd *w, int idx, int bytes) {
  uint32_t v = 0;
  if (idx < 0 || !w->extra_off || idx + bytes > w->nextra) return 0;
  memcpy(&v, mem_arena() + w->extra_off + idx, (size_t)bytes);
  return v;
}
static void extra_set(UWnd *w, int idx, int bytes, uint32_t v) {
  if (idx < 0 || !w->extra_off || idx + bytes > w->nextra) return;
  memcpy(mem_arena() + w->extra_off + idx, &v, (size_t)bytes);
}

uint32_t GetWindowLong(HWND16 hwnd, int16_t idx) {
  UWnd *w = user_wnd(hwnd);
  if (!w) return 0;
  if (idx >= 0) return extra_get(w, idx, 4);
  switch (idx) {
    case GWL_WNDPROC: return w->wndproc;
    case GWL_STYLE: return w->style;
    case GWL_EXSTYLE: return w->exstyle;
    case GWL_ID: return w->id;
    case GWL_HINSTANCE: return w->hinst;
    case GWL_HWNDPARENT: return GetParent(hwnd);
    default: return 0;
  }
}

uint32_t SetWindowLong(HWND16 hwnd, int16_t idx, uint32_t v) {
  UWnd *w = user_wnd(hwnd);
  uint32_t old;
  if (!w) return 0;
  if (idx >= 0) {
    old = extra_get(w, idx, 4);
    extra_set(w, idx, 4, v);
    return old;
  }
  switch (idx) {
    case GWL_WNDPROC: old = w->wndproc; w->wndproc = v; return old;
    case GWL_STYLE: old = w->style; w->style = v; return old;
    case GWL_EXSTYLE: old = w->exstyle; w->exstyle = v; return old;
    case GWL_ID: old = w->id; w->id = (uint16_t)v; return old;
    case GWL_HINSTANCE: old = w->hinst; w->hinst = (uint16_t)v; return old;
    default: return 0;
  }
}

uint16_t GetWindowWord(HWND16 hwnd, int16_t idx) {
  UWnd *w = user_wnd(hwnd);
  if (!w) return 0;
  if (idx >= 0) return (uint16_t)extra_get(w, idx, 2);
  switch (idx) {
    case GWL_ID: return w->id;
    case GWL_HINSTANCE: return w->hinst;
    case GWL_HWNDPARENT: return GetParent(hwnd);
    default: return 0;
  }
}

uint16_t SetWindowWord(HWND16 hwnd, int16_t idx, uint16_t v) {
  UWnd *w = user_wnd(hwnd);
  uint16_t old;
  if (!w) return 0;
  if (idx >= 0) {
    old = (uint16_t)extra_get(w, idx, 2);
    extra_set(w, idx, 2, v);
    return old;
  }
  switch (idx) {
    case GWL_ID: old = w->id; w->id = v; return old;
    case GWL_HINSTANCE: old = w->hinst; w->hinst = v; return old;
    default: return 0;
  }
}

uint16_t GetWindowPlacement(HWND16 hwnd, FARPTR lpwndpl) {
  UWnd *w = user_wnd(hwnd);
  WINDOWPLACEMENT16 *p;
  if (!w || !lpwndpl) return 0;
  p = LIN(lpwndpl);
  p->flags = 0;
  p->showCmd = (w->style & WS_MINIMIZE) ? SW_SHOWMINIMIZED : (w->style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
  p->ptMinPosition = (POINT16){-1, -1};
  p->ptMaxPosition = (POINT16){-1, -1};
  p->rcNormalPosition = (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) ? w->nrect : w->wrect;
  return 1;
}

/* ---- DefWindowProc (the window part; painting is in user_paint.c) ---- */

uint32_t user_def_window_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UWnd *w = user_wnd(h);
  if (!w) return 0;
  switch (msg) {
    case WM_NCCREATE: {
      /* defwnd.c: the window text comes from CREATESTRUCT.lpszName */
      const CREATESTRUCT16 *cs = LIN(lp);
      far_strcpy(w->text, sizeof w->text, cs->lpszName);
      return 1;
    }
    case WM_WINDOWPOSCHANGED: {
      /* W-defpos: defwnd.c handle_window_pos_changed 504-525 */
      const WINDOWPOS16 *p = LIN(lp);
      uint16_t flags = p->flags;
      RECT16 c = w->crect;
      if (!(flags & SWP_NOCLIENTMOVE)) snd(h, WM_MOVE, 0, make_long((uint16_t)c.left, (uint16_t)c.top));
      if (user_wnd(h) && (!(flags & SWP_NOCLIENTSIZE) || (flags & SWP_STATECHANGED))) {
        w = user_wnd(h);
        c = w->crect;
        if (w->style & WS_MINIMIZE) snd(h, WM_SIZE, SIZE_MINIMIZED, 0);
        else
          snd(h, WM_SIZE, (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED,
              make_long((uint16_t)(c.right - c.left), (uint16_t)(c.bottom - c.top)));
      }
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_SETTEXT:
      far_strcpy(w->text, sizeof w->text, lp);
      if (w->style & WS_VISIBLE) user_nc_paint_caption(h);
      return 1;
    case WM_GETTEXT: {
      size_t n = strlen(w->text);
      char *dst;
      if (wp == 0 || !lp) return 0;
      if (n > (size_t)wp - 1) n = (size_t)wp - 1;
      dst = LIN(lp);
      memcpy(dst, w->text, n);
      dst[n] = 0;
      return (uint32_t)n;
    }
    case WM_GETTEXTLENGTH: return (uint32_t)strlen(w->text);
    case WM_SETCURSOR: return user_def_setcursor(h, wp, lp); /* USER part 2: user_misc.c */
    case WM_CANCELMODE: { /* releases the capture of this window (USER part 2) */
      UserState *st = user_state();
      if (st->in.capture == h) st->in.capture = 0;
      return 0;
    }
    case WM_NCCALCSIZE:
    case WM_NCHITTEST:
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
    case WM_SYSCOMMAND:
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
      return user_nc_def_message(h, msg, wp, lp); /* Windows 3.1 look: user_nc.c; keys: menu_ui.c */
    case WM_CTLCOLOR: return user_def_ctlcolor(wp, (uint16_t)(lp >> 16)); /* a system brush (inventory.md section 13, rule 15) */
    case WM_ERASEBKGND: return user_def_erase(h, wp);
    case WM_PAINT: return user_def_paint(h);
    default: return 0;
  }
}

uint32_t DefWindowProc(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  return user_def_window_proc(hwnd, msg, wParam, lParam);
}
