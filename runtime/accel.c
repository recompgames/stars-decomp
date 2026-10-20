/* Accelerators (task stars-0z4.13): LoadAccelerators and TranslateAccelerator.
 *
 * Resource format (Windows 3.x, the one Wine's ACCEL16 describes: include/wine/winuser16.h 168-174):
 * entries of fFlags BYTE, key WORD, cmd WORD, five bytes each; the entry with 0x80 in fFlags is the
 * last of the table. fFlags holds FVIRTKEY (1), FNOINVERT (2), FSHIFT (4), FCONTROL (8), FALT (0x10)
 * (include/winuser.h 735-741). The parser never reads past the resource, whatever the flags say.
 *
 * TranslateAccelerator follows Wine's translate_accelerator (dlls/win32u/menu.c 1713-1872) with the
 * Win16 packing of the command: WM_COMMAND with wParam = the command and lParam = MAKELONG(0, 1),
 * WM_SYSCOMMAND with lParam = 0x00010000. The result is 1 whenever an entry matched, even when the
 * command is not sent (disabled window, grayed menu item, iconic window, mouse captured). */
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/menu_internal.h"

HACCEL16 LoadAccelerators(HANDLE16 hInstance, FARPTR lpTableName) {
  UMenuState *st = user_menu_state();
  HANDLE16 hr = FindResource(hInstance, lpTableName, MK_FP(0, RT_ACCELERATOR)), hg;
  uint32_t size, key;
  FARPTR p;
  int slot = -1;
  if (!st || !hr) return 0;
  key = ((uint32_t)hInstance << 16) | hr;
  for (unsigned i = 0; i < AT_MAX; i++)
    if (st->at[i].used && st->at[i].key == key) return (HACCEL16)(UH_ACCEL_BASE + i);
  for (unsigned i = 0; i < AT_MAX; i++)
    if (!st->at[i].used) {
      slot = (int)i;
      break;
    }
  if (slot < 0) return 0;
  size = SizeofResource(hInstance, hr);
  hg = LoadResource(hInstance, hr);
  if (!hg) return 0;
  p = LockResource(hg);
  if (p && size && mem_check(p, size)) {
    const uint8_t *b = LIN(p);
    UAccelTab *t = &st->at[slot];
    unsigned n = 0;
    memset(t, 0, sizeof *t);
    /* entries until the one flagged last; a table without that flag ends with the resource */
    for (uint32_t off = 0; off + 5 <= size && n < AE_MAX; off += 5) {
      uint8_t fl = b[off];
      t->e[n].fVirt = (uint8_t)(fl & ~ACCEL_LAST);
      t->e[n].key = (uint16_t)(b[off + 1] | (b[off + 2] << 8));
      t->e[n].cmd = (uint16_t)(b[off + 3] | (b[off + 4] << 8));
      n++;
      if (fl & ACCEL_LAST) break;
    }
    t->count = (uint16_t)n;
    t->key = key;
    t->used = 1;
  }
  GlobalUnlock(hg);
  FreeResource(hg);
  return st->at[slot].used ? (HACCEL16)(UH_ACCEL_BASE + (unsigned)slot) : 0;
}

unsigned user_accel_entries(HACCEL16 hAccel, UAccelEntry *out, unsigned max) {
  UMenuState *st = user_menu_state();
  const UAccelTab *t;
  if (!st || hAccel < UH_ACCEL_BASE || hAccel >= UH_ACCEL_BASE + AT_MAX) return 0;
  t = &st->at[hAccel - UH_ACCEL_BASE];
  if (!t->used) return 0;
  for (unsigned i = 0; i < t->count && i < max; i++) out[i] = t->e[i];
  return t->count;
}

/* the menu that holds the item with command cmd, and where that menu sits */
static HMENU16 container_of(HMENU16 top, uint16_t cmd) {
  HMENU16 where = 0;
  unsigned pos = 0;
  if (!top || !user_menu_valid(top)) return 0;
  return user_menu_find(top, cmd, MF_BYCOMMAND, &pos, &where) ? where : 0;
}

/* position of pop-up `sub` in its parent, searching the tree under top */
static int parent_pos(HMENU16 top, HMENU16 sub, int depth) {
  UMenuObj *o = user_menu_obj(top);
  if (!o || depth > 16) return -1;
  for (unsigned i = 0; i < o->count; i++) {
    const UMenuItem *it = user_menu_item(top, i);
    if (!(it->flags & MF_POPUP)) continue;
    if (it->sub == sub) return (int)i;
    {
      int r = parent_pos(it->sub, sub, depth + 1);
      if (r >= 0) return r;
    }
  }
  return -1;
}

static bool match_entry(uint16_t msg, uint16_t wp, uint32_t lp, const UAccelEntry *e) {
  unsigned mask = 0;
  if (wp != e->key) return false;
  if (GetKeyState(VK_CONTROL) < 0) mask |= FCONTROL;
  if (GetKeyState(VK_MENU) < 0) mask |= FALT;
  if (GetKeyState(VK_SHIFT) < 0) mask |= FSHIFT;
  if (msg == WM_CHAR || msg == WM_SYSCHAR) return !(e->fVirt & FVIRTKEY) && (mask & FALT) == (e->fVirt & FALT);
  if (e->fVirt & FVIRTKEY) return mask == (e->fVirt & (FSHIFT | FCONTROL | FALT));
  /* not virtual: Alt+character from the key message (not an extended key, Alt down: lParam bit 29) */
  return !(lp & 0x01000000UL) && (e->fVirt & FALT) && (lp & 0x20000000UL);
}

static void run_entry(HWND16 hwnd, const UAccelEntry *e) {
  HMENU16 menu = (user_wnd(hwnd) && !(user_wnd(hwnd)->style & WS_CHILD)) ? GetMenu(hwnd) : 0;
  HMENU16 sys = GetSystemMenu(hwnd, 0); /* the pop-up of the control menu, 0 without WS_SYSMENU */
  HMENU16 sys_outer = sys ? user_menu_sys_outer(hwnd) : 0;
  uint16_t cmd = e->cmd;
  unsigned why = 0; /* Wine's "reasons for NOT sending" */
  uint16_t st_sys = 0xFFFF, st_menu = 0xFFFF;
  uint16_t msg = 0;
  HMENU16 c;
  if ((c = container_of(sys_outer, cmd)) != 0) {
    if (GetCapture()) why = 2;
    if (!IsWindowEnabled(hwnd)) {
      why = 3;
    } else {
      SendMessage(hwnd, WM_INITMENU, sys_outer, 0);
      if (c != sys_outer) {
        int pos = parent_pos(sys_outer, c, 0);
        SendMessage(hwnd, WM_INITMENUPOPUP, c, make_long((uint16_t)(pos < 0 ? 0 : pos), 1));
      }
      st_sys = GetMenuState(sys, cmd, MF_BYCOMMAND);
    }
  } else if ((c = container_of(menu, cmd)) != 0) {
    if (GetCapture()) why = 2;
    if (!IsWindowEnabled(hwnd)) {
      why = 3;
    } else {
      SendMessage(hwnd, WM_INITMENU, menu, 0);
      if (c != menu) {
        int pos = parent_pos(menu, c, 0);
        SendMessage(hwnd, WM_INITMENUPOPUP, c, make_long((uint16_t)(pos < 0 ? 0 : pos), 0));
      }
      st_menu = GetMenuState(menu, cmd, MF_BYCOMMAND);
    }
  }
  if (why == 0) {
    if (st_sys != 0xFFFF) {
      msg = (st_sys & (MF_DISABLED | MF_GRAYED)) ? 0 : WM_SYSCOMMAND;
    } else if (st_menu != 0xFFFF) {
      if (IsIconic(hwnd)) msg = 0;
      else msg = (st_menu & (MF_DISABLED | MF_GRAYED)) ? 0 : WM_COMMAND;
    } else {
      msg = WM_COMMAND;
    }
  }
  if (msg == WM_COMMAND) SendMessage(hwnd, WM_COMMAND, cmd, make_long(0, 1));
  else if (msg == WM_SYSCOMMAND) SendMessage(hwnd, WM_SYSCOMMAND, cmd, 0x00010000UL);
}

int16_t TranslateAccelerator(HWND16 hwnd, HACCEL16 hAccel, FARPTR lpMsg) {
  UMenuState *st = user_menu_state();
  const MSG16 *m;
  UAccelTab t;
  if (!hwnd || !st || !lpMsg || !mem_check(lpMsg, sizeof(MSG16))) return 0;
  m = LIN(lpMsg);
  if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN && m->message != WM_CHAR && m->message != WM_SYSCHAR) return 0;
  if (hAccel < UH_ACCEL_BASE || hAccel >= UH_ACCEL_BASE + AT_MAX || !st->at[hAccel - UH_ACCEL_BASE].used) return 0;
  /* a private copy: the commands run window procedures, which may load tables */
  t = st->at[hAccel - UH_ACCEL_BASE];
  for (unsigned i = 0; i < t.count; i++) {
    if (!user_wnd(hwnd)) return 0;
    if (match_entry(m->message, m->wParam, m->lParam, &t.e[i])) {
      run_entry(hwnd, &t.e[i]);
      return 1;
    }
  }
  return 0;
}
