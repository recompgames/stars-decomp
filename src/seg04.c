/* Decompiled code segment 4 (NE segment 4, selector 0x0020).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg04.c; Ghidra's pseudo-C in re/decomp/seg04 is only a reading aid.
 *
 * Segment 4 is the main window of the game (FRAMEWNDPROC and its handlers), the title window
 * (TITLEWNDPROC), the host dialogs and the file/menu helpers:
 *   4:0x0000  register_window_classes: register the eleven window classes of the program
 *   4:0x02ca  update_caption_and_panels: the main window caption, then create or invalidate the panels
 *   4:0x0528  FRAMEWNDPROC, the main window procedure
 *   4:0x143a  drag_divider_bars: draw and limit the divider bars while a divider is dragged
 *   4:0x18b0  hit_test_dividers: which divider squares a point is on, and the cursor for it
 *   4:0x197a  sync_selected_object: keep the selected-object record in step with the game
 *   4:0x1af2  encode_serial: encode a key and a registration serial as a text string
 *   4:0x1c78  decode_serial: the inverse of encode_serial, with its checks
 *   4:0x1e5e  select_home_object: select the current player's home planet, else first fleet
 *   4:0x1f40  frame_on_command: the WM_COMMAND handler of the main window
 *   4:0x379a  update_main_menu: the recent-games list and the enabled / checked menu items
 *   4:0x39f8  run_computer_player_turns: once per turn, run the turns of the flagged players
 *   4:0x3b30  get_main_submenu: GetSubMenu of the main menu, skipping a maximised child's system menu
 *   4:0x3b66  open_game_file: choose (GetOpenFileName) and load a game file
 *   4:0x3ecc  check_game_file: is this a good game file of the right version
 *   4:0x4090  run_host_mode: the host-mode dialog loop
 *   4:0x41f8  paint_host_player_list: paint the player list of the host-mode dialog
 *   4:0x451c  refresh_turn_states_as_host: refresh_turn_states with no current player
 *   4:0x4580  refresh_turn_states: the turn-file state of every player
 *   4:0x467c  HOSTMODEDIALOG, the host-mode dialog procedure
 *   4:0x4cec  HOSTOPTIONSDIALOG, the host options dialog procedure
 *   4:0x4db8  host_options_paint_nop: an empty function (RETF)
 *   4:0x4dba  host_timer_proc: the timer procedure of host mode
 *   4:0x4fd6  get_normal_window_rect: GetWindowPlacement's normal rectangle as left, top, width, height
 *   4:0x500c  format_window_state: format a window's state letter and rectangle
 *   4:0x506c  save_settings: write the settings to the INI file
 *   4:0x5d98  layout_panels: lay out the child windows of the main window
 *   4:0x60f6  TITLEWNDPROC, the title window procedure
 * Names: the export names FRAMEWNDPROC, HOSTMODEDIALOG, HOSTOPTIONSDIALOG and TITLEWNDPROC are kept;
 * Ghidra's other names are all generic (FUN_1018_<off>), so the C names describe what each function
 * does (CONVENTIONS rule f). host_options_paint_nop is a placeholder for an empty function. The old
 * fn_04_<off> names are kept as aliases in the comment above each function.
 *
 * Every function that calls anything mirrors its original frame (CONVENTIONS rule d): FRAME_ENTER(n + 2 *
 * pushes) with n the ENTER size (or 0 after PUSH BP; MOV BP,SP) and one push for each PUSH SI / PUSH DI.
 * Leaves that never expose a stack address use ordinary C locals. Near calls inside the segment are
 * PUSH CS; CALL near, i.e. far calls with a 4-byte return address; a callee that mirrors a frame is
 * reached through rt_call_far. The callees in other segments that are not decompiled yet: CALL_ORIG
 * (CDECL: the arguments in declaration order, i.e. the last one pushed first). */
#include <stdio.h>
#include <stdlib.h>
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
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
/* A far pointer to the stack local at BP + o (SS = DGROUP at every call), and its near offset. */
static inline uint32_t bpfp(int16_t o) { return MK_FP(rt_ss, (uint16_t)(rt_bp + o)); }
static inline uint16_t bpoff(int16_t o) { return (uint16_t)(rt_bp + o); }
/* PUSH word / dword on the emulated stack (the variable arguments of _wsprintf and the like). */
static inline void pushw(uint16_t v) {
  rt_sp = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, rt_sp, v);
}
static inline void pushd(uint32_t v) {
  pushw((uint16_t)(v >> 16));
  pushw((uint16_t)v);
}
static inline void popw(uint16_t n) { rt_sp = (uint16_t)(rt_sp + 2 * n); }
/* A by-name call that can run guest code (a dialog or window procedure, original machine code in the diff tests):
 * the original has pushed the call's argument bytes and the 4-byte far return address, so the procedure runs that
 * many bytes lower than the C frame's SP; rule d. */
#define API_SP(argbytes, expr)                                  \
  ({                                                            \
    uint16_t api_sp_ = rt_sp;                                   \
    rt_sp = (uint16_t)(api_sp_ - (uint16_t)(argbytes) - 4u);    \
    __typeof__(expr) api_r_ = (expr);                           \
    rt_sp = api_sp_;                                            \
    api_r_;                                                     \
  })
/* The call (seg 4, off) of a function of this segment with nw argument words (PUSH CS; CALL near). */
static inline uint32_t call4(uint16_t off, const uint16_t *a, uint16_t nw) {
  return rt_call_far(MK_FP(SEG_SEL(4), off), FAR_CDECL, a, nw);
}

/* host_options_paint_nop: 4:0x4db8 (Ghidra FUN_1018_4db8, alias fn_04_4db8).
 * 1 byte: RETF. The caller 4:0x4cec (HOSTOPTIONSDIALOG) calls it and
 * ignores AX. The name is a placeholder: the function does nothing, and only its one call site (the
 * WM_PAINT case of HOSTOPTIONSDIALOG) gives it a meaning. */
DECOMP(4, 0x4db8, host_options_paint_nop, FAR_CDECL)
void host_options_paint_nop(void)
{
}

/* get_main_submenu: 4:0x3b30 (Ghidra FUN_1018_3b30, alias fn_04_3b30).
 * 53 bytes. PUSH BP; MOV BP,SP; PUSH SI. FAR CDECL (hwnd [BP+6],
 * pos [BP+8]). Returns the pop-up menu GetSubMenu(GetMenu(hwnd), pos + 1) when the window DS:0x152
 * exists and is maximised (the system-menu icon of the maximised child is item 0), else
 * GetSubMenu(GetMenu(hwnd), pos). AX only. */
DECOMP(4, 0x3b30, get_main_submenu, FAR_CDECL)
uint16_t get_main_submenu(uint16_t hwnd, int16_t pos)
{
  FRAME_ENTER(0 + 2);
  uint16_t si;
  if (drd16(0x0152) != 0 && IsZoomed(drd16(0x0152)) != 0)
    si = 1;
  else
    si = 0;
  uint16_t menu = GetMenu(hwnd);
  uint16_t r = GetSubMenu(menu, (int16_t)(uint16_t)(si + (uint16_t)pos));
  FRAME_LEAVE();
  return r;
}

/* get_normal_window_rect: 4:0x4fd6 (Ghidra FUN_1018_4fd6, alias fn_04_4fd6).
 * 54 bytes. ENTER 0x16,0; PUSH DI; PUSH SI. FAR CDECL (hwnd [BP+6],
 * rect [BP+8], a near pointer to 4 words). GetWindowPlacement into a WINDOWPLACEMENT at BP-0x16
 * (length 0x16), copies its rcNormalPosition (BP-8, 8 bytes) to DS:rect and turns it into
 * (left, top, width, height). Returns nothing (AX is left over; the caller 4:0x500c overwrites it). */
DECOMP(4, 0x4fd6, get_normal_window_rect, FAR_CDECL)
void get_normal_window_rect(uint16_t hwnd, uint16_t rect)
{
  FRAME_ENTER(0x16 + 4);
  L16(-0x16) = 0x16;
  (void)GetWindowPlacement(hwnd, bpfp(-0x16));
  /* MOVSD x2 from SS:[BP-8] to DS:[rect] */
  for (uint16_t k = 0; k < 8; k++) dwr8((uint16_t)(rect + k), rd8(rt_ss, bpoff((int16_t)(-8 + k))));
  dwr16((uint16_t)(rect + 4), (uint16_t)(drd16((uint16_t)(rect + 4)) - drd16(rect)));
  dwr16((uint16_t)(rect + 6), (uint16_t)(drd16((uint16_t)(rect + 6)) - drd16((uint16_t)(rect + 2))));
  FRAME_LEAVE();
}

/* format_window_state: 4:0x500c (Ghidra FUN_1018_500c, alias fn_04_500c).
 * 95 bytes. ENTER 0xa,0; PUSH SI. FAR CDECL (unused [BP+6], hwnd
 * [BP+8]). Formats the window state into the DS:0x5866 text buffer: letter 'M' when the window is
 * maximised, else 'R' (normal) or 'I' (iconic) (IsIconic 0 gives 0x52, else 0x49), then the rectangle
 * of 4:0x4fd6 (left, top, width, height) and the format string 0x30c of 8:0x8df4, as
 * _wsprintf(DS:0x5866, fmt, letter, left, top, width, height) (the letter word is sign-extended,
 * CBW). AX of the function is the _wsprintf result. */
DECOMP(4, 0x500c, format_window_state, FAR_CDECL)
uint16_t format_window_state(uint16_t unused, uint16_t hwnd)
{
  FRAME_ENTER(0xa + 2);
  (void)unused;
  uint8_t letter;
  if (IsZoomed(hwnd) != 0)
    letter = 0x4d;
  else
    letter = (uint8_t)(IsIconic(hwnd) == 0 ? 0x52 : 0x49);
  L8(-1) = letter;
  uint16_t a[2] = {hwnd, bpoff(-0xa)};
  (void)call4(0x4fd6, a, 2);
  pushd(L32(-6));
  pushd(L32(-0xa));
  pushw((uint16_t)(int16_t)(int8_t)L8(-1)); /* MOV AL,[BP-1]; CBW; PUSH AX */
  uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x030c);
  int16_t r = _wsprintf(dsfp(0x5866), dsfp(fmt), MK_FP(rt_ss, rt_sp));
  popw(6);
  FRAME_LEAVE();
  return (uint16_t)r;
}

/* hit_test_dividers: 4:0x18b0 (Ghidra FUN_1018_18b0, alias fn_04_18b0).
 * 201 bytes. ENTER 2,0; PUSH DI. A leaf (ordinary C locals). FAR
 * CDECL (x [BP+6], y [BP+8], out [BP+0xa], a near pointer or 0). The 8-pixel divider bars that
 * layout_panels (4:0x5d98) leaves between the panels: the vertical bar at x = DS:0x264e and the
 * horizontal bars at y = DS:0x2650 and y = DS:0x2652. `l` says x is on the vertical bar
 * [0x264e, 0x264e + 8), `di` that the point is on the bar at y = 0x2650 (only x < 0x264e + 8 is tested
 * on x), `dx` that it is on the bar at y = 0x2652 (x >= 0x264e when the mode word DS:0x255e is 1 or 2,
 * else x < 0x264e + 8). The result AX is the cursor handle (a LoadCursor result, see the cursors[]
 * slots in src/seg01.c; the callers pass it to SetCursor) chosen from DS:0x2670, DS:0x53b2 and DS:0x2940, or 0
 * when the point is on no bar, and DS:out (when not 0) gets l + 2 * di + 4 * dx. All compares are signed (JL, JLE, JG)
 * on 16-bit values, and the "+ 8" wraps. */
DECOMP(4, 0x18b0, hit_test_dividers, FAR_CDECL)
uint16_t hit_test_dividers(int16_t x, int16_t y, uint16_t out)
{
  int16_t c0 = (int16_t)drd16(0x264e);
  int16_t c1 = (int16_t)drd16(0x2650);
  int16_t c2 = (int16_t)drd16(0x2652);
  uint16_t cx = 0, l, di, dx;
  if (x < c0)
    l = 0;
  else if ((int16_t)(uint16_t)(c0 + 8) <= x)
    l = 0;
  else
    l = 1;
  if ((int16_t)(uint16_t)(c0 + 8) <= x)
    di = 0;
  else if (y < c1)
    di = 0;
  else if ((int16_t)(uint16_t)(c1 + 8) <= y)
    di = 0;
  else
    di = 1;
  {
    /* DEC AX; JL; JO; DEC AX; JLE: the mode word is 1 or 2 */
    int16_t v = (int16_t)drd16(0x255e);
    bool mode12 = (v == 1 || v == 2);
    if (!mode12) {
      if ((int16_t)(uint16_t)(c0 + 8) <= x)
        dx = 0;
      else if (y < c2)
        dx = 0;
      else if ((int16_t)(uint16_t)(c2 + 8) <= y)
        dx = 0;
      else
        dx = 1;
    } else {
      if (x < c0)
        dx = 0;
      else if (y < c2)
        dx = 0;
      else if ((int16_t)(uint16_t)(c2 + 8) > y)
        dx = 1;
      else
        dx = 0;
    }
  }
  if (l != 0) {
    if (di != 0 || dx != 0)
      cx = drd16(0x53b2);
    else
      cx = drd16(0x2670);
  } else if (di != 0 || dx != 0) {
    cx = drd16(0x2940);
  }
  if (out != 0) dwr16(out, (uint16_t)(4 * dx + 2 * di + l));
  return cx;
}

/* refresh_turn_states: 4:0x4580 (Ghidra FUN_1018_4580, alias fn_04_4580).
 * 251 bytes. ENTER 0xa,0; PUSH DI; PUSH SI. FAR CDECL, no argument.
 * Refreshes the per-player status words DS:0x6666 + 2 * i (i < DS:0x78, the player count): 0 when the
 * player's row (DS:0x5a64 + 0xc0 * i; +7 is the high byte of the status word at +6, and its bit 1
 * is the computer-player flag: 4:0x39f8 calls 17:0x0000 for each player with that bit set, and
 * 17:0x0000 dispatches on the top three bits of the status word) says the player is a computer player
 * or 9:0x7c26(i, &err) fails, otherwise the state of the player's turn file: 1 or 2 (not submitted;
 * 2 when DS:0x792 bit 7 is set), 0xffff (row +0x54 bit 0 set: meaning not verified; 17:0x0000 skips
 * generating that player's turn when the bit is set in the row of player DS:0x14c) or 3, 4, 5 (the
 * error codes 0, 0x1c, 0x1d of 9:0x7c26). BP-6 counts the states 1 and 2 (the return value), BP-8
 * the computer players; DS:0x76e is 1 and bit 1 of DS:0x790 is set while it runs. When a word changed
 * (or the stamp DS:0x192 was 0) the stamp DS:0x192/0x194 becomes GetTickCount(). Finally bit 4 of
 * DS:0x792 is set when every player is a computer player. */
DECOMP(4, 0x4580, refresh_turn_states, FAR_CDECL)
uint16_t refresh_turn_states(void)
{
  FRAME_ENTER(0xa + 4);
  uint16_t di, si;
  dwr16(0x076e, 1);
  dwr8(0x0790, (uint8_t)(drd8(0x0790) | 2));
  L32(-8) = 0;
  L16(-4) = 0;
  if ((int16_t)drd16(0x0078) > 0) {
    di = 0x5a6a;
    si = 0x6666;
    for (;;) {
      L16(-2) = drd16(si);
      L16(-0xa) = 0;
      /* `dead` is misnamed: status bit 1 of row +7 is the computer-player flag (see above) */
      bool dead = (drd8((uint16_t)(di + 1)) & 2) != 0;
      if (!dead) {
        uint16_t ax = (uint16_t)CALL_ORIG(9, 0x7c26, FAR_CDECL, 2, L16(-4), bpoff(-0xa));
        if (ax != 0) goto L4610;
        if (L16(-0xa) != 0) {
          uint16_t e = (uint16_t)(L16(-0xa) - 0x1c);
          if (e == 0)
            dwr16(si, 4);
          else if ((uint16_t)(e - 1) == 0)
            dwr16(si, 5);
          else
            dwr16(si, 3);
        } else if ((drd8((uint16_t)(di + 0x4e)) & 1) != 0) {
          dwr16(si, 0xffff);
        } else if ((drd8(0x0792) & 0x80) != 0) {
          dwr16(si, 2);
          L16(-6) = (uint16_t)(L16(-6) + 1);
        } else {
          dwr16(si, 1);
          L16(-6) = (uint16_t)(L16(-6) + 1);
        }
        goto L461d;
      }
    L4610:
      if ((drd8((uint16_t)(di + 1)) & 2) != 0) L16(-8) = (uint16_t)(L16(-8) + 1);
      dwr16(si, 0);
    L461d:
      if (drd32(0x0192) == 0 || L16(-2) != drd16(si)) {
        uint32_t t = GetTickCount();
        dwr16(0x0192, (uint16_t)t);
        dwr16(0x0194, (uint16_t)(t >> 16));
      }
      di = (uint16_t)(di + 0xc0);
      si = (uint16_t)(si + 2);
      L16(-4) = (uint16_t)(L16(-4) + 1);
      if (!((int16_t)L16(-4) < (int16_t)drd16(0x0078))) break;
    }
  }
  dwr8(0x0790, (uint8_t)(drd8(0x0790) & 0xfd));
  {
    uint16_t ax = (L16(-8) == drd16(0x0078)) ? 1 : 0;
    uint8_t al = (uint8_t)(ax << 4);
    al = (uint8_t)(al ^ drd8(0x0792));
    dwr16(0x0792, (uint16_t)(drd16(0x0792) ^ (al & 0x10)));
  }
  dwr16(0x076e, 0);
  uint16_t r = L16(-6);
  FRAME_LEAVE();
  return r;
}

/* refresh_turn_states_as_host: 4:0x451c (Ghidra FUN_1018_451c, alias fn_04_451c).
 * 99 bytes. ENTER 4,0; PUSH DI; PUSH SI. FAR CDECL, no argument.
 * Runs 4:0x4580 with the current player DS:0x14c set to 0xffff and the flags of DS:0x790 forced
 * (bit 3 set, bit 1 clear), then puts bit 1 and bit 3 of DS:0x790 and the player back. Returns the
 * result of 4:0x4580. "as_host" stands for "with no current player" (DS:0x14c = 0xffff). */
DECOMP(4, 0x451c, refresh_turn_states_as_host, FAR_CDECL)
uint16_t refresh_turn_states_as_host(void)
{
  FRAME_ENTER(4 + 4);
  uint16_t si = (uint16_t)((drd16(0x0790) & 8) >> 3);
  uint16_t di = (uint16_t)((drd16(0x0790) & 2) >> 1);
  L16(-2) = drd16(0x014c);
  dwr16(0x014c, 0xffff);
  dwr8(0x0790, (uint8_t)(drd8(0x0790) | 8));
  dwr8(0x0790, (uint8_t)(drd8(0x0790) & 0xfd));
  L16(-4) = (uint16_t)call4(0x4580, NULL, 0);
  {
    uint16_t ax = (uint16_t)(di & 0xff);
    uint8_t al = (uint8_t)(ax + ax);
    al = (uint8_t)(al ^ drd8(0x0790));
    ax = (uint16_t)(al & 2);
    dwr16(0x0790, (uint16_t)(drd16(0x0790) ^ ax));
  }
  {
    uint8_t al = drd8(0x0790);
    si = (uint16_t)(si << 3);
    al = (uint8_t)(al ^ (uint8_t)si);
    dwr16(0x0790, (uint16_t)(drd16(0x0790) ^ (al & 8)));
  }
  dwr16(0x014c, L16(-2));
  uint16_t r = L16(-4);
  FRAME_LEAVE();
  return r;
}

/* select_home_object: 4:0x1e5e (Ghidra FUN_1018_1e5e, alias fn_04_1e5e).
 * 226 bytes. ENTER 8,0; PUSH DI; PUSH SI. FAR CDECL, no argument.
 * Selects the current player's home: the planet 7:0x01e4(player row +8) when it is the current
 * player's (record +2 = DS:0x14c), else the first planet of the table DS:0xb6 (stride 0x38, count
 * DS:0x4d96) that belongs to the player: 9:0x2ed8(0, planet id) and returns 1. Failing that, the
 * first fleet of the table DS:0xba (DS:0x553a far pointers) of the player: 10:0x26b2(0, fleet id) and
 * returns 1. Returns 0 when there is neither. */
DECOMP(4, 0x1e5e, select_home_object, FAR_CDECL)
uint16_t select_home_object(void)
{
  FRAME_ENTER(8 + 4);
  uint16_t si, bx, cx, di;
  uint16_t player = drd16(0x014c);
  uint32_t r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, drd16((uint16_t)(player * 0xc0 + 0x5a6c)));
  si = (uint16_t)r;
  L16(-6) = (uint16_t)(r >> 16);
  if ((L16(-6) | si) != 0 && rd16(L16(-6), (uint16_t)(si + 2)) == drd16(0x014c)) goto L1ed0;
  si = drd16(0x00b6);
  L16(-6) = drd16(0x00b8);
  bx = (uint16_t)(si + (uint16_t)(drd16(0x4d96) * 0x38));
  L16(-2) = L16(-6);
  if (bx > si) {
    do {
      if (rd16(L16(-6), (uint16_t)(si + 2)) == drd16(0x014c)) break;
      si = (uint16_t)(si + 0x38);
    } while (si < bx);
  }
  if (bx == si && L16(-2) == L16(-6)) {
    si = 0;
    L16(-6) = 0;
  }
L1ed0:
  if ((L16(-6) | si) != 0) {
    (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, 0, rd16(L16(-6), si));
    FRAME_LEAVE();
    return 1;
  }
  cx = 0;
  if ((int16_t)drd16(0x553a) <= 0) {
    FRAME_LEAVE();
    return 0;
  }
  bx = drd16(0x00ba);
  L16(-6) = drd16(0x00bc);
  for (;;) {
    di = rd16(L16(-6), bx);
    L16(-2) = rd16(L16(-6), (uint16_t)(bx + 2));
    if ((L16(-2) | di) == 0) break;
    if (rd16(L16(-2), (uint16_t)(di + 2)) == drd16(0x014c)) {
      (void)CALL_ORIG(10, 0x26b2, FAR_CDECL, 2, 0, rd16(L16(-2), di));
      FRAME_LEAVE();
      return 1;
    }
    bx = (uint16_t)(bx + 4);
    cx = (uint16_t)(cx + 1);
    if (!((int16_t)cx < (int16_t)drd16(0x553a))) break;
  }
  FRAME_LEAVE();
  return 0;
}

/* sync_selected_object: 4:0x197a (Ghidra FUN_1018_197a, alias fn_04_197a).
 * 375 bytes. ENTER 4,0; PUSH SI. FAR CDECL, no argument; returns
 * nothing used (the caller 4:0x1f40 ignores AX). Keeps the "selected object" record at DS:0x251a/0x251c/
 * 0x251e (flags word with the kind in bits 5 to 8, the turn, the object id) in step with the game:
 * when the record is another player's or another game's (DS:0x2520 / dword DS:0x2522 against DS:0x14c
 * and DS:0x70) it becomes "kind 0x20" with the current player's home id (player row +8); a kind-0x40
 * record (7:0x14a8(id)) or a kind-0x20 record (7:0x01e4(id) is the player's planet) is selected with
 * 10:0x26b2 / 9:0x2ed8 and its kind cleared. When the record's turn is the current turn (DS:0x82) and
 * DS:0x2528 > 0 it rebuilds DS:0xab6 (the message index, 6:0x6ef4(0) when out of range), clears bit 4 of
 * DS:0x791, calls 6:0x6a68(DS:0x158), invalidates the window DS:0x158 and, with bit 3 of DS:0x791, sets
 * bit 2 of DS:0x53f0 and calls 31:0x61b0. Finally a record that still has a kind selects its object
 * (planet 9:0x2ed8 when it is the player's, else 4:0x1e5e) and clears the kind. */
DECOMP(4, 0x197a, sync_selected_object, FAR_CDECL)
void sync_selected_object(void)
{
  FRAME_ENTER(4 + 2);
  uint16_t si;
  uint32_t r;
  if (drd16(0x2520) != drd16(0x014c)) goto L1a8c;
  if (drd32(0x2522) != drd32(0x0070)) goto L1a8c;
  {
    uint16_t kind = (uint16_t)(drd16(0x251a) & 0x01e0);
    if (kind == 0x40) {
      r = CALL_ORIG(7, 0x14a8, FAR_CDECL, 1, drd16(0x251e));
      if (r != 0) {
        (void)CALL_ORIG(10, 0x26b2, FAR_CDECL, 2, 0, drd16(0x251e));
        dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
        goto L19e2;
      }
      goto L19ca;
    }
    if (kind == 0x20) {
      r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, drd16(0x251e));
      si = (uint16_t)r;
      L16(-2) = (uint16_t)(r >> 16);
      if (r == 0) goto L19d5;
      if (rd16(L16(-2), (uint16_t)(si + 2)) != drd16(0x014c)) goto L19d5;
      (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, 0, drd16(0x251e));
      dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
      goto L19e2;
    }
    goto L19e2;
  }
L19ca:
  dwr16(0x251a, (uint16_t)((drd16(0x251a) & 0xfe3f) | 0x20));
L19d5:
  dwr16(0x251e, drd16((uint16_t)(drd16(0x014c) * 0xc0 + 0x5a6c)));
  goto L19e2;
L1a8c:
  dwr16(0x251a, (uint16_t)((drd16(0x251a) & 0xfe3f) | 0x20));
  dwr16(0x251e, drd16((uint16_t)(drd16(0x014c) * 0xc0 + 0x5a6c)));
  goto L1aa4;
L19e2:
  if (drd16(0x251c) != drd16(0x0082)) goto L1aa4;
  if ((int16_t)drd16(0x2528) <= 0) goto L1aa4;
  {
    uint16_t ax = (uint16_t)(drd16(0x2528) - 1);
    dwr16(0x0ab6, ax);
    uint16_t cx = (uint16_t)(drd16(0x0ab4) + drd16(0x0ac4));
    if (!((int16_t)ax < (int16_t)cx)) dwr16(0x0ab6, 0xffff);
    dwr16(0x0ab6, (uint16_t)rt_call_far(MK_FP(SEG_SEL(6), 0x6ef4), FAR_CDECL, (const uint16_t[]){0}, 1));
    dwr8(0x0791, (uint8_t)(drd8(0x0791) & 0xef));
    (void)rt_call_far(MK_FP(SEG_SEL(6), 0x6a68), FAR_CDECL, (const uint16_t[]){drd16(0x0158)}, 1);
    InvalidateRect(drd16(0x0158), 0, 1);
    if ((drd8(0x0791) & 8) != 0) {
      dwr8(0x53f0, (uint8_t)(drd8(0x53f0) | 4));
      (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    }
  }
L1aa4:
  if ((drd16(0x251a) & 0x01e0) == 0) {
    FRAME_LEAVE();
    return;
  }
  if (drd16(0x251e) != 0xffff) {
    r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, drd16(0x251e));
    si = (uint16_t)r;
    L16(-2) = (uint16_t)(r >> 16);
    if (r != 0 && rd16(L16(-2), (uint16_t)(si + 2)) == drd16(0x014c)) {
      (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, 0, drd16(0x251e));
      goto L1ae8;
    }
  }
  (void)call4(0x1e5e, NULL, 0);
L1ae8:
  dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
  FRAME_LEAVE();
}

/* The permutation table at 4:0x02b5 (21 bytes, a permutation of 0 to 20) of the serial-number coder
 * 4:0x1af2 / 4:0x1c78: output byte i of the shuffled block is input byte tab[i]. */
static const uint8_t cs_perm[21] = {0x0b, 0x04, 0x05, 0x10, 0x11, 0x0c, 0x13, 0x0f, 0x0a, 0x01, 0x0e,
                                    0x0d, 0x03, 0x12, 0x02, 0x14, 0x09, 0x07, 0x00, 0x08, 0x06};

/* A random number 0 to 15 of the generator 8:0x8730(0x10) (AL; its state is in DGROUP). */
static inline uint8_t rnd16(void)
{
  return (uint8_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x10);
}

/* encode_serial: 4:0x1af2 (Ghidra FUN_1018_1af2, alias fn_04_1af2).
 * 390 bytes. ENTER 0x36,0; PUSH DI; PUSH SI. FAR CDECL (key dword
 * [BP+6], src [BP+0xa] a near pointer to 11 bytes, out [BP+0xc] a near pointer to 22 bytes). Encodes
 * the 4-byte key and the 11 source bytes (a registration serial) as a 21-character string of the
 * alphabet A-Z a-z 0-9 '-' '*' (6 bits a character, 0x1c = 28 characters written, then the NUL;
 * the string is 28 characters long). The generator is seeded with the key (8:0x0000(0x0b, 0x11),
 * 8:0x8672(key)); for each of the 11 source bytes it draws `byte` numbers and then one more
 * (the 4-bit digit, low nibble first) and stores them after the 11 bytes; a last nibble is the
 * xor of the 15 bytes. The block is shuffled through the table 4:0x02b5, then packed 6 bits a
 * character. Returns nothing (AX is left over). */
DECOMP(4, 0x1af2, encode_serial, FAR_CDECL)
void encode_serial(uint32_t key, uint16_t src, uint16_t out)
{
  FRAME_ENTER(0x36 + 4);
  uint16_t di, si;
  (void)CALL_ORIG(8, 0x0000, FAR_CDECL, 2, 0x000b, 0x0011);
  (void)CALL_ORIG(8, 0x8672, FAR_CDECL, 2, (uint16_t)key, (uint16_t)(key >> 16));
  L32(-0x20) = key;
  for (uint16_t k = 0; k < 11; k++) L8(-0x1c + (int16_t)k) = drd8((uint16_t)(src + k));
  L16(-0xa) = 0x0f;
  L16(-2) = 0;
  L16(-6) = 0;
  do {
    si = drd8((uint16_t)(L16(-6) + src));
    for (; si != 0; si = (uint16_t)(si - 1)) (void)rnd16();
    if (L16(-2) == 0) {
      uint8_t al = rnd16();
      L8((int16_t)(-0x20 + (int16_t)L16(-0xa))) = al;
    } else {
      uint8_t al = rnd16();
      al = (uint8_t)(al << 4);
      L8((int16_t)(-0x20 + (int16_t)L16(-0xa))) = (uint8_t)(L8((int16_t)(-0x20 + (int16_t)L16(-0xa))) | al);
      L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
    }
    L16(-2) = (uint16_t)(((uint8_t)L16(-2) + 1) & 1);
    L16(-6) = (uint16_t)(L16(-6) + 1);
  } while ((int16_t)L16(-6) < 0x0b);
  L8(-3) = 0;
  for (si = bpoff(-0x20); si < bpoff(-0x11); si++) L8(-3) = (uint8_t)(L8(-3) ^ rd8(rt_ss, si));
  {
    uint8_t al = (uint8_t)(L8(-3) << 4);
    L8((int16_t)(-0x20 + (int16_t)L16(-0xa))) = (uint8_t)(L8((int16_t)(-0x20 + (int16_t)L16(-0xa))) | al);
  }
  (void)CALL_ORIG(8, 0x8654, FAR_CDECL, 0);
  di = 0;
  si = bpoff(-0x36);
  L16(-2) = si;
  do {
    wr8(rt_ss, si, rd8(rt_ss, (uint16_t)(bpoff(-0x20) + cs_perm[di])));
    si++;
    di++;
  } while (si < bpoff(-0x21));
  di = 0;
  L16(-0xa) = di;
  L32(-6) = 0;
  L16(-8) = 0x1c;
  si = out;
  do {
    if ((int16_t)di < 6) {
      uint16_t ax = (uint16_t)(L8((int16_t)(-0x36 + (int16_t)L16(-0xa))) << (di & 0x1f));
      uint16_t dx = (ax & 0x8000) ? 0xffff : 0;
      L16(-6) = (uint16_t)(L16(-6) | ax);
      L16(-4) = (uint16_t)(L16(-4) | dx);
      L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
      di = (uint16_t)(di + 8);
    }
    L8(-1) = (uint8_t)(L8(-6) & 0x3f);
    L32(-6) = (uint32_t)((int32_t)L32(-6) >> 6);
    di = (uint16_t)(di - 6);
    {
      uint8_t v = L8(-1), ch;
      if (v < 0x1a)
        ch = (uint8_t)(v + 0x41);
      else if (v < 0x34)
        ch = (uint8_t)(v + 0x47);
      else if (v < 0x3e)
        ch = (uint8_t)(v - 4);
      else if (v == 0x3e)
        ch = 0x2d;
      else
        ch = 0x2a;
      dwr8(si, ch);
    }
    si++;
    L16(-8) = (uint16_t)(L16(-8) - 1);
  } while (L16(-8) != 0);
  dwr8(si, 0);
  FRAME_LEAVE();
}

/* decode_serial: 4:0x1c78 (Ghidra FUN_1018_1c78, alias fn_04_1c78).
 * 486 bytes. ENTER 0x3a,0; PUSH DI; PUSH SI. FAR CDECL (key
 * [BP+6], a near pointer to a dword; data [BP+8], a near pointer to 11 bytes; text [BP+0xa], a near
 * pointer to the string). The inverse of 4:0x1af2: clears *key and the 11 bytes at data, reads 21
 * characters (A-Z a-z 0-9 -> 6-bit values, '-' is 0x3e, any other 0x3f; the character compares are
 * signed byte compares), packs them 6 bits at a time into bytes, unshuffles through the table
 * 4:0x02b5 and asks 14:0x2bb4(key dword) whether the key is acceptable; if it is, re-seeds the
 * generator with the key and checks the 11 digits and the checksum nibble the way 4:0x1af2 builds
 * them. On success *key = the key and data = the 11 bytes. Returns 1 when everything matched, else
 * 0 (AX; the text pointer is advanced as far as it was read). */
DECOMP(4, 0x1c78, decode_serial, FAR_CDECL)
uint16_t decode_serial(uint16_t key, uint16_t data, uint16_t text)
{
  FRAME_ENTER(0x3a + 4);
  uint16_t di, si;
  dwr32(key, 0);
  for (uint16_t k = 0; k < 11; k++) dwr8((uint16_t)(data + k), 0);
  L32(-8) = 0;
  L16(-0xe) = 0;
  L16(-0xc) = 0;
  L16(-4) = 0;
  L16(-0xa) = 0;
  di = 0;
  while ((int16_t)L16(-0xa) < 0x15) {
    if ((int16_t)di < 8) {
      si = text;
      do {
        int8_t c = (int8_t)drd8(si);
        uint8_t al;
        if (c >= 0x41 && c <= 0x5a)
          al = (uint8_t)(c - 0x41);
        else if (c >= 0x61 && c <= 0x7a)
          al = (uint8_t)(c - 0x47);
        else if (c >= 0x30 && c <= 0x39)
          al = (uint8_t)(c + 4);
        else if (c == 0x2d)
          al = 0x3e;
        else
          al = 0x3f;
        L8(-1) = al;
        uint16_t ax = (uint16_t)(al << (di & 0x1f));
        uint16_t dx = (ax & 0x8000) ? 0xffff : 0;
        L16(-8) = (uint16_t)(L16(-8) | ax);
        L16(-6) = (uint16_t)(L16(-6) | dx);
        si++;
        di = (uint16_t)(di + 6);
      } while ((int16_t)di < 8);
      text = si;
    }
    L8((int16_t)(-0x24 + (int16_t)L16(-0xc))) = L8(-8);
    L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
    di = (uint16_t)(di - 8);
    L32(-8) = (uint32_t)((int32_t)L32(-8) >> 8);
    L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
  }
  di = 0;
  si = bpoff(-0x24);
  do {
    wr8(rt_ss, (uint16_t)(bpoff(-0x3a) + cs_perm[di]), rd8(rt_ss, si));
    si++;
    di++;
  } while (si < bpoff(-0xf));
  L32(-6) = L32(-0x3a);
  if ((uint16_t)CALL_ORIG(14, 0x2bb4, FAR_CDECL, 2, L16(-0x3a), L16(-0x38)) == 0) {
    FRAME_LEAVE();
    return 0;
  }
  L16(-8) = 1;
  (void)CALL_ORIG(8, 0x0000, FAR_CDECL, 2, 0x000b, 0x0011);
  (void)CALL_ORIG(8, 0x8672, FAR_CDECL, 2, L16(-6), L16(-4));
  L32(-0xc) = 0x0f;
  do {
    si = (uint16_t)(rd16(rt_ss, (uint16_t)(bpoff(-0x36) + L16(-0xa))) & 0xff);
    for (; si != 0; si = (uint16_t)(si - 1)) (void)rnd16();
    if (L16(-0xe) == 0) {
      uint8_t al = rnd16();
      uint8_t cl = (uint8_t)(L8((int16_t)(-0x3a + (int16_t)L16(-0xc))) & 0x0f);
      if (cl != al) L16(-8) = 0;
    } else {
      uint8_t al = rnd16();
      uint8_t cl = (uint8_t)(L8((int16_t)(-0x3a + (int16_t)L16(-0xc))) >> 4);
      if (cl != al) L16(-8) = 0;
      L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
    }
    L16(-0xe) = (uint16_t)(((uint8_t)L16(-0xe) + 1) & 1);
    L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
  } while ((int16_t)L16(-0xa) < 0x0b);
  L8(-1) = 0;
  for (si = bpoff(-0x3a); si < bpoff(-0x2b); si++) L8(-1) = (uint8_t)(L8(-1) ^ rd8(rt_ss, si));
  {
    uint8_t al = (uint8_t)(L8((int16_t)(-0x3a + (int16_t)L16(-0xc))) >> 4);
    if (al != (uint8_t)(L8(-1) & 0x0f)) L16(-8) = 0;
  }
  (void)CALL_ORIG(8, 0x8654, FAR_CDECL, 0);
  if (L16(-8) != 0) {
    dwr16(key, L16(-6));
    dwr16((uint16_t)(key + 2), L16(-4));
    for (uint16_t k = 0; k < 11; k++) dwr8((uint16_t)(data + k), rd8(rt_ss, bpoff((int16_t)(-0x36 + k))));
  }
  uint16_t r = L16(-8);
  FRAME_LEAVE();
  return r;
}

/* A far call to a decompiled function of another segment (PUSH args; CALL far): rule e. */
static inline uint32_t far_call(uint16_t seg, uint16_t off, const uint16_t *a, uint16_t nw, int16_t conv)
{
  return rt_call_far(MK_FP(SEG_SEL(seg), off), conv, a, nw);
}
/* The string length at seg:off (REPNE SCASB). */
static inline uint16_t strlen_at(uint16_t seg, uint16_t off)
{
  uint16_t n = 0;
  while (rd8(seg, (uint16_t)(off + n)) != 0) n++;
  return n;
}
/* strcpy(dst, src) in one segment (the original's REPNE SCASB; REP MOVSW; REP MOVSB copies the NUL too). */
static inline void strcpy_at(uint16_t seg, uint16_t dst, uint16_t src)
{
  uint16_t n = (uint16_t)(strlen_at(seg, src) + 1);
  for (uint16_t k = 0; k < n; k++) wr8(seg, (uint16_t)(dst + k), rd8(seg, (uint16_t)(src + k)));
}
/* strcat(dst, src): the NUL of dst is overwritten. */
static inline void strcat_at(uint16_t seg, uint16_t dst, uint16_t src)
{
  strcpy_at(seg, (uint16_t)(dst + strlen_at(seg, dst)), src);
}

/* run_computer_player_turns: 4:0x39f8 (Ghidra FUN_1018_39f8, alias fn_04_39f8).
 * 312 bytes. ENTER 0x2c,0; PUSH DI; PUSH SI. FAR CDECL, no argument;
 * returns nothing used (AX is left over; the callers 4:0x4090 and 4:0x4dba ignore it). Runs once per
 * "turn" (bit 2 of DS:0x791 is set when it has run, and the function does nothing then): saves bit 4 of
 * DS:0x790, and when bit 3 is clear writes the host state with 14:0x2992 and 14:0x0508(DS:0x5764,
 * DS:0x3ad). Copies the status word at row + 6 of each of the DS:0x78 player rows (DS:0x5a64 + 0xc0 * i)
 * to BP-0x2c, sets bit 4, saves DS:0x76e and sets it to 1, then for each player i in order: 8:0xb90e(
 * MulDiv(0x154, i + 1, DS:0x78)) (the progress bar), and for a player whose status bit 9 (row + 7, bit 1)
 * is set: marks DS:0x790 |= 0x0a, asks 14:0x2caa(1, i, 0x20) (is the player's file readable), puts bit 3
 * of DS:0x790 back to the saved value and clears bit 1, and runs 14:0x31c2 when the file is ok, else the
 * computer player's turn 17:0x0000(i, status word). At the end bit 4 is restored, a pending state
 * is written again with 14:0x2992 and 14:0x0508(DS:0x5764, DS:0x3b1), DS:0x76e is restored and bit 2 of
 * DS:0x791 is set. The name takes 17:0x0000 to generate a computer player's turn, as described above; that
 * callee is not renamed yet, so the name may change with it. */
DECOMP(4, 0x39f8, run_computer_player_turns, FAR_CDECL)
void run_computer_player_turns(void)
{
  FRAME_ENTER(0x2c + 4);
  uint16_t di, si, bx, dx;
  L16(-0xc) = (uint16_t)((drd16(0x0790) & 0x10) >> 4);
  L16(-0xa) = 0;
  if ((drd8(0x0791) & 4) != 0) {
    FRAME_LEAVE();
    return;
  }
  {
    uint16_t ax = (uint16_t)(drd8(0x0790) & 8);
    L16(-8) = (uint16_t)(ax >> 3);
    if (ax == 0) {
      (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
      (void)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, 0x03ad);
    }
  }
  L16(-4) = 0;
  if ((int16_t)drd16(0x0078) > 0) {
    di = 0x5a6a;
    bx = bpoff(-0x2c);
    L16(-2) = drd16(0x0078);
    dx = L16(-2);
    do {
      wr16(rt_ss, bx, drd16(di));
      di = (uint16_t)(di + 0xc0);
      bx = (uint16_t)(bx + 2);
      dx = (uint16_t)(dx - 1);
    } while (dx != 0);
  }
  dwr8(0x0790, (uint8_t)(drd8(0x0790) | 0x10));
  L16(-6) = drd16(0x076e);
  dwr16(0x076e, 1);
  si = 0;
  if ((int16_t)drd16(0x0078) > (int16_t)si) {
    di = bpoff(-0x2c);
    do {
      int16_t pct = MulDiv(0x154, (int16_t)(uint16_t)(si + 1), (int16_t)drd16(0x0078));
      (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, (uint16_t)pct);
      if ((rd8(rt_ss, (uint16_t)(di + 1)) & 2) != 0) {
        L16(-0xa) = 1;
        dwr8(0x0790, (uint8_t)(drd8(0x0790) | 0x0a));
        L16(-2) = (uint16_t)CALL_ORIG(14, 0x2caa, FAR_CDECL, 3, 1, si, 0x20);
        {
          uint8_t al = (uint8_t)(L16(-8) << 3);
          dwr8(0x0790, (uint8_t)(drd8(0x0790) & 0xfd));
          al = (uint8_t)(al ^ drd8(0x0790));
          dwr16(0x0790, (uint16_t)(drd16(0x0790) ^ (al & 8)));
        }
        if (L16(-2) != 0) {
          (void)CALL_ORIG(14, 0x31c2, FAR_CDECL, 0);
        } else {
          const uint16_t a[2] = {si, rd16(rt_ss, di)};
          (void)far_call(17, 0x0000, a, 2, FAR_CDECL);
        }
      }
      di = (uint16_t)(di + 2);
      si = (uint16_t)(si + 1);
    } while ((int16_t)si < (int16_t)drd16(0x0078));
  }
  {
    uint8_t al = (uint8_t)(L16(-0xc) << 4);
    al = (uint8_t)(al ^ drd8(0x0790));
    dwr16(0x0790, (uint16_t)(drd16(0x0790) ^ (al & 0x10)));
  }
  if (L16(-0xa) != 0) {
    (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
    (void)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, 0x03b1);
  }
  dwr16(0x076e, L16(-6));
  dwr8(0x0791, (uint8_t)(drd8(0x0791) | 4));
  FRAME_LEAVE();
}

/* check_game_file: 4:0x3ecc (Ghidra FUN_1018_3ecc, alias fn_04_3ecc).
 * 452 bytes. ENTER 0xde,0; PUSH DI; PUSH SI. FAR CDECL (name
 * [BP+6], a near pointer to the file name; strict [BP+8]). "Is this a good game file?": reads the
 * file's header block and its first player block. The catch buffer BP-0x1e is registered in DS:0x6E
 * (34:0x0800, a setjmp; the error paths of the file functions throw back to it and take the nonzero
 * branch). 14:0x3100(name, 0x20) opens the file; 14:0x305a reads a block: the first must be the file
 * header block (type 8, flag nibble 2, version DS:0x4dba & 0xfe0 in [0x620, 0xa40), byte DS:0x4dc0 = 5)
 * and the version goes to DS:0x772; the second must be a player block (type 6): 14:0x0370(BP-0xde,
 * DS:0x4db2) turns it into a player row of 0xc0 bytes at BP-0xde; the third must be the end block
 * (type 0). The file is "good" when the row's dword at +0xc (BP-0xd2) is non-zero, or with `strict` (not
 * 0) when 8:0xb1ec accepts it: the row is then copied to DS:0x51b4 (0xc0 bytes), the name at DS:0x2560
 * (or "" when the dword is 0) to DS:0x1434 and the file name to DS:0x1444. Returns 1; else (after
 * 14:0x31c2) the message from 8:0x8d94(code, 0x10) with the text of DS:0x5866 copied to the name
 * (only when DS:0x76e was 0) and AX = 0 (not a game file) or 0xffff (wrong version or not ok). The
 * old values of DS:0x76e and DS:0x6e are restored on every path. */
DECOMP(4, 0x3ecc, check_game_file, FAR_CDECL)
uint16_t check_game_file(uint16_t name, uint16_t strict)
{
  FRAME_ENTER(0xde + 4);
  uint16_t bx, dx;
  L16(-0xc) = 0xffff;
  L16(-6) = 0;
  bx = drd16(0x076e);
  dwr16(0x076e, 1);
  dx = drd16(0x006e);
  dwr16(0x006e, bpoff(-0x1e));
  L16(-0xa) = bx;
  L16(-8) = dx;
  if ((uint16_t)CALL_ORIG(34, 0x0800, FAR_CDECL, 1, bpoff(-0x1e)) != 0) goto L4046;
  (void)CALL_ORIG(14, 0x3100, FAR_CDECL, 2, name, 0x20);
  (void)CALL_ORIG(14, 0x305a, FAR_CDECL, 0);
  if ((drd8(0x2911) & 0xfc) != 0x20) goto L403c;
  if ((drd8(0x4dbb) & 0xf0) != 0x20) goto L403c;
  if ((drd16(0x4dba) & 0x0fe0) < 0x0620) goto L403c;
  if ((drd16(0x4dba) & 0x0fe0) >= 0x0a40) goto L403c;
  dwr16(0x0772, drd16(0x4dba));
  if (drd8(0x4dc0) != 5) goto L4046;
  (void)CALL_ORIG(14, 0x305a, FAR_CDECL, 0);
  if ((drd8(0x2911) & 0xfc) != 0x18) goto L4046;
  L16(-0xc) = 3;
  (void)CALL_ORIG(14, 0x0370, FAR_CDECL, 2, bpoff(-0xde), 0x4db2);
  (void)CALL_ORIG(14, 0x305a, FAR_CDECL, 0);
  if ((drd8(0x2911) & 0xfc) != 0) goto L4046;
  L32(-4) = drd32(0x031c);
  dwr32(0x031c, L32(-0xd2));
  if (strict != 0) {
    if ((uint16_t)CALL_ORIG(8, 0xb1ec, FAR_CDECL, 0) == 0) {
      dwr32(0x031c, L32(-4));
      goto L4041;
    }
  }
  dwr32(0x031c, L32(-4));
  if (L32(-0xd2) != 0)
    strcpy_at(MEM_SEL_DGROUP, 0x1434, 0x2560);
  else
    dwr8(0x1434, 0);
  for (uint16_t k = 0; k < 0xc0; k++) dwr8((uint16_t)(0x51b4 + k), rd8(rt_ss, bpoff((int16_t)(-0xde + k))));
  strcpy_at(MEM_SEL_DGROUP, 0x1444, name);
  (void)CALL_ORIG(14, 0x31c2, FAR_CDECL, 0);
  dwr16(0x076e, L16(-0xa));
  dwr16(0x006e, L16(-8));
  FRAME_LEAVE();
  return 1;
L403c:
  L16(-0xc) = 0x0d;
L4041:
  L16(-6) = 0xffff;
L4046:
  (void)CALL_ORIG(14, 0x31c2, FAR_CDECL, 0);
  dwr16(0x006e, L16(-8));
  dwr16(0x076e, L16(-0xa));
  if (L16(-0xa) == 0 && L16(-0xc) != 0xffff) {
    strcpy_at(MEM_SEL_DGROUP, 0x5866, name);
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, L16(-0xc), 0x10);
  }
  {
    uint16_t r = L16(-6);
    FRAME_LEAVE();
    return r;
  }
}

/* host_timer_proc: 4:0x4dba (Ghidra FUN_1018_4dba, alias fn_04_4dba).
 * 539 bytes. ENTER 0xa,0; PUSH DI; PUSH SI; RETF 0xa. The timer
 * procedure of the main window: FAR PASCAL (hwnd [BP+0xe], msg [BP+0xc], id [BP+0xa], time [BP+6], a
 * dword); returns nothing (AX left over). Not re-entrant (DS:0x3c0). The timer state DS:0x164 is 0xe
 * (waiting for the next player) or 0xf (flashing) or other: in state 0xe, if 14:0x2f40(DS:0x14c) allows,
 * it kills the timer DS:0x162, formats the player number + 1 with DS:[0x190], 14:0x2992 and 14:0x0508(
 * DS:0x5764, name) (the next player's file; a failure shows 8:0x8d94(0x1b, 0x10)), restores DS:0x14c,
 * sets the caption (4:0x02ca), starts a 1000 ms timer in state 0xf, flashes the window, sets the text
 * of string 0x309 and beeps; in state 0xf it flashes the window; otherwise (state 0x14) it counts the
 * players that still have to play with 4:0x4580 and shows "n player(s) (s) left" (strings 0x30a, 0x30b,
 * the plural DS:0x3cd), or, when bit 4 of DS:0x792 says everybody is done, string 0x2b8 and disables the
 * button 0x408 of the dialog. The common end (4f33) invalidates the sibling window when the parent's
 * last child is the window, and while nobody is left to wait for (count 0) it runs 8:0xb8cc (with
 * DS:0x793 bit 2), 4:0x39f8, 22:0x0000 and 8:0xb8f6 and either quits (DS:0x251a bit 3: PostQuitMessage(
 * DS:0x366)) or runs 4:0x39f8 and counts again. */
DECOMP(4, 0x4dba, host_timer_proc, FAR_PASCAL)
void host_timer_proc(uint16_t hwnd, uint16_t msg, uint16_t id, uint32_t time)
{
  FRAME_ENTER(0xa + 4);
  uint16_t di, ax;
  (void)msg;
  (void)id;
  (void)time;
  if (drd16(0x03c0) != 0) {
    FRAME_LEAVE();
    return;
  }
  dwr16(0x03c0, 1);
  L16(-6) = drd16(0x076e);
  if (drd16(0x0164) == 0x000e) {
    if ((uint16_t)CALL_ORIG(14, 0x2f40, FAR_CDECL, 1, drd16(0x014c)) == 0) goto L4fc3;
    di = drd16(0x014c);
    (void)KillTimer(hwnd, drd16(0x0162));
    {
      uint16_t sp0 = rt_sp;
      wr16(rt_ss, (uint16_t)(sp0 - 2), (uint16_t)(drd16(0x014c) + 1));
      rt_sp = (uint16_t)(sp0 - 2);
      (void)_wsprintf(bpfp(-0xa), dsfp(drd16(0x0190)), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
      rt_sp = sp0;
    }
    (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
    if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, bpoff(-0xa)) == 0) {
      (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x001b, 0x0010);
      goto L4fc3;
    }
    dwr16(0x014c, di);
    (void)call4(0x02ca, NULL, 0);
    ax = SetTimer(0, 0x000f, 0x03e8, drd32(0x53e2));
    dwr16(0x0162, ax);
    dwr16(0x0164, 0x000f);
    (void)FlashWindow(drd16(0x2806), 1);
    pushw(drd16(0x2806)); /* PUSH [0x2806] is still on the stack while 8:0x8df4 runs */
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0309);
    SetWindowText(drd16(0x2806), dsfp(ax));
    popw(1);
    MessageBeep(0x30);
    L16(-4) = 1;
    goto L4f33;
  }
  if (drd16(0x0164) == 0x000f) {
    (void)FlashWindow(drd16(0x2806), 1);
    goto L4fc3;
  }
L4f92:
  L16(-4) = (uint16_t)call4(0x4580, NULL, 0);
  if ((drd8(0x0792) & 0x10) != 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b8, 0x0010);
    ax = GetDlgItem(hwnd, 0x0408);
    (void)EnableWindow(ax, 0);
    goto L4fc3;
  }
  /* 4eaa: PUSH AX (the count), string 0x30a of 8:0x8df4, _wsprintf(DS:0x5866, fmt, count) */
  {
    uint16_t sp0 = rt_sp;
    wr16(rt_ss, (uint16_t)(sp0 - 2), L16(-4));
    rt_sp = (uint16_t)(sp0 - 2);
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x030a);
    (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
    rt_sp = sp0;
  }
  if (L16(-4) != 1) strcat_at(MEM_SEL_DGROUP, 0x5866, 0x03cd);
  ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x030b);
  strcat_at(MEM_SEL_DGROUP, 0x5866, ax);
  SetWindowText(drd16(0x2806), dsfp(0x5866));
L4f33:
  L16(-2) = GetWindow(drd16(0x2806), 3);
  if (GetWindow(L16(-2), 4) == drd16(0x2806)) InvalidateRect(L16(-2), 0, 1);
  if (L16(-4) != 0) goto L4fc3;
  if ((drd8(0x0793) & 4) != 0) (void)CALL_ORIG(8, 0xb8cc, FAR_CDECL, 0);
  (void)call4(0x39f8, NULL, 0);
  (void)CALL_ORIG(22, 0x0000, FAR_CDECL, 0);
  (void)CALL_ORIG(8, 0xb8f6, FAR_CDECL, 0);
  if ((drd8(0x251a) & 8) != 0) {
    PostQuitMessage((int16_t)drd16(0x0366));
    goto L4fc3;
  }
  (void)call4(0x39f8, NULL, 0);
  goto L4f92;
L4fc3:
  dwr16(0x03c0, 0);
  dwr16(0x076e, L16(-6));
  FRAME_LEAVE();
}

/* run_host_mode: 4:0x4090 (Ghidra FUN_1018_4090, alias fn_04_4090).
 * 360 bytes. ENTER 0xc,0; PUSH SI. FAR CDECL, no argument; returns
 * nothing used (every caller ignores AX). Runs the "host mode" dialog (template 0x73, procedure
 * 4:0x467c through MakeProcInstance) over the main window DS:0x2806. Bit 3 of DS:0x790 and bit 5 of
 * DS:0x792 select a status message (14:0x54ea). The main window is hidden first (ShowWindow(DS:0x2806,
 * 0)); when bit 2 of DS:0x251a is set it clears the bit and goes straight to the timer start (41c6).
 * Otherwise it shows the dialog again and again: the result -1 or a timer start (41c6) ends it; a
 * non-zero result runs 8:0xb8cc (when DS:0x793 bit 2), 4:0x39f8 and 22:0x0000, then, while DS:0x378 is
 * still counting down and neither Shift (VK 0x10) nor Control (0x11) is held, again, and finally
 * 8:0xb8f6 and a new dialog; the result 0 (the host chose to continue) clears bit 3 of DS:0x790, runs 14:0x2992,
 * and, unless bit 3 of DS:0x251a is set, creates the window of class DS:0x1ea at the size of the screen
 * (DS:0x312, DS:0x314 = 0) and returns. 41c6 shows the main window minimised (ShowWindow(.., 2)), starts a
 * 10000 ms timer (id 0xd, procedure DS:0x53e2) into DS:0x162 / DS:0x164 = 0xd and calls the timer procedure
 * 4:0x4dba(0, 0, id, 0) once. */
DECOMP(4, 0x4090, run_host_mode, FAR_CDECL)
void run_host_mode(void)
{
  FRAME_ENTER(0xc + 2);
  uint16_t si, ax, dx, w;
  uint32_t pi;
  if ((drd8(0x0790) & 8) == 0) {
    if ((drd8(0x0792) & 0x20) == 0) (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 0x0002, 0xffff, 0x0001, 0x0001);
    dwr8(0x0790, (uint8_t)(drd8(0x0790) | 8));
  }
  (void)ShowWindow(drd16(0x2806), 0);
  if ((drd8(0x251a) & 4) != 0) {
    dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfb));
    goto L41c6;
  }
L40d6:
  pi = MakeProcInstance(MK_FP(SEG_SEL(4), 0x467c), drd16(0x54f4));
  ax = (uint16_t)pi;
  dx = (uint16_t)(pi >> 16);
  L16(-0xc) = ax;
  L16(-0xa) = dx;
  si = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x00000073, drd16(0x2806), pi));
  FreeProcInstance(L32(-0xc));
  if ((int16_t)si == -1) goto L41c6;
  if (si == 0) goto L4158;
L4113:
  if ((drd8(0x0793) & 4) != 0) (void)CALL_ORIG(8, 0xb8cc, FAR_CDECL, 0);
  (void)call4(0x39f8, NULL, 0);
  (void)CALL_ORIG(22, 0x0000, FAR_CDECL, 0);
  if (drd16(0x0378) != 0) {
    dwr16(0x0378, (uint16_t)(drd16(0x0378) - 1));
    if ((int16_t)GetAsyncKeyState(0x10) < 0) goto L4149;
    if ((int16_t)GetAsyncKeyState(0x11) >= 0) goto L4113;
  }
L4149:
  dwr16(0x0378, 0);
  (void)CALL_ORIG(8, 0xb8f6, FAR_CDECL, 0);
  goto L40d6;
L4158:
  if ((drd8(0x0792) & 0x20) == 0) (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 0x0002, 0xffff, 0x0001, 0x0000);
  dwr8(0x0790, (uint8_t)(drd8(0x0790) & 0xf7));
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  if ((drd8(0x251a) & 8) != 0) {
    FRAME_LEAVE();
    return;
  }
  w = (uint16_t)GetSystemMetrics(0);
  L16(-8) = w;
  ax = (uint16_t)GetSystemMetrics(1);
  ax = CreateWindow(dsfp(0x01ea), dsfp(0x03b5), 0x90000000u, 0, 0, (int16_t)L16(-8), (int16_t)ax, drd16(0x2806), 0,
                    drd16(0x54f4), 0);
  dwr16(0x0312, ax);
  dwr16(0x0314, 0);
  FRAME_LEAVE();
  return;
L41c6:
  (void)ShowWindow(drd16(0x2806), 2);
  ax = SetTimer(0, 0x000d, 0x2710, drd32(0x53e2));
  dwr16(0x0162, ax);
  dwr16(0x0164, 0x000d);
  {
    const uint16_t a[5] = {0, 0, ax, 0, 0};
    (void)far_call(4, 0x4dba, a, 5, FAR_PASCAL);
  }
  FRAME_LEAVE();
}

/* Create (or invalidate) one of the small child windows of the main window: CreateWindow(class DS:cls,
 * no title, WS_CHILD | WS_VISIBLE, x, y, cx, cy, parent DS:0x2806, no id, instance DS:0x54f4). */
static uint16_t child_window(uint16_t cls, int16_t x, int16_t y, int16_t cx, int16_t cy)
{
  return CreateWindow(dsfp(cls), 0, 0x50000000u, x, y, cx, cy, drd16(0x2806), 0, drd16(0x54f4), 0);
}

/* update_caption_and_panels: 4:0x02ca (Ghidra FUN_1018_02ca, alias fn_04_02ca).
 * 606 bytes. ENTER 0x7a,0; PUSH DI; PUSH SI. FAR CDECL, no argument; no
 * result (AX left over). Sets the caption of the main window DS:0x2806 to game-name + player (the
 * text of 7:0x0b84 for the current player DS:0x14c), or to the string DS:0x34c (_wsprintf(BP-0x78,
 * DS:0x34c, DS:0x90)) when there is no current player (DS:0x14c = 0xffff). With a player: takes the
 * base name of the file DS:0x5764 (after the last '\' or ':'), copies up to 8 characters to BP-0x14,
 * upper-cases them (34:0x05a0), appends DS:0x32e formatted with player + 1, and the caption is
 * _wsprintf(BP-0x78, DS:0x333, DS:0x90, 7:0x0b84(player, 0, 1L, 0L), BP-0x14). Then the four small child
 * windows of the class names DS:0x1a2, 0x1ac, 0x1b6, 0x202 are created, or invalidated when they
 * exist; the first one is also given 0x3e8 in DS:0x27ea/0x27ec and 11:0x4c62 runs on it, and the
 * window DS:0x158 (class DS:0x1c2) is destroyed and recreated, followed by 4:0x5d98. The second window
 * uses the widths BP-4 and BP-2 (uninitialised stack words, as in the original). */
DECOMP(4, 0x02ca, update_caption_and_panels, FAR_CDECL)
void update_caption_and_panels(void)
{
  FRAME_ENTER(0x7a + 4);
  uint16_t si, di, dx, ax, bx;
  if (drd16(0x014c) == 0xffff) goto L038a;
  dx = 0x5764;
  si = (uint16_t)(0x5763 + strlen_at(MEM_SEL_DGROUP, 0x5764));
  if (si > dx) {
    do {
      bx = (uint16_t)(si - 1);
      L16(-0x7a) = bx;
      if (drd8(bx) == 0x5c) break;
      if (drd8(bx) == 0x3a) break;
      si = bx;
    } while (si > 0x5764);
  }
  L8(-0xc) = 0;
  (void)CALL_ORIG(34, 0x0352, FAR_CDECL, 3, bpoff(-0x14), si, 8);
  (void)CALL_ORIG(34, 0x05a0, FAR_CDECL, 1, bpoff(-0x14));
  {
    uint16_t sp0 = rt_sp;
    wr16(rt_ss, (uint16_t)(sp0 - 2), (uint16_t)(drd16(0x014c) + 1));
    rt_sp = (uint16_t)(sp0 - 2);
    di = bpoff(-0x14);
    si = strlen_at(rt_ss, di);
    (void)_wsprintf(MK_FP(rt_ss, (uint16_t)(bpoff(-0x14) + si)), dsfp(0x032e), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
    rt_sp = sp0;
  }
  /* the stack still holds PUSH SS; PUSH BP-0x14 (the third string of the caption) when 7:0x0b84 is called */
  pushw(rt_ss);
  pushw(bpoff(-0x14));
  ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, drd16(0x014c), 0, 1, 0, 0, 0);
  pushw(MEM_SEL_DGROUP);
  pushw(ax);
  pushw(MEM_SEL_DGROUP);
  pushw(0x0090);
  (void)_wsprintf(bpfp(-0x78), dsfp(0x0333), MK_FP(rt_ss, rt_sp));
  popw(6);
  goto L039f;
L038a:
  {
    pushw(MEM_SEL_DGROUP);
    pushw(0x0090);
    (void)_wsprintf(bpfp(-0x78), dsfp(0x034c), MK_FP(rt_ss, rt_sp));
    popw(2);
  }
L039f:
  SetWindowText(drd16(0x2806), bpfp(-0x78));
  if (drd16(0x014c) == 0xffff) goto L0524;
  if (drd16(0x0150) == 0) {
    dwr16(0x0150, child_window(0x01a2, (int16_t)0xff38, (int16_t)0xff38, 0x0a, 0x0a));
  } else {
    InvalidateRect(drd16(0x0150), 0, 1);
    dwr16(0x27ea, 0x03e8);
    dwr16(0x27ec, 0x03e8);
    (void)CALL_ORIG(11, 0x4c62, FAR_CDECL, 1, drd16(0x0150));
  }
  if (drd16(0x0154) == 0)
    dwr16(0x0154, child_window(0x01ac, (int16_t)0xfe0c, (int16_t)0xfe0c, (int16_t)L16(-4), (int16_t)L16(-2)));
  else
    InvalidateRect(drd16(0x0154), 0, 1);
  if (drd16(0x0156) == 0)
    dwr16(0x0156, child_window(0x01b6, (int16_t)0xfe0c, (int16_t)0xfe0c, 0x0a, 0x0a));
  else
    InvalidateRect(drd16(0x0156), 0, 1);
  if (drd16(0x015a) == 0)
    dwr16(0x015a, child_window(0x0202, (int16_t)0xfe0c, (int16_t)0xfe0c, 0x0a, 0x0a));
  else
    InvalidateRect(drd16(0x015a), 0, 1);
  if (drd16(0x0158) != 0) (void)DestroyWindow(drd16(0x0158));
  dwr16(0x0158, child_window(0x01c2, (int16_t)0xfe0c, (int16_t)0xfe0c, 0x0a, 0x0a));
  (void)call4(0x5d98, NULL, 0);
L0524:
  FRAME_LEAVE();
}

/* open_game_file: 4:0x3b66 (Ghidra FUN_1018_3b66, alias fn_04_3b66).
 * 869 bytes. ENTER 0x34a,0; PUSH DI; PUSH SI. FAR CDECL (hwnd [BP+6],
 * mode [BP+8]); returns AX (0, 1, 0xffff or the result of 4:0x3ecc). "Open a game file" for the main
 * window: BP-0x14a is a path buffer, BP-0x24a the filter text, BP-0x34a the file title and BP-0x4a an
 * OPENFILENAME (0x48 bytes, nFileOffset BP-0x16, nFileExtension BP-0x14) for GetOpenFileName.
 * With bit 0 of DS:0x251a ("use the current name") the name DS:0x5764 is copied to the buffer, its
 * last '\' cut off in DS:0x5764 itself (34:0x0578), and the extension looked up with 34:0x0578(buf,
 * '.'); with no extension 14:0x52bc(2, 0xffff) gives a name in DS:0x5866 that replaces the buffer.
 * Without the bit the filter string of 8:0x8dc0 (string 0x306 + (mode == 0), '|' turned to NUL) and the
 * dialog GetOpenFileName(BP-0x4a) (flags 0x1804, initial dir DS:0x212) choose the name; it asks again from
 * the check at 3c11 while the dialog answers non-zero. DS:0x76e is 1 while the check 4:0x3ecc(name,
 * mode == 0) runs. mode != 0 returns its result (a name copy to DS:0x1444 when positive). Mode 0: a
 * positive result clears bit 0 of DS:0x251a and, unless it was -1, calls 28:0x0000(hwnd, 0L); a
 * zero result loads the game with 14:0x2992 and 14:0x0508(buf, ext), clears bit 0 of DS:0x251a, keeps
 * the directory in DS:0x212 (34:0x0352), resets the selection DS:0x4b74.., redraws the caption (4:0x02ca),
 * and posts the "new game" command 0xfa1 to the main window DS:0x2806; with no current player it
 * destroys the start window DS:0x312 once and runs 4:0x4090. */
DECOMP(4, 0x3b66, open_game_file, FAR_CDECL)
uint16_t open_game_file(uint16_t hwnd, uint16_t mode)
{
  FRAME_ENTER(0x34a + 4);
  uint16_t si, di, ax, bx;
  if ((drd8(0x251a) & 1) != 0) {
    strcpy_at(rt_ss, bpoff(-0x14a), 0x5764);
    bx = (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, 0x5764, 0x5c);
    dwr8(bx, 0);
    L16(-2) = (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, bpoff(-0x14a), 0x2e);
    if (L16(-2) == 0) {
      (void)CALL_ORIG(14, 0x52bc, FAR_CDECL, 2, 0x0002, 0xffff);
      strcpy_at(MEM_SEL_DGROUP, bpoff(-0x14a), 0x5866);
      L16(-2) = (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, bpoff(-0x14a), 0x2e);
    }
    L16(-0x14) = (uint16_t)(L16(-2) - bpoff(-0x14b));
    L16(-0x16) = 0;
    dwr16(0x076e, 1);
    dwr8(0x0212, 0);
    goto L3c1e;
  }
  L8(-0x14a) = 0;
  (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)((mode == 0 ? 1 : 0) + 0x306), bpoff(-0x24a));
  si = 0;
  if (L8(-0x24a) != 0) {
    do {
      if (L8((int16_t)(-0x24a + (int16_t)si)) == 0x7c) L8((int16_t)(-0x24a + (int16_t)si)) = 0;
      si++;
    } while (L8((int16_t)(-0x24a + (int16_t)si)) != 0);
  }
  for (uint16_t k = 0; k < 0x48; k++) L8((int16_t)(-0x4a + (int16_t)k)) = 0;
  L16(-0x4a) = 0x48;
  L16(-0x48) = 0;
  L16(-0x46) = hwnd;
  L16(-0x42) = bpoff(-0x24a);
  L16(-0x40) = rt_ss;
  L16(-0x36) = 1;
  L16(-0x34) = 0;
  L16(-0x32) = bpoff(-0x14a);
  L16(-0x30) = rt_ss;
  L16(-0x2a) = bpoff(-0x34a);
  L16(-0x28) = rt_ss;
  L32(-0x2e) = 0x100;
  L32(-0x26) = 0x100;
  L16(-0x22) = 0x0212;
  L16(-0x20) = MEM_SEL_DGROUP;
  L16(-0x1a) = 0x1804;
  L16(-0x18) = 0;
  if (GetOpenFileName(bpfp(-0x4a)) == 0) goto L3ec5;
  dwr8(0x0212, 0); /* 3c11 */
L3c1e:
  {
    /* PUSH (mode == 0); PUSH BP + [BP-0x16] - 0x14a; CALL 4:0x3ecc */
    const uint16_t a[2] = {(uint16_t)(bpoff(-0x14a) + L16(-0x16)), (uint16_t)(mode == 0 ? 1 : 0)};
    L16(-2) = (uint16_t)call4(0x3ecc, a, 2);
  }
  if (mode != 0) {
    if ((int16_t)L16(-2) > 0) {
      di = (uint16_t)(bpoff(-0x14a) + L16(-0x16));
      strcpy_at(rt_ss, 0x1444, di);
    }
    ax = L16(-2);
    FRAME_LEAVE();
    return ax;
  }
  if (L16(-2) != 0) {
    if ((drd8(0x251a) & 1) != 0 && drd32(0x0872) == 0) L16(-2) = 0xffff;
    dwr16(0x076e, 0);
    dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfe));
    if (L16(-2) == 0xffff) {
      FRAME_LEAVE();
      return 0xffff;
    }
    (void)CALL_ORIG(28, 0x0000, FAR_CDECL, 3, hwnd, 0, 0);
    goto L3ec5;
  }
  /* 3d5e */
  L8((int16_t)(-0x14b + (int16_t)L16(-0x14))) = 0;
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  strcpy_at(rt_ss, 0x5764, bpoff(-0x14a));
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, bpoff(-0x14a), (uint16_t)(bpoff(-0x14a) + L16(-0x14))) == 0) {
    if ((drd8(0x251a) & 1) != 0) {
      dwr16(0x251a, 0);
      dwr16(0x076e, 0);
    }
    goto L3ec5;
  }
  if ((drd8(0x251a) & 1) != 0) {
    dwr16(0x076e, 0);
    dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfe));
    si = (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, bpoff(-0x14a), 0x5c);
    if (si != 0) {
      di = (uint16_t)(si - bpoff(-0x14a));
      (void)CALL_ORIG(34, 0x0352, FAR_CDECL, 3, 0x0212, bpoff(-0x14a), di);
      dwr8((uint16_t)(di + 0x0212), 0);
    }
    if (drd16(0x014c) == 0xffff && (drd8(0x251a) & 8) == 0) dwr8(0x0794, (uint8_t)(drd8(0x0794) | 4));
    if (drd32(0x2522) != drd32(0x0070) || drd16(0x251c) < drd16(0x0082)) dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xef));
  }
  dwr32(0x4b74, 0);
  dwr32(0x4b78, 0xffffffffu);
  dwr16(0x4b82, 0);
  dwr16(0x4b80, 0);
  dwr32(0x4b86, 0xffffffffu);
  dwr16(0x4b84, 0xffff);
  dwr16(0x055c, 0);
  (void)call4(0x02ca, NULL, 0);
  if (drd16(0x014c) == 0xffff) {
    if (drd16(0x0312) != 0 && drd16(0x0314) == 0) {
      dwr16(0x0314, 1);
      (void)DestroyWindow(drd16(0x0312));
      dwr16(0x0312, 0);
    }
    (void)call4(0x4090, NULL, 0);
    goto L3ec5;
  }
  si = (uint16_t)((drd16(0x251a) & 0x01e0) >> 5);
  (void)SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0);
  if (si != 0 && (drd16(0x251a) & 0x01e0) == 0) goto L3e7b;
  dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
  if ((drd16(0x251a) & 0x01e0) != 0) goto L3e7b;
  if (drd16(0x4d96) != 0) (void)call4(0x1e5e, NULL, 0);
L3e7b:
  FRAME_LEAVE();
  return 1;
L3ec5:
  FRAME_LEAVE();
  return 0;
}

/* 16-bit IDIV (CWD; IDIV cx): the original traps (#DE) on a zero divisor and on a quotient outside 16 bits. */
__attribute__((unused)) static void idiv16_cwd(uint16_t ax, uint16_t den, uint16_t *q, uint16_t *r)
{
  int16_t n = (int16_t)ax, d = (int16_t)den;
  if (d == 0 || (n == INT16_MIN && d == -1)) {
    fprintf(stderr, "4:idiv: divide error\n");
    abort();
  }
  *q = (uint16_t)(int16_t)(n / d);
  *r = (uint16_t)(int16_t)(n % d);
}

/* register_window_classes: 4:0x0000 (Ghidra FUN_1018_0000, alias fn_04_0000).
 * 689 bytes. ENTER 0x1a,0. FAR CDECL, no argument; returns 1 when every
 * class was registered, else 0 (the first failing RegisterClass). WNDCLASS16 at BP-0x1a (style, proc
 * offset/selector, cbClsExtra, cbWndExtra, hInstance DS:0x54f4, hIcon, hCursor, hbrBackground, menu name,
 * class name). Eleven classes, in order (proc segment:offset, style, background, class name DS:off):
 *   4:0x528 0xb brush 0xd (a literal, not a stock object), menu 0x324, name 0x196, LoadCursor(0, IDC_ARROW) |
 *   6:0 0x20b stock 1 name 0x1c2 (icon and menu cleared) | 11:0 0x20b stock 4 0x1a2 | 5:0 0x20b stock 1 0x1ac |
 *   13:0 0x208 stock 1 0x202 | 9:0 0x200 stock 1 0x1b6 | 24:0 0xa00 stock 0 0x1d0 | 13:0x1224 0xa00 stock 0 0x20a |
 *   27:0x1c68 0x200 stock 1 0x1dc | 4:0x60f6 style 0 stock 4 0x1ea, new LoadCursor | 32:0 0xb stock 1 0x1f6, new
 *   LoadCursor. Fields a class does not set keep the previous class's values (the same stack words). */
DECOMP(4, 0x0000, register_window_classes, FAR_CDECL)
uint16_t register_window_classes(void)
{
  FRAME_ENTER(0x1a);
  L16(-0x1a) = 0x000b;
  L16(-0x18) = 0x0528;
  L16(-0x16) = SEG_SEL(4);
  L16(-0x14) = 0;
  L16(-0x12) = 0;
  L16(-0x10) = drd16(0x54f4);
  L16(-0x0e) = 0;
  L16(-0x0c) = LoadCursor(0, 0x00007f00);
  L16(-0x0a) = 0x000d;
  L16(-8) = 0x0324;
  L16(-6) = MEM_SEL_DGROUP;
  L16(-4) = 0x0196;
  L16(-2) = MEM_SEL_DGROUP;
  if (RegisterClass(bpfp(-0x1a)) == 0) goto fail;
  {
    /* classes 2 to 9: { style, proc off, proc seg, stock brush, name, clears hIcon (and the menu for the first) } */
    static const struct { uint16_t style, off, seg, brush, name; uint8_t first, icon; } t[] = {
        {0x020b, 0x0000, 6, 1, 0x01c2, 1, 0},  {0x020b, 0x0000, 11, 4, 0x01a2, 0, 0},
        {0x020b, 0x0000, 5, 1, 0x01ac, 0, 0},  {0x0208, 0x0000, 13, 1, 0x0202, 0, 0},
        {0x0200, 0x0000, 9, 1, 0x01b6, 0, 1},  {0x0a00, 0x0000, 24, 0, 0x01d0, 0, 1},
        {0x0a00, 0x1224, 13, 0, 0x020a, 0, 1}, {0x0200, 0x1c68, 27, 1, 0x01dc, 0, 1},
    };
    for (uint16_t i = 0; i < sizeof t / sizeof t[0]; i++) {
      L16(-0x1a) = t[i].style;
      L16(-0x18) = t[i].off;
      L16(-0x16) = SEG_SEL(t[i].seg);
      if (t[i].first) {
        L16(-0x0e) = 0;
        L32(-8) = 0;
      }
      L16(-0x0a) = GetStockObject((int16_t)t[i].brush);
      if (t[i].icon) L16(-0x0e) = 0;
      L16(-4) = t[i].name;
      L16(-2) = MEM_SEL_DGROUP;
      if (RegisterClass(bpfp(-0x1a)) == 0) goto fail;
    }
  }
  /* title class 4:0x60f6 */
  L16(-0x18) = 0x60f6;
  L16(-0x16) = SEG_SEL(4);
  L16(-0x14) = 0;
  L16(-0x12) = 0;
  L16(-0x10) = drd16(0x54f4);
  L16(-0x1a) = 0;
  L16(-0x0e) = 0;
  L16(-0x0c) = LoadCursor(0, 0x00007f00);
  L16(-0x0a) = GetStockObject(4);
  L32(-8) = 0;
  L16(-4) = 0x01ea;
  L16(-2) = MEM_SEL_DGROUP;
  if (RegisterClass(bpfp(-0x1a)) == 0) goto fail;
  L16(-0x1a) = 0x000b;
  L16(-0x18) = 0;
  L16(-0x16) = SEG_SEL(32);
  L16(-0x14) = 0;
  L16(-0x12) = 0;
  L16(-0x10) = drd16(0x54f4);
  L16(-0x0e) = 0;
  L16(-0x0c) = LoadCursor(0, 0x00007f00);
  L16(-0x0a) = GetStockObject(1);
  L32(-8) = 0;
  L16(-4) = 0x01f6;
  L16(-2) = MEM_SEL_DGROUP;
  {
    uint16_t r = RegisterClass(bpfp(-0x1a)) != 0 ? 1 : 0;
    FRAME_LEAVE();
    return r;
  }
fail:
  FRAME_LEAVE();
  return 0;
}

/* layout_panels: 4:0x5d98 (Ghidra FUN_1018_5d98, alias fn_04_5d98).
 * 862 bytes. ENTER 0xa,0; PUSH DI; PUSH SI. FAR CDECL, no argument; no
 * result (AX left over). Lays out the child windows of the main window DS:0x2806 (not when it does not exist or
 * is iconic): the marker/side panels DS:0x150 (map), 0x154, 0x156, 0x158 and 0x15a, sized from the client size
 * DS:0x264a / 0x264c, the font height DS:0x267a and the minimum sizes DS:0x2654..0x265e. The mode word DS:0x255e
 * is 1 or 2 (the panels along the right side: layout B, columns DS:0x2650 / 0x2652 are the rows' tops) or any
 * other (layout A: the panels below the map). It stores the boundaries in DS:0x264e, 0x2650, 0x2652 (and
 * 0x2656 / 0x2658 / 0x265c / 0x265e), calls MoveWindow on each existing panel (DS:0x150 must exist) and
 * finally checks the three menu items 0x82 to 0x84 of the pop-up 4:0x3b30(DS:0x2806, 1) item 4 (8 for the
 * item 0x82 + DS:0x255e, else 0). */
DECOMP(4, 0x5d98, layout_panels, FAR_CDECL)
void layout_panels(void)
{
  FRAME_ENTER(0xa + 4);
  int16_t ax, cx, si, di, bx;
  if (drd16(0x2806) == 0) {
    FRAME_LEAVE();
    return;
  }
  if (IsIconic(drd16(0x2806)) != 0) {
    FRAME_LEAVE();
    return;
  }
  {
    int16_t v = (int16_t)drd16(0x255e);
    bool b = (v == 1 || v == 2);
    if (!b) {
      /* layout A (5dc3) */
      if ((int16_t)(drd16(0x264a) - drd16(0x2654)) >= 100)
        ax = (int16_t)drd16(0x2654);
      else
        ax = (int16_t)(drd16(0x264a) - 100);
      dwr16(0x264e, (uint16_t)ax);
      if (ax < 0xc6) ax = 0xc6;
      dwr16(0x264e, (uint16_t)ax);
      ax = (int16_t)drd16(0x267a);
      cx = ax;
      ax = (int16_t)(uint16_t)(ax + ax);
      ax = (int16_t)(uint16_t)(ax + cx);
      ax = (int16_t)(uint16_t)((uint16_t)ax << 2);
      ax = (int16_t)(uint16_t)(ax + cx);
      cx = ax;
      ax = (int16_t)(ax >> 1);
      ax = (int16_t)(uint16_t)(ax + 10);
      L16(-4) = (uint16_t)ax;
      cx = (int16_t)(uint16_t)(cx - 0x24);
      L16(-6) = (uint16_t)cx;
      if (ax < (int16_t)drd16(0x2656)) ax = (int16_t)drd16(0x2656);
      si = ax;
      dwr16(0x2656, (uint16_t)ax);
      if (cx < (int16_t)drd16(0x2658)) cx = (int16_t)drd16(0x2658);
      di = cx;
      dwr16(0x2658, (uint16_t)cx);
      ax = (int16_t)(uint16_t)(drd16(0x264c) - (uint16_t)cx - (uint16_t)si - 0x10);
      if (ax < 0x32) {
        ax = (int16_t)(uint16_t)(cx + si);
        L16(-8) = (uint16_t)ax;
        cx = (int16_t)(uint16_t)(drd16(0x264c) - 0x42);
        L16(-0xa) = (uint16_t)cx;
        si = MulDiv(cx, si, ax);
        ax = MulDiv((int16_t)L16(-0xa), di, (int16_t)L16(-8));
        di = ax;
        if ((int16_t)L16(-4) > si) {
          ax = (int16_t)(uint16_t)(si - (int16_t)L16(-4));
          di = (int16_t)(uint16_t)(di + ax);
          si = (int16_t)L16(-4);
        } else {
          int16_t dx = (int16_t)L16(-6);
          if (dx > di) {
            ax = (int16_t)(uint16_t)(ax - dx);
            si = (int16_t)(uint16_t)(si + ax);
            di = dx;
          }
        }
      }
      ax = (int16_t)(uint16_t)(drd16(0x264c) - (uint16_t)di - (uint16_t)si - 0x10);
      dwr16(0x2650, (uint16_t)ax);
      ax = (int16_t)(uint16_t)(ax + si + 8);
      dwr16(0x2652, (uint16_t)ax);
      if (drd16(0x0150) == 0) goto L60b5;
      if ((drd8(0x0793) & 0x80) != 0) {
        (void)MoveWindow(drd16(0x015a), (int16_t)(drd16(0x264e) + 8), 0, (int16_t)(drd16(0x264a) - drd16(0x264e) - 8), 0x24, 1);
        L16(-4) = 0x24;
      } else {
        (void)MoveWindow(drd16(0x015a), 0, (int16_t)0xff9c, 0x32, 0x32, 1);
        L16(-4) = 0;
      }
      (void)MoveWindow(drd16(0x0150), (int16_t)(drd16(0x264e) + 8), (int16_t)L16(-4),
                       (int16_t)(drd16(0x264a) - drd16(0x264e) - 8), (int16_t)(drd16(0x264c) - L16(-4)), 1);
      (void)MoveWindow(drd16(0x0156), 0, 0, (int16_t)drd16(0x264e), (int16_t)drd16(0x2650), 1);
      (void)MoveWindow(drd16(0x0158), 0, (int16_t)(drd16(0x2650) + 8), (int16_t)drd16(0x264e), si, 1);
      (void)MoveWindow(drd16(0x0154), 0, (int16_t)(drd16(0x2652) + 8), (int16_t)drd16(0x264e), di, 1);
    } else {
      /* layout B (5f50) */
      if ((int16_t)(drd16(0x264a) - drd16(0x265a)) >= 0xc8)
        ax = (int16_t)drd16(0x265a);
      else
        ax = (int16_t)(drd16(0x264a) - 0xc8);
      dwr16(0x264e, (uint16_t)ax);
      if (ax < 0xc6) ax = 0xc6;
      dwr16(0x264e, (uint16_t)ax);
      ax = (int16_t)drd16(0x267a);
      cx = ax;
      ax = (int16_t)(uint16_t)(ax + ax);
      ax = (int16_t)(uint16_t)(ax + cx);
      ax = (int16_t)(uint16_t)((uint16_t)ax << 2);
      ax = (int16_t)(uint16_t)(ax + cx);
      cx = ax;
      ax = (int16_t)(ax >> 1);
      ax = (int16_t)(uint16_t)(ax + 10);
      if (ax < (int16_t)drd16(0x265c)) ax = (int16_t)drd16(0x265c);
      bx = ax;
      dwr16(0x265c, (uint16_t)ax);
      cx = (int16_t)(uint16_t)(cx - 0x24);
      if (cx < (int16_t)drd16(0x265e)) cx = (int16_t)drd16(0x265e);
      di = cx;
      dwr16(0x265e, (uint16_t)cx);
      L16(-2) = (uint16_t)bx;
      ax = (int16_t)(uint16_t)(drd16(0x264c) - (uint16_t)bx - 8);
      if (ax < 100) L16(-2) = (uint16_t)(drd16(0x264c) - 0x6c);
      ax = (int16_t)(uint16_t)(drd16(0x264c) - (uint16_t)di - 8);
      if (ax < 100) di = (int16_t)(uint16_t)(drd16(0x264c) - 0x6c);
      dwr16(0x2650, (uint16_t)(drd16(0x264c) - L16(-2) - 8));
      dwr16(0x2652, (uint16_t)(drd16(0x264c) - (uint16_t)di - 8));
      if (drd16(0x0150) == 0) goto L60b5;
      if ((drd8(0x0793) & 0x80) != 0) {
        (void)MoveWindow(drd16(0x015a), 0, 0, (int16_t)drd16(0x264a), 0x24, 1);
        si = 0x24;
      } else {
        (void)MoveWindow(drd16(0x015a), 0, (int16_t)0xff9c, 0x32, 0x32, 1);
        si = 0;
      }
      (void)MoveWindow(drd16(0x0150), (int16_t)(drd16(0x264e) + 8), si, (int16_t)(drd16(0x264a) - drd16(0x264e) - 8),
                       (int16_t)(uint16_t)(0 - (uint16_t)(si - (int16_t)drd16(0x2652))), 1);
      (void)MoveWindow(drd16(0x0156), 0, si, (int16_t)drd16(0x264e),
                       (int16_t)(uint16_t)(0 - (uint16_t)(si - (int16_t)drd16(0x2650))), 1);
      (void)MoveWindow(drd16(0x0158), 0, (int16_t)(drd16(0x2650) + 8), (int16_t)drd16(0x264e), (int16_t)L16(-2), 1);
      (void)MoveWindow(drd16(0x0154), (int16_t)(drd16(0x264e) + 8), (int16_t)(drd16(0x2652) + 8),
                       (int16_t)(drd16(0x264a) - drd16(0x264e) - 8), di, 1);
    }
  }
L60b5:
  {
    const uint16_t a[2] = {drd16(0x2806), 1};
    uint16_t m = (uint16_t)call4(0x3b30, a, 2);
    di = (int16_t)GetSubMenu(m, 4);
    for (si = 0x82; si <= 0x84; si++) {
      uint16_t flag = ((uint16_t)(si - drd16(0x255e)) == 0x82) ? 8 : 0;
      (void)CheckMenuItem((uint16_t)di, (uint16_t)si, flag);
    }
  }
  FRAME_LEAVE();
}

/* PatBlt(PATCOPY) of the frame lines of 4:0x0528 (WM_PAINT): every call in the original pushes the ROP 0xf00021. */
static void frame_patblt(uint16_t dc, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  (void)PatBlt(dc, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, 0x00f00021u);
}
/* 11 bytes of DS:a against DS:b (REPE CMPSW x5, REPE CMPSB x1): equal or not. */
static bool ds_equal11(uint16_t a, uint16_t b)
{
  for (uint16_t k = 0; k < 11; k++)
    if (drd8((uint16_t)(a + k)) != drd8((uint16_t)(b + k))) return false;
  return true;
}
/* MOVSD x2, MOVSW, MOVSB: 11 bytes from DS:src to DS:dst. */
static void ds_copy11(uint16_t dst, uint16_t src)
{
  for (uint16_t k = 0; k < 11; k++) dwr8((uint16_t)(dst + k), drd8((uint16_t)(src + k)));
}
/* The common exit of the "quit" paths (4:0x067d): PostQuitMessage(DS:0x366), or ExitWindows when bit 6 of DS:0x792. */
static void frame_quit(void)
{
  if ((drd8(0x0792) & 0x40) == 0)
    PostQuitMessage((int16_t)drd16(0x0366));
  else
    (void)ExitWindows((uint32_t)(int32_t)(int16_t)drd16(0x0366), 0);
}

/* 4:0x0528 FRAMEWNDPROC (Ghidra FRAMEWNDPROC), 3858 bytes (to 4:0x143a). ENTER 0x24,0; PUSH DI; PUSH SI. FAR PASCAL
 * (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]), returns DX:AX (DX = 0 except for DefWindowProc /
 * SendMessage results). The window procedure of the main window of the game. The jump table at 4:0x055c (21 words
 * for the messages 1 to 0x15) is replaced by the switch below; the messages and what they do:
 *   0x01 WM_CREATE     fonts (1:0x08a4(dc), GetTextMetrics: DS:0x59dc = height; DS:0x2646 = (DS:0x267a + 12) * 2),
 *                      1:0x0bb4, 9:0x3aa8(DS:0x255e == 2)
 *   0x02 WM_DESTROY    kills the timer DS:0x162, 4:0x506c, 14:0x54ea / 14:0x2992, quits (4:0x067d)
 *   0x05 WM_SIZE       restored or normal: DS:0x264a / 0x264c = the client size, 4:0x5d98
 *   0x0f WM_PAINT      the icon (iconic) or the frame lines between the panels (layout of DS:0x255e as in 4:0x5d98)
 *   0x10 WM_CLOSE      DestroyWindow;  0x14 WM_ERASEBKGND;  0x15 / 0x1a  3:0x054c
 *   0x20 WM_SETCURSOR  the divider bars 4:0x18b0;  0x24 WM_GETMINMAXINFO  (0x208, 0x17c);  0x37 WM_QUERYDRAGICON
 *   0x102 WM_CHAR      keys to the panels, 9:0x2ed8 / 10:0x26b2 for next / previous object
 *   0x111 WM_COMMAND   4:0x1f40(hwnd, id);  0x112 WM_SYSCOMMAND (maximise, restore);  0x116 4:0x379a;  0x121 31:0x61b0
 *   0x201 WM_LBUTTONDOWN  drags a divider (4:0x18b0, 4:0x143a, 8:0xa2c6) and stores the panel sizes
 *   0x30f / 0x311 the palette;  0x464 start-up/continue;  0x465 4:0x4090;  0x466 load the game DS:0x5764
 * Everything else, and the cases that fall through, go to DefWindowProc. */
DECOMP(4, 0x0528, FRAMEWNDPROC, FAR_PASCAL)
uint32_t FRAMEWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x24 + 4);
  uint16_t di, si, ax, cx;
  const uint16_t lp_lo = (uint16_t)lParam, lp_hi = (uint16_t)(lParam >> 16);
  di = msg;
  if (msg == 0x37) goto m37;
  if (msg > 0x37) {
    if (msg == 0x201) goto m201;
    if (msg > 0x201) {
      if (msg == 0x30f) goto m30f;
      if (msg == 0x311) goto m311;
      if (msg == 0x464) goto m464;
      if (msg == 0x465) goto m465;
      if (msg == 0x466) goto m466;
      goto deflt;
    }
    if (msg == 0x102) goto m102;
    if (msg == 0x111) goto m111;
    if (msg == 0x112) goto m112;
    if (msg == 0x116) goto m116;
    if (msg == 0x121) goto m121;
    goto deflt;
  }
  if (msg == 0x24) goto m24;
  if (msg > 0x24) goto deflt;
  if (msg == 0x1a) goto m15;
  if (msg > 0x1a) {
    if (msg == 0x20) goto m20;
    goto deflt;
  }
  ax = (uint16_t)((msg & 0xff00u) | (uint8_t)(msg - 1)); /* DEC AL */
  if (ax > 0x14) goto deflt;
  switch (ax) {
    case 0x00: goto m01;
    case 0x01: goto m02;
    case 0x04: goto m05;
    case 0x05: goto ret0;
    case 0x0e: goto m0f;
    case 0x0f: goto m10;
    case 0x13: goto m14;
    case 0x14: goto m15;
    default: goto deflt;
  }

deflt: {
    uint32_t r = DefWindowProc(hwnd, msg, wParam, lParam);
    FRAME_LEAVE();
    return r;
  }
ret0:
  FRAME_LEAVE();
  return 0;
ret1:
  FRAME_LEAVE();
  return 1;

m01: /* WM_CREATE */
  di = hwnd;
  si = GetDC(di);
  {
    const uint16_t a[1] = {si};
    (void)rt_call_far(MK_FP(SEG_SEL(1), 0x08a4), FAR_CDECL, a, 1);
  }
  (void)GetTextMetrics(si, bpfp(-0x22));
  dwr16(0x59dc, L16(-0x22));
  dwr16(0x2646, (uint16_t)((uint16_t)(drd16(0x267a) + 0x0c) * 2));
  (void)ReleaseDC(di, si);
  (void)rt_call_far(MK_FP(SEG_SEL(1), 0x0bb4), FAR_CDECL, NULL, 0);
  (void)CALL_ORIG(9, 0x3aa8, FAR_CDECL, 1, drd16(0x255e) == 2 ? 1 : 0);
  goto ret0;

m02: /* WM_DESTROY */
  if (drd16(0x0162) != 0) (void)KillTimer(0, drd16(0x0162));
  (void)call4(0x506c, NULL, 0);
  dwr16(0x0162, 0);
  if ((drd8(0x0790) & 8) != 0) (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 2, 0xffff, 1, 0);
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
L067d:
  frame_quit();
  goto ret0;

m05: /* WM_SIZE */
  if (wParam != 2 && wParam != 0) goto deflt;
  dwr16(0x264a, lp_lo);
  dwr16(0x264c, lp_hi);
  (void)call4(0x5d98, NULL, 0);
  goto ret0;

m0f: /* WM_PAINT */
  di = hwnd;
  if (IsIconic(di) != 0) {
    L16(-2) = BeginPaint(di, bpfp(-0x22));
    if (drd16(0x014c) == 0xffff && drd32(0x0070) != 0)
      cx = drd16(0x0318);
    else if (drd16(0x0162) == 0)
      cx = drd16(0x0316);
    else
      cx = drd16(0x031a);
    (void)DrawIcon(L16(-2), 2, 2, cx);
    EndPaint(di, bpfp(-0x22));
    goto ret0;
  }
  L16(-2) = BeginPaint(di, bpfp(-0x24));
  L16(-4) = SelectObject(L16(-2), drd16(0x0016));
  {
    const uint16_t dc = L16(-2);
    const int16_t v = (int16_t)drd16(0x255e);
    if (!(v == 1 || v == 2)) {
      /* layout A */
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 5), 0, 2, drd16(0x264c));
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2650) + 5), (uint16_t)(drd16(0x264e) + 2), 2);
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2652) + 5), (uint16_t)(drd16(0x264e) + 2), 2);
      (void)SelectObject(dc, drd16(0x0014));
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 1), 0, 1, (uint16_t)(drd16(0x2650) + 2));
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2650) + 1), (uint16_t)(drd16(0x264e) + 1), 1);
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 1), (uint16_t)(drd16(0x2650) + 6), 1,
                   (uint16_t)(drd16(0x2652) - drd16(0x2650) - 4));
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2652) + 1), (uint16_t)(drd16(0x264e) + 1), 1);
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 1), (uint16_t)(drd16(0x2652) + 6), 1,
                   (uint16_t)(drd16(0x264c) - drd16(0x2652) - 6));
    } else {
      /* layout B */
      si = (drd8(0x0793) & 0x80) != 0 ? 0x24 : 0;
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 5), si, 2, (uint16_t)(drd16(0x2652) - si + 2));
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2650) + 5), (uint16_t)(drd16(0x264e) + 2), 2);
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 5), (uint16_t)(drd16(0x2652) + 5),
                   (uint16_t)(drd16(0x264a) - drd16(0x264e) - 5), 2);
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 5), (uint16_t)(drd16(0x2652) + 6), 2,
                   (uint16_t)(drd16(0x264c) - drd16(0x2652) - 5));
      (void)SelectObject(dc, drd16(0x0014));
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 1), si, 1, (uint16_t)(drd16(0x2650) - si + 2));
      frame_patblt(dc, 0, (uint16_t)(drd16(0x2650) + 1), (uint16_t)(drd16(0x264e) + 1), 1);
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 1), (uint16_t)(drd16(0x2650) + 6), 1,
                   (uint16_t)(drd16(0x264c) - drd16(0x2650) - 6));
      frame_patblt(dc, (uint16_t)(drd16(0x264e) + 6), (uint16_t)(drd16(0x2652) + 1),
                   (uint16_t)(drd16(0x264a) - drd16(0x264e) - 6), 1);
    }
    /* the eighth line of each layout is the one the original reaches through the shared tail at 4:0x0974 */
  }
  (void)SelectObject(L16(-2), L16(-4));
  EndPaint(di, bpfp(-0x24));
  goto ret0;

m10: /* WM_CLOSE */
  (void)DestroyWindow(hwnd);
  goto ret0;

m14: /* WM_ERASEBKGND */
  si = hwnd;
  if (IsIconic(si) != 0) {
    GetClientRect(si, bpfp(-8));
    (void)FillRect(wParam, bpfp(-8), drd16(0x001e));
    goto ret0;
  }
  GetClientRect(si, bpfp(-0x10));
  if (drd16(0x0150) != 0) {
    GetClientRect(drd16(0x0150), bpfp(-8));
    MapWindowPoints(drd16(0x0150), si, bpfp(-8), 2);
    (void)ExcludeClipRect(wParam, (int16_t)L16(-8), (int16_t)L16(-6), (int16_t)L16(-4), (int16_t)L16(-2));
  }
  (void)FillRect(wParam, bpfp(-0x10), drd16(0x0010));
  goto ret1;

m15: /* WM_SYSCOLORCHANGE, WM_WININICHANGE */
  (void)rt_call_far(MK_FP(SEG_SEL(3), 0x054c), FAR_CDECL, NULL, 0);
  goto ret0;

m20: /* WM_SETCURSOR */
  di = hwnd;
  if (IsIconic(di) != 0) goto deflt;
  GetCursorPos(bpfp(-4));
  ScreenToClient(drd16(0x2806), bpfp(-4));
  GetClientRect(di, bpfp(-0xc));
  if (PtInRect(bpfp(-0xc), L32(-4)) == 0) goto deflt;
  {
    const uint16_t a[3] = {L16(-4), L16(-2), 0};
    si = (uint16_t)call4(0x18b0, a, 3);
  }
  if (si == 0) goto deflt;
  (void)SetCursor(si);
  goto ret1;

m24: /* WM_GETMINMAXINFO */
  wr32((uint16_t)(lParam >> 16), (uint16_t)((uint16_t)lParam + 0x0c), 0x017c0208u);
  goto ret0;

m37: /* WM_QUERYDRAGICON */
  {
    uint16_t bx;
    if (drd16(0x014c) == 0xffff)
      bx = drd16(0x0318);
    else if (drd16(0x0162) == 0)
      bx = drd16(0x0316);
    else
      bx = drd16(0x031a);
    FRAME_LEAVE();
    return bx;
  }

m102: /* WM_CHAR */
  si = wParam;
  if (drd16(0x0150) != 0 && (si == 0x2d || si == 0x2b)) {
    (void)API_SP(10, SendMessage(drd16(0x0150), 0x0102, si, lParam));
    goto ret0;
  }
  if (drd16(0x0158) != 0 && (si == 0x2d || si == 0x2b || si == 0x0d)) {
    (void)API_SP(10, SendMessage(drd16(0x0158), 0x0102, si, lParam));
    goto ret0;
  }
  if (drd16(0x0156) != 0 && (si == 0x66 || si == 0x46)) {
    (void)API_SP(10, SendMessage(drd16(0x0156), 0x0102, si, lParam));
    goto ret0;
  }
  if (drd16(0x0156) != 0 && drd16(0x4b74) == 1 && (si == 0x71 || si == 0x51)) {
    (void)CALL_ORIG(26, 0x0000, FAR_CDECL, 1, 0);
    goto ret0;
  }
  if ((drd8(0x4b74) & 3) == 0) goto ret0;
  L16(-4) = 0;
  L16(-2) = 0;
  if (si == 0x6e) {
    L16(-4) = 1;
  } else if (si == 0x4e || si == 0x50) {
    if (drd16(0x4b74) == 1) {
      L16(-2) = (uint16_t)CALL_ORIG(9, 0x3082, FAR_CDECL, 2, drd16(0x4c08), si == 0x4e ? 1 : 0);
    } else {
      L16(-4) = si == 0x4e ? 1 : 0xffff;
    }
  } else if (si == 0x70) {
    L16(-4) = 0xffff;
  } else if ((si == 0x72 || si == 0x52) && drd16(0x4b74) == 2) {
    (void)CALL_ORIG(10, 0x1aec, FAR_CDECL, 4, drd16(0x0156), 0, drd16(0x291e), 0);
  }
  if (L16(-4) == 0 && L16(-2) == 0) goto ret0;
  if (drd16(0x4b74) == 2)
    (void)CALL_ORIG(10, 0x26b2, FAR_CDECL, 2, L16(-4), L16(-2));
  else
    (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, L16(-4), L16(-2));
  goto ret0;

m111: /* WM_COMMAND */
  {
    const uint16_t a[2] = {hwnd, wParam};
    (void)call4(0x1f40, a, 2);
  }
  goto ret0;

m112: /* WM_SYSCOMMAND */
  if ((wParam & 0xfff0u) != 0xf030 && (wParam & 0xfff0u) != 0xf120) goto deflt;
  di = drd16(0x0162);
  if (di != 0) {
    (void)KillTimer(0, di);
    dwr16(0x0162, 0);
    (void)call4(0x02ca, NULL, 0);
  }
  if (drd16(0x014c) != 0xffff && (uint16_t)CALL_ORIG(14, 0x2f40, FAR_CDECL, 1, drd16(0x014c)) != 0) {
    if (di != 0) {
      (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b6, 0x0040);
      si = 6;
    } else {
      si = (uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b5, 0x2023);
    }
    if (si == 6) {
      {
        uint16_t sp0 = rt_sp;
        wr16(rt_ss, (uint16_t)(sp0 - 2), (uint16_t)(drd16(0x014c) + 1));
        rt_sp = (uint16_t)(sp0 - 2);
        (void)_wsprintf(bpfp(-4), dsfp(drd16(0x0190)), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
        rt_sp = sp0;
      }
      (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
      if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, bpoff(-4)) == 0)
        (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x001b, 0x0010);
      else
        (void)call4(0x02ca, NULL, 0);
    } else if (si == 2) {
      if (di != 0) {
        (void)PostMessage(drd16(0x2806), 0x0111, 0x006a, 0);
      } else {
        (void)PostMessage(drd16(0x2806), 0x0112, 0xf020, 0);
      }
      goto ret1;
    }
    (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
    goto deflt;
  }
  /* 4:0x0cfe */
  if (di == 0) goto deflt;
  if (drd16(0x0164) == 0x000d) {
    (void)PostMessage(hwnd, 0x0465, 0, 0);
    goto deflt;
  }
  if (drd16(0x0164) == 0x000e) {
    di = (uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x018a, 0x2023);
    if (di == 6) {
      if ((uint16_t)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 1, drd16(0x014c), 2, 0) == 0) {
        (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x018b, 0x0010);
        {
          uint16_t sp0 = rt_sp;
          wr16(rt_ss, (uint16_t)(sp0 - 2), (uint16_t)(drd16(0x014c) + 1));
          rt_sp = (uint16_t)(sp0 - 2);
          (void)_wsprintf(bpfp(-4), dsfp(drd16(0x0190)), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
          rt_sp = sp0;
        }
        (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
        if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, bpoff(-4)) == 0)
          (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x001b, 0x0010);
        else
          (void)call4(0x02ca, NULL, 0);
        goto L0da0;
      }
    }
    if (di == 2) {
      (void)PostMessage(drd16(0x2806), 0x0111, 0x006a, 0);
      goto ret1;
    }
  }
L0da0:
  (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
  if ((int16_t)drd16(0x4b70) > 0x3e8 && (int16_t)drd16(0x4b72) > 0x3e8)
    (void)CALL_ORIG(11, 0x5142, FAR_CDECL, 3, drd16(0x4b70), drd16(0x4b72), 1);
  goto deflt;

m116: /* 0x116 */
  {
    const uint16_t a[1] = {wParam};
    (void)call4(0x379a, a, 1);
  }
  goto ret0;

m121: /* 0x121 */
  if ((drd8(0x0791) & 8) == 0) goto ret0;
  if ((drd8(0x53f0) & 4) == 0) goto ret0;
L0e0c:
  (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  goto ret0;

m201: /* WM_LBUTTONDOWN */
  L16(-0xa) = lp_lo;
  L16(-8) = lp_hi;
  {
    const uint16_t a[3] = {L16(-0xa), L16(-8), bpoff(-6)};
    ax = (uint16_t)call4(0x18b0, a, 3);
  }
  if (ax == 0) goto ret0;
  di = hwnd;
  si = GetDC(di);
  L16(-4) = SelectObject(si, drd16(0x4d92));
  L32(-0x12) = 0;
  {
    const uint16_t a[5] = {si, L16(-6), 0, 0, 0};
    (void)call4(0x143a, a, 5);
  }
  L32(-0x1a) = L32(-0xa);
  (void)SetCapture(di);
  L32(-0x16) = L32(-0xa);
  L32(-0xe) = 0;
  if ((uint16_t)CALL_ORIG(8, 0xa2c6, FAR_CDECL, 1, bpoff(-0xa)) != 0) {
    do {
      L16(-2) = si;
      if (!(L16(-0x16) == L16(-0xa) && L16(-0x14) == L16(-8))) {
        L16(-0x12) = (uint16_t)(L16(-0xa) - L16(-0x1a));
        L16(-0x10) = (uint16_t)(L16(-8) - L16(-0x18));
        L16(-0x1e) = (uint16_t)(L16(-0xa) - L16(-0x16));
        L16(-0x1c) = (uint16_t)(L16(-8) - L16(-0x14));
        const uint16_t a[5] = {si, L16(-6), L16(-0x12), L16(-0x10), bpoff(-0x1e)};
        uint32_t r = call4(0x143a, a, 5);
        L16(-0xe) = (uint16_t)r;
        L16(-0xc) = (uint16_t)(r >> 16);
        L32(-0x16) = L32(-0xa);
      }
    } while ((uint16_t)CALL_ORIG(8, 0xa2c6, FAR_CDECL, 1, bpoff(-0xa)) != 0);
  }
  {
    const uint16_t a[5] = {si, L16(-6), L16(-0x12), L16(-0x10), 0};
    (void)call4(0x143a, a, 5);
  }
  ReleaseCapture();
  (void)SelectObject(si, L16(-4));
  (void)ReleaseDC(hwnd, si);
  if (L16(-0xe) == 0 && L16(-0xc) == 0) goto ret0;
  if ((L8(-6) & 1) != 0) {
    if (drd16(0x255e) == 0)
      dwr16(0x2654, (uint16_t)(L16(-0xe) + drd16(0x264e)));
    else
      dwr16(0x265a, (uint16_t)(L16(-0xe) + drd16(0x264e)));
  }
  if ((L8(-6) & 2) != 0) {
    if (drd16(0x255e) == 0)
      dwr16(0x2656, (uint16_t)(drd16(0x2652) - L16(-0xc) - drd16(0x2650) - 8));
    else
      dwr16(0x265c, (uint16_t)(drd16(0x264c) - L16(-0xc) - drd16(0x2650) - 8));
  }
  if ((L8(-6) & 4) != 0) {
    if (drd16(0x255e) == 0) {
      dwr16(0x2656, (uint16_t)(L16(-0xc) - drd16(0x2650) + drd16(0x2652) - 8));
      dwr16(0x2658, (uint16_t)(drd16(0x264c) - L16(-0xc) - drd16(0x2652) - 8));
    } else {
      dwr16(0x265e, (uint16_t)(drd16(0x264c) - L16(-0xc) - drd16(0x2652) - 8));
    }
  }
  InvalidateRect(hwnd, 0, 1);
  (void)call4(0x5d98, NULL, 0);
  goto ret0;

m311: /* WM_PALETTECHANGED */
  if (wParam == hwnd) goto ret0;
m30f: /* WM_QUERYNEWPALETTE */
  if (drd16(0x0312) != 0) {
    uint32_t r = API_SP(10, SendMessage(drd16(0x0312), di, wParam, lParam));
    FRAME_LEAVE();
    return r;
  }
  si = GetDC(hwnd);
  di = SelectPalette(si, drd16(0x54f8), 0);
  L16(-4) = RealizePalette(si);
  (void)SelectPalette(si, di, 0);
  (void)ReleaseDC(hwnd, si);
  if (L16(-4) == 0) goto ret0;
  InvalidateRect(hwnd, 0, 1);
  goto ret1;

m465: /* 0x465 */
  (void)call4(0x4090, NULL, 0);
  goto ret1;

m464: /* 0x464 */
  dwr16(0x014c, 0xffff);
  if ((drd8(0x251a) & 2) == 0) goto L11b6;
  dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfd));
  if ((drd8(0x251b) & 4) != 0) {
    if (drd32(0x0872) != 0) (void)CALL_ORIG(15, 0x39d4, FAR_CDECL, 1, 0x5764);
    goto L067d;
  }
  if ((drd8(0x251a) & 8) == 0) goto L10dc;
L1039:
  if ((drd8(0x251a) & 4) != 0 || (drd8(0x251a) & 0x10) != 0) {
    if ((uint16_t)call4(0x451c, NULL, 0) != 0) {
      if ((drd8(0x251a) & 0x10) == 0) goto L10dc;
      if ((drd8(0x251b) & 2) == 0) goto L067d;
      if (drd16(0x5394) <= drd16(0x015e)) goto L067d;
      goto L1087;
    }
  }
  (void)call4(0x39f8, NULL, 0);
  (void)CALL_ORIG(22, 0x0000, FAR_CDECL, 0);
  if ((drd8(0x251b) & 2) != 0 && drd16(0x5394) > drd16(0x015e)) goto L1087;
  if (drd16(0x2526) == 0) goto L067d;
  dwr16(0x2526, (uint16_t)(drd16(0x2526) - 1));
  goto L1039;
L1087:
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  {
    uint16_t dst = 0x5764;
    uint16_t offs = drd16(0x015e), segs = drd16(0x0160);
    while (rd8(segs, offs) != 0x0a) {
      if (drd16(0x5394) == offs && drd16(0x5396) == segs) break;
      dwr8(dst, rd8(segs, offs));
      dst++;
      dwr16(0x015e, (uint16_t)(drd16(0x015e) + 1));
      offs = drd16(0x015e);
      segs = drd16(0x0160);
    }
    dwr16(0x015e, (uint16_t)(drd16(0x015e) + 1));
    dwr8((uint16_t)(dst - 1), 0);
  }
  dwr8(0x251a, (uint8_t)(drd8(0x251a) | 1));
  goto L1039;
L10dc:
  {
    const uint16_t a[2] = {hwnd, 0x0ed9};
    (void)call4(0x1f40, a, 2);
  }
  if ((drd8(0x251a) & 0x10) != 0) goto L067d;
  if ((drd8(0x251a) & 8) != 0) goto L120b;
  if (drd32(0x0070) == 0) goto L11b6;
  if (drd16(0x014c) != 0xffff && ((drd8(0x251b) & 0x18) != 0 || (drd8(0x251b) & 0x20) != 0)) {
    if ((drd8(0x251b) & 0x20) != 0) (void)PostMessage(drd16(0x2806), 0x0111, 0x0055, 0);
    if ((drd8(0x251b) & 0x10) != 0) (void)PostMessage(drd16(0x2806), 0x0111, 0x0054, 0);
    if ((drd8(0x251b) & 8) != 0) (void)PostMessage(drd16(0x2806), 0x0111, 0x0053, 0);
    goto L067d;
  }
  (void)ShowWindow(drd16(0x2806), 5);
  {
    const uint16_t a[1] = {0};
    (void)call4(0x379a, a, 1);
  }
  (void)PostMessage(drd16(0x2806), 0x0111, 0x0fa1, 0);
  if ((drd8(0x251a) & 4) != 0) {
    dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfb));
    const uint16_t a[2] = {hwnd, 0x006a};
    (void)call4(0x1f40, a, 2);
  }
  goto L120b;
L11b6:
  if (drd16(0x0312) == 0) {
    L16(-0xa) = (uint16_t)GetSystemMetrics(0);
    ax = (uint16_t)GetSystemMetrics(1);
    ax = CreateWindow(dsfp(0x01ea), dsfp(0x0368), 0x90000000u, 0, 0, (int16_t)L16(-0xa), (int16_t)ax, drd16(0x2806), 0,
                      drd16(0x54f4), 0);
    dwr16(0x0312, ax);
    dwr16(0x0314, 0);
  }
  dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfe));
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
L120b:
  if (drd32(0x0872) != 0 && ds_equal11(0x5382, 0x564c)) goto ret0;
  dwr8(0x588e, (drd32(0x0872) != 0) ? 1 : 0);
  {
    uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(6), 0x7ed0), drd16(0x54f4));
    si = (uint16_t)pi;
    L16(-4) = (uint16_t)(pi >> 16);
    cx = drd16(0x0312) != 0 ? drd16(0x0312) : drd16(0x2806);
    L16(-2) = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x00000056, cx, pi));
    FreeProcInstance(pi);
  }
  if (L16(-2) != 0) {
    if ((uint16_t)CALL_ORIG(7, 0x3ef4, FAR_CDECL, 2, 0x5866, bpoff(-4)) != 0) {
      dwr32(0x0872, L32(-4));
      ds_copy11(0x5382, 0x564c);
      goto L12f1;
    }
    if (drd32(0x0872) != 0) goto L12f1;
  } else {
    dwr32(0x0872, 0);
  }
  ds_copy11(0x5382, 0x564c);
  PostQuitMessage((int16_t)drd16(0x0366));
L12f1:
  (void)call4(0x506c, NULL, 0);
  goto ret0;

m466: /* 0x466 */
  (void)CALL_ORIG(31, 0x5cc2, FAR_CDECL, 1, 0);
  dwr16(0x007e, 0);
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  di = drd16(0x076e);
  dwr16(0x076e, 1);
  dwr8(0x0792, (uint8_t)(drd8(0x0792) | 2));
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, 0x036f) == 0) {
    dwr16(0x076e, di);
    dwr8(0x0792, (uint8_t)(drd8(0x0792) & 0xfd));
    goto ret0;
  }
  si = wParam;
  dwr8(0x0792, (uint8_t)(drd8(0x0792) & 0xfd));
  dwr16(0x076e, di);
  dwr16(0x014c, 0);
  if (si == 0x09ca) {
    dwr8(0x0790, (uint8_t)(drd8(0x0790) | 2));
    {
      pushw(MEM_SEL_DGROUP);
      pushw(0x5764);
      (void)_wsprintf(dsfp(0x5866), dsfp(0x0372), MK_FP(rt_ss, rt_sp));
      popw(2);
    }
    if ((uint16_t)CALL_ORIG(9, 0x78a0, FAR_CDECL, 1, 0x5866) != 0) (void)CALL_ORIG(9, 0x649a, FAR_CDECL, 0);
    dwr8(0x0790, (uint8_t)(drd8(0x0790) & 0xfd));
  }
  (void)call4(0x02ca, NULL, 0);
  (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
  if (si == 0x09ca) (void)API_SP(10, SendMessage(drd16(0x0158), 0x0100, 0x0023, 0));
  dwr16(0x53f2, 0);
  {
    uint16_t axx = (uint16_t)((si == 0x09ca ? 1 : 0) << 9);
    dwr8(0x53f0, (uint8_t)(drd8(0x53f0) & 0xf7));
    uint8_t ah = (uint8_t)((axx >> 8) ^ drd8(0x53f1));
    axx = (uint16_t)(((uint16_t)ah << 8 | (axx & 0xff)) & 0x0200);
    dwr16(0x53f0, (uint16_t)(drd16(0x53f0) ^ axx));
  }
  goto L0e0c;
}

/* PatBlt(PATINVERT) of the drag bars of 4:0x143a (the original pushes the ROP 0x5a0049 each time). */
static void drag_patblt(uint16_t dc, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  (void)PatBlt(dc, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, 0x005a0049u);
}
/* (13 * DS:0x267a) in 16 bits: ADD AX,AX; ADD AX,DX; SHL AX,2; ADD AX,DX. */
static uint16_t times13(void)
{
  uint16_t h = drd16(0x267a);
  return (uint16_t)((uint16_t)((uint16_t)(h + h) + h) * 4u + h);
}

/* drag_divider_bars: 4:0x143a (Ghidra FUN_1018_143a, alias fn_04_143a).
 * 1142 bytes. ENTER 0x14,0; PUSH DI; PUSH SI. FAR CDECL (hdc [BP+6], bits
 * [BP+8], dx [BP+0xA], dy [BP+0xC], prev [BP+0xE] a near pointer to two words or 0). Returns DX:AX = (dy':dx')
 * (AX = [BP+0xA], DX = [BP+0xC] at the end). Draws (PATINVERT) and limits the drag bars of the main window while a divider
 * is dragged (4:0x0528 WM_LBUTTONDOWN): bits 1 = the vertical divider at DS:0x264e, 2 = the first horizontal one
 * (DS:0x2650), 4 = the second (DS:0x2652); the mode word DS:0x255e (1 or 2: the panels along the right side, else
 * below the map) chooses the limits (0x64 / 0xc8 minimum widths, 0x32 / 0x64 minimum heights, 13 * DS:0x267a / 2 + 10).
 * With a previous delta (prev) it first erases the previous bars by calling itself with the difference (when
 * bits is not 1), then computes the change of the limited delta and draws the bars of the new position. */
DECOMP(4, 0x143a, drag_divider_bars, FAR_CDECL)
uint32_t drag_divider_bars(uint16_t hdc, uint16_t bits, uint16_t dx, uint16_t dy, uint16_t prev)
{
  FRAME_ENTER(0x14 + 4);
  uint16_t si = bits, px = dx, py = dy, ax, bx, cx;
  L32(-0x10) = ((uint32_t)dy << 16) | dx;
  if (prev != 0 && si != 1) {
    L16(-4) = (uint16_t)(dx - drd16(prev));
    L16(-2) = (uint16_t)(dy - drd16((uint16_t)(prev + 2)));
    const uint16_t a[5] = {hdc, bits, L16(-4), L16(-2), 0};
    (void)call4(0x143a, a, 5);
  }
  {
    const int16_t v = (int16_t)drd16(0x255e);
    if (!(v == 1 || v == 2)) {
      bx = 0x64;
      cx = 0x32;
      ax = (uint16_t)(drd16(0x2652) - drd16(0x2650) - 8);
      L16(-2) = ax;
      L16(-8) = ax;
      L16(-6) = 1;
      L16(-4) = (uint16_t)((uint16_t)((int16_t)times13() >> 1) + 10);
    } else {
      bx = 0xc8;
      ax = (uint16_t)(drd16(0x264c) - drd16(0x2650) - 8);
      L16(-2) = ax;
      L16(-8) = drd16(0x2652);
      L16(-6) = 0xffff;
      cx = 0x64;
      L16(-4) = cx;
    }
  }
  /* limit the x delta */
  if ((int16_t)(uint16_t)(px + drd16(0x264e)) < 0xc6) px = (uint16_t)(0xc6 - drd16(0x264e));
  if ((int16_t)(uint16_t)(drd16(0x264a) - px - drd16(0x264e)) < (int16_t)bx) px = (uint16_t)(drd16(0x264a) - drd16(0x264e) - bx);
  if ((int16_t)(uint16_t)(px + drd16(0x264e)) > 0x18c) px = (uint16_t)(0x18c - drd16(0x264e));
  if ((si & 2) != 0) {
    if ((int16_t)(uint16_t)(py + drd16(0x2650)) < (int16_t)cx) {
      py = (uint16_t)(cx - drd16(0x2650));
    } else {
      cx = (uint16_t)((uint16_t)((int16_t)times13() >> 1) + 10);
      if (!((int16_t)cx <= (int16_t)(uint16_t)(L16(-2) - py))) py = (uint16_t)(L16(-2) - cx);
    }
  }
  if ((si & 4) != 0) {
    if ((int16_t)(uint16_t)(L16(-8) + py) < (int16_t)L16(-4)) {
      py = (uint16_t)((int32_t)(int16_t)(uint16_t)(L16(-4) - L16(-8)) * (int16_t)L16(-6));
    } else {
      cx = (uint16_t)(times13() - 0x24);
      if ((int16_t)(uint16_t)(drd16(0x264c) - py - drd16(0x2652) - 8) < (int16_t)cx)
        py = (uint16_t)(drd16(0x264c) - cx - drd16(0x2652) - 8);
    }
  }
  if (prev != 0) {
    const uint16_t di = prev;
    L16(-0x14) = px;
    L16(-0x12) = py;
    px = (uint16_t)(L16(-0x10) - drd16(di));
    cx = (uint16_t)(L16(-0xe) - drd16((uint16_t)(di + 2)));
    py = cx;
    if ((int16_t)(uint16_t)(px + drd16(0x264e)) < 0xc6) px = (uint16_t)(0xc6 - drd16(0x264e));
    if ((int16_t)(uint16_t)(drd16(0x264a) - px - drd16(0x264e)) < (int16_t)bx) px = (uint16_t)(drd16(0x264a) - drd16(0x264e) - bx);
    if ((int16_t)(uint16_t)(px + drd16(0x264e)) > 0x18c) px = (uint16_t)(0x18c - drd16(0x264e));
    if ((si & 2) != 0) {
      if ((int16_t)(uint16_t)(cx + drd16(0x2650)) < 0x32) {
        py = (uint16_t)(0x32 - drd16(0x2650));
      } else {
        cx = L16(-2);
        bx = (uint16_t)((uint16_t)((int16_t)times13() >> 1) + 10);
        if (!((int16_t)bx <= (int16_t)(uint16_t)(cx - py))) py = (uint16_t)(cx - bx);
      }
    } else if ((si & 4) != 0) {
      bx = L16(-8);
      if ((int16_t)(uint16_t)(cx + bx) < (int16_t)L16(-4)) {
        py = (uint16_t)((int32_t)(int16_t)(uint16_t)(L16(-4) - bx) * (int16_t)L16(-6));
      } else {
        bx = (uint16_t)(times13() - 0x24);
        if ((int16_t)(uint16_t)(drd16(0x264c) - cx - drd16(0x2652) - 8) < (int16_t)bx)
          py = (uint16_t)(drd16(0x264c) - bx - drd16(0x2652) - 8);
      }
    }
    L16(-0xc) = (uint16_t)(L16(-0x14) - px);
    px = L16(-0x14);
    py = L16(-0x12);
  }
  switch (si) {
    case 2:
      px = 0;
      drag_patblt(hdc, 0, (uint16_t)(py + drd16(0x2650) + 1), (uint16_t)(drd16(0x264e) + 1), 6);
      break;
    case 3:
      drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 1), 0, 6, drd16(0x264c));
      drag_patblt(hdc, 0, (uint16_t)(py + drd16(0x2650) + 1), (uint16_t)(px + drd16(0x264e) + 1), 6);
      break;
    case 4:
      px = 0;
      if (drd16(0x255e) == 0)
        drag_patblt(hdc, 0, (uint16_t)(py + drd16(0x2652) + 1), (uint16_t)(drd16(0x264e) + 1), 6);
      else
        drag_patblt(hdc, (uint16_t)(drd16(0x264e) + 7), (uint16_t)(py + drd16(0x2652) + 1),
                    (uint16_t)(drd16(0x264a) - drd16(0x264e) - 7), 6);
      break;
    case 5:
      drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 1), 0, 6, drd16(0x264c));
      if (drd16(0x255e) == 0)
        drag_patblt(hdc, 0, (uint16_t)(py + drd16(0x2652) + 1), (uint16_t)(px + drd16(0x264e) + 1), 6);
      else
        drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 7), (uint16_t)(py + drd16(0x2652) + 1),
                    (uint16_t)(drd16(0x264a) - px - drd16(0x264e) - 7), 6);
      break;
    case 7:
      drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 1), 0, 6, drd16(0x264c));
      drag_patblt(hdc, 0, (uint16_t)(py + drd16(0x2650) + 1), (uint16_t)(px + drd16(0x264e) + 1), 6);
      drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 7), (uint16_t)(py + drd16(0x2652) + 1),
                  (uint16_t)(drd16(0x264a) - px - drd16(0x264e) - 7), 6);
      break;
    default: {
      py = 0;
      if (prev != 0) {
        const int16_t t = (int16_t)L16(-0xc);
        const int16_t sg = (int16_t)(t >> 15);
        const uint16_t absv = (uint16_t)((uint16_t)(t ^ sg) - (uint16_t)sg);
        if ((int16_t)absv < 6) {
          si = (uint16_t)t;
          if (si == 0) {
            FRAME_LEAVE();
            return ((uint32_t)py << 16) | px;
          }
          uint16_t di;
          if ((int16_t)si >= 0) {
            di = (uint16_t)(px - si + drd16(0x264e) + 1);
          } else {
            di = (uint16_t)(px + drd16(0x264e) + 1);
            si = (uint16_t)(0 - si);
          }
          drag_patblt(hdc, di, 0, si, drd16(0x264c));
          drag_patblt(hdc, (uint16_t)(di + 6), 0, si, drd16(0x264c));
          break;
        }
        drag_patblt(hdc, (uint16_t)(px - L16(-0xc) + drd16(0x264e) + 1), 0, 6, drd16(0x264c));
      }
      drag_patblt(hdc, (uint16_t)(px + drd16(0x264e) + 1), 0, 6, drd16(0x264c));
      break;
    }
  }
  FRAME_LEAVE();
  return ((uint32_t)py << 16) | px;
}

/* DialogBox over a procedure of this module, as 4:0x1f40 does it for most commands: MakeProcInstance(seg:off, DS:0x54f4),
 * DialogBox(DS:0x54f4, template, parent, proc), FreeProcInstance. The result is not used. */
static void cmd_dialog(uint16_t seg, uint16_t off, uint32_t tmpl, uint16_t parent)
{
  uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(seg), off), drd16(0x54f4));
  (void)API_SP(12, DialogBox(drd16(0x54f4), tmpl, parent, pi));
  FreeProcInstance(pi);
}
/* The window of class DS:0x1ea (the "start" window) at the size of the screen, as 4:0x4090 and 4:0x1f40 create it. */
static void cmd_start_window(uint16_t title)
{
  L16(-4) = (uint16_t)GetSystemMetrics(0);
  L16(-2) = (uint16_t)GetSystemMetrics(1);
  uint16_t w = CreateWindow(dsfp(0x01ea), dsfp(title), 0x90000000u, 0, 0, (int16_t)L16(-4), (int16_t)L16(-2), drd16(0x2806), 0,
                            drd16(0x54f4), 0);
  dwr16(0x0312, w);
}
/* (a * b) / c with the original's 32-bit steps: IMUL ECX (the low 32 bits of the product are kept), CDQ, IDIV ECX. */
static int32_t map_scale(int16_t a, int16_t b, int16_t c)
{
  int32_t prod = (int32_t)((uint32_t)(int32_t)a * (uint32_t)(int32_t)b);
  if (c == 0 || (prod == INT32_MIN && c == -1)) {
    fprintf(stderr, "4:1f40: divide error\n");
    abort();
  }
  return prod / (int32_t)c;
}

/* frame_on_command: 4:0x1f40 (Ghidra FUN_1018_1f40, alias fn_04_1f40).
 * 6234 bytes (to 4:0x379a). ENTER 0x17e,0; PUSH DI; PUSH SI. FAR CDECL (hwnd [BP+6], id
 * [BP+8]). The WM_COMMAND handler of the main window: one branch per menu / button command id (a jump table at
 * 4:0x1f80 for ids 0x53 to 0xb3, comparisons for the others). Commands that open a dialog use MakeProcInstance /
 * DialogBox / FreeProcInstance (procedures of segments 3, 6, 8, 11, 27, 30, 32); the list of ids, by the label of the
 * original code (the C label is L<address>):
 *   0x3a98 to 0x3afb  DS:0x4c84 = id - 0x3a98 (a recent-file slot), return
 *   0x53 to 0x55      32:0x67a8 / 0x5ada / 0x5954 (no argument)
 *   0x5f, 0x60 (2168) the dialog 0x66 (32:0x0a66);  0x63 (21a6) About, dialog 0x5a (3:0x0ca0)
 *   0x67, 0x68        10:0x606a(id == 0x67) unless the focus is DS:0x83a (DS:0x4b74 must be 2)
 *   0x69, 0x6c, 0x5208 to 0x520a (220e)  start the game (the turn files; 0x5208 / 0x5209 / 0x520a count down 10 / 100 /
 *                     1000 turns in DS:0x378 with a Shift or Control key test); 0x6a, 0x7da (33df) the host mode
 *   0x6d, 0xed9 (2224) open a game file (4:0x3b66);  0x6e, 0xed8 (2288), 0x9e, 0x9f, 0x71 (22aa) the other file commands
 *   0x7d, 0x89 (22fe), 0x7e, 0x87 (233e), 0x81, 0x9c, 0x9d, 0x82 to 0x84 (the layout, 23e6), 0x88, 0x100 (the report
 *                     window), 0x8a, 0x101 (WinHelp), 0xb3 (the toolbar toggle)
 *   0x6f, 0xeda, 0xedb, 0x428 (2dc3) save;  0x7d9, 0x7de, 0x7db, 0x7dc, 0x10e (2d6c), 0x1068, 0x1069, 0x10cc to 0x10d4 ...
 *   0x8fd to 0x901, 0x8fe (2e52) the report window list;  0x98d, 0x9c1, 0x9c2, 0x9c4, 0x9c5, 0xee2, 0xf3d to 0xf45, 0xfa1
 *   0xd5 (2524)       the print dialog and the map printing loop (PrintDlg answers FALSE in the runtime, so the printing body is
 *                     not reached by the diff tests)
 * All others go to DefWindowProc(hwnd, WM_COMMAND, id, 0). */
DECOMP(4, 0x1f40, frame_on_command, FAR_CDECL)
void frame_on_command(uint16_t hwnd, uint16_t idarg)
{
  FRAME_ENTER(0x17e + 4);
  uint16_t si = idarg, di = 0, ax, cx, bx;
  uint16_t id = idarg; /* [BP+8], which a few commands change */
  if (si >= 0x3a98 && si < 0x3afc) {
    dwr16(0x4c84, (uint16_t)(si - 0x3a98));
    goto L3796;
  }
  if (si == 0xd5) goto L2524;
  if (si > 0xd5) goto L2042;
  ax = (uint16_t)(si - 0x53);
  if (ax > 0x60) goto L2073;
  switch (ax) {
    case 0x00: goto L2150;
    case 0x01: goto L2158;
    case 0x02: goto L2160;
    case 0x0c: case 0x0d: goto L2168;
    case 0x10: goto L21a6;
    case 0x14: case 0x15: goto L21c4;
    case 0x16: goto L21f2;
    case 0x17: goto L33df;
    case 0x19: goto L220e;
    case 0x1a: goto L2224;
    case 0x1b: goto L2288;
    case 0x1c: goto L2dc3;
    case 0x1e: goto L22aa;
    case 0x2a: case 0x36: goto L22fe;
    case 0x2b: case 0x34: goto L233e;
    case 0x2e: goto L23c2;
    case 0x2f: case 0x30: case 0x31: goto L23e6;
    case 0x35: goto L2422;
    case 0x37: goto L247a;
    case 0x49: case 0x4a: goto L2492;
    case 0x4b: case 0x4c: goto L24c6;
    case 0x60: goto L24de;
    default: goto L2073;
  }
L2042:
  if (si > 0x901) goto L20c2;
  if (si >= 0x8ff) goto L2e52;
  if (si == 0x7d9) goto L2dd6;
  if (si > 0x7d9) goto L2096;
  if (si == 0x101) goto L247a;
  if (si > 0x101) {
    if (si == 0x10e) goto L2d6c;
    if (si == 0x428) goto L2dc0;
    goto L2073;
  }
  if (si < 0xfa) goto L2073;
  if ((uint16_t)(si - 0xfa) <= 3) goto L2d4c;
  if (si == 0x100) goto L2422;
  goto L2073;
L2096:
  if (si == 0x7de) goto L2dd6;
  if (si > 0x7de) {
    if (si == 0x8fd) goto L2e52;
    if (si == 0x8fe) goto L2e46;
    goto L2073;
  }
  if (si == 0x7da) goto L33df;
  if (si == 0x7db || si == 0x7dc) goto L2e12;
  goto L2073;
L20c2:
  if (si > 0xedb) goto L2100;
  if (si >= 0xeda) goto L2dc3;
  if (si > 0x9c5) {
    if (si == 0xed8) goto L2288;
    if (si == 0xed9) goto L2224;
    goto L2073;
  }
  if (si >= 0x9c4) goto L2f2c;
  if (si == 0x98d) goto L2ec2;
  {
    const uint16_t t = (uint16_t)(si - 0x98d);
    if (t < 0x34) goto L2073;
    if ((uint16_t)(t - 0x34) <= 1) goto L2f0c;
    goto L2073;
  }
L2100:
  if (si == 0xfa1) goto L3030;
  if (si > 0xfa1) goto L2122;
  if (si == 0xee2) goto L2f40;
  {
    const uint16_t t = (uint16_t)(si - 0xee2);
    if (t < 0x5b) goto L2073;
    if ((uint16_t)(t - 0x5b) <= 8) goto L2f54;
    goto L2073;
  }
L2122:
  if (si < 0x1068) goto L2073;
  if ((uint16_t)(si - 0x1068) <= 1) goto L3066;
  if (si < 0x10cc) goto L2073;
  if (si <= 0x10d4) goto L3090;
  if (si < 0x5208) goto L2073;
  if (si <= 0x520a) goto L220e;
  goto L2073;

L2073: /* the default: DefWindowProc(hwnd, WM_COMMAND, si, 0) */
  (void)DefWindowProc(hwnd, 0x0111, si, 0);
  FRAME_LEAVE();
  return;
L3796:
  FRAME_LEAVE();
  return;

L2150:
  (void)CALL_ORIG(32, 0x67a8, FAR_CDECL, 0);
  goto L3796;
L2158:
  (void)CALL_ORIG(32, 0x5ada, FAR_CDECL, 0);
  goto L3796;
L2160:
  (void)CALL_ORIG(32, 0x5954, FAR_CDECL, 0);
  goto L3796;
L2168:
  if (drd16(0x014c) == 0xffff) goto L3796;
  cmd_dialog(32, 0x0a66, 0x00000066u, hwnd);
  goto L3796;
L21a6:
  cmd_dialog(3, 0x0ca0, 0x0000005au, hwnd);
  goto L3796;
L21c4:
  if (drd16(0x4b74) != 2) goto L2073;
  if (GetFocus() == drd16(0x083a)) goto L2073;
  (void)CALL_ORIG(10, 0x606a, FAR_CDECL, 1, si == 0x67 ? 1 : 0);
  goto L2073;
L21f2:
  if ((drd8(0x0080) & 4) == 0) {
    if ((uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x0062, 0x2024) != 6) goto L3796;
  }
L220e:
  di = drd16(0x014c);
  if (si == 0x5209) {
    dwr16(0x0378, 0x0064);
    goto L320f;
  }
  if (si == 0x520a) {
    dwr16(0x0378, 0x03e8);
    goto L320f;
  }
  dwr16(0x0378, si == 0x5208 ? 0x000a : 0);
L320f:
  if ((drd8(0x0080) & 4) != 0 && drd16(0x0378) != 0) {
    pushw(drd16(0x0378));
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0520);
    (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
    popw(1);
    if (MessageBox(GetFocus(), dsfp(0x5866), dsfp(0x0384), 0x2034) != 6) goto L3796;
  }
  if (drd16(0x014c) == 0xffff) goto L3796;
  if ((drd8(0x0791) & 8) == 0) goto L3398;
  (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  if ((drd8(0x53f0) & 8) == 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x01e6, 0x0010);
    goto L3796;
  }
  (void)CALL_ORIG(31, 0x5cc2, FAR_CDECL, 1, 0);
  if (drd16(0x0082) > 0x23) goto L3398;
  (void)CALL_ORIG(8, 0x8672, FAR_CDECL, 2, 0x02d2, 0x4996);
  (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, di);
  if ((uint16_t)CALL_ORIG(9, 0x7eea, FAR_CDECL, 1, (uint16_t)(drd16(0x0082) + 1)) == 0) goto L32b9;
  L16(-0xa) = di;
  if (drd16(0x0082) == 0x23) {
    if ((uint16_t)CALL_ORIG(9, 0x7eea, FAR_CDECL, 1, (uint16_t)(drd16(0x0082) + 2)) == 0) goto L32b9;
  }
  strcpy_at(MEM_SEL_DGROUP, 0x5866, 0x5764);
  strcat_at(MEM_SEL_DGROUP, 0x5866, 0x038b);
  (void)CALL_ORIG(34, 0x0922, FAR_CDECL, 1, 0x5866);
  (void)CALL_ORIG(9, 0x641c, FAR_CDECL, 1, 0);
  (void)CALL_ORIG(8, 0xb8cc, FAR_CDECL, 0);
  L32(-0x16) = 0x0c;
  (void)TimerCount(bpfp(-0x16));
  L32(-4) = L32(-0x12);
  di = L16(-0xa);
  for (;;) {
    (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, (uint16_t)(2 * (uint16_t)(L16(-8) - L16(-4))));
    (void)TimerCount(bpfp(-0x16));
    L32(-8) = L32(-0x12);
    if (L32(-8) < L32(-4)) break;
    if (!((uint32_t)(L32(-4) + 0x1f4u) > L32(-8))) break;
  }
  si = L16(-2);
  goto L34eb;
L32b9:
  (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, drd16(0x0770), 0x0010);
  goto L3796;

L3398:
  if ((drd8(0x0080) & 4) != 0) {
    dwr16(0x0770, 0xffff);
    if ((uint16_t)CALL_ORIG(14, 0x2f88, FAR_CDECL, 3, 2, 0xffff, 1) == 0) goto L346e;
    if ((uint16_t)CALL_ORIG(14, 0x30d8, FAR_CDECL, 1, drd16(0x0770)) != 0) goto L32b9;
    if ((uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x0062, 0x2024) != 6) goto L3796;
  }
L33df:
  if (drd32(0x00b6) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  if ((drd8(0x0080) & 4) != 0) goto L3796;
  if ((uint16_t)CALL_ORIG(14, 0x2f40, FAR_CDECL, 1, drd16(0x014c)) != 0) goto L356c;
  dwr8(0x0790, (uint8_t)(drd8(0x0790) | 0x10));
  (void)CALL_ORIG(9, 0x7d1e, FAR_CDECL, 2, 0x5764, drd16(0x014c));
  (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, drd16(0x014c));
  SetWindowText(drd16(0x2806), dsfp(0x038f));
  (void)ShowWindow(drd16(0x2806), 2);
  ax = SetTimer(0, 0x000e, 0x2710, drd32(0x53e2));
  dwr16(0x0162, ax);
  dwr16(0x0164, 0x000e);
  {
    const uint16_t a[5] = {0, 0, ax, 0, 0};
    (void)far_call(4, 0x4dba, a, 5, FAR_PASCAL);
  }
  FRAME_LEAVE();
  return;
L346e:
  {
    uint16_t cur = LoadCursor(0, 0x00007f02u);
    si = SetCursor(cur);
  }
  (void)CALL_ORIG(9, 0x7d1e, FAR_CDECL, 2, 0x5764, di);
  (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, di);
  L16(-2) = si;
  L16(-0xa) = di;
  for (;;) {
    (void)CALL_ORIG(8, 0xb8cc, FAR_CDECL, 0);
    (void)call4(0x39f8, NULL, 0);
    (void)CALL_ORIG(22, 0x0000, FAR_CDECL, 0);
    if (idarg != 0x5208 && idarg != 0x5209 && idarg != 0x520a) break;
    dwr16(0x0378, (uint16_t)(drd16(0x0378) - 1));
    if ((int16_t)drd16(0x0378) <= 0) break;
    (void)CALL_ORIG(8, 0xb8f6, FAR_CDECL, 0);
    if ((int16_t)GetAsyncKeyState(0x10) >= 0) continue;
    if ((int16_t)GetAsyncKeyState(0x11) >= 0) continue;
    break;
  }
  dwr16(0x0378, 0);
L34eb:
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  {
    pushw((uint16_t)(di + 1));
    (void)_wsprintf(bpfp(-0x38), dsfp(drd16(0x0190)), MK_FP(rt_ss, rt_sp));
    popw(1);
  }
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, bpoff(-0x38)) == 0) {
    (void)SetCursor(si);
    (void)CALL_ORIG(8, 0xb8f6, FAR_CDECL, 0);
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x001b, 0x0010);
    goto L3796;
  }
  (void)CALL_ORIG(8, 0xb8f6, FAR_CDECL, 0);
  dwr16(0x014c, di);
  (void)call4(0x02ca, NULL, 0);
  (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
  (void)SetCursor(si);
  if ((drd8(0x0791) & 8) == 0) goto L3796;
  dwr16(0x53f0, (uint16_t)(drd16(0x53f0) & 0xfdf7));
  (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  goto L3796;

L2224:
  if ((drd8(0x0791) & 8) != 0) {
    if ((uint16_t)CALL_ORIG(31, 0x64f8, FAR_CDECL, 0) == 0) goto L3796;
  }
  {
    const uint16_t a[2] = {hwnd, 0};
    ax = (uint16_t)call4(0x3b66, a, 2);
  }
  if ((int16_t)ax <= 0) goto L3796;
  {
    const uint16_t a[1] = {0};
    (void)call4(0x379a, a, 1);
  }
  if (drd16(0x0162) == 0) (void)PostMessage(hwnd, 0x0111, 0x0fa1, 0);
  if ((drd8(0x0080) & 8) == 0) goto L3796;
  if (drd16(0x014c) != 0) goto L3796;
L227b:
  (void)CALL_ORIG(31, 0x5f14, FAR_CDECL, 1, 0);
  goto L3796;
L2288:
  if ((drd8(0x0791) & 8) != 0) {
    if ((uint16_t)CALL_ORIG(31, 0x64f8, FAR_CDECL, 0) == 0) goto L3796;
  }
  (void)CALL_ORIG(15, 0x6ab2, FAR_CDECL, 2, hwnd, 0);
  goto L3796;
L22aa:
  if ((drd8(0x0791) & 8) != 0) {
    if ((uint16_t)CALL_ORIG(31, 0x64f8, FAR_CDECL, 0) == 0) goto L3796;
  }
  (void)call4(0x506c, NULL, 0);
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  strcpy_at(MEM_SEL_DGROUP, 0x5764, 0x5866);
  dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
  dwr16(0x251e, 0);
  dwr16(0x2520, 0xffff);
  {
    const uint16_t a[1] = {0};
    (void)call4(0x379a, a, 1);
  }
L3123:
  cmd_start_window(0x03a6);
  dwr16(0x0314, 0);
  (void)ShowWindow(drd16(0x2806), 0);
  goto L3796;
L22fe:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  L32(-4) = 0x01c20262u;
  if (drd16(0x0bbe) != 0) (void)API_SP(10, SendMessage(drd16(0x0bbe), 0x0205, 0, 0));
  (void)CALL_ORIG(25, 0x0000, FAR_CDECL, 2, L16(-4), L16(-2));
  goto L3796;
L233e:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  {
    uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(27), 0x0000), drd16(0x54f4));
    L16(-0x154) = (uint16_t)pi;
    L16(-0x152) = (uint16_t)(pi >> 16);
    di = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x0000007fu, drd16(0x2806), pi));
    FreeProcInstance(pi);
  }
  if (di == 0) goto L3796;
  if (drd16(0x4b74) != 1) goto L3796;
  if (drd32(0x4c3c) != 0)
    (void)CALL_ORIG(9, 0x431c, FAR_CDECL, 5, drd16(0x5440), drd16(0x4c3c), drd16(0x4c3e), 0, 0);
  (void)CALL_ORIG(9, 0x07e0, FAR_CDECL, 2, 0, 0x0048);
  goto L3796;
L23c2:
  for (uint16_t k = 0; k < 0xc0; k++) dwr8((uint16_t)(0x51b4 + k), drd8((uint16_t)(0x0de2 + k)));
  (void)CALL_ORIG(28, 0x0000, FAR_CDECL, 3, hwnd, 0, 0);
  goto L3796;
L23e6:
  if (drd16(0x014c) == 0xffff) goto L3796;
  dwr16(0x255e, (uint16_t)(si - 0x82));
  InvalidateRect(drd16(0x2806), 0, 1);
  (void)CALL_ORIG(9, 0x3aa8, FAR_CDECL, 1, drd16(0x255e) == 2 ? 1 : 0);
  (void)call4(0x5d98, NULL, 0);
  goto L3796;
L2422:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  if (drd16(0x014e) == 0) {
    si = 8;
    (void)CreateDialog(drd16(0x54f4), 0x00000080u, drd16(0x2806), drd32(0x53b4));
  } else {
    si = 0;
    (void)DestroyWindow(drd16(0x014e));
  }
  {
    const uint16_t a[2] = {hwnd, 5};
    ax = (uint16_t)call4(0x3b30, a, 2);
  }
  (void)CheckMenuItem(ax, 0x0100, si);
  goto L3796;
L247a:
  (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 3, 0);
  goto L3796;
L2492:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  for (uint16_t k = 0; k < 0xc0; k++) dwr8((uint16_t)(0x51b4 + k), drd8((uint16_t)(0x5a64 + drd16(0x014c) * 0xc0 + k)));
  (void)CALL_ORIG(28, 0x0000, FAR_CDECL, 3, hwnd, 1, 0);
  goto L3796;
L24c6:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  (void)CALL_ORIG(15, 0x6ab2, FAR_CDECL, 2, hwnd, 1);
  goto L3796;
L24de:
  {
    const uint16_t a[2] = {hwnd, 1};
    ax = (uint16_t)call4(0x3b30, a, 2);
  }
  {
    uint16_t w = drd16(0x0792);
    uint16_t f = (uint16_t)(((w & 0x8000) == 0 ? 1u : 0u) << 15);
    uint16_t t = (uint16_t)((uint16_t)(f ^ w) & 0x7fffu);
    uint16_t nw = (uint16_t)(f ^ t);
    dwr16(0x0792, nw);
    (void)CheckMenuItem(ax, 0x00b3, (nw & 0x8000) != 0 ? 8 : 0);
  }
  (void)call4(0x5d98, NULL, 0);
  goto L3796;
L2d4c:
  if (drd16(0x0312) == 0) goto L3796;
  (void)PostMessage(drd16(0x0312), 0x0111, (uint16_t)(si - 0xfa), 0);
  goto L3796;
L2d6c:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  if ((drd8(0x0080) & 4) != 0) {
    if ((int32_t)drd32(0x031c) <= 0) goto L3796;
  }
  if ((uint16_t)CALL_ORIG(8, 0xb1ec, FAR_CDECL, 0) == 0) goto L3796;
  cmd_dialog(8, 0xb49e, 0x0000008du, drd16(0x2806));
  goto L3796;
L2dc0:
  si = 0x0edb;
L2dc3:
  if (drd16(0x0150) == 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x000e, 0x0010);
    goto L3796;
  }
  if (drd16(0x014c) == 0xffff) {
    (void)CALL_ORIG(14, 0x35b0, FAR_CDECL, 3, 0x5764, drd16(0x014c), 0);
    goto L3796;
  }
  if ((uint16_t)CALL_ORIG(14, 0x2f40, FAR_CDECL, 1, drd16(0x014c)) != 0) goto L356c;
  dwr8(0x0790, (uint8_t)((drd8(0x0790) & 0xef) | (si == 0x0edb ? 0x10 : 0)));
  (void)CALL_ORIG(9, 0x7d1e, FAR_CDECL, 2, 0x5764, drd16(0x014c));
  (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, drd16(0x014c));
  goto L3796;
L2dd6:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  if ((drd8(0x0080) & 4) != 0) goto L3796;
  cmd_dialog(30, 0x0000, 0x000007d8u, drd16(0x2806));
  goto L3796;
L2e12:
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  cmd_dialog(30, 0x049a, 0x000007ddu, drd16(0x2806));
  goto L3796;
L2e46:
  if (drd16(0x177e) != 0) goto L2e9c;
  id = 0x08fd;
  goto L2e52;
L2e9c:
  if (drd16(0x1766) == 0x164e) {
    id = 0x08ff;
    goto L2e52;
  }
  id = drd16(0x1766) == 0x1684 ? 0x0900 : 0x0901;
L2e52:
  L16(-0xc) = 0;
  if (drd32(0x0070) == 0) goto L3796;
  if (drd16(0x014c) == 0xffff) goto L3796;
  {
    const uint16_t a[2] = {hwnd, 4};
    L16(-0xe) = (uint16_t)call4(0x3b30, a, 2);
  }
  si = id;
L2e7c:
  if (drd16(0x177e) == 0) goto L360c;
  if (si == 0x901 && drd16(0x1766) == 0x16f0)
    di = 0;
  else
    di = 1;
  (void)DestroyWindow(drd16(0x177e));
  (void)CheckMenuItem(L16(-0xe), si == 0x08ff ? si : 0x08fd, 0);
  di = (uint16_t)(di - 1);
  if (di == 0) goto L2e7c;
  FRAME_LEAVE();
  return;
L360c:
  L16(-2) = 8;
  if (id == 0x08ff) {
    L16(-0xa) = 0x0489;
    dwr16(0x1766, 0x1684);
    cx = 0;
    if ((int16_t)drd16(0x553a) > (int16_t)cx) {
      bx = drd16(0x00ba);
      L16(-6) = drd16(0x00bc);
      do {
        ax = rd16(L16(-6), bx);
        L16(-2) = rd16(L16(-6), (uint16_t)(bx + 2));
        si = ax;
        if ((L16(-2) | ax) == 0) break;
        if (rd16(L16(-2), (uint16_t)(si + 2)) == drd16(0x014c)) L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
        bx = (uint16_t)(bx + 4);
        cx = (uint16_t)(cx + 1);
      } while ((int16_t)cx < (int16_t)drd16(0x553a));
    }
  } else if (id == 0x0900) {
    L16(-0xa) = 0x048a;
    dwr16(0x1766, 0x16ba);
    cx = 0;
    if ((int16_t)drd16(0x553a) > (int16_t)cx) {
      bx = drd16(0x00ba);
      L16(-6) = drd16(0x00bc);
      do {
        ax = rd16(L16(-6), bx);
        L16(-2) = rd16(L16(-6), (uint16_t)(bx + 2));
        si = ax;
        if ((L16(-2) | ax) == 0) break;
        if (rd16(L16(-2), (uint16_t)(si + 2)) != drd16(0x014c)) L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
        bx = (uint16_t)(bx + 4);
        cx = (uint16_t)(cx + 1);
      } while ((int16_t)cx < (int16_t)drd16(0x553a));
    }
  } else if (id == 0x0901) {
    dwr16(0x1766, 0x16f0);
    L16(-0xa) = 0x048b;
    L16(-0xc) = (uint16_t)CALL_ORIG(29, 0x01c2, FAR_CDECL, 0);
  } else {
    dwr16(0x1766, 0x164e);
    L16(-0xa) = 0x0488;
    bx = drd16(0x00b6);
    L16(-6) = drd16(0x00b8);
    cx = (uint16_t)(drd16(0x4d96) * 0x38);
    ax = (uint16_t)(drd16(0x00b6) + cx);
    L16(-2) = drd16(0x00b8);
    if (ax > bx) {
      L16(-4) = ax;
      si = L16(-0xc);
      cx = ax;
      do {
        if (rd16(L16(-6), (uint16_t)(bx + 2)) == drd16(0x014c)) si = (uint16_t)(si + 1);
        bx = (uint16_t)(bx + 0x38);
      } while (bx < cx);
      L16(-0xc) = si;
    }
  }
  {
    const uint16_t ch = L16(-0xc) == 1 ? 0x20 : 0x73;
    pushw(ch);
    pushw(L16(-0xc));
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, L16(-0xa));
    (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
    popw(2);
  }
  ax = CreateWindow(dsfp(0x01f6), dsfp(0x5866), 0x80cf0000u, 0, 0, 0x64, 0x64, drd16(0x2806), 0, drd16(0x54f4), 0);
  dwr16(0x177e, ax);
  (void)SetWindowPos(ax, 0, 0, 0, 0, 0, 0x47);
  (void)CheckMenuItem(L16(-0xe), id, 8);
  goto L3796;
L2ec2:
  {
    const uint16_t a[2] = {hwnd, 1};
    ax = (uint16_t)call4(0x3b30, a, 2);
  }
  dwr8(0x055f, (uint8_t)(drd8(0x055f) ^ 0x20));
  (void)CheckMenuItem(ax, 0x098d, (drd8(0x055f) & 0x20) != 0 ? 8 : 0);
  dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x40));
  if ((drd8(0x055f) & 0x14) != 0) InvalidateRect(drd16(0x0150), 0, 0);
  goto L3796;
L2f0c:
  (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, si == 0x09c2 ? 0x1195u : 0x32cau);
  goto L3796;
L2f2c:
  if ((drd8(0x0791) & 8) == 0) goto L227b;
  (void)CALL_ORIG(31, 0x5cc2, FAR_CDECL, 1, 1);
  goto L3796;
L2f40:
  (void)API_SP(10, SendMessage(hwnd, 0x0010, 0, 0));
  goto L3796;
L2f54:
  if (drd16(0x0150) == 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x000f, 0x0010);
    goto L3796;
  }
  {
    const uint16_t a[2] = {hwnd, 1};
    ax = (uint16_t)call4(0x3b30, a, 2);
  }
  di = GetSubMenu(ax, 3);
  (void)CheckMenuItem(di, (uint16_t)(drd16(0x0576) + 4), 0x0400);
  GetClientRect(drd16(0x0150), bpfp(-0x50));
  L16(-0x4c) = (uint16_t)((int16_t)(uint16_t)CALL_ORIG(11, 0x0ab0, FAR_CDECL, 1, L16(-0x4c)) >> 1);
  L16(-0x4a) = (uint16_t)((int16_t)(uint16_t)CALL_ORIG(11, 0x0ab0, FAR_CDECL, 1, L16(-0x4a)) >> 1);
  L16(-4) = drd16(0x27ec);
  L16(-2) = (uint16_t)(drd16(0x00b2) - drd16(0x27ea));
  dwr16(0x0576, (uint16_t)(si - 0x0f41));
  (void)CheckMenuItem(di, (uint16_t)(drd16(0x0576) + 4), 0x0408);
  DrawMenuBar(hwnd);
  (void)CALL_ORIG(11, 0x4c62, FAR_CDECL, 1, drd16(0x0150));
  InvalidateRect(drd16(0x0150), 0, 1);
  if (drd16(0x4b80) != 0) {
    L32(-0x34) = drd32(0x4b7c);
  } else {
    L16(-0x34) = (uint16_t)(L16(-4) + L16(-0x4c));
    L16(-0x32) = (uint16_t)(L16(-2) - L16(-0x4a));
  }
  (void)CALL_ORIG(11, 0x5142, FAR_CDECL, 3, L16(-0x34), L16(-0x32), 0);
  goto L3796;
L3030:
  if (drd16(0x0150) == 0) goto L3796;
  dwr8(0x0794, (uint8_t)(drd8(0x0794) | 1));
  (void)call4(0x197a, NULL, 0);
  (void)call4(0x5d98, NULL, 0);
  dwr8(0x0794, (uint8_t)(drd8(0x0794) & 0xfe));
  InvalidateRect(drd16(0x0150), 0, 1);
  UpdateWindow(drd16(0x0150));
  goto L3796;
L3066:
  if (drd16(0x014c) == 0xffff) goto L3796;
  cmd_dialog(11, 0x66d6, 0x0000106au, hwnd);
  goto L3796;
L3090:
  if (drd32(0x05f6) == 0) goto L3796;
  {
    uint16_t bxx = (uint16_t)((idarg & 0xffu) << 8);
    bxx = (uint16_t)(bxx + drd16(0x05f6));
    bxx = (uint16_t)((bxx & 0x00ffu) | (uint16_t)(((bxx >> 8) + 0x34) & 0xffu) << 8);
    const uint16_t seg = drd16(0x05f8);
    L16(-0x17e) = bxx;
    L16(-0x17c) = seg;
    if (rd8(seg, bxx) == 0) goto L3796;
    for (uint16_t k = 0;; k++) {
      uint8_t c = rd8(seg, (uint16_t)(bxx + k));
      wr8(rt_ss, bpoff((int16_t)(-0x150 + k)), c);
      if (c == 0) break;
    }
  }
  L16(-2) = (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, bpoff(-0x150), 0x2e);
  if (L16(-2) == 0) goto L3796;
  dwr8(L16(-2), 0);
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, bpoff(-0x150), (uint16_t)(L16(-2) + 1)) == 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x0004, 0x0010);
    goto L3123;
  }
  if (drd16(0x014c) != 0xffff) {
    (void)call4(0x02ca, NULL, 0);
    (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
    goto L3796;
  }
  (void)call4(0x4090, NULL, 0);
  goto L3796;
L356c:
  (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, drd16(0x014c));
  if (drd16(0x007e) != 0) {
    if ((uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b7, 0x0031) == 2) goto L3796;
  } else {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b6, 0x0040);
  }
  {
    pushw((uint16_t)(drd16(0x014c) + 1));
    (void)_wsprintf(bpfp(-0x38), dsfp(drd16(0x0190)), MK_FP(rt_ss, rt_sp));
    popw(1);
  }
  dwr16(0x007e, 0);
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, bpoff(-0x38)) != 0) {
    (void)call4(0x02ca, NULL, 0);
    (void)API_SP(10, SendMessage(drd16(0x2806), 0x0111, 0x0fa1, 0));
    goto L3796;
  }
  (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x001b, 0x0010);
  goto L3796;

L2524: /* the print dialog and the map printing loop */
  if (drd16(0x014c) == 0xffff) goto L3796;
  {
    uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(32), 0x7382), drd16(0x54f4));
    L16(-0x158) = (uint16_t)pi;
    L16(-0x156) = (uint16_t)(pi >> 16);
    di = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x000000d6u, drd16(0x2806), pi));
    FreeProcInstance(pi);
  }
  if (di == 0) goto L3796;
  si = drd16(0x188a);
  L16(-0x3c) = drd16(0x188c);
  for (uint16_t k = 0; k < 0x34; k++) wr8(rt_ss, bpoff((int16_t)(-0x88 + k)), 0);
  L16(-0x88) = 0x34;
  L16(-0x86) = 0;
  L16(-0x7c) = 0x500;
  L16(-0x7a) = 0;
  if (PrintDlg(bpfp(-0x88)) == 0) {
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x0519, 0x0010);
    goto L3796;
  }
  {
    const uint16_t hdc = L16(-0x7e);
    L16(-0x40) = (uint16_t)GetDeviceCaps(hdc, 8);
    L16(-0x3a) = (uint16_t)GetDeviceCaps(hdc, 0x0a);
    L16(-0x0a) = (uint16_t)GetDeviceCaps(hdc, 0x58);
    di = (uint16_t)GetDeviceCaps(hdc, 0x5a);
    L16(-2) = 0x20;
    L16(-0x16) = (uint16_t)CALL_ORIG(8, 0xbdc8, FAR_CDECL, 3, hdc, 8, bpoff(-0x18));
    L16(-0x24) = (uint16_t)CALL_ORIG(8, 0xbdc8, FAR_CDECL, 3, hdc, 5, bpoff(-0x1a));
    (void)SetBkMode(hdc, 1);
    L32(-0x48) = 0;
    L16(-0x15a) = (int16_t)L16(-0x3a) < (int16_t)L16(-0x40) ? 1 : 0;
    ax = (int16_t)L16(-0x3c) < (int16_t)si ? 1 : 0;
    if (L16(-0x15a) == ax) {
      const uint16_t t = si;
      si = L16(-0x3c);
      L16(-0x3c) = t;
    }
    L32(-4) = (uint32_t)((int32_t)(int16_t)L16(-0x40) * (int32_t)(int16_t)si);
    {
      const int32_t p2 = (int32_t)(int16_t)L16(-0x3a) * (int32_t)(int16_t)L16(-0x3c);
      L32(-8) = (uint32_t)p2;
      if ((int32_t)L32(-4) > 0x7d00) L32(-4) = 0x7d00;
      if (p2 > 0x7d00) L32(-8) = 0x7d00;
    }
    if ((int32_t)L32(-8) > (int32_t)L32(-4)) {
      const int32_t diff = (int32_t)L32(-8) - (int32_t)(int16_t)di;
      L32(-0x15e) = (uint32_t)diff;
      if (diff < (int32_t)L32(-4)) L32(-4) = (uint32_t)diff;
      bx = L16(-4);
      L16(-0x30) = 0x20;
      L16(-0x2e) = (uint16_t)(bx + 0x20);
      L16(-0x2c) = (uint16_t)(((int16_t)bx - (int16_t)((int16_t)bx >> 15)) >> 1);
      L16(-0x2a) = L16(-0x2e);
    } else {
      bx = L16(-0x0a);
      const int16_t a3 = (int16_t)(uint16_t)(bx + bx + bx);
      const int16_t half = (int16_t)(((int16_t)a3 - (int16_t)(a3 >> 15)) >> 1);
      const int32_t diff = (int32_t)L32(-4) - (int32_t)half;
      L32(-0x162) = (uint32_t)diff;
      if (diff < (int32_t)L32(-8)) L32(-8) = (uint32_t)diff;
      bx = L16(-8);
      L16(-0x30) = (uint16_t)(bx + 0x20);
      L16(-0x2e) = 0x20;
      L16(-0x2c) = (uint16_t)(bx + 0x20);
      L16(-0x2a) = (uint16_t)(((int16_t)bx - (int16_t)((int16_t)bx >> 15)) >> 1);
    }
    L16(-0x42) = bx;
    L16(-0x44) = bx;
    L16(-0x22) = (uint16_t)(bx - 0x40);
    L16(-0x54) = 0;
    cx = 0;
    if ((int16_t)si > 0) {
      L16(-0x3e) = si;
      L16(-0x52) = si;
      do {
        L16(-0x28) = 0;
        if ((int16_t)L16(-0x3c) > 0) {
          L16(-0x20) = 0;
          L16(-0x14) = (uint16_t)(0 - cx);
          L16(-0x26) = L16(-0x3c);
          L16(-0x1c) = cx;
          do {
            L16(-0x12) = (uint16_t)(0 - L16(-0x20));
            ax = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x0511, 0x5866);
            (void)Escape(hdc, 0x0a, (int16_t)ax, dsfp(0x5866), 0);
            ax = (uint16_t)(0 - L16(-0x1c));
            (void)Rectangle(hdc, (int16_t)ax, (int16_t)L16(-0x12), (int16_t)(uint16_t)(ax + L16(-0x44)),
                            (int16_t)(uint16_t)(L16(-0x12) + L16(-0x42)));
            if (L16(-0x16) != 0)
              L16(-0x1e) = SelectObject(hdc, L16(-0x16));
            else
              L16(-0x1e) = 0;
            L16(-2) = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x0511, 0x5866);
            {
              const uint16_t x0 = (uint16_t)(L16(-0x14) + L16(-0x30));
              const uint16_t y0 = (uint16_t)(L16(-0x12) + L16(-0x2e));
              (void)TextOut(hdc, (int16_t)x0, (int16_t)y0, dsfp(0x5866), (int16_t)L16(-2));
              si = (uint16_t)(y0 + L16(-0x18));
              di = x0;
              (void)TextOut(hdc, (int16_t)di, (int16_t)si, dsfp(0x0090), (int16_t)strlen_at(MEM_SEL_DGROUP, 0x0090));
              si = (uint16_t)(si + L16(-0x18));
            }
            {
              const uint16_t args[6] = {drd16(0x014c), 1, 1, 1, 0, 0};
              ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, args[0], args[1], args[2], args[3], args[4], args[5]);
            }
            L16(-4) = ax;
            (void)TextOut(hdc, (int16_t)di, (int16_t)si, dsfp(ax), (int16_t)strlen_at(MEM_SEL_DGROUP, ax));
            si = (uint16_t)(si + L16(-0x18));
            {
              pushw((uint16_t)(drd16(0x0082) + 0x0960));
              ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0512);
              L16(-2) = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
              popw(1);
            }
            (void)TextOut(hdc, (int16_t)di, (int16_t)si, dsfp(0x5866), (int16_t)L16(-2));
            if ((drd8(0x055e) & 0x0f) != 5) {
              si = (uint16_t)(L16(-0x12) + L16(-0x2a));
              di = 0;
              do {
                L16(-2) = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(di + 0x0513), 0x5866);
                (void)TextOut(hdc, (int16_t)(uint16_t)(L16(-0x14) + L16(-0x2c)), (int16_t)si, dsfp(0x5866), (int16_t)L16(-2));
                si = (uint16_t)(si + L16(-0x18));
                di = (uint16_t)(di + 1);
              } while ((int16_t)di < 5);
              si = (uint16_t)(L16(-0x12) + L16(-0x2a));
              if (L16(-0x24) != 0) (void)SelectObject(hdc, L16(-0x24));
              di = L16(-0x14);
              {
                const int16_t h1a = (int16_t)(((int16_t)L16(-0x1a) - (int16_t)((int16_t)L16(-0x1a) >> 15)) >> 1);
                int16_t t;
                uint16_t y;
                t = (int16_t)(uint16_t)(L16(-0x18) + L16(-0x18) + L16(-0x18));
                y = (uint16_t)((int16_t)((t - (t >> 15)) >> 1) - h1a + (int16_t)si);
                L16(-0x164) = (uint16_t)(L16(-0x2c) + di);
                (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-0x164), y, 0x037a, 1);
                t = (int16_t)(uint16_t)(L16(-0x18) * 5);
                y = (uint16_t)((int16_t)((t - (t >> 15)) >> 1) - h1a + (int16_t)si);
                (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-0x164), y, 0x037c, 1);
                t = (int16_t)(uint16_t)(L16(-0x18) * 9);
                y = (uint16_t)((int16_t)((t - (t >> 15)) >> 1) - L16(-0x1a) + si + 8);
                (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-0x164), y, 0x037e, 1);
              }
              if (L16(-0x16) != 0) (void)SelectObject(hdc, L16(-0x16));
              {
                int16_t t = (int16_t)L16(-0x18);
                L16(-0x166) = (uint16_t)(L16(-0x2c) + di);
                (void)CALL_ORIG(7, 0x4eca, FAR_CDECL, 4, hdc, L16(-0x166),
                                (uint16_t)(((t - (t >> 15)) >> 1) + (int16_t)si - 4), 1);
                t = (int16_t)(uint16_t)(L16(-0x18) * 7);
                (void)CALL_ORIG(7, 0x4eca, FAR_CDECL, 4, hdc, L16(-0x166), (uint16_t)(((t - (t >> 15)) >> 1) + (int16_t)si - 4), 0);
                t = (int16_t)(uint16_t)(L16(-0x18) * 9);
                (void)CALL_ORIG(7, 0x4eca, FAR_CDECL, 4, hdc, L16(-0x166), (uint16_t)(((t - (t >> 15)) >> 1) + (int16_t)si + 8), 0);
              }
            }
            if ((drd8(0x055e) & 0x0f) != 5) {
              if (L16(-0x24) != 0) (void)SelectObject(hdc, L16(-0x24));
              si = drd16(0x00b6);
              L16(-0xe) = drd16(0x00b8);
              L16(-0xc) = (uint16_t)(drd16(0x00b6) + drd16(0x4d96) * 0x38);
              L16(-0xa) = drd16(0x00b8);
              if (L16(-0xc) > si) {
                do {
                  const uint16_t es = L16(-0xe);
                  di = (uint16_t)(rd16(es, si) << 2);
                  {
                    const int32_t q = map_scale((int16_t)(uint16_t)(drd16((uint16_t)(di + 0x31ba)) - 0x3e8), (int16_t)L16(-0x22),
                                                (int16_t)drd16(0x00b0));
                    const uint32_t x32 = (uint32_t)q + (uint32_t)(int32_t)(int16_t)L16(-0x14) + 0x20u;
                    L32(-4) = x32;
                    cx = (uint16_t)x32;
                  }
                  {
                    const int32_t q = map_scale((int16_t)(uint16_t)(drd16(0x00b2) - drd16((uint16_t)(di + 0x31bc)) - 0x3e8),
                                                (int16_t)L16(-0x22), (int16_t)drd16(0x00b0));
                    const uint32_t y32 = (uint32_t)q + (uint32_t)(int32_t)(int16_t)L16(-0x12) + 0x20u;
                    L32(-8) = y32;
                    cx = (uint16_t)y32;
                  }
                  if (rd16(es, (uint16_t)(si + 2)) == drd16(0x014c)) {
                    if ((rd8(es, (uint16_t)(si + 5)) & 2) != 0) {
                      uint16_t a0 = (uint16_t)(rd8(es, (uint16_t)(si + 0x2c)) & 0x0f);
                      /* LES BX,[owner * 4 + 0x10c]: the owner's ship design array */
                      const uint32_t dsg = drd32((uint16_t)(rd16(es, (uint16_t)(si + 2)) * 4 + 0x010c));
                      const uint16_t dseg = (uint16_t)(dsg >> 16), doff = (uint16_t)dsg;
                      const uint16_t icon = rd16(dseg, (uint16_t)(doff + (uint16_t)(a0 * 0x8d))) == 0x20 ? 0x0380 : 0x0382;
                      (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-4), (uint16_t)(cx - L16(-0x1a) + 4), icon, 1);
                    }
                    (void)CALL_ORIG(7, 0x4eca, FAR_CDECL, 4, hdc, L16(-4), L16(-8), 1);
                  } else if (rd16(es, (uint16_t)(si + 2)) != 0xffff) {
                    pushw((uint16_t)(rd16(es, (uint16_t)(si + 2)) + 1));
                    ax = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(drd16(0x016a)), MK_FP(rt_ss, rt_sp));
                    popw(1);
                    (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-4), (uint16_t)(L16(-8) - L16(-0x1a)), 0x5866, ax);
                  }
                  si = (uint16_t)(si + 0x38);
                } while (si < L16(-0xc));
              }
            }
            if (L16(-0x16) != 0) (void)SelectObject(hdc, L16(-0x16));
            di = 0;
            if ((int16_t)drd16(0x007a) > (int16_t)di) {
              L16(-2) = 0x31ba;
              si = 0x31ba;
              do {
                {
                  const int32_t q = map_scale((int16_t)(uint16_t)(drd16(si) - 0x3e8), (int16_t)L16(-0x22), (int16_t)drd16(0x00b0));
                  L32(-6) = (uint32_t)q + (uint32_t)(int32_t)(int16_t)L16(-0x14) + 0x20u;
                }
                {
                  const int32_t q = map_scale((int16_t)(uint16_t)(drd16(0x00b2) - drd16((uint16_t)(si + 2)) - 0x3e8),
                                              (int16_t)L16(-0x22), (int16_t)drd16(0x00b0));
                  L32(-0xa) = (uint32_t)q + (uint32_t)(int32_t)(int16_t)L16(-0x12) + 0x20u;
                }
                (void)CALL_ORIG(7, 0x4eca, FAR_CDECL, 4, hdc, L16(-6), L16(-0xa), 0);
                if ((drd8(0x055f) & 4) != 0) {
                  pushw(0); /* PUSH 0 stays on the stack while 7:0x1cce runs; it is the last argument of 8:0x9072 */
                  ax = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, di);
                  popw(1);
                  (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, L16(-6), (uint16_t)(L16(-0xa) + 0xe), ax, 0);
                }
                si = (uint16_t)(si + 4);
                di = (uint16_t)(di + 1);
              } while ((int16_t)di < (int16_t)drd16(0x007a));
            }
            if (L16(-0x1e) != 0) (void)SelectObject(hdc, L16(-0x1e));
            (void)Escape(hdc, 1, 0, 0, 0);
            L16(-0x20) = (uint16_t)(L16(-0x20) + L16(-0x3a));
            L16(-0x26) = (uint16_t)(L16(-0x26) - 1);
          } while (L16(-0x26) != 0);
          cx = L16(-0x1c);
        }
        cx = (uint16_t)(cx + L16(-0x40));
        L16(-0x3e) = (uint16_t)(L16(-0x3e) - 1);
      } while (L16(-0x3e) != 0);
    }
    (void)Escape(hdc, 0x0b, 0, 0, 0);
    if (L16(-0x16) != 0) (void)DeleteObject(L16(-0x16));
    if (L16(-0x24) != 0) (void)DeleteObject(L16(-0x24));
    (void)DeleteDC(hdc);
    if (L16(-0x82) != 0) (void)GlobalFree(L16(-0x82));
    if (L16(-0x80) != 0) (void)GlobalFree(L16(-0x80));
  }
  goto L3796;
}

/* update_main_menu: 4:0x379a (Ghidra FUN_1018_379a, alias fn_04_379a).
 * 605 bytes. ENTER 0xa,0; PUSH DI; PUSH SI. FAR CDECL (menu [BP+6], 0 = the menu
 * bar of the main window DS:0x2806). Returns nothing (AX is left over; no caller uses it). Rebuilds the "recent games"
 * part of the first pop-up menu (4:0x3b30(main window, 0)): deletes the items 0x10cc to 0x10d4 and inserts one item
 * "&n name" per non-empty string of the table DS:0x5f6 (strings 0x100 bytes apart, at most 9, at positions 9 + n,
 * ids 0x10cc + n), then enables or greys the items 0x6a, 0x69, 0x10e, 0x7de, 0xedb and 0x992 from the state
 * (DS:0x5764 = the current game file name, DS:0x80 bit 2, DS:0x31c, DS:0x793), checks the items 0xb3, 0x98d and the layout
 * item 0x408 / DS:0x255e, greys or enables the pop-up items of the report menu when the map window DS:0x150 exists, and
 * redraws the menu bar. */
DECOMP(4, 0x379a, update_main_menu, FAR_CDECL)
void update_main_menu(uint16_t menu)
{
  FRAME_ENTER(0xa + 4);
  uint16_t si = menu, di, cx;
  if (si == 0) si = GetMenu(drd16(0x2806));
  {
    const uint16_t a[2] = {drd16(0x2806), 0};
    di = (uint16_t)call4(0x3b30, a, 2);
  }
  L16(-2) = 0x10cc;
  L16(6) = si; /* [BP+6] = si (the argument slot is reused later) */
  si = L16(-2);
  L16(-6) = di;
  do {
    (void)DeleteMenu(di, si, 0);
    si = (uint16_t)(si + 1);
  } while ((int16_t)si <= 0x10d4);
  L32(-4) = 0;
  do {
    const uint16_t off = (uint16_t)(drd16(0x05f6) + L16(-4));
    const uint16_t seg = drd16(0x05f8);
    if (rd8(seg, off) == 0) break;
    dwr8(0x5866, 0x26);
    dwr8(0x5867, (uint8_t)(L8(-2) + 0x31));
    dwr8(0x5868, 0x20);
    for (uint16_t k = 0;; k++) {
      uint8_t c = rd8(seg, (uint16_t)(off + k));
      dwr8((uint16_t)(0x5869 + k), c);
      if (c == 0) break;
    }
    (void)InsertMenu(L16(-6), (uint16_t)(L16(-2) + 9), 0x0400, (uint16_t)(L16(-2) + 0x10cc), dsfp(0x5866));
    L16(-2) = (uint16_t)(L16(-2) + 1);
    L8(-3) = (uint8_t)(L8(-3) + 1);
  } while ((int16_t)L16(-4) < 0x900);
  cx = (drd8(0x5764) != 0 && (drd8(0x0080) & 4) == 0) ? 0 : 3;
  si = L16(6);
  (void)EnableMenuItem(si, 0x006a, cx);
  (void)EnableMenuItem(si, 0x0069, drd8(0x5764) == 0 ? 3 : 0);
  if (drd8(0x5764) == 0)
    cx = 3;
  else if ((drd8(0x0080) & 4) == 0)
    cx = 0;
  else
    cx = (int32_t)drd32(0x031c) <= 0 ? 3 : 0;
  (void)EnableMenuItem(si, 0x010e, cx);
  cx = (drd8(0x5764) != 0 && (drd8(0x0080) & 4) == 0) ? 0 : 3;
  (void)EnableMenuItem(si, 0x07de, cx);
  cx = (drd8(0x5764) != 0 && (drd8(0x0080) & 4) == 0) ? 0 : 3;
  (void)EnableMenuItem(si, 0x0edb, cx);
  (void)EnableMenuItem(si, 0x0992, (drd8(0x0793) & 0x10) != 0 ? 3 : 0);
  {
    const uint16_t a[2] = {drd16(0x2806), 1};
    si = (uint16_t)call4(0x3b30, a, 2);
  }
  (void)CheckMenuItem(si, 0x00b3, (drd8(0x0793) & 0x80) != 0 ? 8 : 0);
  (void)CheckMenuItem(si, 0x098d, (drd8(0x055f) & 0x20) != 0 ? 8 : 0);
  if (drd16(0x0150) == 0) {
    (void)EnableMenuItem(GetMenu(drd16(0x2806)), 0x0001, 0x0403);
  } else {
    (void)EnableMenuItem(GetMenu(drd16(0x2806)), 0x0001, 0x0400);
    {
      const uint16_t a[2] = {drd16(0x2806), 1};
      di = GetSubMenu((uint16_t)call4(0x3b30, a, 2), 3);
    }
    (void)CheckMenuItem(di, (uint16_t)(drd16(0x0576) + 4), 0x0408);
    si = 0;
    L16(-4) = (uint16_t)GetMenuItemCount(di);
    if ((int16_t)L16(-4) > 0) {
      L16(6) = di;
      do {
        (void)EnableMenuItem(di, si, 0x0400);
        si = (uint16_t)(si + 1);
      } while ((int16_t)si < (int16_t)L16(-4));
    }
    {
      const uint16_t a[2] = {drd16(0x2806), 1};
      (void)CheckMenuItem(GetSubMenu((uint16_t)call4(0x3b30, a, 2), 4), drd16(0x255e), 0x0408);
    }
  }
  DrawMenuBar(drd16(0x2806));
  FRAME_LEAVE();
}

/* 4:0x4cec HOSTOPTIONSDIALOG (Ghidra HOSTOPTIONSDIALOG), 203 bytes. ENTER 0x28,0; PUSH SI. FAR PASCAL (hwnd [BP+0xE], msg
 * [BP+0xC], wParam [BP+0xA], lParam [BP+6]), returns AX only (DX is left over). The dialog procedure of the host options
 * dialog (template 0x402): WM_PAINT (0xf) BeginPaint / 4:0x4db8 / EndPaint, answers 1; WM_ERASEBKGND (0x14) and WM_INITDIALOG
 * (0x110) fill the client rectangle with the brush DS:0x10, answer 1; WM_CTLCOLOR (0x19) sets the background colour DS:0x253a
 * of the DC wParam and answers the brush DS:0x10; WM_COMMAND (0x111) with 1 or 2 ends the dialog (EndDialog(hwnd, id == 1)),
 * 0x76 starts the help (WinHelp(hwnd, DS:[0x10a], HELP_CONTEXT, 0x440)); each answers 1; everything else 0. */
DECOMP(4, 0x4cec, HOSTOPTIONSDIALOG, FAR_PASCAL)
uint16_t HOSTOPTIONSDIALOG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x28 + 2);
  uint16_t si, ax, cx;
  (void)lParam;
  if (msg == 0x000f) {
    si = hwnd;
    pushw(0xffff); /* PUSH -1 stays on the stack through BeginPaint and is the last argument of 4:0x4db8 */
    ax = BeginPaint(si, bpfp(-0x28));
    {
      const uint16_t a[2] = {si, ax};
      (void)call4(0x4db8, a, 2);
    }
    popw(1);
    EndPaint(si, bpfp(-0x28));
    goto ret1;
  }
  if (msg == 0x0014 || msg == 0x0110) {
    GetClientRect(hwnd, bpfp(-8));
    (void)FillRect(wParam, bpfp(-8), drd16(0x0010));
    goto ret1;
  }
  if (msg == 0x0019) {
    (void)SetBkColor(wParam, drd32(0x253a));
    ax = drd16(0x0010);
    FRAME_LEAVE();
    return ax;
  }
  if (msg == 0x0111) {
    cx = wParam;
    if (cx == 2 || cx == 1) {
      (void)EndDialog(hwnd, cx == 1 ? 1 : 0);
      goto ret1;
    }
    if (cx == 0x76) {
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x440);
      goto ret1;
    }
  }
  FRAME_LEAVE();
  return 0;
ret1:
  FRAME_LEAVE();
  return 1;
}

/* paint_host_player_list: 4:0x41f8 (Ghidra FUN_1018_41f8, alias fn_04_41f8).
 * 803 bytes. ENTER 0x38,0; PUSH DI; PUSH SI. FAR CDECL (hwnd [BP+6], hdc [BP+8], 0 = get one
 * with GetDC(hwnd) and release it at the end). Returns nothing (AX is left over; no caller uses it). Paints the list of the
 * DS:0x78 players of the host-mode dialog: for each the rectangle (6, 0x30 + n * (h + 4), h + 7, h + 0x31) with 5:0x34fc, the
 * text "n" of string 0x2e0 (8:0x9398), the status word DS:0x6666 + 2 * n in green (> 0) or red, the player's status text
 * 8:0x8dc0(0x2bc + status) or, with bit 6 of DS:0x796, the name; then the elapsed time since the stamp DS:0x192 (seconds,
 * minutes, hours, days: strings 0x2e4 to 0x2e7) in the controls 0x7e0 and 0x7e1 and the turn number DS:0x82 + 0x961. */
DECOMP(4, 0x41f8, paint_host_player_list, FAR_CDECL)
void paint_host_player_list(uint16_t hwnd, uint16_t hdc)
{
  FRAME_ENTER(0x38 + 4);
  uint16_t si, di, ax;
  if (hdc != 0)
    si = hdc;
  else
    si = GetDC(hwnd);
  L16(-0x12) = (uint16_t)SetBkMode(si, 2);
  {
    uint32_t oldc = SetBkColor(si, drd32(0x253a));
    L16(-0xe) = (uint16_t)oldc;
    L16(-0xc) = (uint16_t)(oldc >> 16);
  }
  (void)SelectObject(si, drd16(0x2930));
  pushw(si);
  ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e1);
  di = (uint16_t)GetTextExtent(si, dsfp(ax), 4);
  popw(1);
  di = (uint16_t)(di + drd16(0x267a) + 0x0a);
  L16(-6) = 0x30;
  SetRect(bpfp(-0x1a), 6, 0x30, (int16_t)(drd16(0x267a) + 7), (int16_t)(drd16(0x267a) + 0x31));
  L16(-2) = 0;
  L16(-0x10) = si;
  if ((int16_t)drd16(0x0078) > 0) {
    L16(-4) = 0x6666;
    si = L16(-2);
    L16(-0xa) = di;
    di = L16(-6);
    do {
      {
        const uint16_t a[3] = {L16(-0x10), bpoff(-0x1a), drd16(0x5a5c)};
        (void)rt_call_far(MK_FP(SEG_SEL(5), 0x34fc), FAR_CDECL, a, 3);   /* draw_diamond_marker */
      }
      pushw(0);
      pushw((uint16_t)(si + 1));
      ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e0);
      ax = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
      popw(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, L16(-0x10), L16(-0xa), di, 0x5866, ax, 0);
      (void)SetTextColor(L16(-0x10), (int16_t)drd16(L16(-4)) > 0 ? 0x7fu : 0x7f00u);
      (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(drd16(L16(-4)) + 0x02bc), bpoff(-0x38));
      if ((drd8(0x0796) & 0x40) != 0) {
        pushw(rt_ss);
        pushw(bpoff(-0x38));
        ax = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(0x03bc), MK_FP(rt_ss, rt_sp));
        popw(2);
      } else {
        pushw(rt_ss);
        pushw(bpoff(-0x38));
        ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, si, 1, 1, 1, 0, 0);
        pushw(MEM_SEL_DGROUP);
        pushw(ax);
        ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02ec);
        ax = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
        popw(4);
      }
      L16(-8) = ax;
      (void)TextOut(L16(-0x10), (int16_t)(uint16_t)(L16(-0xa) + 4), (int16_t)di, dsfp(0x5866), (int16_t)L16(-8));
      (void)SetTextColor(L16(-0x10), drd32(0x4ac0));
      OffsetRect(bpfp(-0x1a), 0, (int16_t)(drd16(0x267a) + 4));
      di = (uint16_t)(di + drd16(0x267a) + 4);
      L16(-4) = (uint16_t)(L16(-4) + 2);
      si = (uint16_t)(si + 1);
    } while ((int16_t)si < (int16_t)drd16(0x0078));
  }
  {
    pushw((uint16_t)(drd16(0x0082) + 0x0961));
    ax = (uint16_t)_wsprintf(dsfp(0x5866), dsfp(drd16(0x016a)), MK_FP(rt_ss, rt_sp));
    popw(1);
    (void)ax;
  }
  SetWindowText(GetDlgItem(hwnd, 0x07e0), dsfp(0x5866));
  {
    const uint32_t elapsed = GetTickCount() - drd32(0x0192);
    L32(-4) = elapsed / 1000u;
  }
  if (L32(-4) < 0x3c) {
    pushw((uint16_t)L32(-4));
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e4);
    (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
    popw(1);
  } else {
    const uint32_t mins = L32(-4) / 0x3cu;
    si = (uint16_t)mins;
    {
      const uint16_t prod = (uint16_t)(si * 0x3c);
      const uint32_t t = L32(-4) - prod;
      L32(-4) = t;
    }
    if (si < 0x3c) {
      pushw(L16(-4));
      pushw(si);
      ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e5);
      (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
      popw(2);
    } else {
      di = (uint16_t)(si / 0x3c);
      si = (uint16_t)(si + (uint16_t)(di * (uint16_t)-0x3c));
      if (di < 0x18) {
        pushw(L16(-4));
        pushw(si);
        pushw(di);
        ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e6);
        (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
        popw(3);
      } else {
        L16(-6) = (uint16_t)(di / 0x18);
        di = (uint16_t)(di + (uint16_t)(L16(-6) * (uint16_t)-0x18));
        pushw(L16(-4));
        pushw(si);
        pushw(di);
        pushw(L16(-6));
        ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02e7);
        (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
        popw(4);
      }
    }
  }
  SetWindowText(GetDlgItem(hwnd, 0x07e1), dsfp(0x5866));
  (void)SetBkMode(L16(-0x10), (int16_t)L16(-0x12));
  (void)SetBkColor(L16(-0x10), L32(-0xe));
  if (hdc == 0) (void)ReleaseDC(hwnd, L16(-0x10));
  FRAME_LEAVE();
}

/* 4:0x467c HOSTMODEDIALOG (Ghidra HOSTMODEDIALOG), 1648 bytes. ENTER 0x42,0; PUSH DI; PUSH SI. FAR PASCAL (hwnd [BP+0xE], msg
 * [BP+0xC], wParam [BP+0xA], lParam [BP+6]), returns AX only (DX is left over). The dialog procedure of the host-mode dialog
 * (template 0x73) of 4:0x4090: the list of the DS:0x78 players with their turn-file state (4:0x4580, 4:0x41f8):
 *   WM_DESTROY (2) kills the timer DS:0x162; WM_PAINT (0xf) paints the list; WM_ERASEBKGND (0x14) the background; WM_CTLCOLOR
 *   (0x19) DS:0x253a / brush DS:0x10; WM_SETCURSOR (0x20), WM_LBUTTONDOWN (0x201) and WM_RBUTTONDOWN (0x204) hit-test the list
 *   row under the cursor (rows 0x30 + n * (h + 4)) and a pop-up of three items (ids 0x3a98 + n: computer/human, "off") sets the
 *   player's state bits in the row (+6 / +7) and asks 14:0x54ea; WM_INITDIALOG (0x110) fills the texts and starts the 10 s timer
 *   (id 0xd) then runs the WM_TIMER (0x113) code: 4:0x4580 and 4:0x41f8; WM_COMMAND (0x111): 0x407 (Go: the
 *   countdown question when Shift / Control is held), 2 (cancel), 0x408 (-1: ends with the players still to play), 0x7df (8:0xb49e
 *   dialog 0x8d), 0x405 (the options dialog 4:0x4cec, template 0x402), 0x76 (help). */
DECOMP(4, 0x467c, HOSTMODEDIALOG, FAR_PASCAL)
uint16_t HOSTMODEDIALOG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x42 + 4);
  uint16_t si, di, ax, cx;
  (void)lParam;
  si = msg;
  if (si == 0x20) goto L4788;
  if (si > 0x20) {
    if (si == 0x110) goto L49f0;
    if (si == 0x111) goto L4ad6;
    if (si == 0x113) goto L4aa0;
    if (si == 0x201) goto L4788;
    if (si == 0x204) goto L4788;
    goto L4ce4;
  }
  if (si == 0x19) goto L4772;
  if (si > 0x19) goto L4ce4;
  if ((uint8_t)si == 2) goto L46d0;
  if ((uint8_t)si == 0x0f) goto L46e6;
  if ((uint8_t)si == 0x14) goto L4750;
  goto L4ce4;

L46d0:
  (void)KillTimer(hwnd, drd16(0x0162));
  dwr16(0x0162, 0);
  goto L4ce4;
L46e6:
  si = hwnd;
  L16(-2) = BeginPaint(si, bpfp(-0x34));
  if (drd32(0x0192) == 0) (void)call4(0x4580, NULL, 0);
  if ((drd8(0x0792) & 0x20) == 0) {
    if ((drd8(0x0792) & 0x10) == 0 && (drd16(0x416c) != 0 || drd16(0x416a) != 0))
      di = 1;
    else
      di = 0;
    (void)EnableWindow(GetDlgItem(si, 0x0408), di);
  }
  {
    const uint16_t a[2] = {si, L16(-2)};
    (void)call4(0x41f8, a, 2);
  }
  EndPaint(si, bpfp(-0x34));
  goto L4acc;
L4750:
  GetClientRect(hwnd, bpfp(-0x14));
  (void)FillRect(wParam, bpfp(-0x14), drd16(0x0010));
  goto L4acc;
L4772:
  (void)SetBkColor(wParam, drd32(0x253a));
  ax = drd16(0x0010);
  FRAME_LEAVE();
  return ax;
L4788:
  GetCursorPos(bpfp(-0xc));
  ScreenToClient(hwnd, bpfp(-0xc));
  if ((int16_t)L16(-0xc) < 6) goto L4ce4;
  if ((int16_t)(uint16_t)(drd16(0x267a) + 7) <= (int16_t)L16(-0xc)) goto L4ce4;
  if ((int16_t)L16(-0xa) < 0x30) goto L4ce4;
  {
    uint16_t q, r;
    idiv16_cwd((uint16_t)(L16(-0xa) - 0x30), (uint16_t)(drd16(0x267a) + 4), &q, &r);
    di = q;
    if ((int16_t)di >= (int16_t)drd16(0x0078)) goto L4ce4;
    idiv16_cwd((uint16_t)(L16(-0xa) - 0x30), (uint16_t)(drd16(0x267a) + 4), &q, &r);
    if ((int16_t)r >= (int16_t)(uint16_t)(drd16(0x267a) + 1)) goto L4ce4;
  }
  if (si == 0x20) {
    (void)SetCursor(drd16(0x5442));
    goto L4acc;
  }
  {
    const uint16_t row = (uint16_t)(di * 0xc0);
    L16(-0x36) = row;
    if ((drd8((uint16_t)(row + 0x5a6b)) & 2) != 0)
      ax = (uint8_t)(drd8((uint16_t)(row + 0x5a6b)) & 0xe0) == 0xe0 ? 2 : 1;
    else
      ax = 0;
    L16(-6) = ax;
    L16(-8) = CreatePopupMenu();
    dwr16(0x4c84, 0xffff);
    si = 0;
    L16(-4) = di;
    do {
      (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(si + 0x0209), 0x5866);
      {
        const uint16_t b = drd8((uint16_t)(L16(-0x36) + 0x5a6b));
        if (si == 1)
          L16(-2) = ((b & 2) == 0 || (uint8_t)(b & 0xe0) == 0xe0) ? 3 : 0;
        else
          L16(-2) = ((b & 2) != 0 && (uint8_t)(b & 0xe0) != 0xe0) ? 3 : 0;
      }
      (void)AppendMenu(L16(-8), (uint16_t)((L16(-6) == si ? 8 : 0) | L16(-2)), (uint16_t)(si + 0x3a98), dsfp(0x5866));
      si = (uint16_t)(si + 1);
    } while ((int16_t)si < 3);
  }
  si = L16(-8);
  ClientToScreen(hwnd, bpfp(-0xc));
  L16(-0x38) = si;
  ax = msg == 0x201 ? 0 : 2;
  (void)TrackPopupMenu(si, (uint16_t)(ax | 4), (int16_t)L16(-0xc), (int16_t)L16(-0xa), 0, hwnd, 0);
  (void)DestroyMenu(L16(-0x38));
  L16(-2) = 0xffff;
  if (PeekMessage(bpfp(-0x26), hwnd, 0x0111, 0x0111, 2) != 0 && L16(-0x22) >= 0x3a98) {
    si = L16(-2);
    if (L16(-0x22) < 0x3afc) si = (uint16_t)(L16(-0x22) - 0x3a98);
  } else {
    si = L16(-2);
  }
  if (si == 0xffff) goto L4ce4;
  if (L16(-6) == si) goto L4ce4;
  {
    const uint16_t row = L16(-0x36);
    ax = (int16_t)si > 0 ? 1 : 0;
    ax = (uint16_t)(ax << 9);
    {
      uint8_t ah = (uint8_t)((ax >> 8) ^ drd8((uint16_t)(row + 0x5a6b)));
      ax = (uint16_t)(((uint16_t)ah << 8 | (ax & 0xff)) & 0x0200);
    }
    dwr16((uint16_t)(row + 0x5a6a), (uint16_t)(drd16((uint16_t)(row + 0x5a6a)) ^ ax));
    if (si == 2) dwr8((uint16_t)(row + 0x5a6b), (uint8_t)(drd8((uint16_t)(row + 0x5a6b)) | 0xe0));
    dwr16((uint16_t)(row + 0x5a70), (uint16_t)~drd16((uint16_t)(row + 0x5a70)));
    dwr16((uint16_t)(row + 0x5a72), (uint16_t)~drd16((uint16_t)(row + 0x5a72)));
  }
  ax = si != 0 ? 1 : 0;
  L16(-0x3a) = ax;
  (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 3, di, 8, ax);
  (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 2, di, 8, L16(-0x3a));
  dwr8(0x0791, (uint8_t)(drd8(0x0791) & 0xfb));
  dwr16(0x03c0, 1);
  (void)call4(0x4580, NULL, 0);
  if ((drd8(0x0792) & 0x10) == 0 && (drd16(0x416c) != 0 || drd16(0x416a) != 0))
    si = 1;
  else
    si = 0;
  (void)EnableWindow(GetDlgItem(hwnd, 0x0408), si);
  {
    const uint16_t a[2] = {hwnd, 0};
    (void)call4(0x41f8, a, 2);
  }
  dwr16(0x03c0, 0);
  goto L4ce4;
L49f0:
  si = hwnd;
  (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, si, 0x03c2, 1);
  SetWindowText(GetDlgItem(si, 0x0409), dsfp(0x0090));
  SetWindowText(GetDlgItem(si, 0x040a), dsfp(0x5764));
  if ((drd8(0x0792) & 0x20) == 0 && (drd16(0x416c) != 0 || drd16(0x416a) != 0))
    di = 1;
  else
    di = 0;
  (void)EnableWindow(GetDlgItem(si, 0x0408), di);
  (void)EnableWindow(GetDlgItem(si, 0x0407), (drd8(0x0792) & 0x20) == 0 ? 1 : 0);
  (void)EnableWindow(GetDlgItem(si, 0x07df), (drd8(0x0792) & 0x20) == 0 ? 1 : 0);
  ax = SetTimer(si, 0x000d, 0x2710, 0);
  dwr16(0x0162, ax);
L4aa0:
  if (drd16(0x03c0) == 0) {
    dwr16(0x03c0, 1);
    (void)call4(0x4580, NULL, 0);
    {
      const uint16_t a[2] = {hwnd, 0};
      (void)call4(0x41f8, a, 2);
    }
    dwr16(0x03c0, 0);
  }
  if (msg == 0x113) goto L4ce4;
L4acc:
  FRAME_LEAVE();
  return 1;
L4ad6:
  si = wParam;
  if (si == 0x407 || si == 2 || si == 0x408) goto L4bea;
  if (si == 0x07df) {
    if ((uint16_t)CALL_ORIG(8, 0xb1ec, FAR_CDECL, 0) == 0) goto L4ce4;
    {
      uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(8), 0xb49e), drd16(0x54f4));
      L16(-0x3e) = (uint16_t)pi;
      L16(-0x3c) = (uint16_t)(pi >> 16);
      di = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x0000008du, hwnd, pi));
      FreeProcInstance(pi);
    }
    (void)SetFocus(hwnd);
    goto L4b40;
  }
  if (si == 0x405) {
    {
      uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(4), 0x4cec), drd16(0x54f4));
      L16(-0x42) = (uint16_t)pi;
      L16(-0x40) = (uint16_t)(pi >> 16);
      di = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x00000402u, hwnd, pi));
      FreeProcInstance(pi);
    }
    (void)SetFocus(hwnd);
    if (di == 0) goto L4b40;
    if ((drd8(0x0792) & 0x20) != 0) goto L4b40;
    if ((drd8(0x0792) & 0x10) == 0 && (drd16(0x416c) != 0 || drd16(0x416a) != 0))
      si = 1;
    else
      si = 0;
    (void)EnableWindow(GetDlgItem(hwnd, 0x0408), si);
    goto L4b40;
  }
  if (si != 0x76) goto L4ce4;
  (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x440);
  goto L4acc;
L4b40:
  FRAME_LEAVE();
  return di;
L4bea:
  if (si == 0x407) {
    if ((int16_t)GetAsyncKeyState(0x10) < 0) {
      if ((int16_t)GetAsyncKeyState(0x11) < 0)
        ax = 0x03e8;
      else
        ax = 0x000a;
    } else if ((int16_t)GetAsyncKeyState(0x11) < 0) {
      ax = 0x0064;
    } else {
      ax = 0;
    }
    dwr16(0x0378, ax);
    if (ax != 0) {
      pushw(ax);
      ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0520);
      (void)_wsprintf(dsfp(0x5866), dsfp(ax), MK_FP(rt_ss, rt_sp));
      popw(1);
      if (MessageBox(GetFocus(), dsfp(0x5866), dsfp(0x03c6), 0x2034) != 6) {
        dwr16(0x0378, 0);
        goto L4acc;
      }
    } else {
      if ((uint16_t)call4(0x4580, NULL, 0) != 0) {
        if ((uint16_t)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x0264, 0x1024) != 6) goto L4acc;
      }
    }
  }
  (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x03c2, 0);
  if (si == 2)
    cx = 0;
  else if (si == 0x408)
    cx = 0xffff;
  else
    cx = 1;
  (void)EndDialog(hwnd, (int16_t)cx);
  if (si == 0x408) {
    (void)call4(0x39f8, NULL, 0);
    goto L4acc;
  }
  if (si == 2 && (drd8(0x0794) & 4) != 0) PostQuitMessage((int16_t)drd16(0x0366));
  goto L4acc;
L4ce4:
  FRAME_LEAVE();
  return 0;
}

/* Helpers of 4:0x506c (the settings writer). The frame is the caller's: app at BP-0x30, key at BP-0x20, file at
 * BP-0x40, the "%d" format at BP-0x10. */
static inline void ini_load(uint16_t id, int16_t bufoff)
{
  (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, bpoff(bufoff));
}
/* WritePrivateProfileString(app, key, DS:0x5866, file) */
static inline void ini_put(void)
{
  (void)WritePrivateProfileString(bpfp(-0x30), bpfp(-0x20), dsfp(0x5866), bpfp(-0x40));
}
/* _wsprintf(DS:0x5866, ss:BP-0x10 "%d", v) */
static inline void ini_fmt_d(uint16_t v)
{
  pushw(v);
  (void)_wsprintf(dsfp(0x5866), bpfp(-0x10), MK_FP(rt_ss, rt_sp));
  popw(1);
}
/* _wsprintf(DS:0x5866, DS:[0x16a], v): the format is the string a variable points to. */
static inline void ini_fmt_var(uint16_t v)
{
  pushw(v);
  (void)_wsprintf(dsfp(0x5866), dsfp(drd16(0x016a)), MK_FP(rt_ss, rt_sp));
  popw(1);
}
/* The window rectangle at DS:o as (x, y, x + w, y + h) with the 8:0x8df4 format 0x30c and the letter 0x4d. */
static inline void ini_rect(uint16_t o)
{
  pushw((uint16_t)(drd16((uint16_t)(o + 2)) + drd16((uint16_t)(o + 6))));
  pushw((uint16_t)(drd16(o) + drd16((uint16_t)(o + 4))));
  pushd(drd32(o));
  pushw(0x004d); /* the high word of PUSH DWORD 0x4d030c; the low word is the argument of 8:0x8df4 */
  uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x030c);
  (void)_wsprintf(dsfp(0x5866), dsfp(fmt), MK_FP(rt_ss, rt_sp));
  popw(5);
}
static inline void ini_itoa(uint16_t v)
{
  (void)CALL_ORIG(34, 0x037e, FAR_CDECL, 3, v, 0x5866, 10);
}

/* save_settings: 4:0x506c (Ghidra FUN_1018_506c, alias fn_04_506c).
 * 3371 bytes. ENTER 0x40,0; PUSH DI; PUSH SI. FAR CDECL, no arguments.
 * Saves the settings into the INI file (WritePrivateProfileString, app name 8:0x8dc0(0x90) at BP-0x30,
 * file name 0x91 at BP-0x40, the key name loaded into BP-0x20 before every write, the value text in
 * DS:0x5866): the serial encoded by 4:0x1af2, the flags, the window states (4:0x500c) and rectangles, the
 * numbers DS:0x255e, 0x2654..0x265e, the two tables at DS:0x7c2 / 0x8d6 (stars and a letter per entry), the
 * mode letter of DS:0x4b74, and (flag bits of DS:0x794) the per-game and the two tables of per-slot settings
 * (DS:0x545f stride 0x18, DS:0x2585 stride 0x28). AX is left over (the last write result or an earlier
 * value), so the function is void. When DS:0x4b74 is 3 or above 4 the mode letter at BP-1 is never set (a stale
 * stack byte is formatted). */
DECOMP(4, 0x506c, save_settings, FAR_CDECL)
void save_settings(void)
{
  FRAME_ENTER(0x40 + 4);
  uint16_t di, si, ax, cx, dx;
  L8(-0x10) = 0x25;
  L8(-0x0f) = 0x64;
  L8(-0x0e) = 0;
  ini_load(0x90, -0x30);
  ini_load(0x91, -0x40);
  ini_load(0xa4, -0x20);
  {
    const uint16_t a[4] = {(uint16_t)drd32(0x0872), (uint16_t)(drd32(0x0872) >> 16), 0x5382, 0x5866};
    (void)call4(0x1af2, a, 4);
  }
  ini_put();
  ini_load(0xad, -0x20);
  L16(-6) = 0;
  if ((int16_t)drd16(0x54f6) <= 4) L16(-6) = 1;
  if ((drd8(0x0791) & 0xc0) == 0) L8(-6) = (uint8_t)(L8(-6) | 2);
  ini_fmt_d(L16(-6));
  ini_put();
  ini_load(0x94, -0x20);
  {
    const uint16_t a[2] = {0x5866, drd16(0x2806)};
    (void)call4(0x500c, a, 2);
  }
  ini_put();
  ini_load(0xb2, -0x20);
  ini_rect(0x1694);
  ini_put();
  ini_load(0xb3, -0x20);
  ini_rect(0x16ca);
  ini_put();
  ini_load(0xb4, -0x20);
  ini_rect(0x1700);
  ini_put();
  ini_load(0xb1, -0x20);
  ini_rect(0x165e);
  ini_put();
  {
    static const struct { uint16_t id, var; } nums[] = {
      {0xa3, 0x255e}, {0xa5, 0x2654}, {0xa6, 0x2656}, {0xa7, 0x2658}, {0xa8, 0x265a}, {0xa9, 0x265c}, {0xaa, 0x265e},
    };
    for (uint16_t k = 0; k < sizeof nums / sizeof nums[0]; k++) {
      ini_load(nums[k].id, -0x20);
      ini_fmt_d(drd16(nums[k].var));
      ini_put();
    }
  }
  ini_load(0xab, -0x20);
  ini_fmt_d((uint16_t)(drd16(0x0792) >> 15));
  ini_put();
  /* The two tables of 6 and 7 entries (stride 0x10, flags at +0xa): a run of '*' (the count in the low 3 bits,
   * cumulative over the entries), then a letter 'a'/'A' plus the nibble in bits 3..6. */
  L16(-0xa) = 2;
  di = 0x07c2;
  L16(-8) = 6;
  ini_load(0x96, -0x20);
  for (;;) {
    ax = L16(-0xa);
    L16(-0xa) = (uint16_t)(L16(-0xa) - 1);
    if (ax == 0) break;
    L16(-4) = 0x5866;
    dx = 0;
    cx = 0;
    ax = (uint16_t)(di + 0xa);
    L16(-2) = ax;
    L16(-0xc) = di;
    di = ax;
    while ((int16_t)L16(-8) > (int16_t)cx) {
      if ((uint16_t)(drd8(di) & 7) > dx) {
        si = L16(-4);
        L16(-6) = cx;
        L16(-2) = di;
        do {
          dwr8(si, 0x2a);
          si = (uint16_t)(si + 1);
          dx = (uint16_t)(dx + 1);
        } while ((uint16_t)(drd8(di) & 7) > dx);
      } else {
        si = L16(-4);
      }
      dwr8(si, (drd8(di) & 0x80) == 0 ? 0x61 : 0x41);
      ax = drd16(di);
      di = (uint16_t)(di + 0x10);
      dwr8(si, (uint8_t)(drd8(si) + ((ax >> 3) & 0xf)));
      cx = (uint16_t)(cx + 1);
      si = (uint16_t)(si + 1);
      L16(-4) = si;
    }
    dwr8(L16(-4), 0);
    ini_put();
    ini_load(0x95, -0x20);
    di = 0x08d6;
    L16(-8) = 7;
  }
  ini_load(0x98, -0x20);
  ax = drd16(0x4b74);
  if (ax == 0)
    L8(-1) = 0x4e;
  else if (ax == 1)
    L8(-1) = 0x50;
  else if (ax == 2)
    L8(-1) = 0x53;
  else if (ax == 4)
    L8(-1) = 0x45;
  pushw(drd16(0x4b78));
  pushw((uint16_t)(int16_t)(int8_t)(uint8_t)(drd8(0x014c) + 0x42));
  pushw((uint16_t)(int16_t)(int8_t)L8(-1));
  {
    uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x030d);
    (void)_wsprintf(dsfp(0x5866), dsfp(fmt), MK_FP(rt_ss, rt_sp));
  }
  popw(3);
  ini_put();
  ini_load(0xc8, -0x20);
  ini_fmt_var(drd16(0x0ab6));
  ini_put();
  ini_load(0xac, -0x20);
  pushd(drd32(0x0070));
  (void)_wsprintf(dsfp(0x5866), dsfp(0x03cf), MK_FP(rt_ss, rt_sp));
  popw(2);
  ini_put();
  ini_load(0x97, -0x20);
  dwr8(0x5866, (uint8_t)(drd8(0x0576) + 0x35));
  dwr8(0x5867, 0);
  ini_put();
  if ((drd8(0x0794) & 0x40) != 0) {
    ini_itoa(drd16(0x055e));
    ini_load(0x9d, -0x20);
    ini_put();
    ini_itoa(drd16(0x54ec));
    ini_load(0x9e, -0x20);
    ini_put();
    ini_itoa(drd16(0x538e));
    ini_load(0x9f, -0x20);
    ini_put();
    ini_itoa(drd16(0x4c76));
    ini_load(0xa0, -0x20);
    ini_put();
    ini_itoa(drd16(0x0560));
    ini_load(0xa1, -0x20);
    ini_put();
  }
  ini_itoa(drd16(0x04c8));
  ini_load(0xa2, -0x20);
  ini_put();
  if (drd16(0x014c) != 0xffff) {
    ini_load(0x99, -0x30);
    ini_load(0x9a, -0x20);
    dwr8(0x5866, (uint8_t)((drd16(0x0162) == 0 ? 0 : 1) + 0x30));
    ini_put();
    if ((drd8(0x0795) & 1) != 0) {
      ini_itoa(drd16(0x0082));
      ini_load(0x9c, -0x20);
      ini_put();
      dwr8(0x0795, (uint8_t)(drd8(0x0795) & 0xfe));
    }
    ini_load(0x9b, -0x20);
    pushw((uint16_t)(drd16(0x014c) + 1));
    pushd(dsfp(0x5764));
    (void)_wsprintf(dsfp(0x5866), dsfp(0x03d3), MK_FP(rt_ss, rt_sp));
    popw(3);
    ini_put();
  }
  ini_load(0x92, -0x30);
  if ((drd8(0x0794) & 0x80) != 0) {
    static const struct { uint16_t id, var, hi; } w[] = {
      {0xb5, 0x164e, 0},      {0xb6, 0x1658, 0x165a}, {0xb7, 0x1684, 0},      {0xb8, 0x168e, 0x1690},
      {0xb9, 0x16ba, 0},      {0xba, 0x16c4, 0x16c6}, {0xbb, 0x16f0, 0},      {0xbc, 0x16fa, 0x16fc},
    };
    for (uint16_t k = 0; k < sizeof w / sizeof w[0]; k++) {
      ini_load(w[k].id, -0x20);
      si = drd16(w[k].var);
      if (w[k].hi != 0 && drd16(w[k].hi) != 0) si = (uint16_t)(si | 0x100);
      ini_fmt_var(si);
      ini_put();
    }
    ini_load(0xbd, -0x20);
    ini_fmt_var((uint16_t)(drd8(0x0796) & 0xf));
    ini_put();
  }
  ini_load(0xbf, -0x20);
  ini_fmt_var(drd16(0x27f4));
  ini_put();
  ini_load(0xc0, -0x20);
  ini_fmt_var(drd16(0x15c2));
  ini_put();
  if ((drd8(0x0794) & 0x10) != 0) {
    ini_load(0x93, -0x30);
    L16(-6) = 0;
    L16(-2) = 0x545f;
    do {
      strcpy_at(rt_ss, bpoff(-0x20), bpoff(-0x30));
      {
        const uint16_t n = strlen_at(rt_ss, bpoff(-0x20));
        wr8(rt_ss, (uint16_t)(bpoff(-0x20) + n), (uint8_t)(L16(-6) + 0x31));
        wr8(rt_ss, (uint16_t)(bpoff(-0x20) + n + 1), 0);
      }
      const uint16_t bx = L16(-2);
      if (drd8(bx) != 0) {
        si = 0x5866;
        di = (uint16_t)(bx - 0x17);
        for (dx = 5; dx != 0; dx--) {
          dwr8(si++, (uint8_t)(((drd16(di) >> 12) & 0xf) + 0x61));
          dwr8(si++, (uint8_t)((drd8(di) & 0xf) + 0x61));
          dwr8(si++, (uint8_t)(((drd16(di) >> 4) & 0xf) + 0x61));
          dwr8(si++, (uint8_t)((drd8((uint16_t)(di + 1)) & 0xf) + 0x61));
          di = (uint16_t)(di + 2);
        }
        strcpy_at(MEM_SEL_DGROUP, si, (uint16_t)(L16(-2) - 0xd));
      } else {
        dwr8(0x5866, 0);
      }
      ini_put();
      L16(-6) = (uint16_t)(L16(-6) + 1);
      L16(-2) = (uint16_t)(L16(-2) + 0x18);
    } while (L16(-2) < 0x54bf);
  }
  if ((drd8(0x0794) & 0x20) != 0) {
    L16(-6) = 0;
    L16(-2) = 0x2585;
    do {
      ini_load(0x93, -0x30);
      strcpy_at(rt_ss, bpoff(-0x20), bpoff(-0x30));
      {
        const uint16_t n = strlen_at(rt_ss, bpoff(-0x20));
        L16(-4) = (uint16_t)(bpoff(-0x20) + n);
        wr8(rt_ss, L16(-4), 0x50);
        L16(-4) = (uint16_t)(L16(-4) + 1);
        wr8(rt_ss, L16(-4), (uint8_t)(L16(-6) + 0x31));
        wr8(rt_ss, (uint16_t)(L16(-4) + 1), 0);
      }
      const uint16_t bx = L16(-2);
      if (drd8((uint16_t)(bx - 2)) != 0) {
        dwr8(0x5866, (uint8_t)(drd8((uint16_t)(bx - 1)) + 0x61));
        dwr8(0x5867, (uint8_t)(drd8(bx) + 0x61));
        si = 0x5868;
        dx = 0;
        if (drd8(bx) != 0) {
          di = (uint16_t)(bx + 1);
          do {
            dwr8(si++, (uint8_t)((drd8(di) & 0xf) + 0x61));
            dwr8(si++, (uint8_t)(((drd16(di) >> 4) & 0xf) + 0x61));
            dwr8(si++, (uint8_t)((drd8((uint16_t)(di + 1)) & 0xf) + 0x61));
            dwr8(si++, (uint8_t)(((drd16(di) >> 12) & 0xf) + 0x61));
            di = (uint16_t)(di + 2);
            dx = (uint16_t)(dx + 1);
          } while ((int16_t)drd8(bx) > (int16_t)dx);
        }
        strcpy_at(MEM_SEL_DGROUP, si, (uint16_t)(L16(-2) - 0xf));
      } else {
        dwr8(0x5866, 0);
      }
      ini_put();
      L16(-6) = (uint16_t)(L16(-6) + 1);
      L16(-2) = (uint16_t)(L16(-2) + 0x28);
    } while (L16(-2) < 0x264d);
  }
  FRAME_LEAVE();
}

/* (x * 3) / 2 style halving of a 16-bit product, rounded toward zero: CWD; SUB AX,DX; SAR AX,1. */
static inline uint16_t sdiv2(uint16_t ax)
{
  const uint16_t dx = (int16_t)ax < 0 ? 0xffffu : 0;
  return (uint16_t)((int16_t)(uint16_t)(ax - dx) >> 1);
}

/* 4:0x60f6 TITLEWNDPROC (Ghidra TITLEWNDPROC), 1486 bytes. ENTER 0x46,0; PUSH DI; PUSH SI. FAR PASCAL (hwnd [BP+0xE],
 * msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]); returns DX:AX (DX is 0 except for DefWindowProc). The window
 * procedure of the title (start-up) window: four buttons whose captions are the strings 8:0x8df4(0x1db + k) (ids
 * 0..3, handles in DS:0x3de..0x3e4), a banner painted either as a bitmap (256 colours: DS:0x54f6 >= 8 and the
 * resource of 8:0xad28 in DS:0x3dc, palette DS:0x3da) or as text.
 *   0x01 WM_CREATE   (loads the bitmap and its palette when DS:0x791 has bits 6/7), creates the four BUTTONs, disables
 *                    the one at DS:0x3e2 when the game name DS:0x5764 is empty or 34:0x84e finds no such file
 *   0x02 WM_DESTROY  frees the bitmap resource, saves the settings (4:0x506c) and quits (or ExitWindows)
 *   0x0f WM_PAINT    the icon when iconic, else the banner and the version text
 *   0x111 WM_COMMAND the button ids 0..3
 *   0x30f / 0x311    realises the palette (0x311 from the window itself does nothing)
 * Everything else, and the cases that fall through, go to DefWindowProc. */
DECOMP(4, 0x60f6, TITLEWNDPROC, FAR_PASCAL)
uint32_t TITLEWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x46 + 4);
  uint16_t di, si, ax, cx;
  uint16_t q, r;
  if (msg == 0x0f) goto m0f;
  if (msg > 0x0f) {
    if (msg == 0x111) goto m111;
    if (msg == 0x30f) goto m30f;
    if (msg == 0x311) goto m311;
    goto deflt;
  }
  if (msg == 1) goto m01;
  if (msg == 2) goto m02;

deflt: {
    uint32_t rv = DefWindowProc(hwnd, msg, wParam, lParam);
    FRAME_LEAVE();
    return rv;
  }
ret0:
  FRAME_LEAVE();
  return 0;

m01: /* WM_CREATE */
  if ((int16_t)drd16(0x54f6) >= 8 && (drd8(0x0791) & 0xc0) != 0) {
    ax = (uint16_t)CALL_ORIG(8, 0xad28, FAR_CDECL, 1, 0x01c1);
    dwr16(0x03dc, ax);
    if (drd16(0x03da) == 0 && ax != 0) {
      ax = (uint16_t)CALL_ORIG(8, 0xa910, FAR_CDECL, 1, ax);
      dwr16(0x03da, ax);
    }
  }
  GetClientRect(hwnd, bpfp(-0x1e));
  si = (uint16_t)((int16_t)L16(-0x1a) >> 3);
  if ((int16_t)si < 0x78) si = 0x78;
  if ((int16_t)L16(-0x18) < 0x28a) {
    idiv16_cwd(si, 6, &q, &r);
    si = (uint16_t)(si + q);
  }
  ax = (uint16_t)((uint16_t)(si << 2) - L16(-0x1a));
  ax = (uint16_t)-(int16_t)ax;
  ax = (uint16_t)((int16_t)ax >> 2);
  L16(-0xc) = ax;
  L16(-8) = (uint16_t)((int16_t)ax >> 1);
  if ((int16_t)L16(-0x18) > 0x1f4)
    ax = sdiv2((uint16_t)(drd16(0x267a) * 5));
  else
    ax = (uint16_t)(drd16(0x267a) + drd16(0x267a));
  L16(-0xa) = ax;
  di = 0;
  L16(-2) = 0x03de;
  L16(-0xe) = si;
  si = L16(-2);
  do {
    ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, (uint16_t)(di + 0x01db));
    L16(-4) = ax;
    ax = (uint16_t)(-(int16_t)(uint16_t)(sdiv2((uint16_t)(drd16(0x267a) * 5)) - L16(-0x18)) - L16(-0xa));
    ax = CreateWindow(dsfp(0x03e6), dsfp(L16(-4)), 0x50000000u, (int16_t)L16(-8), (int16_t)ax, (int16_t)L16(-0xe),
                      (int16_t)L16(-0xa), hwnd, di, drd16(0x54f4), 0);
    dwr16(si, ax);
    if (si == 0x03e2) {
      uint16_t skip = 0;
      if (drd8(0x5764) != 0) {
        ax = (uint16_t)CALL_ORIG(34, 0x084e, FAR_CDECL, 2, 0x5764, 0);
        skip = (uint16_t)(ax + 1) != 0;
      }
      if (!skip) (void)EnableWindow(drd16(0x03e2), 0);
    }
    if ((int16_t)L16(-0x18) < 0x1f4) (void)API_SP(10, SendMessage(drd16(si), 0x30, drd16(0x2930), 0));
    L16(-8) = (uint16_t)(L16(-8) + (uint16_t)(L16(-0xc) + L16(-0xe)));
    di = (uint16_t)(di + 1);
    si = (uint16_t)(si + 2);
  } while (si < 0x03e6);
  goto ret0;

m02: /* WM_DESTROY */
  if (drd16(0x03dc) != 0) {
    (void)GlobalUnlock(drd16(0x03dc));
    (void)FreeResource(drd16(0x03dc));
    dwr16(0x03dc, 0);
  }
  if (drd16(0x0314) == 0) {
    if ((drd8(0x0792) & 0x40) != 0) {
      (void)ExitWindows((uint32_t)(int32_t)(int16_t)drd16(0x0366), 0);
    } else {
      (void)call4(0x506c, NULL, 0);
      PostQuitMessage((int16_t)drd16(0x0366));
    }
  }
  goto deflt;

m0f: /* WM_PAINT */
  if (IsIconic(hwnd) != 0) {
    ax = BeginPaint(hwnd, bpfp(-0x3e));
    (void)DrawIcon(ax, 2, 2, drd16(0x0316));
    EndPaint(hwnd, bpfp(-0x3e));
    goto ret0;
  }
  L16(-4) = LocalAlloc(0x40, 0x32);
  L16(-2) = BeginPaint(hwnd, bpfp(-0x46));
  (void)SelectObject(L16(-2), drd16(0x0010));
  GetClientRect(hwnd, bpfp(-0xe));
  if ((int16_t)drd16(0x54f6) >= 8) {
    (void)SetTextColor(L16(-2), 0x02ff7fffu);
    if (drd16(0x03dc) != 0) {
      (void)SelectPalette(L16(-2), drd16(0x03da), 0);
      (void)RealizePalette(L16(-2));
      {
        const uint16_t cy = (uint16_t)GetSystemMetrics(1);
        const uint16_t cxw = (uint16_t)GetSystemMetrics(0);
        (void)CALL_ORIG(8, 0xaa4e, FAR_CDECL, 12, L16(-2), 0, 0, cxw, cy, drd16(0x03dc), 0, 0, 0x0320, 0x0258, 0x0020, 0x00cc);
      }
      goto t6469;
    }
  }
  L16(-0x1a) = L16(-0xa);
  L16(-0x1e) = 0;
  L16(-0x18) = 0;
  (void)CALL_ORIG(7, 0x3cd4, FAR_CDECL, 2, L16(-2), bpoff(-0xe));
  idiv16_cwd(L16(-8), 0xfffd, &q, &r);
  {
    const uint16_t bx = L16(-4);
    dwr16(bx, q);
    strcpy_at(MEM_SEL_DGROUP, (uint16_t)(bx + 0x12), 0x072a);
    L16(-6) = CreateFontIndirect(dsfp(bx));
  }
  (void)SetTextColor(L16(-2), 0x009b009bu);
  if (L16(-6) != 0) {
    si = SelectObject(L16(-2), L16(-6));
    (void)SetBkMode(L16(-2), 1);
    L32(-0x16) = L32(-0xe);
    {
      const uint32_t eax = L32(-0xa);
      const uint16_t dx = L16(-8);
      L32(-0x12) = eax;
      ax = (uint16_t)(dx + dx + dx);
      cx = (uint16_t)((int16_t)ax < 0 ? 0xffffu : 0);
      ax = (uint16_t)(ax + (cx & 3));
      L16(-0x10) = (uint16_t)((int16_t)ax >> 2);
    }
    (void)CALL_ORIG(8, 0x9318, FAR_CDECL, 4, L16(-2), bpoff(-0x16), 0x03ed, 6);
    (void)SelectObject(L16(-2), si);
    (void)DeleteObject(L16(-6));
  }
t6469:
  (void)SelectObject(L16(-2), drd16(0x4a5c));
  (void)SetBkMode(L16(-2), 1);
  (void)far_call(3, 0x0c7a, NULL, 0, FAR_CDECL);
  GetWindowRect(drd16(0x03de), bpfp(-0x26));
  ax = (uint16_t)(sdiv2((uint16_t)(drd16(0x267a) * 9)) - L16(-0x24));
  ax = (uint16_t)-(int16_t)ax;
  L16(-0xc) = ax;
  L16(-8) = (uint16_t)(ax + sdiv2((uint16_t)(drd16(0x267a) * 3)));
  (void)CALL_ORIG(8, 0x9318, FAR_CDECL, 4, L16(-2), bpoff(-0xe), 0x5866, strlen_at(MEM_SEL_DGROUP, 0x5866));
  OffsetRect(bpfp(-0xe), 0, (int16_t)sdiv2((uint16_t)(drd16(0x267a) * 3)));
  (void)CALL_ORIG(8, 0x9318, FAR_CDECL, 4, L16(-2), bpoff(-0xe), 0x03f4, 0x22);
  EndPaint(hwnd, bpfp(-0x46));
  (void)LocalFree(L16(-4));
  goto ret0;

m111: /* WM_COMMAND */
  if (wParam == 0) {
    {
      const uint16_t a[2] = {hwnd, 0};
      (void)far_call(15, 0x6ab2, a, 2, FAR_CDECL);
    }
    if (drd32(0x00b6) == 0 && drd32(0x0070) == 0) {
      (void)SetFocus(hwnd);
      goto ret0;
    }
    if (drd16(0x0314) == 0) {
      dwr16(0x0314, 1);
      (void)DestroyWindow(drd16(0x0312));
      dwr16(0x0312, 0);
    }
    (void)ShowWindow(drd16(0x2806), 5);
    goto ret0;
  }
  if (wParam != 1 && wParam != 2) {
    if (wParam != 3) goto ret0;
    if ((drd8(0x0792) & 0x40) != 0) {
      (void)ExitWindows((uint32_t)(int32_t)(int16_t)drd16(0x0366), 0);
      goto ret0;
    }
    (void)call4(0x506c, NULL, 0);
    PostQuitMessage((int16_t)drd16(0x0366));
    goto ret0;
  }
  if (wParam == 2) dwr8(0x251a, (uint8_t)(drd8(0x251a) | 1));
  {
    const uint16_t a[2] = {hwnd, 0};
    ax = (uint16_t)call4(0x3b66, a, 2);
  }
  if ((int16_t)ax <= 0) {
    (void)SetFocus(hwnd);
  } else {
    if (drd16(0x0314) == 0) {
      dwr16(0x0314, 1);
      (void)DestroyWindow(drd16(0x0312));
      dwr16(0x0312, 0);
    }
    if (drd16(0x014c) != 0xffff) (void)ShowWindow(drd16(0x2806), 5);
    {
      const uint16_t a[1] = {0};
      (void)call4(0x379a, a, 1);
    }
    (void)PostMessage(drd16(0x2806), 0x0111, 0x0fa1, 0);
    if ((drd8(0x0080) & 8) != 0 && drd16(0x014c) == 0) {
      const uint16_t a[1] = {0};
      (void)far_call(31, 0x5f14, a, 1, FAR_CDECL);
    }
  }
  dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfe));
  goto ret0;

m311: /* WM_PALETTECHANGED from another window */
  if (wParam == hwnd) goto ret0;
m30f: /* WM_QUERYNEWPALETTE */
  if ((int16_t)drd16(0x54f6) < 8) goto ret0;
  si = GetDC(hwnd);
  di = SelectPalette(si, drd16(0x03da), 0);
  L16(-6) = RealizePalette(si);
  (void)SelectPalette(si, di, 0);
  (void)ReleaseDC(hwnd, si);
  if (L16(-6) == 0) goto ret0;
  InvalidateRect(hwnd, 0, 1);
  FRAME_LEAVE();
  return 1;
}
