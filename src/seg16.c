/* Decompiled code segment 16 (NE segment 16, selector 0x0080). Local-only.
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg16.c; Ghidra's pseudo-C in re/decomp/seg16 is only a reading aid.
 *
 * Segment 16 holds four dialog procedures and the fleet code around waypoint tasks, stargates and the
 * sums over a fleet's ship designs:
 *   16:0x0000 ZIPORDERDLG    the ZIP order dialog (four presets, radio buttons 0x431 to 0x434)
 *   16:0x0606 zip_enable_preset_buttons  enable or disable the two preset buttons of ZIPORDERDLG
 *   16:0x063e RENAMEZIPDLG   the preset-name dialog of ZIPORDERDLG
 *   16:0x0794 RENAMEDLG      a second name dialog (the same shape, a 31-character name)
 *   16:0x0962 apply_stargate_losses  the ship losses of a fleet's stargate jump, with the messages
 *   16:0x0e10 stargate_jump_loss  can a design of a given mass go a given distance through two gates,
 *             and at what loss percentage
 *   16:0x1060 update_fleet_waypoints  every fleet: resolve fleet targets, move a reached waypoint 1 into slot 0
 *   16:0x133e freeze_waypoints_aimed_at_fleet  other players' waypoints that target a fleet keep its position
 *   16:0x13f8 route_fleet_from_planet  a one-leg course to the planet a planet names, with a warp speed
 *   16:0x16f6 fleet_has_colony_hull / 16:0x175c fleet_has_scout_frigate_destroyer  hull tests
 *   16:0x17c2 set_mining_waypoint_task  the task of waypoint 0 of a fleet with mining robots
 *   16:0x193e fleet_mining_rate / 16:0x1a34 fleet_orbital_adjusters / 16:0x1aea fleet_mine_laying_rate /
 *   16:0x1ca2 fleet_beam_rating / 16:0x1d1c design_beam_rating / 16:0x1e02 fleet_cloak_percent /
 *   16:0x20c8 slot_cloak_units  sums over the component slots of a fleet's (or one design's) designs
 *   16:0x21bc MERGEFLEETSDLG the dialog that picks the fleets to merge
 *   16:0x244c tally_design_parts  a design's maximum tech requirements and its special-part counts
 * Names: the export names ZIPORDERDLG, RENAMEZIPDLG, RENAMEDLG and MERGEFLEETSDLG are kept. Ghidra's
 * other names are all generic (FUN_1078_<off>) and none of them wraps a single Win16 API, so their C
 * names describe what each function does (CONVENTIONS rule f); they are descriptive, not the game's.
 * The component meanings come from the item tables of segment 2 (2:0x50f8 check_item_usable), whose
 * records carry the item names: category 0x10 is the beam weapons (+0x34 range, +0x36 power, +0x3a bit
 * 0 on the sappers, bit 1 on the gatling guns), 0x80 the mining robots (items 0 to 6, +0x34 the mining
 * rate; item 7 the Orbital Adjuster), 0x100 the mine dispensers (0 to 3 standard, 4 to 6 heavy, 7 to 9
 * speed trap; +0x34 the mines laid / 10), 0x200 the stargates (items 0 to 6 "Stargate mass/range": +0x34
 * the mass limit, +0x36 the range, -1 for "any") and mass drivers, 0x800 items 0 to 4 the cloaks and
 * the Multi Function Pod (+0x34 the cloak units, 300 for Transport Cloaking), and 0x4000 the hulls
 * (4 Scout, 5 Frigate, 6 Destroyer, 14 Mini-Colony Ship, 15 Colony Ship, 27 / 28 Mini / Super Mine
 * Layer; from 0x20 up the starbase hulls of 2:0x0000). The six bytes at +2 of a record are its tech
 * requirements. A design's word +0x28 is compared with the stargates' mass limits, so it is the design's
 * mass. route_fleet_from_planet is a placeholder name: that the planet word +0x30 names the planet new
 * fleets are routed to is read from the code (waypoint task 8), not confirmed. The old fn_16_<off>
 * names are kept as aliases in the comment above each function.
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n + 2 * pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. Locals whose address goes to a callee
 * live at their original BP offsets (BP_LOCAL); the other locals are ordinary C variables. Callees
 * in other segments are not decompiled yet: CALL_ORIG. Near calls inside the segment are
 * PUSH CS; CALL near, i.e. far calls with a 4-byte return address; a callee that mirrors a frame is
 * reached through rt_call_far. Memory is read and written in 16-bit pieces: an offset is a uint16_t
 * and wraps like the original's. DS = SS = DGROUP at every call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/fp80.h"
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
/* A pointer to a DGROUP offset, as the far pointer the APIs take. */
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }
/* A pointer to the stack slot at BP + off (a local of the running function's frame). */
static inline uint32_t bpfp(int16_t off) { return MK_FP(rt_ss, (uint16_t)(rt_bp + off)); }

/* The text buffer the name dialogs and ZIPORDERDLG format into. */
#define TEXTBUF 0x5866

/* strlen of the NUL-terminated string at DS:off (REPNE SCASB with CX = 0xFFFF; NOT CX; DEC CX). */
static uint16_t ds_strlen(uint16_t off) {
  uint16_t n = 0;
  while (drd8((uint16_t)(off + n)) != 0) n++;
  return n;
}
/* strcpy within DGROUP, NUL included (REPNE SCASB for the length, then REP MOVSW; ADC CX,CX; REP MOVSB). */
static void ds_strcpy(uint16_t dst, uint16_t src) {
  uint16_t n = (uint16_t)(ds_strlen(src) + 1);
  for (uint16_t k = 0; k < n; k++) dwr8((uint16_t)(dst + k), drd8((uint16_t)(src + k)));
}
/* Copy the string at DS:src to DS:dst doubling every '&' (a menu/label accelerator escape) and
 * return the offset after the last character written; the caller writes the NUL. */
static uint16_t ds_amp_copy(uint16_t dst, uint16_t src) {
  while (drd8(src) != 0) {
    uint8_t c = drd8(src++);
    dwr8(dst++, c);
    if (c == '&') dwr8(dst++, c);
  }
  return dst;
}
/* _wsprintf(DS:TEXTBUF, format string number fmt_id of the string table 8:0x8df4, one word value).
 * The original pushes the word first, then the id for 8:0x8df4; the word stays on the stack and is the
 * variable argument of _wsprintf. */
static void ds_wsprintf_id(uint16_t fmt_id, uint16_t value) {
  uint16_t sp0 = rt_sp;
  wr16(rt_ss, (uint16_t)(sp0 - 2), value);
  rt_sp = (uint16_t)(sp0 - 2);
  uint16_t fmt = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, fmt_id);
  (void)_wsprintf(dsfp(TEXTBUF), dsfp(fmt), MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
  rt_sp = sp0;
}
/* WM_ERASEBKGND of the three dialogs: fill the client rectangle (the RECT at BP + rc) with the
 * background brush DS:0x10. */
static uint16_t dlg_erase(uint16_t hwnd, uint16_t hdc, int16_t rc) {
  GetClientRect(hwnd, bpfp(rc));
  (void)FillRect(hdc, bpfp(rc), drd16(0x0010));
  return 1;
}

/* zip_enable_preset_buttons: 16:0x0606 (Ghidra FUN_1078_0606, alias fn_16_0606), 56 bytes.
 * PUSH BP; MOV BP,SP; PUSH DI; PUSH SI.
 * FAR CDECL (hwnd [BP+6], preset [BP+8]). Enables both preset buttons (ids 0x817 and 0x41b) of the
 * dialog when the "custom" flag byte of the preset (DS:0x545f + 0x18 * preset) is non-zero, else
 * disables them. AX is the result of the last EnableWindow (the previous disabled state). */
DECOMP(16, 0x0606, zip_enable_preset_buttons, FAR_CDECL)
uint16_t zip_enable_preset_buttons(uint16_t hwnd, uint16_t preset)
{
  FRAME_ENTER(0 + 4);
  uint16_t on = drd8((uint16_t)(0x545f + (uint16_t)(preset * 0x18)));
  (void)EnableWindow(GetDlgItem(hwnd, 0x817), on);
  uint16_t ret = EnableWindow(GetDlgItem(hwnd, 0x41b), on);
  FRAME_LEAVE();
  return ret;
}

/* The ZIP order dialog (16:0x0000) keeps four presets in DGROUP, stride 0x18 from DS:0x5448: five
 * words (the high nibble of a word is a string number, the low 12 bits a count), a 13-byte name at
 * +0x0a (DS:0x5452 + 0x18 * i) and a flag byte at +0x17 (DS:0x545f + 0x18 * i): non-zero for a preset
 * with its own name and rows. DS:0x51b2 is the selected preset. */

/* WM_PAINT: frame the area from the first to the last radio button, print the heading, and below it
 * either the five rows of the selected custom preset or a one-line text. Uses the locals of the
 * dialog procedure's frame: PAINTSTRUCT BP-0x3c, RECT BP-0x1c, RECT BP-0x14, POINT BP-0x18. */
static void zip_paint(uint16_t hwnd)
{
  uint16_t hdc = BeginPaint(hwnd, bpfp(-0x3c));
  GetClientRect(hwnd, bpfp(-0x1c));
  GetWindowRect(GetDlgItem(hwnd, 0x431), bpfp(-0x14));
  ScreenToClient(hwnd, bpfp(-0x14));
  GetWindowRect(GetDlgItem(hwnd, 0x434), bpfp(-0x1c));
  ScreenToClient(hwnd, bpfp(-0x18));
  /* the union of the two controls: left/top of the first, right/bottom of the last */
  BP_LOCAL(uint16_t, -0x10) = BP_LOCAL(uint16_t, -0x18);
  BP_LOCAL(uint16_t, -0x0e) = BP_LOCAL(uint16_t, -0x16);
  uint16_t line = drd16(0x267a);
  (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, (uint16_t)(rt_bp - 0x14), line, (uint16_t)((int16_t)line >> 1));
  (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, (uint16_t)(rt_bp - 0x14), 0xffff);
  (void)SelectObject(hdc, drd16(0x2930));
  (void)SetBkColor(hdc, drd32(0x253a));
  uint16_t n = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x04b0, TEXTBUF);
  uint16_t left = BP_LOCAL(uint16_t, -0x14), top = BP_LOCAL(uint16_t, -0x12);
  (void)TextOut(hdc, (int16_t)(left + 8), (int16_t)(top - (uint16_t)((int16_t)line >> 1)), dsfp(TEXTBUF), (int16_t)n);
  uint16_t y = (uint16_t)(BP_LOCAL(uint16_t, -0x0e) + 8); /* the row below the frame */
  uint16_t sel = (uint16_t)(drd16(0x51b2) * 0x18);
  if (drd8((uint16_t)(0x545f + sel)) != 0) {
    uint16_t w = (uint16_t)((uint16_t)GetTextExtent(hdc, dsfp(drd16(0x0468)), 9) + 8);
    for (uint16_t k = 0; k < 5; k++) {
      (void)SetTextColor(hdc, drd32((uint16_t)(0x0418 + 4 * k)));
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, w, y, drd16((uint16_t)(0x0464 + 2 * k)), 0, 0);
      (void)SetTextColor(hdc, 0);
      uint16_t word = drd16((uint16_t)(0x5448 + 2 * (uint16_t)(12 * drd16(0x51b2) + k)));
      uint16_t len = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)((word >> 12) + 0x6d), TEXTBUF);
      if (drd8((uint16_t)(TEXTBUF - 1 + len)) == '.') {
        /* a string that ends in '.' gets the count (low 12 bits) appended: _wsprintf(buf + len - 3, DS:0x18b0, count) */
        wr16(rt_ss, (uint16_t)(rt_sp - 2), (uint16_t)(word & 0x0fff));
        (void)_wsprintf(dsfp((uint16_t)(0x5863 + len)), dsfp(0x18b0), MK_FP(rt_ss, (uint16_t)(rt_sp - 2)));
      }
      (void)TextOut(hdc, (int16_t)(w + 6), (int16_t)y, dsfp(TEXTBUF), (int16_t)ds_strlen(TEXTBUF));
      y = (uint16_t)(y + line);
    }
  } else {
    uint16_t len = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x04b7, TEXTBUF);
    (void)TextOut(hdc, 0x0c, (int16_t)y, dsfp(TEXTBUF), (int16_t)len);
  }
  (void)EndPaint(hwnd, bpfp(-0x3c));
}

/* WM_INITDIALOG: title, hide control 0x417, select preset 0, fill the four radio buttons with the
 * preset names (a name with '&' doubled, else the generated "Preset n" text) and place the dialog. */
static void zip_init(uint16_t hwnd)
{
  (void)SetWindowText(hwnd, dsfp((uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x022b)));
  (void)ShowWindow(GetDlgItem(hwnd, 0x417), 0);
  dwr16(0x18ae, hwnd);
  (void)CheckRadioButton(hwnd, 0x431, 0x434, 0x431);
  (void)rt_call_far(MK_FP(SEG_SEL(16), 0x0606), FAR_CDECL, (const uint16_t[]){hwnd, 0}, 2);
  dwr16(0x51b2, 0);
  uint16_t name = 0x5452;
  for (uint16_t id = 0x431; (int16_t)id <= 0x434; id++) {
    if (drd8((uint16_t)(name + 0x0d)) != 0) {
      uint16_t end = ds_amp_copy(TEXTBUF, name);
      dwr8(end, 0);
    } else {
      /* no name yet: _wsprintf(buf, string 0x4ad, id - 0x430) */
      ds_wsprintf_id(0x04ad, (uint16_t)(id - 0x430));
    }
    (void)SetWindowText(GetDlgItem(hwnd, id), dsfp(TEXTBUF));
    name = (uint16_t)(name + 0x18);
  }
  (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x18aa, 1);
}

/* Rename the selected preset: the name edit dialog (resource 0x7e3, RENAMEZIPDLG) starts with the
 * current name in the text buffer; when it ends with a result the new name is stored (a custom preset
 * keeps the ten bytes of the current map object), the button text is refreshed and the preset's
 * buttons are updated. */
static void zip_rename(uint16_t hwnd, uint16_t cmd)
{
  uint16_t sel = (uint16_t)(drd16(0x51b2) * 0x18);
  if (drd8((uint16_t)(0x545f + sel)) != 0) {
    ds_strcpy(TEXTBUF, (uint16_t)(0x5452 + sel));
  } else {
    ds_wsprintf_id(0x04ae, drd16(0x51b2));
  }
  uint32_t proc = MakeProcInstance(MK_FP(SEG_SEL(16), 0x063e), drd16(0x54f4));
  if (DialogBox(drd16(0x54f4), 0x000007e3, drd16(0x2806), proc) != 0) {
    if (drd8(TEXTBUF) == 0) ds_wsprintf_id(0x04ae, drd16(0x51b2));
    sel = (uint16_t)(drd16(0x51b2) * 0x18);
    ds_strcpy((uint16_t)(0x5452 + sel), TEXTBUF);
    /* the label: the name with '&' doubled, in the second buffer */
    uint16_t label = 0x58a6;
    label = ds_amp_copy(label, TEXTBUF);
    dwr8(label, 0);
    (void)SetWindowText(GetDlgItem(hwnd, (uint16_t)(drd16(0x51b2) + 0x431)), dsfp(0x58a6));
    if (cmd == 0x816) {
      uint32_t src = drd32(0x4bf0);
      uint16_t from = (uint16_t)(FP_OFF(src) + (uint16_t)(drd16(0x4b7a) * 0x12) + 0x0c);
      sel = (uint16_t)(drd16(0x51b2) * 0x18);
      dwr8((uint16_t)(0x545f + sel), 1);
      for (uint16_t k = 0; k < 10; k++) dwr8((uint16_t)(0x5448 + sel + k), rd8(FP_SEG(src), (uint16_t)(from + k)));
      (void)InvalidateRect(hwnd, 0, 1);
    }
    (void)rt_call_far(MK_FP(SEG_SEL(16), 0x0606), FAR_CDECL, (const uint16_t[]){hwnd, drd16(0x51b2)}, 2);
  }
  FreeProcInstance(proc);
  (void)SetFocus(hwnd);
}

/* 16:0x0000 ZIPORDERDLG (Ghidra ZIPORDERDLG), 1542 bytes. ENTER 0x3e,0; PUSH DI; PUSH SI.
 * FAR PASCAL dialog procedure (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6..8]).
 * Handles WM_PAINT, WM_ERASEBKGND, WM_CTLCOLOR (the four radio buttons get the dialog colour),
 * WM_INITDIALOG and WM_COMMAND (select a preset, OK/Cancel, reset (0x817), rename (0x816, 0x41b),
 * help (0x76)). Returns 1 for the messages it handles itself, else 0 (WM_CTLCOLOR returns the brush). */
DECOMP(16, 0x0000, ZIPORDERDLG, FAR_PASCAL)
uint16_t ZIPORDERDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x3e + 4);
  uint16_t ret = 0;
  switch (msg) {
  case 0x000f: /* WM_PAINT */
    zip_paint(hwnd);
    ret = 1;
    break;
  case 0x0014: /* WM_ERASEBKGND */
    ret = dlg_erase(hwnd, wParam, -0x1c);
    break;
  case 0x0019: { /* WM_CTLCOLOR */
    uint16_t id = 0x431;
    do {
      if (GetDlgItem(hwnd, id) == (uint16_t)lParam) break;
      id++;
    } while ((int16_t)id <= 0x434);
    if ((int16_t)id <= 0x434) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
    break;
  }
  case 0x0110: /* WM_INITDIALOG */
    zip_init(hwnd);
    if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    ret = 1;
    break;
  case 0x0111: { /* WM_COMMAND */
    uint16_t id = wParam;
    if ((uint16_t)(lParam >> 16) == 0 && id >= 0x431 && id <= 0x434) {
      /* a radio button: select the preset */
      dwr16(0x51b2, (uint16_t)(id - 0x431));
      (void)rt_call_far(MK_FP(SEG_SEL(16), 0x0606), FAR_CDECL, (const uint16_t[]){hwnd, (uint16_t)(id - 0x431)}, 2);
      (void)InvalidateRect(hwnd, 0, 1);
    } else if (id == 1 || id == 2) {
      dwr16(0x18ae, 0);
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x18aa, 0);
      (void)EndDialog(hwnd, id == 1 ? 1 : 0);
      if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
      ret = 1;
    } else if (id == 0x816 || id == 0x41b) {
      zip_rename(hwnd, id);
      dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x10));
    } else if (id == 0x817) {
      /* reset: the preset loses its own name and rows */
      dwr8((uint16_t)(0x545f + (uint16_t)(drd16(0x51b2) * 0x18)), 0);
      ds_wsprintf_id(0x04ad, (uint16_t)(drd16(0x51b2) + 1));
      (void)SetWindowText(GetDlgItem(hwnd, (uint16_t)(drd16(0x51b2) + 0x431)), dsfp(TEXTBUF));
      (void)InvalidateRect(hwnd, 0, 1);
      dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x10));
    } else if (id == 0x76) {
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x0000044a);
      ret = 1;
    }
    break;
  }
  default:
    break;
  }
  FRAME_LEAVE();
  return ret;
}

/* 16:0x063e RENAMEZIPDLG (Ghidra RENAMEZIPDLG), 342 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * FAR PASCAL dialog procedure of the preset-name dialog (resource 0x7e3, edit control 0x10c, limit 12
 * characters). WM_INITDIALOG puts the title (string 0x4b1, or 0x4b2 while DS:0x18ae is 0) and the
 * name in DS:0x5866 in, and places the dialog; OK reads the name back into DS:0x5866. */
DECOMP(16, 0x063e, RENAMEZIPDLG, FAR_PASCAL)
uint16_t RENAMEZIPDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(8 + 4);
  uint16_t ret = 0;
  if (msg == 0x0014) { /* WM_ERASEBKGND */
    ret = dlg_erase(hwnd, wParam, -8);
  } else if (msg == 0x0019) { /* WM_CTLCOLOR */
    if ((uint16_t)(lParam >> 16) == 6) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
  } else if (msg == 0x0110) { /* WM_INITDIALOG */
    uint16_t title = (uint16_t)((drd16(0x18ae) == 0 ? 1 : 0) + 0x4b1);
    (void)SetWindowText(hwnd, dsfp((uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, title)));
    (void)SetWindowPos(hwnd, 0, (int16_t)(drd16(0x08a8) + 0x46), (int16_t)(drd16(0x08aa) + 0x46), 0, 0, 0x15);
    (void)SendDlgItemMessage(hwnd, 0x010c, 0x0415, 0x000c, 0);
    (void)SetWindowText(GetDlgItem(hwnd, 0x010c), dsfp(TEXTBUF));
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x08a8, 1);
    ret = 1;
  } else if (msg == 0x0111) { /* WM_COMMAND */
    uint16_t id = wParam;
    if (id == 1 || id == 2) {
      if (id == 1) (void)GetDlgItemText(hwnd, 0x010c, dsfp(TEXTBUF), 0x0e);
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x08a8, 0);
      (void)EndDialog(hwnd, id == 1 ? 1 : 0);
      ret = 1;
    } else if (id == 0x76) {
      uint32_t topic = drd16(0x18ae) == 0 ? 0x0452 : 0x0c1f;
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, topic);
      ret = 1;
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* 16:0x0794 RENAMEDLG (Ghidra RENAMEDLG), 461 bytes. ENTER 0xc,0; PUSH SI.
 * FAR PASCAL dialog procedure of the second name dialog (edit control 0x10c, limit 31 characters, the
 * title is string 0x2cc). While the user types (EN_UPDATE, 0x400) a name that is wider than 0xa0
 * pixels is cut (8:0xa42e) and put back with the selection kept; OK stores the name in DS:0x5866 cut
 * to that width. DS:0xc4a guards the control against re-entry from its own SetWindowText. */
DECOMP(16, 0x0794, RENAMEDLG, FAR_PASCAL)
uint16_t RENAMEDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0xc + 2);
  uint16_t ret = 0;
  if (msg == 0x0014) { /* WM_ERASEBKGND */
    ret = dlg_erase(hwnd, wParam, -0xc);
  } else if (msg == 0x0019) { /* WM_CTLCOLOR */
    if ((uint16_t)(lParam >> 16) == 6) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
  } else if (msg == 0x0110) { /* WM_INITDIALOG */
    (void)SetWindowText(hwnd, dsfp((uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x02cc)));
    (void)SetWindowPos(hwnd, 0, (int16_t)(drd16(0x08a8) + 0x46), (int16_t)(drd16(0x08aa) + 0x46), 0, 0, 0x15);
    (void)SendDlgItemMessage(hwnd, 0x010c, 0x0415, 0x001f, 0);
    (void)SetWindowText(GetDlgItem(hwnd, 0x010c), dsfp(TEXTBUF));
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x08a8, 1);
    ret = 1;
  } else if (msg == 0x0111) { /* WM_COMMAND */
    uint16_t id = wParam;
    uint16_t ctl = (uint16_t)lParam, notify = (uint16_t)(lParam >> 16);
    if (id == 1 || id == 2) {
      if (id == 1) {
        (void)GetDlgItemText(hwnd, 0x010c, dsfp(TEXTBUF), 0x20);
        (void)CALL_ORIG(8, 0xa42e, FAR_CDECL, 3, TEXTBUF, MEM_SEL_DGROUP, 0x00a0);
      }
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x08a8, 0);
      (void)EndDialog(hwnd, id == 1 ? 1 : 0);
      ret = 1;
    } else if (id == 0x010c && notify == 0x0400 && drd16(0x0c4a) == 0) {
      dwr16(0x0c4a, 1);
      (void)GetWindowText(ctl, dsfp(TEXTBUF), 0x00fa);
      uint32_t sel = SendMessage(ctl, 0x0400, 0, 0); /* EM_GETSEL */
      if ((uint16_t)CALL_ORIG(8, 0xa42e, FAR_CDECL, 3, TEXTBUF, MEM_SEL_DGROUP, 0x00a0) == 0) {
        (void)SetWindowText(ctl, dsfp(TEXTBUF));
        (void)SendMessage(ctl, 0x0401, 0, sel); /* EM_SETSEL */
      }
      dwr16(0x0c4a, 0);
    } else if (id == 0x76) {
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x00000447);
      ret = 1;
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* The ship design table of a fleet's owner: the far pointer DS:(0xBE + 4 * owner) to 16 designs of
 * 0x8d bytes (struct ShipDesign). Returns it as a far pointer. */
static uint32_t owner_designs(uint16_t owner) {
  uint16_t slot = (uint16_t)(owner << 2);
  return MK_FP(drd16((uint16_t)(slot + 0xc0)), drd16((uint16_t)(slot + 0xbe)));
}

/* Does the fleet hold a ship (count at fleet+0x0c + 2 * i non-zero) whose design i has a hull number
 * (the byte at design +0) with bit `1 << hull` in `mask`? The original shifts AX by CL, and the
 * 386 takes the count modulo 32, so a hull number of 46 acts like 14. */
static uint16_t fleet_has_hull_in(uint32_t fleet, uint16_t mask)
{
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
  for (uint16_t i = 0; i < 16; i++) {
    if (rd16(seg, (uint16_t)(off + 0x0c + 2 * i)) != 0) {
      uint8_t hull = rd8(FP_SEG(designs), (uint16_t)(FP_OFF(designs) + 0x8d * i));
      if (((uint16_t)(1u << (hull & 31)) & mask) != 0) return 1;
    }
  }
  return 0;
}

/* fleet_has_colony_hull: 16:0x16f6 (Ghidra FUN_1078_16f6, alias fn_16_16f6), 102 bytes. ENTER 8,0;
 * PUSH DI; PUSH SI; a leaf.
 * FAR CDECL (fleet [BP+6]): 1 when the fleet holds a ship of a design whose hull number is 14 or 15
 * (mod 32; the test is TEST AH,0xC0 on 1 << hull), else 0. AX only. Hulls 14 and 15 are the Mini-Colony
 * Ship and the Colony Ship in segment 2's hull table. */
DECOMP(16, 0x16f6, fleet_has_colony_hull, FAR_CDECL)
uint16_t fleet_has_colony_hull(uint32_t fleet)
{
  return fleet_has_hull_in(fleet, 0xc000);
}

/* fleet_has_scout_frigate_destroyer: 16:0x175c (Ghidra FUN_1078_175c, alias fn_16_175c), 102 bytes.
 * The same with TEST AL,0x70: hull numbers 4, 5 and 6 (Scout, Frigate and Destroyer). */
DECOMP(16, 0x175c, fleet_has_scout_frigate_destroyer, FAR_CDECL)
uint16_t fleet_has_scout_frigate_destroyer(uint32_t fleet)
{
  return fleet_has_hull_in(fleet, 0x0070);
}

/* freeze_waypoints_aimed_at_fleet: 16:0x133e (Ghidra FUN_1078_133e, alias fn_16_133e), 186 bytes.
 * ENTER 0x12,0; PUSH DI; PUSH SI; a leaf.
 * FAR CDECL (fleet [BP+6]), no result. Every other fleet of the table (far pointer DS:0xBA, count
 * DS:0x553A, a null entry ends the walk) of a different owner, with more than one waypoint, has its
 * waypoints 1 and up looked at: one of type 2 (low nibble of +7: "follow a fleet") aimed at this
 * fleet's id (word +4) gets bit 0x20 in +7 and this fleet's position (the dword at fleet +8) in +0.
 * update_fleet_waypoints (16:0x1060) leaves the position of a waypoint with bit 0x20 alone, hence the
 * "freeze" of the name. */
DECOMP(16, 0x133e, freeze_waypoints_aimed_at_fleet, FAR_CDECL)
void freeze_waypoints_aimed_at_fleet(uint32_t fleet)
{
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint16_t owner = rd16(seg, (uint16_t)(off + 2)), id = rd16(seg, off);
  for (uint16_t n = 0; (int16_t)n < (int16_t)drd16(0x553a); n++) {
    uint16_t at = (uint16_t)(drd16(0x00ba) + 4 * n);
    uint32_t other = MK_FP(rd16(drd16(0x00bc), (uint16_t)(at + 2)), rd16(drd16(0x00bc), at));
    if (other == 0) return;
    uint16_t fs = FP_SEG(other), fo = FP_OFF(other);
    if (rd16(fs, (uint16_t)(fo + 2)) == owner || (int16_t)rd16(fs, (uint16_t)(fo + 0x62)) <= 1) continue;
    uint16_t ws = rd16(fs, (uint16_t)(fo + 0x66));
    uint16_t wo = (uint16_t)(rd16(fs, (uint16_t)(fo + 0x64)) + 0x16);  /* waypoint 1 */
    uint16_t k = 1;
    do {
      if ((rd8(ws, (uint16_t)(wo + 7)) & 0x0f) == 2 && rd16(ws, (uint16_t)(wo + 4)) == id) {
        wr8(ws, (uint16_t)(wo + 7), (uint8_t)(rd8(ws, (uint16_t)(wo + 7)) | 0x20));
        wr32(ws, wo, rd32(seg, (uint16_t)(off + 8)));
      }
      wo = (uint16_t)(wo + 0x12);
      k++;
    } while ((int16_t)rd16(fs, (uint16_t)(fo + 0x62)) > (int16_t)k);
  }
}

/* A component table entry: 2:0x50f8(&slot) with the 4-byte slot (category word, item byte, count
 * byte) at BP+slot_bp, whose frame the caller mirrors, stores the far pointer to the table entry at
 * BP+slot_bp+4 (the word at entry +0x34 is the part's cost, +0x36 its weight, and so on). Returns
 * that far pointer. A slot the table does not know leaves the old stack contents there; the
 * callers only pass slots of known categories. */
static uint32_t part_entry(int16_t slot_bp)
{
  (void)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, (const uint16_t[]){(uint16_t)(rt_bp + slot_bp)}, 1);
  return BP_LOCAL(uint32_t, slot_bp + 4);
}

/* slot_cloak_units: 16:0x20c8 (Ghidra FUN_1078_20c8, alias fn_16_20c8), 244 bytes. ENTER 0xa,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (slot [BP+6], a 4-byte design slot: category word, item byte at +2, count byte at +3).
 * The cloak units of the slot: 0 when the count is 0; otherwise a fixed number by (category, item) (for
 * category 0x800 and items 0 to 4, the cloaks and the Multi Function Pod, the word at +0x34 of the
 * component table entry 2:0x50f8 gives), multiplied by the count when the count is above 1. The fixed
 * ones are, in segment 2's names: Enigma Pulsar 20, Chameleon Scanner 40, Shadow Shield 70, Langston
 * Shell 20, Depleted Neutronium 50, Mega Poly Shell 40, Multi Contained Munition 20, Alien Miner 60,
 * Orbital Adjuster 50, Multi Cargo Pod 20. AX only. */
DECOMP(16, 0x20c8, slot_cloak_units, FAR_CDECL)
uint16_t slot_cloak_units(uint32_t slot)
{
  FRAME_ENTER(0x0a + 4);
  uint16_t seg = FP_SEG(slot), off = FP_OFF(slot);
  uint8_t item = rd8(seg, (uint16_t)(off + 2)), count = rd8(seg, (uint16_t)(off + 3));
  uint16_t value = 0;
  if (count == 0) {
    FRAME_LEAVE();
    return 0;
  }
  switch (rd16(seg, off)) {
  case 0x0001: if (item == 8) value = 0x14; break;
  case 0x0002: if (item == 6) value = 0x28; break;
  case 0x0004: value = item == 4 ? 0x46 : item == 6 ? 0x14 : 0; break;
  case 0x0008: value = item == 7 ? 0x32 : item == 9 ? 0x28 : 0; break;
  case 0x0010: if (item == 0x12) value = 0x14; break;
  case 0x0080: value = item == 6 ? 0x3c : item == 7 ? 0x32 : 0; break;
  case 0x0800:
    if (item <= 4) {
      BP_LOCAL(uint32_t, -0x0a) = rd32(seg, off);
      uint32_t entry = part_entry(-0x0a);
      value = rd16(FP_SEG(entry), (uint16_t)(FP_OFF(entry) + 0x34));
    }
    break;
  case 0x1000: if (item == 4) value = 0x14; break;
  default: break;
  }
  if (count > 1) value = (uint16_t)(count * value);
  FRAME_LEAVE();
  return value;
}

/* design_beam_rating: 16:0x1d1c (Ghidra FUN_1078_1d1c, alias fn_16_1d1c), 230 bytes. ENTER 0x1e,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (design [BP+6]), DX:AX. A design's beam weapon number: for every slot of category 0x10
 * (beam weapons) the count times the entry's +0x36 (power) times a number t squared, with t = 4 for an
 * entry with bit 1 of +0x3a (the gatling guns), nothing for one with bit 0 (and not bit 1; the
 * sappers), else the entry's +0x34 (range), plus one when the hull word of the design is 0x20 or more
 * (signed; the starbase hulls). 0 when the sum is not positive. */
DECOMP(16, 0x1d1c, design_beam_rating, FAR_CDECL)
uint32_t design_beam_rating(uint32_t design)
{
  FRAME_ENTER(0x1e + 4);
  uint16_t seg = FP_SEG(design), off = FP_OFF(design);
  int16_t big = (int16_t)rd16(seg, off) >= 0x20 ? 1 : 0;
  uint16_t nslots = rd8(seg, (uint16_t)(off + 0x7a));
  uint16_t slot = (uint16_t)(off + 0x3a);
  uint32_t total = 0;
  for (uint16_t k = 0; (int16_t)nslots > 0 && k < nslots; k++, slot = (uint16_t)(slot + 4)) {
    if (rd16(seg, slot) != 0x10) continue;
    BP_LOCAL(uint32_t, -0x1a) = rd32(seg, slot);
    uint32_t entry = part_entry(-0x1a);
    uint16_t es = FP_SEG(entry), eo = FP_OFF(entry);
    uint32_t t;
    if ((rd8(es, (uint16_t)(eo + 0x3a)) & 2) != 0) {
      t = 4;
    } else {
      if ((rd8(es, (uint16_t)(eo + 0x3a)) & 1) != 0) continue;
      t = (uint32_t)(int32_t)(int16_t)rd16(es, (uint16_t)(eo + 0x34));
    }
    if (big) t++;
    uint32_t w = (uint32_t)(int32_t)(int16_t)rd16(es, (uint16_t)(eo + 0x36));
    total += (uint32_t)rd8(seg, (uint16_t)(slot + 3)) * w * t * t;
  }
  FRAME_LEAVE();
  return (int32_t)total > 0 ? total : 0;
}

/* fleet_beam_rating: 16:0x1ca2 (Ghidra FUN_1078_1ca2, alias fn_16_1ca2), 122 bytes. ENTER 0xe,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6]), DX:AX. The fleet's total beam weapon number: the sum over the designs of the
 * owner of (ship count, a signed word at fleet +0x0c + 2 * i, when above 0) times design_beam_rating
 * (16:0x1d1c) of the design; 0 when the sum is not positive. */
DECOMP(16, 0x1ca2, fleet_beam_rating, FAR_CDECL)
uint32_t fleet_beam_rating(uint32_t fleet)
{
  FRAME_ENTER(0x0e + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint32_t total = 0;
  for (uint16_t i = 0; (int16_t)(0x8d * i) < 0x8d0; i++) {
    int16_t count = (int16_t)rd16(seg, (uint16_t)(off + 0x0c + 2 * i));
    if (count > 0) {
      uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
      uint32_t d = MK_FP(FP_SEG(designs), (uint16_t)(FP_OFF(designs) + 0x8d * i));
      uint32_t v = rt_call_far(MK_FP(SEG_SEL(16), 0x1d1c), FAR_CDECL, (const uint16_t[]){FP_OFF(d), FP_SEG(d)}, 2);
      total += (uint32_t)(int32_t)count * v;
    }
  }
  FRAME_LEAVE();
  return (int32_t)total > 0 ? total : 0;
}

/* fleet_mining_rate: 16:0x193e (Ghidra FUN_1078_193e, alias fn_16_193e), 246 bytes. ENTER 0x22,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6]), DX:AX. The fleet's mining rate: over the designs of the owner with a
 * ship count above 0, count times the sum over the slots of category 0x80 and item 0 to 6 (the mining
 * robots) of (the entry's +0x34 word, the robot's rate, times the slot count); at most 0xfa0. */
DECOMP(16, 0x193e, fleet_mining_rate, FAR_CDECL)
uint32_t fleet_mining_rate(uint32_t fleet)
{
  FRAME_ENTER(0x22 + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint32_t total = 0;
  uint16_t di = 0;
  do {
    int16_t count = (int16_t)rd16(seg, (uint16_t)(off + 0x0c + 2 * (di / 0x8d)));
    if (count > 0) {
      uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
      uint16_t dseg = FP_SEG(designs), dbase = (uint16_t)(FP_OFF(designs) + di);
      uint16_t nslots = rd8(dseg, (uint16_t)(dbase + 0x7a));
      uint16_t slot = (uint16_t)(dbase + 0x3a);
      uint32_t sum = 0;
      for (uint16_t k = 0; k < nslots; k++, slot = (uint16_t)(slot + 4)) {
        if (rd16(dseg, slot) == 0x80 && rd8(dseg, (uint16_t)(slot + 2)) <= 6) {
          BP_LOCAL(uint32_t, -0x22) = rd32(dseg, slot);
          uint32_t entry = part_entry(-0x22);
          uint32_t w = (uint32_t)(int32_t)(int16_t)rd16(FP_SEG(entry), (uint16_t)(FP_OFF(entry) + 0x34));
          sum += w * rd8(dseg, (uint16_t)(slot + 3));
        }
      }
      total += (uint32_t)(int32_t)count * sum;
    }
    di = (uint16_t)(di + 0x8d);
  } while ((int16_t)di < 0x8d0);
  FRAME_LEAVE();
  return (int32_t)total > 0xfa0 ? 0xfa0 : total;
}

/* fleet_orbital_adjusters: 16:0x1a34 (Ghidra FUN_1078_1a34, alias fn_16_1a34), 182 bytes. ENTER 0x20,0;
 * PUSH DI; PUSH SI; a leaf.
 * FAR CDECL (fleet [BP+6]), DX:AX. The sum over the designs of the owner of (ship count, when above 0)
 * times the sum of the slot counts of the slots with category 0x80 and item 7 (the Orbital Adjuster). */
DECOMP(16, 0x1a34, fleet_orbital_adjusters, FAR_CDECL)
uint32_t fleet_orbital_adjusters(uint32_t fleet)
{
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
  uint16_t dseg = FP_SEG(designs);
  uint32_t total = 0;
  for (uint16_t i = 0; i < 16; i++) {
    int16_t count = (int16_t)rd16(seg, (uint16_t)(off + 0x0c + 2 * i));
    if (count > 0) {
      uint16_t dbase = (uint16_t)(FP_OFF(designs) + 0x8d * i);
      uint16_t nslots = rd8(dseg, (uint16_t)(dbase + 0x7a));
      uint16_t slot = (uint16_t)(dbase + 0x3a);
      uint32_t sum = 0;
      for (uint16_t k = 0; k < nslots; k++, slot = (uint16_t)(slot + 4))
        if (rd16(dseg, slot) == 0x80 && rd8(dseg, (uint16_t)(slot + 2)) == 7) sum += rd8(dseg, (uint16_t)(slot + 3));
      total += (uint32_t)(int32_t)count * sum;
    }
  }
  return total;
}

/* fleet_mine_laying_rate: 16:0x1aea (Ghidra FUN_1078_1aea, alias fn_16_1aea), 440 bytes. ENTER 0x2c,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6], group [BP+0xA], which [BP+0xC]), DX:AX. The fleet's mine-laying rate: the sum
 * over the designs i (all of them when which is -1, else only design `which`) with a ship count above 0
 * of count times the sum over the slots of: category 0x100 (the mine dispensers) with an item in the
 * range of the group (0: 0 to 3 standard, 1: 4 to 6 heavy, 2: 7 to 9 speed trap, else 0 to 9) -> the
 * entry's +0x34 word times the slot count; category 0x10 item 0x12 (the Multi Contained Munition) and
 * group <= 0 -> 4 times the slot count. The sum doubles for a hull word of 0x1b or 0x1c (the Mini and
 * Super Mine Layer). The result is 10 times the total, or 100000000 when the total is above 10000000
 * or negative. */
DECOMP(16, 0x1aea, fleet_mine_laying_rate, FAR_CDECL)
uint32_t fleet_mine_laying_rate(uint32_t fleet, int16_t group, int16_t which)
{
  FRAME_ENTER(0x2c + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  uint16_t lo = group == 0 ? 0 : group == 1 ? 4 : group == 2 ? 7 : 0;
  uint16_t hi = group == 0 ? 3 : group == 1 ? 6 : 9;
  uint32_t total = 0;
  uint16_t dofs = 0;
  for (uint16_t i = 0; (int16_t)dofs < 0x8d0; i++, dofs = (uint16_t)(dofs + 0x8d)) {
    int16_t count = (int16_t)rd16(seg, (uint16_t)(off + 0x0c + 2 * i));
    if (count <= 0 || (which != -1 && which != (int16_t)i)) continue;
    uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
    uint16_t dseg = FP_SEG(designs), dbase = (uint16_t)(FP_OFF(designs) + dofs);
    uint16_t nslots = rd8(dseg, (uint16_t)(dbase + 0x7a));
    uint16_t slot = (uint16_t)(dbase + 0x3a);
    uint32_t sum = 0;
    for (uint16_t k = 0; k < nslots; k++, slot = (uint16_t)(slot + 4)) {
      uint16_t cat = rd16(dseg, slot);
      uint16_t item = rd8(dseg, (uint16_t)(slot + 2));
      if (cat == 0x100 && item >= lo && item <= hi) {
        BP_LOCAL(uint32_t, -0x2a) = rd32(dseg, slot);
        uint32_t entry = part_entry(-0x2a);
        uint32_t w = (uint32_t)(int32_t)(int16_t)rd16(FP_SEG(entry), (uint16_t)(FP_OFF(entry) + 0x34));
        sum += (uint32_t)rd8(dseg, (uint16_t)(slot + 3)) * w;
      } else if (group <= 0 && cat == 0x10 && item == 0x12) {
        sum += (uint32_t)rd8(dseg, (uint16_t)(slot + 3)) * 4;
      }
    }
    uint16_t hull = rd16(dseg, dbase);
    if (hull == 0x1b || hull == 0x1c) sum += sum;
    total += (uint32_t)(int32_t)count * sum;
  }
  FRAME_LEAVE();
  if ((int32_t)total > 10000000 || (int16_t)(total >> 16) < 0) return 100000000;
  return total * 10;
}

/* Fold the six signed bytes at entry +2 of a component table entry into the maxima at DS:0x27d6. */
static void fold_entry_maxima(uint32_t entry)
{
  for (uint16_t k = 0; k < 6; k++) {
    int16_t v = (int8_t)rd8(FP_SEG(entry), (uint16_t)(FP_OFF(entry) + 2 + k));
    uint8_t cur = drd8((uint16_t)(0x27d6 + k));
    dwr8((uint16_t)(0x27d6 + k), v >= (int16_t)cur ? (uint8_t)v : cur);
  }
}

/* tally_design_parts: 16:0x244c (Ghidra FUN_1078_244c, alias fn_16_244c), 442 bytes. ENTER 0x12,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (design [BP+6], player [BP+0xA]), no result. With the current player DS:0x14c set to
 * `player` (restored at the end): looks up the design's hull (category 0x4000, item = the byte at
 * design +0) and each slot with a non-zero count in the component table 2:0x50f8, and keeps the
 * maximum of the six signed bytes at entry +2 (the tech requirements) in the bytes DS:0x27d6 to 0x27db;
 * a slot of ten known (category, item) pairs also adds its count to the byte DS:0x27dc + n (n = 0 to
 * 11; 8 and 10 are never used), which stops growing at 0x19. In segment 2's names, n is 0 Multi Cargo
 * Pod, 1 Multi Function Pod, 2 Langston Shell, 3 Mega Poly Shell, 4 Alien Miner, 5 Hush-a-Boom, 6 Anti
 * Matter Torpedo, 7 Multi Contained Munition, 9 Enigma Pulsar, 11 Jump Gate. The local slot at
 * BP-0x12 is seen by 2:0x50f8: category word, item byte, count byte, and the far pointer to the
 * entry at BP-0xE. */
DECOMP(16, 0x244c, tally_design_parts, FAR_CDECL)
void tally_design_parts(uint32_t design, uint16_t player)
{
  FRAME_ENTER(0x12 + 4);
  uint16_t seg = FP_SEG(design), off = FP_OFF(design);
  uint16_t saved = drd16(0x014c);
  dwr16(0x014c, player);
  BP_LOCAL(uint16_t, -0x12) = 0x4000;
  BP_LOCAL(uint8_t, -0x10) = rd8(seg, off);  /* the count byte at BP-0xF keeps its old contents */
  fold_entry_maxima(part_entry(-0x12));
  uint16_t nslots = rd8(seg, (uint16_t)(off + 0x7a));
  for (uint16_t si = 0; (int16_t)nslots > (int16_t)si; si++) {
    uint16_t slot = (uint16_t)(off + 0x3a + 4 * si);
    if (rd8(seg, (uint16_t)(slot + 3)) == 0) continue;
    BP_LOCAL(uint32_t, -0x12) = rd32(seg, slot);
    fold_entry_maxima(part_entry(-0x12));
    uint16_t cat = BP_LOCAL(uint16_t, -0x12);
    uint8_t item = BP_LOCAL(uint8_t, -0x10);
    int16_t n = -1;
    switch (cat) {
    case 0x0001: if (item == 8) n = 9; break;
    case 0x0004: if (item == 6) n = 2; break;
    case 0x0008: if (item == 9) n = 3; break;
    case 0x0010: if (item == 0x12) n = 7; break;
    case 0x0020: if (item == 7) n = 6; break;
    case 0x0040: if (item == 8) n = 5; break;
    case 0x0080: if (item == 6) n = 4; break;
    case 0x0800: if (item == 4) n = 1; break;
    case 0x1000: n = item == 4 ? 0 : item == 9 ? 0x0b : -1; break;
    default: break;
    }
    if (n != -1 && drd8((uint16_t)(0x27dc + n)) < 0x19) {
      dwr8((uint16_t)(0x27dc + n), (uint8_t)(drd8((uint16_t)(0x27dc + n)) + BP_LOCAL(uint8_t, -0x0f)));
      if (drd8((uint16_t)(0x27dc + n)) > 0x19) dwr8((uint16_t)(0x27dc + n), 0x19);
    }
  }
  dwr16(0x014c, saved);
  FRAME_LEAVE();
}

/* IDIV r/m32 on EDX:EAX = the sign extension of num: the original traps (#DE) on a zero divisor and
 * on INT32_MIN / -1; the C version stops with a message (CONVENTIONS rule b). */
static int32_t idiv32(int32_t num, int32_t den, uint16_t where)
{
  if (den == 0 || (num == INT32_MIN && den == -1)) {
    fprintf(stderr, "16:0x%04x: divide error (IDIV %d by %d)\n", where, num, den);
    abort();
  }
  return num / den;
}
/* The low 32 bits of a signed 32 x 32 product (IMUL r/m32 followed by CDQ throws the high half away). */
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
/* A 32-bit add that wraps (ADD / ADC pairs). */
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

/* stargate_jump_loss: 16:0x0e10 (Ghidra FUN_1078_0e10, alias fn_16_0e10), 591 bytes. ENTER 0x22,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (item1 [BP+6], item2 [BP+8] (bytes), n [BP+0xA], d [BP+0xC], out [BP+0xE] = a near pointer
 * to a word), AX = 0xffff, 0xfffe, 0 or 1. Looks up the category 0x200 component table entries of the
 * two items (2:0x50f8; items 0 to 6 are the stargates) and measures, with v = the word at entry 1 +0x36
 * (the gate's range; -1, "any", means 8000), how much of n (the distance) and d (a design's mass) the
 * entries cover, in units of 1/10000: AX = 0xffff when 5v < n, 0xfffe when 5 * (the word at +0x34, the
 * mass limit, of entry 1 or of entry 2, when positive) < d; 0 with *out = 100 when a computed step is not
 * positive; else 1 with *out = (10000 - the product) / 100, the loss percentage. The slots (category
 * 0x200, item byte; the count byte is whatever the stack held) and the entry pointers live in the
 * frame at BP-0x10/-0xC and BP-0x18/-0x14. */
DECOMP(16, 0x0e10, stargate_jump_loss, FAR_CDECL)
uint16_t stargate_jump_loss(uint16_t item1, uint16_t item2, int16_t n, int16_t d, uint16_t out)
{
  FRAME_ENTER(0x22 + 4);
  BP_LOCAL(uint16_t, -0x18) = 0x0200;
  BP_LOCAL(uint16_t, -0x10) = 0x0200;
  BP_LOCAL(uint8_t, -0x0e) = (uint8_t)item1;
  BP_LOCAL(uint8_t, -0x16) = (uint8_t)item2;
  uint32_t e1 = part_entry(-0x10);
  uint32_t e2 = part_entry(-0x18);
  uint16_t s1 = FP_SEG(e1), o1 = FP_OFF(e1), s2 = FP_SEG(e2), o2 = FP_OFF(e2);
  int32_t ten_k = 10000;
  int32_t v = (int16_t)rd16(s1, (uint16_t)(o1 + 0x36));
  if (v == -1) v = 8000;
  int32_t t = mul32(v, 5);
  uint16_t ret;
  if (t < n) {
    ret = 0xffff;
    goto done;
  }
  int16_t w1 = (int16_t)rd16(s1, (uint16_t)(o1 + 0x34));
  if (w1 > 0 && mul32(w1, 5) < d) {
    ret = 0xfffe;
    goto done;
  }
  int16_t w2 = (int16_t)rd16(s2, (uint16_t)(o2 + 0x34));
  if (w2 > 0 && mul32(w2, 5) < d) {
    ret = 0xfffe;
    goto done;
  }
  if (v < n) {
    ten_k = idiv32(mul32(t - n, 2500), v, 0x0e10);
    if (ten_k <= 0) goto fail;
  }
  for (int16_t pass = 0; pass < 2; pass++) {
    int16_t w = pass == 0 ? w1 : w2;
    if (w < d && w > 0) {
      int32_t q = idiv32(mul32(mul32(w, 5) - d, 2500), w, 0x0e10);
      if (q <= 0) goto fail;
      ten_k = idiv32(mul32(q, ten_k), 10000, 0x0e10);
    }
  }
  dwr16(out, (uint16_t)idiv32((int32_t)(10000u - (uint32_t)ten_k), 100, 0x0e10));
  ret = 1;
  goto done;
fail:
  dwr16(out, 100);
  ret = 0;
done:
  FRAME_LEAVE();
  return ret;
}

/* 6:0x7344 posts a message to a player: owner, kind, two ids (the second with bit 15 set), the planet
 * ids and three words. Ten words, CDECL. */
static void post_message(uint16_t owner, uint16_t kind, uint16_t id, uint16_t planet, uint16_t where,
                         uint16_t w6, uint16_t w7, uint16_t w8, uint16_t w9)
{
  (void)rt_call_far(MK_FP(SEG_SEL(6), 0x7344), FAR_CDECL,
                    (const uint16_t[]){owner, kind, (uint16_t)(id | 0x8000), id, planet, where, w6, w7, w8, w9}, 10);
}

/* apply_stargate_losses: 16:0x0962 (Ghidra FUN_1078_0962, alias fn_16_0962), 1197 bytes. ENTER 0x13e,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6], a [BP+0xA], b [BP+0xC], c [BP+0xE]), AX = 1 or 0. Damages the ships of a
 * fleet that goes through a stargate (the planet named in the messages: by the id of waypoint 1, or found
 * by its position in the table DS:0x31ba). The frame holds the copy F of the fleet at BP-0xC2 (0x7c bytes),
 * the record G of the ships lost (BP-0x13E, 0x7c bytes) and 16 words of loss percentages at BP-0x46, which
 * stargate_jump_loss (16:0x0e10) fills for every design with ships (a and b are the two gates' items, c
 * the distance, the design word +0x28 the mass). Loss 100: all ships go. Else the ships are hit one by
 * one with chances from the random generator 8:0x8730, the damage word (+0x2c) is rebuilt, and the
 * lost ships are summed.
 * The messages 0xe3 (stargate_jump_loss answered 0xffff: too far), 0xe4 (0xfffe: design i too heavy),
 * 0xe7 (nothing left) end it with 0; otherwise
 * when ships were lost one of 0xe8 to 0xeb tells how many, and G and F go to 10:0x6c1c; F replaces the
 * fleet and the result is 1. */
DECOMP(16, 0x0962, apply_stargate_losses, FAR_CDECL)
uint16_t apply_stargate_losses(uint32_t fleet, uint16_t a, uint16_t b, uint16_t c)
{
  FRAME_ENTER(0x13e + 4);
#define FB (-0xc2)
#define GB (-0x13e)
#define LB (-0x46)
#define FLT BP_LOCAL(struct Fleet, FB)
#define LOSS(i) BP_LOCAL(uint16_t, LB + 2 * (i))
  uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  uint16_t cnt = 0;
  int32_t lost = 0, total = 0;
  for (uint16_t i = 0; i < 16; i++) LOSS(i) = 0;
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + FB))), LIN(fleet), 0x7c);
  /* the planet: by the id of waypoint 1, or the table index of its position */
  uint16_t wseg = FP_SEG(FLT.waypoints), woff = FP_OFF(FLT.waypoints);
  uint16_t pid;
  if ((rd8(wseg, (uint16_t)(woff + 0x1d)) & 0x0f) == 1) {
    pid = rd16(wseg, (uint16_t)(woff + 0x1a));
  } else {
    uint32_t xy = rd32(wseg, (uint16_t)(woff + 0x16));
    pid = 0;
    if ((int16_t)drd16(0x007a) > 0) {
      uint16_t at = 0x31ba;
      do {
        if (xy == drd32(at)) break;
        at = (uint16_t)(at + 4);
        pid++;
      } while ((int16_t)pid < (int16_t)drd16(0x007a));
    }
  }
  /* the loss percentage of every design with ships */
  for (uint16_t i = 0; i < 16; i++) {
    uint16_t n = FLT.ship_count[i];
    if (n == 0) continue;
    cnt++;
    total += (int16_t)n;
    uint32_t designs = owner_designs(FLT.owner);
    uint16_t d28 = rd16(FP_SEG(designs), (uint16_t)(FP_OFF(designs) + 0x8d * i + 0x28));
    uint16_t r = (uint16_t)rt_call_far(MK_FP(SEG_SEL(16), 0x0e10), FAR_CDECL,
                                       (const uint16_t[]){a, b, c, d28, (uint16_t)(rt_bp + LB + 2 * i)}, 5);
    if (r == 0xfffe) {
      post_message(FLT.owner, 0xe4, FLT.id, FLT.planet_id, pid, i, 0, 0, 0);
      FRAME_LEAVE();
      return 0;
    }
    if (r == 0xffff) {
      post_message(FLT.owner, 0xe3, FLT.id, FLT.planet_id, pid, 0, 0, 0, 0);
      FRAME_LEAVE();
      return 0;
    }
    if (r == 0) cnt--;
    else if (r == 1) FLT.flags |= 0x40;
  }
  if (cnt != 0) {
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + GB))), 0, 0x7c);
    for (uint16_t i = 0; i < 16; i++) {
      uint16_t ls = LOSS(i);
      if (FLT.ship_count[i] == 0 || ls == 0) continue;
      if (ls == 0x64) {
        lost += (int16_t)FLT.ship_count[i];
        FLT.ship_count[i] = 0;
        FLT.fld_2c[i] = 0;
        cnt--;
      } else {
        int16_t si = (int16_t)FLT.ship_count[i];
        uint32_t designs = owner_designs(rd16(fseg, (uint16_t)(foff + 2)));
        int16_t strength = (int16_t)rd16(FP_SEG(designs), (uint16_t)(FP_OFF(designs) + 0x8d * i + 0x38));
        int16_t attr = (int16_t)(uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2,
                                                    (uint16_t)(0x5a64 + (uint16_t)(rd16(fseg, (uint16_t)(foff + 2)) * 0xc0)), 14);
        uint8_t hit = attr == 7 ? 0 : (uint8_t)((int16_t)ls / 3);
        uint16_t w8 = 0;
        if (FLT.fld_2c[i] != 0)
          w8 = (uint16_t)idiv32((int32_t)(int16_t)(FLT.fld_2c[i] & 0x7f) * si, 100, 0x0962);
        if (FLT.fld_2c[i] != 0 && w8 == 0) w8 = 1;
        if (hit != 0) {
          uint16_t rounds = 0;
          if ((int16_t)FLT.ship_count[i] > 0) {
            int16_t survivors = si;
            si = (int16_t)w8;
            do {
              uint16_t roll = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
              if ((int16_t)roll < (int16_t)(uint16_t)hit) {
                survivors--;
                if (si != 0) {
                  uint16_t roll2 = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x1f4);
                  if ((uint16_t)(FLT.fld_2c[i] >> 7) > roll2) si--;
                }
              }
              rounds++;
            } while ((int16_t)FLT.ship_count[i] > (int16_t)rounds);
            w8 = (uint16_t)si;
            si = survivors;
          }
          lost += (int16_t)(uint16_t)(FLT.ship_count[i] - (uint16_t)si);
        }
        if (si != 0) {
          uint16_t wa = 0, wl;
          if (FLT.fld_2c[i] != 0) {
            wa = (uint16_t)idiv32((int32_t)(uint16_t)(FLT.fld_2c[i] >> 7) * strength, 500, 0x0962);
            if (wa == 0) wa = 1;
          }
          wl = (uint16_t)idiv32((int32_t)(int16_t)ls * strength, 100, 0x0962);
          if (wl == 0) wl = 1;
          if (w8 != 0 && !((int16_t)(uint16_t)(wl + wa) < strength)) {
            lost += (int16_t)w8;
            si = (int16_t)(uint16_t)((uint16_t)si - w8);
          }
          if (si != 0) {
            int32_t sum = add32(mul32((int16_t)wl, si), (int32_t)(int16_t)wa * (int16_t)w8);
            int32_t q1 = idiv32(sum, si, 0x0962);
            uint16_t dmg = (uint16_t)idiv32(mul32(q1, 500), strength, 0x0962);
            if (dmg == 0) dmg = 1;
            uint16_t t = (uint16_t)(dmg << 7);
            uint8_t old = (uint8_t)FLT.fld_2c[i];
            uint16_t nw = (uint16_t)(t ^ (uint16_t)((uint8_t)(old ^ (uint8_t)t) & 0x7f));
            FLT.fld_2c[i] = (uint16_t)((nw & 0xff00) | (uint8_t)(((uint8_t)nw & 0xe4) | 0x64));
          }
        }
        FLT.ship_count[i] = (uint16_t)si;
        if (si == 0) {
          FLT.fld_2c[i] = 0;
          cnt--;
        }
      }
      BP_LOCAL(uint16_t, GB + 0x0c + 2 * i) = (uint16_t)(rd16(fseg, (uint16_t)(foff + 0x0c + 2 * i)) - FLT.ship_count[i]);
    }
  }
  if (cnt == 0) {
    wr8(fseg, (uint16_t)(foff + 5), (uint8_t)(rd8(fseg, (uint16_t)(foff + 5)) | 4));
    post_message(FLT.owner, 0xe7, FLT.id, FLT.planet_id, pid, 0, 0, 0, 0);
    FRAME_LEAVE();
    return 0;
  }
  if (lost != 0) {
    uint16_t owner = rd16(fseg, (uint16_t)(foff + 2)), id = rd16(fseg, foff), planet = rd16(fseg, (uint16_t)(foff + 6));
    if ((uint16_t)((uint32_t)lost >> 16) != 0) {
      post_message(owner, 0xeb, id, planet, pid, (uint16_t)lost, (uint16_t)((uint32_t)lost >> 16), 0, 0);
    } else {
      uint16_t kind = (total >> 2) > lost ? 0xe8 : (total >> 1) < lost ? 0xea : 0xe9;
      post_message(owner, kind, id, planet, pid, (uint16_t)lost, 0, 0, 0);
    }
    BP_LOCAL(uint16_t, GB + 2) = FLT.owner;
    BP_LOCAL(uint8_t, GB + 5) = (uint8_t)(BP_LOCAL(uint8_t, GB + 5) | 4);
    BP_LOCAL(uint8_t, GB + 4) = 7;
    (void)CALL_ORIG(10, 0x6c1c, FAR_CDECL, 2, (uint16_t)(rt_bp + FB), (uint16_t)(rt_bp + GB));
  }
  memcpy(LIN(fleet), LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + FB))), 0x7c);
  FRAME_LEAVE();
  return 1;
#undef FB
#undef GB
#undef LB
#undef FLT
#undef LOSS
}

/* set_mining_waypoint_task: 16:0x17c2 (Ghidra FUN_1078_17c2, alias fn_16_17c2), 379 bytes. ENTER 0x12,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6], planet [BP+0xA]), no result. Sets the task of the fleet's first waypoint (waypoint 0, at
 * the waypoint list +4): unless the planet belongs to somebody (owner word +2 not -1) who is not the
 * fleet's owner or the fleet's owner lacks attribute 14 = 8, and when fleet_mining_rate (16:0x193e) of the
 * fleet is not 0: for an unowned planet it looks through the fleet table (ascending owners) for another live
 * fleet of the same owner at the same position whose fleet_mining_rate is between 1 and 0xfa0 - 1; if found,
 * word +6 of the waypoint becomes (w & 0xf2f4) | 0x0204 (task nibble 4, target type 2: a fleet) and its
 * word +4 the id of that fleet, else (w & 0xfff3) | 3 (task nibble 3). What tasks 3 and 4 are is not
 * confirmed here. Finally the byte at fleet +0x60 is cleared. */
DECOMP(16, 0x17c2, set_mining_waypoint_task, FAR_CDECL)
void set_mining_waypoint_task(uint32_t fleet, uint32_t planet)
{
  FRAME_ENTER(0x12 + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet), pseg = FP_SEG(planet), poff = FP_OFF(planet);
  uint32_t list = rd32(seg, (uint16_t)(off + 0x64));
  uint16_t lseg = FP_SEG(list), wp0 = (uint16_t)(FP_OFF(list) + 4);
  bool found = false;
  uint16_t fseg = 0, foff = 0;
  bool go = true;
  if (rd16(pseg, (uint16_t)(poff + 2)) != 0xffff) {
    uint16_t owner = rd16(seg, (uint16_t)(off + 2));
    int16_t attr = (int16_t)(uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(0x5a64 + (uint16_t)(owner * 0xc0)), 14);
    if (attr != 8 || rd16(pseg, (uint16_t)(poff + 2)) != owner) go = false;
  }
  if (go) {
    uint32_t v = rt_call_far(MK_FP(SEG_SEL(16), 0x193e), FAR_CDECL, (const uint16_t[]){off, seg}, 2);
    if (v == 0) go = false;
  }
  if (go) {
    if (rd16(pseg, (uint16_t)(poff + 2)) == 0xffff && (int16_t)drd16(0x553a) > 0) {
      for (uint16_t n = 0; (int16_t)n < (int16_t)drd16(0x553a); n++) {
        uint16_t at = (uint16_t)(drd16(0x00ba) + 4 * n);
        uint16_t so = rd16(drd16(0x00bc), at), ss = rd16(drd16(0x00bc), (uint16_t)(at + 2));
        if ((ss | so) == 0) break;
        int16_t fo_owner = (int16_t)rd16(ss, (uint16_t)(so + 2)), my_owner = (int16_t)rd16(seg, (uint16_t)(off + 2));
        if (fo_owner < my_owner) continue;
        if ((rd8(ss, (uint16_t)(so + 5)) & 4) != 0) continue;
        if (so == off && ss == seg) continue;
        if (fo_owner > my_owner) break;
        if (rd16(ss, (uint16_t)(so + 8)) != rd16(seg, (uint16_t)(off + 8))) continue;
        if (rd16(ss, (uint16_t)(so + 0x0a)) != rd16(seg, (uint16_t)(off + 0x0a))) continue;
        uint32_t v2 = rt_call_far(MK_FP(SEG_SEL(16), 0x193e), FAR_CDECL, (const uint16_t[]){so, ss}, 2);
        if ((int32_t)v2 <= 0) continue;
        if (v2 < 0xfa0) {
          found = true;
          fseg = ss;
          foff = so;
          break;
        }
      }
    }
    uint16_t w = rd16(lseg, (uint16_t)(wp0 + 6));
    if (found) {
      wr16(lseg, (uint16_t)(wp0 + 6), (uint16_t)((w & 0xf2f4) | 0x0204));
      wr16(lseg, (uint16_t)(wp0 + 4), rd16(fseg, foff));
    } else {
      wr16(lseg, (uint16_t)(wp0 + 6), (uint16_t)((w & 0xfff3) | 0x0003));
    }
  }
  wr8(seg, (uint16_t)(off + 0x60), 0);
  FRAME_LEAVE();
}

/* fleet_cloak_percent: 16:0x1e02 (Ghidra FUN_1078_1e02, alias fn_16_1e02), 709 bytes. ENTER 0x3a,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6]), AX = 0 to 98. The fleet's cloaking (0 to 98, so a percentage): for every design
 * with a ship count above 0, ca = count * the design word +0x28 (the mass; 32-bit, the word zero-extended)
 * and sum = the slot_cloak_units (16:0x20c8) of the slots (+ 300 when the owner's attribute 14 is 1); a = the
 * sum of the ca, b = the sum of sum * ca. When any of
 * sum > 0xfa0, ca > 500000, b > 100000000 or a > 50000000 happens the totals switch to doubles (x87:
 * FILD, FIMUL, FADD, FSTP m64 at every store; Fp80 here, rounding to a double at each store). Then
 * the four cargo dwords at fleet +0x4c are added to a unless the attribute is 1, and the ratio b / a
 * (IDIV, or FDIV and the C runtime's ftol) is mapped piecewise to 0..98 (halved up to 100, then steps
 * of 8, 24, 64 and a last 96 / 97 / 98). 0 when b is 0 or the ratio is negative. */
DECOMP(16, 0x1e02, fleet_cloak_percent, FAR_CDECL)
uint16_t fleet_cloak_percent(uint32_t fleet)
{
  FRAME_ENTER(0x3a + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet);
  int32_t a = 0, b = 0;
  double dbl_a, dbl_b;
  memcpy(&dbl_a, LINN(0x208c), 8);
  memcpy(&dbl_b, LINN(0x208c), 8);
  bool fp = false;
  uint16_t dofs = 0;
  for (uint16_t i = 0; (int16_t)dofs < 0x8d0; i++, dofs = (uint16_t)(dofs + 0x8d)) {
    int16_t count = (int16_t)rd16(seg, (uint16_t)(off + 0x0c + 2 * i));
    if (count <= 0) continue;
    uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
    uint16_t dseg = FP_SEG(designs), dbase = (uint16_t)(FP_OFF(designs) + dofs);
    uint16_t nslots = rd8(dseg, (uint16_t)(dbase + 0x7a));
    int32_t ca = mul32(count, rd16(dseg, (uint16_t)(dbase + 0x28)));
    int32_t sum = 0;
    int16_t attr = (int16_t)(uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2,
                                                (uint16_t)(0x5a64 + (uint16_t)(rd16(seg, (uint16_t)(off + 2)) * 0xc0)), 14);
    if (attr == 1) sum = 300;
    uint16_t slot = (uint16_t)(dbase + 0x3a);
    for (uint16_t k = 0; k < nslots; k++, slot = (uint16_t)(slot + 4))
      sum = add32(sum, (int16_t)(uint16_t)rt_call_far(MK_FP(SEG_SEL(16), 0x20c8), FAR_CDECL, (const uint16_t[]){slot, dseg}, 2));
    if (sum > 0) {
      if (!fp && (sum > 0xfa0 || ca > 500000 || b > 100000000 || a > 50000000)) {
        dbl_b = fp80_to_f64(fp80_from_i32(b));
        dbl_a = fp80_to_f64(fp80_from_i32(a));
        fp = true;
      }
      if (fp)
        dbl_b = fp80_to_f64(fp80_add(fp80_mul(fp80_from_i32(sum), fp80_from_i32(ca)), fp80_from_f64(dbl_b)));
      else
        b = add32(b, mul32(sum, ca));
    }
    if (fp) dbl_a = fp80_to_f64(fp80_add(fp80_from_i32(ca), fp80_from_f64(dbl_a)));
    else a = add32(a, ca);
  }
  if (!fp && b == 0) {
    FRAME_LEAVE();
    return 0;
  }
  int16_t attr = (int16_t)(uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2,
                                              (uint16_t)(0x5a64 + (uint16_t)(rd16(seg, (uint16_t)(off + 2)) * 0xc0)), 14);
  if (attr != 1) {
    if (fp) {
      Fp80 acc = fp80_from_f64(dbl_a);
      for (uint16_t k = 0; k < 4; k++) acc = fp80_add(fp80_from_i32((int32_t)rd32(seg, (uint16_t)(off + 0x4c + 4 * k))), acc);
      dbl_a = fp80_to_f64(acc);
    } else {
      for (uint16_t k = 0; k < 4; k++) a = add32(a, (int32_t)rd32(seg, (uint16_t)(off + 0x4c + 4 * k)));
    }
  }
  int32_t ratio;
  if (fp) ratio = fp80_to_i32_ftol(fp80_div(fp80_from_f64(dbl_b), fp80_from_f64(dbl_a)));
  else ratio = idiv32(b, a, 0x1e02);
  uint16_t ret;
  if ((int16_t)(ratio >> 16) < 0) {
    ret = 0;
  } else {
    int16_t bx = (int16_t)ratio;
    if (bx <= 100) {
      ret = (uint16_t)(bx >> 1);
    } else if ((bx = (int16_t)(bx - 100)) <= 200) {
      ret = (uint16_t)((bx >> 3) + 0x32);
    } else if ((bx = (int16_t)(bx - 200)) <= 0x138) {
      ret = (uint16_t)(bx / 24 + 0x4b);
    } else if ((bx = (int16_t)(bx - 0x138)) <= 0x200) {
      ret = (uint16_t)((bx >> 6) + 0x58);
    } else if (bx >= 0x3e8) {
      ret = 0x62;
    } else {
      ret = (uint16_t)((bx >= 0x300 ? 1 : 0) + 0x60);
    }
  }
  FRAME_LEAVE();
  return ret;
}

/* update_fleet_waypoints: 16:0x1060 (Ghidra FUN_1078_1060, alias fn_16_1060), 733 bytes. ENTER 0xc,0;
 * PUSH DI; PUSH SI.
 * FAR CDECL, no arguments or result. For every fleet of the table (far pointer DS:0xBA, count DS:0x553A, a null
 * entry ends the walk) with more than one waypoint: a fleet with bit 7 of its flags (+5) is handed to 10:0x6230
 * and told off with the message 0x137; for the other, each waypoint k >= 1 of type 2 (follow a fleet: the id
 * is the word at +4) is resolved with 7:0x14a8: a fleet found gives the waypoint its position (unless
 * bit 5 of +7 is set) and f +0x68 is cleared, none found makes the waypoint a plain one ((w & 0xf4ff) | 0x0400 at +6,
 * id 0). When waypoint 1 lies at the fleet's own position it moves up into slot 0 (18 bytes); a following
 * waypoint gets its task fixed from the fleet's planet; 10:0x6230 then does its work (it may drop waypoints) and,
 * left with exactly one, the task nibble of waypoint 0 decides whether message 0x4e goes out (8: only when the
 * fleet's planet is another player's or has no route set (word +0x30 & 0x3ff is 0), 0 / 4 / above 8:
 * always). */
DECOMP(16, 0x1060, update_fleet_waypoints, FAR_CDECL)
void update_fleet_waypoints(void)
{
  FRAME_ENTER(0x0c + 4);
  for (uint16_t n = 0; (int16_t)n < (int16_t)drd16(0x553a); n++) {
    uint16_t at = (uint16_t)(drd16(0x00ba) + 4 * n);
    uint16_t fo = rd16(drd16(0x00bc), at), fs = rd16(drd16(0x00bc), (uint16_t)(at + 2));
    if ((fs | fo) == 0) break;
    if ((int16_t)rd16(fs, (uint16_t)(fo + 0x62)) <= 1) continue;
    uint16_t owner = rd16(fs, (uint16_t)(fo + 2)), id = rd16(fs, fo);
    if ((rd8(fs, (uint16_t)(fo + 5)) & 0x80) != 0) {
      (void)CALL_ORIG(10, 0x6230, FAR_CDECL, 4, fo, fs, 1, 0);
      post_message(owner, 0x137, id, 0, 0, 0, 0, 0, 0);
      continue;
    }
    /* follow-a-fleet waypoints 1 and up */
    uint16_t k = 1, di = 0x12;
    do {
      uint32_t list = rd32(fs, (uint16_t)(fo + 0x64));
      uint16_t ls = FP_SEG(list), lo = FP_OFF(list);
      if ((rd8(ls, (uint16_t)(lo + di + 0x0b)) & 0x0f) == 2) {
        uint32_t target = CALL_ORIG(7, 0x14a8, FAR_CDECL, 1, rd16(ls, (uint16_t)(lo + di + 8)));
        wr32(fs, (uint16_t)(fo + 0x68), target);
        if (target != 0) {
          if ((rd8(ls, (uint16_t)(lo + di + 0x0b)) & 0x20) == 0)
            wr32(ls, (uint16_t)(lo + di + 4), rd32(FP_SEG(target), (uint16_t)(FP_OFF(target) + 8)));
          wr32(fs, (uint16_t)(fo + 0x68), 0);
        } else {
          wr16(ls, (uint16_t)(lo + di + 0x0a), (uint16_t)((rd16(ls, (uint16_t)(lo + di + 0x0a)) & 0xf4ff) | 0x0400));
          wr16(ls, (uint16_t)(lo + di + 8), 0);
        }
      }
      di = (uint16_t)(di + 0x12);
      k++;
    } while ((int16_t)rd16(fs, (uint16_t)(fo + 0x62)) > (int16_t)k);
    /* waypoint 1 at the fleet's position moves up into slot 0 */
    uint32_t list = rd32(fs, (uint16_t)(fo + 0x64));
    uint16_t ls = FP_SEG(list), lo = FP_OFF(list);
    if (rd32(ls, (uint16_t)(lo + 0x16)) != rd32(fs, (uint16_t)(fo + 8))) continue;
    for (uint16_t b = 0; b < 0x12; b++) wr8(ls, (uint16_t)(lo + 4 + b), rd8(ls, (uint16_t)(lo + 0x16 + b)));
    if ((rd8(ls, (uint16_t)(lo + 0x0b)) & 0x0f) == 2 && (rd8(ls, (uint16_t)(lo + 0x0a)) & 0x0f) != 1 &&
        (rd8(ls, (uint16_t)(lo + 0x0a)) & 0x0f) != 4) {
      if (rd16(fs, (uint16_t)(fo + 6)) == 0xffff) {
        wr16(ls, (uint16_t)(lo + 0x0a), (uint16_t)((rd16(ls, (uint16_t)(lo + 0x0a)) & 0xf4ff) | 0x0400));
        wr16(ls, (uint16_t)(lo + 8), 0);
      } else {
        wr16(ls, (uint16_t)(lo + 0x0a), (uint16_t)((rd16(ls, (uint16_t)(lo + 0x0a)) & 0xf1ff) | 0x0100));
        wr16(ls, (uint16_t)(lo + 8), rd16(fs, (uint16_t)(fo + 6)));
      }
    }
    uint16_t flag;
    if ((rd8(ls, (uint16_t)(lo + 0x1c)) & 0x0f) == 7 && (rd8(ls, (uint16_t)(lo + 0x1d)) & 0x0f) == 2) flag = 0;
    else flag = (uint16_t)((rd8(fs, (uint16_t)(fo + 5)) & 2) >> 1);
    (void)CALL_ORIG(10, 0x6230, FAR_CDECL, 4, fo, fs, 1, flag);
    if (rd16(fs, (uint16_t)(fo + 0x62)) != 1) continue;
    /* one waypoint left: the task of waypoint 0 */
    list = rd32(fs, (uint16_t)(fo + 0x64));
    ls = FP_SEG(list);
    lo = FP_OFF(list);
    uint16_t task = rd8(ls, (uint16_t)(lo + 0x0a)) & 0x0f;
    bool say = true;
    if (task == 8) {
      if (rd16(fs, (uint16_t)(fo + 6)) != 0xffff) {
        uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, rd16(fs, (uint16_t)(fo + 6)));
        uint16_t ps = FP_SEG(planet), po = FP_OFF(planet);
        if (rd16(ps, (uint16_t)(po + 2)) == rd16(fs, (uint16_t)(fo + 2)) && (rd16(ps, (uint16_t)(po + 0x30)) & 0x3ff) != 0) say = false;
      }
    } else if ((task >= 1 && task <= 3) || (task >= 5 && task <= 7)) {
      say = false;
    }
    if (say) post_message(owner, 0x4e, id, 0, 0, 0, 0, 0, 0);
  }
  FRAME_LEAVE();
}

/* Merge the speed into the high nibble of a byte: b ^= ((b ^ (speed << 4)) & 0xf0). */
static void set_speed_nibble(uint16_t seg, uint16_t off, int16_t speed)
{
  uint8_t b = rd8(seg, off);
  wr8(seg, off, (uint8_t)(b ^ ((b ^ (uint8_t)((uint16_t)speed << 4)) & 0xf0)));
}

/* route_fleet_from_planet (PLACEHOLDER NAME, see the file header): 16:0x13f8 (Ghidra FUN_1078_13f8,
 * alias fn_16_13f8), 765 bytes. ENTER 0x26,0; PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6], planet [BP+0xA]), no result. Makes the fleet's course one leg to the planet
 * that `planet` names:
 * the waypoint count becomes 2 (the list header's count too), waypoint 1 gets the task bits
 * (w & 0xf1f8) | 0x0108 at +6, the planet id (the low 10 bits of the planet's word +0x30, minus 1) at
 * +4, that planet's position (DS:0x31b6 + 4 * (the 10 bits)) at +0 and bit 4 at +7. The warp speed starts as
 * 10:0x678c(fleet). When the planet and the planet with that id belong to one player, both have bit 1
 * of +5 and the fleet carries no cargo (the four dwords at +0x4c), and 7:0xaf8 finds a stargate item (not
 * -1) for both, stargate_jump_loss (16:0x0e10) with the heaviest design (word +0x28, the mass) of the fleet
 * and the distance (the C runtime's ftol
 * of the double 7:0x28ea returns, plus DS:0x2084) may set the speed to 11. A speed below 9 is then
 * searched downward from 9 while the cost 10:0x6312(fleet, speed) stays below the distance, when the
 * other planet's starbase hull (2:0x507c, word +0x34 non-zero) exists. Then, for a speed of 1 to 10, it is
 * lowered while the distance over speed squared stays the same (down to 2) and while the cost exceeds
 * the fleet's fuel (dword +0x5c, the fifth cargo dword; 10:0x6312 reads the 2:0x50a2 kind-1 item
 * records at +0x36 + 2 * speed and computes this dword * 1000 / a value derived from them). The
 * speed ends in the high nibble of the byte at waypoint 1 +6. */
DECOMP(16, 0x13f8, route_fleet_from_planet, FAR_CDECL)
void route_fleet_from_planet(uint32_t fleet, uint32_t planet)
{
  FRAME_ENTER(0x26 + 4);
  uint16_t seg = FP_SEG(fleet), off = FP_OFF(fleet), pseg = FP_SEG(planet), poff = FP_OFF(planet);
  wr16(seg, (uint16_t)(off + 0x62), 2);
  uint32_t list = rd32(seg, (uint16_t)(off + 0x64));
  uint16_t ls = FP_SEG(list), wp = (uint16_t)(FP_OFF(list) + 0x16);        /* waypoint 1 */
  wr8(ls, (uint16_t)(FP_OFF(list) + 3), 2);
  wr16(ls, (uint16_t)(wp + 6), (uint16_t)((rd16(ls, (uint16_t)(wp + 6)) & 0xfff8) | 0x0008));
  wr16(ls, (uint16_t)(wp + 6), (uint16_t)((rd16(ls, (uint16_t)(wp + 6)) & 0xf1ff) | 0x0100));
  uint16_t pid = rd16(pseg, (uint16_t)(poff + 0x30)) & 0x3ff;
  wr16(ls, (uint16_t)(wp + 4), (uint16_t)(pid - 1));
  uint32_t other = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, (uint16_t)(pid - 1));
  uint16_t os = FP_SEG(other), oo = FP_OFF(other);
  wr32(ls, wp, drd32((uint16_t)((pid << 2) + 0x31b6)));
  wr8(ls, (uint16_t)(wp + 7), (uint8_t)(rd8(ls, (uint16_t)(wp + 7)) | 0x10));
  int16_t speed = (int16_t)(uint16_t)CALL_ORIG(10, 0x678c, FAR_CDECL, 3, off, seg, 0);
  uint16_t dptr = (uint16_t)CALL_ORIG(7, 0x28ea, FAR_CDECL, 4, rd16(seg, (uint16_t)(off + 8)), rd16(seg, (uint16_t)(off + 0x0a)),
                                      rd16(ls, wp), rd16(ls, (uint16_t)(wp + 2)));
  double dist, add;
  memcpy(&dist, LINN(dptr), 8);
  memcpy(&add, LINN(0x2084), 8);
  int32_t need = fp80_to_i32_ftol(fp80_add(fp80_from_f64(dist), fp80_from_f64(add)));
  bool searched = false;      /* the block for a speed below 9 is reached only on the "same player" path */
  if (rd16(pseg, (uint16_t)(poff + 2)) == rd16(os, (uint16_t)(oo + 2)) && (rd8(pseg, (uint16_t)(poff + 5)) & 2) != 0 &&
      (rd8(os, (uint16_t)(oo + 5)) & 2) != 0) {
    searched = true;
    int16_t item_other = (int16_t)(uint16_t)CALL_ORIG(7, 0x0af8, FAR_CDECL, 2, oo, os);
    int16_t item_here = (int16_t)(uint16_t)CALL_ORIG(7, 0x0af8, FAR_CDECL, 2, poff, pseg);
    uint16_t c = 0;
    while (c < 4 && rd32(seg, (uint16_t)(off + 0x4c + 4 * c)) == 0) c++;
    if (c == 4 && item_other != -1 && item_here != -1) {
      int16_t widest = 0;
      uint32_t designs = owner_designs(rd16(seg, (uint16_t)(off + 2)));
      for (uint16_t i = 0; i < 16; i++) {
        if (rd16(seg, (uint16_t)(off + 0x0c + 2 * i)) == 0) continue;
        int16_t w = (int16_t)rd16(FP_SEG(designs), (uint16_t)(FP_OFF(designs) + 0x28 + 0x8d * i));
        if (w > widest) widest = w;
      }
      uint16_t r = (uint16_t)rt_call_far(MK_FP(SEG_SEL(16), 0x0e10), FAR_CDECL,
                                         (const uint16_t[]){(uint16_t)item_here, (uint16_t)item_other, (uint16_t)need, (uint16_t)widest,
                                                            (uint16_t)(rt_bp - 0x1a)}, 5);
      if (r == 1 && BP_LOCAL(uint16_t, -0x1a) == 0) speed = 11;
    }
  }
  if (searched && speed < 9) {
    /* the other planet's starbase design (index: the low nibble of its byte +0x2c) must have a hull entry */
    uint32_t sbd = MK_FP(drd16((uint16_t)(rd16(seg, (uint16_t)(off + 2)) * 4 + 0x10e)), drd16((uint16_t)(rd16(seg, (uint16_t)(off + 2)) * 4 + 0x10c)));
    uint16_t hull = rd16(FP_SEG(sbd), (uint16_t)(FP_OFF(sbd) + (uint16_t)((rd8(os, (uint16_t)(oo + 0x2c)) & 0x0f) * 0x8d)));
    uint32_t entry = rt_call_far(MK_FP(SEG_SEL(2), 0x507c), FAR_CDECL, (const uint16_t[]){hull}, 1);
    if (rd16(FP_SEG(entry), (uint16_t)(FP_OFF(entry) + 0x34)) != 0) {
      speed = 9;
      for (;;) {
        int32_t cost = (int32_t)CALL_ORIG(10, 0x6312, FAR_CDECL, 7, off, seg, 0, (uint16_t)speed, 0xffff, 0xffff, 1);
        if (cost >= need) break;
        speed--;
        if (!(speed > 0)) break;
      }
      set_speed_nibble(ls, (uint16_t)(wp + 6), speed);
    }
  }
  if (speed < 0x0b && speed != 0) {
    int32_t base = idiv32(idiv32(need, speed, 0x13f8), speed, 0x13f8);
    while (speed > 2) {
      speed--;
      if (idiv32(idiv32(need, speed, 0x13f8), speed, 0x13f8) > base) {
        speed++;
        break;
      }
    }
    while (speed != 0) {
      int32_t cost = (int32_t)CALL_ORIG(10, 0x6312, FAR_CDECL, 7, off, seg, 0, (uint16_t)speed, (uint16_t)need, (uint16_t)((uint32_t)need >> 16), 0);
      if ((int32_t)rd32(seg, (uint16_t)(off + 0x5c)) >= cost) break;
      speed--;
    }
  }
  set_speed_nibble(ls, (uint16_t)(wp + 6), speed);
  FRAME_LEAVE();
}

/* 16:0x21bc MERGEFLEETSDLG (Ghidra MERGEFLEETSDLG), 656 bytes. ENTER 0x60,0; PUSH DI; PUSH SI.
 * FAR PASCAL dialog procedure (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6..8]) of the dialog that
 * chooses which of DS:0x27f2 fleets to merge; their indices in the fleet table (DS:0xBA) are the words
 * of the array DS:0x53a0 points to, and the list box 0x51 holds one line per fleet. WM_ERASEBKGND fills the
 * background; WM_CTLCOLOR leaves the list box alone and gives the others the dialog colour; WM_INITDIALOG
 * fills the list box with the fleet names (7:0x1a70 of the id, with DS:0x18ba appended for a fleet of
 * more than one waypoint) and selects the line of the current fleet (its id is DS:0x4b8c), or every line when
 * exactly two fleets were chosen; WM_COMMAND: OK / Cancel (1, 2) store 0xffff for every line left unselected
 * and end the dialog (OK asks 31:0xab72 first when bit 3 of DS:0x791 is set), 0x7f8 / 0x7f9 select / clear
 * all lines, 0x76 is help. Returns 1 for what it handled. */
DECOMP(16, 0x21bc, MERGEFLEETSDLG, FAR_PASCAL)
uint16_t MERGEFLEETSDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x60 + 4);
  uint16_t ret = 0;
  if (msg == 0x0014) { /* WM_ERASEBKGND */
    ret = dlg_erase(hwnd, wParam, -0x0e);
  } else if (msg == 0x0019) { /* WM_CTLCOLOR */
    if (GetDlgItem(hwnd, 0x51) != (uint16_t)lParam) {
      (void)SetBkColor(wParam, drd32(0x253a));
      ret = drd16(0x0010);
    }
  } else if (msg == 0x0110) { /* WM_INITDIALOG */
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x18b6, 1);
    for (uint16_t i = 0; (int16_t)i < (int16_t)drd16(0x27f2); i++) {
      uint16_t index = drd16((uint16_t)(drd16(0x53a0) + 2 * i));
      uint32_t fleet = MK_FP(rd16(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + 4 * index + 2)), rd16(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + 4 * index)));
      uint16_t name = (uint16_t)CALL_ORIG(7, 0x1a70, FAR_CDECL, 1, rd16(FP_SEG(fleet), FP_OFF(fleet)));
      uint16_t len = ds_strlen(name);
      for (uint16_t k = 0; k <= len; k++) BP_LOCAL(uint8_t, -0x5e + k) = drd8((uint16_t)(name + k));
      if ((int16_t)rd16(FP_SEG(fleet), (uint16_t)(FP_OFF(fleet) + 0x62)) > 1) {
        uint16_t tail = ds_strlen(0x18ba);
        for (uint16_t k = 0; k <= tail; k++) BP_LOCAL(uint8_t, -0x5e + len + k) = drd8((uint16_t)(0x18ba + k));
      }
      (void)SendMessage(GetDlgItem(hwnd, 0x51), 0x0401, 0, bpfp(-0x5e));
      uint16_t selected = (drd16(0x27f2) == 2 || rd16(FP_SEG(fleet), FP_OFF(fleet)) == drd16(0x4b8c)) ? 1 : 0;
      (void)SendMessage(GetDlgItem(hwnd, 0x51), 0x0406, selected, (uint32_t)(int32_t)(int16_t)i);
    }
    if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    ret = 1;
  } else if (msg == 0x0111) { /* WM_COMMAND */
    if (wParam == 1 || wParam == 2) {
      for (uint16_t i = 0; (int16_t)drd16(0x27f2) > (int16_t)i; i++) {
        if (SendMessage(GetDlgItem(hwnd, 0x51), 0x0408, i, 0) == 0) dwr16((uint16_t)(drd16(0x53a0) + 2 * i), 0xffff);
      }
      bool done = false;
      if (wParam == 1 && (drd8(0x0791) & 8) != 0 && (uint16_t)CALL_ORIG(31, 0xab72, FAR_CDECL, 0) == 0) done = true;
      if (!done) {
        (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, 0x18b6, 0);
        (void)EndDialog(hwnd, wParam == 1 ? 1 : 0);
      }
      ret = 1;
    } else if (wParam == 0x76) {
      (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x00000453);
      ret = 1;
    } else if (wParam == 0x7f8 || wParam == 0x7f9) {
      for (uint16_t i = 0; (int16_t)i < (int16_t)drd16(0x27f2); i++)
        (void)SendMessage(GetDlgItem(hwnd, 0x51), 0x0406, wParam == 0x7f8 ? 1 : 0, (uint32_t)(int32_t)(int16_t)i);
      ret = 1;
    }
  }
  FRAME_LEAVE();
  return ret;
}
