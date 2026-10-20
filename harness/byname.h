/* By-name Win16 API calls with API-log entries (native builds only). stars-4a6.
 *
 * Decompiled code in src/ calls Windows APIs by name (docs/CONVENTIONS.md rule e). The harness API
 * log used by the diff tests is written at the import thunk, so a by-name call would be missing from
 * the DECOMP side of the log. This header renames each listed runtime function to a wrapper that
 * records the call the way the thunk does (module, ordinal, name, raw argument bytes, return value)
 * around the real call. The runtime itself is unchanged and does not include this header
 * (make check-split); src/stars.h includes it behind #ifndef __EMSCRIPTEN__, so the wasm build
 * calls the runtime directly.
 *
 * To log another API, add one wrapper below: list its argument words in PUSH order (the order the
 * original pushes them: first argument first, a 32-bit value as high word then low word), and put
 * the module name and ordinal from runtime/win16_api.inc. Define the #define after the wrapper. */
#ifndef STARS_HARNESS_BYNAME_H
#define STARS_HARNESS_BYNAME_H

#ifdef __EMSCRIPTEN__
#error "harness/byname.h is for native builds only"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "harness/harness.h"
#include "runtime/frame.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/commdlg.h"
#include "runtime/kernel_file.h"
#include "runtime/mem.h"
#include "runtime/text.h"
#include "runtime/toolhelp.h"
#include "runtime/user.h"

#define BYNAME_HI(v) ((uint16_t)((uint32_t)(v) >> 16))
#define BYNAME_LO(v) ((uint16_t)(v))

/* KERNEL global memory (runtime/mem.h). */
static inline uint16_t byname_GlobalAlloc(uint16_t flags, uint32_t bytes) {
  const uint16_t w[] = {flags, BYNAME_HI(bytes), BYNAME_LO(bytes)};
  size_t i = harness_log_begin("KERNEL", 15, w, 3);
  uint16_t r = (GlobalAlloc)(flags, bytes);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GlobalReAlloc(uint16_t h, uint32_t bytes, uint16_t flags) {
  const uint16_t w[] = {h, BYNAME_HI(bytes), BYNAME_LO(bytes), flags};
  size_t i = harness_log_begin("KERNEL", 16, w, 4);
  uint16_t r = (GlobalReAlloc)(h, bytes, flags);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GlobalFree(uint16_t h) {
  size_t i = harness_log_begin("KERNEL", 17, &h, 1);
  uint16_t r = (GlobalFree)(h);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_GlobalLock(uint16_t h) {
  size_t i = harness_log_begin("KERNEL", 18, &h, 1);
  uint32_t r = (GlobalLock)(h);
  harness_log_end(i, r);
  return r;
}
static inline bool byname_GlobalUnlock(uint16_t h) {
  size_t i = harness_log_begin("KERNEL", 19, &h, 1);
  bool r = (GlobalUnlock)(h);
  harness_log_end(i, r ? 1u : 0u); /* the thunk handler returns 1 or 0 */
  return r;
}
static inline uint32_t byname_GlobalSize(uint16_t h) {
  size_t i = harness_log_begin("KERNEL", 20, &h, 1);
  uint32_t r = (GlobalSize)(h);
  harness_log_end(i, r);
  return r;
}

/* GDI (runtime/gdi.h, runtime/text.h) and USER (runtime/user.h). */
static inline uint16_t byname_SelectObject(uint16_t hdc, uint16_t h) {
  const uint16_t w[] = {hdc, h};
  size_t i = harness_log_begin("GDI", 45, w, 2);
  uint16_t r = (SelectObject)(hdc, h);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_SetBkMode(uint16_t hdc, int16_t mode) {
  const uint16_t w[] = {hdc, (uint16_t)mode};
  size_t i = harness_log_begin("GDI", 2, w, 2);
  int16_t r = (SetBkMode)(hdc, mode);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint32_t byname_GetTextExtent(uint16_t hdc, uint32_t str, int16_t count) {
  const uint16_t w[] = {hdc, BYNAME_HI(str), BYNAME_LO(str), (uint16_t)count};
  size_t i = harness_log_begin("GDI", 91, w, 4);
  uint32_t r = (GetTextExtent)(hdc, str, count);
  harness_log_end(i, r);
  return r;
}
/* _wsprintf is VARARGS: the thunk logs the fixed part only, the two far pointers (8 bytes); the
 * variable arguments follow them in memory at lpArgs and are not part of the log. cdecl pushes
 * the format pointer first, then the output pointer. */
static inline int16_t byname__wsprintf(uint32_t out, uint32_t fmt, uint32_t args) {
  const uint16_t w[] = {BYNAME_HI(fmt), BYNAME_LO(fmt), BYNAME_HI(out), BYNAME_LO(out)};
  size_t i = harness_log_begin("USER", 420, w, 4);
  int16_t r = (_wsprintf)(out, fmt, args);
  harness_log_end(i, (uint16_t)r);
  return r;
}

/* Added by stars-bfj.5 (segment 3): the other APIs it calls by name. Each wrapper logs exactly what the
 * harness handler for the thunk logs: a void API leaves ret 0, a signed 16-bit result is logged as a
 * word. */
static inline uint32_t byname_MakeProcInstance(uint32_t proc, uint16_t hinst) {
  const uint16_t w[] = {BYNAME_HI(proc), BYNAME_LO(proc), hinst};
  size_t i = harness_log_begin("KERNEL", 51, w, 3);
  uint32_t r = (MakeProcInstance)(proc, hinst);
  harness_log_end(i, r);
  return r;
}
static inline void byname_FreeProcInstance(uint32_t proc) {
  const uint16_t w[] = {BYNAME_HI(proc), BYNAME_LO(proc)};
  size_t i = harness_log_begin("KERNEL", 52, w, 2);
  (FreeProcInstance)(proc);
  harness_log_end(i, 0);
}
static inline uint16_t byname_FreeResource(uint16_t h) {
  const uint16_t w[] = {h};
  size_t i = harness_log_begin("KERNEL", 63, w, 1);
  uint16_t r = (FreeResource)(h);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_SetBkColor(uint16_t hdc, uint32_t c) {
  const uint16_t w[] = {hdc, BYNAME_HI(c), BYNAME_LO(c)};
  size_t i = harness_log_begin("GDI", 1, w, 3);
  uint32_t r = (SetBkColor)(hdc, c);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_SetTextColor(uint16_t hdc, uint32_t c) {
  const uint16_t w[] = {hdc, BYNAME_HI(c), BYNAME_LO(c)};
  size_t i = harness_log_begin("GDI", 9, w, 3);
  uint32_t r = (SetTextColor)(hdc, c);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_IntersectClipRect(uint16_t hdc, int16_t left, int16_t top, int16_t right, int16_t bottom) {
  const uint16_t w[] = {hdc, (uint16_t)left, (uint16_t)top, (uint16_t)right, (uint16_t)bottom};
  size_t i = harness_log_begin("GDI", 22, w, 5);
  int16_t r = (IntersectClipRect)(hdc, left, top, right, bottom);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline int16_t byname_SelectClipRgn(uint16_t hdc, uint16_t rgn) {
  const uint16_t w[] = {hdc, rgn};
  size_t i = harness_log_begin("GDI", 44, w, 2);
  int16_t r = (SelectClipRgn)(hdc, rgn);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_DeleteObject(uint16_t h) {
  const uint16_t w[] = {h};
  size_t i = harness_log_begin("GDI", 69, w, 1);
  uint16_t r = (DeleteObject)(h);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetDeviceCaps(uint16_t hdc, int16_t index) {
  const uint16_t w[] = {hdc, (uint16_t)index};
  size_t i = harness_log_begin("GDI", 80, w, 2);
  int16_t r = (GetDeviceCaps)(hdc, index);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline void byname_GetCursorPos(uint32_t pt) {
  const uint16_t w[] = {BYNAME_HI(pt), BYNAME_LO(pt)};
  size_t i = harness_log_begin("USER", 17, w, 2);
  (GetCursorPos)(pt);
  harness_log_end(i, 0);
}
static inline uint16_t byname_SetTimer(uint16_t hwnd, uint16_t id, uint16_t elapse, uint32_t proc) {
  const uint16_t w[] = {hwnd, id, elapse, BYNAME_HI(proc), BYNAME_LO(proc)};
  size_t i = harness_log_begin("USER", 10, w, 5);
  uint16_t r = (SetTimer)(hwnd, id, elapse, proc);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_KillTimer(uint16_t hwnd, uint16_t id) {
  const uint16_t w[] = {hwnd, id};
  size_t i = harness_log_begin("USER", 12, w, 2);
  uint16_t r = (KillTimer)(hwnd, id);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_GetTickCount(void) {
  size_t i = harness_log_begin("USER", 13, NULL, 0);
  uint32_t r = (GetTickCount)();
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetFocus(void) {
  size_t i = harness_log_begin("USER", 23, NULL, 0);
  uint16_t r = (GetFocus)();
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_WindowFromPoint(uint32_t pt) {
  const uint16_t w[] = {BYNAME_HI(pt), BYNAME_LO(pt)};
  size_t i = harness_log_begin("USER", 30, w, 2);
  uint16_t r = (WindowFromPoint)(pt);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_IsIconic(uint16_t hwnd) {
  const uint16_t w[] = {hwnd};
  size_t i = harness_log_begin("USER", 31, w, 1);
  uint16_t r = (IsIconic)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline void byname_GetClientRect(uint16_t hwnd, uint32_t rect) {
  const uint16_t w[] = {hwnd, BYNAME_HI(rect), BYNAME_LO(rect)};
  size_t i = harness_log_begin("USER", 33, w, 3);
  (GetClientRect)(hwnd, rect);
  harness_log_end(i, 0);
}
static inline void byname_SetWindowText(uint16_t hwnd, uint32_t str) {
  const uint16_t w[] = {hwnd, BYNAME_HI(str), BYNAME_LO(str)};
  size_t i = harness_log_begin("USER", 37, w, 3);
  (SetWindowText)(hwnd, str);
  harness_log_end(i, 0);
}
static inline uint16_t byname_GetParent(uint16_t hwnd) {
  const uint16_t w[] = {hwnd};
  size_t i = harness_log_begin("USER", 46, w, 1);
  uint16_t r = (GetParent)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_DestroyWindow(uint16_t hwnd) {
  const uint16_t w[] = {hwnd};
  size_t i = harness_log_begin("USER", 53, w, 1);
  uint16_t r = (DestroyWindow)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetActiveWindow(void) {
  size_t i = harness_log_begin("USER", 60, NULL, 0);
  uint16_t r = (GetActiveWindow)();
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetDC(uint16_t hwnd) {
  const uint16_t w[] = {hwnd};
  size_t i = harness_log_begin("USER", 66, w, 1);
  uint16_t r = (GetDC)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_ReleaseDC(uint16_t hwnd, uint16_t hdc) {
  const uint16_t w[] = {hwnd, hdc};
  size_t i = harness_log_begin("USER", 68, w, 2);
  uint16_t r = (ReleaseDC)(hwnd, hdc);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_SetCursor(uint16_t h) {
  const uint16_t w[] = {h};
  size_t i = harness_log_begin("USER", 69, w, 1);
  uint16_t r = (SetCursor)(h);
  harness_log_end(i, r);
  return r;
}
static inline void byname_OffsetRect(uint32_t rect, int16_t dx, int16_t dy) {
  const uint16_t w[] = {BYNAME_HI(rect), BYNAME_LO(rect), (uint16_t)dx, (uint16_t)dy};
  size_t i = harness_log_begin("USER", 77, w, 4);
  (OffsetRect)(rect, dx, dy);
  harness_log_end(i, 0);
}
static inline int16_t byname_FillRect(uint16_t hdc, uint32_t rect, uint16_t hbr) {
  const uint16_t w[] = {hdc, BYNAME_HI(rect), BYNAME_LO(rect), hbr};
  size_t i = harness_log_begin("USER", 81, w, 4);
  int16_t r = (FillRect)(hdc, rect, hbr);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline int16_t byname_DialogBox(uint16_t hinst, uint32_t name, uint16_t hwnd, uint32_t proc) {
  const uint16_t w[] = {hinst, BYNAME_HI(name), BYNAME_LO(name), hwnd, BYNAME_HI(proc), BYNAME_LO(proc)};
  size_t i = harness_log_begin("USER", 87, w, 6);
  int16_t r = (DialogBox)(hinst, name, hwnd, proc);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_EndDialog(uint16_t hdlg, int16_t result) {
  const uint16_t w[] = {hdlg, (uint16_t)result};
  size_t i = harness_log_begin("USER", 88, w, 2);
  uint16_t r = (EndDialog)(hdlg, result);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetDlgItem(uint16_t hdlg, int16_t id) {
  const uint16_t w[] = {hdlg, (uint16_t)id};
  size_t i = harness_log_begin("USER", 91, w, 2);
  uint16_t r = (GetDlgItem)(hdlg, id);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetKeyState(int16_t vk) {
  const uint16_t w[] = {(uint16_t)vk};
  size_t i = harness_log_begin("USER", 106, w, 1);
  int16_t r = (GetKeyState)(vk);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_GetMessage(uint32_t msg, uint16_t hwnd, uint16_t min, uint16_t max) {
  const uint16_t w[] = {BYNAME_HI(msg), BYNAME_LO(msg), hwnd, min, max};
  size_t i = harness_log_begin("USER", 108, w, 5);
  uint16_t r = (GetMessage)(msg, hwnd, min, max);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_PostMessage(uint16_t hwnd, uint16_t msg, uint16_t wparam, uint32_t lparam) {
  const uint16_t w[] = {hwnd, msg, wparam, BYNAME_HI(lparam), BYNAME_LO(lparam)};
  size_t i = harness_log_begin("USER", 110, w, 5);
  uint16_t r = (PostMessage)(hwnd, msg, wparam, lparam);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_SendMessage(uint16_t hwnd, uint16_t msg, uint16_t wparam, uint32_t lparam) {
  const uint16_t w[] = {hwnd, msg, wparam, BYNAME_HI(lparam), BYNAME_LO(lparam)};
  size_t i = harness_log_begin("USER", 111, w, 5);
  uint32_t r = (SendMessage)(hwnd, msg, wparam, lparam);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_TranslateMessage(uint32_t msg) {
  const uint16_t w[] = {BYNAME_HI(msg), BYNAME_LO(msg)};
  size_t i = harness_log_begin("USER", 113, w, 2);
  uint16_t r = (TranslateMessage)(msg);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_DispatchMessage(uint32_t msg) {
  const uint16_t w[] = {BYNAME_HI(msg), BYNAME_LO(msg)};
  size_t i = harness_log_begin("USER", 114, w, 2);
  /* The import thunk is entered with the message pointer and the return address (8 bytes) on the
   * emulated stack; the window procedure is called below them. Lower rt_sp by 8 so a decompiled
   * caller's C frame gives the procedure the same stack layout (stars-n7l). */
  uint16_t sv_sp = rt_sp;
  if (rt_sp != 0) rt_sp = (uint16_t)(rt_sp - 8);
  uint32_t r = (DispatchMessage)(msg);
  rt_sp = sv_sp;
  harness_log_end(i, r);
  return r;
}
static inline void byname_InvalidateRect(uint16_t hwnd, uint32_t rect, uint16_t erase) {
  const uint16_t w[] = {hwnd, BYNAME_HI(rect), BYNAME_LO(rect), erase};
  size_t i = harness_log_begin("USER", 125, w, 4);
  (InvalidateRect)(hwnd, rect, erase);
  harness_log_end(i, 0);
}
static inline uint16_t byname_LoadCursor(uint16_t hinst, uint32_t name) {
  const uint16_t w[] = {hinst, BYNAME_HI(name), BYNAME_LO(name)};
  size_t i = harness_log_begin("USER", 173, w, 3);
  uint16_t r = (LoadCursor)(hinst, name);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_TranslateAccelerator(uint16_t hwnd, uint16_t haccel, uint32_t msg) {
  const uint16_t w[] = {hwnd, haccel, BYNAME_HI(msg), BYNAME_LO(msg)};
  size_t i = harness_log_begin("USER", 178, w, 4);
  int16_t r = (TranslateAccelerator)(hwnd, haccel, msg);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline int16_t byname_GetSystemMetrics(int16_t index) {
  const uint16_t w[] = {(uint16_t)index};
  size_t i = harness_log_begin("USER", 179, w, 1);
  int16_t r = (GetSystemMetrics)(index);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint32_t byname_GetSysColor(uint16_t index) {
  const uint16_t w[] = {index};
  size_t i = harness_log_begin("USER", 180, w, 1);
  uint32_t r = (GetSysColor)(index);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_DestroyIcon(uint16_t h) {
  const uint16_t w[] = {h};
  size_t i = harness_log_begin("USER", 457, w, 1);
  uint16_t r = (DestroyIcon)(h);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_DestroyCursor(uint16_t h) {
  const uint16_t w[] = {h};
  size_t i = harness_log_begin("USER", 458, w, 1);
  uint16_t r = (DestroyCursor)(h);
  harness_log_end(i, r);
  return r;
}

/* Added by stars-bfj.15 (segment 13): the other APIs it calls by name. Same convention as above:
 * words in push order, a void API leaves ret 0, a signed 16-bit result is logged as a word. */
static inline uint16_t byname_PatBlt(uint16_t hdc, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rop) {
  const uint16_t a[] = {hdc, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, BYNAME_HI(rop), BYNAME_LO(rop)};
  size_t i = harness_log_begin("GDI", 29, a, 7);
  uint16_t r = (PatBlt)(hdc, x, y, w, h, rop);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_ExtTextOut(uint16_t hdc, int16_t x, int16_t y, uint16_t options, uint32_t rect,
                                         uint32_t str, uint16_t count, uint32_t dx) {
  const uint16_t a[] = {hdc,           (uint16_t)x,    (uint16_t)y,   options,        BYNAME_HI(rect), BYNAME_LO(rect),
                        BYNAME_HI(str), BYNAME_LO(str), count,        BYNAME_HI(dx), BYNAME_LO(dx)};
  size_t i = harness_log_begin("GDI", 351, a, 11);
  uint16_t r = (ExtTextOut)(hdc, x, y, options, rect, str, count, dx);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_BeginPaint(uint16_t hwnd, uint32_t ps) {
  const uint16_t a[] = {hwnd, BYNAME_HI(ps), BYNAME_LO(ps)};
  size_t i = harness_log_begin("USER", 39, a, 3);
  uint16_t r = (BeginPaint)(hwnd, ps);
  harness_log_end(i, r);
  return r;
}
static inline void byname_EndPaint(uint16_t hwnd, uint32_t ps) {
  const uint16_t a[] = {hwnd, BYNAME_HI(ps), BYNAME_LO(ps)};
  size_t i = harness_log_begin("USER", 40, a, 3);
  (EndPaint)(hwnd, ps);
  harness_log_end(i, 0);
}
static inline uint32_t byname_CallWindowProc(uint32_t proc, uint16_t hwnd, uint16_t msg, uint16_t wparam,
                                             uint32_t lparam) {
  const uint16_t a[] = {BYNAME_HI(proc), BYNAME_LO(proc), hwnd, msg, wparam, BYNAME_HI(lparam), BYNAME_LO(lparam)};
  size_t i = harness_log_begin("USER", 122, a, 7);
  uint32_t r = (CallWindowProc)(proc, hwnd, msg, wparam, lparam);
  harness_log_end(i, r);
  return r;
}
static inline void byname_ClientToScreen(uint16_t hwnd, uint32_t pt) {
  const uint16_t a[] = {hwnd, BYNAME_HI(pt), BYNAME_LO(pt)};
  size_t i = harness_log_begin("USER", 28, a, 3);
  (ClientToScreen)(hwnd, pt);
  harness_log_end(i, 0);
}
static inline void byname_ScreenToClient(uint16_t hwnd, uint32_t pt) {
  const uint16_t a[] = {hwnd, BYNAME_HI(pt), BYNAME_LO(pt)};
  size_t i = harness_log_begin("USER", 29, a, 3);
  (ScreenToClient)(hwnd, pt);
  harness_log_end(i, 0);
}
static inline uint16_t byname_CreateWindow(uint32_t cls, uint32_t name, uint32_t style, int16_t x, int16_t y,
                                           int16_t cx, int16_t cy, uint16_t parent, uint16_t menu, uint16_t inst,
                                           uint32_t param) {
  const uint16_t a[] = {BYNAME_HI(cls),   BYNAME_LO(cls), BYNAME_HI(name), BYNAME_LO(name), BYNAME_HI(style),
                        BYNAME_LO(style), (uint16_t)x,    (uint16_t)y,     (uint16_t)cx,    (uint16_t)cy,
                        parent,           menu,           inst,            BYNAME_HI(param), BYNAME_LO(param)};
  size_t i = harness_log_begin("USER", 41, a, 15);
  uint16_t r = (CreateWindow)(cls, name, style, x, y, cx, cy, parent, menu, inst, param);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_DefWindowProc(uint16_t hwnd, uint16_t msg, uint16_t wparam, uint32_t lparam) {
  const uint16_t a[] = {hwnd, msg, wparam, BYNAME_HI(lparam), BYNAME_LO(lparam)};
  size_t i = harness_log_begin("USER", 107, a, 5);
  uint32_t r = (DefWindowProc)(hwnd, msg, wparam, lparam);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_EqualRect(uint32_t r1, uint32_t r2) {
  const uint16_t a[] = {BYNAME_HI(r1), BYNAME_LO(r1), BYNAME_HI(r2), BYNAME_LO(r2)};
  size_t i = harness_log_begin("USER", 244, a, 4);
  uint16_t r = (EqualRect)(r1, r2);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_FrameRect(uint16_t hdc, uint32_t rect, uint16_t hbr) {
  const uint16_t a[] = {hdc, BYNAME_HI(rect), BYNAME_LO(rect), hbr};
  size_t i = harness_log_begin("USER", 83, a, 4);
  int16_t r = (FrameRect)(hdc, rect, hbr);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_GetWindow(uint16_t hwnd, uint16_t cmd) {
  const uint16_t a[] = {hwnd, cmd};
  size_t i = harness_log_begin("USER", 262, a, 2);
  uint16_t r = (GetWindow)(hwnd, cmd);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_GetWindowLong(uint16_t hwnd, int16_t index) {
  const uint16_t a[] = {hwnd, (uint16_t)index};
  size_t i = harness_log_begin("USER", 135, a, 2);
  uint32_t r = (GetWindowLong)(hwnd, index);
  harness_log_end(i, r);
  return r;
}
static inline uint32_t byname_SetWindowLong(uint16_t hwnd, int16_t index, uint32_t value) {
  const uint16_t a[] = {hwnd, (uint16_t)index, BYNAME_HI(value), BYNAME_LO(value)};
  size_t i = harness_log_begin("USER", 136, a, 4);
  uint32_t r = (SetWindowLong)(hwnd, index, value);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetWindowText(uint16_t hwnd, uint32_t buf, int16_t max) {
  const uint16_t a[] = {hwnd, BYNAME_HI(buf), BYNAME_LO(buf), (uint16_t)max};
  size_t i = harness_log_begin("USER", 36, a, 4);
  int16_t r = (GetWindowText)(hwnd, buf, max);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_IsWindowVisible(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 49, a, 1);
  uint16_t r = (IsWindowVisible)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline void byname_MapWindowPoints(uint16_t from, uint16_t to, uint32_t pts, uint16_t count) {
  const uint16_t a[] = {from, to, BYNAME_HI(pts), BYNAME_LO(pts), count};
  size_t i = harness_log_begin("USER", 258, a, 5);
  (MapWindowPoints)(from, to, pts, count);
  harness_log_end(i, 0);
}
static inline void byname_MessageBeep(uint16_t type) {
  const uint16_t a[] = {type};
  size_t i = harness_log_begin("USER", 104, a, 1);
  (MessageBeep)(type);
  harness_log_end(i, 0);
}
static inline uint16_t byname_PtInRect(uint32_t rect, uint32_t pt) {
  const uint16_t a[] = {BYNAME_HI(rect), BYNAME_LO(rect), BYNAME_HI(pt), BYNAME_LO(pt)};
  size_t i = harness_log_begin("USER", 76, a, 4);
  uint16_t r = (PtInRect)(rect, pt);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_RealizePalette(uint16_t hdc) {
  const uint16_t a[] = {hdc};
  size_t i = harness_log_begin("USER", 283, a, 1);
  uint16_t r = (RealizePalette)(hdc);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_SelectPalette(uint16_t hdc, uint16_t hpal, uint16_t background) {
  const uint16_t a[] = {hdc, hpal, background};
  size_t i = harness_log_begin("USER", 282, a, 3);
  uint16_t r = (SelectPalette)(hdc, hpal, background);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_SetCapture(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 18, a, 1);
  uint16_t r = (SetCapture)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline void byname_ReleaseCapture(void) {
  size_t i = harness_log_begin("USER", 19, NULL, 0);
  (ReleaseCapture)();
  harness_log_end(i, 0);
}
static inline uint16_t byname_SetFocus(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 22, a, 1);
  uint16_t r = (SetFocus)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_SetWindowPos(uint16_t hwnd, uint16_t after, int16_t x, int16_t y, int16_t cx,
                                           int16_t cy, uint16_t flags) {
  const uint16_t a[] = {hwnd, after, (uint16_t)x, (uint16_t)y, (uint16_t)cx, (uint16_t)cy, flags};
  size_t i = harness_log_begin("USER", 232, a, 7);
  uint16_t r = (SetWindowPos)(hwnd, after, x, y, cx, cy, flags);
  harness_log_end(i, r);
  return r;
}
static inline void byname_UpdateWindow(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 124, a, 1);
  (UpdateWindow)(hwnd);
  harness_log_end(i, 0);
}


/* Added by stars-bfj.18 (segment 16): the dialog procedures' other APIs. Same rules as above. */
static inline uint16_t byname_TextOut(uint16_t hdc, int16_t x, int16_t y, uint32_t str, int16_t count) {
  const uint16_t a[] = {hdc, (uint16_t)x, (uint16_t)y, BYNAME_HI(str), BYNAME_LO(str), (uint16_t)count};
  size_t i = harness_log_begin("GDI", 33, a, 6);
  uint16_t r = (TextOut)(hdc, x, y, str, count);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CheckRadioButton(uint16_t dlg, int16_t first, int16_t last, int16_t check) {
  const uint16_t a[] = {dlg, (uint16_t)first, (uint16_t)last, (uint16_t)check};
  size_t i = harness_log_begin("USER", 96, a, 4);
  uint16_t r = (CheckRadioButton)(dlg, first, last, check);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_EnableWindow(uint16_t hwnd, uint16_t enable) {
  const uint16_t a[] = {hwnd, enable};
  size_t i = harness_log_begin("USER", 34, a, 2);
  uint16_t r = (EnableWindow)(hwnd, enable);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetDlgItemText(uint16_t dlg, int16_t id, uint32_t buf, int16_t max) {
  const uint16_t a[] = {dlg, (uint16_t)id, BYNAME_HI(buf), BYNAME_LO(buf), (uint16_t)max};
  size_t i = harness_log_begin("USER", 93, a, 5);
  int16_t r = (GetDlgItemText)(dlg, id, buf, max);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline void byname_GetWindowRect(uint16_t hwnd, uint32_t rect) {
  const uint16_t a[] = {hwnd, BYNAME_HI(rect), BYNAME_LO(rect)};
  size_t i = harness_log_begin("USER", 32, a, 3);
  (GetWindowRect)(hwnd, rect);
  harness_log_end(i, 0);
}
static inline uint32_t byname_SendDlgItemMessage(uint16_t dlg, int16_t id, uint16_t msg, uint16_t wparam,
                                                 uint32_t lparam) {
  const uint16_t a[] = {dlg, (uint16_t)id, msg, wparam, BYNAME_HI(lparam), BYNAME_LO(lparam)};
  size_t i = harness_log_begin("USER", 101, a, 6);
  uint32_t r = (SendDlgItemMessage)(dlg, id, msg, wparam, lparam);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_ShowWindow(uint16_t hwnd, int16_t cmd) {
  const uint16_t a[] = {hwnd, (uint16_t)cmd};
  size_t i = harness_log_begin("USER", 42, a, 2);
  uint16_t r = (ShowWindow)(hwnd, cmd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_WinHelp(uint16_t hwnd, uint32_t file, uint16_t cmd, uint32_t data) {
  const uint16_t a[] = {hwnd, BYNAME_HI(file), BYNAME_LO(file), cmd, BYNAME_HI(data), BYNAME_LO(data)};
  size_t i = harness_log_begin("USER", 171, a, 6);
  uint16_t r = (WinHelp)(hwnd, file, cmd, data);
  harness_log_end(i, r);
  return r;
}


/* Added by stars-bfj.7 (segment 5): the drawing and rectangle APIs it calls by name. Same convention:
 * words in push order, a void API leaves ret 0, a signed 16-bit result is logged as a word. */
static inline uint16_t byname_BitBlt(uint16_t hdc, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t src,
                                     int16_t xs, int16_t ys, uint32_t rop) {
  const uint16_t a[] = {hdc, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, src, (uint16_t)xs, (uint16_t)ys,
                        BYNAME_HI(rop), BYNAME_LO(rop)};
  size_t i = harness_log_begin("GDI", 34, a, 10);
  uint16_t r = (BitBlt)(hdc, x, y, w, h, src, xs, ys, rop);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreateCompatibleDC(uint16_t hdc) {
  size_t i = harness_log_begin("GDI", 52, &hdc, 1);
  uint16_t r = (CreateCompatibleDC)(hdc);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_DeleteDC(uint16_t hdc) {
  size_t i = harness_log_begin("GDI", 68, &hdc, 1);
  uint16_t r = (DeleteDC)(hdc);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_DrawText(uint16_t hdc, uint32_t str, int16_t count, uint32_t rect, uint16_t flags) {
  const uint16_t a[] = {hdc, BYNAME_HI(str), BYNAME_LO(str), (uint16_t)count, BYNAME_HI(rect), BYNAME_LO(rect), flags};
  size_t i = harness_log_begin("USER", 85, a, 7);
  int16_t r = (DrawText)(hdc, str, count, rect, flags);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline void byname_SetRect(uint32_t rect, int16_t l, int16_t t, int16_t r, int16_t b) {
  const uint16_t a[] = {BYNAME_HI(rect), BYNAME_LO(rect), (uint16_t)l, (uint16_t)t, (uint16_t)r, (uint16_t)b};
  size_t i = harness_log_begin("USER", 72, a, 6);
  (SetRect)(rect, l, t, r, b);
  harness_log_end(i, 0);
}

#define GlobalAlloc byname_GlobalAlloc
#define GlobalReAlloc byname_GlobalReAlloc
#define GlobalFree byname_GlobalFree
#define GlobalLock byname_GlobalLock
#define GlobalUnlock byname_GlobalUnlock
#define GlobalSize byname_GlobalSize
#define SelectObject byname_SelectObject
#define SetBkMode byname_SetBkMode
#define GetTextExtent byname_GetTextExtent
#define _wsprintf byname__wsprintf
#define MakeProcInstance byname_MakeProcInstance
#define FreeProcInstance byname_FreeProcInstance
#define FreeResource byname_FreeResource
#define SetBkColor byname_SetBkColor
#define SetTextColor byname_SetTextColor
#define IntersectClipRect byname_IntersectClipRect
#define SelectClipRgn byname_SelectClipRgn
#define DeleteObject byname_DeleteObject
#define GetDeviceCaps byname_GetDeviceCaps
#define GetCursorPos byname_GetCursorPos
#define SetTimer byname_SetTimer
#define KillTimer byname_KillTimer
#define GetTickCount byname_GetTickCount
#define GetFocus byname_GetFocus
#define WindowFromPoint byname_WindowFromPoint
#define IsIconic byname_IsIconic
#define GetClientRect byname_GetClientRect
#define SetWindowText byname_SetWindowText
#define GetParent byname_GetParent
#define DestroyWindow byname_DestroyWindow
#define GetActiveWindow byname_GetActiveWindow
#define GetDC byname_GetDC
#define ReleaseDC byname_ReleaseDC
#define SetCursor byname_SetCursor
#define OffsetRect byname_OffsetRect
#define FillRect byname_FillRect
#define DialogBox byname_DialogBox
#define EndDialog byname_EndDialog
#define GetDlgItem byname_GetDlgItem
#define GetKeyState byname_GetKeyState
#define GetMessage byname_GetMessage
#define PostMessage byname_PostMessage
#define SendMessage byname_SendMessage
#define TranslateMessage byname_TranslateMessage
#define DispatchMessage byname_DispatchMessage
#define InvalidateRect byname_InvalidateRect
#define LoadCursor byname_LoadCursor
#define TranslateAccelerator byname_TranslateAccelerator
#define GetSystemMetrics byname_GetSystemMetrics
#define GetSysColor byname_GetSysColor
#define DestroyIcon byname_DestroyIcon
#define DestroyCursor byname_DestroyCursor
#define PatBlt byname_PatBlt
#define ExtTextOut byname_ExtTextOut
#define BeginPaint byname_BeginPaint
#define EndPaint byname_EndPaint
#define CallWindowProc byname_CallWindowProc
#define ClientToScreen byname_ClientToScreen
#define ScreenToClient byname_ScreenToClient
#define CreateWindow byname_CreateWindow
#define DefWindowProc byname_DefWindowProc
#define EqualRect byname_EqualRect
#define FrameRect byname_FrameRect
#define GetWindow byname_GetWindow
#define GetWindowLong byname_GetWindowLong
#define SetWindowLong byname_SetWindowLong
#define GetWindowText byname_GetWindowText
#define IsWindowVisible byname_IsWindowVisible
#define MapWindowPoints byname_MapWindowPoints
#define MessageBeep byname_MessageBeep
#define PtInRect byname_PtInRect
#define RealizePalette byname_RealizePalette
#define SelectPalette byname_SelectPalette
#define SetCapture byname_SetCapture
#define ReleaseCapture byname_ReleaseCapture
#define SetFocus byname_SetFocus
#define SetWindowPos byname_SetWindowPos
#define UpdateWindow byname_UpdateWindow
#define TextOut byname_TextOut
#define CheckRadioButton byname_CheckRadioButton
#define EnableWindow byname_EnableWindow
#define GetDlgItemText byname_GetDlgItemText
#define GetWindowRect byname_GetWindowRect
#define SendDlgItemMessage byname_SendDlgItemMessage
#define ShowWindow byname_ShowWindow
#define WinHelp byname_WinHelp

/* Added by stars-bfj.3 (segment 1): start-up, fonts, the main window and the ini file. Same rules as
 * above. LocalAlloc is logged as the import thunk does and calls the same function (the thunk goes
 * through kernel_LocalAlloc, which only strips the no-op LMEM_NOCOMPACT / LMEM_NODISCARD flags). */
static inline uint16_t byname_LocalAlloc(uint16_t flags, uint16_t bytes) {
  const uint16_t a[] = {flags, bytes};
  size_t i = harness_log_begin("KERNEL", 5, a, 2);
  uint16_t r = kernel_LocalAlloc(flags, bytes);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_LocalFree(uint16_t h) {
  const uint16_t a[] = {h};
  size_t i = harness_log_begin("KERNEL", 7, a, 1);
  uint16_t r = (LocalFree)(h);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetPrivateProfileInt(uint32_t app, uint32_t key, int16_t def, uint32_t file) {
  const uint16_t a[] = {BYNAME_HI(app), BYNAME_LO(app), BYNAME_HI(key), BYNAME_LO(key), (uint16_t)def, BYNAME_HI(file), BYNAME_LO(file)};
  size_t i = harness_log_begin("KERNEL", 127, a, 7);
  uint16_t r = (GetPrivateProfileInt)(app, key, def, file);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetPrivateProfileString(uint32_t app, uint32_t key, uint32_t def, uint32_t ret,
                                                      uint16_t size, uint32_t file) {
  const uint16_t a[] = {BYNAME_HI(app), BYNAME_LO(app), BYNAME_HI(key), BYNAME_LO(key), BYNAME_HI(def), BYNAME_LO(def),
                        BYNAME_HI(ret), BYNAME_LO(ret), size, BYNAME_HI(file), BYNAME_LO(file)};
  size_t i = harness_log_begin("KERNEL", 128, a, 11);
  uint16_t r = (GetPrivateProfileString)(app, key, def, ret, size, file);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreateFontIndirect(uint32_t lplf) {
  const uint16_t a[] = {BYNAME_HI(lplf), BYNAME_LO(lplf)};
  size_t i = harness_log_begin("GDI", 57, a, 2);
  uint16_t r = (CreateFontIndirect)(lplf);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreatePatternBrush(uint16_t hbm) {
  const uint16_t a[] = {hbm};
  size_t i = harness_log_begin("GDI", 60, a, 1);
  uint16_t r = (CreatePatternBrush)(hbm);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreatePen(int16_t style, int16_t width, uint32_t color) {
  const uint16_t a[] = {(uint16_t)style, (uint16_t)width, BYNAME_HI(color), BYNAME_LO(color)};
  size_t i = harness_log_begin("GDI", 61, a, 4);
  uint16_t r = (CreatePen)(style, width, color);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreateRectRgn(int16_t l, int16_t t, int16_t r_, int16_t b) {
  const uint16_t a[] = {(uint16_t)l, (uint16_t)t, (uint16_t)r_, (uint16_t)b};
  size_t i = harness_log_begin("GDI", 64, a, 4);
  uint16_t r = (CreateRectRgn)(l, t, r_, b);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetTextMetrics(uint16_t hdc, uint32_t lptm) {
  const uint16_t a[] = {hdc, BYNAME_HI(lptm), BYNAME_LO(lptm)};
  size_t i = harness_log_begin("GDI", 93, a, 3);
  uint16_t r = (GetTextMetrics)(hdc, lptm);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_MulDiv(int16_t a_, int16_t b_, int16_t c_) {
  const uint16_t a[] = {(uint16_t)a_, (uint16_t)b_, (uint16_t)c_};
  size_t i = harness_log_begin("GDI", 128, a, 3);
  int16_t r = (MulDiv)(a_, b_, c_);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_LoadIcon(uint16_t hinst, uint32_t name) {
  const uint16_t a[] = {hinst, BYNAME_HI(name), BYNAME_LO(name)};
  size_t i = harness_log_begin("USER", 174, a, 3);
  uint16_t r = (LoadIcon)(hinst, name);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_LoadBitmap(uint16_t hinst, uint32_t name) {
  const uint16_t a[] = {hinst, BYNAME_HI(name), BYNAME_LO(name)};
  size_t i = harness_log_begin("USER", 175, a, 3);
  uint16_t r = (LoadBitmap)(hinst, name);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_LoadAccelerators(uint16_t hinst, uint32_t name) {
  const uint16_t a[] = {hinst, BYNAME_HI(name), BYNAME_LO(name)};
  size_t i = harness_log_begin("USER", 177, a, 3);
  uint16_t r = (LoadAccelerators)(hinst, name);
  harness_log_end(i, r);
  return r;
}
#define LocalAlloc byname_LocalAlloc
#define LocalFree byname_LocalFree
#define GetPrivateProfileInt byname_GetPrivateProfileInt
#define GetPrivateProfileString byname_GetPrivateProfileString
#define CreateFontIndirect byname_CreateFontIndirect
#define CreatePatternBrush byname_CreatePatternBrush
#define CreatePen byname_CreatePen
#define CreateRectRgn byname_CreateRectRgn
#define GetTextMetrics byname_GetTextMetrics
#define MulDiv byname_MulDiv
#define LoadIcon byname_LoadIcon
#define LoadBitmap byname_LoadBitmap
#define LoadAccelerators byname_LoadAccelerators
#define BitBlt byname_BitBlt
#define CreateCompatibleDC byname_CreateCompatibleDC
#define DeleteDC byname_DeleteDC
#define DrawText byname_DrawText
#define SetRect byname_SetRect

/* Added by stars-bfj.6 (segment 4): the menu, placement and other APIs it calls by name. Same rules as
 * above: argument words in push order (taken from harness/user_thunks.c and tools/winespec), a void API
 * leaves ret 0, a signed 16-bit result is logged as a word. */
static inline uint16_t byname_IsZoomed(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 272, a, 1);
  uint16_t r = (IsZoomed)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetMenu(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 157, a, 1);
  uint16_t r = (GetMenu)(hwnd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetSubMenu(uint16_t hmenu, int16_t pos) {
  const uint16_t a[] = {hmenu, (uint16_t)pos};
  size_t i = harness_log_begin("USER", 159, a, 2);
  uint16_t r = (GetSubMenu)(hmenu, pos);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetWindowPlacement(uint16_t hwnd, uint32_t wp) {
  const uint16_t a[] = {hwnd, BYNAME_HI(wp), BYNAME_LO(wp)};
  size_t i = harness_log_begin("USER", 370, a, 3);
  uint16_t r = (GetWindowPlacement)(hwnd, wp);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetAsyncKeyState(int16_t vk) {
  const uint16_t a[] = {(uint16_t)vk};
  size_t i = harness_log_begin("USER", 249, a, 1);
  int16_t r = (GetAsyncKeyState)(vk);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_FlashWindow(uint16_t hwnd, uint16_t invert) {
  const uint16_t a[] = {hwnd, invert};
  size_t i = harness_log_begin("USER", 105, a, 2);
  uint16_t r = (FlashWindow)(hwnd, invert);
  harness_log_end(i, r);
  return r;
}
static inline void byname_PostQuitMessage(int16_t code) {
  const uint16_t a[] = {(uint16_t)code};
  size_t i = harness_log_begin("USER", 6, a, 1);
  (PostQuitMessage)(code);
  harness_log_end(i, 0);
}
static inline uint16_t byname_GetOpenFileName(uint32_t ofn) {
  const uint16_t a[] = {BYNAME_HI(ofn), BYNAME_LO(ofn)};
  size_t i = harness_log_begin("COMMDLG", 1, a, 2);
  uint16_t r = (GetOpenFileName)(ofn);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_MoveWindow(uint16_t hwnd, int16_t x, int16_t y, int16_t cx, int16_t cy, uint16_t repaint) {
  const uint16_t a[] = {hwnd, (uint16_t)x, (uint16_t)y, (uint16_t)cx, (uint16_t)cy, repaint};
  size_t i = harness_log_begin("USER", 56, a, 6);
  uint16_t r = (MoveWindow)(hwnd, x, y, cx, cy, repaint);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CheckMenuItem(uint16_t hmenu, uint16_t id, uint16_t check) {
  const uint16_t a[] = {hmenu, id, check};
  size_t i = harness_log_begin("USER", 154, a, 3);
  uint16_t r = (CheckMenuItem)(hmenu, id, check);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreatePopupMenu(void) {
  size_t i = harness_log_begin("USER", 415, NULL, 0);
  uint16_t r = (CreatePopupMenu)();
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_AppendMenu(uint16_t hmenu, uint16_t flags, uint16_t id, uint32_t item) {
  const uint16_t a[] = {hmenu, flags, id, BYNAME_HI(item), BYNAME_LO(item)};
  size_t i = harness_log_begin("USER", 411, a, 5);
  uint16_t r = (AppendMenu)(hmenu, flags, id, item);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_TrackPopupMenu(uint16_t hmenu, uint16_t flags, int16_t x, int16_t y, int16_t reserved,
                                             uint16_t hwnd, uint32_t rect) {
  const uint16_t a[] = {hmenu, flags, (uint16_t)x, (uint16_t)y, (uint16_t)reserved, hwnd, BYNAME_HI(rect), BYNAME_LO(rect)};
  size_t i = harness_log_begin("USER", 416, a, 8);
  uint16_t r = (TrackPopupMenu)(hmenu, flags, x, y, reserved, hwnd, rect);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_DestroyMenu(uint16_t hmenu) {
  const uint16_t a[] = {hmenu};
  size_t i = harness_log_begin("USER", 152, a, 1);
  uint16_t r = (DestroyMenu)(hmenu);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_PeekMessage(uint32_t msg, uint16_t hwnd, uint16_t min, uint16_t max, uint16_t flags) {
  const uint16_t a[] = {BYNAME_HI(msg), BYNAME_LO(msg), hwnd, min, max, flags};
  size_t i = harness_log_begin("USER", 109, a, 6);
  uint16_t r = (PeekMessage)(msg, hwnd, min, max, flags);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_MessageBox(uint16_t hwnd, uint32_t text, uint32_t caption, uint16_t type) {
  const uint16_t a[] = {hwnd, BYNAME_HI(text), BYNAME_LO(text), BYNAME_HI(caption), BYNAME_LO(caption), type};
  size_t i = harness_log_begin("USER", 1, a, 6);
  int16_t r = (MessageBox)(hwnd, text, caption, type);
  harness_log_end(i, (uint16_t)r);
  return r;
}
static inline uint16_t byname_RegisterClass(uint32_t wc) {
  const uint16_t a[] = {BYNAME_HI(wc), BYNAME_LO(wc)};
  size_t i = harness_log_begin("USER", 57, a, 2);
  uint16_t r = (RegisterClass)(wc);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_GetStockObject(int16_t id) {
  const uint16_t a[] = {(uint16_t)id};
  size_t i = harness_log_begin("GDI", 87, a, 1);
  uint16_t r = (GetStockObject)(id);
  harness_log_end(i, r);
  return r;
}
#define IsZoomed byname_IsZoomed
#define MoveWindow byname_MoveWindow
#define CheckMenuItem byname_CheckMenuItem
#define CreatePopupMenu byname_CreatePopupMenu
#define AppendMenu byname_AppendMenu
#define TrackPopupMenu byname_TrackPopupMenu
#define DestroyMenu byname_DestroyMenu
#define PeekMessage byname_PeekMessage
#define MessageBox byname_MessageBox
#define RegisterClass byname_RegisterClass
#define GetStockObject byname_GetStockObject
#define GetAsyncKeyState byname_GetAsyncKeyState
#define FlashWindow byname_FlashWindow
#define PostQuitMessage byname_PostQuitMessage
#define GetOpenFileName byname_GetOpenFileName
#define GetMenu byname_GetMenu
#define GetSubMenu byname_GetSubMenu
#define GetWindowPlacement byname_GetWindowPlacement
static inline uint16_t byname_DrawIcon(uint16_t hdc, int16_t x, int16_t y, uint16_t hicon) {
  const uint16_t a[] = {hdc, (uint16_t)x, (uint16_t)y, hicon};
  size_t i = harness_log_begin("USER", 84, a, 4);
  uint16_t r = (DrawIcon)(hdc, x, y, hicon);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_ExitWindows(uint32_t code, uint16_t reserved) {
  const uint16_t a[] = {BYNAME_HI(code), BYNAME_LO(code), reserved};
  size_t i = harness_log_begin("USER", 7, a, 3);
  uint16_t r = (ExitWindows)(code, reserved);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_ExcludeClipRect(uint16_t hdc, int16_t l, int16_t t, int16_t r_, int16_t b) {
  const uint16_t a[] = {hdc, (uint16_t)l, (uint16_t)t, (uint16_t)r_, (uint16_t)b};
  size_t i = harness_log_begin("GDI", 21, a, 5);
  int16_t r = (ExcludeClipRect)(hdc, l, t, r_, b);
  harness_log_end(i, (uint16_t)r);
  return r;
}
#define DrawIcon byname_DrawIcon
#define ExitWindows byname_ExitWindows
#define ExcludeClipRect byname_ExcludeClipRect

static inline uint16_t byname_Rectangle(uint16_t hdc, int16_t l, int16_t t, int16_t r_, int16_t b) {
  const uint16_t a[] = {hdc, (uint16_t)l, (uint16_t)t, (uint16_t)r_, (uint16_t)b};
  size_t i = harness_log_begin("GDI", 27, a, 5);
  uint16_t r = (Rectangle)(hdc, l, t, r_, b);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_Escape(uint16_t hdc, int16_t code, int16_t cb, uint32_t in, uint32_t out) {
  const uint16_t a[] = {hdc, (uint16_t)code, (uint16_t)cb, BYNAME_HI(in), BYNAME_LO(in), BYNAME_HI(out), BYNAME_LO(out)};
  size_t i = harness_log_begin("GDI", 38, a, 7);
  uint16_t r = (uint16_t)(Escape)(hdc, code, cb, in, out);
  harness_log_end(i, r);
  return r;
}
/* Test hook: when set, PrintDlg is answered by it instead of the runtime (which always answers FALSE: printing is not
 * implemented). The scenario sets the same function as the handler of COMMDLG.20 for the original code. */
extern uint16_t (*byname_printdlg_hook)(uint32_t lppd);
static inline uint16_t byname_PrintDlg(uint32_t pd) {
  const uint16_t a[] = {BYNAME_HI(pd), BYNAME_LO(pd)};
  size_t i = harness_log_begin("COMMDLG", 20, a, 2);
  uint16_t r = byname_printdlg_hook ? byname_printdlg_hook(pd) : (PrintDlg)(pd);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_TimerCount(uint32_t ti) {
  const uint16_t a[] = {BYNAME_HI(ti), BYNAME_LO(ti)};
  size_t i = harness_log_begin("TOOLHELP", 80, a, 2);
  uint16_t r = (TimerCount)(ti);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_CreateDialog(uint16_t hinst, uint32_t name, uint16_t parent, uint32_t proc) {
  const uint16_t a[] = {hinst, BYNAME_HI(name), BYNAME_LO(name), parent, BYNAME_HI(proc), BYNAME_LO(proc)};
  size_t i = harness_log_begin("USER", 89, a, 6);
  uint16_t r = (CreateDialog)(hinst, name, parent, proc);
  harness_log_end(i, r);
  return r;
}
static inline void byname_DrawMenuBar(uint16_t hwnd) {
  const uint16_t a[] = {hwnd};
  size_t i = harness_log_begin("USER", 160, a, 1);
  (DrawMenuBar)(hwnd);
  harness_log_end(i, 0);
}
#define Rectangle byname_Rectangle
#define Escape byname_Escape
#define PrintDlg byname_PrintDlg
#define TimerCount byname_TimerCount
#define CreateDialog byname_CreateDialog
#define DrawMenuBar byname_DrawMenuBar

static inline uint16_t byname_DeleteMenu(uint16_t hmenu, uint16_t pos, uint16_t flags) {
  const uint16_t a[] = {hmenu, pos, flags};
  size_t i = harness_log_begin("USER", 413, a, 3);
  uint16_t r = (DeleteMenu)(hmenu, pos, flags);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_InsertMenu(uint16_t hmenu, uint16_t pos, uint16_t flags, uint16_t id, uint32_t item) {
  const uint16_t a[] = {hmenu, pos, flags, id, BYNAME_HI(item), BYNAME_LO(item)};
  size_t i = harness_log_begin("USER", 410, a, 6);
  uint16_t r = (InsertMenu)(hmenu, pos, flags, id, item);
  harness_log_end(i, r);
  return r;
}
static inline uint16_t byname_EnableMenuItem(uint16_t hmenu, uint16_t id, uint16_t enable) {
  const uint16_t a[] = {hmenu, id, enable};
  size_t i = harness_log_begin("USER", 155, a, 3);
  uint16_t r = (EnableMenuItem)(hmenu, id, enable);
  harness_log_end(i, r);
  return r;
}
static inline int16_t byname_GetMenuItemCount(uint16_t hmenu) {
  const uint16_t a[] = {hmenu};
  size_t i = harness_log_begin("USER", 263, a, 1);
  int16_t r = (GetMenuItemCount)(hmenu);
  harness_log_end(i, (uint16_t)r);
  return r;
}
#define DeleteMenu byname_DeleteMenu
#define InsertMenu byname_InsertMenu
#define EnableMenuItem byname_EnableMenuItem
#define GetMenuItemCount byname_GetMenuItemCount

static inline uint16_t byname_WritePrivateProfileString(uint32_t app, uint32_t key, uint32_t str, uint32_t file) {
  const uint16_t a[] = {BYNAME_HI(app), BYNAME_LO(app), BYNAME_HI(key), BYNAME_LO(key),
                        BYNAME_HI(str), BYNAME_LO(str), BYNAME_HI(file), BYNAME_LO(file)};
  size_t i = harness_log_begin("KERNEL", 129, a, 8);
  uint16_t r = (WritePrivateProfileString)(app, key, str, file);
  harness_log_end(i, r);
  return r;
}
#define WritePrivateProfileString byname_WritePrivateProfileString

#endif /* STARS_HARNESS_BYNAME_H */
