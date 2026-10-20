/* Tests for by-name API call logging (harness/byname.h). Task stars-4a6.
 * A by-name call must leave the same API log entry as the same call through the import thunk. */
#include <stdint.h>
#include <string.h>

#include "harness/byname.h"
#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "test.h"

#define EXE "original/stars26i.exe"

static uint32_t g_th[6]; /* thunks for KERNEL 15..20 */

static void setup(void) {
  static const struct { uint16_t ord; const char *name; uint16_t argbytes; int ret; } k[6] = {
      {15, "GlobalAlloc", 6, HR_RET16}, {16, "GlobalReAlloc", 8, HR_RET16},
      {17, "GlobalFree", 2, HR_RET16},  {18, "GlobalLock", 2, HR_RET32},
      {19, "GlobalUnlock", 2, HR_RET16}, {20, "GlobalSize", 2, HR_RET32}};
  CHECK_EQ(harness_load(EXE), 0); /* harness_call needs a loaded exe */
  kernel_register();
  CHECK_EQ(gdi_init(), 0);
  CHECK_EQ(user_init(), 0);
  gdi_register();
  user_register();
  for (int i = 0; i < 6; i++) {
    g_th[i] = harness_thunk_far("KERNEL", k[i].ord); /* imported by the exe, else add it */
    if (!g_th[i])
      g_th[i] = harness_add_import("KERNEL", k[i].ord, k[i].name, HK_PASCAL, k[i].argbytes, k[i].ret, NULL);
    CHECK(g_th[i] != 0);
  }
}

/* Same module, ordinal, name, argbytes, argument bytes and return value (the last when want_ret). */
static void same_entry(const HLogEntry *a, const HLogEntry *b, bool want_ret) {
  CHECK(a && b);
  if (!a || !b) return;
  CHECK_EQ(strcmp(a->module, b->module), 0);
  CHECK_EQ(a->ordinal, b->ordinal);
  CHECK(a->name && b->name && strcmp(a->name, b->name) == 0);
  CHECK_EQ(a->argbytes, b->argbytes);
  CHECK_MEM(a->args, b->args, a->argbytes);
  if (want_ret) CHECK_EQ(a->ret, b->ret);
  CHECK_EQ(a->implemented, b->implemented);
}

static void test_alloc(void) {
  const uint16_t args[] = {0x22, 0x0001, 0x0200}; /* flags, size high, size low (push order) */
  harness_log_clear();
  harness_call_far(g_th[0], HCONV_PASCAL, args, 3);
  uint16_t h = GlobalAlloc(0x22, 0x10200); /* by name: the macro from byname.h */
  CHECK(h != 0);
  CHECK_EQ(harness_log_count(), 2);
  same_entry(harness_log_get(0), harness_log_get(1), false);
  CHECK_EQ(harness_log_get(1)->ret, h);
  CHECK_EQ(harness_log_get(1)->args[0], 0x00); /* size low word first: the stack image */
  CHECK_EQ(harness_log_get(1)->args[1], 0x02);
  CHECK_EQ(harness_log_get(1)->args[2], 0x01);
  CHECK_EQ(harness_log_get(1)->args[4], 0x22);
}

/* Two by-name calls give two entries in call order, each equal to its thunk counterpart. */
static void test_order_and_returns(void) {
  uint16_t h = GlobalAlloc(0x22, 0x100);
  CHECK(h != 0);
  harness_log_clear();
  uint32_t p1 = harness_call_far(g_th[3], HCONV_PASCAL, &h, 1); /* thunk: GlobalLock */
  uint32_t p2 = GlobalLock(h);                                  /* by name */
  bool still = GlobalUnlock(h);                                 /* by name, still locked once */
  uint32_t r3 = harness_call_far(g_th[4], HCONV_PASCAL, &h, 1); /* thunk: GlobalUnlock */
  CHECK_EQ(p1, p2);
  CHECK(still);
  CHECK_EQ(r3 & 0xFFFF, 0);
  CHECK_EQ(harness_log_count(), 4);
  same_entry(harness_log_get(0), harness_log_get(1), true);
  CHECK_EQ(harness_log_get(1)->ret, p2);
  CHECK_EQ(harness_log_get(2)->ordinal, 19);
  CHECK_EQ(harness_log_get(2)->ret, 1);
  CHECK_EQ(harness_log_get(3)->ordinal, 19);
  CHECK_EQ(harness_log_get(3)->ret, 0);
  CHECK_EQ(GlobalSize(h), 0x100);
  CHECK_EQ(GlobalReAlloc(h, 0x200, 0x22), h);
  CHECK_EQ(GlobalFree(h), 0);
  CHECK_EQ(harness_log_get(harness_log_count() - 1)->ordinal, 17);
}


/* Thunk for (module, ordinal): the exe's own import when it has one, else added here. */
static uint32_t thunk_for(const char *module, uint16_t ord, const char *name, uint16_t argbytes, int kind,
                          int ret) {
  uint32_t t = harness_thunk_far(module, ord);
  if (!t) t = harness_add_import(module, ord, name, kind, argbytes, ret, NULL);
  CHECK(t != 0);
  return t;
}

/* SelectObject, SetBkMode, GetTextExtent and _wsprintf (stars-bfj.35): each by-name call leaves the
 * entry of the same call through the thunk, and returns the same value. */
static void test_gdi_user(void) {
  uint32_t t_sel = thunk_for("GDI", 45, "SelectObject", 4, HK_PASCAL, HR_RET16);
  uint32_t t_bk = thunk_for("GDI", 2, "SetBkMode", 4, HK_PASCAL, HR_RET16);
  uint32_t t_ext = thunk_for("GDI", 91, "GetTextExtent", 8, HK_PASCAL, HR_RET32);
  uint32_t t_wsp = thunk_for("USER", 420, "_wsprintf", 8, HK_VARARGS, HR_RET16);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  uint16_t font = GetStockObject(16); /* OEM_FIXED_FONT */
  CHECK(font != 0);

  harness_log_clear();
  const uint16_t sel_args[] = {dc, font};
  uint32_t r1 = harness_call_far(t_sel, HCONV_PASCAL, sel_args, 2);
  uint16_t r2 = SelectObject(dc, font);
  CHECK_EQ(harness_log_count(), 2);
  same_entry(harness_log_get(0), harness_log_get(1), false); /* the old object differs: state */
  CHECK_EQ(harness_log_get(1)->ret, r2);
  CHECK_EQ(r2, font); /* by name, second: returns the font the thunk call selected */
  CHECK(r1 != 0);

  harness_log_clear();
  const uint16_t bk_args[] = {dc, 1};
  r1 = harness_call_far(t_bk, HCONV_PASCAL, bk_args, 2);
  int16_t b2 = SetBkMode(dc, 1);
  CHECK_EQ(harness_log_count(), 2);
  same_entry(harness_log_get(0), harness_log_get(1), false);
  CHECK_EQ(harness_log_get(1)->ret, (uint16_t)b2);
  CHECK_EQ(b2, 1); /* the second call returns the mode set by the first */
  harness_log_clear();
  r1 = harness_call_far(t_bk, HCONV_PASCAL, (const uint16_t[]){dc, 0xFFFF}, 2);
  b2 = SetBkMode(dc, -1);
  same_entry(harness_log_get(0), harness_log_get(1), false); /* a negative mode is logged as a word */
  CHECK_EQ(r1 & 0xFFFF, 1);
  CHECK_EQ(harness_log_get(1)->ret, (uint16_t)b2);
  CHECK_EQ(b2, -1);

  uint16_t h = GlobalAlloc(0x22, 0x100);
  uint32_t buf = GlobalLock(h);
  CHECK(buf != 0);
  memcpy(LIN(buf), "Hello 12345", 12);
  harness_log_clear();
  const uint16_t ext_args[] = {dc, BYNAME_HI(buf), BYNAME_LO(buf), 5};
  r1 = harness_call_far(t_ext, HCONV_PASCAL, ext_args, 4);
  uint32_t e2 = GetTextExtent(dc, buf, 5);
  CHECK_EQ(harness_log_count(), 2);
  same_entry(harness_log_get(0), harness_log_get(1), true);
  CHECK_EQ(r1, e2);
  CHECK(e2 != 0);

  /* _wsprintf: cdecl, out pointer then format pointer then the arguments. */
  uint32_t out = buf + 0x40, fmt = buf + 0x20;
  memcpy(LIN(fmt), "%d-%ld", 7);
  uint32_t va = buf + 0x80;
  const uint16_t vals[] = {7, 0x0001, 0x0002}; /* int 7, long 0x00020001 */
  memcpy(LIN(va), vals, 6);
  harness_log_clear();
  /* through the thunk the arguments follow on the stack */
  const uint16_t wa[] = {BYNAME_LO(out), BYNAME_HI(out), BYNAME_LO(fmt), BYNAME_HI(fmt), 7, 0x0001, 0x0002};
  r1 = harness_call_far(t_wsp, HCONV_CDECL, wa, 7);
  char first[16];
  memcpy(first, LIN(out), 16);
  memset(LIN(out), 0, 16);
  int16_t n2 = _wsprintf(out, fmt, va);
  CHECK_EQ(harness_log_count(), 2);
  same_entry(harness_log_get(0), harness_log_get(1), true);
  CHECK_EQ(harness_log_get(1)->argbytes, 8);
  CHECK_EQ(r1 & 0xFFFF, (uint16_t)n2);
  CHECK_EQ(n2, 8); /* "7-131073" */
  CHECK_MEM(first, LIN(out), 9);
  CHECK_EQ(strcmp((const char *)LIN(out), "7-131073"), 0);
  CHECK_EQ(GlobalFree(h), 0);
}


/* Segment 3 (stars-bfj.5): every wrapper added for it leaves the entry of the same call through the
 * import thunk: module, ordinal, name, argbytes and the raw argument bytes, and, where the result does
 * not depend on state, the return value. The calls use a zeroed global block for pointer arguments and
 * handle 0 where the runtime accepts it. */
#define BN_CASE(mod, ord, nm, rk, want_ret, byexpr, ...)                                          \
  do {                                                                                            \
    const uint16_t a_[] = {__VA_ARGS__};                                                          \
    uint32_t t_ = thunk_for(mod, ord, nm, (uint16_t)sizeof a_, HK_PASCAL, rk);                    \
    harness_log_clear();                                                                          \
    (void)harness_call_far(t_, HCONV_PASCAL, a_, (int)(sizeof a_ / 2));                           \
    (void)(byexpr);                                                                               \
    CHECK_EQ(harness_log_count(), 2);                                                             \
    same_entry(harness_log_get(0), harness_log_get(1), want_ret);                                 \
  } while (0)
/* The same for an API without arguments. */
#define BN_CASE0(mod, ord, nm, rk, want_ret, byexpr)                                              \
  do {                                                                                            \
    uint32_t t_ = thunk_for(mod, ord, nm, 0, HK_PASCAL, rk);                                      \
    harness_log_clear();                                                                          \
    (void)harness_call_far(t_, HCONV_PASCAL, NULL, 0);                                            \
    (void)(byexpr);                                                                               \
    CHECK_EQ(harness_log_count(), 2);                                                             \
    same_entry(harness_log_get(0), harness_log_get(1), want_ret);                                 \
  } while (0)

static void test_seg3_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t rect = blk, msg = blk + 0x40, str = blk + 0x80;
  memcpy(LIN(str), "text", 5);
  const uint16_t R_HI = BYNAME_HI(rect), R_LO = BYNAME_LO(rect);
  const uint16_t M_HI = BYNAME_HI(msg), M_LO = BYNAME_LO(msg);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);

  /* KERNEL */
  BN_CASE("KERNEL", 51, "MakeProcInstance", HR_RET32, false, MakeProcInstance(0x00180ec2, 0x1234), 0x0018, 0x0ec2, 0x1234);
  BN_CASE("KERNEL", 52, "FreeProcInstance", HR_RET16, true, FreeProcInstance(0x00180ec2), 0x0018, 0x0ec2);
  BN_CASE("KERNEL", 63, "FreeResource", HR_RET16, false, FreeResource(0), 0);
  /* GDI */
  BN_CASE("GDI", 1, "SetBkColor", HR_RET32, false, SetBkColor(dc, 0x00ABCDEF), dc, 0x00AB, 0xCDEF);
  BN_CASE("GDI", 9, "SetTextColor", HR_RET32, false, SetTextColor(dc, 0x00123456), dc, 0x0012, 0x3456);
  BN_CASE("GDI", 22, "IntersectClipRect", HR_RET16, false, IntersectClipRect(dc, 0, 0, 100, -5), dc, 0, 0, 100, 0xFFFB);
  BN_CASE("GDI", 44, "SelectClipRgn", HR_RET16, false, SelectClipRgn(dc, 0), dc, 0);
  BN_CASE("GDI", 69, "DeleteObject", HR_RET16, false, DeleteObject(0), 0);
  BN_CASE("GDI", 80, "GetDeviceCaps", HR_RET16, true, GetDeviceCaps(dc, 12), dc, 12);
  BN_CASE("GDI", 80, "GetDeviceCaps", HR_RET16, true, GetDeviceCaps(dc, 14), dc, 14);
  /* USER */
  BN_CASE("USER", 17, "GetCursorPos", HR_RET16, true, GetCursorPos(msg), M_HI, M_LO);
  BN_CASE("USER", 10, "SetTimer", HR_RET16, false, SetTimer(0, 0, 50, 0), 0, 0, 50, 0, 0);
  BN_CASE("USER", 12, "KillTimer", HR_RET16, false, KillTimer(0, 7), 0, 7);
  BN_CASE0("USER", 13, "GetTickCount", HR_RET32, false, GetTickCount());
  BN_CASE0("USER", 23, "GetFocus", HR_RET16, true, GetFocus());
  BN_CASE("USER", 30, "WindowFromPoint", HR_RET16, true, WindowFromPoint(0x00050007), 0x0005, 0x0007);
  BN_CASE("USER", 31, "IsIconic", HR_RET16, true, IsIconic(0), 0);
  BN_CASE("USER", 33, "GetClientRect", HR_RET16, true, GetClientRect(0, rect), 0, R_HI, R_LO);
  BN_CASE("USER", 37, "SetWindowText", HR_RET16, true, SetWindowText(0, str), 0, S_HI, S_LO);
  BN_CASE("USER", 46, "GetParent", HR_RET16, true, GetParent(0), 0);
  BN_CASE("USER", 53, "DestroyWindow", HR_RET16, true, DestroyWindow(0), 0);
  BN_CASE0("USER", 60, "GetActiveWindow", HR_RET16, true, GetActiveWindow());
  BN_CASE("USER", 66, "GetDC", HR_RET16, false, GetDC(0), 0);
  BN_CASE("USER", 68, "ReleaseDC", HR_RET16, false, ReleaseDC(0, dc), 0, dc);
  BN_CASE("USER", 69, "SetCursor", HR_RET16, false, SetCursor(0), 0);
  BN_CASE("USER", 77, "OffsetRect", HR_RET16, true, OffsetRect(rect, 3, -4), R_HI, R_LO, 3, 0xFFFC);
  BN_CASE("USER", 81, "FillRect", HR_RET16, false, FillRect(dc, rect, 0), dc, R_HI, R_LO, 0);
  BN_CASE("USER", 87, "DialogBox", HR_RET16, true, DialogBox(0, 0x00000061, 0, 0), 0, 0, 0x61, 0, 0, 0);
  BN_CASE("USER", 88, "EndDialog", HR_RET16, true, EndDialog(0, 1), 0, 1);
  BN_CASE("USER", 91, "GetDlgItem", HR_RET16, true, GetDlgItem(0, 0x401), 0, 0x401);
  BN_CASE("USER", 106, "GetKeyState", HR_RET16, true, GetKeyState(0x10), 0x10);
  BN_CASE("USER", 110, "PostMessage", HR_RET16, true, PostMessage(0, 0x464, 0, 0), 0, 0x464, 0, 0, 0);
  BN_CASE("USER", 111, "SendMessage", HR_RET32, true, SendMessage(0, 0x102, 0x2b, 0x00010002), 0, 0x102, 0x2b, 0x0001, 0x0002);
  BN_CASE("USER", 113, "TranslateMessage", HR_RET16, true, TranslateMessage(msg), M_HI, M_LO);
  BN_CASE("USER", 114, "DispatchMessage", HR_RET32, true, DispatchMessage(msg), M_HI, M_LO);
  BN_CASE("USER", 125, "InvalidateRect", HR_RET16, true, InvalidateRect(0, 0, 1), 0, 0, 0, 1);
  BN_CASE("USER", 173, "LoadCursor", HR_RET16, true, LoadCursor(0, 0x00007f00), 0, 0x0000, 0x7f00);
  BN_CASE("USER", 178, "TranslateAccelerator", HR_RET16, true, TranslateAccelerator(0, 0, msg), 0, 0, M_HI, M_LO);
  BN_CASE("USER", 179, "GetSystemMetrics", HR_RET16, true, GetSystemMetrics(4), 4);
  BN_CASE("USER", 180, "GetSysColor", HR_RET32, true, GetSysColor(15), 15);
  BN_CASE("USER", 457, "DestroyIcon", HR_RET16, true, DestroyIcon(0), 0);
  BN_CASE("USER", 458, "DestroyCursor", HR_RET16, true, DestroyCursor(0), 0);
  /* The last ones change the message queue state: GetMessage with nothing to deliver ends the run. */
  BN_CASE("USER", 108, "GetMessage", HR_RET16, false, GetMessage(msg, 0, 0, 0), M_HI, M_LO, 0, 0, 0);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* Segment 13 (stars-bfj.15): the wrappers added for it, each against the entry of the same call
 * through the import thunk (module, ordinal, name, argbytes, argument bytes, and the return value
 * where it does not depend on state). Handle 0 or a zeroed block where the runtime accepts it. */
static void test_seg13_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t rect = blk, rect2 = blk + 0x20, pt = blk + 0x40, str = blk + 0x80, ps = blk + 0x100;
  memcpy(LIN(str), "text", 5);
  const uint16_t R_HI = BYNAME_HI(rect), R_LO = BYNAME_LO(rect);
  const uint16_t R2_HI = BYNAME_HI(rect2), R2_LO = BYNAME_LO(rect2);
  const uint16_t P_HI = BYNAME_HI(pt), P_LO = BYNAME_LO(pt);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  const uint16_t PS_HI = BYNAME_HI(ps), PS_LO = BYNAME_LO(ps);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  BN_CASE("GDI", 29, "PatBlt", HR_RET16, true, PatBlt(dc, 1, 2, 3, 4, 0x00F00021), dc, 1, 2, 3, 4, 0x00F0, 0x0021);
  BN_CASE("GDI", 351, "ExtTextOut", HR_RET16, true, ExtTextOut(dc, 5, 6, 0, 0, str, 4, 0), dc, 5, 6, 0, 0, 0, S_HI, S_LO, 4, 0, 0);
  BN_CASE("USER", 28, "ClientToScreen", HR_RET16, true, ClientToScreen(0, pt), 0, P_HI, P_LO);
  BN_CASE("USER", 29, "ScreenToClient", HR_RET16, true, ScreenToClient(0, pt), 0, P_HI, P_LO);
  BN_CASE("USER", 258, "MapWindowPoints", HR_RET16, true, MapWindowPoints(0, 0, pt, 1), 0, 0, P_HI, P_LO, 1);
  BN_CASE("USER", 36, "GetWindowText", HR_RET16, true, GetWindowText(0, str + 0x20, 0x14), 0, S_HI, (uint16_t)(S_LO + 0x20), 0x14);
  BN_CASE("USER", 49, "IsWindowVisible", HR_RET16, true, IsWindowVisible(0), 0);
  BN_CASE("USER", 262, "GetWindow", HR_RET16, true, GetWindow(0, 5), 0, 5);
  BN_CASE("USER", 135, "GetWindowLong", HR_RET32, true, GetWindowLong(0, -4), 0, 0xFFFC);
  BN_CASE("USER", 136, "SetWindowLong", HR_RET32, true, SetWindowLong(0, -4, 0x00010002), 0, 0xFFFC, 0x0001, 0x0002);
  BN_CASE("USER", 104, "MessageBeep", HR_RET16, true, MessageBeep(0), 0);
  BN_CASE("USER", 76, "PtInRect", HR_RET16, true, PtInRect(rect, 0x00050007), R_HI, R_LO, 0x0005, 0x0007);
  BN_CASE("USER", 244, "EqualRect", HR_RET16, true, EqualRect(rect, rect2), R_HI, R_LO, R2_HI, R2_LO);
  BN_CASE("USER", 83, "FrameRect", HR_RET16, true, FrameRect(dc, rect, 0), dc, R_HI, R_LO, 0);
  BN_CASE("USER", 282, "SelectPalette", HR_RET16, false, SelectPalette(dc, 0, 0), dc, 0, 0);
  BN_CASE("USER", 283, "RealizePalette", HR_RET16, true, RealizePalette(dc), dc);
  BN_CASE("USER", 18, "SetCapture", HR_RET16, false, SetCapture(0), 0);
  BN_CASE0("USER", 19, "ReleaseCapture", HR_RET16, true, ReleaseCapture());
  BN_CASE("USER", 22, "SetFocus", HR_RET16, false, SetFocus(0), 0);
  BN_CASE("USER", 124, "UpdateWindow", HR_RET16, true, UpdateWindow(0), 0);
  BN_CASE("USER", 232, "SetWindowPos", HR_RET16, true, SetWindowPos(0, 0xFFFF, 1, 2, 3, 4, 0x10), 0, 0xFFFF, 1, 2, 3, 4, 0x10);
  BN_CASE("USER", 107, "DefWindowProc", HR_RET32, true, DefWindowProc(0, 0x20, 0, 0x00010002), 0, 0x20, 0, 0x0001, 0x0002);
  BN_CASE("USER", 39, "BeginPaint", HR_RET16, true, BeginPaint(0, ps), 0, PS_HI, PS_LO);
  BN_CASE("USER", 40, "EndPaint", HR_RET16, true, EndPaint(0, ps), 0, PS_HI, PS_LO);
  BN_CASE("USER", 41, "CreateWindow", HR_RET16, true, CreateWindow(str, str, 0, 1, 2, 3, 4, 0, 0, 0x1234, 0), S_HI, S_LO, S_HI, S_LO, 0, 0, 1, 2, 3, 4, 0, 0, 0x1234, 0, 0);
  BN_CASE("USER", 122, "CallWindowProc", HR_RET32, true, CallWindowProc(0, 0, 0x20, 0, 0), 0, 0, 0, 0x20, 0, 0, 0);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* Segment 16 (stars-bfj.18): the wrappers added for it, each against the entry of the same call
 * through the import thunk. */
static void test_seg16_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t rect = blk, str = blk + 0x80;
  memcpy(LIN(str), "text", 5);
  const uint16_t R_HI = BYNAME_HI(rect), R_LO = BYNAME_LO(rect);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  BN_CASE("GDI", 33, "TextOut", HR_RET16, true, TextOut(dc, 5, -6, str, 4), dc, 5, 0xFFFA, S_HI, S_LO, 4);
  BN_CASE("USER", 96, "CheckRadioButton", HR_RET16, true, CheckRadioButton(0, 0x431, 0x434, 0x432), 0, 0x431, 0x434, 0x432);
  BN_CASE("USER", 34, "EnableWindow", HR_RET16, true, EnableWindow(0, 1), 0, 1);
  BN_CASE("USER", 93, "GetDlgItemText", HR_RET16, true, GetDlgItemText(0, 0x10c, str + 0x20, 0xe), 0, 0x10c, S_HI, (uint16_t)(S_LO + 0x20), 0xe);
  BN_CASE("USER", 32, "GetWindowRect", HR_RET16, true, GetWindowRect(0, rect), 0, R_HI, R_LO);
  BN_CASE("USER", 101, "SendDlgItemMessage", HR_RET32, true, SendDlgItemMessage(0, 0x415, 0x401, 0, 0x000c0000), 0, 0x415, 0x401, 0, 0x000c, 0);
  BN_CASE("USER", 42, "ShowWindow", HR_RET16, true, ShowWindow(0, 0), 0, 0);
  BN_CASE("USER", 171, "WinHelp", HR_RET16, true, WinHelp(0, str, 1, 0x00010000), 0, S_HI, S_LO, 1, 0x0001, 0);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* Segment 1 (stars-bfj.3): the wrappers added for it, each against the entry of the same call through
 * the import thunk. Handles and pointers that differ between two calls are not compared. */
static void test_seg01_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t str = blk + 0x80, out = blk + 0x100, tm = blk + 0x200;
  memcpy(LIN(str), "text", 5);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  const uint16_t O_HI = BYNAME_HI(out), O_LO = BYNAME_LO(out);
  const uint16_t T_HI = BYNAME_HI(tm), T_LO = BYNAME_LO(tm);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* LocalAlloc needs the local heap */
  BN_CASE("KERNEL", 5, "LocalAlloc", HR_RET16, false, LocalAlloc(0x40, 0x32), 0x40, 0x32);
  BN_CASE("KERNEL", 7, "LocalFree", HR_RET16, true, LocalFree(0), 0);
  BN_CASE("KERNEL", 127, "GetPrivateProfileInt", HR_RET16, true, GetPrivateProfileInt(str, str + 1, -7, str + 2), S_HI, S_LO, S_HI,
          (uint16_t)(S_LO + 1), 0xFFF9, S_HI, (uint16_t)(S_LO + 2));
  BN_CASE("KERNEL", 128, "GetPrivateProfileString", HR_RET16, true, GetPrivateProfileString(str, str + 1, str, out, 0x14, str + 2),
          S_HI, S_LO, S_HI, (uint16_t)(S_LO + 1), S_HI, S_LO, O_HI, O_LO, 0x14, S_HI, (uint16_t)(S_LO + 2));
  BN_CASE("GDI", 57, "CreateFontIndirect", HR_RET16, false, CreateFontIndirect(tm), T_HI, T_LO);
  BN_CASE("GDI", 60, "CreatePatternBrush", HR_RET16, true, CreatePatternBrush(0), 0);
  BN_CASE("GDI", 61, "CreatePen", HR_RET16, false, CreatePen(1, 0, 0x00FF0000), 1, 0, 0x00FF, 0x0000);
  BN_CASE("GDI", 64, "CreateRectRgn", HR_RET16, false, CreateRectRgn(-10, 0, 2000, 2000), 0xFFF6, 0, 2000, 2000);
  BN_CASE("GDI", 93, "GetTextMetrics", HR_RET16, true, GetTextMetrics(dc, tm), dc, T_HI, T_LO);
  BN_CASE("GDI", 128, "MulDiv", HR_RET16, true, MulDiv(10, 0x5a, 0x48), 10, 0x5a, 0x48);
  BN_CASE("GDI", 128, "MulDiv", HR_RET16, true, MulDiv(-10, 0x5a, 0x48), 0xFFF6, 0x5a, 0x48);
  BN_CASE("USER", 174, "LoadIcon", HR_RET16, true, LoadIcon(0, 0x00007f00), 0, 0x0000, 0x7f00);
  BN_CASE("USER", 175, "LoadBitmap", HR_RET16, true, LoadBitmap(0, 0x00000058), 0, 0x0000, 0x58);
  BN_CASE("USER", 177, "LoadAccelerators", HR_RET16, true, LoadAccelerators(0, 0x00000074), 0, 0x0000, 0x74);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* Segment 5 (stars-bfj.7): the wrappers added for it, each against the entry of the same call through
 * the import thunk. Handles that differ between two calls are not compared. */
static void test_seg05_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t rect = blk, str = blk + 0x80;
  memcpy(LIN(str), "text", 5);
  const uint16_t R_HI = BYNAME_HI(rect), R_LO = BYNAME_LO(rect);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  BN_CASE("GDI", 34, "BitBlt", HR_RET16, true, BitBlt(dc, 5, -6, 11, 12, dc, 0x23, 0x39, 0x008800C6), dc, 5, 0xFFFA, 11, 12, dc, 0x23, 0x39,
          0x0088, 0x00C6);
  BN_CASE("GDI", 52, "CreateCompatibleDC", HR_RET16, false, CreateCompatibleDC(dc), dc);
  BN_CASE("GDI", 68, "DeleteDC", HR_RET16, true, DeleteDC(0), 0);
  BN_CASE("USER", 85, "DrawText", HR_RET16, true, DrawText(dc, str, -1, rect, 0x25), dc, S_HI, S_LO, 0xFFFF, R_HI, R_LO, 0x25);
  BN_CASE("USER", 72, "SetRect", HR_RET16, true, SetRect(rect, 1, -2, 3, 4), R_HI, R_LO, 1, 0xFFFE, 3, 4);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* Segment 4 (stars-bfj.6): the wrappers added for it (menus, icons, clipping, Escape, ExitWindows, the INI writer), each against
 * the entry of the same call through the import thunk. PrintDlg (test hook), TimerCount and CreateDialog need a PRINTDLG, a
 * TIMERINFO and a dialog template and are covered by the segment 4 scenarios instead. */
static void test_seg04_wrappers(void) {
  uint16_t h = GlobalAlloc(0x42, 0x400); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  uint32_t str = blk + 0x80;
  memcpy(LIN(str), "text", 5);
  const uint16_t S_HI = BYNAME_HI(str), S_LO = BYNAME_LO(str);
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  BN_CASE("USER", 84, "DrawIcon", HR_RET16, true, DrawIcon(dc, 2, 3, 0), dc, 2, 3, 0);
  BN_CASE("GDI", 21, "ExcludeClipRect", HR_RET16, true, ExcludeClipRect(dc, 1, 2, 30, 40), dc, 1, 2, 30, 40);
  BN_CASE("GDI", 27, "Rectangle", HR_RET16, true, Rectangle(dc, 1, 2, 30, 40), dc, 1, 2, 30, 40);
  BN_CASE("GDI", 38, "Escape", HR_RET16, true, Escape(dc, 8, 2, str, 0), dc, 8, 2, S_HI, S_LO, 0, 0);
  BN_CASE("USER", 413, "DeleteMenu", HR_RET16, true, DeleteMenu(0, 3, 0x400), 0, 3, 0x400);
  BN_CASE("USER", 410, "InsertMenu", HR_RET16, true, InsertMenu(0, 1, 0x400, 0x10cc, str), 0, 1, 0x400, 0x10cc, S_HI, S_LO);
  BN_CASE("USER", 155, "EnableMenuItem", HR_RET16, true, EnableMenuItem(0, 0x6a, 3), 0, 0x6a, 3);
  BN_CASE("USER", 263, "GetMenuItemCount", HR_RET16, true, GetMenuItemCount(0), 0);
  BN_CASE("USER", 160, "DrawMenuBar", HR_RET16, false, DrawMenuBar(0), 0);
  BN_CASE("KERNEL", 129, "WritePrivateProfileString", HR_RET16, true,
          WritePrivateProfileString(str, str + 1, str + 2, str + 3), S_HI, S_LO, S_HI, (uint16_t)(S_LO + 1), S_HI, (uint16_t)(S_LO + 2), S_HI,
          (uint16_t)(S_LO + 3));
  BN_CASE("USER", 7, "ExitWindows", HR_RET16, true, ExitWindows(1, 0), 0, 1, 0);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

/* stars-n7l: DispatchMessage by name must lower the emulated SP by 8 (message pointer plus return
 * address), as the import thunk does, so the window procedure sees the stack layout of the original. */
static uint16_t g_wp_sp, g_wp_calls;

static uint32_t ad_wndproc_sp(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)r;
  g_wp_sp = sp; /* at the return address */
  g_wp_calls++;
  return 0;
}

static void test_dispatch_sp(void) {
  static const uint8_t stub[] = {0xcb};
  uint16_t sel = harness_alloc_code(stub, sizeof stub);
  CHECK(sel != 0);
  uint16_t h = GlobalAlloc(0x42, 0x100);
  uint32_t blk = GlobalLock(h);
  CHECK(blk != 0);
  /* WM_TIMER with a callback in lParam is dispatched to that procedure, with no window needed. */
  MSG16 *m = LIN(blk);
  memset(m, 0, sizeof *m);
  m->message = 0x0113;
  m->lParam = MK_FP(sel, 0);

  DtEntry e;
  memset(&e, 0, sizeof e);
  e.seg = sel; e.name = "wp_sp"; e.conv = FAR_PASCAL; e.adapter = ad_wndproc_sp; e.ret_words = 2;
  difftest_unregister_all();
  difftest_register(&e);
  rt_set_mode(RT_MODE_DECOMP);
  const uint16_t top = harness_stack_top();
  rt_ss = MEM_SEL_DGROUP;
  rt_sp = top;
  rt_bp = 0;
  g_wp_sp = 0;
  g_wp_calls = 0;
  DispatchMessage(blk);
  CHECK_EQ(g_wp_calls, 1);
  /* top - 8 (message pointer and return address) - 10 (five argument words) - 4 (far return address) */
  CHECK_EQ(g_wp_sp, (uint16_t)(top - 8 - 10 - 4));
  CHECK_EQ(rt_sp, top); /* restored */
  rt_set_mode(RT_MODE_ORIGINAL);
  rt_sp = rt_bp = 0;
  difftest_unregister_all();
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}

int main(void) {
  setup();
  test_alloc();
  test_order_and_returns();
  test_gdi_user();
  test_seg3_wrappers();
  test_seg13_wrappers();
  test_seg16_wrappers();
  test_seg01_wrappers();
  test_seg05_wrappers();
  test_seg04_wrappers();
  test_dispatch_sp();
  harness_shutdown();
  return TEST_DONE();
}
