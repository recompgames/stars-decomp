/* USER part 2 (task stars-0z4.7): cursors (shape state only), MessageBeep, FlashWindow,
 * ExitWindows, InitApp, system metrics and colours, rectangle helpers. Plain C, no harness
 * dependency.
 *
 * Sources. The Windows 3.1 SDK text is not available and no reference capture exists
 * (docs/PLAN.md decision 9), so the metrics and colours below are UNVERIFIED: they are the
 * Windows 3.1 default values as remembered, adjusted to the decided display mode (800x600,
 * 256 colours, decision 8). Each row and its source status is in docs/inventory.md section 13.
 * Wine is cited for behaviour (wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
 * dlls/user.exe16/user.c): the rectangle functions 667-745 follow it. */
#include <string.h>

#include "runtime/user_internal.h"

void (*user_beep_hook)(uint16_t type);

/* ---- cursors ---- */

/* LoadCursor, DestroyCursor and the cursor shapes are in user_cursor.c (task stars-0z4.9). */
#define IDC_ARROW 32512

HCURSOR16 SetCursor(HCURSOR16 hCursor) {
  UserState *st = user_state();
  HCURSOR16 prev;
  if (!st) return 0;
  prev = st->in.cursor;
  st->in.cursor = hCursor;
  return prev;
}

HCURSOR16 user_current_cursor(void) {
  UserState *st = user_state();
  return st ? st->in.cursor : 0;
}

void MessageBeep(uint16_t uType) {
  if (user_beep_hook) user_beep_hook(uType);
}

/* DefWindowProc(WM_SETCURSOR), dlls/win32u/defwnd.c 2690-2703 and handle_set_cursor 436-483:
 * a child window offers the message to its parent first (except for the sizing hit codes);
 * HTCLIENT sets the class cursor if there is one; the sizing codes set the matching sizing
 * cursor; HTERROR beeps on a button-down; everything else sets the arrow. DefWindowProc returns 0
 * (Wine ignores handle_set_cursor's result there). */
uint32_t user_def_setcursor(HWND16 h, uint16_t wp, uint32_t lp) {
  UserState *st = user_state();
  const UWnd *w = user_wnd(h);
  int16_t hit = (int16_t)(lp & 0xFFFF);
  uint16_t m = (uint16_t)(lp >> 16), id = IDC_ARROW;
  const UWnd *tw;
  if (!w) return 0;
  if (w->style & WS_CHILD) {
    if (hit < HTLEFT || hit > HTBOTTOMRIGHT) {
      HWND16 parent = w->parent;
      if (parent != user_desktop() && user_wnd(parent) && SendMessage(parent, WM_SETCURSOR, wp, lp)) return 1;
    }
  }
  tw = user_wnd(wp);
  switch (hit) {
    case HTERROR:
      if (m == WM_LBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_RBUTTONDOWN) MessageBeep(0);
      break;
    case HTCLIENT: {
      uint16_t c = tw ? st->c[tw->cls].hcursor : 0;
      if (!c) return 0;
      SetCursor(c);
      return 0;
    }
    case HTLEFT:
    case HTRIGHT: id = 32644; break;       /* IDC_SIZEWE */
    case HTTOP:
    case HTBOTTOM: id = 32645; break;      /* IDC_SIZENS */
    case HTTOPLEFT:
    case HTBOTTOMRIGHT: id = 32642; break; /* IDC_SIZENWSE */
    case HTTOPRIGHT:
    case HTBOTTOMLEFT: id = 32643; break;  /* IDC_SIZENESW */
    default: break;
  }
  SetCursor(LoadCursor(0, (FARPTR)id));
  return 0;
}

/* ---- application control ---- */

/* UNVERIFIED rule (no non-client drawing exists yet): the window's displayed "active" state is
 * (it is the active window) XOR (the flash bit); FlashWindow flips or clears the bit, sends
 * WM_NCACTIVATE with the new displayed state and returns the previous displayed state. */
uint16_t FlashWindow(HWND16 hwnd, uint16_t bInvert) {
  UWnd *w = user_wnd(hwnd);
  uint16_t prev, now;
  if (!w || hwnd == user_desktop()) return 0;
  prev = (uint16_t)(((GetActiveWindow() == hwnd) ? 1 : 0) ^ (w->pad & 1));
  if (bInvert) w->pad ^= 1;
  else w->pad &= (uint16_t)~1u;
  now = (uint16_t)(((GetActiveWindow() == hwnd) ? 1 : 0) ^ (w->pad & 1));
  SendMessage(hwnd, WM_NCACTIVATE, now, 0);
  return prev;
}

/* UNVERIFIED: "ends the program" (issue text). The real call first sends WM_QUERYENDSESSION and
 * WM_ENDSESSION to every top-level window; those are not generated. The host sees
 * user_exit_requested() and GetMessage returns WM_QUIT with the low word of the return code. */
uint16_t ExitWindows(uint32_t dwReturnCode, uint16_t wReserved) {
  UserState *st = user_state();
  (void)wReserved;
  if (!st) return 0;
  st->in.exit_requested = 1;
  PostQuitMessage((int16_t)(dwReturnCode & 0xFFFF));
  return 1;
}

bool user_exit_requested(void) {
  UserState *st = user_state();
  return st && st->in.exit_requested;
}

/* USER.5: Wine's InitApp16 (dlls/user.exe16/user.c 531-535) creates the task queue and returns
 * non-zero. The queue always exists here. */
uint16_t InitApp(HANDLE16 hInstance) {
  (void)hInstance;
  return 1;
}

/* ---- system metrics ---- */

/* UNVERIFIED (see the file comment). Index = SM_xxx of the Windows 3.1 SDK (0..44). */
int16_t GetSystemMetrics(int16_t nIndex) {
  switch (nIndex) {
    case 0: return GDI_SCREEN_W;            /* SM_CXSCREEN */
    case 1: return GDI_SCREEN_H;            /* SM_CYSCREEN */
    case 2: return 16;                      /* SM_CXVSCROLL */
    case 3: return 16;                      /* SM_CYHSCROLL */
    case 4: return 20;                      /* SM_CYCAPTION (caption plus border line) */
    case 5: return 1;                       /* SM_CXBORDER */
    case 6: return 1;                       /* SM_CYBORDER */
    case 7: return 4;                       /* SM_CXDLGFRAME */
    case 8: return 4;                       /* SM_CYDLGFRAME */
    case 9: return 16;                      /* SM_CYVTHUMB */
    case 10: return 16;                     /* SM_CXHTHUMB */
    case 11: return 32;                     /* SM_CXICON */
    case 12: return 32;                     /* SM_CYICON */
    case 13: return 32;                     /* SM_CXCURSOR */
    case 14: return 32;                     /* SM_CYCURSOR */
    case 15: return 19;                     /* SM_CYMENU */
    case 16: return GDI_SCREEN_W;           /* SM_CXFULLSCREEN */
    case 17: return GDI_SCREEN_H - 20;      /* SM_CYFULLSCREEN: screen minus SM_CYCAPTION */
    case 18: return 0;                      /* SM_CYKANJIWINDOW */
    case 19: return 1;                      /* SM_MOUSEPRESENT */
    case 20: return 16;                     /* SM_CYVSCROLL */
    case 21: return 16;                     /* SM_CXHSCROLL */
    case 22: return 0;                      /* SM_DEBUG */
    case 23: return 0;                      /* SM_SWAPBUTTON */
    case 24: case 25: case 26: case 27: return 0; /* SM_RESERVED1..4 */
    case 28: return 112;                    /* SM_CXMIN */
    case 29: return 27;                     /* SM_CYMIN */
    case 30: return 18;                     /* SM_CXSIZE */
    case 31: return 18;                     /* SM_CYSIZE */
    case 32: return 4;                      /* SM_CXFRAME */
    case 33: return 4;                      /* SM_CYFRAME */
    case 34: return 112;                    /* SM_CXMINTRACK */
    case 35: return 27;                     /* SM_CYMINTRACK */
    case 36: return 4;                      /* SM_CXDOUBLECLK */
    case 37: return 4;                      /* SM_CYDOUBLECLK */
    case 38: return 75;                     /* SM_CXICONSPACING */
    case 39: return 75;                     /* SM_CYICONSPACING */
    case 40: return 0;                      /* SM_MENUDROPALIGNMENT */
    case 41: return 0;                      /* SM_PENWINDOWS */
    case 42: return 0;                      /* SM_DBCSENABLED */
    case 43: return 2;                      /* SM_CMOUSEBUTTONS */
    case 44: return 0;                      /* SM_SECURE */
    default: return 0;
  }
}

/* ---- system colours ---- */

/* Windows 3.1 default scheme ("Windows Default"), COLOR_SCROLLBAR (0) .. COLOR_BTNHIGHLIGHT (20).
 * UNVERIFIED: remembered values, no SDK text or capture. */
static const COLORREF16 SYSCOLORS[21] = {
    GDI_RGB(192, 192, 192), /* 0 COLOR_SCROLLBAR */
    GDI_RGB(192, 192, 192), /* 1 COLOR_BACKGROUND */
    GDI_RGB(0, 0, 128),     /* 2 COLOR_ACTIVECAPTION */
    GDI_RGB(255, 255, 255), /* 3 COLOR_INACTIVECAPTION */
    GDI_RGB(255, 255, 255), /* 4 COLOR_MENU */
    GDI_RGB(255, 255, 255), /* 5 COLOR_WINDOW */
    GDI_RGB(0, 0, 0),       /* 6 COLOR_WINDOWFRAME */
    GDI_RGB(0, 0, 0),       /* 7 COLOR_MENUTEXT */
    GDI_RGB(0, 0, 0),       /* 8 COLOR_WINDOWTEXT */
    GDI_RGB(255, 255, 255), /* 9 COLOR_CAPTIONTEXT */
    GDI_RGB(192, 192, 192), /* 10 COLOR_ACTIVEBORDER */
    GDI_RGB(192, 192, 192), /* 11 COLOR_INACTIVEBORDER */
    GDI_RGB(128, 128, 128), /* 12 COLOR_APPWORKSPACE */
    GDI_RGB(0, 0, 128),     /* 13 COLOR_HIGHLIGHT */
    GDI_RGB(255, 255, 255), /* 14 COLOR_HIGHLIGHTTEXT */
    GDI_RGB(192, 192, 192), /* 15 COLOR_BTNFACE */
    GDI_RGB(128, 128, 128), /* 16 COLOR_BTNSHADOW */
    GDI_RGB(128, 128, 128), /* 17 COLOR_GRAYTEXT */
    GDI_RGB(0, 0, 0),       /* 18 COLOR_BTNTEXT */
    GDI_RGB(0, 0, 0),       /* 19 COLOR_INACTIVECAPTIONTEXT */
    GDI_RGB(255, 255, 255), /* 20 COLOR_BTNHIGHLIGHT */
};

uint32_t user_syscolor(unsigned idx) { return idx < 21 ? SYSCOLORS[idx] : 0; }

uint32_t GetSysColor(uint16_t nIndex) { return user_syscolor(nIndex); }

/* ---- rectangles (dlls/user.exe16/user.c 650-730) ---- */

static RECT16 *rp(FARPTR p) { return (p && mem_check(p, sizeof(RECT16))) ? (RECT16 *)LIN(p) : NULL; }

void SetRect(FARPTR lpRect, int16_t xLeft, int16_t yTop, int16_t xRight, int16_t yBottom) {
  RECT16 *r = rp(lpRect);
  if (!r) return;
  r->left = xLeft;
  r->top = yTop;
  r->right = xRight;
  r->bottom = yBottom;
}

void SetRectEmpty(FARPTR lpRect) { SetRect(lpRect, 0, 0, 0, 0); }

uint16_t CopyRect(FARPTR lpDest, FARPTR lpSrc) {
  RECT16 *d = rp(lpDest), *s = rp(lpSrc);
  if (!d || !s) return 0;
  *d = *s;
  return 1;
}

uint16_t IsRectEmpty(FARPTR lpRect) {
  RECT16 *r = rp(lpRect);
  return (!r || r->left >= r->right || r->top >= r->bottom) ? 1 : 0;
}

/* The right and bottom edges are outside (user.c 667-671). */
uint16_t PtInRect(FARPTR lpRect, uint32_t pt) {
  RECT16 *r = rp(lpRect);
  int16_t x = (int16_t)(pt & 0xFFFF), y = (int16_t)(pt >> 16);
  if (!r) return 0;
  return (x >= r->left && x < r->right && y >= r->top && y < r->bottom) ? 1 : 0;
}

void OffsetRect(FARPTR lpRect, int16_t dx, int16_t dy) {
  RECT16 *r = rp(lpRect);
  if (!r) return;
  r->left = (int16_t)(r->left + dx);
  r->right = (int16_t)(r->right + dx);
  r->top = (int16_t)(r->top + dy);
  r->bottom = (int16_t)(r->bottom + dy);
}

void InflateRect(FARPTR lpRect, int16_t dx, int16_t dy) {
  RECT16 *r = rp(lpRect);
  if (!r) return;
  r->left = (int16_t)(r->left - dx);
  r->top = (int16_t)(r->top - dy);
  r->right = (int16_t)(r->right + dx);
  r->bottom = (int16_t)(r->bottom + dy);
}

/* user.c 701-717: empty or non-overlapping sources give an empty destination and FALSE. */
uint16_t IntersectRect(FARPTR lpDest, FARPTR lpSrc1, FARPTR lpSrc2) {
  RECT16 *d = rp(lpDest), *a = rp(lpSrc1), *b = rp(lpSrc2);
  RECT16 t;
  if (!d || !a || !b) return 0;
  if (IsRectEmpty(lpSrc1) || IsRectEmpty(lpSrc2) || a->left >= b->right || b->left >= a->right || a->top >= b->bottom ||
      b->top >= a->bottom) {
    d->left = d->top = d->right = d->bottom = 0;
    return 0;
  }
  t.left = a->left > b->left ? a->left : b->left;
  t.right = a->right < b->right ? a->right : b->right;
  t.top = a->top > b->top ? a->top : b->top;
  t.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
  *d = t;
  return 1;
}

/* user.c 720-745 */
uint16_t UnionRect(FARPTR lpDest, FARPTR lpSrc1, FARPTR lpSrc2) {
  RECT16 *d = rp(lpDest), *a = rp(lpSrc1), *b = rp(lpSrc2);
  RECT16 t;
  if (!d || !a || !b) return 0;
  if (IsRectEmpty(lpSrc1)) {
    if (IsRectEmpty(lpSrc2)) {
      d->left = d->top = d->right = d->bottom = 0;
      return 0;
    }
    *d = *b;
    return 1;
  }
  if (IsRectEmpty(lpSrc2)) {
    *d = *a;
    return 1;
  }
  t.left = a->left < b->left ? a->left : b->left;
  t.right = a->right > b->right ? a->right : b->right;
  t.top = a->top < b->top ? a->top : b->top;
  t.bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
  *d = t;
  return 1;
}

uint16_t EqualRect(FARPTR lpRect1, FARPTR lpRect2) {
  RECT16 *a = rp(lpRect1), *b = rp(lpRect2);
  if (!a || !b) return 0;
  return (a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom) ? 1 : 0;
}

/* FillRect16 (user.c 756-768): select the brush, PatBlt PATCOPY, restore. The Win16 variant does
 * not take COLOR_xxx + 1 pseudo-brushes. */
int16_t FillRect(HDC16 hdc, FARPTR lpRect, HBRUSH16 hbr) {
  RECT16 *r = rp(lpRect);
  HGDIOBJ16 old;
  if (!r) return 0;
  old = SelectObject(hdc, hbr);
  if (!old) return 0;
  PatBlt(hdc, r->left, r->top, (int16_t)(r->right - r->left), (int16_t)(r->bottom - r->top), PATCOPY);
  SelectObject(hdc, old);
  return 1;
}

/* FrameRect16 (user.c 785-795) forwards to GDI. The SDK: a border one logical unit wide drawn
 * with the brush, inside the rectangle (right and bottom edges excluded). The edge pixels are
 * UNVERIFIED (no SDK text, no capture). */
int16_t FrameRect(HDC16 hdc, FARPTR lpRect, HBRUSH16 hbr) {
  RECT16 *r = rp(lpRect);
  HGDIOBJ16 old;
  int w, h;
  if (!r) return 0;
  w = r->right - r->left;
  h = r->bottom - r->top;
  if (w <= 0 || h <= 0) return 0;
  old = SelectObject(hdc, hbr);
  if (!old) return 0;
  PatBlt(hdc, r->left, r->top, 1, (int16_t)h, PATCOPY);                    /* left */
  PatBlt(hdc, (int16_t)(r->right - 1), r->top, 1, (int16_t)h, PATCOPY);    /* right */
  if (w > 2) {
    PatBlt(hdc, (int16_t)(r->left + 1), r->top, (int16_t)(w - 2), 1, PATCOPY);                    /* top */
    PatBlt(hdc, (int16_t)(r->left + 1), (int16_t)(r->bottom - 1), (int16_t)(w - 2), 1, PATCOPY);  /* bottom */
  }
  SelectObject(hdc, old);
  return 1;
}

uint16_t WinHelp(HWND16 hwnd, FARPTR lpHelpFile, uint16_t uCommand, uint32_t dwData) {
  (void)hwnd;
  (void)lpHelpFile;
  (void)uCommand;
  (void)dwData;
  return 1;
}
