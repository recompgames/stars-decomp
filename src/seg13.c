/* Decompiled code segment 13 (NE segment 13, selector 0x0068). Local-only.
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg13.c; Ghidra's pseudo-C in re/decomp/seg13 is only a reading aid.
 *
 * Segment 13 is the toolbar of the main window and its tooltip:
 *   13:0x0000  TBWNDPROC, the toolbar window procedure
 *   13:0x053e  toolbar_paint_bar: paint every button of the bar
 *   13:0x05a4  toolbar_paint_button: paint one button
 *   13:0x0844  toolbar_hit_test: which item is under a point
 *   13:0x08b0  toolbar_item_width: width of a toolbar item
 *   13:0x0902  toolbar_item_is_on: is a toolbar item switched on
 *   13:0x09ae  toolbar_set_item: set a toolbar item on or off (or choose from its pop-up menu)
 *   13:0x0fb8  toolbar_zoom_update: read or set the percentage of the zoom box
 *   13:0x109e  tooltip_update: show or hide the tooltip
 *   13:0x1224  TOOLTIPWNDPROC, the tooltip window procedure
 *   13:0x14bc  FAKECOMBOPROC and 13:0x1508 FAKECEPROC, subclass procedures of the zoom combo box
 * Names: TBWNDPROC, TOOLTIPWNDPROC, FAKECOMBOPROC and FAKECEPROC are the export names and are kept.
 * Ghidra's names for the others are generic (FUN_1060_<off>) and none of them wraps a single Win16
 * API, so their C names describe what each function does (CONVENTIONS rule f); they are descriptive,
 * not the game's. The old fn_13_<off> names are kept as aliases in the comment above each function.
 *
 * The code segment also holds data the functions read through CS: the byte table at 13:0x0521 (29
 * toolbar items in bar order, -1 = gap, -2 = narrow gap, -3 = the zoom box) and 9 words at 13:0x050f
 * (the zoom presets 25, 38, 50, 75, 100, 125, 150, 200, 400 of the menu of 13:0x09ae item 0x10). Both
 * are kept as static const tables below; the diff tests check them through 13:0x0844, 13:0x053e and 13:0x09ae.
 * Near calls inside the segment are PUSH CS; CALL near, so they are far calls with a 4-byte return
 * address; a callee that mirrors a frame (rule d) is reached through rt_call_far, a leaf (13:0x08b0
 * toolbar_item_width, 13:0x0902 toolbar_item_is_on) by its C name.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n + 2 * pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. Memory is read and written in 16-bit
 * pieces: an offset is a uint16_t and wraps like the original's. DS = SS = DGROUP in every function
 * here. The generated globals.h types some DGROUP words by Ghidra's guess, so words are accessed with
 * the helpers below at the original offsets, named in the comments. */
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

/* The toolbar items in bar order (13:0x0521, 29 bytes, read as signed bytes). */
static const int8_t tb_items[0x1d] = {0, 1, 2, 3, 4, 5, -1, 6, -1, 7, -2, -3, -1, 8, -1,
                                      9, -1, 0xb, 0x11, -1, 0xa, -1, 0xc, 0xd, -1, 0xe, 0xf, -1, 0x10};

/* toolbar_item_width: 13:0x08b0 (Ghidra FUN_1060_08b0, alias fn_13_08b0).
 * 81 bytes. PUSH BP; MOV BP,SP (a leaf: no locals, no pushes).
 * Width in pixels of toolbar item `item` (FAR CDECL, [BP+6]): 0x1d for a button, 0x0b for items 0xd
 * and 0xf, 6 for the gap -1, 2 for the narrow gap -2, 0x3c (0x46 when the cell height DS:0x267a is
 * 0x10 or more, a signed compare) for the zoom box -3, 0 for any other negative value. */
DECOMP(13, 0x08b0, toolbar_item_width, FAR_CDECL)
uint16_t toolbar_item_width(int16_t item)
{
  if (item >= 0) return (item == 0xd || item == 0xf) ? 0x0b : 0x1d;
  if (item == -1) return 6;
  if (item == -2) return 2;
  if (item != -3) return 0;
  return ((int16_t)drd16(0x267a) < 0x10) ? 0x3c : 0x46;
}

/* toolbar_item_is_on: 13:0x0902 (Ghidra FUN_1060_0902, alias fn_13_0902).
 * 172 bytes (the inventory counted only the 21 bytes before the jump table). PUSH BP; MOV BP,SP
 * (a leaf). Jump table at 13:0x0918, 18 words.
 * Is toolbar item `item` (FAR CDECL, [BP+6]) switched on: AX = 1 or 0. Items 0 to 5 are the values of
 * the low nibble of the view flags DS:0x055e; 6, 7, 9 are its bits 4, 5, 7; 0xa, 0xc, 0xb, 0xe, 0x11
 * are bits 8, 9, 10, 11, 12; 8 needs bit 6 and DS:0x4c76 == 0xf. Items 0xd, 0xf, 0x10 and anything
 * above 0x11 (unsigned compare) are never on. */
DECOMP(13, 0x0902, toolbar_item_is_on, FAR_CDECL)
uint16_t toolbar_item_is_on(uint16_t item)
{
  uint16_t flags = drd16(0x055e);
  if (item > 0x11) return 0;
  switch (item) {
    case 0: case 1: case 2: case 3: case 4: case 5:
      return (uint16_t)((flags & 0x0f) == item);
    case 6:  return (flags & 0x0010) != 0;
    case 7:  return (flags & 0x0020) != 0;
    case 8:
      if ((flags & 0x0040) == 0) return 0;
      return drd16(0x4c76) == 0x000f;
    case 9:  return (flags & 0x0080) != 0;
    case 10: return (flags & 0x0100) != 0;
    case 11: return (flags & 0x0400) != 0;
    case 12: return (flags & 0x0200) != 0;
    case 14: return (flags & 0x0800) != 0;
    case 17: return (flags & 0x1000) != 0;
    default: return 0;
  }
}

/* toolbar_hit_test: 13:0x0844 (Ghidra FUN_1060_0844, alias fn_13_0844).
 * 107 bytes. ENTER 2,0; PUSH DI; PUSH SI. Its only callee is the leaf 13:0x08b0 (toolbar_item_width),
 * which exposes no stack address, so ordinary C locals stand for the frame.
 * Hit test: FAR CDECL, [BP+6] is a near pointer to a POINT {x, y} in DGROUP. A point with x < 4,
 * y < 4 or y >= 0x20 (signed compares) hits nothing (AX = -1). Otherwise it walks the bar from x = 4,
 * adding each item's width; the first item whose end lies beyond x is hit: the point is rewritten to
 * {left edge of the item, 4} and AX is the item (a signed byte of the table). Past the end of the bar
 * the point is left alone and AX = -1. */
DECOMP(13, 0x0844, toolbar_hit_test, FAR_CDECL)
int16_t toolbar_hit_test(uint16_t pt)
{
  int16_t left = 4;
  int16_t px = (int16_t)drd16(pt);
  int16_t py = (int16_t)drd16((uint16_t)(pt + 2));
  if (px < 4 || py < 4 || py >= 0x20) return -1;
  for (int16_t i = 0; i < 0x1d; i++) {
    int16_t w = (int16_t)toolbar_item_width(tb_items[i]);
    if ((int16_t)(w + left) > px) {
      dwr16(pt, (uint16_t)left);
      dwr16((uint16_t)(pt + 2), 4);
      return tb_items[i];
    }
    left = (int16_t)(w + left);
  }
  return -1;
}

#define ROP_PAT 0x00F00021u /* PATCOPY-style raster operation used by every PatBlt of the bar */

/* toolbar_paint_button: 13:0x05a4 (Ghidra FUN_1060_05a4, alias fn_13_05a4).
 * 672 bytes. ENTER 0x16,0; PUSH DI; PUSH SI.
 * Paints one button: FAR CDECL (hdc [BP+6], x [BP+8], y [BP+0xA], item [BP+0xC], state [BP+0xE]).
 * The button is di = width(item) wide and 0x1c high: a bevel of 1-pixel PatBlts (light brush DS:0x14,
 * dark brush DS:0x16, swapped when state is not 0; face brush DS:0x10), a pressed/selected inset
 * of `state` pixels when state is not 0, then the glyph from 8:0xaa4e (a DIB blit, or a PatBlt when the
 * DIB handle DS:0x5a4e is 0) at the offset (state, state), with the glyph width 7 when the item is
 * narrower than 0x18 and 0x18 otherwise. With state > 1 it ends with one more dark pixel. Returns
 * nothing (AX is left over). */
DECOMP(13, 0x05a4, toolbar_paint_button, FAR_CDECL)
void toolbar_paint_button(uint16_t hdc, uint16_t x, uint16_t y, uint16_t item, uint16_t state)
{
  FRAME_ENTER(0x16 + 4);
  uint16_t di = toolbar_item_width((int16_t)item);
  uint16_t gw = ((int16_t)di < 0x18) ? 7 : 0x18;          /* [BP-2] */
  uint16_t b4, b6;                                          /* [BP-4], [BP-6] */
  if (state != 0) {
    b4 = drd16(0x16);
    b6 = drd16(0x14);
  } else {
    b4 = drd16(0x14);
    b6 = drd16(0x16);
  }
  (void)SelectObject(hdc, b4);
  uint16_t x2 = (uint16_t)(x + 2);                          /* [BP-8] */
  uint16_t w4 = (uint16_t)(di - 4);
  (void)PatBlt(hdc, (int16_t)x2, (int16_t)y, (int16_t)w4, 1, ROP_PAT);
  uint16_t y2 = (uint16_t)(y + 2);                          /* [BP-0xA] */
  (void)PatBlt(hdc, (int16_t)x, (int16_t)y2, 1, 0x18, ROP_PAT);
  uint16_t x1 = (uint16_t)(x + 1);                          /* [BP-0xC] */
  uint16_t y1 = (uint16_t)(y + 1);                          /* [BP-0xE] */
  (void)PatBlt(hdc, (int16_t)x1, (int16_t)y1, 1, 1, ROP_PAT);
  uint16_t y1a = (uint16_t)(y + 0x1a);                      /* [BP-0x10] */
  (void)PatBlt(hdc, (int16_t)x1, (int16_t)y1a, 1, 1, ROP_PAT);
  (void)SelectObject(hdc, b6);
  (void)PatBlt(hdc, (int16_t)x2, (int16_t)(uint16_t)(y + 0x1b), (int16_t)w4, 1, ROP_PAT);
  uint16_t xe = (uint16_t)(x + di);                         /* [BP-0x12] */
  (void)PatBlt(hdc, (int16_t)(uint16_t)(xe - 1), (int16_t)y2, 1, 0x18, ROP_PAT);
  uint16_t xe2 = (uint16_t)(xe - 2);                        /* [BP-0x14] */
  (void)PatBlt(hdc, (int16_t)xe2, (int16_t)y1, 1, 1, ROP_PAT);
  (void)PatBlt(hdc, (int16_t)xe2, (int16_t)y1a, 1, 1, ROP_PAT);
  (void)SelectObject(hdc, drd16(0x10));
  (void)PatBlt(hdc, (int16_t)x2, (int16_t)y1, (int16_t)w4, 1, ROP_PAT);
  (void)PatBlt(hdc, (int16_t)x1, (int16_t)y2, 1, 0x18, ROP_PAT);
  if (state != 0) {
    (void)PatBlt(hdc, (int16_t)x2, (int16_t)y2, (int16_t)w4, (int16_t)state, ROP_PAT);
    (void)PatBlt(hdc, (int16_t)x2, (int16_t)y2, (int16_t)state, 0x18, ROP_PAT);
    if (state == 1) {
      (void)PatBlt(hdc, (int16_t)x2, (int16_t)y1a, (int16_t)w4, 1, ROP_PAT);
      (void)PatBlt(hdc, (int16_t)(uint16_t)(xe - 2), (int16_t)y2, 1, 0x18, ROP_PAT);
    }
  } else {
    (void)PatBlt(hdc, (int16_t)x2, (int16_t)(uint16_t)(y + 0x19), (int16_t)w4, 2, ROP_PAT);
    (void)PatBlt(hdc, (int16_t)(uint16_t)(xe - 3), (int16_t)y2, 2, 0x18, ROP_PAT);
  }
  CALL_ORIG(8, 0xaa4e, FAR_CDECL, 12, hdc, (uint16_t)(state + x + 2), (uint16_t)(state + y + 2), gw, 0x17,
            drd16(0x5a4e), (uint16_t)((int16_t)item * 0x18), 0, gw, 0x17, 0x0020, 0x00cc);
  if ((int16_t)state > 1) {
    (void)SelectObject(hdc, drd16(0x14));
    (void)PatBlt(hdc, (int16_t)xe2, (int16_t)y1a, 1, 1, ROP_PAT);
  }
  FRAME_LEAVE();
}

/* toolbar_paint_bar: 13:0x053e (Ghidra FUN_1060_053e, alias fn_13_053e).
 * 102 bytes. ENTER 6,0; PUSH DI; PUSH SI.
 * Paints the whole bar: FAR CDECL (hdc [BP+6]; the rectangle pointer at [BP+8] is not used). Selects
 * and realizes the palette DS:0x54f8 (background 0), then walks the 29 items from x = y = 4 (the POINT
 * at BP-6) and paints each real item (id >= 0) with 13:0x05a4 (state = 13:0x0902 of the item),
 * stepping x by 13:0x08b0 of every item, gaps included. */
DECOMP(13, 0x053e, toolbar_paint_bar, FAR_CDECL)
void toolbar_paint_bar(uint16_t hdc, uint16_t rect)
{
  FRAME_ENTER(6 + 4);
  (void)rect;
  (void)SelectPalette(hdc, drd16(0x54f8), 0);
  (void)RealizePalette(hdc);
  uint16_t x = 4, y = 4;                                    /* the POINT at [BP-6], [BP-4] */
  for (uint16_t i = 0; i < 0x1d; i++) {
    int16_t item = tb_items[i];                             /* [BP-2] */
    if (item >= 0) {
      uint16_t on = toolbar_item_is_on((uint16_t)item);
      const uint16_t a[] = {hdc, x, y, (uint16_t)item, on};
      (void)rt_call_far(MK_FP(SEG_SEL(13), 0x05a4), FAR_CDECL, a, 5);
    }
    x = (uint16_t)(x + toolbar_item_width(item));
  }
  FRAME_LEAVE();
}

/* _wsprintf with one variable word: the original pushes the argument below the two far pointers, so
 * the variable part is a word on the emulated stack; the by-name wrapper (harness/byname.h) takes a
 * far pointer to it and logs only the fixed part, as the import thunk does. */
static int16_t wsprintf_w(uint16_t out, uint16_t fmt, uint16_t arg)
{
  uint16_t at = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, at, arg);
  return _wsprintf(dsfp(out), dsfp(fmt), MK_FP(rt_ss, at));
}

/* toolbar_zoom_update: 13:0x0fb8 (Ghidra FUN_1060_0fb8, alias fn_13_0fb8).
 * 229 bytes. PUSH BP; MOV BP,SP; PUSH DI; PUSH SI.
 * Zoom box: FAR CDECL (mode [BP+6]), returns nothing. Sets the "dirty" bit 0x40 in DS:0x0794. With
 * mode 0 the percentage is parsed from the text of the combo box DS:0x18be (GetWindowText into
 * DS:0x5866, 20 bytes): decimal digits, then end of text or '%', else 0; otherwise it is DS:0x0560.
 * It is clamped to 3..100 (signed), the combo selection is set to (100 - percent) / 10, the box text
 * is rewritten with the format at DS:[0x178] ("%d%%"), and when the percentage differs from DS:0x0560
 * that is updated, the window DS:0x015a is invalidated (only when bit 0x20 of DS:0x055e is clear),
 * bit 0x20 is set and the window DS:0x0150 is invalidated. The focus goes to DS:0x2806. */
DECOMP(13, 0x0fb8, toolbar_zoom_update, FAR_CDECL)
void toolbar_zoom_update(uint16_t mode)
{
  FRAME_ENTER(0 + 4);
  dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x40));
  uint16_t pct;                                             /* DI */
  if (mode == 0) {
    (void)GetWindowText(drd16(0x18be), dsfp(0x5866), 0x14);
    pct = 0;
    uint16_t at = 0x5866;                                   /* SI */
    while ((int8_t)drd8(at) >= 0x30 && (int8_t)drd8(at) <= 0x39) {
      pct = (uint16_t)(pct * 10 + (uint16_t)(int16_t)(int8_t)drd8(at) - 0x30);
      at = (uint16_t)(at + 1);
    }
    if (drd8(at) != 0 && drd8(at) != 0x25) pct = 0;
  } else {
    pct = drd16(0x0560);
  }
  if ((int16_t)pct < 3) {
    pct = 3;
  } else if ((int16_t)pct > 0x64) {
    pct = 0x64;
  }
  (void)SendMessage(drd16(0x18be), 0x040e, (uint16_t)((int16_t)(0x64 - (int16_t)pct) / 10), 0);
  (void)wsprintf_w(0x5866, drd16(0x0178), pct);
  SetWindowText(drd16(0x18be), dsfp(0x5866));
  if (drd16(0x0560) != pct) {
    dwr16(0x0560, pct);
    if ((drd8(0x055e) & 0x20) == 0) InvalidateRect(drd16(0x015a), 0, 1);
    dwr8(0x055e, (uint8_t)(drd8(0x055e) | 0x20));
    InvalidateRect(drd16(0x0150), 0, 1);
  }
  (void)SetFocus(drd16(0x2806));
  FRAME_LEAVE();
}

/* The 9 zoom presets in the code segment (13:0x050f, 9 words), the menu entries of 13:0x09ae item 0x10. */
static const uint16_t tb_zoom[9] = {25, 38, 50, 75, 100, 125, 150, 200, 400};

/* `SHL AX,CL`: the count is the low byte of CX masked to 5 bits, a count of 16 or more clears AX. */
static uint16_t shl16(uint16_t v, uint16_t cx) { return (uint16_t)((uint32_t)v << (cx & 0x1f)); }

/* FAR pointer to the stack slot at BP + off. */
#define BPFP(off) MK_FP(rt_ss, (uint16_t)(rt_bp + (off)))

/* toolbar_set_item: 13:0x09ae (Ghidra FUN_1060_09ae, alias fn_13_09ae).
 * 1546 bytes (the inventory counted only 31). ENTER 0x86,0; PUSH DI; PUSH SI. Jump table at 13:0x09ce,
 * 18 words. The name says "set" because the function sets the item to `flag`; its callers (TBWNDPROC,
 * 3:0x0f90) pass 1 for an item that is off and 0 for one that is on, so for them it toggles the item.
 * Set toolbar item `item` ([BP+6]) to `flag` ([BP+8]), FAR CDECL, returns nothing. Sets bit 0x40 of
 * DS:0x0794. Items 0 to 5 are the view modes (low nibble of the view flags DS:0x055e, set when flag is
 * not 0, else nothing happens); 6, 7, 9, 0xa, 0xb, 0xc, 0xe, 0x11 set (flag) or clear (no flag) the
 * bit 0x10, 0x20, 0x80, 0x100, 0x400, 0x200, 0x800, 0x1000. Items 8, 0xd, 0xf and 0x10 open a pop-up menu
 * through 24:0x0e24 (hwnd DS:0x015a, the mouse position in client coordinates, the item count, a dword
 * flag array, a near pointer array to the strings, 0xfffe, 0) built in the frame at BP-0x56/-0x26 (8),
 * BP-0x86/-0x36 (0xd), BP-0x54/-0x24 (0xf) and BP-0x42/-0x1e (0x10); strings come from 8:0x8dc0.
 * Item 8 chooses the sets DS:0x4c76 (bit mask of 4 values, entries 0 to 2 are the all/none/invert of 0xf),
 * 0xd the layers DS:0x54ec (16 bits), 0xf the bits DS:0x538e (8); item 0x10 picks a zoom preset and hands
 * it to 4:0x1f40. Common tail: invalidate DS:0x0150 unless item 6, then 31:0x61b0 when bit 3 of
 * DS:0x0791 is set. */
DECOMP(13, 0x09ae, toolbar_set_item, FAR_CDECL)
void toolbar_set_item(uint16_t item, uint16_t flag)
{
  FRAME_ENTER(0x86 + 4);
  dwr8(0x0794, (uint8_t)(drd8(0x0794) | 0x40));
  uint16_t mask = 0;
  if (item > 0x11) goto out;
  switch (item) {
    case 0: case 1: case 2: case 3: case 4: case 5:
      if (flag == 0) goto out;
      dwr16(0x055e, (uint16_t)((drd16(0x055e) & 0x3ff0) + item));
      goto tail;
    case 6: mask = 0x0010; goto setbit;
    case 7: mask = 0x0020; goto setbit;
    case 9: mask = 0x0080; goto setbit;
    case 10: mask = 0x0100; goto setbit;
    case 11: mask = 0x0400; goto setbit;
    case 12: mask = 0x0200; goto setbit;
    case 14: mask = 0x0800; goto setbit;
    case 17: mask = 0x1000; goto setbit;
    case 8: {
      /* sets: strings 0x4ef, 0x4f0, a separator, then 4 more (0x4f1..) */
      if ((drd8(0x055e) & 0x40) == 0) dwr16(0x4c76, 0);
      uint16_t pp = (uint16_t)(rt_bp - 0x26), fp = (uint16_t)(rt_bp - 0x56);   /* [BP-8], [BP-4] */
      uint16_t sb = 0x5906;                                                       /* [BP-6] */
      for (uint16_t id = 0x4ef; (int16_t)id <= 0x4f0; id++) {
        uint16_t v = (id == 0x4ef) ? (drd16(0x4c76) == 0x0f ? 1 : 0) : (drd16(0x4c76) == 0 ? 1 : 0);
        wr32(rt_ss, fp, v);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, sb);
        wr16(rt_ss, pp, sb);
        sb = (uint16_t)(sb + 0x1e);
        pp = (uint16_t)(pp + 2);
        fp = (uint16_t)(fp + 4);
      }
      uint16_t cnt = 2;                                                           /* [BP-0xA] */
      wr32(rt_ss, (uint16_t)(rt_bp - 0x56 + 4 * cnt), 0);
      dwr8(0x5960, 0xff);
      dwr8(0x5961, 0);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x26 + 2 * cnt), 0x5960);
      cnt = (uint16_t)(cnt + 1);
      pp = (uint16_t)(rt_bp - 0x26 + 2 * cnt);
      fp = (uint16_t)(rt_bp - 0x56 + 4 * cnt);
      cnt = (uint16_t)(cnt + 4);
      uint16_t st = 0x5866;
      for (uint16_t k = 0; st < 0x58de; k++) {
        uint16_t on = (shl16(1, k) & drd16(0x4c76)) != 0;
        wr32(rt_ss, fp, on);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(k + 0x4f1), st);
        wr16(rt_ss, pp, st);
        pp = (uint16_t)(pp + 2);
        st = (uint16_t)(st + 0x1e);
        fp = (uint16_t)(fp + 4);
      }
      GetCursorPos(BPFP(-0xe));
      ScreenToClient(drd16(0x015a), BPFP(-0xe));
      const uint16_t a[] = {drd16(0x015a), rd16(rt_ss, (uint16_t)(rt_bp - 0xe)), rd16(rt_ss, (uint16_t)(rt_bp - 0xc)), cnt,
                            (uint16_t)(rt_bp - 0x56), (uint16_t)(rt_bp - 0x26), 0xfffe, 0};
      int16_t sel = (int16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
      if (sel == -1) goto out;
      if (sel < 3) dwr16(0x4c76, (sel == 0) ? 0x000f : 0);
      else dwr16(0x4c76, (uint16_t)(drd16(0x4c76) ^ shl16(1, (uint16_t)(sel - 3))));
      if (drd16(0x4c76) == 0) dwr8(0x055e, (uint8_t)(drd8(0x055e) & 0xbf));
      else dwr8(0x055e, (uint8_t)(drd8(0x055e) | 0x40));
      InvalidateRect(drd16(0x015a), 0, 1);
      goto tail;
    }
    case 13: {
      /* layers: strings 0x4ec..0x4ee, a separator, then one entry per table row whose flag bit 1 is clear */
      uint16_t pp = (uint16_t)(rt_bp - 0x36), fp = (uint16_t)(rt_bp - 0x86);       /* [BP-4], [BP-2] */
      uint16_t sb = 0x5866;
      for (uint16_t id = 0x4ec; (int16_t)id <= 0x4ee; id++) {
        wr32(rt_ss, fp, 0);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, sb);
        wr16(rt_ss, pp, sb);
        pp = (uint16_t)(pp + 2);
        sb = (uint16_t)(sb + 0x14);
        fp = (uint16_t)(fp + 4);
      }
      uint16_t cnt = 3;                                                            /* [BP-0xA] */
      wr32(rt_ss, (uint16_t)(rt_bp - 0x86 + 4 * cnt), 0);
      dwr8(0x592e, 0xff);
      dwr8(0x592f, 0);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x36 + 2 * cnt), 0x592e);
      uint16_t bit = 1;
      cnt = (uint16_t)(cnt + 1);
      pp = (uint16_t)(rt_bp - 0x36 + 2 * cnt);                                     /* [BP-6] */
      fp = (uint16_t)(rt_bp - 0x86 + 4 * cnt);                                     /* [BP-8] */
      for (uint16_t e = 0x41f5; e < 0x4ac5; e = (uint16_t)(e + 0x8d)) {
        if ((drd8((uint16_t)(e + 1)) & 2) == 0) {
          wr32(rt_ss, fp, (drd16(0x54ec) & bit) != 0);
          fp = (uint16_t)(fp + 4);
          wr16(rt_ss, pp, (uint16_t)(e - 0x73));
          pp = (uint16_t)(pp + 2);
          cnt = (uint16_t)(cnt + 1);
        }
        bit = (uint16_t)(bit + bit);
      }
      GetCursorPos(BPFP(-0xe));
      ScreenToClient(drd16(0x015a), BPFP(-0xe));
      int16_t sel = (int16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, drd16(0x015a), rd16(rt_ss, (uint16_t)(rt_bp - 0xe)),
                                       rd16(rt_ss, (uint16_t)(rt_bp - 0xc)), cnt, (uint16_t)(rt_bp - 0x86),
                                       (uint16_t)(rt_bp - 0x36), 0xfffe, 0);
      if (sel == -1) goto out;
      uint16_t m;                                                                  /* the layer bit chosen, for the last test */
      if (sel < 4) {
        if (sel == 0) dwr16(0x54ec, 0xffff);
        else dwr16(0x54ec, (sel == 1) ? (uint16_t)~drd16(0x54ec) : 0);
        m = drd16(0x54ec);
        if ((drd8(0x055f) & 2) == 0) goto layer_set;
      } else {
        int16_t left = (int16_t)(sel - 4);
        uint16_t n = 0;
        for (uint16_t e = 0x41f5; e < 0x4ac5; e = (uint16_t)(e + 0x8d)) {
          if ((drd8((uint16_t)(e + 1)) & 2) == 0) {
            left = (int16_t)(left - 1);
            if (left < 0) break;
          }
          n = (uint16_t)(n + 1);
        }
        dwr16(0x54ec, (uint16_t)(drd16(0x54ec) ^ shl16(1, n)));
        if ((drd8(0x055f) & 2) == 0) {
          m = (uint16_t)(drd16(0x54ec) & shl16(1, n));
          goto layer_set;
        }
      }
      goto layer_tail;
    layer_set:
      if (m != 0) {
        dwr8(0x055f, (uint8_t)(drd8(0x055f) | 2));
        InvalidateRect(drd16(0x015a), 0, 1);
      }
    layer_tail:
      if ((drd8(0x055f) & 2) != 0) goto tail;
      goto out;
    }
    case 15: {
      /* bits of DS:0x538e: strings 0x4ec..0x4ee, a separator, then 8 entries 0x179.. */
      uint16_t pp = (uint16_t)(rt_bp - 0x24), fp = (uint16_t)(rt_bp - 0x54);
      uint16_t sb = 0x5906;
      for (uint16_t id = 0x4ec; (int16_t)id <= 0x4ee; id++) {
        wr32(rt_ss, fp, 0);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, sb);
        wr16(rt_ss, pp, sb);
        pp = (uint16_t)(pp + 2);
        sb = (uint16_t)(sb + 0x14);
        fp = (uint16_t)(fp + 4);
      }
      uint16_t cnt = 3;                                                            /* [BP-8] */
      wr32(rt_ss, (uint16_t)(rt_bp - 0x54 + 4 * cnt), 0);
      dwr8(0x5960, 0xff);
      dwr8(0x5961, 0);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x24 + 2 * cnt), 0x5960);
      cnt = (uint16_t)(cnt + 1);
      pp = (uint16_t)(rt_bp - 0x24 + 2 * cnt);
      fp = (uint16_t)(rt_bp - 0x54 + 4 * cnt);
      cnt = (uint16_t)(cnt + 8);
      uint16_t st = 0x5866;
      for (uint16_t k = 0; st < 0x5906; k++) {
        wr32(rt_ss, fp, (shl16(1, k) & drd16(0x538e)) != 0);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(k + 0x179), st);
        wr16(rt_ss, pp, st);
        pp = (uint16_t)(pp + 2);
        st = (uint16_t)(st + 0x14);
        fp = (uint16_t)(fp + 4);
      }
      GetCursorPos(BPFP(-0xc));
      ScreenToClient(drd16(0x015a), BPFP(-0xc));
      int16_t sel = (int16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, drd16(0x015a), rd16(rt_ss, (uint16_t)(rt_bp - 0xc)),
                                       rd16(rt_ss, (uint16_t)(rt_bp - 0xa)), cnt, (uint16_t)(rt_bp - 0x54),
                                       (uint16_t)(rt_bp - 0x24), 0xfffe, 0);
      if (sel == -1) goto out;
      uint16_t m;
      uint16_t cl = 0;
      if (sel < 4) {
        if (sel == 0) dwr16(0x538e, 0x00ff);
        else if (sel == 1) dwr8(0x538e, (uint8_t)(drd8(0x538e) ^ 0xff));
        else dwr16(0x538e, 0);
        m = drd16(0x538e);
        if ((drd8(0x055f) & 8) == 0) goto bits_set;
      } else {
        cl = (uint16_t)(sel - 4);
        dwr16(0x538e, (uint16_t)(drd16(0x538e) ^ shl16(1, cl)));
        if ((drd8(0x055f) & 8) == 0) {
          m = (uint16_t)(drd16(0x538e) & shl16(1, cl));
          goto bits_set;
        }
      }
      goto bits_tail;
    bits_set:
      if (m != 0) {
        dwr8(0x055f, (uint8_t)(drd8(0x055f) | 8));
        InvalidateRect(drd16(0x015a), 0, 1);
      }
    bits_tail:
      if ((drd8(0x055f) & 8) != 0) goto tail;
      goto out;
    }
    case 16: {
      /* zoom presets: 9 entries "%d%%" of tb_zoom, the current one (DS:0x0576 - index == -4) marked */
      uint16_t pp = (uint16_t)(rt_bp - 0x1e), fp = (uint16_t)(rt_bp - 0x42);
      uint16_t sb = 0x5866;
      for (uint16_t k = 0; k < 9; k++) {
        wr32(rt_ss, fp, ((uint16_t)(drd16(0x0576) - k) == 0xfffc) ? 1 : 0);
        (void)wsprintf_w(sb, drd16(0x0178), tb_zoom[k]);
        wr16(rt_ss, pp, sb);
        pp = (uint16_t)(pp + 2);
        sb = (uint16_t)(sb + 8);
        fp = (uint16_t)(fp + 4);
      }
      GetCursorPos(BPFP(-0xc));
      ScreenToClient(drd16(0x015a), BPFP(-0xc));
      int16_t sel = (int16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, drd16(0x015a), rd16(rt_ss, (uint16_t)(rt_bp - 0xc)),
                                       rd16(rt_ss, (uint16_t)(rt_bp - 0xa)), 9, (uint16_t)(rt_bp - 0x42),
                                       (uint16_t)(rt_bp - 0x1e), 0xfffe, 0);
      if (sel == -1) goto out;
      {
        const uint16_t a[2] = {drd16(0x2806), (uint16_t)(sel + 0xf3d)};
        (void)rt_call_far(MK_FP(SEG_SEL(4), 0x1f40), FAR_CDECL, a, 2);
      }
      goto out;
    }
    default:
      goto out;
  }
setbit:
  if (flag != 0) dwr16(0x055e, (uint16_t)(drd16(0x055e) | mask));
  else dwr16(0x055e, (uint16_t)(drd16(0x055e) & (uint16_t)~mask));
tail:
  if (item != 6) InvalidateRect(drd16(0x0150), 0, 1);
  if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
out:
  FRAME_LEAVE();
}

/* tooltip_update: 13:0x109e (Ghidra FUN_1060_109e, alias fn_13_109e).
 * 390 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * Show or hide the tooltip: FAR CDECL (id [BP+6], rect [BP+8] a near pointer to a RECT, the screen
 * rectangle of the item). `vis` = the tooltip window DS:0x18ce exists and is visible. With id < 0 or
 * no rectangle the tooltip window is destroyed and, if it was visible, the time is kept in DS:0x18d2.
 * With the id and rectangle already shown (DS:0x4da6, DS:0x29c8) nothing is done. Otherwise it fetches the
 * text of `id` (8:0x8dc0 into DS:0x5866), keeps the id and rectangle, measures the text in the font
 * DS:0x292e (DS:0x4d86 = width), creates the tooltip window of the class DS:0x020a (WS_POPUP, parent
 * DS:0x015a) or invalidates it, positions it topmost and sends it the message 0x5f3 with wParam 1 when the
 * last tooltip left less than 400 ms ago or one is visible, else 0 (which starts the show timer). */
DECOMP(13, 0x109e, tooltip_update, FAR_CDECL)
void tooltip_update(uint16_t id, uint16_t rect)
{
  FRAME_ENTER(8 + 4);
  uint16_t vis = 0;                                         /* [BP-4] */
  if (drd16(0x18ce) != 0 && IsWindowVisible(drd16(0x18ce)) != 0) vis = 1;
  if ((int16_t)id < 0 || rect == 0) {
    if (drd16(0x18ce) != 0) (void)DestroyWindow(drd16(0x18ce));
    if (vis != 0) dwr32(0x18d2, GetTickCount());
    FRAME_LEAVE();
    return;
  }
  if (id == drd16(0x4da6) && EqualRect(dsfp(rect), dsfp(0x29c8)) != 0) {
    FRAME_LEAVE();
    return;
  }
  dwr16(0x4da6, id);
  uint16_t len = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, 0x5866);   /* [BP-6] */
  dwr32(0x29c8, drd32(rect));
  dwr32(0x29cc, drd32((uint16_t)(rect + 4)));
  uint16_t hdc = GetDC(0);                                  /* [BP-2] */
  uint16_t old = SelectObject(hdc, drd16(0x292e));          /* [BP-8] */
  dwr16(0x4d86, (uint16_t)GetTextExtent(hdc, dsfp(0x5866), (int16_t)len));
  (void)SelectObject(hdc, old);
  (void)ReleaseDC(0, hdc);
  if (drd16(0x18ce) == 0) {
    (void)CreateWindow(dsfp(0x020a), 0, 0x80000000u, 0x0064, 0x0064, (int16_t)(uint16_t)(drd16(0x4d86) + 6),
                       (int16_t)(uint16_t)(drd16(0x267a) + 6), drd16(0x015a), 0, drd16(0x54f4), 0);
  } else {
    InvalidateRect(drd16(0x18ce), 0, 1);
  }
  (void)SetWindowPos(drd16(0x18ce), 0xffff, 0, 0, (int16_t)(uint16_t)(drd16(0x4d86) + 6),
                     (int16_t)(uint16_t)(drd16(0x267a) + 6), 0x0216);
  uint32_t tick = GetTickCount();
  uint32_t limit = (uint32_t)(drd32(0x18d2) + 0x190u);
  uint16_t wp = (tick > limit && vis == 0) ? 0 : 1;
  const uint16_t a[] = {drd16(0x18ce), 0x05f3, wp, 0, 0};
  (void)rt_call_far(MK_FP(SEG_SEL(13), 0x1224), FAR_PASCAL, a, 5);
  FRAME_LEAVE();
}

/* 13:0x1224 TOOLTIPWNDPROC, 664 bytes. ENTER 0x30,0; PUSH DI; PUSH SI.
 * FAR PASCAL (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]), returns DX:AX.
 * WM_CREATE (1) keeps the window in DS:0x18ce; WM_DESTROY (2) clears it, kills the timer DS:0x18d0 (-1 =
 * none) and forgets the shown id DS:0x4da6 (-1); both go on to DefWindowProc. WM_PAINT (0xf) frames the
 * client rectangle (PAINTSTRUCT at BP-0x30, RECT at BP-0x10) with the brush DS:0x1c and draws the text of
 * DS:0x4da6 (8:0x8dc0 into DS:0x5866) in the font DS:0x292e, transparent, colour DS:0x4ac0, at (3, 3);
 * returns 0. WM_ERASEBKGND (0x14) fills the client rectangle with DS:0x29d4 and returns 1. WM_MOUSEMOVE,
 * WM_LBUTTONDOWN and WM_LBUTTONDBLCLK (0x200, 0x201, 0x203) destroy the window; returns 0.
 * 0x5f3 (private: show) with wParam 0 restarts the 700 ms timer 0x39e; with another wParam, and WM_TIMER
 * (0x113) with wParam 0x39e, it checks the cursor against the item rectangle DS:0x29c8: outside, or a
 * timer tick when it stayed there more than 10 s (DS:0x5a44 + 10000), destroys the window; inside it moves
 * the window just under the item (keeping it on screen, DS:0x264a is the right limit), updates it and starts
 * a 50 ms timer. Everything else goes to DefWindowProc. DS:0x18d0 gets 0x39e when the timer was created,
 * else -1. */
DECOMP(13, 0x1224, TOOLTIPWNDPROC, FAR_PASCAL)
uint32_t TOOLTIPWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x30 + 4);
  if (msg == 0x14) {
    GetClientRect(hwnd, BPFP(-0x10));
    (void)FillRect(wParam, BPFP(-0x10), drd16(0x29d4));
    FRAME_LEAVE();
    return 1;
  }
  if (msg == 0x0f) {
    uint16_t dc = BeginPaint(hwnd, BPFP(-0x30));
    GetClientRect(hwnd, BPFP(-0x10));
    (void)FrameRect(dc, BPFP(-0x10), drd16(0x001c));
    (void)SelectObject(dc, drd16(0x292e));
    uint16_t len = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, drd16(0x4da6), 0x5866);
    int16_t oldmode = SetBkMode(dc, 1);
    (void)SetTextColor(dc, drd32(0x4ac0));
    (void)ExtTextOut(dc, 3, 3, 0, 0, dsfp(0x5866), len, 0);
    (void)SetBkMode(dc, oldmode);
    EndPaint(hwnd, BPFP(-0x30));
    FRAME_LEAVE();
    return 0;
  }
  if (msg == 1) {
    dwr16(0x18ce, hwnd);
    goto deflt;
  }
  if (msg == 2) {
    dwr16(0x18ce, 0);
    if (drd16(0x18d0) != 0xffff) {
      (void)KillTimer(hwnd, drd16(0x18d0));
      dwr16(0x18d0, 0xffff);
    }
    dwr16(0x4da6, 0xffff);
    goto deflt;
  }
  if (msg == 0x200 || msg == 0x201 || msg == 0x203) {
    (void)DestroyWindow(hwnd);
    FRAME_LEAVE();
    return 0;
  }
  if (msg == 0x5f3) {
    if (wParam == 0) {
      if (drd16(0x18d0) != 0xffff) (void)KillTimer(hwnd, drd16(0x18d0));
      uint16_t t = SetTimer(hwnd, 0x039e, 0x02bc, 0);
      dwr16(0x18d0, (uint16_t)(((t == 0) ? 0xfc61 : 0) + 0x039e));
      FRAME_LEAVE();
      return 0;
    }
    wParam = 0x039e;
  } else if (msg != 0x113) {
    goto deflt;
  }
  if (wParam != 0x039e) goto deflt;
  {
    if (msg == 0x113 && IsWindowVisible(hwnd) != 0) {
      dwr32(0x18d2, GetTickCount());
      GetCursorPos(BPFP(-8));
      if (PtInRect(dsfp(0x29c8), rd32(rt_ss, (uint16_t)(rt_bp - 8))) != 0 &&
          (uint32_t)(drd32(0x5a44) + 0x2710u) >= drd32(0x18d2)) {
        FRAME_LEAVE();
        return 0;
      }
      (void)DestroyWindow(hwnd);
      FRAME_LEAVE();
      return 0;
    }
    uint32_t t = GetTickCount();
    dwr32(0x5a44, t);
    GetCursorPos(BPFP(-8));
    if (PtInRect(dsfp(0x29c8), rd32(rt_ss, (uint16_t)(rt_bp - 8))) == 0) {
      (void)DestroyWindow(hwnd);
      FRAME_LEAVE();
      return 0;
    }
    ScreenToClient(drd16(0x2806), BPFP(-8));
    if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp - 8)) + drd16(0x4d86)) > (int16_t)drd16(0x264a))
      wr16(rt_ss, (uint16_t)(rt_bp - 8), (uint16_t)(drd16(0x264a) - drd16(0x4d86) - 5));
    ClientToScreen(drd16(0x2806), BPFP(-8));
    int32_t h3 = (int16_t)(uint16_t)(drd16(0x267a) * 3);                 /* h + h + h in 16 bits */
    int16_t h32 = (int16_t)((h3 + (h3 < 0 ? 1 : 0)) >> 1);               /* CDQ; SUB AX,DX; SAR AX,1 */
    (void)SetWindowPos(hwnd, 0xffff, (int16_t)rd16(rt_ss, (uint16_t)(rt_bp - 8)),
                       (int16_t)(uint16_t)(h32 + rd16(rt_ss, (uint16_t)(rt_bp - 6))), 0, 0, 0x0251);
    UpdateWindow(hwnd);
    if (drd16(0x18d0) != 0xffff) (void)KillTimer(hwnd, drd16(0x18d0));
    uint16_t tm = SetTimer(hwnd, 0x039e, 0x0032, 0);
    dwr16(0x18d0, (uint16_t)(((tm == 0) ? 0xfc61 : 0) + 0x039e));
    FRAME_LEAVE();
    return 0;
  }
deflt:
  {
    uint32_t r = DefWindowProc(hwnd, msg, wParam, lParam);
    FRAME_LEAVE();
    return r;
  }
}

/* 13:0x14bc FAKECOMBOPROC and 13:0x1508 FAKECEPROC, 76 bytes each. PUSH BP; MOV BP,SP (no locals).
 * FAR PASCAL (hwnd, msg, wParam, lParam): window procedures that the toolbar puts in front of the zoom
 * combo box (FAKECOMBOPROC) and of its edit control (FAKECEPROC). WM_MOUSEMOVE (0x200) is first shown to
 * TBWNDPROC (its result is dropped); WM_LBUTTONDOWN and WM_LBUTTONDBLCLK (0x201, 0x203) hide the tooltip
 * (13:0x109e with id 0xffff and no rectangle). Every message is then passed to the previous procedure
 * (DS:0x2668 for the combo box, DS:0x31b6 for the edit control) with CallWindowProc, whose DX:AX is returned. */
static void fake_pre(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  if (msg == 0x200) {
    const uint16_t a[] = {hwnd, msg, wParam, (uint16_t)(lParam >> 16), (uint16_t)lParam};
    (void)rt_call_far(MK_FP(SEG_SEL(13), 0x0000), FAR_PASCAL, a, 5);
  } else if (msg == 0x201 || msg == 0x203) {
    const uint16_t a[] = {0xffff, 0};
    (void)rt_call_far(MK_FP(SEG_SEL(13), 0x109e), FAR_CDECL, a, 2);
  }
}

DECOMP(13, 0x14bc, FAKECOMBOPROC, FAR_PASCAL)
uint32_t FAKECOMBOPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0);
  fake_pre(hwnd, msg, wParam, lParam);
  uint32_t r = CallWindowProc(drd32(0x2668), hwnd, msg, wParam, lParam);
  FRAME_LEAVE();
  return r;
}

DECOMP(13, 0x1508, FAKECEPROC, FAR_PASCAL)
uint32_t FAKECEPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0);
  fake_pre(hwnd, msg, wParam, lParam);
  uint32_t r = CallWindowProc(drd32(0x31b6), hwnd, msg, wParam, lParam);
  FRAME_LEAVE();
  return r;
}

/* 13:0x0000 TBWNDPROC, 1295 bytes. ENTER 0x3e,0; PUSH DI; PUSH SI.
 * The toolbar window procedure, FAR PASCAL (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam
 * [BP+6]), returns DX:AX. The frame holds, by BP offset: -0x3e PAINTSTRUCT (0x20 bytes), -0x1e RECT,
 * -0x16 and -0x12 POINTs, and the temporaries named below. WM_CREATE builds the zoom combo box; WM_PAINT
 * paints the border and the bar; WM_MOUSEMOVE shows the tooltip of the item under the mouse; WM_LBUTTONDOWN
 * and WM_LBUTTONDBLCLK track a click (below); WM_COMMAND forwards the combo box notification 8 (CBN_...) to
 * itself as 0x5f4 (zoom entered); WM_ERASEBKGND (0x14) fills the client area with DS:0x10; WM_CTLCOLOR (0x19)
 * sets the background colour DS:0x253a on the DC and returns DS:0x10; WM_SETCURSOR (0x20) sets the arrow.
 * AX = 1 for WM_ERASEBKGND and WM_SETCURSOR with DX left as it was on entry (the code jumps past its
 * XOR DX,DX; the C version returns DX = 0, which is what a call with DX = 0 gives); DefWindowProc's DX:AX
 * for everything unhandled; 0:0 for the rest.
 * Click tracking (item >= 0): hides the tooltip first; items 0 to 5 that are on beep; the toggles 8, 0xd, 0xf,
 * 0x10 call 13:0x09ae at once with flag = (item was off); others capture the mouse, paint the item pressed
 * while the mouse is inside its rectangle and not otherwise, driven by the message pump 8:0xa2c6 (returns 0
 * when the button was released; it fills the POINT at BP-0x12). On release, if the last position was
 * inside ([BP-2] is the last PtInRect result; it is read from the stack even when the pump returned 0 the
 * first time, in which case it is whatever the stack held) 13:0x09ae toggles the item; then the bar (item
 * <= 5, 13:0x053e) or the item (13:0x05a4) is redrawn with the new state. */
DECOMP(13, 0x0000, TBWNDPROC, FAR_PASCAL)
uint32_t TBWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER(0x3e + 4);
  uint32_t ret = 0;
  uint16_t lpl = (uint16_t)lParam, lph = (uint16_t)(lParam >> 16);
  if (msg == 0x20) {
    (void)SetCursor(LoadCursor(0, 0x7f00));
    ret = 1;
    goto done;
  }
  if (msg > 0x20) {
    if (msg == 0x111) {
      if (lpl == drd16(0x18be) && lph == 8) (void)PostMessage(hwnd, 0x05f4, 0, 0);
    } else if (msg == 0x200) {
      wr16(rt_ss, (uint16_t)(rt_bp - 0x12), lpl);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x10), lph);
      if (drd16(0x015a) != hwnd) MapWindowPoints(hwnd, drd16(0x015a), BPFP(-0x12), 1);
      if (rd32(rt_ss, (uint16_t)(rt_bp - 0x12)) != drd32(0x18c0)) {
        dwr32(0x18c0, rd32(rt_ss, (uint16_t)(rt_bp - 0x12)));
        int16_t hit = toolbar_hit_test((uint16_t)(rt_bp - 0x12));
        uint16_t tip;
        if (hit < 0) {
          if (hit != -3) goto zero;
          tip = 0x178;
        } else {
          tip = (uint16_t)(hit + 0x166);
        }
        wr16(rt_ss, (uint16_t)(rt_bp - 0x1e), rd16(rt_ss, (uint16_t)(rt_bp - 0x12)));
        wr16(rt_ss, (uint16_t)(rt_bp - 0x1c), rd16(rt_ss, (uint16_t)(rt_bp - 0x10)));
        wr16(rt_ss, (uint16_t)(rt_bp - 0x1a),
             (uint16_t)(toolbar_item_width(hit) + rd16(rt_ss, (uint16_t)(rt_bp - 0x1e))));
        wr16(rt_ss, (uint16_t)(rt_bp - 0x18), 0x1c);
        MapWindowPoints(drd16(0x015a), 0, BPFP(-0x1e), 2);
        const uint16_t a[] = {tip, (uint16_t)(rt_bp - 0x1e)};
        (void)rt_call_far(MK_FP(SEG_SEL(13), 0x109e), FAR_CDECL, a, 2);
      }
    } else if (msg == 0x201 || msg == 0x203) {
      const uint16_t hide[] = {0xffff, 0};
      (void)rt_call_far(MK_FP(SEG_SEL(13), 0x109e), FAR_CDECL, hide, 2);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x12), lpl);
      wr16(rt_ss, (uint16_t)(rt_bp - 0x10), lph);
      wr32(rt_ss, (uint16_t)(rt_bp - 0x16), rd32(rt_ss, (uint16_t)(rt_bp - 0x12)));
      int16_t item = toolbar_hit_test((uint16_t)(rt_bp - 0x16));    /* DI */
      if (item < 0) goto zero;
      uint16_t on = toolbar_item_is_on((uint16_t)item);              /* [BP-0xA] */
      if (on != 0 && item <= 5) {
        MessageBeep(0);
        goto zero;
      }
      if (item == 0xd || item == 0xf || item == 0x10 || item == 8) {
        const uint16_t a[] = {(uint16_t)item, (uint16_t)(on == 0 ? 1 : 0)};
        (void)rt_call_far(MK_FP(SEG_SEL(13), 0x09ae), FAR_CDECL, a, 2);
        goto zero;
      }
      uint16_t width = toolbar_item_width(item);                     /* [BP-0xC] */
      wr16(rt_ss, (uint16_t)(rt_bp - 0x1e), rd16(rt_ss, (uint16_t)(rt_bp - 0x16)));
      wr16(rt_ss, (uint16_t)(rt_bp - 0x1a),
           (uint16_t)(rd16(rt_ss, (uint16_t)(rt_bp - 0x16)) + width));
      wr16(rt_ss, (uint16_t)(rt_bp - 0x1c), rd16(rt_ss, (uint16_t)(rt_bp - 0x14)));
      wr16(rt_ss, (uint16_t)(rt_bp - 0x18), (uint16_t)(rd16(rt_ss, (uint16_t)(rt_bp - 0x14)) + 0x1c));
      uint16_t dc = GetDC(hwnd);                                     /* [BP-6] */
      (void)SelectPalette(dc, drd16(0x54f8), 0);
      (void)RealizePalette(dc);
      (void)SetCapture(hwnd);
      uint16_t last = 0xffff;                                        /* [BP-8] */
      uint16_t cur = 0;                                              /* SI */
      if ((uint16_t)CALL_ORIG(8, 0xa2c6, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x12)) != 0) {
        do {
          cur = PtInRect(BPFP(-0x1e), rd32(rt_ss, (uint16_t)(rt_bp - 0x12)));
          if (cur != last) {
            const uint16_t a[] = {dc, rd16(rt_ss, (uint16_t)(rt_bp - 0x16)), rd16(rt_ss, (uint16_t)(rt_bp - 0x14)),
                                  (uint16_t)item, (uint16_t)(on + cur)};
            (void)rt_call_far(MK_FP(SEG_SEL(13), 0x05a4), FAR_CDECL, a, 5);
            last = cur;
          }
        } while ((uint16_t)CALL_ORIG(8, 0xa2c6, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x12)) != 0);
        BP_LOCAL(uint16_t, -2) = cur;
      }
      ReleaseCapture();
      if (BP_LOCAL(uint16_t, -2) != 0) {
        const uint16_t a[] = {(uint16_t)item, (uint16_t)(on == 0 ? 1 : 0)};
        (void)rt_call_far(MK_FP(SEG_SEL(13), 0x09ae), FAR_CDECL, a, 2);
        on = toolbar_item_is_on((uint16_t)item);
      }
      if (item <= 5) {
        GetClientRect(hwnd, BPFP(-0x1e));
        const uint16_t a[] = {dc, (uint16_t)(rt_bp - 0x1e)};
        (void)rt_call_far(MK_FP(SEG_SEL(13), 0x053e), FAR_CDECL, a, 2);
      } else {
        const uint16_t a[] = {dc, rd16(rt_ss, (uint16_t)(rt_bp - 0x16)), rd16(rt_ss, (uint16_t)(rt_bp - 0x14)),
                              (uint16_t)item, on};
        (void)rt_call_far(MK_FP(SEG_SEL(13), 0x05a4), FAR_CDECL, a, 5);
      }
      (void)ReleaseDC(hwnd, dc);
    } else if (msg == 0x5f4) {
      const uint16_t a[] = {0};
      (void)rt_call_far(MK_FP(SEG_SEL(13), 0x0fb8), FAR_CDECL, a, 1);
    } else {
      goto deflt;
    }
    goto zero;
  }
  if (msg == 0x19) {
    (void)SetBkColor(wParam, drd32(0x253a));
    ret = drd16(0x0010);
    goto done;
  }
  if (msg > 0x19) goto deflt;
  if (msg == 1) {
    uint16_t x = 4;                                                /* [BP-0xA] */
    for (uint16_t i = 0; (int16_t)i < 0x1d; i++) {                 /* [BP-0xE] */
      int16_t it = tb_items[i];                                    /* SI */
      uint16_t w = toolbar_item_width(it);                         /* [BP-0xC] */
      if (it == -3) {
        int16_t h = (int16_t)drd16(0x267a);
        int32_t v = (int16_t)(uint16_t)(0x14 - h);
        int16_t y = (int16_t)(((v + (v < 0 ? 1 : 0)) >> 1) + 4);   /* CDQ; SUB AX,DX; SAR AX,1; ADD AX,4 */
        uint16_t cy = (uint16_t)(h * 11 + 0x1c);
        uint16_t cb = CreateWindow(dsfp(0x18c4), 0, 0x50200042u, (int16_t)x, y, (int16_t)w, (int16_t)cy, hwnd, 0,
                                   drd16(0x54f4), 0);
        dwr16(0x18be, cb);
        (void)SendMessage(cb, 0x30, drd16(0x2930), 0);
        uint16_t sel = 0xffff;                                     /* [BP-8] */
        uint16_t idx = 0;
        uint16_t pct = 100;
        do {
          if (pct == drd16(0x0560)) sel = idx;
          (void)wsprintf_w(0x5866, drd16(0x0178), pct);
          (void)SendMessage(drd16(0x18be), 0x0403, 0, dsfp(0x5866));
          idx = (uint16_t)(idx + 1);
          pct = (uint16_t)(pct - 10);
        } while ((int16_t)pct > 0);
        (void)SendMessage(drd16(0x18be), 0x0415, 4, 0);
        (void)SendMessage(drd16(0x18be), 0x040e, sel, 0);
        (void)wsprintf_w(0x5866, drd16(0x0178), drd16(0x0560));
        SetWindowText(drd16(0x18be), dsfp(0x5866));
        dwr32(0x2668, GetWindowLong(drd16(0x18be), -4));
        (void)SetWindowLong(drd16(0x18be), -4, drd32(0x27be));
        uint16_t child = GetWindow(drd16(0x18be), 5);
        if (child != 0) {
          dwr32(0x31b6, GetWindowLong(child, -4));
          (void)SetWindowLong(child, -4, drd32(0x29c2));
        }
      }
      x = (uint16_t)(x + w);
    }
    goto zero;
  }
  if (msg == 0x0f) {
    uint16_t dc = BeginPaint(hwnd, BPFP(-0x3e));
    GetClientRect(hwnd, BPFP(-0x1e));
    uint16_t right = rd16(rt_ss, (uint16_t)(rt_bp - 0x1a));
    uint16_t bottom = rd16(rt_ss, (uint16_t)(rt_bp - 0x18));
    (void)PatBlt(dc, 0, 0, (int16_t)right, 1, 0x42);
    (void)PatBlt(dc, 0, (int16_t)(uint16_t)(bottom - 1), (int16_t)right, 1, 0x42);
    if (drd16(0x255e) == 0) (void)PatBlt(dc, 0, 0, 1, (int16_t)bottom, 0x42);
    const uint16_t a[] = {dc, (uint16_t)(rt_bp - 0x1e)};
    (void)rt_call_far(MK_FP(SEG_SEL(13), 0x053e), FAR_CDECL, a, 2);
    EndPaint(hwnd, BPFP(-0x3e));
    goto zero;
  }
  if (msg == 0x14) {
    GetClientRect(hwnd, BPFP(-0x1e));
    (void)FillRect(wParam, BPFP(-0x1e), drd16(0x0010));
    ret = 1;
    goto done;
  }
deflt:
  ret = DefWindowProc(hwnd, msg, wParam, lParam);
  goto done;
zero:
  ret = 0;
done:
  FRAME_LEAVE();
  return ret;
}
