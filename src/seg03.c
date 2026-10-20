/* Decompiled code segment 3 (NE segment 3, selector 0x0018).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg03.c; Ghidra's pseudo-C in re/decomp/seg03 is only a reading aid.
 *
 * Segment 3 is the program's main module:
 *   3:0x0000  WinMain: start-up, command line parser, message loop, clean-up
 *   3:0x03e0  read_batch_file_name: read the first line of the batch file named by -b into the
 *             file name buffer
 *   3:0x04a2  find_player_key_conflict: find another player slot with the same key and different name
 *             data
 *   3:0x054c  update_system_colors: (re)create the system colour brushes and cache colours and
 *             metrics (FRAMEWNDPROC 4:0x0528 also calls it on WM_SYSCOLORCHANGE and WM_WININICHANGE)
 *   3:0x0766  release_all_resources: release every GDI object, resource, procedure instance and heap
 *             of the program
 *   3:0x0c7a  format_version_string: format the version string of the About box
 *   3:0x0ca0  ABOUT, the About dialog procedure (credits scroller on a timer)
 *   3:0x0ec2  ORDERINFODLG, the order-information dialog procedure
 *   3:0x0f3a  filter_char_message: WM_CHAR filter of the message loop
 *   3:0x0f90  filter_key_message: WM_KEYDOWN / WM_KEYUP filter of the message loop
 * Names: WinMain and the export names ABOUT and ORDERINFODLG are kept; Ghidra's other names are all
 * generic (FUN_1010_<off>), so the C names describe what each function does (CONVENTIONS rule f).
 * The old fn_03_<off> names are kept as aliases in the comment above each function.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n + 2 * pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. 3:0x054c has no BP frame and pushes DI
 * and SI, 3:0x0c7a pushes its own arguments: both move rt_sp by hand so the callees see the original
 * stack. A callee that mirrors a frame is reached through rt_call_far; callees not decompiled yet use
 * CALL_ORIG. The catch buffer of 3:0x03e0 (34:0x0800 / 34:0x0825, a setjmp / longjmp pair) cannot be
 * resumed from C: a throw from the file functions inside CALL_ORIG is not modelled (same limitation
 * as in seg12.c for 12:0x0000).
 * Memory is read in 16-bit pieces: an offset is a uint16_t and wraps like the original's. DS = SS =
 * DGROUP in every function here (the few instructions that load another DS restore it). The
 * generated globals.h types some DGROUP words by Ghidra's guess (byte for 0x251A, which the code
 * reads and writes as a word), so words are accessed with the helpers below at the original
 * offsets, named in the comments. */
#include <string.h>

#include "src/stars.h"

static inline uint8_t rd8(uint16_t seg, uint16_t off) { return *(uint8_t *)LIN(MK_FP(seg, off)); }
static inline void wr8(uint16_t seg, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(seg, off)) = v; }
static inline uint16_t rd16(uint16_t seg, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 2);
  return v;
}
static inline void wr16(uint16_t seg, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 2); }
static inline uint32_t rd32(uint16_t seg, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 4);
  return v;
}
static inline void wr32(uint16_t seg, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 4); }
/* DGROUP (DS) bytes, words and dwords. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }
static inline void dwr32(uint16_t off, uint32_t v) { wr32(MEM_SEL_DGROUP, off, v); }
/* A pointer to a DGROUP offset, as the far pointer the APIs take. */
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }

/* format_version_string: 3:0x0c7a (Ghidra FUN_1010_0c7a, alias fn_03_0c7a).
 * 38 bytes. No BP frame, no pushes of its own.
 * Version string of the About box: FAR CDECL, no arguments, returns AX = 0x5866 (DX is whatever
 * the call to 8:0x8df4 left; the caller ignores it). It pushes the three words 2, 0x3c (60) and
 * 0x69 (105), then the dword 0x227 as the argument of 8:0x8df4 (a string lookup, returns a near
 * pointer to the format); the words 2, 60, 105 stay on the stack and are the arguments of
 * _wsprintf(DS:0x5866, DS:format, ...). The words are written to the emulated stack where the
 * original leaves them, and the call to 8:0x8df4 starts where the original's pushed argument is. */
DECOMP(3, 0x0c7a, format_version_string, FAR_CDECL)
uint16_t format_version_string(void)
{
  uint16_t sp0 = rt_sp;                       /* the return address */
  wr16(rt_ss, (uint16_t)(sp0 - 2), 0x0069);
  wr16(rt_ss, (uint16_t)(sp0 - 4), 0x003c);
  wr16(rt_ss, (uint16_t)(sp0 - 6), 0x0002);
  rt_sp = (uint16_t)(sp0 - 6);                /* CALL_ORIG pushes 0x227 at sp0 - 8, as the original */
  uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0227);
  (void)_wsprintf(dsfp(0x5866), dsfp(fmt), MK_FP(rt_ss, (uint16_t)(sp0 - 6)));
  rt_sp = sp0;
  return 0x5866;
}

/* ORDERINFODLG: 3:0x0ec2 (Ghidra and export name ORDERINFODLG, no fn_03 alias).
 * 120 bytes. ENTER 8,0.
 * ORDERINFODLG(hwnd, msg, wParam, lParam): FAR PASCAL dialog procedure (hwnd at [BP+0xE], msg
 * [BP+0xC], wParam [BP+0xA], lParam [BP+6]). WM_ERASEBKGND (0x14) fills the client rect (a RECT at
 * BP-8) with the brush DS:0x10 and returns 1; WM_CTLCOLOR (0x19) with control type 6 (high word of
 * lParam) sets the background colour DS:0x253A (a COLORREF dword) on the DC wParam and returns the
 * brush DS:0x10; WM_COMMAND (0x111) with id 1 or 2 ends the dialog with 1 and returns 1; anything
 * else returns 0. */
DECOMP(3, 0x0ec2, ORDERINFODLG, FAR_PASCAL)
uint16_t ORDERINFODLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(8);
  uint16_t ret = 0;
  if (msg == 0x14) {
    uint32_t rect = MK_FP(rt_ss, (uint16_t)(rt_bp - 8));
    GetClientRect(hwnd, rect);
    (void)FillRect(wParam, rect, drd16(0x0010));
    ret = 1;
  } else if (msg == 0x19) {
    if ((uint16_t)(lParam >> 16) == 6) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
  } else if (msg == 0x111) {
    if (wParam == 2 || wParam == 1) {
      (void)EndDialog(hwnd, 1);
      ret = 1;
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* filter_char_message: 3:0x0f3a (Ghidra FUN_1010_0f3a, alias fn_03_0f3a).
 * 86 bytes. PUSH BP; MOV BP,SP; PUSH DI; PUSH SI.
 * WM_CHAR filter of the message loop: FAR CDECL (hwnd [BP+6], char [BP+8], lParam [BP+0xA]).
 * Returns 1 when it forwarded the character to the window DS:0x150 with SendMessage(WM_CHAR), else
 * 0. Forwards '+' and '-' when DS:0x150 is set, and 'v' and 'V' always, unless the edit window
 * DS:0x158 is set and has the focus (DS:0x59E6). */
DECOMP(3, 0x0f3a, filter_char_message, FAR_CDECL)
uint16_t filter_char_message(uint16_t hwnd, uint16_t ch, uint32_t lParam)
{
  FRAME_ENTER(0 + 4);
  (void)hwnd;
  uint16_t si = ch;
  uint16_t ret = 0;
  bool hit = (drd16(0x0150) != 0 && (si == 0x2b || si == 0x2d)) || si == 0x76 || si == 0x56;
  if (hit) {
    uint16_t focus = GetFocus();
    if (drd16(0x0158) == 0 || focus != drd16(0x59e6)) {
      (void)SendMessage(drd16(0x0150), 0x102, si, lParam);
      ret = 1;
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* find_player_key_conflict: 3:0x04a2 (Ghidra FUN_1010_04a2, alias fn_03_04a2).
 * 169 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * find_player_key_conflict(n): FAR CDECL, n at [BP+6]. The table at the far pointer DS:0x980 has 16-byte entries:
 * a key dword and name data. Returns -1 when the key of entry n fails the id check 14:0x2bb4.
 * Otherwise it looks at every player slot j (count DS:0x78, signed) other than n whose flag byte
 * DS:0x5AB8 + 0xC0 * j has bit 2 set, and returns the first j whose key dword equals the key of n
 * but whose 11 bytes of name data (offsets 4 to 14) differ from those of n; -1 if there is none. */
DECOMP(3, 0x04a2, find_player_key_conflict, FAR_CDECL)
int16_t find_player_key_conflict(int16_t n)
{
  FRAME_ENTER(8 + 4);
  uint16_t bx = (uint16_t)((uint16_t)n << 4);          /* [bp-8] */
  uint32_t tab = drd32(0x0980);
  uint16_t si = FP_OFF(tab), es = FP_SEG(tab);
  uint32_t key = rd32(es, (uint16_t)(bx + si));
  int16_t ret = -1;
  if ((uint16_t)CALL_ORIG(14, 0x2bb4, FAR_CDECL, 2, (uint16_t)key, (uint16_t)(key >> 16)) != 0) {
    if ((int16_t)drd16(0x0078) > 0) {
      uint16_t j = 0;                                   /* [bp-6] */
      uint16_t off = 0;                                 /* [bp-2], 16 * j */
      uint16_t flags = 0x5ab8;                          /* [bp-4], DS:0x5AB8 + 0xC0 * j */
      do {
        if (j != (uint16_t)n && (drd8(flags) & 4) != 0 &&
            rd32(es, (uint16_t)(off + si)) == rd32(es, (uint16_t)(bx + si))) {
          bool same = true;
          for (uint16_t k = 0; k < 11 && same; k++)
            same = rd8(es, (uint16_t)(bx + si + 4 + k)) == rd8(es, (uint16_t)(off + si + 4 + k));
          if (!same) {
            ret = (int16_t)j;
            break;
          }
        }
        off = (uint16_t)(off + 0x10);
        flags = (uint16_t)(flags + 0xc0);
        j = (uint16_t)(j + 1);
      } while ((int16_t)j < (int16_t)drd16(0x0078));
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* read_batch_file_name: 3:0x03e0 (Ghidra FUN_1010_03e0, alias fn_03_03e0).
 * 193 bytes. ENTER 0x18,0; PUSH SI.
 * read_batch_file_name(): FAR CDECL, no arguments, returns 1 when it read a batch file name, else 0. Opens
 * the file named in DS:0x5764 (14:0x3100, mode 0x20), reads it whole into a block of heap 9 (size
 * from 34:0x03b6; 12:0x0232; 14:0x31da), keeps the far pointer in DS:0x15E/0x160 and the end in
 * DS:0x5394/0x5396, and copies the first line (up to LF or the end of the data) over the name at
 * DS:0x5764, overwriting the last copied byte (the CR of CR LF) with NUL. The catch buffer at
 * BP-0x18 is registered in DS:0x6E with 34:0x0800 (a setjmp); the error paths of the file functions
 * throw back to it (34:0x0825) and then take the nonzero branch, which clears the name. The word 9
 * of the call to 12:0x0232 is pushed before the argument of 34:0x03b6 and stays on the stack; the C
 * version moves SP down by that word for the call to 34:0x03b6 and rt_call_far writes the 9 into the
 * same slot for the call to 12:0x0232. The throw cannot resume a C function: see the note in
 * tests/scenarios/test_seg03.c. */
DECOMP(3, 0x03e0, read_batch_file_name, FAR_CDECL)
int16_t read_batch_file_name(void)
{
  FRAME_ENTER(0x18 + 2);
  int16_t result = 0;                                    /* [bp-6] */
  uint16_t jb = (uint16_t)(rt_bp - 0x18);                /* the catch buffer */
  dwr16(0x006e, jb);
  uint16_t thrown = (uint16_t)CALL_ORIG(34, 0x0800, FAR_CDECL, 1, jb);
  if (thrown == 0) {
    CALL_ORIG(14, 0x3100, FAR_CDECL, 2, 0x5764, 0x0020);
    uint16_t sp0 = rt_sp;
    rt_sp = (uint16_t)(sp0 - 2);                         /* PUSH 9: its slot is written by rt_call_far below ({size, 9}) */
    uint16_t size = (uint16_t)CALL_ORIG(34, 0x03b6, FAR_CDECL, 1, drd16(0x09e2));   /* [bp-2] */
    rt_sp = sp0;
    const uint16_t a[2] = {size, 9};
    uint32_t p = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, a, 2);
    dwr16(0x015e, (uint16_t)p);
    dwr16(0x0160, (uint16_t)(p >> 16));
    CALL_ORIG(14, 0x31da, FAR_CDECL, 3, (uint16_t)p, (uint16_t)(p >> 16), size);
    dwr32(0x5394, (drd32(0x015e) & 0xffff0000u) | (uint16_t)(drd16(0x015e) + size));  /* the end */
    uint16_t si = 0x5764;
    uint16_t bx = drd16(0x015e), es = drd16(0x0160);
    while (rd8(es, bx) != 0x0a) {
      if (drd16(0x5394) == bx && drd16(0x5396) == es) break;
      dwr8(si, rd8(es, bx));
      si = (uint16_t)(si + 1);
      dwr16(0x015e, (uint16_t)(drd16(0x015e) + 1));
      bx = drd16(0x015e);
      es = drd16(0x0160);
    }
    dwr16(0x015e, (uint16_t)(drd16(0x015e) + 1));
    dwr8((uint16_t)(si - 1), 0);
    result = 1;
  }
  dwr16(0x006e, 0);
  CALL_ORIG(14, 0x31c2, FAR_CDECL, 0);
  if (result == 0) dwr8(0x5764, 0);
  FRAME_LEAVE();
  return result;
}

/* update_system_colors: 3:0x054c (Ghidra FUN_1010_054c, alias fn_03_054c).
 * 538 bytes. No BP frame; PUSH DI; PUSH SI.
 * update_system_colors(): FAR CDECL, no arguments, returns 1. Releases the eight system colour brushes in
 * DS:0x10, 0x14, 0x16, 0x12, 0x1a, 0x18, 0x1c, 0x1e (8:0xa542), then reads the system colours and
 * makes a brush of each (8:0xa4ae, which returns the brush handle) in the same slots. COLORREFs are
 * cached for COLOR_WINDOW (15) in DS:0x253A, 20 in 0x4166, 16 in 0x267E, 18 in 0x2870, 5 in 0x27EE
 * and 8 in 0x4AC0. The metrics 4, 0x20, 0x21 go to DS:0x5274, 0x266C, 0x266E. The colour in 0x253A
 * is patched into the two resource blocks DS:0xC7C (offsets 0x40C to 0x40E) and DS:0x5A4E (0x41C to
 * 0x41E) as three bytes (B, G, R order). Last, DS:0x54F6 = planes * bits per pixel of the screen. */
static void release_brush(uint16_t slot)
{
  if (drd16(slot) != 0) CALL_ORIG(8, 0xa542, FAR_CDECL, 1, drd16(slot));
}
static uint16_t make_brush(uint32_t colorref)
{
  return (uint16_t)CALL_ORIG(8, 0xa4ae, FAR_CDECL, 2, (uint16_t)colorref, (uint16_t)(colorref >> 16));
}
static void patch_colour(uint16_t handle, uint16_t at)
{
  uint32_t p = GlobalLock(handle);
  uint16_t si = FP_OFF(p), es = FP_SEG(p);
  wr8(es, (uint16_t)(si + at + 2), drd8(0x253a));
  wr8(es, (uint16_t)(si + at + 1), drd8(0x253b));
  wr8(es, (uint16_t)(si + at), drd8(0x253c));
  (void)GlobalUnlock(handle);
}

DECOMP(3, 0x054c, update_system_colors, FAR_CDECL)
uint16_t update_system_colors(void)
{
  uint16_t sp0 = rt_sp;
  rt_sp = (uint16_t)(sp0 - 4);                           /* PUSH DI; PUSH SI */
  release_brush(0x10);
  release_brush(0x14);
  release_brush(0x16);
  release_brush(0x12);
  release_brush(0x1a);
  release_brush(0x18);
  release_brush(0x1c);
  release_brush(0x1e);
  uint32_t c = GetSysColor(0x0f);
  dwr16(0x253a, (uint16_t)c);
  dwr16(0x253c, (uint16_t)(c >> 16));
  dwr16(0x10, make_brush(c));
  c = GetSysColor(0x14);
  dwr16(0x4166, (uint16_t)c);
  dwr16(0x4168, (uint16_t)(c >> 16));
  dwr16(0x14, make_brush(c));
  c = GetSysColor(0x10);
  dwr16(0x267e, (uint16_t)c);
  dwr16(0x2680, (uint16_t)(c >> 16));
  dwr16(0x16, make_brush(c));
  c = GetSysColor(0x12);
  dwr16(0x2870, (uint16_t)c);
  dwr16(0x2872, (uint16_t)(c >> 16));
  dwr16(0x12, make_brush(c));
  c = GetSysColor(0x06);
  dwr16(0x1c, make_brush(c));
  c = GetSysColor(0x01);
  dwr16(0x1e, make_brush(c));
  c = GetSysColor(0x05);
  dwr16(0x27ee, (uint16_t)c);
  dwr16(0x27f0, (uint16_t)(c >> 16));
  dwr16(0x18, make_brush(c));
  c = GetSysColor(0x08);
  dwr16(0x4ac0, (uint16_t)c);
  dwr16(0x4ac2, (uint16_t)(c >> 16));
  dwr16(0x1a, make_brush(c));
  dwr16(0x5274, (uint16_t)GetSystemMetrics(4));
  dwr16(0x266c, (uint16_t)GetSystemMetrics(0x20));
  dwr16(0x266e, (uint16_t)GetSystemMetrics(0x21));
  if (drd16(0x0c7c) != 0) patch_colour(drd16(0x0c7c), 0x40c);
  if (drd16(0x5a4e) != 0) patch_colour(drd16(0x5a4e), 0x41c);
  uint16_t dc = GetDC(0);
  int16_t bits = GetDeviceCaps(dc, 0x0c);
  int16_t planes = GetDeviceCaps(dc, 0x0e);
  dwr16(0x54f6, (uint16_t)((uint32_t)(uint16_t)bits * (uint32_t)(uint16_t)planes));
  (void)ReleaseDC(0, dc);
  rt_sp = sp0;
  return 1;
}

/* filter_key_message: 3:0x0f90 (Ghidra FUN_1010_0f90, alias fn_03_0f90).
 * 954 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * Keyboard filter of the message loop for WM_KEYDOWN (0x100) and WM_KEYUP (0x101): FAR CDECL
 * (hwnd [BP+6], msg [BP+8], key [BP+0xA], lParam [BP+0xC]). Returns 1 when it handled the key
 * (the loop then does not dispatch it), else 0.
 *  - ESC down: destroys the dialog DS:0x14E when it is the active window; else cancels the window
 *    DS:0xBBE with WM_LBUTTONUP; else destroys the window DS:0x177E.
 *  - ESC or Enter up, with DS:0x15A set and the focus a child or grandchild of it: 13:0x0FB8(esc).
 *  - SHIFT with DS:0x150 under the cursor: WM_SETCURSOR to it.
 *  - Key down only, for the editing keys: nothing when the focus is one of the windows that keep the
 *    keyboard (rules apply when DS:0x158 is set). Digits 1 to 6 choose a view (13:0x09AE) unless it is
 *    the one in DS:0x55E; 0, 7, 8, 9 toggle flags (13:0x0902 / 0x09AE); cursor keys go to DS:0x158 as
 *    WM_KEYDOWN; DELETE and BACKSPACE ask 10:0x606A(8) when DS:0x4B74 is 2; "," and "." step the
 *    high nibble of byte 10 of record DS:0x4B7A (or 1) of the table at DS:0x4BF2:0x4BF0 (stride 0x12,
 *    range 0 to 11) and redraw (7:0x153E, 9:0x07E0); "[" and "]" call 32:0x5418. */
DECOMP(3, 0x0f90, filter_key_message, FAR_CDECL)
uint16_t filter_key_message(uint16_t hwnd, uint16_t msg, uint16_t key, uint32_t lParam)
{
  FRAME_ENTER(8 + 4);
  (void)hwnd;
  uint16_t si = key;
  uint16_t di;
  uint16_t ret = 1;
  if (msg == 0x100) {
    if (si == 0x1b && drd16(0x014e) != 0) {
      uint16_t aw = GetActiveWindow();
      if (aw == drd16(0x014e)) {
        (void)DestroyWindow(aw);
        goto done;
      }
    }
    if (si == 0x1b && drd16(0x0bbe) != 0) {
      (void)SendMessage(drd16(0x0bbe), 0x0202, 0, 0);
      goto done;
    }
    if (si == 0x1b && drd16(0x177e) != 0) {
      (void)DestroyWindow(drd16(0x177e));
      goto done;
    }
  } else if (msg == 0x101 && drd16(0x015a) != 0 && (si == 0x1b || si == 0x0d)) {
    uint16_t parent = GetParent(GetFocus());              /* [bp-4] */
    if (parent == drd16(0x015a) || GetParent(parent) == drd16(0x015a)) {
      const uint16_t zoom[] = {(uint16_t)(si == 0x1b ? 1 : 0)};
      (void)rt_call_far(MK_FP(SEG_SEL(13), 0x0fb8), FAR_CDECL, zoom, 1);
    }
  }
  if (si == 0x10 && drd16(0x0150) != 0) {
    uint32_t ptp = MK_FP(rt_ss, (uint16_t)(rt_bp - 4));
    GetCursorPos(ptp);
    uint16_t w = WindowFromPoint(rd32(rt_ss, (uint16_t)(rt_bp - 4)));
    if (drd16(0x0150) == w) (void)SendMessage(w, 0x20, w, 0);
  }
  if (msg != 0x100) {
    ret = 0;
    goto done;
  }
  if (!(si == 8 || si == 0x2e || si == 0x28 || si == 0x26 || si == 0x24 || si == 0x23 ||
        ((int16_t)si >= 0x30 && (int16_t)si <= 0x39) || si == 0xbc || si == 0xbe || si == 0xdb ||
        si == 0xdd)) {
    ret = 0;
    goto done;
  }
  di = GetFocus();
  if (drd16(0x0158) != 0) {
    if (drd16(0x015a) != 0) {
      if (drd16(0x015a) == di || GetParent(di) == drd16(0x015a) || GetParent(GetParent(di)) == drd16(0x015a)) {
        ret = 0;
        goto done;
      }
    }
    for (uint16_t p = 0x5a48; p < 0x5a4e; p = (uint16_t)(p + 2)) {
      if (drd16(p) == di) {
        ret = 0;
        goto done;
      }
    }
    if (drd16(0x539e) == di || drd16(0x5440) == di || drd16(0x59e6) == di || drd16(0x53e0) == di ||
        drd16(0x083a) == di || drd16(0x54fe) == di || drd16(0x539e) == di || drd16(0x54f2) == di) {
      ret = 0;
      goto done;
    }
    if (drd16(0x014e) != 0 && GetDlgItem(drd16(0x014e), 0x10b) == di) {
      ret = 0;
      goto done;
    }
  }
  if ((int16_t)si >= 0x30 && (int16_t)si <= 0x39) {
    uint16_t view;
    uint16_t flag;
    if ((int16_t)si >= 0x31 && (int16_t)si <= 0x36) {
      if ((drd8(0x055e) & 0x0f) == (uint16_t)(si - 0x31)) goto done;
      view = (uint16_t)(si - 0x31);
      flag = 1;
    } else {
      if (si == 0x39) view = 9;
      else if (si == 0x30) view = GetKeyState(0x10) < 0 ? 0x11 : 0x0b;
      else if (si == 0x37) view = 7;
      else view = 8;
      uint16_t r = toolbar_item_is_on(view);
      flag = (uint16_t)(r == 0 ? 1 : 0);
    }
    {
      const uint16_t tog[] = {view, flag};
      (void)rt_call_far(MK_FP(SEG_SEL(13), 0x09ae), FAR_CDECL, tog, 2);
    }
    InvalidateRect(drd16(0x015a), 0, 0);
    goto done;
  }
  if (si == 0x08 || si == 0x2e) {
    if (drd16(0x4b74) == 2) CALL_ORIG(10, 0x606a, FAR_CDECL, 1, 8);
    goto done;
  }
  if (si == 0x28 || si == 0x26 || si == 0x24 || si == 0x23) {
    if (di == drd16(0x54fa)) {
      ret = 0;
      goto done;
    }
    (void)SendMessage(drd16(0x0158), 0x100, si, lParam);
    goto done;
  }
  if (si == 0xbc || si == 0xbe) {
    if (drd16(0x4b74) != 2) goto done;
    if (!((int16_t)drd16(0x4b7a) > 0 || (int16_t)drd16(0x4bee) > 1)) goto done;
    uint16_t rec = (int16_t)drd16(0x4b7a) > 0 ? drd16(0x4b7a) : 1;
    uint16_t es = drd16(0x4bf2);
    uint16_t bx = (uint16_t)((uint16_t)(rec * 0x12) + drd16(0x4bf0) + 0x0a);
    uint16_t nib = (uint16_t)((rd8(es, bx) & 0xf0) >> 4);
    nib = (uint16_t)(si == 0xbc ? nib - 1 : nib + 1);
    if ((int16_t)nib < 0 || (int16_t)nib > 0x0b) goto done;
    uint8_t cl = (uint8_t)(nib << 4);
    uint16_t x = (uint16_t)((uint8_t)(rd8(es, bx) ^ cl) & 0xf0);
    wr16(es, bx, (uint16_t)(rd16(es, bx) ^ x));
    CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
    CALL_ORIG(9, 0x07e0, FAR_CDECL, 2, 0x0000, 0x4220);
    goto done;
  }
  if (si == 0xdb || si == 0xdd) {
    CALL_ORIG(32, 0x5418, FAR_CDECL, 5, 0, 0, 2, 0, (uint16_t)(si == 0xdb ? 0xfffe : 0xffff));
    goto done;
  }
  ret = 0;
done:
  FRAME_LEAVE();
  return ret;
}

/* release_all_resources: 3:0x0766 (Ghidra FUN_1010_0766, alias fn_03_0766).
 * 1300 bytes. ENTER 2,0; PUSH DI; PUSH SI.
 * release_all_resources(): FAR CDECL, no arguments, no result. The program's clean-up at the end of WinMain:
 * releases, in this order, the brush slots (8:0xA542 on each non-zero or, for the unconditional
 * groups, on every slot), the procedure instances (KERNEL.FreeProcInstance), the GDI objects, the
 * cursors (after selecting the arrow, IDC_ARROW = 0x7F00) and icons, every locked resource
 * (GlobalUnlock + FreeResource), the two heap blocks DS:0x96C (heap 4) and DS:0xAAC (heap 2) with
 * 12:0x04d0 (zeroing the pointers), more brushes and GDI objects, and last all twelve heaps
 * (12:0x01b0 on each head in DS:0xD74..0xDA4). The slot addresses are DGROUP offsets, written as
 * literals in the order the original uses them. */
static void rel_brush_u(uint16_t slot) { CALL_ORIG(8, 0xa542, FAR_CDECL, 1, drd16(slot)); }
static void del_obj(uint16_t slot) { (void)DeleteObject(drd16(slot)); }
static void del_obj_if(uint16_t slot) { if (drd16(slot) != 0) (void)DeleteObject(drd16(slot)); }
static void dest_cursor(uint16_t slot) { (void)DestroyCursor(drd16(slot)); }
static void dest_icon(uint16_t slot) { (void)DestroyIcon(drd16(slot)); }
static void free_res(uint16_t slot)
{
  (void)GlobalUnlock(drd16(slot));
  (void)FreeResource(drd16(slot));
}

DECOMP(3, 0x0766, release_all_resources, FAR_CDECL)
void release_all_resources(void)
{
  FRAME_ENTER(2 + 4);
  static const uint16_t cond_brushes[] = {0x10, 0x14, 0x16, 0x12, 0x1a, 0x18, 0x1c, 0x1e, 0x24, 0x26, 0x28, 0x290c, 0x29d4};
  for (uint16_t i = 0; i < (uint16_t)(sizeof cond_brushes / sizeof cond_brushes[0]); i++) release_brush(cond_brushes[i]);
  for (uint16_t p = 0x27f6; p <= 0x27fe; p = (uint16_t)(p + 2)) release_brush(p);
  for (uint16_t p = 0x5742; p < 0x574e; p = (uint16_t)(p + 2)) rel_brush_u(p);
  for (uint16_t p = 0x5754; p < 0x5764; p = (uint16_t)(p + 2)) rel_brush_u(p);
  FreeProcInstance(drd32(0x27be));
  FreeProcInstance(drd32(0x29c2));
  FreeProcInstance(drd32(0x27b2));
  FreeProcInstance(drd32(0x2682));
  FreeProcInstance(drd32(0x53e2));
  FreeProcInstance(drd32(0x53b4));
  if (drd32(0x002a) != 0) FreeProcInstance(drd32(0x002a));
  del_obj(0x543e);
  del_obj(0x253e);
  (void)SetCursor(LoadCursor(0, 0x00007f00));
  static const uint16_t cursors[] = {0x4c80, 0x4d94, 0x54e8, 0x5864, 0x2804, 0x415e, 0x2670, 0x2940, 0x53b2, 0x4ac8, 0x5442};
  for (uint16_t i = 0; i < (uint16_t)(sizeof cursors / sizeof cursors[0]); i++) dest_cursor(cursors[i]);
  del_obj(0x4c74);
  del_obj(0x5a3c);
  del_obj(0x4d98);
  del_obj(0x4174);
  dest_icon(0x0316);
  dest_icon(0x0318);
  dest_icon(0x031a);
  for (uint16_t p = 0x2760; p < 0x276e; p = (uint16_t)(p + 2)) dest_icon(p);
  free_res(0x4b6c);
  free_res(0x26ae);
  free_res(0x5a4e);
  free_res(0x565a);
  free_res(0x2880);
  free_res(0x287e);
  del_obj(0x275c);
  del_obj(0x2884);
  del_obj(0x5a54);
  (void)FreeResource(drd16(0x0c7c));
  for (int16_t i = 0; i < 10; i = (int16_t)(i + 2)) {
    free_res((uint16_t)(0x4d88 + i));
    free_res((uint16_t)(0x5344 + i));
  }
  for (uint16_t p = 0x2808; p < 0x2816; p = (uint16_t)(p + 2)) free_res(p);
  {
    const uint16_t a[3] = {(uint16_t)drd32(0x096c), (uint16_t)(drd32(0x096c) >> 16), 4};
    (void)rt_call_far(MK_FP(SEG_SEL(12), 0x04d0), FAR_CDECL, a, 3);
    dwr32(0x096c, 0);
  }
  {
    const uint16_t a[3] = {(uint16_t)drd32(0x0aac), (uint16_t)(drd32(0x0aac) >> 16), 2};
    (void)rt_call_far(MK_FP(SEG_SEL(12), 0x04d0), FAR_CDECL, a, 3);
    dwr32(0x0aac, 0);
  }
  del_obj(0x54f8);
  del_obj_if(0x03da);
  static const uint16_t brushes_u[] = {0x54fc, 0x59ce, 0x5a5c, 0x5a58, 0x26b0, 0x27d4};
  for (uint16_t i = 0; i < (uint16_t)(sizeof brushes_u / sizeof brushes_u[0]); i++) rel_brush_u(brushes_u[i]);
  release_brush(0x5750);
  rel_brush_u(0x20);
  rel_brush_u(0x22);
  rel_brush_u(0x2546);
  rel_brush_u(0x5380);
  del_obj(0x4d92);
  for (uint16_t p = 0x59e0; p < 0x59e6; p = (uint16_t)(p + 2)) del_obj(p);
  static const uint16_t objs1[] = {0x53ee, 0x5446, 0x6664, 0x553c, 0x293c, 0x5a42, 0x574e, 0x5a60, 0x59f8};
  for (uint16_t i = 0; i < (uint16_t)(sizeof objs1 / sizeof objs1[0]); i++) del_obj(objs1[i]);
  del_obj_if(0x5740);
  static const uint16_t objs2[] = {0x5752, 0x293e, 0x2648, 0x4a5a, 0x4a5c};
  for (uint16_t i = 0; i < (uint16_t)(sizeof objs2 / sizeof objs2[0]); i++) del_obj(objs2[i]);
  for (uint16_t p = 0x292e; p < 0x2938; p = (uint16_t)(p + 2)) del_obj(p);
  del_obj(0x293a);
  del_obj(0x2938);
  for (uint16_t p = 0x0d74; p < 0x0da4; p = (uint16_t)(p + 4)) {
    const uint16_t a[2] = {(uint16_t)drd32(p), (uint16_t)(drd32(p) >> 16)};
    (void)rt_call_far(MK_FP(SEG_SEL(12), 0x01b0), FAR_CDECL, a, 2);
  }
  FRAME_LEAVE();
}

/* ABOUT: 3:0x0ca0 (Ghidra and export name ABOUT, no fn_03 alias).
 * 545 bytes. ENTER 0xC,0; PUSH DI; PUSH SI.
 * ABOUT(hwnd, msg, wParam, lParam): FAR PASCAL dialog procedure of the About box (hwnd [BP+0xE], msg
 * [BP+0xC], wParam [BP+0xA], lParam [BP+6]). A RECT sits at BP-0xC (left, top, right, bottom).
 *   WM_ERASEBKGND (0x14): fill the client rect with the brush DS:0x10; returns 1.
 *   WM_CTLCOLOR (0x19), control type 6: background colour DS:0x253A; returns the brush DS:0x10.
 *   WM_INITDIALOG (0x110): DS:0x5A56 = -11 (first credits line), DS:0x5390 = 0 (pixel offset); the
 *     version string (3:0x0c7a) goes to control 0x401; a 50 ms timer with id 14 is started, its id
 *     kept in DS:0x162; returns 1. The control handle stays pushed on the stack while 3:0x0c7a runs
 *     (it is the first argument of SetWindowText).
 *   WM_COMMAND (0x111): id 1 or 2 stops the timer and ends the dialog (returns 1); id 0x76 opens the
 *     order-information dialog (template 0x61, procedure 3:0x0ec2) and returns 0.
 *   WM_TIMER (0x113): scrolls the credits in control 0x41F by two pixels; when DS:0x5390 reaches the
 *     line height DS:0x267A the offset restarts and the first line advances (wrapping to -11 after
 *     70); the lines DS:0x5A56 .. +9 (strings 0x270 + n for n in 0..0x44, via 8:0x8df4 and drawn by
 *     8:0x9318) are drawn on a clipped DC; the rest of the control is filled with the brush and the
 *     clip region is removed. The DC is released with the dialog's handle (as in the original).
 * Every other message returns 0. */
DECOMP(3, 0x0ca0, ABOUT, FAR_PASCAL)
uint16_t ABOUT(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0xc + 4);
  uint16_t rectp = (uint16_t)(rt_bp - 0xc);
  uint32_t rect = MK_FP(rt_ss, rectp);
  uint16_t ret = 0;
  if (msg == 0x14) {
    GetClientRect(hwnd, rect);
    (void)FillRect(wParam, rect, drd16(0x0010));
    ret = 1;
  } else if (msg == 0x19) {
    if ((uint16_t)(lParam >> 16) == 6) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
  } else if (msg == 0x110) {
    dwr16(0x5a56, 0xfff5);
    dwr16(0x5390, 0);
    uint16_t ctl = GetDlgItem(hwnd, 0x401);
    uint16_t sp0 = rt_sp;
    wr16(rt_ss, (uint16_t)(sp0 - 2), ctl);                /* PUSH AX stays under the near call */
    rt_sp = (uint16_t)(sp0 - 2);
    uint16_t str = (uint16_t)rt_call_far(MK_FP(SEG_SEL(3), 0x0c7a), FAR_CDECL, NULL, 0);
    rt_sp = sp0;
    SetWindowText(ctl, dsfp(str));
    dwr16(0x0162, SetTimer(hwnd, 0x000e, 0x0032, 0));
    ret = 1;
  } else if (msg == 0x111) {
    if (wParam == 1 || wParam == 2) {
      (void)KillTimer(hwnd, drd16(0x0162));
      dwr16(0x0162, 0);
      (void)EndDialog(hwnd, 1);
      ret = 1;
    } else if (wParam == 0x76) {
      uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(3), 0x0ec2), drd16(0x54f4));
      (void)DialogBox(drd16(0x54f4), 0x00000061, hwnd, pi);
      FreeProcInstance(pi);
    }
  } else if (msg == 0x113) {
    uint16_t item = GetDlgItem(hwnd, 0x041f);              /* SI */
    uint16_t lineh = drd16(0x267a);
    dwr16(0x5390, (uint16_t)(drd16(0x5390) + 2));
    if (!((int16_t)drd16(0x5390) < (int16_t)lineh)) {
      dwr16(0x5390, 0);
      dwr16(0x5a56, (uint16_t)(drd16(0x5a56) + 1));
      if ((int16_t)drd16(0x5a56) > 0x46) dwr16(0x5a56, 0xfff5);
    }
    GetClientRect(item, rect);
    uint16_t dc = GetDC(item);                             /* DI */
    (void)SelectObject(dc, drd16(0x2930));
    (void)SetBkMode(dc, 2);
    (void)SetBkColor(dc, drd32(0x253a));
    (void)SetTextColor(dc, drd32(0x2870));
    (void)IntersectClipRect(dc, 0, 0, BP_LOCAL(int16_t, -8), BP_LOCAL(int16_t, -6));
    BP_LOCAL(uint16_t, -0xa) = (uint16_t)(BP_LOCAL(uint16_t, -0xa) - drd16(0x5390));
    BP_LOCAL(uint16_t, -6) = (uint16_t)(BP_LOCAL(uint16_t, -0xa) + drd16(0x267a));
    BP_LOCAL(uint16_t, -2) = drd16(0x5a56);
    if ((int16_t)(uint16_t)(drd16(0x5a56) + 10) > (int16_t)drd16(0x5a56)) {
      uint16_t line = BP_LOCAL(uint16_t, -2);              /* SI */
      for (;;) {
        if ((int16_t)line >= 0 && (int16_t)line < 0x45) {
          uint16_t sp0 = rt_sp;
          wr16(rt_ss, (uint16_t)(sp0 - 2), 0xffff);        /* the length argument, pushed first */
          rt_sp = (uint16_t)(sp0 - 2);
          uint16_t strp = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, (uint16_t)(line + 0x270));
          CALL_ORIG(8, 0x9318, FAR_CDECL, 3, dc, rectp, strp);
          rt_sp = sp0;
        } else if ((int16_t)line >= 0x45) {
          break;
        }
        OffsetRect(rect, 0, (int16_t)drd16(0x267a));
        line = (uint16_t)(line + 1);
        if (!((int16_t)(uint16_t)(drd16(0x5a56) + 10) > (int16_t)line)) break;
      }
    }
    BP_LOCAL(uint16_t, -6) = 1000;
    (void)FillRect(dc, rect, drd16(0x0010));
    (void)SelectClipRgn(dc, 0);
    (void)ReleaseDC(hwnd, dc);
  }
  FRAME_LEAVE();
  return ret;
}

/* WinMain: 3:0x0000 (Ghidra FUN_1010_0000, no fn_03 alias: the C name was always WinMain).
 * 991 bytes including the jump table at 0x00FA..0x0169. ENTER 0x18,0;
 * PUSH DI; PUSH SI.
 * WinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow): FAR PASCAL (hInstance [BP+0xE],
 * hPrevInstance [BP+0xC], lpCmdLine [BP+8], nCmdShow [BP+6]); returns the WM_QUIT exit code in AX.
 * The MSG for the loop is at BP-0x18 (hwnd, message, wParam, lParam, time, pt). Steps:
 *  1. DS:0x54F4 = hInstance; DS:0x5764 (the game file name) = ""; the option word DS:0x251A = 0; the
 *     44 bytes at DS:0x53F0 and the 10 bytes at DS:0x416A are cleared; DS:0x416C = 1.
 *  2. First instance only (hPrevInstance = 0): 4:0x0000 registers the window classes; zero shows the
 *     fatal box 8:0x8D94(0x18, 0x10) and returns 0. Then 8:0x86CC(GetTickCount) seeds the random
 *     generator, 1:0x0000 starts the game (zero: return 0 without a message), 3:0x054c makes the
 *     brushes and 1:0x0AF8(nCmdShow) creates the windows (zero: the fatal box, return 0).
 *  3. The command line: a word that does not start with '-' or '/' is the game file name (copied to
 *     DS:0x5764, DS:0x251A |= 3). Otherwise each letter after the dash is an option (letters are
 *     matched by the 56-entry jump table at 0xFA, upper and lower case alike): A flag 0x400; B <file>
 *     reads a batch file name (3:0x03e0; success sets 0x20B); C sets bit 1 of the option word when a
 *     file name is present; D takes letters (P 0x1000, F 0x800, M 0x2000); G <n> sets bit 3 and
 *     DS:0x2526 = n - 1 (n capped at 1000); H sets DS:0x795 bit 1; P <name> keeps up to 15 characters
 *     in DS:0x2560 and 8:0xb28a turns it into the far value DS:0x320; T 0x10; W 4; X sets DS:0x792
 *     bit 6.
 *  4. PostMessage(DS:0x2806, 0x464) starts the game; then GetMessage / TranslateAccelerator (the
 *     table DS:0x4C72 when DS:0x312 is set, else DS:0x2666 unless the window is iconic) /
 *     TranslateMessage; key down and up go through 3:0x0f90, WM_CHAR through 3:0x0f3a (a nonzero
 *     result swallows the message); everything else is dispatched.
 *  5. At WM_QUIT the clean-up 3:0x0766 runs and wParam is returned.
 * The registered callees reached with a frame (3:0x054c, 0x03e0, 0x0766, 0x0f90, 0x0f3a) go through
 * rt_call_far. The jump table is data in the code segment and has no counterpart in C. */
DECOMP(3, 0x0000, WinMain, FAR_PASCAL)
uint16_t WinMain(uint16_t hInstance, uint16_t hPrevInstance, uint32_t lpCmdLine, uint16_t nCmdShow)
{
  FRAME_ENTER(0x18 + 4);
  uint16_t ret = 0;
  uint16_t es = FP_SEG(lpCmdLine);
  uint16_t si = FP_OFF(lpCmdLine);
  uint16_t msgp = (uint16_t)(rt_bp - 0x18);
  uint32_t msg = MK_FP(rt_ss, msgp);
  dwr16(0x54f4, hInstance);
  dwr8(0x5764, 0);
  dwr16(0x251a, 0);
  for (uint16_t i = 0; i < 11; i++) dwr32((uint16_t)(0x53f0 + 4 * i), 0);
  for (uint16_t i = 0; i < 5; i++) dwr16((uint16_t)(0x416a + 2 * i), 0);
  dwr16(0x416c, 1);
  if (hPrevInstance == 0 && (uint16_t)rt_call_far(MK_FP(SEG_SEL(4), 0x0000), FAR_CDECL, NULL, 0) == 0) goto fatal;
  {
    uint32_t tick = GetTickCount();
    CALL_ORIG(8, 0x86cc, FAR_CDECL, 2, (uint16_t)tick, (uint16_t)(tick >> 16));
  }
  if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(1), 0x0000), FAR_CDECL, NULL, 0) == 0) goto out;
  if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(3), 0x054c), FAR_CDECL, NULL, 0) == 0) goto fatal;
  {
    const uint16_t show[1] = {nCmdShow};
    if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(1), 0x0af8), FAR_CDECL, show, 1) == 0) goto fatal;
  }
  /* the command line */
  while (rd8(es, si) != 0) {
    if (rd8(es, si) == 0x20) {
      do si = (uint16_t)(si + 1); while (rd8(es, si) == 0x20);
    }
    if (rd8(es, si) != 0x2d && rd8(es, si) != 0x2f) {
      uint16_t di = 0x5764;
      while (rd8(es, si) != 0 && rd8(es, si) != 0x20) {
        dwr8(di, rd8(es, si));
        di = (uint16_t)(di + 1);
        si = (uint16_t)(si + 1);
      }
      dwr8(di, 0);
      dwr8(0x251a, (uint8_t)(drd8(0x251a) | 3));
      continue;
    }
    si = (uint16_t)(si + 1);
    while (rd8(es, si) != 0 && rd8(es, si) != 0x20) {
      uint16_t d = (uint16_t)((int16_t)(int8_t)rd8(es, si) - 0x41);   /* CBW; SUB AX,0x41 */
      if (d <= 0x37) {
        switch ((uint8_t)(d + 0x41)) {
        case 'A': case 'a':
          dwr8(0x251b, (uint8_t)(drd8(0x251b) | 4));
          break;
        case 'B': case 'b': {
          si = (uint16_t)(si + 1);
          while (rd8(es, si) == 0x20) si = (uint16_t)(si + 1);
          uint16_t di = 0x5764;
          while (rd8(es, si) != 0 && rd8(es, si) != 0x20) {
            dwr8(di, rd8(es, si));
            di = (uint16_t)(di + 1);
            si = (uint16_t)(si + 1);
          }
          dwr8(di, 0);
          si = (uint16_t)(si - 1);
          if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(3), 0x03e0), FAR_CDECL, NULL, 0) != 0)
            dwr16(0x251a, (uint16_t)(drd16(0x251a) | 0x020b));
          break;
        }
        case 'C': case 'c': {
          uint8_t v = (uint8_t)(drd8(0x5764) >= 1 ? 2 : 0);
          uint16_t x = (uint16_t)((uint8_t)(v ^ drd8(0x251a)) & 2);
          dwr16(0x251a, (uint16_t)(drd16(0x251a) ^ x));
          break;
        }
        case 'D': case 'd':
          si = (uint16_t)(si + 1);
          while (rd8(es, si) != 0 && rd8(es, si) != 0x20) {
            uint8_t c = rd8(es, si);
            if (c == 'p' || c == 'P') dwr8(0x251b, (uint8_t)(drd8(0x251b) | 0x10));
            else if (c == 'F' || c == 'f') dwr8(0x251b, (uint8_t)(drd8(0x251b) | 0x08));
            else if (c == 'M' || c == 'm') dwr8(0x251b, (uint8_t)(drd8(0x251b) | 0x20));
            si = (uint16_t)(si + 1);
          }
          si = (uint16_t)(si - 1);
          break;
        case 'G': case 'g': {
          dwr8(0x251a, (uint8_t)(drd8(0x251a) | 8));
          int16_t cx = 0;
          while ((int8_t)rd8(es, (uint16_t)(si + 1)) >= 0x30 && (int8_t)rd8(es, (uint16_t)(si + 1)) <= 0x39) {
            si = (uint16_t)(si + 1);
            cx = (int16_t)((uint16_t)(int16_t)(int8_t)rd8(es, si) + (uint16_t)((uint16_t)cx * 10u) - 0x30u);
            if (cx <= 0x3e8) continue;
            cx = 0x3e8;
            while ((int8_t)rd8(es, (uint16_t)(si + 1)) >= 0x30 && (int8_t)rd8(es, (uint16_t)(si + 1)) <= 0x39)
              si = (uint16_t)(si + 1);
            break;
          }
          if (cx > 0) dwr16(0x2526, (uint16_t)(cx - 1));
          break;
        }
        case 'H': case 'h':
          dwr8(0x0795, (uint8_t)(drd8(0x0795) | 2));
          break;
        case 'P': case 'p': {
          si = (uint16_t)(si + 1);
          while (rd8(es, si) == 0x20) si = (uint16_t)(si + 1);
          uint16_t di = 0x2560;
          while (rd8(es, si) != 0 && rd8(es, si) != 0x20 && di < 0x256f) {
            dwr8(di, rd8(es, si));
            di = (uint16_t)(di + 1);
            si = (uint16_t)(si + 1);
          }
          dwr8(di, 0);
          si = (uint16_t)(si - 1);
          uint32_t r = CALL_ORIG(8, 0xb28a, FAR_CDECL, 1, 0x2560);
          dwr16(0x0320, (uint16_t)r);
          dwr16(0x0322, (uint16_t)(r >> 16));
          break;
        }
        case 'T': case 't':
          dwr8(0x251a, (uint8_t)(drd8(0x251a) | 0x10));
          break;
        case 'W': case 'w':
          dwr8(0x251a, (uint8_t)(drd8(0x251a) | 4));
          break;
        case 'X': case 'x':
          dwr8(0x0792, (uint8_t)(drd8(0x0792) | 0x40));
          break;
        default:
          break;
        }
      }
      si = (uint16_t)(si + 1);
    }
  }
  /* the message loop */
  (void)PostMessage(drd16(0x2806), 0x0464, 0, 0);
  while (GetMessage(msg, 0, 0, 0) != 0) {
    bool dispatch = true;
    if (drd16(0x0312) != 0) {
      if (TranslateAccelerator(drd16(0x2806), drd16(0x4c72), msg) != 0) continue;
      (void)TranslateMessage(msg);
    } else {
      if (IsIconic(drd16(0x2806)) == 0 && TranslateAccelerator(drd16(0x2806), drd16(0x2666), msg) != 0) continue;
      (void)TranslateMessage(msg);
      const uint16_t m = rd16(rt_ss, (uint16_t)(rt_bp - 0x16)); /* re-read after the translation, as the original does */
      const uint16_t hw = rd16(rt_ss, (uint16_t)(rt_bp - 0x18));
      const uint16_t wp = rd16(rt_ss, (uint16_t)(rt_bp - 0x14));
      const uint32_t lp = rd32(rt_ss, (uint16_t)(rt_bp - 0x12));
      if (m == 0x100 || m == 0x101) {
        const uint16_t a[5] = {hw, m, wp, (uint16_t)lp, (uint16_t)(lp >> 16)};
        if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(3), 0x0f90), FAR_CDECL, a, 5) != 0) dispatch = false;
      }
      if (dispatch && m == 0x102) {
        const uint16_t a[4] = {hw, wp, (uint16_t)lp, (uint16_t)(lp >> 16)};
        if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(3), 0x0f3a), FAR_CDECL, a, 4) != 0) dispatch = false;
      }
    }
    if (dispatch) (void)DispatchMessage(msg);
  }
  (void)rt_call_far(MK_FP(SEG_SEL(3), 0x0766), FAR_CDECL, NULL, 0);
  ret = rd16(rt_ss, (uint16_t)(rt_bp - 0x14));
  goto out;
fatal:
  CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x18, 0x10);
  ret = 0;
out:
  FRAME_LEAVE();
  return ret;
}
