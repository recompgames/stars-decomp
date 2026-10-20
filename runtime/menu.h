/* Menus and accelerators (task stars-0z4.13): menu objects, the STARSMENU resource, the menu
 * bar in the non-client area, pop-up menus, TrackPopupMenu, the system (control) menu pop-up,
 * mouse and keyboard tracking, LoadAccelerators and TranslateAccelerator. Plain C functions
 * named and typed like the Win16 API; no harness dependency (the PASCAL stack adapters are in
 * harness/user_thunks.c).
 *
 * Files: menu.c (menu objects, API, resources, window integration), menu_ui.c (drawing, layout,
 * tracking, pop-up windows, system menu), accel.c (accelerator tables).
 *
 * Model.
 *  - Menus and accelerator tables are objects in one arena block (rt_alloc), so a snapshot of the
 *    arena covers them. A menu handle is 0x1000 + slot, an accelerator handle 0x1100 + slot.
 *  - A menu is an ordered list of items {flags, id, sub-menu handle, text}. The flags are the
 *    Win16 MF_* ones (MF_GRAYED, MF_DISABLED, MF_CHECKED, MF_POPUP, MF_MENUBARBREAK, MF_MENUBREAK,
 *    MF_SEPARATOR; MF_HILITE is the tracking highlight). A string item with an empty text is a
 *    separator (Wine dlls/user.exe16/user.c parse_menu_resource 1440-1488, AppendMenuA with NULL).
 *  - Pop-up menus are real windows of the runtime class "#32768" (USER_CB_MENUPOPUP in the callback
 *    segment). They are created for the time of the tracking only.
 *  - The tracker (one at a time) keeps its state in the arena block, so it can be inspected by
 *    tests through user_menu_track_info.
 *
 * References. Win16 message numbers and MF_/TPM_/F* values: Wine at commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0, include/winuser.rh lines 462 (WM_COMMAND), 471-478
 * (WM_INITMENU .. WM_ENTERIDLE), 916-944 (MF_*), include/winuser.h 1637-1655 (TPM_*) and 735-741
 * (FVIRTKEY, FNOINVERT, FSHIFT, FCONTROL, FALT), include/wine/winuser16.h 168-174 (ACCEL16).
 * Behaviour: dlls/win32u/menu.c (translate_accelerator 1713-1872, select_item 3179-3240,
 * show_sub_popup 3370-3420, exec_focused_item 3472-3512, init_tracking 4378-4415,
 * track_mouse_menu_bar 4428-4445, track_keyboard_menu_bar 4447-4503, NtUserTrackPopupMenuEx
 * 4505-4570), dlls/win32u/defwnd.c (SC_MOUSEMENU/SC_KEYMENU 1016-1022, WM_SYSKEYDOWN / WM_SYSKEYUP /
 * WM_SYSCHAR 2756-2815), dlls/user.exe16/message.c (Win16 packing of WM_MENUSELECT, WM_MENUCHAR,
 * WM_COMMAND: 809-846 and 1183-1224). Every rule without a source there is marked UNVERIFIED at
 * the code that implements it and in docs/inventory.md section 18. */
#ifndef STARS_RUNTIME_MENU_H
#define STARS_RUNTIME_MENU_H

#include "runtime/user.h"

typedef uint16_t HMENU16;
typedef uint16_t HACCEL16;

/* Messages (Win16 numbering, winuser.rh 462, 471-478). Win16 packing:
 *   WM_INITMENU       wParam = menu, lParam = 0
 *   WM_INITMENUPOPUP  wParam = pop-up, LOWORD(lParam) = index in the parent, HIWORD(lParam) = system menu flag
 *   WM_MENUSELECT     wParam = item id, or the pop-up handle for a pop-up item; LOWORD(lParam) = flags
 *                     (0xFFFF when the menu closed), HIWORD(lParam) = menu handle (0 when closed)
 *   WM_MENUCHAR       wParam = character, LOWORD(lParam) = MF_POPUP / MF_SYSMENU, HIWORD(lParam) = menu
 *   WM_COMMAND        wParam = id, lParam = 0 for a menu, MAKELONG(0, 1) for an accelerator */
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011F
#define WM_MENUCHAR 0x0120
#define WM_ENTERIDLE 0x0121

/* MF_* (winuser.rh 916-944) */
#define MF_INSERT 0x0000
#define MF_CHANGE 0x0080
#define MF_APPEND 0x0100
#define MF_DELETE 0x0200
#define MF_REMOVE 0x1000
#define MF_END 0x0080
#define MF_ENABLED 0x0000
#define MF_GRAYED 0x0001
#define MF_DISABLED 0x0002
#define MF_STRING 0x0000
#define MF_BITMAP 0x0004
#define MF_UNCHECKED 0x0000
#define MF_CHECKED 0x0008
#define MF_POPUP 0x0010
#define MF_MENUBARBREAK 0x0020
#define MF_MENUBREAK 0x0040
#define MF_UNHILITE 0x0000
#define MF_HILITE 0x0080
#define MF_OWNERDRAW 0x0100
#define MF_BYCOMMAND 0x0000
#define MF_BYPOSITION 0x0400
#define MF_SEPARATOR 0x0800
#define MF_SYSMENU 0x2000
#define MF_HELP 0x4000
#define MF_MOUSESELECT 0x8000

/* TPM_* (winuser.h 1637-1655). The game uses 0, TPM_RIGHTBUTTON, TPM_CENTERALIGN | 0 and
 * TPM_CENTERALIGN | TPM_RIGHTBUTTON (re/decomp/seg04/467c_HOSTMODEDIALOG.c:93, seg15/55a6_NEWGAMEDLG2.c:348,
 * seg24/0e24_FUN_10b8_0e24.c:218). TPM_NONOTIFY and TPM_RETURNCMD are Win32 flags, accepted because
 * Wine accepts them. */
#define TPM_LEFTBUTTON 0x0000
#define TPM_RIGHTBUTTON 0x0002
#define TPM_LEFTALIGN 0x0000
#define TPM_CENTERALIGN 0x0004
#define TPM_RIGHTALIGN 0x0008
#define TPM_TOPALIGN 0x0000
#define TPM_VCENTERALIGN 0x0010
#define TPM_BOTTOMALIGN 0x0020
#define TPM_NONOTIFY 0x0080
#define TPM_RETURNCMD 0x0100

/* Accelerator flags (winuser.h 735-741) */
#define FVIRTKEY 0x01
#define FNOINVERT 0x02
#define FSHIFT 0x04
#define FCONTROL 0x08
#define FALT 0x10
#define ACCEL_LAST 0x80 /* resource only: marks the last entry of a table */

/* The pop-up window class procedure (callback segment). */
#define USER_CB_MENUPOPUP MK_FP(MEM_SEL_CALLBACK, 0x0090)

/* ---- menu API ---- */

HMENU16 CreateMenu(void);
HMENU16 CreatePopupMenu(void);
uint16_t DestroyMenu(HMENU16 hMenu);
uint16_t AppendMenu(HMENU16 hMenu, uint16_t uFlags, uint16_t uIDNewItem, FARPTR lpNewItem);
uint16_t InsertMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags, uint16_t uIDNewItem, FARPTR lpNewItem);
uint16_t DeleteMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags); /* destroys a pop-up item's sub-menu */
uint16_t RemoveMenu(HMENU16 hMenu, uint16_t uPosition, uint16_t uFlags); /* keeps it */
HMENU16 GetMenu(HWND16 hwnd);
uint16_t SetMenu(HWND16 hwnd, HMENU16 hMenu);
HMENU16 GetSubMenu(HMENU16 hMenu, int16_t nPos);
int16_t GetMenuItemCount(HMENU16 hMenu);                                    /* -1 for a bad handle */
uint16_t CheckMenuItem(HMENU16 hMenu, uint16_t uIDCheckItem, uint16_t uCheck); /* previous MF_CHECKED / MF_UNCHECKED, 0xFFFF when absent */
uint16_t EnableMenuItem(HMENU16 hMenu, uint16_t uIDEnableItem, uint16_t uEnable); /* previous MF_GRAYED / MF_DISABLED / MF_ENABLED, 0xFFFF */
uint16_t GetMenuState(HMENU16 hMenu, uint16_t uId, uint16_t uFlags);       /* flags, or for a pop-up item count << 8 | flags & 0xFF; 0xFFFF when absent */
void DrawMenuBar(HWND16 hwnd);
HMENU16 LoadMenu(HANDLE16 hInstance, FARPTR lpMenuName);                    /* RT_MENU resource of the loaded module */
HMENU16 GetSystemMenu(HWND16 hwnd, uint16_t bRevert);                       /* the control menu (one pop-up inside an outer menu) */
/* TrackPopupMenu: modal. Win3.1 returns TRUE; with TPM_RETURNCMD it returns the id. The command is POSTED as WM_COMMAND
 * (the game peeks for it right after: re/decomp/seg04/467c_HOSTMODEDIALOG.c:96). */
uint16_t TrackPopupMenu(HMENU16 hMenu, uint16_t fuFlags, int16_t x, int16_t y, int16_t nReserved, HWND16 hwnd, FARPTR lpRect);

/* ---- accelerators ---- */

HACCEL16 LoadAccelerators(HANDLE16 hInstance, FARPTR lpTableName); /* RT_ACCELERATOR resource; 0 when absent */
/* Returns 1 when the message matched an entry (WM_COMMAND / WM_SYSCOMMAND sent when allowed), else 0. */
int16_t TranslateAccelerator(HWND16 hwnd, HACCEL16 hAccel, FARPTR lpMsg);

/* ---- hooks for tests and for the window system ---- */

typedef struct UMenuItemInfo {
  uint16_t flags, id, sub; /* sub: sub-menu handle for MF_POPUP items, else 0 */
  char text[64];
} UMenuItemInfo;
bool user_menu_item_info(HMENU16 hMenu, unsigned pos, UMenuItemInfo *out);

typedef struct UAccelEntry {
  uint8_t fVirt;
  uint16_t key, cmd;
} UAccelEntry;
/* Copies up to max entries; returns the number of entries of the table (0 when the handle is bad). */
unsigned user_accel_entries(HACCEL16 hAccel, UAccelEntry *out, unsigned max);

/* The tracker, as seen from outside (valid while GetMessage is running inside the modal loop). */
#define UM_MAX_LEVELS 8
typedef struct UMenuTrackInfo {
  int active;                  /* a tracking loop is running */
  int depth;                   /* levels in use, level 0 is the menu bar or the TrackPopupMenu menu */
  HWND16 owner;
  struct {
    HMENU16 menu;
    HWND16 hwnd;               /* pop-up window, 0 for the menu bar */
    RECT16 rect;               /* screen rectangle */
    int sel;                   /* selected item, -1 none */
  } lv[UM_MAX_LEVELS];
} UMenuTrackInfo;
bool user_menu_track_info(UMenuTrackInfo *out);
/* Screen rectangle of an item. For the menu bar of a visible window or for a menu on screen in the tracker. */
bool user_menu_item_rect(HMENU16 hMenu, unsigned pos, RECT16 *out);
/* Size (width, height) of a menu drawn as a pop-up. */
bool user_menu_popup_size(HMENU16 hMenu, int *w, int *h);
/* The outer menu of a window's control menu (its only item is the pop-up GetSystemMenu returns); 0 without WS_SYSMENU. */
HMENU16 user_menu_sys_outer(HWND16 hwnd);

/* ---- internals shared by menu.c, menu_ui.c, accel.c and the window system ---- */

typedef struct UMenuItem {
  uint16_t flags, id, sub;
  char text[64];
} UMenuItem;

void user_menu_init(void);                 /* called by user_init */
void user_menu_shutdown(void);             /* called by user_shutdown */
void user_menu_window_gone(HWND16 hwnd, uint16_t hmenu, uint16_t hsys); /* the window slot is being freed */
uint16_t user_menu_for_new_window(HWND16 hwnd, uint32_t style, HANDLE16 hMenuArg, HANDLE16 hinst, uint32_t class_menu_name);
int user_menu_bar_height(void);            /* SM_CYMENU */
/* Menu object access (menu.c) */
bool user_menu_valid(HMENU16 h);
UMenuItem *user_menu_find(HMENU16 h, uint16_t id, uint16_t flags, unsigned *pos, HMENU16 *where); /* by command (recursive) or by position */
/* Drawing and tracking (menu_ui.c) */
void user_menu_paint_bar(HWND16 hwnd, HDC16 hdc, RECT16 bar); /* bar in window coordinates */
void user_menu_redraw_bar(HWND16 hwnd);
void user_menu_track_mouse(HWND16 hwnd, int ht, int sx, int sy);          /* SC_MOUSEMENU (ht = 0 or HTSYSMENU) */
void user_menu_track_keyboard(HWND16 hwnd, uint16_t wparam, uint16_t ch);  /* SC_KEYMENU */
uint32_t user_menu_popup_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
uint32_t user_menu_key_event(HWND16 hwnd, uint16_t msg, uint16_t wp, uint32_t lp); /* DefWindowProc keys: Alt / F10 / Alt+Space / Alt+letter */

#endif /* STARS_RUNTIME_MENU_H */
