/* Menus (task stars-0z4.13), part 1: menu objects, the Win16 menu API, the RT_MENU resource and
 * the window integration (the menu bar of a top-level window, the control menu). Drawing and
 * tracking are in menu_ui.c, accelerators in accel.c. Plain C, no harness dependency.
 *
 * The resource format is the Windows 3.x menu template read by Wine's parse_menu_resource
 * (dlls/user.exe16/user.c 1445-1488): a header (version WORD = 0, offset WORD), then items, each a
 * flags WORD, an id WORD unless MF_POPUP, and a zero-terminated string; MF_END (0x80) marks the
 * last item of a level (it is the same bit as MF_HILITE, so it is removed on loading); a pop-up
 * item is followed by its own items. Separators are items with flags 0, id 0 and an empty string.
 * Rules without a source are marked UNVERIFIED and listed in docs/inventory.md section 18. */
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/menu_internal.h"

static uint32_t g_state_off;

UMenuState *user_menu_state(void) { return g_state_off ? (UMenuState *)(mem_arena() + g_state_off) : NULL; }

void user_menu_init(void) {
  UMenuState *st;
  if (g_state_off) return;
  st = rt_alloc(sizeof *st);
  if (!st) return;
  memset(st, 0, sizeof *st);
  g_state_off = mem_off(st);
  user_menu_ui_init();
}

void user_menu_shutdown(void) {
  UMenuState *st = user_menu_state();
  if (!st) return;
  user_menu_ui_shutdown();
  rt_free(st);
  g_state_off = 0;
}

int user_menu_bar_height(void) { return GetSystemMetrics(15); }

/* ---- objects ---- */

bool user_menu_valid(HMENU16 h) {
  UMenuState *st = user_menu_state();
  return st && h >= UH_MENU_BASE && h < UH_MENU_BASE + MN_MAX && st->m[h - UH_MENU_BASE].used;
}

UMenuObj *user_menu_obj(HMENU16 h) { return user_menu_valid(h) ? &user_menu_state()->m[h - UH_MENU_BASE] : NULL; }

HMENU16 user_menu_new(bool popup, bool sys) {
  UMenuState *st = user_menu_state();
  (void)popup;
  if (!st) return 0;
  for (unsigned i = 0; i < MN_MAX; i++)
    if (!st->m[i].used) {
      memset(&st->m[i], 0, sizeof st->m[i]);
      st->m[i].used = 1;
      st->m[i].sys = sys ? 1 : 0;
      return (HMENU16)(UH_MENU_BASE + i);
    }
  return 0;
}

HMENU16 CreateMenu(void) { return user_menu_new(false, false); }
HMENU16 CreatePopupMenu(void) { return user_menu_new(true, false); }

/* slot index (0-based) of item pos, or -1 */
static int slot_at(const UMenuObj *o, unsigned pos) {
  UMenuState *st = user_menu_state();
  int s = (int)o->first - 1;
  while (s >= 0 && pos--) s = (int)st->it[s].next - 1;
  return s;
}

UMenuItem *user_menu_item(HMENU16 h, unsigned pos) {
  UMenuObj *o = user_menu_obj(h);
  int s;
  if (!o || pos >= o->count) return NULL;
  s = slot_at(o, pos);
  return s < 0 ? NULL : &user_menu_state()->it[s].it;
}

bool user_menu_item_info(HMENU16 h, unsigned pos, UMenuItemInfo *out) {
  const UMenuItem *it = user_menu_item(h, pos);
  if (!it) return false;
  out->flags = it->flags;
  out->id = it->id;
  out->sub = (it->flags & MF_POPUP) ? it->sub : 0;
  strcpy(out->text, it->text);
  return true;
}

#define ITEM_STORED (MF_GRAYED | MF_DISABLED | MF_BITMAP | MF_CHECKED | MF_POPUP | MF_MENUBARBREAK | MF_MENUBREAK | MF_SEPARATOR | MF_HELP)

bool user_menu_insert(HMENU16 h, unsigned pos, uint16_t flags, uint16_t id, const char *text) {
  UMenuState *st = user_menu_state();
  UMenuObj *o = user_menu_obj(h);
  int ns = -1;
  UItemSlot *n;
  if (!st || !o) return false;
  if ((flags & MF_POPUP) && !user_menu_valid(id)) return false;
  for (unsigned i = 0; i < MI_MAX; i++)
    if (!st->it[i].used) {
      ns = (int)i;
      break;
    }
  if (ns < 0) return false;
  n = &st->it[ns];
  memset(n, 0, sizeof *n);
  n->used = 1;
  n->it.flags = (uint16_t)(flags & ITEM_STORED);
  n->it.id = id;
  if (flags & MF_POPUP) n->it.sub = id;
  if (text && !(flags & (MF_SEPARATOR | MF_BITMAP))) {
    strncpy(n->it.text, text, sizeof n->it.text - 1);
    n->it.text[sizeof n->it.text - 1] = 0;
  }
  /* a string item with no text, no id and no sub-menu is a separator (Wine's resource loader passes NULL text for it) */
  if (!(n->it.flags & (MF_POPUP | MF_SEPARATOR | MF_BITMAP)) && !n->it.text[0] && id == 0) n->it.flags |= MF_SEPARATOR;
  if (pos > o->count) pos = o->count;
  if (pos == 0) {
    n->next = o->first;
    o->first = (uint16_t)(ns + 1);
  } else {
    int prev = slot_at(o, pos - 1);
    n->next = st->it[prev].next;
    st->it[prev].next = (uint16_t)(ns + 1);
  }
  o->count++;
  return true;
}

/* Unlinks item pos; returns its slot (still marked used) or -1. */
static int unlink_item(UMenuObj *o, unsigned pos) {
  UMenuState *st = user_menu_state();
  int s;
  if (pos >= o->count) return -1;
  if (pos == 0) {
    s = (int)o->first - 1;
    o->first = st->it[s].next;
  } else {
    int prev = slot_at(o, pos - 1);
    s = (int)st->it[prev].next - 1;
    st->it[prev].next = st->it[s].next;
  }
  o->count--;
  return s;
}

static void destroy_rec(HMENU16 h, int depth) {
  UMenuState *st = user_menu_state();
  UMenuObj *o = user_menu_obj(h);
  int s;
  if (!o || depth > 16) return;
  s = (int)o->first - 1;
  o->used = 0; /* first, so that a cycle ends */
  while (s >= 0) {
    UItemSlot *it = &st->it[s];
    int nx = (int)it->next - 1;
    if (it->it.flags & MF_POPUP) destroy_rec(it->it.sub, depth + 1);
    it->used = 0;
    s = nx;
  }
  o->count = 0;
  o->first = 0;
}

uint16_t DestroyMenu(HMENU16 hMenu) {
  if (!user_menu_valid(hMenu)) return 0;
  destroy_rec(hMenu, 0);
  return 1;
}

/* ---- searching ---- */

static UMenuItem *find_rec(HMENU16 h, uint16_t id, unsigned *pos, HMENU16 *where, int depth) {
  UMenuObj *o = user_menu_obj(h);
  if (!o || depth > 16) return NULL;
  for (unsigned i = 0; i < o->count; i++) {
    UMenuItem *it = user_menu_item(h, i);
    if (!(it->flags & MF_SEPARATOR) && it->id == id) {
      if (pos) *pos = i;
      if (where) *where = h;
      return it;
    }
    if (it->flags & MF_POPUP) {
      UMenuItem *r = find_rec(it->sub, id, pos, where, depth + 1);
      if (r) return r;
    }
  }
  return NULL;
}

UMenuItem *user_menu_find(HMENU16 h, uint16_t id, uint16_t flags, unsigned *pos, HMENU16 *where) {
  if (flags & MF_BYPOSITION) {
    UMenuItem *it = user_menu_item(h, id);
    if (!it) return NULL;
    if (pos) *pos = id;
    if (where) *where = h;
    return it;
  }
  return find_rec(h, id, pos, where, 0);
}

/* ---- API ---- */

static const char *far_text(uint16_t flags, FARPTR item, char *buf, size_t cap) {
  size_t n = 0;
  if ((flags & (MF_SEPARATOR | MF_BITMAP)) || !item) return NULL;
  while (n + 1 < cap && mem_check(item + (uint32_t)n, 1)) {
    char c = *(const char *)LIN(item + (uint32_t)n);
    if (!c) break;
    buf[n++] = c;
  }
  buf[n] = 0;
  return buf;
}

uint16_t InsertMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags, uint16_t uIDNewItem, FARPTR lpNewItem) {
  char buf[64];
  const char *text = far_text(uFlags, lpNewItem, buf, sizeof buf);
  HMENU16 where = hMenu;
  unsigned pos;
  if (!user_menu_valid(hMenu)) return 0;
  if (uFlags & MF_BYPOSITION) {
    pos = uPosition;
  } else {
    /* by command: before that item, in the menu that holds it (UNVERIFIED when it does not exist: the call fails) */
    if (!user_menu_find(hMenu, uPosition, MF_BYCOMMAND, &pos, &where)) return 0;
  }
  return user_menu_insert(where, pos, uFlags, uIDNewItem, text) ? 1 : 0;
}

uint16_t AppendMenu(HMENU16 hMenu, uint16_t uFlags, uint16_t uIDNewItem, FARPTR lpNewItem) {
  char buf[64];
  const char *text = far_text(uFlags, lpNewItem, buf, sizeof buf);
  if (!user_menu_valid(hMenu)) return 0;
  return user_menu_insert(hMenu, 0xFFFF, uFlags, uIDNewItem, text) ? 1 : 0;
}

static uint16_t delete_item(HMENU16 hMenu, uint16_t pos, uint16_t flags, bool destroy_sub) {
  UMenuItem *it;
  HMENU16 where = hMenu;
  unsigned p = 0;
  UMenuObj *o;
  int s;
  if (!user_menu_valid(hMenu)) return 0;
  it = user_menu_find(hMenu, pos, flags, &p, &where);
  if (!it) return 0;
  o = user_menu_obj(where);
  if ((it->flags & MF_POPUP) && destroy_sub) destroy_rec(it->sub, 0);
  s = unlink_item(o, p);
  if (s >= 0) user_menu_state()->it[s].used = 0;
  return 1;
}

uint16_t DeleteMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags) { return delete_item(hMenu, uPosition, uFlags, true); }
uint16_t RemoveMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags) { return delete_item(hMenu, uPosition, uFlags, false); }

HMENU16 GetSubMenu(HMENU16 hMenu, int16_t nPos) {
  const UMenuItem *it;
  if (nPos < 0) return 0;
  it = user_menu_item(hMenu, (unsigned)nPos);
  return (it && (it->flags & MF_POPUP)) ? it->sub : 0;
}

int16_t GetMenuItemCount(HMENU16 hMenu) {
  UMenuObj *o = user_menu_obj(hMenu);
  return o ? (int16_t)o->count : -1;
}

/* Wine NtUserCheckMenuItem / NtUserEnableMenuItem: the previous state bits, 0xFFFF when there is no such item. */
uint16_t CheckMenuItem(HMENU16 hMenu, uint16_t uIDCheckItem, uint16_t uCheck) {
  UMenuItem *it = user_menu_valid(hMenu) ? user_menu_find(hMenu, uIDCheckItem, uCheck, NULL, NULL) : NULL;
  uint16_t old;
  if (!it) return 0xFFFF;
  old = (uint16_t)(it->flags & MF_CHECKED);
  it->flags = (uint16_t)((it->flags & ~MF_CHECKED) | (uCheck & MF_CHECKED));
  return old;
}

uint16_t EnableMenuItem(HMENU16 hMenu, uint16_t uIDEnableItem, uint16_t uEnable) {
  UMenuItem *it = user_menu_valid(hMenu) ? user_menu_find(hMenu, uIDEnableItem, uEnable, NULL, NULL) : NULL;
  uint16_t old;
  if (!it) return 0xFFFF;
  old = (uint16_t)(it->flags & (MF_GRAYED | MF_DISABLED));
  it->flags = (uint16_t)((it->flags & ~(MF_GRAYED | MF_DISABLED)) | (uEnable & (MF_GRAYED | MF_DISABLED)));
  return old;
}

void user_menu_enable_item(HMENU16 h, uint16_t id, bool enabled) { EnableMenuItem(h, id, (uint16_t)(MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED))); }

uint16_t GetMenuState(HMENU16 hMenu, uint16_t uId, uint16_t uFlags) {
  UMenuItem *it = user_menu_valid(hMenu) ? user_menu_find(hMenu, uId, uFlags, NULL, NULL) : NULL;
  if (!it) return 0xFFFF;
  if (it->flags & MF_POPUP) {
    UMenuObj *sub = user_menu_obj(it->sub);
    return (uint16_t)(((sub ? sub->count : 0) << 8) | (it->flags & 0xFF));
  }
  return it->flags;
}

/* ---- resources ---- */

static const uint8_t *parse_items(const uint8_t *p, const uint8_t *end, HMENU16 menu, int depth) {
  bool last;
  if (depth > 8) return NULL;
  do {
    uint16_t flags, id = 0;
    char text[64];
    size_t n = 0;
    if (end - p < 2) return NULL;
    flags = (uint16_t)(p[0] | (p[1] << 8));
    p += 2;
    last = (flags & MF_END) != 0;
    flags &= (uint16_t)~MF_END; /* the same bit as MF_HILITE (user.c 1465-1467) */
    if (!(flags & MF_POPUP)) {
      if (end - p < 2) return NULL;
      id = (uint16_t)(p[0] | (p[1] << 8));
      p += 2;
    }
    while (p < end && *p) {
      if (n + 1 < sizeof text) text[n++] = (char)*p;
      p++;
    }
    if (p >= end) return NULL;
    p++;
    text[n] = 0;
    if (flags & MF_POPUP) {
      HMENU16 sub = user_menu_new(true, false);
      if (!sub) return NULL;
      if (!user_menu_insert(menu, 0xFFFF, flags, sub, text)) {
        DestroyMenu(sub);
        return NULL;
      }
      p = parse_items(p, end, sub, depth + 1);
      if (!p) return NULL;
    } else if (!user_menu_insert(menu, 0xFFFF, flags, id, text)) {
      return NULL;
    }
  } while (!last);
  return p;
}

HMENU16 LoadMenu(HANDLE16 hInstance, FARPTR lpMenuName) {
  HANDLE16 hr = FindResource(hInstance, lpMenuName, MK_FP(0, RT_MENU)), hg;
  uint32_t size;
  FARPTR p;
  HMENU16 m = 0;
  if (!hr) return 0;
  size = SizeofResource(hInstance, hr);
  hg = LoadResource(hInstance, hr);
  if (!hg) return 0;
  p = LockResource(hg);
  if (p && size >= 4 && mem_check(p, size)) {
    const uint8_t *b = LIN(p), *e = b + size;
    uint16_t version = (uint16_t)(b[0] | (b[1] << 8)), offset = (uint16_t)(b[2] | (b[3] << 8));
    if (version == 0 && (size_t)4 + offset < size) {
      m = user_menu_new(false, false);
      if (m && !parse_items(b + 4 + offset, e, m, 0)) {
        DestroyMenu(m);
        m = 0;
      }
    }
  }
  GlobalUnlock(hg);
  FreeResource(hg);
  return m;
}

/* ---- windows ---- */

HMENU16 GetMenu(HWND16 hwnd) {
  const UWnd *w = user_wnd(hwnd);
  if (!w || hwnd == user_desktop() || (w->style & WS_CHILD)) return 0;
  return w->hmenu;
}

uint16_t SetMenu(HWND16 hwnd, HMENU16 hMenu) {
  UWnd *w = user_wnd(hwnd);
  bool had;
  if (!w || hwnd == user_desktop() || (w->style & WS_CHILD)) return 0;
  if (hMenu && !user_menu_valid(hMenu)) return 0;
  had = w->hmenu != 0;
  w->hmenu = hMenu;
  if (had != (hMenu != 0)) {
    /* the client area changes: WM_NCCALCSIZE and a new non-client paint (Wine SetMenu -> SetWindowPos FRAMECHANGED) */
    SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  } else {
    DrawMenuBar(hwnd);
  }
  return 1;
}

/* A menu for a window being created: hMenuArg when it is a menu, else the class menu. Child windows have none. */
uint16_t user_menu_for_new_window(HWND16 hwnd, uint32_t style, HANDLE16 hMenuArg, HANDLE16 hinst, uint32_t class_menu_name) {
  const UWnd *w = user_wnd(hwnd);
  (void)w;
  if (style & WS_CHILD) return 0;
  if (hMenuArg && user_menu_valid(hMenuArg)) return hMenuArg;
  if (class_menu_name) return LoadMenu(hinst, class_menu_name);
  return 0;
}

void user_menu_window_gone(HWND16 hwnd, uint16_t hmenu, uint16_t hsys) {
  (void)hwnd;
  if (hmenu) DestroyMenu(hmenu);
  if (hsys) DestroyMenu(hsys);
}

/* ---- the control menu ---- */

/* Refreshes the enabled state of the standard items from the window style (user_sysmenu_items). */
static void sys_refresh(HWND16 hwnd, HMENU16 pop) {
  USysItem items[10];
  unsigned n = user_sysmenu_items(hwnd, items);
  for (unsigned i = 0; i < n; i++)
    if (items[i].id) user_menu_enable_item(pop, items[i].id, items[i].enabled != 0);
}

HMENU16 user_menu_sys_popup(HWND16 hwnd) {
  UWnd *w = user_wnd(hwnd);
  HMENU16 outer, pop;
  USysItem items[10];
  unsigned n;
  if (!w || hwnd == user_desktop() || !(w->style & WS_SYSMENU)) return 0;
  if (!w->hsys || !user_menu_valid(w->hsys)) {
    outer = user_menu_new(false, true);
    pop = user_menu_new(true, true);
    if (!outer || !pop) {
      if (outer) DestroyMenu(outer);
      if (pop) DestroyMenu(pop);
      return 0;
    }
    n = user_sysmenu_items(hwnd, items);
    for (unsigned i = 0; i < n; i++)
      user_menu_insert(pop, 0xFFFF, items[i].id ? 0 : MF_SEPARATOR, items[i].id, items[i].id ? items[i].text : NULL);
    user_menu_insert(outer, 0xFFFF, MF_POPUP, pop, "");
    w = user_wnd(hwnd);
    w->hsys = outer;
  }
  pop = GetSubMenu(w->hsys, 0);
  if (pop) sys_refresh(hwnd, pop);
  return pop;
}

HMENU16 GetSystemMenu(HWND16 hwnd, uint16_t bRevert) {
  UWnd *w = user_wnd(hwnd);
  if (!w || hwnd == user_desktop() || !(w->style & WS_SYSMENU)) return 0;
  if (bRevert) {
    if (w->hsys) DestroyMenu(w->hsys);
    w->hsys = 0;
    return 0;
  }
  return user_menu_sys_popup(hwnd);
}

HMENU16 user_menu_sys_outer(HWND16 hwnd) {
  const UWnd *w;
  if (!user_menu_sys_popup(hwnd)) return 0;
  w = user_wnd(hwnd);
  return w ? w->hsys : 0;
}
