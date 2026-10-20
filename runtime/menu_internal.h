/* Shared state of menu.c, menu_ui.c and accel.c (task stars-0z4.13). One block in the arena
 * (rt_alloc) holds the menu objects, the accelerator tables and the tracker, so a snapshot of the
 * arena covers all of it. Not part of the public API. */
#ifndef STARS_RUNTIME_MENU_INTERNAL_H
#define STARS_RUNTIME_MENU_INTERNAL_H

#include "runtime/user_internal.h"

#define MN_MAX 128  /* menu objects */
#define MI_MAX 1024 /* items in all menus together */
#define AT_MAX 16   /* accelerator tables */
#define AE_MAX 64   /* entries per table */
#define UH_MENU_BASE 0x1000u
#define UH_ACCEL_BASE 0x1100u
#define MNO_NONE 0xFFFF

typedef struct UMenuObj {
  uint8_t used, sys; /* sys: a control menu (outer or pop-up), MF_SYSMENU in WM_MENUSELECT */
  uint16_t count, first; /* first item slot + 1, 0 = none */
} UMenuObj;

typedef struct UItemSlot {
  UMenuItem it;
  uint16_t next; /* slot + 1, 0 = end */
  uint8_t used, pad;
} UItemSlot;

typedef struct UAccelTab {
  uint8_t used, pad;
  uint16_t count;
  uint32_t key; /* hinst << 16 | resource index: LoadAccelerators returns the same table for the same resource */
  UAccelEntry e[AE_MAX];
} UAccelTab;

/* Tracker kinds of level 0 */
enum { MK_BAR = 0, MK_SYSBAR = 1, MK_POPUP = 2 };

typedef struct MLevel {
  uint16_t menu;
  HWND16 hwnd; /* pop-up window, 0 for the bar and the control-menu box */
  RECT16 rect; /* screen */
  int16_t sel; /* selected item, -1 none */
} MLevel;

typedef struct MTrack {
  uint8_t active, kind, exit, rcvd_btn_up, pending_return, pad0;
  uint16_t flags;      /* TPM_* */
  HWND16 owner;
  int16_t depth;       /* levels in use */
  int16_t px, py;      /* last mouse point (screen), the lParam of a posted WM_SYSCOMMAND */
  uint32_t start_time; /* time of the press that started a mouse tracking */
  int32_t result;      /* executed command id, -1 none */
  MLevel lv[UM_MAX_LEVELS];
} MTrack;

typedef struct UMenuState {
  UMenuObj m[MN_MAX];
  UItemSlot it[MI_MAX];
  UAccelTab at[AT_MAX];
  MTrack trk;
  uint8_t sys_key, f10_key; /* defwnd.c menu_sys_key / f10_key */
} UMenuState;

UMenuState *user_menu_state(void);

UMenuObj *user_menu_obj(HMENU16 h);
UMenuItem *user_menu_item(HMENU16 h, unsigned pos);  /* NULL out of range */
/* Inserts before position pos (>= count appends). Returns false on a bad handle or a full pool. */
bool user_menu_insert(HMENU16 h, unsigned pos, uint16_t flags, uint16_t id, const char *text);
HMENU16 user_menu_new(bool popup, bool sys);
void user_menu_enable_item(HMENU16 h, uint16_t id, bool enabled); /* by command, grayed when !enabled */
/* The control menu of a window (outer menu with the pop-up, filled from user_sysmenu_items). */
HMENU16 user_menu_sys_popup(HWND16 hwnd);

/* menu_ui.c */
void user_menu_ui_init(void);
void user_menu_ui_shutdown(void);

#endif /* STARS_RUNTIME_MENU_INTERNAL_H */
