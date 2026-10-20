/* USER part 1 (task stars-0z4.6): window classes, window tree, messages, painting, as plain C
 * functions named and typed like the Win16 API. No harness dependency: the native harness
 * decodes the PASCAL stack into these calls in harness/user_thunks.c (user_register). Far
 * pointers are FARPTR (selector:offset); handles are 16 bits.
 *
 * Model (docs/PLAN.md section 3):
 *  - All state (window table, classes, message queue, update regions) lives in the arena
 *    (runtime/mem.h rt_alloc), so a snapshot of the arena is a snapshot of USER.
 *  - A window procedure is a far pointer: to original code (through rt_call_far), to
 *    decompiled C (registry, through rt_call_far), or to a runtime class procedure in the
 *    callback segment MEM_SEL_CALLBACK (USER_CB_DEFWNDPROC), which GetWindowLong(GWL_WNDPROC)
 *    can return so that subclassing works.
 *  - Messages are delivered with rt_call_far(proc, FAR_PASCAL, {hwnd, msg, wParam, lParam hi,
 *    lParam lo}). Message numbers are the Windows 3.1 (Win16) ones, never the Win32 ones.
 *
 * Message order and the other behaviours that no reference capture backs are listed in
 * docs/inventory.md section 13, each marked verified (SDK or Wine, with source) or UNVERIFIED.
 * Non-client painting and hit-testing are the Windows 3.1 look task (stars-0z4.9, user_nc.c,
 * user_scroll.c, user_cursor.c, user_msgbox.c; the declarations are at the end of this file): the
 * client rectangle is what DefWindowProc(WM_NCCALCSIZE) leaves inside the frame, caption and scroll
 * bars of the window style. Input, focus, capture, timers, cursors, system values and rectangle helpers are USER part 2
 * (stars-0z4.7: user_input.c, user_misc.c, wsprintf.c).
 */
#ifndef STARS_RUNTIME_USER_H
#define STARS_RUNTIME_USER_H

#include <stdbool.h>
#include <stdint.h>

#include "runtime/gdi.h"
#include "runtime/include/win16.h"
#include "runtime/mem.h"

typedef uint16_t HWND16;

#pragma pack(push, 1)
typedef struct POINT16 {
  int16_t x, y;
} POINT16;

/* Win16 MSG: 18 bytes. */
typedef struct MSG16 {
  HWND16 hwnd;
  uint16_t message;
  uint16_t wParam;
  uint32_t lParam;
  uint32_t time;
  POINT16 pt;
} MSG16;

/* Win16 PAINTSTRUCT: 32 bytes. */
typedef struct PAINTSTRUCT16 {
  HDC16 hdc;
  uint16_t fErase;
  RECT16 rcPaint;
  uint16_t fRestore;
  uint16_t fIncUpdate;
  uint8_t rgbReserved[16];
} PAINTSTRUCT16;

/* Win16 CREATESTRUCT: 34 bytes. */
typedef struct CREATESTRUCT16 {
  FARPTR lpCreateParams;
  HANDLE16 hInstance;
  HANDLE16 hMenu;
  HWND16 hwndParent;
  int16_t cy, cx, y, x;
  uint32_t style;
  FARPTR lpszName;
  FARPTR lpszClass;
  uint32_t dwExStyle;
} CREATESTRUCT16;

/* Win16 WNDCLASS: 26 bytes. */
typedef struct WNDCLASS16 {
  uint16_t style;
  FARPTR lpfnWndProc;
  int16_t cbClsExtra, cbWndExtra;
  HANDLE16 hInstance;
  HANDLE16 hIcon, hCursor, hbrBackground;
  FARPTR lpszMenuName;
  FARPTR lpszClassName;
} WNDCLASS16;

/* Win16 WINDOWPOS: 14 bytes. */
typedef struct WINDOWPOS16 {
  HWND16 hwnd;
  HWND16 hwndInsertAfter;
  int16_t x, y, cx, cy;
  uint16_t flags;
} WINDOWPOS16;

/* Win16 NCCALCSIZE_PARAMS: 28 bytes. */
typedef struct NCCALCSIZE_PARAMS16 {
  RECT16 rgrc[3];
  FARPTR lppos;
} NCCALCSIZE_PARAMS16;

/* Win16 MINMAXINFO: 20 bytes. */
typedef struct MINMAXINFO16 {
  POINT16 ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO16;

/* Win16 WINDOWPLACEMENT: 22 bytes. */
typedef struct WINDOWPLACEMENT16 {
  uint16_t length, flags, showCmd;
  POINT16 ptMinPosition, ptMaxPosition;
  RECT16 rcNormalPosition;
} WINDOWPLACEMENT16;
#pragma pack(pop)

/* ---- Windows 3.1 constants ---- */

/* Window messages (Win16 numbering; WM_CTLCOLOR is 0x0019 here, the Win32 WM_CTLCOLORxxx
 * messages do not exist). */
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_CTLCOLOR 0x0019
#define WM_CANCELMODE 0x001F
#define WM_SETCURSOR 0x0020
#define WM_CHILDACTIVATE 0x0022
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_KEYLAST 0x0108
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_SYSTIMER 0x0118
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSELAST 0x0209
#define WM_PARENTNOTIFY 0x0210
#define WM_USER 0x0400

#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2

#define HTERROR (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2
#define HTSYSMENU 3
#define HTGROWBOX 4
#define HTMENU 5
#define HTHSCROLL 6
#define HTVSCROLL 7
#define HTREDUCE 8
#define HTZOOM 9
#define HTLEFT 10
#define HTRIGHT 11
#define HTTOP 12
#define HTTOPLEFT 13
#define HTTOPRIGHT 14
#define HTBOTTOM 15
#define HTBOTTOMLEFT 16
#define HTBOTTOMRIGHT 17
#define HTBORDER 18

/* WM_ACTIVATE wParam */
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2

/* Mouse message wParam flags */
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010

/* Virtual keys used by the runtime itself */
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_F10 0x79
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91

/* Window styles. */
#define WS_OVERLAPPED 0x00000000UL
#define WS_POPUP 0x80000000UL
#define WS_CHILD 0x40000000UL
#define WS_MINIMIZE 0x20000000UL
#define WS_VISIBLE 0x10000000UL
#define WS_DISABLED 0x08000000UL
#define WS_CLIPSIBLINGS 0x04000000UL
#define WS_CLIPCHILDREN 0x02000000UL
#define WS_MAXIMIZE 0x01000000UL
#define WS_CAPTION 0x00C00000UL
#define WS_BORDER 0x00800000UL
#define WS_DLGFRAME 0x00400000UL
#define WS_VSCROLL 0x00200000UL
#define WS_HSCROLL 0x00100000UL
#define WS_SYSMENU 0x00080000UL
#define WS_THICKFRAME 0x00040000UL
#define WS_MINIMIZEBOX 0x00020000UL
#define WS_MAXIMIZEBOX 0x00010000UL
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_DLGMODALFRAME 0x00000001UL
#define DS_MODALFRAME 0x0080 /* dialog style; the dialog manager turns it into WS_EX_DLGMODALFRAME */
#define WS_EX_NOPARENTNOTIFY 0x00000004UL
#define WS_EX_TOPMOST 0x00000008UL
#define WS_EX_TRANSPARENT 0x00000020UL

#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_NOCLOSE 0x0200
#define CW_USEDEFAULT ((int16_t)0x8000)

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOCOPYBITS 0x0100
#define SWP_NOOWNERZORDER 0x0200
#define SWP_NOSENDCHANGING 0x0400
/* Runtime-internal flags, as the WINDOWPOS flags Wine passes to WM_WINDOWPOSCHANGED. */
#define SWP_NOCLIENTSIZE 0x0800
#define SWP_NOCLIENTMOVE 0x1000

#define HWND_TOP ((HWND16)0)
#define HWND_BOTTOM ((HWND16)1)
#define HWND_TOPMOST ((HWND16)0xFFFF)
#define HWND_NOTOPMOST ((HWND16)0xFFFE)

#define GW_HWNDFIRST 0
#define GW_HWNDLAST 1
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWW_ID GWL_ID

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002

#define SW_PARENTCLOSING 1
#define SW_PARENTOPENING 3

/* ---- lifecycle ---- */

/* Builds the USER state in the arena (call after mem_init and gdi_init). Safe to call again
 * after user_shutdown. Returns 0 on success, negative on failure. */
int user_init(void);
/* Destroys every window silently (no messages) and frees the USER blocks. Call while the arena
 * is alive. */
void user_shutdown(void);

/* ---- runtime window procedures ---- */

/* Offsets in the callback segment (MEM_SEL_CALLBACK) of the runtime class procedures. */
#define USER_CB_DEFWNDPROC MK_FP(MEM_SEL_CALLBACK, 0x0000)

/* Calls a window procedure (runtime class procedure or rt_call_far). */
uint32_t user_call_wndproc(FARPTR proc, HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam);

/* GetMessage, when nothing is queued and nothing can be synthesised, sets this and returns 0:
 * in native tests "no more input" ends the run. The host (browser) yields instead through
 * user_wait_hook, which returns true when it produced input. */
extern int user_input_exhausted;
extern bool (*user_wait_hook)(void);

/* ---- classes and windows ---- */

uint16_t RegisterClass(FARPTR lpWndClass); /* returns the class atom, 0 on failure */
HWND16 CreateWindow(FARPTR lpClassName, FARPTR lpWindowName, uint32_t dwStyle, int16_t x, int16_t y,
                    int16_t cx, int16_t cy, HWND16 hwndParent, HANDLE16 hMenu, HANDLE16 hInstance,
                    FARPTR lpParam);
uint16_t DestroyWindow(HWND16 hwnd);
uint16_t ShowWindow(HWND16 hwnd, int16_t nCmdShow); /* returns whether the window was visible */
uint16_t MoveWindow(HWND16 hwnd, int16_t x, int16_t y, int16_t cx, int16_t cy, uint16_t fRepaint);
uint16_t SetWindowPos(HWND16 hwnd, HWND16 hwndInsertAfter, int16_t x, int16_t y, int16_t cx, int16_t cy,
                      uint16_t flags);
void GetWindowRect(HWND16 hwnd, FARPTR lpRect);
void GetClientRect(HWND16 hwnd, FARPTR lpRect);
void ClientToScreen(HWND16 hwnd, FARPTR lpPoint);
void ScreenToClient(HWND16 hwnd, FARPTR lpPoint);
void MapWindowPoints(HWND16 hwndFrom, HWND16 hwndTo, FARPTR lpPoints, uint16_t cPoints);
HWND16 WindowFromPoint(uint32_t pt); /* x in the low word, y in the high word (screen) */
HWND16 GetParent(HWND16 hwnd);
HWND16 GetWindow(HWND16 hwnd, uint16_t uCmd);
HWND16 GetDesktopWindow(void);
uint16_t IsWindow(HWND16 hwnd);
uint16_t IsWindowVisible(HWND16 hwnd);
uint16_t IsIconic(HWND16 hwnd);
uint16_t IsZoomed(HWND16 hwnd);
uint16_t IsWindowEnabled(HWND16 hwnd);
uint16_t EnableWindow(HWND16 hwnd, uint16_t bEnable); /* returns the previous disabled state */
int16_t GetWindowText(HWND16 hwnd, FARPTR lpString, int16_t nMaxCount);
void SetWindowText(HWND16 hwnd, FARPTR lpString);
int16_t GetWindowTextLength(HWND16 hwnd);
uint32_t GetWindowLong(HWND16 hwnd, int16_t nIndex);
uint32_t SetWindowLong(HWND16 hwnd, int16_t nIndex, uint32_t dwNewLong);
uint16_t GetWindowWord(HWND16 hwnd, int16_t nIndex);
uint16_t SetWindowWord(HWND16 hwnd, int16_t nIndex, uint16_t wNewWord);
uint32_t CallWindowProc(FARPTR lpPrevWndFunc, HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam);
uint32_t DefWindowProc(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam);
uint16_t GetWindowPlacement(HWND16 hwnd, FARPTR lpwndpl);

/* ---- messages ---- */

uint16_t GetMessage(FARPTR lpMsg, HWND16 hwnd, uint16_t wMsgFilterMin, uint16_t wMsgFilterMax);
uint16_t PeekMessage(FARPTR lpMsg, HWND16 hwnd, uint16_t wMsgFilterMin, uint16_t wMsgFilterMax,
                     uint16_t wRemoveMsg);
uint16_t PostMessage(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam);
uint32_t SendMessage(HWND16 hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam);
uint16_t TranslateMessage(FARPTR lpMsg); /* WM_KEYDOWN / WM_SYSKEYDOWN -> WM_CHAR / WM_SYSCHAR (user_input.c); 1 when a character was posted */
uint32_t DispatchMessage(FARPTR lpMsg);
void PostQuitMessage(int16_t nExitCode);

/* ---- painting ---- */

HDC16 BeginPaint(HWND16 hwnd, FARPTR lpPaint);
void EndPaint(HWND16 hwnd, FARPTR lpPaint);
void InvalidateRect(HWND16 hwnd, FARPTR lpRect, uint16_t bErase); /* lpRect 0: whole client area */
void ValidateRect(HWND16 hwnd, FARPTR lpRect);
void UpdateWindow(HWND16 hwnd);
HDC16 GetDC(HWND16 hwnd);
uint16_t ReleaseDC(HWND16 hwnd, HDC16 hdc);
void ScrollWindow(HWND16 hwnd, int16_t dx, int16_t dy, FARPTR lpRect, FARPTR lpClipRect);

/* ---- USER part 2 (task stars-0z4.7): input, focus, capture, timers, cursors, metrics ----
 *
 * Host entry points. Both only queue a raw event; hit testing, WM_NCHITTEST, WM_SETCURSOR,
 * capture routing and key-state updates happen when the application retrieves the message
 * (GetMessage / PeekMessage), after posted messages and WM_QUIT and before WM_PAINT and timers.
 *  - rt_input_mouse: x, y in screen coordinates; buttons is the full button state
 *    (RT_BTN_LEFT | RT_BTN_RIGHT | RT_BTN_MIDDLE); time_ms is the host time stamp that ends up in
 *    MSG.time. For rt_input_mouse and rt_input_key alike it is on the SAME boot-relative base as
 *    GetTickCount, so MSG.time is consistent with the time stamps of timer messages. A change of position queues WM_MOUSEMOVE (consecutive moves are merged), then
 *    each button change queues its down / up message (left, right, middle order).
 *  - rt_input_key: vk is the Win16 virtual-key code (generic VK_SHIFT / VK_CONTROL / VK_MENU),
 *    scancode the keyboard scan code (bit 8 = extended key), down 1 or 0 (a down for a key that
 *    is already down is an auto-repeat). */
#define RT_BTN_LEFT 1
#define RT_BTN_RIGHT 2
#define RT_BTN_MIDDLE 4
void rt_input_mouse(int16_t x, int16_t y, uint16_t buttons, uint32_t time_ms);
void rt_input_key(uint16_t vk, uint16_t scancode, int down, uint32_t time_ms);

typedef uint16_t HCURSOR16;

/* Focus, activation, capture */
HWND16 SetFocus(HWND16 hwnd);
HWND16 GetFocus(void);
HWND16 SetActiveWindow(HWND16 hwnd);
HWND16 GetActiveWindow(void);
HWND16 SetCapture(HWND16 hwnd);
void ReleaseCapture(void);
HWND16 GetCapture(void);
void GetCursorPos(FARPTR lpPoint);
int16_t GetKeyState(int16_t nVirtKey);
int16_t GetAsyncKeyState(int16_t nVirtKey);

/* Timers. The clock is the runtime clock (rt_clock_get) with ONE-SECOND resolution:
 * GetTickCount is milliseconds since user_init (0 at start, whatever the date) and always a
 * multiple of 1000, and a timer fires at most once per clock second (Windows 3.1 ticks at about 55 ms:
 * UNVERIFIED difference, recorded in docs/inventory.md section 13). */
uint16_t SetTimer(HWND16 hwnd, uint16_t nIDEvent, uint16_t wElapse, FARPTR lpTimerFunc);
uint16_t KillTimer(HWND16 hwnd, uint16_t nIDEvent);
uint32_t GetTickCount(void);
uint32_t GetCurrentTime(void);

/* Cursors (the shapes and the software cursor are in user_cursor.c, stars-0z4.9) */
HCURSOR16 LoadCursor(HANDLE16 hInstance, FARPTR lpCursorName); /* lpCursorName: MAKEINTRESOURCE (selector 0) or a string */
HCURSOR16 SetCursor(HCURSOR16 hCursor);                         /* returns the previous cursor */
HCURSOR16 user_current_cursor(void);                            /* test / host hook */

/* Miscellaneous */
extern void (*user_beep_hook)(uint16_t type);   /* host callback for MessageBeep */
void MessageBeep(uint16_t uType);
uint16_t FlashWindow(HWND16 hwnd, uint16_t bInvert);
uint16_t ExitWindows(uint32_t dwReturnCode, uint16_t wReserved); /* queues WM_QUIT; returns 1 */
bool user_exit_requested(void);
uint16_t InitApp(HANDLE16 hInstance);

/* System values */
int16_t GetSystemMetrics(int16_t nIndex);
uint32_t GetSysColor(uint16_t nIndex);

/* Rectangles. lpRect arguments are far pointers to RECT16; a POINT passed by value is one
 * 32-bit value, x in the low word and y in the high word. */
void SetRect(FARPTR lpRect, int16_t xLeft, int16_t yTop, int16_t xRight, int16_t yBottom);
void SetRectEmpty(FARPTR lpRect);
uint16_t CopyRect(FARPTR lpDest, FARPTR lpSrc);
uint16_t IsRectEmpty(FARPTR lpRect);
uint16_t PtInRect(FARPTR lpRect, uint32_t pt);
void OffsetRect(FARPTR lpRect, int16_t dx, int16_t dy);
void InflateRect(FARPTR lpRect, int16_t dx, int16_t dy);
uint16_t IntersectRect(FARPTR lpDest, FARPTR lpSrc1, FARPTR lpSrc2);
uint16_t UnionRect(FARPTR lpDest, FARPTR lpSrc1, FARPTR lpSrc2);
uint16_t EqualRect(FARPTR lpRect1, FARPTR lpRect2);
int16_t FillRect(HDC16 hdc, FARPTR lpRect, HBRUSH16 hbr);
int16_t FrameRect(HDC16 hdc, FARPTR lpRect, HBRUSH16 hbr);

/* wsprintf (USER.420, varargs, cdecl). lpArgs is a far pointer to the argument words that
 * follow the two fixed pointers on the 16-bit stack (a far pointer or long is two words, low
 * word first). Returns the number of characters stored (output limited to 1023 + NUL). */
int16_t _wsprintf(FARPTR lpOut, FARPTR lpFmt, FARPTR lpArgs);

/* ---- helpers for later USER tasks and for tests ---- */

/* Screen rectangle of the client area of hwnd; false when hwnd is not a window. */
bool user_client_rect_screen(HWND16 hwnd, RECT16 *out);
/* Number of live windows, the desktop excluded. */
unsigned user_window_count(void);
/* Number of rectangles in hwnd's update region (0 when it needs no painting); test hook. */
unsigned user_window_nupd(HWND16 hwnd);


/* ---- Windows 3.1 look (task stars-0z4.9): non-client area, scroll bars, cursors, icons, MessageBox ----
 *
 * No Windows 3.11 file exists yet (owner amendment to decision 8): the chrome is drawn in code to
 * the Windows 3.1 look from the metrics of GetSystemMetrics and the colours of GetSysColor. Every
 * pixel result is UNVERIFIED against Windows 3.1 (docs/inventory.md section 13, "Windows 3.1
 * look"); structure (rectangles, hit-test regions, sizes) follows the metrics. */

/* WM_SYSCOMMAND wParam (low 4 bits are the hit-test or sizing code) */
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_NEXTWINDOW 0xF040
#define SC_PREVWINDOW 0xF050
#define SC_CLOSE 0xF060
#define SC_VSCROLL 0xF070
#define SC_HSCROLL 0xF080
#define SC_MOUSEMENU 0xF090
#define SC_KEYMENU 0xF100
#define SC_ARRANGE 0xF110
#define SC_RESTORE 0xF120
#define SC_TASKLIST 0xF130

/* WM_SYSCOMMAND SC_SIZE + WMSZ_xxx */
#define WMSZ_LEFT 1
#define WMSZ_RIGHT 2
#define WMSZ_TOP 3
#define WMSZ_TOPLEFT 4
#define WMSZ_TOPRIGHT 5
#define WMSZ_BOTTOM 6
#define WMSZ_BOTTOMLEFT 7
#define WMSZ_BOTTOMRIGHT 8

/* Scroll bars */
#define SB_HORZ 0
#define SB_VERT 1
#define SB_CTL 2
#define SB_BOTH 3
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_BOTTOM 7
#define SB_ENDSCROLL 8

/* System cursor and icon identifiers (MAKEINTRESOURCE with hInstance 0) */
#define IDC_ARROW_ID 32512
#define IDC_IBEAM_ID 32513
#define IDC_WAIT_ID 32514
#define IDC_CROSS_ID 32515
#define IDC_UPARROW_ID 32516
#define IDC_SIZE_ID 32640
#define IDC_ICON_ID 32641
#define IDC_SIZENWSE_ID 32642
#define IDC_SIZENESW_ID 32643
#define IDC_SIZEWE_ID 32644
#define IDC_SIZENS_ID 32645
#define IDI_APPLICATION_ID 32512
#define IDI_HAND_ID 32513
#define IDI_QUESTION_ID 32514
#define IDI_EXCLAMATION_ID 32515
#define IDI_ASTERISK_ID 32516

typedef uint16_t HICON16;

/* MessageBox */
#define MB_OK 0x0000
#define MB_OKCANCEL 0x0001
#define MB_ABORTRETRYIGNORE 0x0002
#define MB_YESNOCANCEL 0x0003
#define MB_YESNO 0x0004
#define MB_RETRYCANCEL 0x0005
#define MB_TYPEMASK 0x000F
#define MB_ICONHAND 0x0010
#define MB_ICONQUESTION 0x0020
#define MB_ICONEXCLAMATION 0x0030
#define MB_ICONASTERISK 0x0040
#define MB_ICONMASK 0x00F0
#define MB_DEFBUTTON1 0x0000
#define MB_DEFBUTTON2 0x0100
#define MB_DEFBUTTON3 0x0200
#define MB_DEFMASK 0x0F00
#define MB_APPLMODAL 0x0000
#define MB_SYSTEMMODAL 0x1000
#define MB_TASKMODAL 0x2000
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

/* Window DC, scroll bars */
HDC16 GetWindowDC(HWND16 hwnd); /* the whole window, non-client area included */
int16_t SetScrollPos(HWND16 hwnd, int16_t nBar, int16_t nPos, uint16_t bRedraw); /* returns the previous position */
int16_t GetScrollPos(HWND16 hwnd, int16_t nBar);
void SetScrollRange(HWND16 hwnd, int16_t nBar, int16_t nMinPos, int16_t nMaxPos, uint16_t bRedraw);
void GetScrollRange(HWND16 hwnd, int16_t nBar, FARPTR lpMinPos, FARPTR lpMaxPos);
void ShowScrollBar(HWND16 hwnd, int16_t wBar, uint16_t bShow);
void AdjustWindowRect(FARPTR lpRect, uint32_t dwStyle, uint16_t bMenu);

/* Cursors and icons. LoadCursor / LoadIcon take MAKEINTRESOURCE or a name; a system id needs
 * hInstance 0. The game's own cursors and icons are read from the loaded module's
 * GROUP_CURSOR / GROUP_ICON resources. */
uint16_t DestroyCursor(HCURSOR16 hCursor);
int16_t ShowCursor(uint16_t bShow); /* display counter */
HICON16 LoadIcon(HANDLE16 hInstance, FARPTR lpIconName);
/* RT_BITMAP resource of the program as a screen-compatible bitmap (1-bpp: monochrome); 0 when
 * hInstance is 0 (system bitmaps) or the resource is missing (runtime/user_bitmap.c). */
HBITMAP16 LoadBitmap(HANDLE16 hInstance, FARPTR lpBitmapName);
/* WinHelp: there is no help engine; the call is accepted and does nothing, returning TRUE
 * (runtime/user_misc.c). */
uint16_t WinHelp(HWND16 hwnd, FARPTR lpHelpFile, uint16_t uCommand, uint32_t dwData);
uint16_t DrawIcon(HDC16 hdc, int16_t x, int16_t y, HICON16 hIcon);
uint16_t DestroyIcon(HICON16 hIcon);
/* Present: fb_present plus the software cursor (AND mask, then XOR mask, at the pointer
 * position minus the hot spot) composited into out[fb_width() * fb_height()] RGBA. The
 * framebuffer itself is not touched. */
void user_present(uint32_t *out);
/* Cursor shape for tests and the host: 32x32 masks, row-major, one byte per pixel (1 = set). */
bool user_cursor_masks(HCURSOR16 h, uint8_t and_mask[32 * 32], uint8_t xor_mask[32 * 32], int16_t *hot_x, int16_t *hot_y);
/* Icon pixels for tests: 32x32 RGB (GDI_RGB) with the AND mask (1 = transparent). */
bool user_icon_pixels(HICON16 h, uint32_t rgb[32 * 32], uint8_t and_mask[32 * 32]);

/* MessageBox: modal; runs its own message loop and returns the button id (IDOK ...), 0 when the
 * message queue ends (WM_QUIT is re-posted). */
int16_t MessageBox(HWND16 hwndParent, FARPTR lpText, FARPTR lpCaption, uint16_t uType);

/* Test hook: the MessageBox on screen (false when none). Rectangles are screen coordinates. */
bool user_msgbox_info(HWND16 *hwnd, int *nbtn, RECT16 btn[3], uint16_t ids[3], int *def, RECT16 *icon, RECT16 *text);

/* Timer id used by the scroll bar tracking loop for auto-repeat (a WM_TIMER with this id, sent to
 * the notified window, repeats the current action; tests post it). */
#define USER_SB_TIMER_ID 0x7FF1

/* Desktop: fills the screen with COLOR_BACKGROUND (done by user_init) and draws the minimised
 * windows as icons. */
void user_paint_desktop(void);

/* Non-client layout, window coordinates (0,0 = top-left of the window rectangle). Empty
 * rectangles for parts a window does not have. Test and host hook. */
typedef struct UNcLayout {
  RECT16 window, client;
  int16_t frame;                       /* frame/border width in pixels (0, 1 or SM_CXFRAME/SM_CXDLGFRAME) */
  uint8_t thick, dlg, border, caption, iconic;
  RECT16 capbar;                       /* caption bar; a 1-pixel black line follows below it */
  RECT16 sysbox, minbtn, maxbtn, title;
  RECT16 hscroll, vscroll, sizebox;
  RECT16 menubar;                      /* menu bar (stars-0z4.13); empty without a menu */
} UNcLayout;
bool user_nc_layout(HWND16 hwnd, UNcLayout *out);
/* The same computation for a style and window size (what AdjustWindowRect and WM_NCCALCSIZE use). */
void user_nc_layout_for(uint32_t style, uint32_t exstyle, int w, int h, UNcLayout *out);
/* The same with a menu bar (SM_CYMENU below the caption). */
void user_nc_layout_for_menu(uint32_t style, uint32_t exstyle, bool has_menu, int w, int h, UNcLayout *out);

/* System menu (control menu) content: id is an SC_xxx command, 0 for a separator. */
typedef struct USysItem {
  uint16_t id;
  uint8_t enabled;
  char text[24];
} USysItem;
unsigned user_sysmenu_items(HWND16 hwnd, USysItem out[10]);
/* A host or a test may set this to take the click on the control-menu box itself; it receives the window and
 * the screen position under the box. Without it the click runs the menu tracker (runtime/menu_ui.c, stars-0z4.13):
 * WM_SYSCOMMAND SC_MOUSEMENU + HTSYSMENU, then the control-menu pop-up. */
extern bool (*user_sysmenu_hook)(HWND16 hwnd, int16_t x, int16_t y);

/* Standard controls (stars-0z4.10): BUTTON, STATIC, SCROLLBAR classes and the dialog button helpers. */
#include "runtime/ctl.h"

/* Dialog manager (stars-0z4.12): DialogBox, CreateDialog, EndDialog, GetDlgItem, ... */
#include "runtime/dialog.h"

/* Menus and accelerators (stars-0z4.13): menu.c, menu_ui.c, accel.c. */
#include "runtime/menu.h"

#endif /* STARS_RUNTIME_USER_H */
