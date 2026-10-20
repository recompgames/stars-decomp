/* Internal state and helpers shared by runtime/user_win.c, user_msg.c and user_paint.c
 * (task stars-0z4.6). Not part of the public API. All state is one block in the arena. */
#ifndef STARS_RUNTIME_USER_INTERNAL_H
#define STARS_RUNTIME_USER_INTERNAL_H

#include <string.h>

#include "runtime/user.h"

#define UW_MAX 256   /* window slots, slot 0 is the desktop */
#define UC_MAX 48    /* window classes */
#define UQ_MAX 256   /* posted-message queue (Windows 3.1's default is 8: UNVERIFIED choice) */
#define UPD_MAX 8    /* rectangles per update region before they collapse to a bounding box */
#define UTEXT_MAX 80
#define UCLS_NAME 40
#define UHWND_BASE 0x0A00u
#define UHWND_STEP 4u
#define UATOM_BASE 0xC000u
#define UHWND_BROADCAST 0xFFFFu

/* Internal SWP flag: the window changed minimised/maximised state (Wine's SWP_STATECHANGED). */
#define SWP_STATECHANGED 0x8000
#define SWP_AGG_NOCLIENTCHANGE (SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE)
#define SWP_AGG_NOPOSCHANGE (SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOZORDER)
#define SWP_AGG_STATUSFLAGS (SWP_AGG_NOPOSCHANGE | SWP_FRAMECHANGED | SWP_HIDEWINDOW | SWP_SHOWWINDOW)
#define SWP_AGG_NOGEOMETRYCHANGE (SWP_NOSIZE | SWP_NOCLIENTSIZE | SWP_NOZORDER)

typedef struct UWnd {
  uint8_t used, need_size, erase, nupd;
  uint16_t cls;                       /* class slot */
  HWND16 parent, child, next, owner;  /* parent is the desktop for top-level windows; child is
                                         the topmost child, next the sibling below */
  uint16_t id, hinst, nextra, pad;
  uint16_t hmenu, hsys;               /* menu bar and control-menu handles (menu.c, stars-0z4.13) */
  uint8_t ncact;                      /* non-client "active" look (set by DefWindowProc(WM_NCACTIVATE)) */
  uint8_t ncdirty;                    /* the non-client area needs WM_NCPAINT before the next WM_PAINT */
  RECT16 nrect;                       /* restored rectangle while minimised or maximised */
  POINT16 minpos;                     /* icon position (-1,-1: not placed yet) */
  uint32_t style, exstyle, wndproc, extra_off;
  RECT16 wrect, crect;                /* window and client rectangles in the PARENT's client
                                         coordinates */
  RECT16 upd[UPD_MAX];                /* update region, client coordinates */
  char text[UTEXT_MAX];
} UWnd;

typedef struct UClass {
  uint8_t used, pad;
  uint16_t style;
  uint32_t wndproc;
  int16_t cbcls, cbwnd;
  uint16_t hinst, hicon, hcursor, hbr;
  uint32_t menu;
  char name[UCLS_NAME];
} UClass;

typedef struct UMsg {
  HWND16 hwnd;
  uint16_t msg, wp;
  uint32_t lp, time;
  int16_t x, y;
} UMsg;

/* USER part 2 state (task stars-0z4.7), embedded in UserState so a snapshot of the arena covers it. */
#define UH_MAX 64    /* raw hardware events waiting for GetMessage / PeekMessage */
#define UT_MAX 32    /* timers (Windows 3.1's system limit: UNVERIFIED) */
#define UCUR_MAX 16  /* application cursors handed out by LoadCursor */

typedef struct UHw {
  UMsg m;       /* mouse: raw message code, screen x/y, time; key: final message code, wp = vk, lp */
  uint32_t seq; /* identity, so an entry can be found again after window procedures ran */
} UHw;

typedef struct UTimer {
  uint8_t used, pad;
  HWND16 hwnd;
  uint16_t id, elapse;
  uint32_t proc, due;
} UTimer;

typedef struct UCursorEnt {
  uint16_t used, hinst;
  uint32_t key;
} UCursorEnt;

typedef struct UInput {
  HWND16 focus, active, capture;
  uint16_t hhead, hcount;
  uint32_t hseq;
  UHw hq[UH_MAX];
  uint8_t keysync[256];     /* key state as of the messages the application has retrieved */
  uint8_t keyasync[256];    /* hardware state as of the latest rt_input_* call */
  uint8_t keypressed[256];  /* GetAsyncKeyState bit 0: pressed since the previous call */
  uint16_t btn_sync, btn_async;
  int16_t px, py;
  uint8_t has_pos, alt_pressed, exit_requested, pad0;
  uint16_t clk_msg;         /* last button-down remembered for double-click detection (0 = none) */
  HWND16 clk_hwnd;
  uint32_t clk_time;
  int16_t clk_x, clk_y;
  UTimer timers[UT_MAX];
  uint16_t next_timer_id;
  uint32_t tick_base;       /* rt_clock_ms when user_init ran: GetTickCount is relative to it */
  HCURSOR16 cursor;
  UCursorEnt cursors[UCUR_MAX];
} UInput;

/* Windows 3.1 look state (task stars-0z4.9), all in the arena. */
#define UI_MAX 24      /* icons handed out by LoadIcon */
#define UHICON_BASE 0x0E00u
typedef struct UCursorData {
  int16_t hx, hy;
  uint8_t andm[128], xorm[128];   /* 32x32, 1 bit per pixel, MSB first, top row first */
} UCursorData;

typedef struct UIcon {
  uint8_t used, pad;
  uint16_t hinst;
  uint32_t key;
  uint32_t pal[16];               /* GDI_RGB */
  uint8_t px[32 * 32];            /* palette index per pixel, top row first */
  uint8_t andm[32 * 32];          /* 1 = transparent */
} UIcon;

typedef struct UScroll {
  int16_t min, max, pos;
  uint8_t inited;
  uint8_t arrows;      /* SBM_ENABLE_ARROWS (stars-0z4.10): ESB_DISABLE_LTUP (1) / ESB_DISABLE_RTDN (2) bits */
} UScroll;

typedef struct UMsgBox {
  HWND16 hwnd, owner;
  uint16_t type;
  int16_t result;
  uint8_t nbtn, def, focus, pressed, done;
  uint16_t ids[3];
  char labels[3][12];
  RECT16 btn[3];
  RECT16 textrc, iconrc;
  HICON16 icon;
  char text[1024]; /* was 256: longer texts were cut at 255 characters */
} UMsgBox;

typedef struct UserState {
  UWnd w[UW_MAX];
  UClass c[UC_MAX];
  UMsg q[UQ_MAX];
  uint16_t qhead, qcount;
  uint8_t quit_pending;
  int16_t quit_code;
  uint32_t clock; /* fake milliseconds: +1 per posted message, for MSG.time */
  UInput in;
  UCursorData curdata[UCUR_MAX];
  UIcon icons[UI_MAX];
  UScroll sb[UW_MAX][3];          /* SB_HORZ, SB_VERT, SB_CTL per window slot */
  int16_t cursor_count;           /* ShowCursor display counter */
  UMsgBox mb;                     /* the MessageBox being shown (saved and restored when nested) */
  uint16_t ctl_brush[3];          /* standard controls (stars-0z4.10): COLOR_BTNFACE, COLOR_WINDOW, COLOR_SCROLLBAR brushes */
  uint32_t ctl_clip_off;          /* standard controls part 2 (stars-0z4.11): the edit controls' internal clipboard (arena block, 0 = none) */
  uint16_t ctl_clip_len, ctl_clip_cap;
} UserState;

UserState *user_state(void);
UWnd *user_wnd(HWND16 h);          /* NULL when h is not a live window (the desktop is one) */
HWND16 user_hwnd(const UWnd *w);
HWND16 user_desktop(void);
void user_client_origin(const UWnd *w, int *x, int *y);   /* client origin in screen coordinates */
void user_window_origin(const UWnd *w, int *x, int *y);   /* window origin in screen coordinates */
HWND16 user_window_from_point_skip(uint32_t pt, const HWND16 *skip, unsigned nskip); /* WindowFromPoint past HTTRANSPARENT windows */
bool user_visible(HWND16 h);       /* the window and every ancestor have WS_VISIBLE */
HWND16 user_first_paint(HWND16 parent);
void user_queue_purge(HWND16 hwnd);
uint32_t user_def_window_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);

/* USER part 2 internals (user_input.c, user_misc.c, user_msg.c) */
uint32_t user_tick(void);                 /* GetTickCount: runtime clock, one-second resolution */
bool user_activate_shown(HWND16 hwnd);    /* ShowWindow / SetWindowPos made a top-level window visible: activate it */
void user_input_init(void);               /* called by user_init */
void user_input_pre_destroy(HWND16 hwnd);   /* DestroyWindow, before the hide: capture is released */
void user_input_leave_destroy(HWND16 hwnd); /* DestroyWindow, after the hide and owned windows: activation and focus leave */
void user_input_disabled(HWND16 hwnd);    /* EnableWindow(FALSE): the focus window loses the focus */
void user_input_window_gone(HWND16 hwnd); /* the window slot is being freed */
bool user_input_fetch(MSG16 *out, HWND16 hwnd, uint16_t min, uint16_t max, bool remove);
bool user_timer_fetch(MSG16 *out, HWND16 hwnd, uint16_t min, uint16_t max, bool remove);
uint32_t user_def_setcursor(HWND16 h, uint16_t wp, uint32_t lp);
uint32_t user_syscolor(unsigned idx);     /* GetSysColor, 0 for an index out of range */
uint16_t user_post_ex(HWND16 hwnd, uint16_t msg, uint16_t wp, uint32_t lp, uint32_t time, int16_t x, int16_t y);
uint16_t user_translate(const MSG16 *m);
bool user_in_range(uint16_t msg, uint16_t min, uint16_t max);

/* Far scratch memory for message parameter blocks (global heap, freed by the caller). */
FARPTR user_scratch(uint32_t bytes, uint16_t *handle);
void user_scratch_free(uint16_t handle);

/* Update regions (user_paint.c). Rectangles are client coordinates. */
void user_upd_add(UWnd *w, RECT16 r, bool erase);
void user_upd_sub(UWnd *w, RECT16 r);
bool user_upd_bbox(const UWnd *w, RECT16 *out);
HDC16 user_get_dc(HWND16 h, const RECT16 *paint_clip);
uint32_t user_def_erase(HWND16 h, HDC16 hdc);
uint32_t user_def_paint(HWND16 h);

HWND16 user_create_window_ex(uint32_t exstyle, FARPTR lpClassName, FARPTR lpWindowName, uint32_t dwStyle, int16_t x, int16_t y,
                             int16_t cx, int16_t cy, HWND16 hwndParent, HANDLE16 hMenu, HANDLE16 hInstance, FARPTR lpParam);

/* Windows 3.1 look (user_nc.c, user_scroll.c, user_cursor.c, user_msgbox.c) */
uint32_t user_nc_calcsize(HWND16 h, uint16_t wp, uint32_t lp);
int user_nc_hittest(HWND16 h, int sx, int sy);
void user_nc_paint(HWND16 h);                  /* draws the whole non-client area now */
void user_nc_paint_caption(HWND16 h);          /* caption bar only (title/activation changes) */
uint32_t user_nc_def_message(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp); /* WM_NC*, WM_SYSCOMMAND, sys keys */
void user_nc_expose(HWND16 h, RECT16 old_wrect); /* a top-level window left old_wrect: desktop background */
HDC16 user_get_window_dc(HWND16 h);
void user_sb_reset(HWND16 h);                  /* the window slot is being freed or created */
UScroll *user_sb_get(HWND16 h, int bar);       /* lazily initialised: range 0..100, position 0 */
void user_sb_draw(HDC16 hdc, RECT16 bar, bool vert, const UScroll *s, bool enabled, int pressed_part, int thumb_pos, int parity);
void user_sb_track(HWND16 notify, HWND16 ctl, int bar, RECT16 bar_screen, bool vert, int sx, int sy);
void user_sb_draw_window(HWND16 h, int bar);   /* redraws one window scroll bar now */
HICON16 user_class_icon_or_default(HWND16 h);
void user_cursor_init(void);
void user_paint_icon_at(HDC16 hdc, int x, int y, HICON16 icon);
uint32_t user_msgbox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
void user_draw_text(HDC16 hdc, int x, int y, const char *s);
int user_text_width(HDC16 hdc, const char *s);
int user_text_height(HDC16 hdc);
void user_fill(HDC16 hdc, RECT16 r, uint32_t color);
/* Message pump for the modal tracking loops: user_pump_get returns 0 when the loop must end (no more input, or WM_QUIT,
 * which is posted again). */
typedef struct UPump {
  FARPTR p;
  uint16_t hnd;
} UPump;
bool user_pump_open(UPump *pu);
void user_pump_close(UPump *pu);
int user_pump_get(UPump *pu, MSG16 *m);
void user_pump_dispatch(UPump *pu);
void user_nc_place(HWND16 h, int mode, RECT16 *out);   /* mode 1: maximised, 2: minimised; parent client coordinates */
void user_tri_left(HDC16 dc, int x0, int y0, uint32_t c);
void user_tri_right(HDC16 dc, int x0, int y0, uint32_t c);
void user_tri_up(HDC16 dc, int x0, int y0, uint32_t c);
void user_tri_down(HDC16 dc, int x0, int y0, uint32_t c);

static inline bool r_empty(RECT16 r) { return r.right <= r.left || r.bottom <= r.top; }
static inline RECT16 r_make(int l, int t, int r, int b) {
  RECT16 x = {(int16_t)l, (int16_t)t, (int16_t)r, (int16_t)b};
  return x;
}
static inline RECT16 r_offset(RECT16 r, int dx, int dy) { return r_make(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy); }
static inline RECT16 r_isect(RECT16 a, RECT16 b) {
  RECT16 r = r_make(a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
                    a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom);
  if (r_empty(r)) r = r_make(0, 0, 0, 0);
  return r;
}
static inline RECT16 r_union(RECT16 a, RECT16 b) {
  if (r_empty(a)) return b;
  if (r_empty(b)) return a;
  return r_make(a.left < b.left ? a.left : b.left, a.top < b.top ? a.top : b.top,
                a.right > b.right ? a.right : b.right, a.bottom > b.bottom ? a.bottom : b.bottom);
}
/* a minus b as up to 4 disjoint rectangles; returns the count. */
static inline int r_subtract(RECT16 a, RECT16 b, RECT16 out[4]) {
  RECT16 i = r_isect(a, b);
  int n = 0;
  if (r_empty(a)) return 0;
  if (r_empty(i)) {
    out[0] = a;
    return 1;
  }
  if (i.top > a.top) out[n++] = r_make(a.left, a.top, a.right, i.top);
  if (i.bottom < a.bottom) out[n++] = r_make(a.left, i.bottom, a.right, a.bottom);
  if (i.left > a.left) out[n++] = r_make(a.left, i.top, i.left, i.bottom);
  if (i.right < a.right) out[n++] = r_make(i.right, i.top, a.right, i.bottom);
  return n;
}
static inline bool r_contains(RECT16 r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static inline uint32_t make_long(uint16_t lo, uint16_t hi) { return (uint32_t)lo | ((uint32_t)hi << 16); }

#endif /* STARS_RUNTIME_USER_INTERNAL_H */
