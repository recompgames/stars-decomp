/* Decompiled code segment 5 (NE segment 5, selector 0x0028).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg05.c; Ghidra's pseudo-C in re/decomp/seg05 is only a reading aid.
 *
 * Segment 5 is the information window of the selected thing (a planet DS:0x4B84, a fleet DS:0x4B86 or a
 * map object DS:0x4B8A; the kind is DS:0x4B80 / DS:0x4B82) with its panel, header text and order button:
 *   5:0x0000  MINEWNDPROC, the window procedure (create, paint, cursor, button command, mouse clicks)
 *   5:0x0324  invalidate_info_areas: measure the labels and invalidate the list and label areas
 *   5:0x0474  own_object_index_and_count: count the current player's map objects of kind 0 and find
 *             one by its first word
 *   5:0x04e8  paint_info_panel: paint the panel of the selected planet / fleet / map object
 *   5:0x2840  hit_test_info_zone: the zone of the panel under a point
 *   5:0x2ab4  info_on_click: fills the descriptor DS:0x0BC0 that 24:0x091e shows (zones 1 to 14)
 *   5:0x329a  update_info_header: rebuild the header text and the order button
 *   5:0x344a  draw_cycle_button: draw the glyph of the zone-9 button that cycles the things at the place
 *   5:0x34fc  draw_diamond_marker: draw a small 3-D diamond (PatBlt)
 *   5:0x36a8  several_things_at_place: is more than one thing at the selected place
 *   5:0x3774  popup_things_at_place: pop-up menu of the things at the selected place
 *   5:0x3a72  planet_mineral_output: mineral output of a planet and of the fleets mining it
 * Names: the export name MINEWNDPROC is kept; Ghidra's other names are all generic (FUN_1020_<off>),
 * so the C names describe what each function does (CONVENTIONS rule f). draw_cycle_button is named
 * from its caller: MINEWNDPROC draws it at the right end of the title line, in the strip that
 * hit_test_info_zone reports as zone 9 (where info_on_click cycles the selection), with the state
 * several_things_at_place. The old fn_05_<off> names are kept as aliases in the comment above each
 * function.
 *
 * Near calls inside the segment are PUSH CS; CALL near, so they are far calls with a 4-byte return
 * address; a callee that mirrors a frame (rule d) is reached through rt_call_far, a leaf (5:0x0474,
 * 5:0x36a8) by its C name. The functions mirror their original frames (CONVENTIONS rule d):
 * FRAME_ENTER(n + 2 * pushes) with n the ENTER size and one push for each PUSH SI / PUSH DI. Memory is
 * read and written in 16-bit pieces: an offset is a uint16_t and wraps like the original's. DS = SS =
 * DGROUP in every function here (a function that loads DS from another segment restores it). The
 * generated globals.h types some DGROUP words by Ghidra's guess, so words are accessed with the helpers
 * below at the original offsets, named in the comments. The one x87 use is the square root of 5:0x04e8
 * (a map object of kind 0), through the stand-ins of src/crt34.h (rule g). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/crt34.h"
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
/* Signed 16-bit result of an arithmetic expression (wraps like the register). */
#define S16(e) ((int16_t)(uint16_t)(e))

/* IDIV r/m16 of the sign-extended value num (CWD) by den: the original traps (#DE) on a zero divisor
 * and on a quotient that does not fit (CONVENTIONS rule b); the C version stops with a message. `where`
 * is the offset in segment 5 of the instruction. */
static int16_t idiv16(int16_t num, int16_t den, uint16_t where)
{
  if (den == 0 || (num == -32768 && den == -1)) {
    fprintf(stderr, "5:0x%04x: divide error (IDIV16 %d / %d)\n", where, num, den);
    abort();
  }
  return (int16_t)(num / den);
}

/* The map-object array: base offset, selector and 16-bit end offset (count * 0x12 wraps). */
#define DS_MO_OFF 0x188e
#define DS_MO_SEG 0x1890
#define DS_MO_COUNT 0x1892
static inline uint16_t mo_end(void) {
  return (uint16_t)(g_map_objects + (uint16_t)((uint32_t)g_map_object_count * sizeof(struct MapObject)));
}

/* "Nothing selected" in the selected planet, fleet and map-object indexes DS:0x4B84, 0x4B86, 0x4B8A. */
#define NO_SELECTION 0xffff

/* How many map objects (minefields) the current player owns, and the 1-based place of the last one
 * whose id word equals key among them (0 when none). Both results are written to the caller's
 * DGROUP words out_pos and out_cnt.
 * 5:0x0474 FUN_1020_0474, FAR CDECL */
DECOMP(5, 0x0474, own_object_index_and_count, FAR_CDECL)
void own_object_index_and_count(uint16_t key, uint16_t out_pos, uint16_t out_cnt)
{
  uint16_t pos = 0, cnt = 0;
  uint16_t first = g_map_objects;
  uint16_t seg = g_map_objects_segment_maybe;
  uint16_t end = mo_end();
  for (uint16_t at = first; at < end; at = (uint16_t)(at + sizeof(struct MapObject))) {
    const struct MapObject *obj = FAR_REC(struct MapObject, seg, at);
    uint8_t bits = obj->owner_kind_bits;
    if (map_object_kind(bits) != MAPOBJ_KIND_MINEFIELD || map_object_owner(bits) != g_current_player) continue;
    cnt = (uint16_t)(cnt + 1);
    if (obj->id_kind == key) pos = cnt;
  }
  NEAR_AT(uint16_ua, out_pos) = pos;
  NEAR_AT(uint16_ua, out_cnt) = cnt;
}

/* Is more than one thing at the selected place (DS:0x4B7C, an x,y dword)? A selected planet together
 * with a selected fleet or map object is already more than one. A selected fleet alone is counted
 * against the other fleets at that place, a selected map object against the other map objects; the
 * second match answers yes. Returns 1 for yes, 0 for no.
 * 5:0x36a8 FUN_1020_36a8, FAR CDECL */
DECOMP(5, 0x36a8, several_things_at_place, FAR_CDECL)
uint16_t several_things_at_place(void)
{
  uint16_t planet = g_selected_planet, fleet = g_selected_fleet, object = g_selected_map_object;
  if (planet != NO_SELECTION && (fleet != NO_SELECTION || object != NO_SELECTION)) return 1;
  if (fleet != NO_SELECTION) {
    if (object != NO_SELECTION) return 1;
    uint16_t found = 0;
    uint16_t slots_off = NEAR_AT(struct FarPtrWords, DS_FLEET_PTRS).off;
    uint16_t slots_seg = NEAR_AT(struct FarPtrWords, DS_FLEET_PTRS).seg;
    for (int16_t i = 0; (int16_t)g_fleet_count > i; i++) {
      FARPTR fp = *FAR_REC(uint32_ua, slots_seg, slots_off + (uint16_t)i * sizeof(FARPTR));
      if (fp == 0) break;
      if (*FAR_REC(uint32_ua, FP_SEG(fp), FP_OFF(fp) + offsetof(struct Fleet, xy)) == g_selected_place_xy && ++found > 1) return 1;
    }
  }
  if (object != NO_SELECTION) {
    uint16_t found = 0;
    uint16_t first = g_map_objects;
    uint16_t seg = g_map_objects_segment_maybe;
    uint16_t end = mo_end();
    for (uint16_t at = first; at < end; at = (uint16_t)(at + sizeof(struct MapObject))) {
      if (*FAR_REC(uint32_ua, seg, at + offsetof(struct MapObject, xy)) == g_selected_place_xy && ++found > 1) return 1;
    }
  }
  return 0;
}

/* The middle of [lo, hi): CWD, SUB AX,DX, SAR AX,1, ADD AX,lo (round toward zero), as the code does for
 * x. The y centre uses a plain SAR (round down). */
static inline int16_t mid_x(int16_t lo, int16_t hi)
{
  int16_t v = (int16_t)(uint16_t)((uint16_t)hi - (uint16_t)lo);
  int16_t sign = v < 0 ? -1 : 0;
  return (int16_t)(uint16_t)((uint16_t)(int16_t)((int16_t)(uint16_t)((uint16_t)v - (uint16_t)sign) >> 1) + (uint16_t)lo);
}
static inline int16_t mid_y(int16_t lo, int16_t hi)
{
  int16_t v = (int16_t)(uint16_t)((uint16_t)hi - (uint16_t)lo);
  return (int16_t)(uint16_t)((uint16_t)(int16_t)(v >> 1) + (uint16_t)lo);
}

/* draw_cycle_button: 5:0x344a (Ghidra FUN_1020_344a, alias fn_05_344a).
 * 178 bytes. ENTER 4,0; PUSH DI; PUSH SI. FAR CDECL: hdc [BP+6],
 * near pointer to a RECT [BP+8], state [BP+0xA]; returns nothing. Draws an 11 x 12 glyph from the
 * bitmap DS:0x4C74 (selected into a memory DC) centred in the rectangle: first masked (SRCAND,
 * source 0x23,0x39), then painted (SRCPAINT) from x 0x18 when state is not 0, with y 0x39 (0x2D
 * when state is 0). */
DECOMP(5, 0x344a, draw_cycle_button, FAR_CDECL)
void draw_cycle_button(uint16_t hdc, uint16_t rect, uint16_t state)
{
  FRAME_ENTER(4 + 4);
  int16_t left = (int16_t)drd16(rect), top = (int16_t)drd16((uint16_t)(rect + 2));
  int16_t right = (int16_t)drd16((uint16_t)(rect + 4)), bottom = (int16_t)drd16((uint16_t)(rect + 6));
  uint16_t mem = CreateCompatibleDC(hdc);
  uint16_t old = SelectObject(mem, drd16(0x4c74));
  int16_t cx = mid_x(left, right);
  int16_t cy = (int16_t)(uint16_t)((uint16_t)mid_y(top, bottom) - 5);
  (void)BitBlt(hdc, (int16_t)(uint16_t)((uint16_t)cx - 5), cy, 0x0b, 0x0c, mem, 0x23, 0x39, 0x008800c6);
  (void)BitBlt(hdc, (int16_t)(uint16_t)((uint16_t)cx - 5), cy, 0x0b, 0x0c, mem, state >= 1 ? 0x18 : 0,
               state < 1 ? 0x2d : 0x39, 0x00ee0086);
  (void)SelectObject(mem, old);
  (void)DeleteDC(mem);
  FRAME_LEAVE();
}

/* draw_diamond_marker: 5:0x34fc (Ghidra FUN_1020_34fc, alias fn_05_34fc).
 * 427 bytes. ENTER 0x12,0; PUSH DI; PUSH SI. FAR CDECL: hdc [BP+6],
 * near pointer to a RECT [BP+8], brush [BP+0xA]; returns nothing. Draws a small 3-D diamond in the
 * rectangle with PatBlt (rop 0x00F00021 = PATCOPY): the brush DS:0x14 for the left edges (2 x 1 runs
 * stepping left from centre - 1, one from the top + 1 down, one from the bottom - 2 up), DS:0x16 for
 * the 1 x 1 dots at the top and bottom of the centre and the right edges (the same runs stepping
 * right from the centre), then the caller's brush to fill the inside (rows widening by 2 from the top
 * + 4 and the bottom - 5 toward the middle). */
DECOMP(5, 0x34fc, draw_diamond_marker, FAR_CDECL)
void draw_diamond_marker(uint16_t hdc, uint16_t rect, uint16_t brush)
{
  FRAME_ENTER(0x12 + 4);
  const uint32_t ROP = 0x00f00021;
  int16_t left = (int16_t)drd16(rect), top = (int16_t)drd16((uint16_t)(rect + 2));
  int16_t right = (int16_t)drd16((uint16_t)(rect + 4)), bottom = (int16_t)drd16((uint16_t)(rect + 6));
  int16_t midx = mid_x(left, right);
  int16_t di = (int16_t)(uint16_t)((uint16_t)top + 1);
  int16_t lowy = (int16_t)(uint16_t)((uint16_t)bottom - 2);
  int16_t xm1 = (int16_t)(uint16_t)((uint16_t)midx - 1);
  uint16_t old = SelectObject(hdc, drd16(0x0014));
  if (!(lowy < di)) {
    int16_t x = xm1;
    do {
      (void)PatBlt(hdc, x, di, 2, 1, ROP);
      int16_t xold = x;
      x = (int16_t)(uint16_t)((uint16_t)x - 1);
      (void)PatBlt(hdc, xold, lowy, 2, 1, ROP);
      di = (int16_t)(uint16_t)((uint16_t)di + 1);
      lowy = (int16_t)(uint16_t)((uint16_t)lowy - 1);
    } while (lowy >= di);
  }
  (void)SelectObject(hdc, drd16(0x0016));
  (void)PatBlt(hdc, midx, top, 1, 1, ROP);
  (void)PatBlt(hdc, midx, (int16_t)(uint16_t)((uint16_t)bottom - 1), 1, 1, ROP);
  {
    int16_t y1 = (int16_t)(uint16_t)((uint16_t)top + 1);
    lowy = (int16_t)(uint16_t)((uint16_t)bottom - 2);
    if (!(y1 > lowy)) {
      int16_t x = midx;
      di = y1;
      do {
        (void)PatBlt(hdc, x, di, 2, 1, ROP);
        di = (int16_t)(uint16_t)((uint16_t)di + 1);
        int16_t xold = x;
        x = (int16_t)(uint16_t)((uint16_t)x + 1);
        (void)PatBlt(hdc, xold, lowy, 2, 1, ROP);
        lowy = (int16_t)(uint16_t)((uint16_t)lowy - 1);
      } while (lowy >= di);
    }
  }
  {
    int16_t y = (int16_t)(uint16_t)((uint16_t)top + 4);
    int16_t yb = (int16_t)(uint16_t)((uint16_t)bottom - 5);
    (void)SelectObject(hdc, brush);
    if (!(yb < y)) {
      int16_t x = midx;
      uint16_t w = 1;
      int16_t ytop = y;
      do {
        (void)PatBlt(hdc, x, ytop, (int16_t)w, 1, ROP);
        int16_t xold = x;
        x = (int16_t)(uint16_t)((uint16_t)x - 1);
        (void)PatBlt(hdc, xold, yb, (int16_t)w, 1, ROP);
        w = (uint16_t)(w + 2);
        ytop = (int16_t)(uint16_t)((uint16_t)ytop + 1);
        yb = (int16_t)(uint16_t)((uint16_t)yb - 1);
      } while (yb >= ytop);
    }
  }
  (void)SelectObject(hdc, old);
  FRAME_LEAVE();
}

/* hit_test_info_zone: 5:0x2840 (Ghidra FUN_1020_2840, alias fn_05_2840).
 * 628 bytes. ENTER 0x44,0; PUSH DI; PUSH SI. FAR CDECL: window
 * [BP+6], point x [BP+8], y [BP+0xA]; returns AX = the zone code under the point (0 = none).
 * The zone depends on the mode DS:0x4B80 (4 means: 1 when bit 0 of the byte DS:0x4B82 is set, else
 * 2 when bit 1 is, else 0) and on the client rectangle of the window with its top moved down by two
 * text heights (DS:0x267A): 9 for the strip above the list when 5:0x36a8 says more than one is
 * selected; for modes other than 2 (and 8 unless the selected map object is of kind 0x40) the rows
 * of the planet list (7:0x0358 fetches the record of the selected planet DS:0x4B84 into the frame
 * at BP-0x42: owner at +2, flags at +5) give 5, 6 + row, 1 + row, 0xA, 0xC or 0xD; modes 2 and 8
 * test the fixed boxes (0xA, 0xE, 0xB). */
DECOMP(5, 0x2840, hit_test_info_zone, FAR_CDECL)
uint16_t hit_test_info_zone(uint16_t hwnd, int16_t x, int16_t y)
{
  FRAME_ENTER(0x44 + 4);
  uint16_t ret = 0;
  uint16_t mode = drd16(0x4b80);
  if (mode == 4) {
    if (drd8(0x4b82) & 1) mode = 1;
    else mode = (drd8(0x4b82) & 2) ? 2 : 0;
  }
  GetClientRect(hwnd, MK_FP(rt_ss, (uint16_t)(rt_bp - 0xa)));
  int16_t left = BP_LOCAL(int16_t, -0xa);
  int16_t top = BP_LOCAL(int16_t, -8);
  int16_t right = BP_LOCAL(int16_t, -6);
  int16_t bottom = BP_LOCAL(int16_t, -4);
  int16_t h = (int16_t)drd16(0x267a);
  top = S16(top + 2 * h);
  if (S16(top - 5) > y && y > 4 && S16(right - 4) > x && S16(right - top) < x) {
    if (several_things_at_place() != 0) { ret = 9; goto done; }
  }
  if (mode != 2) {
    bool boxes = false;
    if (mode == 8) {
      uint16_t o = (uint16_t)(drd16(0x4b8a) * 0x12);
      if ((rd8(drd16(DS_MO_SEG), (uint16_t)(drd16(DS_MO_OFF) + o + 1)) & 0xe0) != 0x40) boxes = true;
    }
    if (!boxes) {
      if (drd16(0x4b84) == 0xffff) { ret = 0; goto done; }
      uint16_t rec = CALL_ORIG(7, 0x0358, FAR_CDECL, 2, drd16(0x4b84), (uint16_t)(rt_bp - 0x42)) & 0xffff;
      if (rec == 0) { ret = 0; goto done; }
      uint16_t owner = BP_LOCAL(uint16_t, -0x40);
      uint8_t flags = BP_LOCAL(uint8_t, -0x3d);
      top = S16(top - 4);
      int16_t n = S16(idiv16(S16(bottom - 4 * h - top - 2), 6, 0x2920) + 1) & (int16_t)0xfffe;
      int16_t di = S16(2 * h + top);
      if (di > y) {
        if (top >= y) { ret = 0; goto done; }
        if (S16(right - 0x18) > x) {
          if (owner != drd16(0x014c) && idiv16(S16(right * 3), 5, 0x295e) < x && S16(di - h) <= y && owner != 0xffff) {
            ret = 0xa;
            goto done;
          }
          ret = 0xc;
          goto done;
        }
        ret = (flags & 2) ? 0xd : 0;
        goto done;
      }
      if (S16(di + 3 * n) > y) {
        ret = (uint16_t)(idiv16(S16(y - di), n, 0x29a8) + 6);
        goto done;
      }
      di = S16(di + S16(h >> 1) + 3 * n + 1);
      if (S16(di + 3 * n) > y) {
        ret = (uint16_t)(idiv16(S16(y - di), n, 0x29cd) + 1);
        goto done;
      }
      ret = 5;
      goto done;
    }
  }
  /* 0x29dc: modes 2 and 8 */
  if (!(S16(left + 0x19) > x) && S16(left + 0x39) > x && !(S16(top + 0x47) > y) && S16(top + 0x67) > y) {
    ret = 0xa;
    goto done;
  }
  if (mode == 8 && (rd8(drd16(DS_MO_SEG), (uint16_t)(drd16(DS_MO_OFF) + (uint16_t)(drd16(0x4b8a) * 0x12) + 1)) & 0xe0) == 0 &&
      !(S16(left + 9) > x) && S16(left + 0x49) > x && !(S16(top + 9) > y) && S16(top + 0x49) > y) {
    ret = 0xe;
    goto done;
  }
  if (mode == 8) { ret = 0; goto done; }
  if (!(S16(left + 9) > x) && S16(left + 0x49) > x && !(S16(top + 9) > y) && S16(top + 0x49) > y) { ret = 0xb; goto done; }
  if (!(S16(left + 0x5c) > x) && !(S16(top + 4) > y) && S16(top + h + 7) > y) { ret = 0xb; goto done; }
  ret = 0;
done:
  FRAME_LEAVE();
  return ret;
}

/* strlen of the DGROUP string at off (REPNE SCASB). */
static uint16_t ds_strlen(uint16_t off)
{
  uint16_t n = 0;
  while (drd8((uint16_t)(off + n)) != 0) n = (uint16_t)(n + 1);
  return n;
}

/* invalidate_info_areas: 5:0x0324 (Ghidra FUN_1020_0324, alias fn_05_0324).
 * 335 bytes. ENTER 0x16,0; PUSH DI; PUSH SI. FAR CDECL, no
 * arguments, returns nothing. Measures the label strings (8:0x8df4 returns the near string of a
 * string id: 0x223 five characters wide, 0x224 twenty-one, and the text DS:[0x490]) with the fonts
 * DS:0x292E and DS:0x2930 in the DC of the window DS:0x0154, then invalidates two rectangles of it:
 * the label box (left = right - width of string 0x224) and the list area (RECT at BP-0xE). The
 * frame holds the client RECT at BP-0xE (its API-visible address) and the label RECT at BP-0x16. */
DECOMP(5, 0x0324, invalidate_info_areas, FAR_CDECL)
void invalidate_info_areas(void)
{
  FRAME_ENTER(0x16 + 4);
  uint16_t hwnd = drd16(0x0154);
  GetClientRect(hwnd, MK_FP(rt_ss, (uint16_t)(rt_bp - 0xe)));
  uint16_t dc = GetDC(hwnd);
  uint16_t old = SelectObject(dc, drd16(0x292e));
  uint16_t s1 = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x223);
  uint16_t w1 = (uint16_t)((uint16_t)GetTextExtent(dc, dsfp(s1), 5) + 6);
  BP_LOCAL(uint16_t, -0xa) = (uint16_t)(BP_LOCAL(uint16_t, -0xa) - w1);
  (void)SelectObject(dc, drd16(0x2930));
  uint16_t s2 = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x224);
  uint16_t w2 = (uint16_t)((uint16_t)GetTextExtent(dc, dsfp(s2), 0x15) + 6);
  uint16_t txt = drd16(0x0490);
  uint16_t w3 = (uint16_t)((uint16_t)GetTextExtent(dc, dsfp(txt), (int16_t)ds_strlen(txt)) + 6);
  uint16_t add;
  if ((int16_t)(uint16_t)(w3 << 2) > (int16_t)BP_LOCAL(uint16_t, -0xa)) {
    add = (uint16_t)((uint16_t)GetTextExtent(dc, dsfp(drd16(0x0490)), 4) + 6);
  } else {
    add = w3;
  }
  BP_LOCAL(uint16_t, -0xe) = (uint16_t)(BP_LOCAL(uint16_t, -0xe) + add);
  (void)SelectObject(dc, old);
  ReleaseDC(hwnd, dc);
  uint16_t h = drd16(0x267a);
  BP_LOCAL(uint16_t, -0xc) = (uint16_t)(BP_LOCAL(uint16_t, -0xc) + (uint16_t)((uint16_t)(h - 2) * 2));
  BP_LOCAL(uint16_t, -0x14) = (uint16_t)(BP_LOCAL(uint16_t, -0xc) + 2);
  BP_LOCAL(uint16_t, -0x10) = (uint16_t)(BP_LOCAL(uint16_t, -0x14) + h);
  BP_LOCAL(uint16_t, -0x12) = BP_LOCAL(uint16_t, -0xa);
  BP_LOCAL(uint16_t, -0x16) = (uint16_t)(BP_LOCAL(uint16_t, -0x12) - w2);
  InvalidateRect(hwnd, MK_FP(rt_ss, (uint16_t)(rt_bp - 0x16)), 1);
  uint16_t bot = BP_LOCAL(uint16_t, -8);
  int16_t rows = idiv16(S16(bot - (uint16_t)(h << 2) - BP_LOCAL(uint16_t, -0xc) - 2), 6, 0x0432);
  uint16_t di = (uint16_t)((uint16_t)(rows + 1) & 0xfffe);
  uint16_t t = (uint16_t)(3 * di + (uint16_t)(((int16_t)(uint16_t)(5 * h)) >> 1) + 1);
  BP_LOCAL(uint16_t, -0xc) = (uint16_t)(BP_LOCAL(uint16_t, -0xc) + t);
  BP_LOCAL(uint16_t, -8) = (uint16_t)(3 * di + BP_LOCAL(uint16_t, -0xc));
  InvalidateRect(hwnd, MK_FP(rt_ss, (uint16_t)(rt_bp - 0xe)), 0);
  FRAME_LEAVE();
}

/* REP MOVSW then REP MOVSB (DF clear) of count bytes from sseg:soff to dseg:doff: the word moves read
 * and write in order, so an overlapping copy behaves like the CPU's. */
static void rep_movs(uint16_t dseg, uint16_t doff, uint16_t sseg, uint16_t soff, uint16_t count)
{
  uint16_t words = (uint16_t)(count >> 1);
  while (words != 0) {
    wr16(dseg, doff, rd16(sseg, soff));
    doff = (uint16_t)(doff + 2);
    soff = (uint16_t)(soff + 2);
    words = (uint16_t)(words - 1);
  }
  if (count & 1) wr8(dseg, doff, rd8(sseg, soff));
}
/* strcpy(dst, src) and strcat(dst, src) on DGROUP strings as the compiler's inline code does it: the
 * length of src (with the NUL) is found with REPNE SCASB, then copied. */
static void ds_strcpy(uint16_t dst, uint16_t src)
{
  rep_movs(MEM_SEL_DGROUP, dst, MEM_SEL_DGROUP, src, (uint16_t)(ds_strlen(src) + 1));
}
static void ds_strcat(uint16_t dst, uint16_t src)
{
  rep_movs(MEM_SEL_DGROUP, (uint16_t)(dst + ds_strlen(dst)), MEM_SEL_DGROUP, src, (uint16_t)(ds_strlen(src) + 1));
}

/* update_info_header: 5:0x329a (Ghidra FUN_1020_329a, alias fn_05_329a).
 * 431 bytes. ENTER 0x10,0; PUSH DI; PUSH SI. FAR CDECL: window
 * [BP+6]; returns nothing. Rebuilds the name shown in the list header into DS:0x2816 and the state of
 * the order button DS:0x573c. The mode is DS:0x4B80 (DS:0x4B82 when that is 4); bit 0 shows the
 * planet DS:0x4B84 (name 7:0x1cce + suffix DS:0x4EC), bit 1 the fleet DS:0x4B86 (7:0x1a70 + DS:0x4F5),
 * bit 3 the map object DS:0x4B8A (7:0x1902 + DS:0x4FE); otherwise, or with that index -1, the default
 * DS:0x4E1. The window is invalidated; the order button is sent 0x401 and shown (flags 0x44) or hidden
 * (0x84) at the bottom: shown only for a map object of kind 0 without +0xC set that belongs to the
 * current player (DS:0x14C) whose player record has the byte 5 at +0x4C (28:0x21ee). */
DECOMP(5, 0x329a, update_info_header, FAR_CDECL)
void update_info_header(uint16_t hwnd)
{
  FRAME_ENTER(0x10 + 4);
  uint16_t flag = 0;
  uint16_t src = 0x4e1;
  uint16_t mode = (drd16(0x4b80) == 4) ? drd16(0x4b82) : drd16(0x4b80);
  uint16_t suffix = 0;
  if (mode & 1) {
    if (drd16(0x4b84) != 0xffff) {
      src = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, drd16(0x4b84));
      suffix = 0x4ec;
    }
  } else if (mode & 2) {
    if (drd16(0x4b86) != 0xffff) {
      uint16_t slot = (uint16_t)(drd16(0x4b86) << 2);
      uint32_t fleet = rd32(drd16(0x00bc), (uint16_t)(slot + drd16(0x00ba)));
      src = (uint16_t)CALL_ORIG(7, 0x1a70, FAR_CDECL, 1, rd16(FP_SEG(fleet), FP_OFF(fleet)));
      suffix = 0x4f5;
    }
  } else if (mode & 8) {
    if (drd16(0x4b8a) != 0xffff) {
      flag = 1;
      uint16_t at = (uint16_t)(drd16(DS_MO_OFF) + (uint16_t)(drd16(0x4b8a) * 0x12));
      src = (uint16_t)CALL_ORIG(7, 0x1902, FAR_CDECL, 1, rd16(drd16(DS_MO_SEG), at));
      suffix = 0x4fe;
    }
  }
  if (suffix != 0) ds_strcat(src, suffix);
  ds_strcpy(0x2816, src);
  InvalidateRect(hwnd, 0, 1);
  uint16_t shown = flag;
  if (flag != 0) {
    uint16_t at = (uint16_t)(drd16(DS_MO_OFF) + (uint16_t)(drd16(0x4b8a) * 0x12));
    uint16_t seg = drd16(DS_MO_SEG);
    uint8_t b1 = rd8(seg, (uint16_t)(at + 1));
    shown = 0;
    if ((b1 & 0xe0) == 0 && rd8(seg, (uint16_t)(at + 0xc)) == 0 && (uint16_t)((b1 & 0x1e) >> 1) == drd16(0x014c)) {
      uint16_t player = (uint16_t)((b1 & 0x1e) >> 1);
      if ((uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(0x5a64 + player * 0xc0), 0xe) == 5) shown = 1;
    }
  }
  if (shown != 0) {
    uint16_t at = (uint16_t)(drd16(DS_MO_OFF) + (uint16_t)(drd16(0x4b8a) * 0x12));
    (void)SendMessage(drd16(0x573c), 0x401, rd8(drd16(DS_MO_SEG), (uint16_t)(at + 0xd)) != 0 ? 1 : 0, 0);
  }
  GetClientRect(hwnd, MK_FP(rt_ss, (uint16_t)(rt_bp - 0xc)));
  uint16_t h = drd16(0x267a);
  (void)SetWindowPos(drd16(0x573c), 0, 0x50, S16(0 - (uint16_t)(2 * h - BP_LOCAL(uint16_t, -6))),
                     S16(BP_LOCAL(uint16_t, -8) - 0x58), S16(h + 4), shown == 0 ? 0x84 : 0x44);
  FRAME_LEAVE();
}

/* idiv for the 32-bit divisions of 5:0x3a72 (CDQ; IDIV r/m32): the original traps (#DE) on a zero divisor
 * and on a quotient that does not fit; the C version stops with a message (CONVENTIONS rule b). */
static int32_t idiv32(int32_t num, int32_t den, uint16_t where)
{
  if (den == 0 || (num == INT32_MIN && den == -1)) {
    fprintf(stderr, "5:0x%04x: divide error (IDIV32 %d / %d)\n", where, num, den);
    abort();
  }
  return num / den;
}
static inline int32_t imul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

/* planet_mineral_output: 5:0x3a72 (Ghidra FUN_1020_3a72, alias fn_05_3a72).
 * 1075 bytes. ENTER 0x24,0; PUSH DI; PUSH SI. FAR CDECL: record
 * [BP+6] offset, [BP+8] selector, near pointer out [BP+0xA] (3 dwords in DGROUP), dword d [BP+0xC],
 * flag [BP+0x10]; returns nothing. The record is a planet (id +0, owner +2, flags +5 with bit 2 = no
 * output, three bytes at +6 and at +9, minerals as dwords from +0x1C, population at +0x28; the owner's
 * type byte is read with 28:0x21ee). For each of the three kinds k it computes in out[k] the amount
 * produced from the byte at +9+k with the rate d (d = -1 asks for the rate: 9:0x4b0c, scaled by the
 * owner's type byte 4); when flag is set it also adds the amount to the stock at +0x1C+4k and
 * depletes the concentration at +6+k and the count at +9+k. With d = -1 and an owner of the special
 * type 8 it then adds the output of the fleets on a mining task at the planet (recursive calls with
 * fleet_mining_rate (16:0x193e) of each fleet as d: the sum of its mining robots' rates, capped at
 * 0xfa0). */
DECOMP(5, 0x3a72, planet_mineral_output, FAR_CDECL)
void planet_mineral_output(uint16_t off, uint16_t seg, uint16_t out, uint32_t d, uint16_t flag)
{
  FRAME_ENTER(0x24 + 4);
  uint16_t special = 0;                          /* [BP-0x22] */
  uint16_t explicit_rate = (d != 0xffffffffu);   /* [BP-0x24] */
  int32_t rate, factor;                          /* [BP-0x18], [BP-0x14] */
  uint16_t owner = rd16(seg, (uint16_t)(off + 2));
  if ((int16_t)owner != -1) {
    uint16_t r = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(0x5a64 + owner * 0xc0), 0xe);
    special = (r == 8) ? 1 : 0;
  }
  if (d == 0xffffffffu) {
    if ((int16_t)rd16(seg, (uint16_t)(off + 2)) == -1 || rd32(seg, (uint16_t)(off + 0x28)) == 0) {
      for (uint16_t k = 0; k < 3; k++) dwr32((uint16_t)(out + 4 * k), 0xffffffffu);
      FRAME_LEAVE();
      return;
    }
    rate = (int32_t)(int16_t)(uint16_t)CALL_ORIG(9, 0x4b0c, FAR_CDECL, 2, off, seg);
    if (special == 0) {
      factor = (int32_t)(int16_t)(uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2,
                                                     (uint16_t)(0x5a64 + rd16(seg, (uint16_t)(off + 2)) * 0xc0), 4);
    } else {
      factor = 10;
    }
  } else {
    rate = (int32_t)d;
    factor = 10;
  }
  uint16_t fld = (uint16_t)(off + 0x1c);
  uint16_t outc = out;
  for (uint16_t k = 0; k <= 2; k++) {
    uint32_t cnt = rd8(seg, (uint16_t)(off + 9 + k));                  /* [BP-8] */
    uint16_t si = explicit_rate;
    if (cnt < 0x1e) {
      if ((rd8(seg, (uint16_t)(off + 5)) & 4) != 0 && (si == 0 || special != 0)) cnt = 0x1e;
    }
    int32_t prod = imul32((int32_t)cnt, rate);                          /* [BP-4] */
    int32_t amount;                                                     /* [BP-8] */
    if (si == 0) amount = idiv32(imul32(factor, prod), 10, 0x3bb6);
    else amount = prod;
    int32_t rem = amount - (amount / 100) * 100;                        /* IDIV 100: remainder takes the sign of the dividend */
    int32_t whole = idiv32(amount, 100, 0x3bd1);
    amount = whole;
    if (rem != 0 && (drd8(0x0790) & 2) != 0) {
      uint16_t r = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
      if ((int16_t)r < (int16_t)(uint16_t)rem) amount = (int32_t)((uint32_t)amount + 1);
    }
    dwr32(outc, (uint32_t)amount);
    if (flag != 0) {
      wr32(seg, fld, rd32(seg, fld) + (uint32_t)amount);
      prod = idiv32(prod, 100, 0x3c3a);
      if (prod > 0) {
        for (;;) {
          if (rd8(seg, (uint16_t)(off + 9 + k)) <= 1) break;
          uint32_t conc = rd8(seg, (uint16_t)(off + 6 + k));            /* [BP-0xC] */
          uint16_t count = rd8(seg, (uint16_t)(off + 9 + k));           /* CX */
          int32_t divisor = count;
          if (conc == 0) conc = 0x100;
          if (count > 0x64) divisor = 0x64;
          else if (count < 5) divisor = 0xa;
          else if (count < 0x19) divisor = 0x19;
          int32_t t = idiv32(idiv32(imul32((int32_t)conc, 0x30d4), 0x100, 0x3ce4), divisor, 0x3ce9);   /* [BP-0x10] */
          if (t > prod) {
            int32_t q2 = idiv32(0x30d4, divisor, 0x3d2c);
            t = idiv32(imul32(t - prod, 256), q2, 0x3d42);
            if (t < 1) t = 1;
            if (!(t < (int32_t)conc)) t = (int32_t)(conc - 1);
            wr8(seg, (uint16_t)(off + 6 + k), (uint8_t)t);
            if (t == 0) wr8(seg, (uint16_t)(off + 9 + k), (uint8_t)(rd8(seg, (uint16_t)(off + 9 + k)) - 1));
            break;
          }
          wr8(seg, (uint16_t)(off + 9 + k), (uint8_t)(rd8(seg, (uint16_t)(off + 9 + k)) - 1));
          wr8(seg, (uint16_t)(off + 6 + k), 0);
          prod -= t;
          if (!(prod > 0)) break;
        }
      }
    }
    fld = (uint16_t)(fld + 4);
    outc = (uint16_t)(outc + 4);
  }
  if (special != 0 && !explicit_rate) {
    for (int16_t j = 0; j < (int16_t)drd16(0x553a); j++) {
      uint32_t fp = rd32(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + (uint16_t)(4 * j)));
      if (fp == 0) break;
      uint16_t fdi = (uint16_t)fp, fseg = (uint16_t)(fp >> 16);
      if (rd16(fseg, (uint16_t)(fdi + 6)) != rd16(seg, off)) continue;
      if (rd16(fseg, (uint16_t)(fdi + 2)) != rd16(seg, (uint16_t)(off + 2))) continue;
      if ((rd8(fseg, (uint16_t)(fdi + 5)) & 4) != 0) continue;
      if ((int16_t)rd16(fseg, (uint16_t)(fdi + 0x62)) > 1) continue;
      uint32_t wp = rd32(fseg, (uint16_t)(fdi + 0x64));
      if ((rd8((uint16_t)(wp >> 16), (uint16_t)((uint16_t)wp + 0xa)) & 0xf) != 3) continue;
      const uint16_t wargs[2] = {fdi, fseg};
      d = rt_call_far(MK_FP(SEG_SEL(16), 0x193e), FAR_CDECL, wargs, 2);
      if ((int16_t)(uint16_t)(d >> 16) < 0) continue;
      if (d == 0) continue;
      const uint16_t rargs[6] = {off, seg, (uint16_t)(rt_bp - 0x18), (uint16_t)d, (uint16_t)(d >> 16), flag};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3a72), FAR_CDECL, rargs, 6);
      for (uint16_t k = 0; k < 3; k++) {
        dwr32((uint16_t)(out + 4 * k), drd32((uint16_t)(out + 4 * k)) + rd32(rt_ss, (uint16_t)(rt_bp - 0x18 + 4 * k)));
      }
      if (flag != 0) wr8(fseg, (uint16_t)(fdi + 5), (uint8_t)(rd8(fseg, (uint16_t)(fdi + 5)) & 0xdf));
    }
  }
  FRAME_LEAVE();
}

/* The item array of the pop-up menu of 5:0x3774 lives in the frame at BP-0x1B6: 4 bytes per item,
 * an id word and a flag word (0x8000 a fleet, 0x2000 a map object, a planet has 0 or 0xFFFF, a
 * separator is -1 in both). */
#define MENU_ITEM(i) ((uint16_t)(rt_bp - 0x1b6 + 4 * (uint16_t)(i)))

/* popup_things_at_place: 5:0x3774 (Ghidra FUN_1020_3774, alias fn_05_3774).
 * 766 bytes. ENTER 0x1BA,0; PUSH DI; PUSH SI. FAR CDECL: window
 * [BP+6], screen point (dword) [BP+8]; returns nothing. Lists the objects at the place DS:0x4B7C
 * (a dword key): the selected planet DS:0x4B84 (item, then a separator), the fleets whose dword at +8
 * equals the key (at most up to the frame limit BP-0x26), a separator and the map objects whose
 * dword at +2 equals the key; shows them with the pop-up 24:0x0e24 (the current selection DS:0x4B80
 * 1 = the planet, 2 = fleet DS:0x4B86, 8 = map object DS:0x4B8A is preselected) and, for the chosen
 * item, saves DS:0x4B7C..0x4B8B in the frame, calls 11:0x62f2, and when the chosen thing belongs to
 * the current player DS:0x14C selects it through 9:0x2a74 (kind 2 fleet, 8 map object, 1 planet)
 * between two calls of 11:0x4eaa. */
DECOMP(5, 0x3774, popup_things_at_place, FAR_CDECL)
void popup_things_at_place(uint16_t hwnd, uint32_t pt)
{
  FRAME_ENTER(0x1ba + 4);
  uint16_t sel = 0xffff;                            /* [BP-0x12] */
  uint16_t n = 0;                                   /* BX */
  uint16_t last_fleet = 0, last_fleet_seg = 0;      /* [BP-0x16], [BP-0x14] */
  if (drd16(0x4b84) != 0xffff) {
    uint16_t id = drd16(0x4b84);
    wr16(rt_ss, MENU_ITEM(0), id);
    wr16(rt_ss, (uint16_t)(MENU_ITEM(0) + 2), (id & 0x8000) ? 0xffff : 0);
    wr32(rt_ss, MENU_ITEM(1), 0xffffffffu);
    n = 2;
    if (drd16(0x4b80) == 1) sel = 0;
  }
  if ((int16_t)drd16(0x553a) > 0) {
    uint16_t at = MENU_ITEM(n);
    uint16_t cur = drd16(0x00ba), curseg = drd16(0x00bc);
    uint16_t j = 0;
    uint16_t di = 0;
    for (;;) {
      uint32_t fp = rd32(curseg, cur);
      di = (uint16_t)fp;
      uint16_t fseg = (uint16_t)(fp >> 16);
      last_fleet_seg = fseg;
      if (fp == 0) break;
      if (rd32(fseg, (uint16_t)(di + 8)) == drd32(0x4b7c)) {
        uint16_t id = rd16(fseg, di);
        if (drd16(0x4b80) == 2) {
          uint32_t other = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(drd16(0x4b86) << 2) + drd16(0x00ba)));
          if (rd16((uint16_t)(other >> 16), (uint16_t)other) == id) sel = n;
        }
        wr16(rt_ss, at, id);
        wr16(rt_ss, (uint16_t)(at + 2), (id & 0x8000) ? 0xffff : 0x8000);
        at = (uint16_t)(at + 4);
        n = (uint16_t)(n + 1);
        if (at >= (uint16_t)(rt_bp - 0x26)) break;
      }
      cur = (uint16_t)(cur + 4);
      j = (uint16_t)(j + 1);
      if (!((int16_t)j < (int16_t)drd16(0x553a))) break;
    }
    last_fleet = di;
  }
  if (n == 2 && drd16(0x4b84) != 0xffff) n = 1;
  uint16_t separator = (n == 0) ? 1 : 0;           /* [BP-4] */
  {
    uint16_t base = drd16(DS_MO_OFF), mseg = drd16(DS_MO_SEG);
    uint16_t end = mo_end();
    uint16_t di = base;
    if (end > di) {
      do {
        if (rd32(mseg, (uint16_t)(di + 2)) == drd32(0x4b7c)) {
          if (separator == 0) {
            wr32(rt_ss, MENU_ITEM(n), 0xffffffffu);
            n = (uint16_t)(n + 1);
            separator = 1;
          }
          if (drd16(0x4b80) == 8) {
            if (idiv16(S16(di - base), 0x12, 0x38af) == (int16_t)drd16(0x4b8a)) sel = n;
          }
          wr16(rt_ss, MENU_ITEM(n), rd16(mseg, di));
          wr16(rt_ss, (uint16_t)(MENU_ITEM(n) + 2), 0x2000);
          n = (uint16_t)(n + 1);
        }
        di = (uint16_t)(di + 0x12);
      } while (di < end);
    }
  }
  int16_t chosen = (int16_t)(uint16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, hwnd, (uint16_t)pt, (uint16_t)(pt >> 16), n,
                                                (uint16_t)(rt_bp - 0x1b6), 0, sel, 1);
  if (chosen < 0) {
    FRAME_LEAVE();
    return;
  }
  rep_movs(rt_ss, (uint16_t)(rt_bp - 0x26), MEM_SEL_DGROUP, 0x4b7c, 16);
  uint32_t item = rd32(rt_ss, MENU_ITEM((uint16_t)chosen));
  uint16_t kind, si;
  bool mine;
  if ((item >> 16) & 0x8000) {
    kind = 2;
    wr16(rt_ss, (uint16_t)(rt_bp - 0x22), 2);        /* the copy at BP-0x26 becomes the new selection */
    uint16_t id = (uint16_t)item;
    uint16_t di = 0, dseg = 0;
    uint16_t cx = 0;
    if ((int16_t)drd16(0x553a) > 0) {
      uint16_t bx = drd16(0x00ba), es = drd16(0x00bc);
      for (;;) {
        uint32_t fp = rd32(es, bx);
        di = (uint16_t)fp;
        dseg = (uint16_t)(fp >> 16);
        if ((dseg | di) == 0) break;
        if (rd16(dseg, di) == id) break;
        bx = (uint16_t)(bx + 4);
        cx = (uint16_t)(cx + 1);
        if (!((int16_t)drd16(0x553a) > (int16_t)cx)) break;
      }
    } else {
      di = last_fleet;
      dseg = last_fleet_seg;
    }
    wr16(rt_ss, (uint16_t)(rt_bp - 0x1c), cx);
    si = id;
    mine = rd16(dseg, (uint16_t)(di + 2)) == drd16(0x014c);
  } else if (((item >> 16) & 0x2000) != 0) {
    kind = 8;
    wr16(rt_ss, (uint16_t)(rt_bp - 0x22), 8);
    uint16_t bx = drd16(DS_MO_OFF), mseg = drd16(DS_MO_SEG);
    uint16_t end = mo_end();
    if (end > bx) {
      do {
        if (rd16(mseg, bx) == (uint16_t)item) break;
        bx = (uint16_t)(bx + 0x12);
      } while (bx < end);
    }
    wr16(rt_ss, (uint16_t)(rt_bp - 0x18), (uint16_t)idiv16(S16(bx - drd16(DS_MO_OFF)), 0x12, 0x39e5));
    si = rd16(mseg, bx);
    mine = false;
  } else {
    kind = 1;
    wr16(rt_ss, (uint16_t)(rt_bp - 0x22), 1);
    si = drd16(0x4b84);
    uint32_t rec = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, si);
    mine = rd16((uint16_t)(rec >> 16), (uint16_t)((uint16_t)rec + 2)) == drd16(0x014c);
  }
  (void)CALL_ORIG(11, 0x62f2, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x26), 2);
  if (mine) {
    (void)CALL_ORIG(11, 0x4eaa, FAR_CDECL, 2, 0, 0);
    (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, kind, si);
    (void)CALL_ORIG(11, 0x4eaa, FAR_CDECL, 2, 0, 1);
  }
  FRAME_LEAVE();
}

/* strlen of a string in another segment. */
static uint16_t strlen_far(uint16_t seg, uint16_t off)
{
  uint16_t n = 0;
  while (rd8(seg, (uint16_t)(off + n)) != 0) n = (uint16_t)(n + 1);
  return n;
}

/* Frame (BP-relative) accessors for 5:0x2ab4. */
#define LB(o) BP_LOCAL(uint8_t, (o))
#define LW(o) BP_LOCAL(uint16_t, (o))
#define LD(o) BP_LOCAL(uint32_t, (o))
#define BPN(o) ((uint16_t)(rt_bp + (o)))

/* The descriptor DS:0x0BC0 of the clicked thing is shown with 24:0x091e (window DS:0x0154, the point). */
static void show_descriptor(uint32_t pt)
{
  (void)CALL_ORIG(24, 0x091e, FAR_CDECL, 3, drd16(0x0154), (uint16_t)pt, (uint16_t)(pt >> 16));
}

/* _wsprintf with one variable word, as in 13:0x0fb8: the argument sits below the frame. */
static int16_t wsprintf_w(uint16_t out_seg, uint16_t out, uint16_t fmt, uint16_t arg)
{
  uint16_t at = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, at, arg);
  return _wsprintf(MK_FP(out_seg, out), dsfp(fmt), MK_FP(rt_ss, at));
}

/* info_on_click: 5:0x2ab4 (Ghidra FUN_1020_2ab4, alias fn_05_2ab4).
 * 2022 bytes (the inventory counted only 60). ENTER 0x254,0; PUSH DI;
 * PUSH SI. Jump table at 5:0x2af0, 14 words. FAR CDECL: point (dword) [BP+6], message [BP+0xA], a
 * word [BP+0xC] that is not used; returns nothing. A mouse click (WM_LBUTTONDOWN 0x201, or the right
 * button 0x204) in the window DS:0x0154: the zone under the point is 5:0x2840 (zones 4 and 0 do
 * nothing; the right button acts only in zones 9 and 11). The zone fills the descriptor at
 * DS:0x0BC0 (kind, ids, numbers) that 24:0x091e shows: zones 1 to 3 the mineral n of the selected
 * planet DS:0x4B84 (the amount and what the planet and the mining fleets yield, 5:0x3a72); 5 a list
 * of 9 zoom values chosen with the pop-up 24:0x0e24 (stored in DS:0x04C8); 6 to 8 the habitability
 * of the planet (9:0x5220); 9 cycle the selection to the next fleet / map object at the same place
 * (right button: the pop-up 5:0x3774); 10 the owner of the selection; 11 the designs of the fleet
 * (right button: choose from a pop-up); 12 the planet; 13 its star base design; 14 the component of
 * the selected map object (2:0x50f8). The frame holds the planet record at BP-0x60 (BP-0x4E for
 * zones 6 to 8), the output dwords of 5:0x3a72 at BP-0x1C / BP-0x28, the value and string lists
 * of zone 5, the design name buffers of zone 11 and the 16-byte copy of the selection of zone 9. */
DECOMP(5, 0x2ab4, info_on_click, FAR_CDECL)
void info_on_click(uint32_t pt, uint16_t msg, uint16_t wparam)
{
  FRAME_ENTER(0x254 + 4);
  (void)wparam;
  const uint16_t zargs[3] = {drd16(0x0154), (uint16_t)pt, (uint16_t)(pt >> 16)};
  uint16_t zone = (uint16_t)rt_call_far(MK_FP(SEG_SEL(5), 0x2840), FAR_CDECL, zargs, 3);
  if (msg == 0x204 && zone != 9 && zone != 0xb) goto done;
  if ((uint16_t)(zone - 1) > 0xd) goto done;
  switch (zone) {
  case 1:
  case 2:
  case 3: {
    (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, drd16(0x4b84), BPN(-0x60));
    dwr16(0xbc0, 1);
    dwr16(0xbc2, (uint16_t)(zone - 1));
    dwr16(0xbc4, 0);
    for (uint16_t k = 0; k < 4; k++) dwr32((uint16_t)(0xbc6 + 4 * k), 0xffffffffu);
    if (LB(-0x5c) < 3) break;
    dwr16(0xbce, LB((int16_t)(-0x58 + zone)));
    dwr16(0xbd0, 0);
    dwr16(0xbc6, (uint16_t)((LB(-0x5b) & 4) >> 2));
    dwr16(0xbc8, 0);
    if (LB(-0x5c) <= 3) break;
    LD(-0xe) = 0;
    dwr32(0xbca, LD((int16_t)(-0x48 + 4 * zone)));
    {
      const uint16_t a[6] = {BPN(-0x60), rt_ss, BPN(-0x1c), 0xffff, 0xffff, 0};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3a72), FAR_CDECL, a, 6);
    }
    dwr32(0xbd2, LD((int16_t)(-0x20 + 4 * zone)));
    if ((int16_t)LW(-0x5e) != -1) break;
    LW(-0x10) = zone;
    {
      uint16_t di = 0, si = 0;
      if ((int16_t)drd16(0x553a) > 0) {
        for (;;) {
          LW(-0xa) = 0;
          uint32_t fp = rd32(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + si));
          LD(-4) = fp;
          if (fp == 0) break;
          uint16_t fo = (uint16_t)fp, fs = (uint16_t)(fp >> 16);
          if (rd16(fs, (uint16_t)(fo + 6)) == LW(-0x60) && rd16(fs, (uint16_t)(fo + 2)) == drd16(0x014c) &&
              (rd8(fs, (uint16_t)(fo + 5)) & 4) == 0) {
            uint32_t wp = rd32(fs, (uint16_t)(fo + 0x64));
            if ((rd8((uint16_t)(wp >> 16), (uint16_t)((uint16_t)wp + 0xa)) & 0xf) == 3) {
              const uint16_t wa[2] = {fo, fs};
              uint32_t w = rt_call_far(MK_FP(SEG_SEL(16), 0x193e), FAR_CDECL, wa, 2);
              LW(-8) = (uint16_t)w;
              LW(-6) = (uint16_t)(w >> 16);
              if (!((int16_t)(w >> 16) < 0) && w != 0) {
                const uint16_t a[6] = {BPN(-0x60), rt_ss, BPN(-0x28), (uint16_t)w, (uint16_t)(w >> 16), 0};
                (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3a72), FAR_CDECL, a, 6);
                LD(-0xe) = LD(-0xe) + LD((int16_t)(-0x2c + 4 * (int16_t)LW(-0x10)));
              }
            }
          }
          si = (uint16_t)(si + 4);
          di = (uint16_t)(di + 1);
          if (!((int16_t)di < (int16_t)drd16(0x553a))) break;
        }
      }
      if ((int32_t)LD(-0xe) <= 0) break;
      dwr32(0xbd2, LD(-0xe));
    }
    break;
  }
  case 4:
    goto done;
  case 5: {
    LW(-6) = 0xffff;
    LW(-0x18) = 0x0064; LW(-0x16) = 0x01f4; LW(-0x14) = 0x03e8; LW(-0x12) = 0x09c4;
    LW(-0x10) = 0x1388; LW(-0x0e) = 0x1d4c; LW(-0x0c) = 0x2710; LW(-0x0a) = 0x4e20; LW(-8) = 0x7530;
    uint16_t si = 0, di = 0;
    LW(-2) = BPN(-0x84);
    do {
      (void)wsprintf_w(rt_ss, LW(-2), drd16(0x0180), LW((int16_t)(-0x18 + si)));
      LW((int16_t)(-0x2a + si)) = LW(-2);
      if (LW((int16_t)(-0x18 + si)) == drd16(0x04c8)) LW(-6) = di;
      si = (uint16_t)(si + 2);
      di = (uint16_t)(di + 1);
      LW(-2) = (uint16_t)(LW(-2) + 0xa);
    } while (LW(-2) < BPN(-0x2a));
    int16_t chosen = (int16_t)(uint16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, drd16(0x0154), (uint16_t)pt, (uint16_t)(pt >> 16), 9, 0,
                                                  BPN(-0x2a), LW(-6), 1);
    LW(-4) = (uint16_t)chosen;
    if (chosen == -1) goto done;
    LW(-0x254) = LW((int16_t)(-0x18 + 2 * (uint16_t)chosen));
    if (LW(-0x254) == drd16(0x04c8)) goto done;
    dwr16(0x04c8, LW(-0x254));
    InvalidateRect(drd16(0x0154), 0, 1);
    if ((drd8(0x055e) & 0xf) != 1) goto done;
    InvalidateRect(drd16(0x0150), 0, 1);
    goto done;
  }
  case 6:
  case 7:
  case 8: {
    (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, drd16(0x4b84), BPN(-0x4e));
    dwr16(0xbc0, 5);
    dwr16(0xbc2, LW(-0x4e));
    uint16_t k = (uint16_t)(zone - 6);
    dwr16(0xbc4, k);
    dwr16(0xbc6, LB(-0x4a) < 3 ? 0xffff : (uint16_t)(int16_t)(int8_t)LB((int16_t)(-0x42 + k)));
    bool ok = false;
    if (LB(-0x4a) >= 3) {
      uint16_t r = (uint16_t)CALL_ORIG(9, 0x5220, FAR_CDECL, 6, BPN(-0x4e), rt_ss, BPN(-0xc), BPN(-6), BPN(-0x16), 1);
      if (r != 0) {
        dwr16(0xbc8, LW((int16_t)(-0xc + 2 * k)));
        dwr16(0xbca, LW((int16_t)(-6 + 2 * k)));
        if (drd16(0xbc8) == 0xffff) dwr16(0xbc8, drd16(0xbc6));
        if (drd16(0xbca) == 0xffff) dwr16(0xbca, drd16(0xbc6));
        ok = drd16(0xbc8) != drd16(0xbca);
      }
    }
    if (!ok) {
      dwr16(0xbc8, 0xffff);
      dwr16(0xbca, 0xffff);
    }
    {
      uint16_t si = (uint16_t)(drd16(0x014c) * 0xc0);
      uint16_t bx = drd16(0xbc4);
      dwr16(0xbcc, (uint16_t)(int16_t)(int8_t)drd8((uint16_t)(bx + si + 0x5a74)));
      dwr16(0xbce, (uint16_t)(int16_t)(int8_t)drd8((uint16_t)(bx + si + 0x5a77)));
      dwr16(0xbd0, (uint16_t)(int16_t)(int8_t)drd8((uint16_t)(bx + si + 0x5a7a)));
    }
    break;
  }
  case 9: {
    if (msg == 0x204) {
      const uint16_t pa[3] = {drd16(0x0154), (uint16_t)pt, (uint16_t)(pt >> 16)};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3774), FAR_CDECL, pa, 3);
      goto done;
    }
    /* The selection is copied to BP-0x1A (key dword, then the words DS:0x4B80 kind -> BP-0x16, 0x4B82
     * -> BP-0x14, 0x4B84 -> BP-0x12, 0x4B86 -> BP-0x10, 0x4B88 -> BP-0xE, 0x4B8A -> BP-0xC) and moved to the
     * next thing at the same place: the next fleet, else the other kinds in the order the mask
     * DS:0x4B82 allows (bit 0 planet, bit 1 fleet, bit 3 map object). Labels are the original's offsets. */
    uint16_t cx = 0, di = 0, bx = 0;
    rep_movs(rt_ss, BPN(-0x1a), MEM_SEL_DGROUP, 0x4b7c, 16);
    LW(-0xe) = 0;
    if (LW(-0x16) == 8) {
      bx = LW(-0xc);
      goto l_2f3b;
    }
l_2e2e:
    cx = (LW(-0x16) == 2) ? (uint16_t)(LW(-0x10) + 1) : 0;
    {
      uint16_t slot = (uint16_t)((uint16_t)(cx << 2) + drd16(0x00ba));
      if (!((int16_t)cx >= (int16_t)drd16(0x553a))) {
        for (;;) {
          uint32_t fp = rd32(drd16(0x00bc), slot);
          di = (uint16_t)fp;
          LW(-4) = (uint16_t)(fp >> 16);
          if (fp == 0) break;
          if (rd32((uint16_t)(fp >> 16), (uint16_t)(di + 8)) == LD(-0x1a)) break;
          slot = (uint16_t)(slot + 4);
          cx = (uint16_t)(cx + 1);
          if (!((int16_t)cx < (int16_t)drd16(0x553a))) break;
        }
      }
    }
    if ((int16_t)drd16(0x553a) > (int16_t)cx) {      /* 2e85: found the fleet with number cx */
      LW(-0x10) = cx;
      LW(-0x16) = 2;
      uint32_t fp = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(cx << 2) + drd16(0x00ba)));
      LW(-6) = rd16((uint16_t)(fp >> 16), (uint16_t)fp);
      di = (rd16((uint16_t)(fp >> 16), (uint16_t)((uint16_t)fp + 2)) == drd16(0x014c)) ? 1 : 0;
      goto l_2f77;
    }
    if (LB(-0x14) & 8) goto l_2f19;
    if (LB(-0x14) & 1) goto l_2fc8;
l_2ec8:
    di = 0;
    if ((int16_t)drd16(0x553a) > 0) {
      uint16_t slot = drd16(0x00ba);
      for (;;) {
        uint32_t fp = rd32(drd16(0x00bc), slot);
        cx = (uint16_t)fp;
        LW(-4) = (uint16_t)(fp >> 16);
        if (fp == 0) break;
        if (rd32((uint16_t)(fp >> 16), (uint16_t)(cx + 8)) == LD(-0x1a)) break;
        slot = (uint16_t)(slot + 4);
        di = (uint16_t)(di + 1);
        if (!((int16_t)di < (int16_t)drd16(0x553a))) break;
      }
    }
    if (drd16(0x553a) != di || (LB(-0x14) & 8) == 0) {   /* 2ff6: also taken when no fleet was found */
      LW(-2) = di;
      LW(-0x16) = 2;
      LW(-0x10) = di;
      uint32_t fp = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(di << 2) + drd16(0x00ba)));
      LW(-6) = rd16((uint16_t)(fp >> 16), (uint16_t)fp);
      di = (rd16((uint16_t)(fp >> 16), (uint16_t)((uint16_t)fp + 2)) == drd16(0x014c)) ? 1 : 0;
      goto l_2f77;
    }
l_2f19:
    bx = 0;
    goto l_2f3c;
l_2f3b:
    bx = (uint16_t)(bx + 1);
l_2f3c:
    while ((int16_t)drd16(DS_MO_COUNT) > (int16_t)bx) {
      uint16_t at = (uint16_t)((uint16_t)(bx * 0x12) + drd16(DS_MO_OFF));
      if (rd16(drd16(DS_MO_SEG), (uint16_t)(at + 2)) == LW(-0x1a) && rd16(drd16(DS_MO_SEG), (uint16_t)(at + 4)) == LW(-0x18)) break;
      bx = (uint16_t)(bx + 1);
    }
    if ((int16_t)drd16(DS_MO_COUNT) > (int16_t)bx) {  /* 2f60: found the map object bx */
      LW(-0xc) = bx;
      LW(-0x16) = 8;
      LW(-6) = rd16(drd16(DS_MO_SEG), (uint16_t)((uint16_t)(bx * 0x12) + drd16(DS_MO_OFF)));
      di = 0;
      goto l_2f77;
    }
    if (LB(-0x14) & 1) goto l_2fc8;
    if (LB(-0x14) & 2) goto l_2ec8;
    if (LB(-0x14) & 8) goto l_2f19;
    goto l_2e2e;
l_2fc8:
    LW(-0x16) = 1;
    LW(-6) = LW(-0x12);
    {
      uint32_t rec = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, LW(-6));
      di = (rec != 0 && rd16((uint16_t)(rec >> 16), (uint16_t)((uint16_t)rec + 2)) == drd16(0x014c)) ? 1 : 0;
    }
l_2f77:
    if (di == 0 || drd16(0x4b74) != 2) LW(-0xe) = drd16(0x4b88);
    (void)CALL_ORIG(11, 0x62f2, FAR_CDECL, 2, BPN(-0x1a), 2);
    if (di == 0) goto done;
    (void)CALL_ORIG(11, 0x4eaa, FAR_CDECL, 2, 0, 0);
    (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, LW(-0x16), LW(-6));
    (void)CALL_ORIG(11, 0x4eaa, FAR_CDECL, 2, 0, 1);
    goto done;
  }
  case 10: {
    uint16_t player;
    if (drd16(0x4b80) == 1) {
      uint32_t rec = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, drd16(0x4b84));
      LW(-2) = (uint16_t)(rec >> 16);
      player = rd16((uint16_t)(rec >> 16), (uint16_t)((uint16_t)rec + 2));
    } else if (drd16(0x4b80) == 8) {
      uint16_t at = (uint16_t)((uint16_t)(drd16(0x4b8a) * 0x12) + drd16(DS_MO_OFF));
      player = (uint16_t)((rd8(drd16(DS_MO_SEG), (uint16_t)(at + 1)) & 0x1e) >> 1);
    } else {
      uint32_t fp = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(drd16(0x4b86) << 2) + drd16(0x00ba)));
      player = (uint16_t)((rd8((uint16_t)(fp >> 16), (uint16_t)((uint16_t)fp + 1)) & 0x1e) >> 1);
    }
    dwr16(0xbc2, player);
    dwr16(0xbc0, 2);
    break;
  }
  case 11: {
    if (msg == 0x204) {
      uint32_t fleet = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(drd16(0x4b86) << 2) + drd16(0x00ba)));
      uint16_t fo = (uint16_t)fleet, fs = (uint16_t)(fleet >> 16);
      LD(-0x12) = fleet;
      LW(-0xe) = 0;
      LD(-4) = 0;
      LW(-8) = 0;
      LW(-6) = BPN(-0x252);
      LW(-0xc) = (uint16_t)(fo + 0xc);
      LW(-0xa) = fs;
      LW(-2) = 0;
      for (;;) {
        if (!((int16_t)rd16(LW(-0xa), LW(-0xc)) <= 0)) {
          uint16_t at = LW(-4);
          LW((int16_t)(-0x32 + at)) = LW(-2);
          uint16_t owner = rd16(fs, (uint16_t)(fo + 2));
          uint16_t slot = (uint16_t)(owner << 2);
          uint16_t dofs = (uint16_t)(drd16((uint16_t)(slot + 0xbe)) + LW(-8) + 8);
          uint16_t dseg = drd16((uint16_t)(slot + 0xc0));
          rep_movs(rt_ss, LW(-6), dseg, dofs, (uint16_t)(strlen_far(dseg, dofs) + 1));
          LW((int16_t)(-0x52 + at)) = LW(-6);
          LW(-4) = (uint16_t)(LW(-4) + 2);
          LW(-6) = (uint16_t)(LW(-6) + 0x20);
          LW(-0xe) = (uint16_t)(LW(-0xe) + 1);
        }
        LW(-0xc) = (uint16_t)(LW(-0xc) + 2);
        LW(-2) = (uint16_t)(LW(-2) + 1);
        LW(-8) = (uint16_t)(LW(-8) + 0x8d);
        if (!((int16_t)LW(-8) < 0x8d0)) break;
      }
      uint16_t si = LW(-0xe);
      if ((int16_t)si > 1) {
        int16_t chosen = (int16_t)(uint16_t)CALL_ORIG(24, 0x0e24, FAR_CDECL, 8, drd16(0x0154), (uint16_t)pt, (uint16_t)(pt >> 16),
                                                      si, 0, BPN(-0x52), 0xffff, 1);
        si = (uint16_t)chosen;
        if (chosen == -1) goto done;
      } else {
        si = 0;
      }
      dwr16(0xbc0, 0xb);
      uint16_t owner = rd16(fs, (uint16_t)(fo + 2));
      uint16_t slot = (uint16_t)(owner << 2);
      dwr16(0xbc2, (uint16_t)(drd16((uint16_t)(slot + 0xbe)) + (uint16_t)(LW((int16_t)(-0x32 + 2 * si)) * 0x8d)));
      dwr16(0xbc4, drd16((uint16_t)(slot + 0xc0)));
      dwr16(0xbc8, owner == drd16(0x014c) ? 0 : 1);
      dwr16(0xbc6, 0);
      dwr16(0xbca, 0);
      dwr16(0xbcc, 1);
      break;
    }
    dwr16(0xbc0, 3);
    {
      uint32_t fp = rd32(drd16(0x00bc), (uint16_t)((uint16_t)(drd16(0x4b86) << 2) + drd16(0x00ba)));
      dwr16(0xbc2, (uint16_t)fp);
      dwr16(0xbc4, (uint16_t)(fp >> 16));
      dwr16(0xbc6, rd8((uint16_t)(fp >> 16), (uint16_t)((uint16_t)fp + 4)) == 7 ? 1 : 0);
    }
    break;
  }
  case 12:
    dwr16(0xbc0, 7);
    dwr16(0xbc2, drd16(0x4b84));
    break;
  case 13: {
    dwr16(0xbc0, 0xb);
    uint32_t rec = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, drd16(0x4b84));
    uint16_t ro = (uint16_t)rec, rs = (uint16_t)(rec >> 16);
    LW(-2) = rs;
    uint16_t idx = (uint16_t)((rd8(rs, (uint16_t)(ro + 0x2c)) & 0xf) * 0x8d);
    uint16_t owner = rd16(rs, (uint16_t)(ro + 2));
    uint32_t sb = drd32((uint16_t)((uint16_t)(owner << 2) + 0x10c));
    dwr32(0xbc2, (sb & 0xffff0000u) | (uint16_t)((uint16_t)sb + idx));
    dwr16(0xbc8, owner == drd16(0x014c) ? 0 : 1);
    dwr16(0xbc6, 1);
    dwr16(0xbca, 0);
    dwr16(0xbcc, 1);
    break;
  }
  case 14: {
    uint16_t at = (uint16_t)(drd16(0x4b8a) * 0x12);
    uint16_t item = rd8(drd16(DS_MO_SEG), (uint16_t)(drd16(DS_MO_OFF) + at + 0xc));
    LW(-6) = (uint16_t)(drd8((uint16_t)(item + 0x4de)) | (LB(-5) << 8));
    LW(-8) = 0x100;
    (void)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, (const uint16_t[]){BPN(-8)}, 1);
    dwr16(0xbc0, 9);
    dwr32(0xbc2, LD(-8));
    dwr32(0xbc6, LD(-4));
    break;
  }
  default:
    break;
  }
  show_descriptor(pt);
done:
  FRAME_LEAVE();
}

/* ---- 5:0x04e8: the information panel of the selected thing ------------------------------------------
 * Helpers of paint_info_panel. They are plain C functions that work in the frame of paint_info_panel (rt_bp is
 * its BP): the text buffer at BP-0xF8, a RECT at BP-0x4C and so on. PUSHW and POPW mirror the words
 * the original leaves on the stack around a call (the stack addresses its callees see). */
#define PAT 0x00f00021u
#define I16(e) ((int16_t)(uint16_t)(e))
#define U16(e) ((uint16_t)(e))
#define PB(x, y, w, h) (void)PatBlt(hdc, I16(x), I16(y), (int16_t)(w), (int16_t)(h), PAT)
#define TXTBUF BPN(-0xf8)
#define DSP(off) MK_FP(MEM_SEL_DGROUP, (uint16_t)(off))
#define SSP(off) MK_FP(rt_ss, (uint16_t)(off))

static inline void pushw(uint16_t v)
{
  rt_sp = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, rt_sp, v);
}
static inline void popw(uint16_t n) { rt_sp = (uint16_t)(rt_sp + 2 * n); }

/* _wsprintf with up to six variable words, which the original has pushed below the format pointer;
 * the by-name wrapper takes a far pointer to them. */
static int16_t wsprintf_va(uint32_t out, uint32_t fmt, uint16_t n, const uint16_t *w)
{
  uint16_t at = (uint16_t)(rt_sp - 2 * n);
  for (uint16_t i = 0; i < n; i++) wr16(rt_ss, (uint16_t)(at + 2 * i), w[i]);
  return _wsprintf(out, fmt, MK_FP(rt_ss, at));
}
#define WSP0(out, fmt) wsprintf_va((out), (fmt), 0, NULL)
#define WSP1(out, fmt, a) wsprintf_va((out), (fmt), 1, (const uint16_t[]){(uint16_t)(a)})
#define WSP2(out, fmt, a, b) wsprintf_va((out), (fmt), 2, (const uint16_t[]){(uint16_t)(a), (uint16_t)(b)})
#define WSP3(out, fmt, a, b, c) wsprintf_va((out), (fmt), 3, (const uint16_t[]){(uint16_t)(a), (uint16_t)(b), (uint16_t)(c)})

/* 8:0x8dc0 (string id, destination) copies the string of an id to the destination and returns its length;
 * 8:0x8df4 (id) returns the near string; 8:0x9398 (hdc, x, y, text, length, flag) draws a text line. */
static uint16_t str_copy(uint16_t id, uint16_t dest) { return (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, dest); }
static uint16_t str_near(uint16_t id) { return (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id); }
#define TEXT_LINE(hdc, x, y, text, len, flag) \
  (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, (hdc), U16(x), U16(y), U16(text), U16(len), (flag))
/* The string id the original computes with SBB and AND AL,0xA8 from the bit 3 of DS:0x792: k when it is set,
 * k - 0x58 when it is clear (only the low byte of the mask is applied, so the high byte stays 0xFF). */
static uint16_t id792(uint16_t k) { return (drd8(0x792) & 8) ? k : U16(k - 0x58); }
static uint16_t shl_ax_cl(uint16_t v, uint16_t cl) { return (uint16_t)((uint32_t)v << (cl & 31)); }
/* 8:0x9318 (hdc, rect, text, count) draws centred text; 8:0x9958 (hdc, rect, 0) draws a frame. */

/* The sprite blit 8:0xaa4e (hdc, x, y, w, h, source, xs, ys, w, h, rop low, rop high). */
#define SPRITE(hdc, x, y, w, h, src, xs, ys, w2, h2) \
  (void)CALL_ORIG(8, 0xaa4e, FAR_CDECL, 12, (hdc), U16(x), U16(y), (w), (h), (src), U16(xs), U16(ys), (w2), (h2), 0x0020, 0x00cc)

/* Mode 2 of 5:0x04e8: the selected fleet DS:0x4B86 (0x56a to 0xca8). */
static void panel_fleet(uint16_t hdc, uint16_t rect)
{
  const uint16_t h = drd16(0x267a);
  if (drd16(0x4b86) == 0xffff) return;
  uint16_t si = U16(drd16(rect) + 6), di = U16(drd16((uint16_t)(rect + 2)) + 6);
  (void)SelectObject(hdc, drd16(0x16));
  PB(si, di, 0x46, 2);
  PB(si, di + 2, 2, 0x44);
  PB(si + 0x10, di + 0x44, 2, 0x26);
  uint16_t old_brush = SelectObject(hdc, drd16(0x14));
  PB(si + 2, di + 0x44, 0xf, 1);
  PB(si + 1, di + 0x45, 0xf, 1);
  PB(si + 0x34, di + 0x44, 0x12, 2);
  PB(si + 0x44, di + 2, 2, 0x42);
  PB(si + 0x45, di + 1, 1, 1);
  PB(si + 0x11, di + 0x68, 0x25, 2);
  PB(si + 0x10, di + 0x69, 1, 1);
  PB(si + 0x34, di + 0x46, 2, 0x22);
  (void)PatBlt(hdc, I16(si + 2), I16(di + 2), 0x42, 0x42, 0x42);
  (void)PatBlt(hdc, I16(si + 0x12), I16(di + 0x44), 0x22, 0x24, 0x42);
  (void)SelectObject(hdc, old_brush);
  uint32_t fleet = rd32(drd16(0x00bc), U16((uint16_t)(drd16(0x4b86) << 2) + drd16(0x00ba)));
  uint16_t fo = (uint16_t)fleet, fs = (uint16_t)(fleet >> 16);
  (void)CALL_ORIG(10, 0x2d28, FAR_CDECL, 11, fo, fs, hdc, U16(si + 2), U16(di + 2), 0, 0xffff, 0, 0, 0xffff, 0);
  uint16_t player = U16((rd8(fs, U16(fo + 1)) & 0x1e) >> 1);
  uint16_t icon = U16((drd8(U16(player * 0xc0 + 0x5a6a)) & 0xf8) >> 3);
  (void)SelectPalette(hdc, drd16(0x54f8), 0);
  (void)RealizePalette(hdc);
  SPRITE(hdc, si + 0x13, di + 0x47, 0x20, 0x20, drd16(0x565a), (icon & 7) << 5, 0 - (((icon & 0xf8) << 2) - 0x60), 0x20, 0x20);
  (void)SetTextColor(hdc, drd32(0x2870));
  (void)SetBkColor(hdc, drd32(0x253a));
  (void)SelectObject(hdc, drd16(0x2930));
  int32_t total = 0;
  uint16_t y = di;                                   /* [BP-0x1C] */
  for (uint16_t i = 0; i < 0x10; i++) total += (int32_t)(int16_t)rd16(fs, U16(fo + 0xc + 2 * i));
  (void)str_copy(0x21a, TXTBUF);
  int16_t n = WSP2(DSP(0x5866), SSP(TXTBUF), (uint16_t)total, (uint16_t)((uint32_t)total >> 16));
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  uint32_t value;                                    /* [BP-0x12] */
  uint8_t kind = rd8(fs, U16(fo + 4));
  if (kind == 7 || kind == 4) {
    value = CALL_ORIG(7, 0x4c90, FAR_CDECL, 2, fo, fs);
    uint16_t len1 = str_copy(0x219, TXTBUF);         /* [BP-0xE] */
    uint32_t ext1 = GetTextExtent(hdc, SSP(TXTBUF), (int16_t)len1);
    uint16_t len2 = str_copy(0x43, 0x5866);          /* [BP-0x26] */
    uint32_t ext2 = GetTextExtent(hdc, DSP(0x5866), (int16_t)len2);
    if ((int16_t)(ext2 >> 16) > (int16_t)(ext1 >> 16) ||
        ((int16_t)(ext2 >> 16) == (int16_t)(ext1 >> 16) && (uint16_t)ext2 > (uint16_t)ext1))
      ext1 = ext2;
    uint16_t w = (uint16_t)ext1;                     /* [BP-0xC] */
    SetRect(SSP(BPN(-0x4c)), I16(drd16(rect) + w + 0x5a), I16(y), I16(drd16((uint16_t)(rect + 4)) - 4), I16(y + h));
    if (!((int16_t)LW(-0x48) <= (int16_t)LW(-0x4c))) {
      (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), SSP(TXTBUF), (int16_t)len1);
      y = U16(y + h + 2);
      (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), (int16_t)len2);
      (void)CALL_ORIG(10, 0x2ab0, FAR_CDECL, 5, hdc, BPN(-0x4c), fo, fs, 4);
      (void)CALL_ORIG(8, 0x9700, FAR_CDECL, 3, BPN(-0x4c), 0, U16(h + 4));
      (void)CALL_ORIG(10, 0x2ab0, FAR_CDECL, 5, hdc, BPN(-0x4c), fo, fs, 5);
      y = U16(y + h + 2);
    }
  } else {
    value = rd32(fs, U16(fo + 0x2c));
  }
  /* the line with the value (DS:0x792 bit 3 picks the string id) */
  (void)str_copy(id792(0x26f), TXTBUF);
  n = WSP2(DSP(0x5866), SSP(TXTBUF), (uint16_t)value, (uint16_t)(value >> 16));
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  if (rd8(fs, U16(fo + 4)) != 7) {
    if (!(rd8(fs, U16(fo + 0x76)) & 0x10)) return;
    uint16_t len;
    if (rd8(fs, U16(fo + 0x76)) & 0xf) {
      (void)str_copy(id792(0x26d), TXTBUF);
      len = (uint16_t)WSP1(DSP(0x5866), SSP(TXTBUF), rd8(fs, U16(fo + 0x76)) & 0xf);
    } else {
      len = str_copy(id792(0x26e), 0x5866);
    }
    (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), (int16_t)len);
    return;
  }
  /* a fleet with orders: the waypoint list (header 4 bytes, then 0x12-byte waypoints) */
  uint32_t wp = rd32(fs, U16(fo + 0x64));
  uint16_t wseg = (uint16_t)(wp >> 16);
  di = U16((uint16_t)wp + 4);
  if ((int16_t)rd16(fs, U16(fo + 0x62)) > 1) di = U16(di + 0x12);
  (void)str_copy(id792(0x26c), BPN(-0x44));
  uint16_t target;
  if (rd16(fs, U16(fo + 0x62)) == 1) {
    target = str_near(0x218);
  } else {
    target = (uint16_t)CALL_ORIG(7, 0x25ce, FAR_CDECL, 4, rd8(wseg, U16(di + 7)) & 0xf, rd16(wseg, U16(di + 4)), rd16(wseg, di),
                                 rd16(wseg, U16(di + 2)));
  }
  n = WSP2(SSP(TXTBUF), SSP(BPN(-0x44)), target, MEM_SEL_DGROUP);
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), SSP(TXTBUF), n);
  y = U16(y + h + 2);
  (void)str_copy(id792(0x26b), TXTBUF);
  uint16_t task = str_near(U16((rd8(wseg, U16(di + 6)) & 0xf) + 0x63));
  n = WSP2(DSP(0x5866), SSP(TXTBUF), task, MEM_SEL_DGROUP);
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  uint16_t len;
  if ((int16_t)rd16(fs, U16(fo + 0x62)) > 1) {
    if ((rd8(wseg, U16(di + 6)) & 0xf0) == 0xb0) {
      len = str_copy(0x383, 0x5866);
    } else {
      (void)str_copy(id792(0x26d), TXTBUF);
      len = (uint16_t)WSP1(DSP(0x5866), SSP(TXTBUF), (rd8(wseg, U16(di + 6)) & 0xf0) >> 4);
    }
  } else {
    len = str_copy(id792(0x26e), 0x5866);
  }
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), (int16_t)len);
  y = U16(y + h + 2);
  const uint16_t fa[2] = {fo, fs};
  uint32_t power = rt_call_far(MK_FP(SEG_SEL(16), 0x1ca2), FAR_CDECL, fa, 2);
  if ((int16_t)(power >> 16) < 0 || power == 0) return;
  uint16_t fmt = str_near(0xe4);
  n = WSP2(DSP(0x5866), MK_FP(MEM_SEL_DGROUP, fmt), (uint16_t)power, (uint16_t)(power >> 16));
  (void)TextOut(hdc, I16(drd16(rect) + 0x56), I16(y), DSP(0x5866), n);
}

/* Half of v rounded toward zero (CWD, SUB AX,DX, SAR AX,1). */
static inline uint16_t half_trunc(uint16_t v)
{
  int16_t s = (int16_t)v;
  return (uint16_t)((int16_t)(uint16_t)((uint16_t)s - (uint16_t)(s < 0 ? -1 : 0)) >> 1);
}

/* Mode 8 of 5:0x04e8: the selected map object DS:0x4B8A (0xca8 to 0x1722). */
static void panel_object(uint16_t hdc, uint16_t rect)
{
  const uint16_t h = drd16(0x267a);
  if (drd16(0x4b8a) == 0xffff) return;
  uint16_t mo = U16((uint16_t)(drd16(0x4b8a) * 0x12) + drd16(DS_MO_OFF)), mseg = drd16(DS_MO_SEG);   /* [BP-8], [BP-6] */
  uint16_t player = U16((rd8(mseg, U16(mo + 1)) & 0x1e) >> 1);
  uint16_t si = U16((drd16(U16(player * 0xc0 + 0x5a6a)) & 0xf8) >> 3);          /* the picture number */
  uint16_t x = U16(drd16(rect) + 6), y = U16(drd16((uint16_t)(rect + 2)) + 6);  /* [BP-4], [BP-2] */
  const uint16_t x0 = x, y0 = y;
  uint16_t old_brush = SelectObject(hdc, drd16(0x16));                          /* [BP-0x32] */
  PB(x0, y0, 0x46, 2);
  const uint16_t y1 = U16(y0 + 2);                                              /* [BP-0x1C0] */
  PB(x0, y1, 2, 0x44);
  uint16_t kind = U16((rd8(mseg, U16(mo + 1)) & 0xe0) << 8);                    /* [BP-0x1C2] */
  uint16_t di;
  if (kind == 0) di = rd8(mseg, U16(mo + 0xc));
  else if (kind == 0x2000) di = (rd8(mseg, U16(mo + 7)) & 0x3c) ? 4 : 3;
  else if (kind == 0x6000) di = 6;
  else di = 5;
  (void)SelectPalette(hdc, drd16(0x54f8), 0);
  (void)RealizePalette(hdc);
  kind = U16((rd8(mseg, U16(mo + 1)) & 0xe0) << 8);
  if (kind != 0x4000 && kind != 0x6000) {
    PB(x0 + 0x10, y0 + 0x44, 2, 0x26);
    (void)SelectObject(hdc, drd16(0x14));
    PB(x0 + 2, y0 + 0x44, 0xf, 1);
    PB(x0 + 1, y0 + 0x45, 0xf, 1);
    PB(x0 + 0x34, y0 + 0x44, 0x12, 2);
    PB(x0 + 0x44, y1, 2, 0x42);
    PB(x0 + 0x45, y0 + 1, 1, 1);
    PB(x0 + 0x11, y0 + 0x68, 0x25, 2);
    PB(x0 + 0x10, y0 + 0x69, 1, 1);
    PB(x0 + 0x34, y0 + 0x46, 2, 0x22);
    (void)PatBlt(hdc, I16(x0 + 2), I16(y1), 0x42, 0x42, 0x42);
    (void)PatBlt(hdc, I16(x0 + 0x12), I16(y0 + 0x44), 0x22, 0x24, 0x42);
    SPRITE(hdc, x0 + 0x13, y0 + 0x47, 0x20, 0x20, drd16(0x565a), (si & 7) << 5, 0 - (((si & 0xfff8) << 2) - 0x60), 0x20, 0x20);
  } else {
    (void)SelectObject(hdc, drd16(0x14));
    PB(x0 + 2, y0 + 0x44, 0x44, 1);
    PB(x0 + 1, y0 + 0x45, 0x45, 1);
    PB(x0 + 0x44, y1, 2, 0x42);
    PB(x0 + 0x45, y0 + 1, 1, 1);
    (void)PatBlt(hdc, I16(x0 + 2), I16(y1), 0x42, 0x42, 0x42);
  }
  (void)SelectObject(hdc, old_brush);
  (void)CALL_ORIG(8, 0xaa4e, FAR_CDECL, 12, hdc, U16(x0 + 2), y1, 0x40, 0x40, drd16(0x26ae), U16(di << 6), 0, 0x40, 0x40, 0x20, 0xcc);
  kind = U16((rd8(mseg, U16(mo + 1)) & 0xe0) << 8);
  if (kind == 0x2000) {
    x = U16(x + 0x50);
    (void)SetTextColor(hdc, drd32(0x2870));
    (void)SetBkColor(hdc, drd32(0x253a));
    (void)SelectObject(hdc, drd16(0x2930));
    if (rd8(mseg, U16(mo + 7)) & 0x3c) {
      (void)str_copy(0x37b, TXTBUF);
      int16_t n = WSP1(DSP(0x5866), SSP(TXTBUF), U16(((rd8(mseg, U16(mo + 7)) & 0x3c) >> 2) + 4));
      (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
      y = U16(y + h + 2);
      (void)str_copy(0x37d, TXTBUF);
      uint16_t name = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, rd16(mseg, U16(mo + 6)) & 0x3ff);
      ds_strcat(TXTBUF, name);
      (void)TextOut(hdc, I16(x), I16(y), SSP(TXTBUF), (int16_t)ds_strlen(TXTBUF));
    }
    y = U16(y + half_trunc(U16(3 * h)));
    x = U16(x + (uint16_t)GetTextExtent(hdc, DSP(drd16(0x468)), (int16_t)ds_strlen(drd16(0x468))));
    for (uint16_t i = 0; i < 3; i++) {
      uint16_t sy = y;
      uint16_t fmt = str_near(0x37c);
      int16_t n = WSP2(DSP(0x5866), DSP(fmt), drd16(U16(0x464 + 2 * i)), MEM_SEL_DGROUP);
      TEXT_LINE(hdc, x, sy, 0x5866, n, 0);
      n = WSP1(DSP(0x5866), DSP(drd16(0x180)), rd16(mseg, U16(mo + 8 + 2 * i)));
      (void)TextOut(hdc, I16(x), I16(sy), DSP(0x5866), n);
      y = U16(sy + h + 2);
    }
    return;
  }
  if (kind == 0x4000) {
    x = U16(x + 0x5e);
    (void)SetTextColor(hdc, drd32(0x2870));
    (void)SetBkColor(hdc, drd32(0x253a));
    (void)SelectObject(hdc, drd16(0x2930));
    uint16_t len = str_copy(0x3bd, TXTBUF);
    x = U16(x + (uint16_t)GetTextExtent(hdc, SSP(TXTBUF), (int16_t)len) + 0x14);
    TEXT_LINE(hdc, x - 4, y, TXTBUF, len, 0);
    di = U16(x - 4);
    (void)str_copy(0x3c0, TXTBUF);
    int16_t n = WSP2(DSP(0x5866), SSP(TXTBUF), rd16(mseg, U16(mo + 2)), rd16(mseg, U16(mo + 4)));
    (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
    y = U16(y + half_trunc(U16(3 * h)));
    len = str_copy(0x3be, 0x5866);
    TEXT_LINE(hdc, di, y, 0x5866, len, 0);
    if (rd16(mseg, U16(mo + 0xa)) & shl_ax_cl(1, drd16(0x014c))) {
      uint32_t rec = CALL_ORIG(7, 0x01a0, FAR_CDECL, 1, rd16(mseg, U16(mo + 0xc)));
      if (rec != 0) {
        uint16_t ro = (uint16_t)rec, rs = (uint16_t)(rec >> 16);
        n = WSP2(DSP(0x5866), SSP(TXTBUF), rd16(rs, U16(ro + 2)), rd16(rs, U16(ro + 4)));
      } else {
        n = (int16_t)str_copy(0x24e, 0x5866);
      }
    } else {
      n = (int16_t)str_copy(0x24e, 0x5866);
    }
    (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
    y = U16(y + half_trunc(U16(3 * h)));
    len = str_copy(0x3bf, 0x5866);
    TEXT_LINE(hdc, x - 4, y, 0x5866, len, 0);
    const uint16_t oa[2] = {mo, mseg};
    uint16_t r33 = (uint16_t)rt_call_far(MK_FP(SEG_SEL(33), 0x0742), FAR_CDECL, oa, 2);
    len = str_copy(U16(r33 + 0x3b6), 0x5866);
    (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), (int16_t)len);
    return;
  }
  if (kind == 0x6000) {
    uint16_t rr = rect;
    (void)SetTextColor(hdc, drd32(0x2870));
    (void)SetBkColor(hdc, drd32(0x253a));
    (void)SelectObject(hdc, drd16(0x2930));
    x = U16(x + 0x50);
    SetRect(SSP(BPN(-0x54)), I16(x), I16(y), I16(drd16((uint16_t)(rr + 4)) - 8), I16(drd16((uint16_t)(rr + 6)) - 8));
    if (!(rd16(mseg, U16(mo + 0xc)) & shl_ax_cl(1, drd16(0x014c)))) {
      uint16_t t = str_near(0x3c1);
      int16_t r = DrawText(hdc, DSP(t), (int16_t)ds_strlen(t), SSP(BPN(-0x54)), 0x810);
      y = U16(y + (uint16_t)r + 8);
    }
    uint16_t fmt = str_near(U16(0x3c2));
    int16_t n = WSP1(DSP(0x5866), DSP(fmt), rd8(mseg, U16(mo + 0xa)) & 0xf);
    (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
    return;
  }
  if (kind != 0) return;
  (void)SetTextColor(hdc, drd32(0x2870));
  (void)SetBkColor(hdc, drd32(0x253a));
  (void)SelectObject(hdc, drd16(0x2930));
  (void)str_copy(0x2c2, TXTBUF);
  x = U16(x + 0x50);
  int16_t n = WSP2(DSP(0x5866), SSP(TXTBUF), rd16(mseg, U16(mo + 2)), rd16(mseg, U16(mo + 4)));
  (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  (void)str_copy(0x2c3, TXTBUF);
  n = WSP2(DSP(0x5866), SSP(TXTBUF), drd16(U16(0x4c2 + 2 * rd8(mseg, U16(mo + 0xc)))), MEM_SEL_DGROUP);
  (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  (void)str_copy(0x2c4, TXTBUF);
  int32_t pop = (int32_t)rd32(mseg, U16(mo + 6));
  uint16_t root = (uint16_t)fp80_to_i32_ftol(crt_sqrt_34_0d68(fp80_from_i32(pop)));
  n = WSP3(DSP(0x5866), SSP(TXTBUF), root, (uint16_t)pop, (uint16_t)((uint32_t)pop >> 16));
  (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  uint16_t pl = U16((rd8(mseg, U16(mo + 1)) & 0x1e) >> 1);
  uint16_t ret28 = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, U16(pl * 0xc0 + 0x5a64), 0xe);
  uint16_t f = (ret28 == 5) ? 1 : 4;
  uint16_t cnt = (uint16_t)rt_call_far(MK_FP(SEG_SEL(33), 0x01a0), FAR_CDECL,
                                       (const uint16_t[]){rd16(mseg, U16(mo + 2)), rd16(mseg, U16(mo + 4)), rd16(mseg, U16(mo + 6)),
                                                          rd16(mseg, U16(mo + 8))}, 4);
  int32_t v = (int32_t)(int16_t)U16((uint16_t)((int32_t)(int16_t)cnt * (int32_t)f) + 2);          /* [BP-0xC] */
  if (v > 0x32) v = 0x32;
  if (rd8(mseg, U16(mo + 0xd)) != 0) v += 0x19;
  int32_t q = (int32_t)((uint32_t)rd32(mseg, U16(mo + 6)) * (uint32_t)v) / 100;                    /* [BP-0x10] */
  if (q < v) q = v;
  if (rd8(mseg, U16(mo + 0xc)) != 2 && q < 10) q = 10;
  (void)str_copy(0x2c5, TXTBUF);
  n = WSP2(DSP(0x5866), SSP(TXTBUF), (uint16_t)q, (uint16_t)((uint32_t)q >> 16));
  (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
  y = U16(y + h + 2);
  if (U16((rd8(mseg, U16(mo + 1)) & 0x1e) >> 1) != drd16(0x014c)) return;
  own_object_index_and_count(rd16(mseg, mo), BPN(-0x22), BPN(-0x26));
  (void)str_copy(0x51f, TXTBUF);
  n = WSP2(DSP(0x5866), SSP(TXTBUF), LW(-0x22), LW(-0x26));
  (void)TextOut(hdc, I16(x), I16(y), DSP(0x5866), n);
}

/* Mode 1 of 5:0x04e8: the selected planet DS:0x4B84 (0x1722 to 0x27ac). Returns 1 when the picture of
 * the empty panel has to be drawn (the tail at 0x27ac), else 0. Frame: the planet record at BP-0xA8,
 * the saved record of the current player at BP-0x1B8, the rectangle copy at BP-0x54, a RECT at BP-0x4C,
 * the habitability ranges of 9:0x5220 at BP-0x44 / -0x3E / -0x38, the mineral output of 5:0x3a72 at
 * BP-0x6C (own) and BP-0x60 (the fleets'). PUSHW mirrors the words the original leaves on the stack
 * under 8:0x8ff4, 7:0x0b84 and 8:0x9398 (they end up as the last arguments of 8:0x9398). */
static int16_t panel_planet(uint16_t hdc, uint16_t rect)
{
  const uint16_t h = drd16(0x267a);
  const uint16_t REC = BPN(-0xa8);
  if (drd16(0x4b84) == 0xffff) return 1;
  if ((uint16_t)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, drd16(0x4b84), REC) == 0) return 1;
  uint16_t narrow = 0;                                                  /* [BP-0x30] */
  rep_movs(rt_ss, BPN(-0x1b8), MEM_SEL_DGROUP, U16(drd16(0x014c) * 0xc0 + 0x5a64), 0xc0);
  rep_movs(rt_ss, BPN(-0x54), MEM_SEL_DGROUP, rect, 8);
  {
    uint16_t cur = drd16(0x014c), owner = LW(-0xa6);
    uint16_t t = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, U16(cur * 0xc0 + 0x5a64), 0xe);
    if (t == 3 && (int16_t)owner != -1) {
      if (owner == cur || drd8(U16(owner + cur * 0xc0 + 0x5ad4)) == 1) {
        for (uint16_t k = 0; k < 3; k++) {   /* the race ranges of the planet's owner replace the player's */
          dwr8(U16(cur * 0xc0 + k + 0x5a74), drd8(U16(owner * 0xc0 + k + 0x5a74)));
          dwr8(U16(cur * 0xc0 + k + 0x5a77), drd8(U16(owner * 0xc0 + k + 0x5a77)));
          dwr8(U16(cur * 0xc0 + k + 0x5a7a), drd8(U16(owner * 0xc0 + k + 0x5a7a)));
        }
      }
    }
  }
  (void)SelectObject(hdc, drd16(0x292e));
  const uint16_t titlew = U16((uint16_t)GetTextExtent(hdc, DSP(str_near(0x223)), 5) + 6);              /* [BP-2] */
  (void)SelectObject(hdc, drd16(0x2930));
  uint16_t si = U16((uint16_t)GetTextExtent(hdc, DSP(drd16(0x0490)), (int16_t)ds_strlen(drd16(0x0490))) + 6);
  if ((int16_t)U16(si << 2) > (int16_t)LW(-0x50)) {
    narrow = 1;
    si = U16((uint16_t)GetTextExtent(hdc, DSP(drd16(0x0490)), 4) + 6);
  }
  const uint16_t valx = U16(si + LW(-0x54));                                                          /* [BP-0x20] */
  const uint16_t rightx = U16(LW(-0x50) - titlew);                                                    /* [BP-0x2a] */
  const uint16_t rowh = U16((uint16_t)(idiv16(I16(LW(-0x4e) - (uint16_t)(h << 2) - LW(-0x52) - 2), 6, 0x1884) + 1) & 0xfffe);
  const uint16_t rows3 = U16(3 * rowh);                                                               /* [BP-0x1d4] */
  uint16_t y = U16(LW(-0x52) + 2);                                                                    /* [BP-0x28] */
  if (LB(-0xa3) & 2) {                                       /* a star base: its small picture */
    uint16_t memdc = CreateCompatibleDC(hdc);
    uint16_t old = SelectObject(memdc, drd16(0x5a54));
    uint32_t fg = SetTextColor(hdc, 0);
    uint32_t bk = SetBkColor(hdc, 0xffffff);
    (void)BitBlt(hdc, I16(LW(-0x50) - 0x16), I16(y + 2), 0xd, 0x10, memdc, 0, 0xc, 0x008800c6);
    (void)SetTextColor(hdc, 0xffff);
    (void)SetBkColor(hdc, 0);
    (void)BitBlt(hdc, I16(LW(-0x50) - 0x16), I16(y + 2), 0xd, 0x10, memdc, 0, 0xc, 0x00ee0086);
    (void)SetTextColor(hdc, fg);
    (void)SetBkColor(hdc, bk);
    (void)SelectObject(memdc, old);
    (void)DeleteDC(memdc);
  }
  if (LB(-0xa4) >= 3) {                                      /* habitability */
    uint16_t len = str_copy(narrow ? 0x21e : 0x21f, 0x5866);
    uint16_t ew = (uint16_t)GetTextExtent(hdc, DSP(0x5866), (int16_t)len);
    (void)SetTextColor(hdc, drd32(0x2870));
    (void)TextOut(hdc, I16(valx), I16(y), DSP(0x5866), (int16_t)len);
    int16_t hab = (int16_t)(uint16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, REC, rt_ss, drd16(0x014c));
    len = (uint16_t)WSP1(DSP(0x5866), DSP(drd16(0x0178)), hab);
    (void)SetTextColor(hdc, hab < 0 ? 0x000000ffu : 0x00007f00u);
    (void)TextOut(hdc, I16(valx + ew), I16(y), DSP(0x5866), (int16_t)len);
    if (narrow == 0) {
      ew = U16(ew + (uint16_t)GetTextExtent(hdc, DSP(0x5866), (int16_t)len));
      int16_t best = (int16_t)(uint16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, REC, rt_ss, drd16(0x014c));
      if (best > hab) {
        if (best > 0) (void)SetTextColor(hdc, best > 10 ? 0x00007f00u : 0x00007f7fu);
        len = (uint16_t)WSP1(DSP(0x5866), DSP(0x4ca), best);
        (void)TextOut(hdc, I16(valx + ew), I16(y), DSP(0x5866), (int16_t)len);
      }
    }
  }
  /* the owner line (at the right of the list) */
  (void)SetTextColor(hdc, drd32(0x2870));
  const int16_t owner = (int16_t)LW(-0xa6);
  if (owner != -1) ds_strcpy(TXTBUF, str_near(narrow ? 0x21c : 0x21d));
  if (LB(-0xa4) == 7) {
    uint32_t v = rd32(rt_ss, BPN(-0x80)) * 100u;             /* the population in hundreds */
    uint16_t slen = ds_strlen(TXTBUF);
    pushw(0);
    uint16_t n = (uint16_t)CALL_ORIG(8, 0x8ff4, FAR_CDECL, 3, U16(TXTBUF + slen), (uint16_t)v, (uint16_t)(v >> 16));
    (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 5, hdc, rightx, y, TXTBUF, U16(slen + n));
    popw(1);
  } else if (owner == -1) {
    TEXT_LINE(hdc, rightx, y, str_near(0x220), 0xb, 0);
  } else {
    if (LB(-0xa4) >= 3) {
      uint32_t big = (uint32_t)(LW(-0x96) & 0x0fff) * 4u;
      ds_strcpy(0x5866, TXTBUF);
      uint16_t len = ds_strlen(TXTBUF);
      pushw(0);
      uint16_t add;
      if ((int32_t)big > 0) {
        add = (uint16_t)WSP3(DSP(0x5866 + len), DSP(str_near(0x21b)), 0x00b1, (uint16_t)big, (uint16_t)(big >> 16));
      } else {
        add = str_copy(0x4e1, U16(0x5866 + len));
      }
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 5, hdc, rightx, y, 0x5866, U16(len + add));
      popw(1);
    }
    (void)SetTextColor(hdc, 0xff);
    pushw(0);
    pushw(0);
    uint16_t nm = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, LW(-0xa6), 0, 1, 0, 0, 0);
    (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 4, hdc, rightx, U16(y + h - 2), nm);
    popw(2);
  }
  y = U16(y + h - 2);
  /* the age of the information */
  uint16_t age = U16(drd16(0x0082) - LW(-0x76));                                                     /* [BP-2] */
  uint16_t len;
  if (age == 0) {
    len = (uint16_t)WSP0(DSP(0x5866), DSP(str_near(narrow ? 0x1b8 : 0x225)));
  } else if (narrow != 0) {
    len = (uint16_t)WSP1(DSP(0x5866), DSP(str_near(0x221)), age);
  } else {
    (void)WSP1(DSP(0x5866), DSP(str_near(0x222)), age);
    if ((int16_t)age > 1) ds_strcat(0x5866, 0x4d2);
    ds_strcat(0x5866, 0x4d4);
    len = ds_strlen(0x5866);
  }
  (void)SetTextColor(hdc, (int16_t)age > 5 ? 0xffu : 0u);
  (void)TextOut(hdc, I16(valx), I16(y), DSP(0x5866), (int16_t)len);
  y = U16(y + h);
  /* the frame around the three bars */
  const uint16_t old_brush = SelectObject(hdc, drd16(0x16));                                         /* [BP-0x32] */
  si = U16(rightx - valx);
  const uint16_t x1 = U16(valx + 1);                                                                 /* [BP-0x1d2] */
  PB(valx, y, si, 1);
  const uint16_t y1 = U16(y + 1);
  (void)PatBlt(hdc, I16(x1), I16(y1), I16(si - 2), I16(rows3 - 1), 0x42);
  PB(valx, y, 1, rows3);
  const uint16_t x2 = U16(valx + 2), w2 = U16(si - 4);                                               /* [BP-0x1d6], [BP-0x1d8] */
  PB(x2, rowh + y, w2, 1);
  PB(x2, rowh * 2 + y, w2, 1);
  (void)SelectObject(hdc, drd16(0x14));
  PB(rightx - 1, y1, 1, rows3);
  PB(valx, rows3 + y, si, 1);
  (void)SetTextColor(hdc, drd32(0x2870));
  (void)SetBkMode(hdc, 1);
  const uint16_t have_ranges = (uint16_t)CALL_ORIG(9, 0x5220, FAR_CDECL, 6, REC, rt_ss, BPN(-0x44), BPN(-0x3e), BPN(-0x38), 1);
  const uint16_t barw = w2;                                                                          /* [BP-0xE] */
  const uint16_t toff = U16((int16_t)U16(rowh - h) >> 1);                                           /* [BP-0x1e] */
  for (uint16_t i = 0; i < 3; i++) {
    uint16_t cur = U16(drd16(0x014c) * 0xc0 + i);
    int16_t lo = (int8_t)drd8(U16(cur + 0x5a77)), hi = (int8_t)drd8(U16(cur + 0x5a7a));              /* [BP-2], [BP-4] */
    int16_t env = (int8_t)LB((int16_t)(-0x9c + i));                                                  /* [BP-0x10] */
    uint16_t label = narrow ? drd16(U16(0x4a2 + 2 * i)) : drd16(U16(0x48e + 2 * i));                  /* [BP-6] */
    TEXT_LINE(hdc, valx - 2, y + toff, label, 0, 0);
    if (LB(-0xa4) >= 3) {
      uint16_t str = (uint16_t)CALL_ORIG(9, 0x3e72, FAR_CDECL, 2, i, (uint16_t)env);
      uint16_t slen = ds_strlen(str);
      (void)SelectObject(hdc, drd16(0x292e));
      (void)TextOut(hdc, I16(rightx + 2), I16(y + toff), DSP(str), (int16_t)slen);
    }
    (void)SelectObject(hdc, drd16(0x2930));
    (void)SelectObject(hdc, drd16(U16((i << 2) + 0x5742)));
    {
      int16_t x = MulDiv(lo, (int16_t)barw, 0x64);
      int16_t w = MulDiv((int16_t)U16(hi - lo), (int16_t)barw, 0x64);
      (void)PatBlt(hdc, I16(x + valx + 2), I16(y + 2), w, I16(rowh - 3), PAT);
    }
    if (LB(-0xa4) >= 3) {
      (void)SelectObject(hdc, drd16(U16((i << 2) + 0x5744)));
      int16_t v = (int8_t)LB((int16_t)(-0x99 + i));
      uint16_t mx = U16(MulDiv(v, (int16_t)barw, 0x64) + valx + 2);                                  /* [BP-0x2e] */
      uint16_t my = U16(half_trunc(rowh) + y);                                                       /* [BP-0x2c] */
      int16_t q4 = (int16_t)((int16_t)U16(rowh + (((int16_t)rowh < 0) ? 3 : 0)) >> 2);
      int16_t d = (int16_t)(q4 - 1);
      if (d < 2) d = 2;
      PB(mx - d, my, 2 * d + 1, 1);
      PB(mx, my - d, 1, 2 * d + 1);
      uint16_t top = U16(y + 3), bot = U16(rowh + y - 3), cnt = 1;                                   /* [BP-0xa], DI, [BP-0xc] */
      uint16_t sx = U16(MulDiv(env, (int16_t)barw, 0x64) + valx + 2);
      uint16_t x2l = 0;
      for (;;) {
        PB(sx, top, 1, 1);
        x2l = U16(cnt + sx - 1);
        PB(x2l, top, 1, 1);
        top = U16(top + 1);
        if (!((int16_t)top <= (int16_t)bot)) break;
        PB(sx, bot, 1, 1);
        PB(x2l, bot, 1, 1);
        bot = U16(bot - 1);
        sx = U16(sx - 1);
        cnt = U16(cnt + 2);
      }
      if (have_ranges != 0) {
        uint16_t ly = U16(top - 1);
        int16_t a = (int16_t)LW((int16_t)(-0x44 + 2 * i));
        int16_t t = (a != -1) ? (int16_t)(env - a) : 0;
        if (t < 0) t = 0;
        uint16_t dx = U16(MulDiv(I16(env - t), (int16_t)barw, 0x64) + valx + 2);
        int16_t b = (int16_t)LW((int16_t)(-0x3e + 2 * i));
        int16_t t2 = (b != -1) ? (int16_t)(b - env) : 0;
        if (t2 < 0) t2 = 0;
        PB(dx, ly, MulDiv(I16(env + t2), (int16_t)barw, 0x64) - (int16_t)dx + (int16_t)valx + 3, 1);
      }
    }
    y = U16(y + rowh);
  }
  /* the ruler under the bars */
  uint16_t ry = U16(y + (uint16_t)((int16_t)h >> 1));                                                /* SI */
  (void)SelectObject(hdc, drd16(0x16));
  uint16_t rw = U16(rightx - valx);                                                                  /* DI */
  PB(valx, ry, rw, 1);
  (void)PatBlt(hdc, I16(x1), I16(ry + 1), I16(rw - 2), I16(rows3 + 1), 0x42);
  const uint16_t rh = U16(rows3 + 2);                                                                /* [BP-0x1e2] */
  PB(valx, ry, 1, rh);
  (void)SelectObject(hdc, drd16(0x14));
  PB(rightx - 1, ry + 1, 1, rh);
  PB(valx, rows3 + ry + 2, rw, 1);
  (void)SelectObject(hdc, drd16(0x2938));
  {
    int16_t n = WSP1(DSP(0x5866), DSP(drd16(0x016a)), drd16(0x04c8));
    uint16_t tw = (uint16_t)GetTextExtent(hdc, DSP(0x5866), n);                                      /* [BP-4] */
    uint16_t inner = U16(rw - 3);                                                                    /* [BP-0x1c] */
    int16_t div1 = idiv16(I16(inner), I16(((int16_t)tw >> 1) + tw), 0x2281);
    int16_t step = idiv16((int16_t)drd16(0x04c8), div1, 0x2289);
    int16_t cap = (int16_t)drd16(0x04c8);
    int16_t unit;
    if (cap < 0x1f4) unit = (int16_t)(idiv16(I16(step + 0x31), 0x32, 0x229a) * 10);
    else if (cap < 0x3e8) unit = (int16_t)(idiv16(I16(step + 0x31), 0x32, 0x22b7) * 0x32);
    else if (cap < 0x9c4) unit = (int16_t)(idiv16(I16(step + 0x63), 0x64, 0x22cc) * 0x64);
    else if (cap < 0x1d4c) unit = (int16_t)(idiv16(I16(step + 0xf9), 0xfa, 0x22e3) * 0xfa);
    else if (cap < 0x3a98) unit = (int16_t)(idiv16(I16(step + 0x1f3), 0x1f4, 0x22fb) * 0x1f4);
    else unit = (int16_t)(idiv16(I16(step + 0x3e7), 0x3e8, 0x230b) * 0x3e8);
    int16_t ticks = idiv16(cap, unit, 0x2318);                                                       /* [BP-4] */
    (void)SetTextColor(hdc, drd32(0x2870));
    const uint16_t tick_y = U16(rows3 + ry + 4);                                                     /* [BP-0x1e] */
    y = ry;
    uint16_t k = 0;                                                                                   /* [BP-0x22] */
    if (!(ticks < 0)) {
      int32_t span = (int32_t)(int16_t)inner * (int32_t)unit;                                        /* [BP-0x1e6] */
      for (;;) {
        int32_t pos = idiv32(imul32(span, (int32_t)k), cap, 0x23ad);
        uint16_t tx = U16((uint16_t)pos + valx);
        PB(tx, ry + 2, 1, rows3 - 1);
        n = WSP1(DSP(0x5866), DSP(drd16(0x016a)), U16(k * (uint16_t)unit));
        (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, tx, tick_y, 0x5866, (uint16_t)n);
        k = U16(k + 1);
        if (!((int16_t)k <= ticks)) break;
      }
    }
    TEXT_LINE(hdc, valx - 4, tick_y, 0x4d9, 2, 0);
  }
  /* what the planet yields per turn: own planet by 5:0x3a72, else the fleets mining it */
  if (LW(-0xa6) == drd16(0x014c)) {
    const uint16_t a[6] = {REC, rt_ss, BPN(-0x6c), 0xffff, 0xffff, 0};
    (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3a72), FAR_CDECL, a, 6);
  } else {
    LD(-0x64) = LD(-0x68) = LD(-0x6c) = 0;
    if ((int16_t)LW(-0xa6) == -1) {
      uint16_t j = 0, off = 0;
      if ((int16_t)drd16(0x553a) > 0) {
        for (;;) {
          uint32_t fp = rd32(drd16(0x00bc), U16(drd16(0x00ba) + off));
          if (fp == 0) break;
          uint16_t fo = (uint16_t)fp, fs = (uint16_t)(fp >> 16);
          if (rd16(fs, U16(fo + 6)) == LW(-0xa8) && rd16(fs, U16(fo + 2)) == drd16(0x014c) && (rd8(fs, U16(fo + 5)) & 4) == 0) {
            uint32_t wp = rd32(fs, U16(fo + 0x64));
            if ((rd8((uint16_t)(wp >> 16), U16((uint16_t)wp + 0xa)) & 0xf) == 3) {
              const uint16_t wa[2] = {fo, fs};
              uint32_t w = rt_call_far(MK_FP(SEG_SEL(16), 0x193e), FAR_CDECL, wa, 2);
              if (!((int16_t)(w >> 16) < 0) && w != 0) {
                const uint16_t a[6] = {REC, rt_ss, BPN(-0x60), (uint16_t)w, (uint16_t)(w >> 16), 0};
                (void)rt_call_far(MK_FP(SEG_SEL(5), 0x3a72), FAR_CDECL, a, 6);
                for (uint16_t k = 0; k < 3; k++) LD(-0x6c + 4 * k) = LD(-0x6c + 4 * k) + LD(-0x60 + 4 * k);
              }
            }
          }
          off = U16(off + 4);
          j = U16(j + 1);
          if (!((int16_t)j < (int16_t)drd16(0x553a))) break;
        }
      }
    }
  }
  /* the three mineral rows */
  (void)SelectObject(hdc, drd16(0x2930));
  const uint16_t toff2 = U16(((int16_t)U16(rowh - h) >> 1) + 2);                                     /* [BP-0x1e] */
  const int32_t inner32 = (int32_t)(int16_t)U16(rw - 3);                                             /* [BP-0x1ea] */
  const int32_t cap32 = (int32_t)(int16_t)drd16(0x04c8);
  for (uint16_t i = 0; i < 3; i++) {
    (void)SetTextColor(hdc, drd32(U16((i << 2) + 0x418)));
    uint16_t tl = narrow ? 4 : ds_strlen(drd16(U16(i * 2 + 0x464)));
    TEXT_LINE(hdc, valx - 2, y + toff2, drd16(U16(i * 2 + 0x464)), tl, 0);
    uint32_t own = LD((int16_t)(-0x6c + 4 * i)), stock = LD((int16_t)(-0x8c + 4 * i));
    uint32_t sum = own + stock;
    uint32_t clipped = ((int32_t)sum > cap32) ? (uint32_t)cap32 : sum;
    uint16_t top = U16(y + 4);
    uint16_t q = (uint16_t)idiv32(imul32((int32_t)clipped, inner32), (int32_t)cap32, 0x25ad);
    const uint16_t bottom = U16(rowh + y);                                                           /* [BP-0x1ee] */
    SetRect(SSP(BPN(-0x4c)), I16(x1), I16(top), I16(q + valx + 1), I16(bottom - 1));
    if (q != 0) (void)FillRect(hdc, SSP(BPN(-0x4c)), drd16(U16((i << 2) + 0x5756)));
    if ((int32_t)sum > cap32) {
      (void)SetTextColor(hdc, drd32(0x2870));
      (void)TextOut(hdc, I16(rightx), I16(y + toff2), DSP(0x4dc), 1);
    }
    uint32_t c2 = ((int32_t)stock > cap32) ? (uint32_t)cap32 : stock;
    uint16_t q2 = (uint16_t)idiv32(imul32((int32_t)c2, inner32), (int32_t)cap32, 0x2679);
    SetRect(SSP(BPN(-0x4c)), I16(x1), I16(top), I16(q2 + valx + 1), I16(bottom - 1));
    if (q2 != 0) (void)FillRect(hdc, SSP(BPN(-0x4c)), drd16(U16((i << 2) + 0x5754)));
    if (LB(-0xa4) >= 3) {
      for (uint16_t k = 0; k < 2; k++) {
        (void)SelectObject(hdc, drd16(U16(((i << 1) + k) * 2 + 0x5754)));
        uint16_t t = top;                                                                            /* [BP-0xa] */
        uint16_t b = U16(bottom - 2);                                                                /* [BP-6] */
        uint8_t cnt = LB((int16_t)(-0x9f + i));
        if (cnt > 0x64) cnt = 0x64;
        uint16_t bx = U16(MulDiv(cnt, I16(inner32 - 8), 0x64) + valx + k + 1);                       /* [BP-8] */
        uint16_t wdt = 1;                                                                            /* DI */
        for (;;) {
          PB(bx, t, wdt, 1);
          t = U16(t + 1);
          if (!((int16_t)t <= (int16_t)b)) break;
          uint16_t x0b = bx;
          bx = U16(bx - 1);
          uint16_t yb = b;
          b = U16(b - 1);
          PB(x0b, yb, wdt, 1);
          wdt = U16(wdt + 2);
        }
      }
    }
    y = bottom;
  }
  (void)SetBkMode(hdc, 2);
  (void)SelectObject(hdc, old_brush);
  rep_movs(MEM_SEL_DGROUP, U16(drd16(0x014c) * 0xc0 + 0x5a64), rt_ss, BPN(-0x1b8), 0xc0);
  return 0;
}

/* paint_info_panel: 5:0x04e8 (Ghidra FUN_1020_04e8, alias fn_05_04e8).
 * 9048 bytes. ENTER 0x1EE,0; PUSH DI; PUSH SI. FAR CDECL: DC [BP+6],
 * near pointer to the RECT of the panel [BP+8]; returns nothing. Paints the information panel of the
 * selected thing into the rectangle: with the text colours and transparent background set first and
 * restored at the end. The kind of the selection is DS:0x4B80 (4 means: planet when bit 0 of DS:0x4B82
 * is set, fleet for bit 1, map object for bit 3, else nothing): 2 the fleet DS:0x4B86 (frame, ship
 * picture, ship count, cargo / orders lines), 8 the map object DS:0x4B8A (frame, picture, text lines by
 * its kind), anything else the planet DS:0x4B84 (habitability, owner, age, the mineral bars, the
 * ruler and the mineral rows) or, when there is none, a picture of the empty panel. */
DECOMP(5, 0x04e8, paint_info_panel, FAR_CDECL)
void paint_info_panel(uint16_t hdc, uint16_t rect)
{
  FRAME_ENTER(0x1ee + 4);
  uint16_t memdc = CreateCompatibleDC(hdc);                       /* [BP-0x6E] */
  uint32_t old_bk = SetBkColor(hdc, drd32(0x253a));               /* [BP-0x16] */
  uint32_t old_fg = SetTextColor(hdc, 0x0000ffff);                /* [BP-0x1A] */
  uint16_t old_mode = (uint16_t)SetBkMode(hdc, 2);                /* [BP-0x70] */
  uint16_t mode = drd16(0x4b80);
  if (mode == 4) {
    if (drd8(0x4b82) & 1) mode = 1;
    else if (drd8(0x4b82) & 2) mode = 2;
    else mode = (drd8(0x4b82) & 8) ? 8 : 0;
  }
  if (mode != 0) {
    if (mode == 2) {
      panel_fleet(hdc, rect);
    } else if (mode == 8) {
      panel_object(hdc, rect);
    } else if (panel_planet(hdc, rect) != 0 && drd16(0x4b84) != 0xffff) {
      /* no planet to show: the picture of the empty panel, centred in the rectangle */
      uint16_t old = SelectObject(memdc, drd16(0x4174));
      (void)SetTextColor(hdc, 0);
      uint16_t l = drd16(rect), t = drd16((uint16_t)(rect + 2)), r = drd16((uint16_t)(rect + 4)), b = drd16((uint16_t)(rect + 6));
      uint16_t x = U16(((int16_t)U16(r - l - 0x40) >> 1) + l);
      uint16_t y = U16(((int16_t)U16(b - t - 0x40) >> 1) + t);
      (void)BitBlt(hdc, I16(x), I16(y), 0x40, 0x40, memdc, 0, 0, 0x00cc0020);
      (void)SelectObject(memdc, old);
    }
  }
  (void)SetBkMode(hdc, (int16_t)old_mode);
  (void)SetTextColor(hdc, old_fg);
  (void)SetBkColor(hdc, old_bk);
  (void)DeleteDC(memdc);
  FRAME_LEAVE();
}

/* 5:0x0000 MINEWNDPROC (Ghidra FUN_1020_0000), 804 bytes. ENTER 0x3E,0; PUSH DI; PUSH SI. Exported
 * window procedure of the information window (the panel painted by 5:0x04e8 with the title line, the
 * order button DS:0x573c and the click handling). FAR PASCAL: window [BP+0xE], message [BP+0xC],
 * wParam [BP+0xA], lParam (dword) [BP+6]; returns DX:AX. Messages: WM_CREATE (1) makes the button
 * (class DS:[0x7C0], style 0x40000003 at 100,100, 150 wide) and sets its font; WM_PAINT (0xF) draws the
 * title line (the text DS:0x2816 cut to fit, 8:0x9318) and the panel (5:0x344a, 5:0x04e8); WM_ERASEBKGND
 * (0x14) fills the client area with the brush DS:0x10; WM_CTLCOLOR (0x19) sets the background colour
 * and returns that brush; WM_SETCURSOR (0x20) shows DS:0x5442 over the strip of zone 9 and DS:0x4AC8
 * over the other zones; WM_COMMAND (0x111) from the button toggles the order of the selected map object
 * (the flag byte +0xD, recorded by 9:0x6372); the mouse buttons (0x201, 0x203, 0x204) go to 5:0x2ab4
 * unless DS:0x0BBE is set; the rest is DefWindowProc. */
DECOMP(5, 0x0000, MINEWNDPROC, FAR_PASCAL)
uint32_t MINEWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wparam, uint32_t lparam)
{
  FRAME_ENTER(0x3e + 4);
  uint32_t ret = 0;
  switch (msg) {
  case 0x0019:                                       /* WM_CTLCOLOR */
    (void)SetBkColor(wparam, drd32(0x253a));
    ret = drd16(0x0010);
    goto done;
  case 0x0014:                                       /* WM_ERASEBKGND */
    GetClientRect(hwnd, SSP(BPN(-0x1e)));
    (void)FillRect(wparam, SSP(BPN(-0x1e)), drd16(0x0010));
    ret = 1;
    goto done;
  case 0x0001: {                                     /* WM_CREATE */
    uint16_t name = str_near(0x4ac);
    uint16_t button = CreateWindow(DSP(drd16(0x07c0)), DSP(name), 0x40000003, 0x64, 0x64, 0x96, I16(drd16(0x267a)), hwnd, 0,
                                   drd16(0x54f4), 0);
    dwr16(0x573c, button);
    (void)SendMessage(button, 0x30, drd16(0x2930), 0);
    const uint16_t a[1] = {hwnd};
    (void)rt_call_far(MK_FP(SEG_SEL(5), 0x329a), FAR_CDECL, a, 1);
    goto done;
  }
  case 0x000f: {                                     /* WM_PAINT */
    uint16_t hdc = BeginPaint(hwnd, SSP(BPN(-0x3e)));
    GetClientRect(hwnd, SSP(BPN(-0x1e)));
    SetRect(SSP(BPN(-0x16)), 4, 4, I16(LW(-0x1a) - 4), I16((drd16(0x267a) - 2) * 2));
    (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, BPN(-0x16), 0);
    uint32_t old_fg = SetTextColor(hdc, drd32(0x2870));                    /* [BP-0xA] */
    uint32_t old_bk = SetBkColor(hdc, drd32(0x253a));                      /* [BP-0xE] */
    const uint16_t limit = U16(LW(-0x12) - LW(-0x16) - 0x18);              /* [BP-6] */
    uint16_t len = ds_strlen(0x2816);
    uint16_t si = len;
    if ((int16_t)len > 0) {
      for (;;) {
        uint16_t w = (uint16_t)GetTextExtent(hdc, DSP(0x2816), (int16_t)si);
        if (w <= limit) break;
        si = U16(si - 1);
        if (!((int16_t)si > 0)) break;
      }
    }
    LW(-0x12) = U16(LW(-0x12) - 8);
    (void)CALL_ORIG(8, 0x9318, FAR_CDECL, 4, hdc, BPN(-0x16), 0x2816, si);
    LW(-0x12) = U16(LW(-0x12) + 8);
    (void)SetTextColor(hdc, old_fg);
    (void)SetBkColor(hdc, old_bk);
    SetRect(SSP(BPN(-0x16)), I16(LW(-0x14) - LW(-0x10) + LW(-0x12) + 2), I16(LW(-0x14) + 3), I16(LW(-0x12) - 4), I16(LW(-0x10) - 2));
    {
      const uint16_t b[3] = {hdc, BPN(-0x16), several_things_at_place()};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x344a), FAR_CDECL, b, 3);
    }
    LW(-0x1c) = U16(LW(-0x1c) + (uint16_t)((drd16(0x267a) - 2) * 2));
    {
      const uint16_t c[2] = {hdc, BPN(-0x1e)};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x04e8), FAR_CDECL, c, 2);
    }
    EndPaint(hwnd, SSP(BPN(-0x3e)));
    goto done;
  }
  case 0x0020: {                                     /* WM_SETCURSOR */
    GetCursorPos(SSP(BPN(-6)));
    ScreenToClient(hwnd, SSP(BPN(-6)));
    GetClientRect(hwnd, SSP(BPN(-0xe)));
    if (PtInRect(SSP(BPN(-0xe)), LD(-6)) != 0) {
      const uint16_t z[3] = {hwnd, LW(-6), LW(-4)};
      uint16_t zone = (uint16_t)rt_call_far(MK_FP(SEG_SEL(5), 0x2840), FAR_CDECL, z, 3);
      LW(-2) = zone;
      if (zone != 0) {
        (void)SetCursor(zone == 9 ? drd16(0x5442) : drd16(0x4ac8));
        ret = 1;
        goto done;
      }
    }
    break;
  }
  case 0x0111:                                       /* WM_COMMAND */
    if (((uint16_t)lparam) == drd16(0x573c) && ((uint16_t)(lparam >> 16)) == 0) {
      uint16_t checked = (uint16_t)SendMessage(drd16(0x573c), 0x400, 0, 0);
      uint16_t at = U16(drd16(DS_MO_OFF) + (uint16_t)(drd16(0x4b8a) * 0x12));
      LW(-4) = rd16(drd16(DS_MO_SEG), at);
      LW(-2) = checked;
      (void)CALL_ORIG(9, 0x6372, FAR_CDECL, 3, 0x002b, 0x0004, BPN(-4));
      at = U16((uint16_t)(drd16(0x4b8a) * 0x12) + drd16(DS_MO_OFF));
      wr8(drd16(DS_MO_SEG), U16(at + 0xd), (uint8_t)checked);
    }
    goto done;
  case 0x0201:
  case 0x0203:
  case 0x0204:
    if (drd16(0x0bbe) == 0) {
      const uint16_t k[4] = {(uint16_t)lparam, (uint16_t)(lparam >> 16), msg, wparam};
      (void)rt_call_far(MK_FP(SEG_SEL(5), 0x2ab4), FAR_CDECL, k, 4);
    }
    goto done;
  default:
    break;
  }
  ret = DefWindowProc(hwnd, msg, wparam, lparam);
done:
  FRAME_LEAVE();
  return ret;
}
