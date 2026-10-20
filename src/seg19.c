/* Decompiled code segment 19 (NE segment 19, selector 0x0098).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg19.c; Ghidra's pseudo-C in re/decomp/seg19 is only a reading aid.
 *
 * Segment 19 is the newer half of the computer players' code (the other half is segment 17):
 *   19:0x0000  ai7_turn: the turn of the type-7 player: clear bit 7 of the flags of the player's
 *              fleets, select each one (9:0x2a74(2, fleet id)) and hand it a new order
 *   19:0x03a0  fleet_meets_ship_threshold_9_12: does a fleet hold enough ships of designs 9 to 12
 *   19:0x0400  ai2_turn: the turn of the type-2 player (the main one of this segment)
 *   19:0x12f0  ai2_create_designs: the type-2 player's new ship designs (called by 19:0x0400)
 *   19:0x1594  ai3_turn: the turn of the type-3 player, the shorter, older variant of 19:0x0400
 *   19:0x212e  ai3_turn_nop: an empty function (RETF) that 19:0x1594 calls
 * The segment also holds a byte table at 19:0x02c5 to 19:0x039f (below, cs_tab) that 19:0x0000 and
 * 19:0x0400 pass to 18:0x28fc as a far pointer and that 19:0x12f0 reads through CS.
 *
 * Names: Ghidra's are all generic (FUN_1090_<off>), no function here has an export name and none
 * wraps a single Win16 API, so the C names describe what each function does (CONVENTIONS rule f);
 * they are descriptive, not the game's. "type-2" / "type-3" / "type-7" (ai2_, ai3_, ai7_) are
 * placeholders, as in segment 17: they are the values 2, 3 and 7 of the player word DS:0x5a6a +
 * 0xc0 * player shifted right by 13, by which 17:0x0000 (ai_player_turn) picks 19:0x0400, 19:0x1594
 * and 19:0x0000; which computer player kind each value is has not been established. "Design" follows
 * 18:0x0000 as in segment 17's ai0_create_designs / ai1_create_designs. "Designs 9 to 12" reads the
 * fleet words +0x1e to +0x24 as the ship counts at fleet +0x0c + 2 * design (the layout segment 16
 * uses). The old fn_19_<off> names are kept as aliases in the comment above each function.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n + 2 * pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. They are written as the original's
 * basic blocks: the registers are C locals, the stack locals are BP_LOCAL slots at the original
 * offsets, and the labels carry the original offsets. Memory is read and written in 16-bit pieces: an
 * offset is a uint16_t and wraps like the original's. DS = SS = DGROUP at every call; the few
 * instructions that load another DS restore it before the next call. Near calls inside the segment
 * are PUSH CS; CALL near, i.e. far calls with a 4-byte return address; a callee that mirrors a frame
 * (rule d) is reached through rt_call_far, a leaf by its C name. The callees in other segments are
 * not decompiled yet: CALL_ORIG. */
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

/* ai3_turn_nop: 19:0x212e (Ghidra FUN_1090_212e, alias fn_19_212e), 1 byte: RETF, an empty function.
 * The callers (19:0x1594) push one word first and pop it afterwards (the return of 18:0x28fc); the
 * function ignores it. */
DECOMP(19, 0x212e, ai3_turn_nop, FAR_CDECL)
void ai3_turn_nop(uint16_t unused)
{
  (void)unused;
}

/* fleet_meets_ship_threshold_9_12: 19:0x03a0 (Ghidra FUN_1090_03a0, alias fn_19_03a0), 95 bytes.
 * ENTER 2,0; PUSH DI; PUSH SI (a leaf: no callee). The twin of 17:0x20f4 over other ship-count words.
 * Does the fleet `fleet` (FAR CDECL, far pointer at [BP+6]) hold enough ships of designs 9 to 12:
 * di = the 16-bit sum of the words at +0x22 and +0x24 plus twice the words at +0x1e and +0x20 (the
 * counts of ship designs 9 to 12). With `mode` ([BP+0xA], a signed compare) below 2 the answer is 1;
 * otherwise it is 1 when the byte DS:0x5340 is not above the sum (a signed 16-bit compare), else 0.
 * AX only (DX is the fleet's segment). */
DECOMP(19, 0x03a0, fleet_meets_ship_threshold_9_12, FAR_CDECL)
uint16_t fleet_meets_ship_threshold_9_12(uint32_t fleet, int16_t mode)
{
  uint16_t seg = (uint16_t)(fleet >> 16), off = (uint16_t)fleet;
  uint16_t di = 0;
  for (uint16_t k = 0; k < 2; k++) di = (uint16_t)(di + rd16(seg, (uint16_t)(off + 0x22 + 2 * k)));
  for (uint16_t k = 0; k < 2; k++) {
    uint16_t w = rd16(seg, (uint16_t)(off + 0x1e + 2 * k));
    di = (uint16_t)(di + (uint16_t)(w + w));
  }
  if (mode < 2) return 1;
  return ((int16_t)drd8(0x5340) <= (int16_t)di) ? 1 : 0;
}

/* The code segment's data at 19:0x02c5 to 19:0x039f (219 bytes). 19:0x0000 and 19:0x0400 pass 19:0x02c5
 * to 18:0x28fc, 19:0x12f0 passes 19:(0x2d7 + a byte of the table's tail) to 18:0x0000 (the bytes from
 * 19:0x038d on are such indexes). */
#define CS_TAB_BASE 0x02c5
static const uint8_t cs_tab[0xdb] = {
  0x86, 0x64, 0x45, 0x25, 0xa4, 0x04, 0x87, 0x66, 0x47, 0x28, 0xa6, 0x07, 0x8c, 0x6d, 0x49, 0x2b,
  0xa7, 0x0a, 0x1e, 0x1f, 0x0a, 0x1e, 0x1a, 0x04, 0x1e, 0x1a, 0x03, 0x1e, 0x1a, 0x02, 0x1e, 0x00,
  0x00, 0x0d, 0x09, 0x12, 0x0b, 0x1e, 0x01, 0x01, 0x0b, 0x09, 0x12, 0x0b, 0x08, 0x0a, 0x0f, 0x07,
  0x04, 0x06, 0x09, 0x08, 0x0b, 0x0d, 0x00, 0x00, 0x00, 0x09, 0x08, 0x0a, 0x0f, 0x07, 0x03, 0x06,
  0x11, 0x08, 0x0b, 0x0d, 0x01, 0x01, 0x01, 0x11, 0x08, 0x0c, 0x0a, 0x06, 0x03, 0x05, 0x03, 0x07,
  0x09, 0x14, 0x14, 0x08, 0x0c, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x13, 0x0b, 0x08, 0x0c,
  0x0a, 0x06, 0x03, 0x04, 0x02, 0x07, 0x11, 0x14, 0x14, 0x08, 0x0c, 0x0a, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x11, 0x13, 0x0b, 0x08, 0x10, 0x0a, 0x08, 0x15, 0x17, 0x0c, 0x08, 0x15, 0x17, 0x17, 0x17,
  0x0c, 0x0a, 0x1e, 0x0a, 0x0c, 0x19, 0x20, 0x08, 0x10, 0x0a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x09, 0x0c, 0x13, 0x1a, 0x21,
  0x28, 0x2f, 0x36, 0x41, 0x4c, 0x57, 0x62, 0x65, 0x69, 0x70, 0x75,
};
/* The byte at 19:`off` of that table (MOV AL, CS:[off]); the callers' indexes stay inside it. */
static inline uint8_t cs_byte(uint16_t off) { return cs_tab[(uint16_t)(off - CS_TAB_BASE)]; }

/* The byte DS:(rec + disp), read signed: with rec = 0xc0 * player and disp 0x5a7f to 0x5a83, one of the
 * five bytes +0x1b to +0x1f of the current player's record DS:0x5a64 + 0xc0 * player that 19:0x12f0
 * compares with minimum levels. */
static inline int8_t pl_attr(uint16_t rec, uint16_t disp) { return (int8_t)drd8((uint16_t)(rec + disp)); }

/* ai2_create_designs: 19:0x12f0 (Ghidra FUN_1090_12f0, alias fn_19_12f0), 675 bytes. ENTER 0x90,0;
 * PUSH DI; PUSH SI. FAR CDECL, no arguments, no result. The twin of 17:0x1730 / 17:0x38b0 for the
 * type-2 player: builds designs in the slots 4, 5, 14, 1, 0, 6, 2, 3 and 9 (in that order) of the
 * 0x8d-byte record array DS:0x417a whose bit 1 of byte +0x7c (DS:0x442a, 0x44b7, 0x49ac, 0x4283,
 * 0x41f6, 0x4544, 0x4310, 0x439d, 0x46eb) is set and (for all but slots 1 and 0) whose minimum levels
 * (the current player's bytes +0x1b to +0x1f, DS:0x14c, record DS:0x5a64 + 0xc0 * player, signed)
 * are met, through 18:0x0000(slot | hull word << 16, 19:(0x2d7 + a table byte) as the part list).
 * 18:0x0000 builds a design record and stores it (it queues no message). Slots 14 and 9 try up to
 * 4 / 5 random table entries (8:0x8730 with n = 1 / 4) until 18:0x0000 answers non-zero. Slots 1 and
 * 0 are also taken when the bit is clear but the dword +0x83 of the record (DS:0x428a / DS:0x41fd)
 * is zero; then they first copy the 0x8d-byte record of the slot (DS:0x4207 / DS:0x417a) to the
 * local buffer BP-0x8e, set bit 1 of its byte at +0x7c and pass it to 18:0x0630 (with the slot,
 * 1 / 0). The local buffer's address is visible to 18:0x0630. */
DECOMP(19, 0x12f0, ai2_create_designs, FAR_CDECL)
void ai2_create_designs(void)
{
  FRAME_ENTER(0x90 + 4);
  uint16_t bx, si;
  /* the design call: 18:0x0000(slot | hull word << 16, 19:off of the part list) */
#define MSG(lo, hi, tabbyte) \
  (uint16_t)CALL_ORIG(18, 0x0000, FAR_CDECL, 4, (lo), (hi), (uint16_t)((tabbyte) + 0x2d7), SEG_SEL(19))
  if ((drd8(0x442a) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a80) >= 5) (void)MSG(4, 1, cs_byte(0x39b));
  }
  if ((drd8(0x44b7) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a80) >= 7) (void)MSG(5, 3, cs_byte(0x39f));
  }
  if ((drd8(0x49ac) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a7f) >= 5 && pl_attr(bx, 0x5a82) >= 6 && pl_attr(bx, 0x5a81) >= 4 &&
        pl_attr(bx, 0x5a80) >= 5) {
      si = 0;
      do {
        uint16_t n = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 1);
        if (MSG(0x000e, 6, cs_byte((uint16_t)(n + 0x391))) != 0) break;
        si++;
      } while ((int16_t)si < 4);
    }
  }
  if ((drd8(0x4283) & 2) != 0 || drd32(0x428a) == 0) {
    if ((drd8(0x4283) & 2) == 0) {
      memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x8e))), LINN(0x4207), 0x8d);
      BP_LOCAL(uint8_t, -0x12) = (uint8_t)(BP_LOCAL(uint8_t, -0x12) | 2);
      (void)CALL_ORIG(18, 0x0630, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x8e), 1);
    }
    (void)MSG(1, 1, cs_byte(0x38d));
  }
  if ((drd8(0x41f6) & 2) != 0 || drd32(0x41fd) == 0) {
    if ((drd8(0x41f6) & 2) == 0) {
      memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x8e))), LINN(0x417a), 0x8d);
      BP_LOCAL(uint8_t, -0x12) = (uint8_t)(BP_LOCAL(uint8_t, -0x12) | 2);
      (void)CALL_ORIG(18, 0x0630, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x8e), 0);
    }
    (void)MSG(0, 4, cs_byte(0x38e));
  }
  if ((drd8(0x4544) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a81) >= 4 && pl_attr(bx, 0x5a80) >= 5 && pl_attr(bx, 0x5a83) >= 6)
      (void)MSG(6, 0x0b, cs_byte(0x39e));
  }
  if ((drd8(0x4310) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a7f) >= 8 && pl_attr(bx, 0x5a82) >= 7 && pl_attr(bx, 0x5a81) >= 6 &&
        pl_attr(bx, 0x5a80) >= 7)
      (void)MSG(2, 0x11, cs_byte(0x39c));
  }
  if ((drd8(0x439d) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a7f) >= 0x0b && pl_attr(bx, 0x5a82) >= 0x0c && pl_attr(bx, 0x5a81) >= 0x0f &&
        pl_attr(bx, 0x5a80) >= 9)
      (void)MSG(3, 0x13, cs_byte(0x39d));
  }
  if ((drd8(0x46eb) & 2) != 0) {
    bx = (uint16_t)((uint16_t)drd16(0x14c) * 0xc0);
    if (pl_attr(bx, 0x5a7f) >= 5 && pl_attr(bx, 0x5a82) >= 6 && pl_attr(bx, 0x5a81) >= 0x0d &&
        pl_attr(bx, 0x5a80) >= 7) {
      si = 0;
      do {
        uint16_t n = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 4);
        if (MSG(9, 9, cs_byte((uint16_t)(n + 0x397))) != 0) {
          FRAME_LEAVE();
          return;
        }
        si++;
      } while ((int16_t)si < 5);
    }
  }
#undef MSG
  FRAME_LEAVE();
}

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
/* ai7_turn: 19:0x0000 (Ghidra FUN_1090_0000, alias fn_19_0000), 709 bytes. ENTER 0x40,0; PUSH DI; PUSH SI.
 * FAR CDECL (param_1 at [BP+6], passed on to 18:0x59ba). No result.
 * Walks the fleet pointer table (far pointer DS:0xba, count DS:0x553a) and, for each fleet of the
 * current player (DS:0x14c) with a waypoint count (+0x62) of 0 or 1: clears bit 7 of its flags
 * (+5), selects it as the current map object (9:0x2a74(2, fleet id), kind 2 = fleet) and then, by
 * the fleet's kind (16:0x175c fleet_has_scout_frigate_destroyer; 16:0x16f6 fleet_has_colony_hull)
 * and its target (+6, +0x5c), asks 18:0x0dee / 18:0x098a / 18:0x4510 for a new order, adds a
 * waypoint through 18:0x2664 (directly, or via 18:0x0864 for a planet or 18:0x08fe for a location),
 * updates word +0xa (word +6 of the first entry) of the waypoint list at DS:0x4bf0 and sets to 4
 * byte +0xf of the info array slot (DS:0x1600, 16 bytes per planet id) of the planet id that
 * 18:0x0dee / 18:0x098a returned (an index into the planet table DS:0x31ba, not the fleet's id).
 * It frames the walk with 18:0x28fc (save, flag 0xf from turn DS:0x82 >= 0x14) and 18:0x59ba
 * (restore), then calls 21:0x1e2a.
 * The 18-byte record at BP-0x40 for 18:0x2664 is only partly written (the rest is stack junk the
 * callee sees). */
DECOMP(19, 0x0000, ai7_turn, FAR_CDECL)
void ai7_turn(uint16_t param_1)
{
  FRAME_ENTER(0x40 + 4);
  uint16_t di, si = 0, bx, es;
  uint16_t flag = (drd16(0x0082) < 0x14) ? 0 : 0x0f;
  L16(-8) = (uint16_t)CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0, 0, 0, (uint16_t)(rt_bp - 0xa), flag);
  dwr16(0x255c, 0);
  L16(-6) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L2a3;
  di = 0;
L03b:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  L32(-4) = rd32(es, (uint16_t)(bx + di));
  if (L32(-4) == 0) goto L2a3;
  es = L16(-2);
  bx = L16(-4);
  if (rd16(es, (uint16_t)(bx + 2)) != drd16(0x014c)) goto L293;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x62)) > 1) goto L293;
  wr8(es, (uint16_t)(bx + 5), (uint8_t)(rd8(es, (uint16_t)(bx + 5)) & 0x7f));
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, bx));
  if (fleet_has_scout_frigate_destroyer(MK_FP(L16(-2), L16(-4))) == 0) goto L172;
  es = L16(-2);
  bx = L16(-4);
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x5c)) >= 2) goto L0ab;
  if (rd16(es, (uint16_t)(bx + 6)) == 0xffff) goto L1d3;
  if (drd16(0x4c0a) != drd16(0x014c)) goto L1d3;
L0ab:
  si = (uint16_t)CALL_ORIG(18, 0x0dee, FAR_CDECL, 3, L16(-4), L16(-2), (uint16_t)(rt_bp - 0xe));
  if (si == 0xffff) goto L156;
  es = L16(-2);
  bx = L16(-4);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, bx));
  es = L16(-2);
  bx = L16(-4);
  if (rd16(es, (uint16_t)(bx + 6)) == 0xffff) goto L111;
  if (drd16(0x4c0a) != drd16(0x014c)) goto L111;
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(es, (uint16_t)(bx + 6)), 2, rd16(es, bx), 4, 0x32);
  es = L16(-2);
  bx = L16(-4);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c);
L111:
  L16(-0x3c) = si;
  L16(-0x3a) = (uint16_t)((L16(-0x3a) & 0xf1ff) | 0x0100);
  bx = (uint16_t)(si << 2);
  L32(-0x40) = rd32(MEM_SEL_DGROUP, (uint16_t)(bx + 0x31ba));
  L8(-0x3a) = (uint8_t)(L8(-0x3a) & 0xf0);
  L8(-0x39) = (uint8_t)(L8(-0x39) | 0x10);
  L16(-0x3a) = (uint16_t)((L16(-0x3a) & 0xff4f) | 0x0040);
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, L16(-4), L16(-2), (uint16_t)(rt_bp - 0x40), 0);
  goto L281;
L156:
  if (L32(-0xe) == 0) goto L293;
  (void)CALL_ORIG(18, 0x08fe, FAR_CDECL, 4, L16(-4), L16(-2), L16(-0xe), L16(-0xc));
  goto L293;
L172:
  if (fleet_has_colony_hull(MK_FP(L16(-2), L16(-4))) == 0) goto L293;
  es = L16(-2);
  bx = L16(-4);
  if (rd16(es, (uint16_t)(bx + 6)) == 0xffff) goto L1a2;
  if (drd16(0x4c0a) != drd16(0x014c)) goto L1a2;
  if ((int32_t)drd32(0x4c30) >= 0x32) goto L1f4;
L1a2:
  if (rd32(es, (uint16_t)(bx + 0x5c)) != 0) goto L1f4;
  if (drd16(0x4b92) == 0xffff) goto L1c1;
  if (drd16(0x4c0a) != drd16(0x014c)) goto L1c1;
  if ((drd8(0x4c0d) & 2) != 0) goto L1d3;
L1c1:
  if ((uint16_t)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, bx, es, 0) != 0) goto L293;
L1d3:
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr16(es, (uint16_t)(bx + 0xa), (uint16_t)((rd16(es, (uint16_t)(bx + 0xa)) & 0xfff5) | 5));
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  goto L293;
L1f4:
  si = (uint16_t)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, bx, L16(-2), 0);
  if (si == 0xffff) goto L293;
  es = L16(-2);
  bx = L16(-4);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, bx));
  es = L16(-2);
  bx = L16(-4);
  if (rd16(es, (uint16_t)(bx + 6)) == 0xffff) goto L274;
  if (drd16(0x4c0a) != drd16(0x014c)) goto L274;
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(es, (uint16_t)(bx + 6)), 2, rd16(es, bx), 4, 0x64);
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(es, (uint16_t)(bx + 6)), 2, rd16(es, bx), 3, 0x19);
  es = L16(-2);
  bx = L16(-4);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c);
L274:
  (void)CALL_ORIG(18, 0x0864, FAR_CDECL, 3, L16(-4), L16(-2), si);
L281:
  bx = (uint16_t)((uint16_t)(si << 4) + drd16(0x1600));
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + 0xf), 4);
L293:
  di = (uint16_t)(di + 4);
  L16(-6) = (uint16_t)(L16(-6) + 1);
  if ((int16_t)L16(-6) < (int16_t)drd16(0x553a)) goto L03b;
L2a3:
  (void)CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-8), param_1, L16(-0xa), (uint16_t)(rt_bp - 0x2e),
                  (uint16_t)(rt_bp - 0x1e));
  (void)rt_call_far(MK_FP(SEG_SEL(21), 0x1e2a), FAR_CDECL, NULL, 0);
  FRAME_LEAVE();
}

/* Fields of a record at seg:base (word, byte, dword), with the 16-bit offset wrap of the original. */
#define RW(seg, base, d) rd16((seg), (uint16_t)((base) + (d)))
#define RB(seg, base, d) rd8((seg), (uint16_t)((base) + (d)))
#define RD(seg, base, d) rd32((seg), (uint16_t)((base) + (d)))
#define WW(seg, base, d, v) wr16((seg), (uint16_t)((base) + (d)), (uint16_t)(v))
#define WB(seg, base, d, v) wr8((seg), (uint16_t)((base) + (d)), (uint8_t)(v))
#define WD(seg, base, d, v) wr32((seg), (uint16_t)((base) + (d)), (uint32_t)(v))
/* The info array (DS:0x1600): 16 bytes per planet id; slot(id) is the offset of a slot's byte d. */
#define INFO_SEG drd16(0x1602)
#define INFO(idx16, d) ((uint16_t)(drd16(0x1600) + (uint16_t)((idx16)) + (d)))
#define HIGHLOW(hi, lo) (((uint32_t)(uint16_t)(hi) << 16) | (uint16_t)(lo))

/* ai3_turn: 19:0x1594 (Ghidra FUN_1090_1594, alias fn_19_1594), 2970 bytes. ENTER 0x72,0; PUSH DI; PUSH SI.
 * FAR CDECL (param_1 at [BP+6], passed on to 26:0x0100 and 18:0x59ba). No result. The older, shorter
 * variant of 19:0x0400 (same pieces, written before that one was extended). In order:
 *  1. 18:0x28fc saves state (flag 0xf when the turn DS:0x82 >= 0x14); 19:0x212e is called with the result.
 *  2. Counts the planets (table DS:0xb6, DS:0x4d96 records of 0x38 bytes) without an owner (+2 = -1)
 *     with info level (+4) >= 3 and 9:0x462a > 0 for the current player (DS:0x14c): BP-0xe.
 *  3. Walks the planets again: sets byte +9 of the planet's slot in the info array (DS:0x1600, 16 bytes
 *     per id) to 1; for an unowned planet of level >= 3 sums three bytes (+9..+0xb, halved, 0x4b above
 *     0x42) into slot byte +1 (capped to 0x7f); for another owner's planet sets slot +0xa and (9:0x462a
 *     > 0) slot +3; for the player's own planet with 9:0x474c < 0 sets slot +2; for an own planet with
 *     flags bit 1 and a dword at +0x28 of at least 1000 selects the planet
 *     (9:0x2a74(1, planet id), kind 1 = planet) and calls 26:0x0100(param_1),
 *     looks in the list at DS:0xcd2 for an entry and accordingly adds production entries through
 *     18:0x2732 to the list at DS:0xcd2 and commits or discards that list with 26:0x0810
 *     (argument 0 discards).
 *  4. Walks the fleets (DS:0xba, DS:0x553a): other owners' fleets are chained through +0x68 (list head
 *     BP-0x24); the player's own are chained (BP-0x28) when 18:0x2e4e says so and are cleared of flag
 *     0x80 and handled by their state (+0x1a, +0x1c, +0x62, +6, +0xe) with 7:0x01e4, 18:0x2e90, 28:0x21ee,
 *     18:0x2664, 9:0x2a74, 7:0x153e, 18:0x3bb4, the waypoint list DS:0x4bf0 and the slot bytes
 *     +1, +3.
 *  5. Walks the fleets once more and hands each of the player's idle ones its order (18:0x3c64,
 *     18:0x2664, 18:0x39f6, 18:0x19c4, 18:0x4510, 18:0x098a, 18:0x0864, 18:0x08fe, 18:0x0750, ...), sets slot
 *     byte +0xf to 4, and so on.
 *  6. 18:0x59ba restores the state; 21:0x1e2a is called.
 * Locals follow the original BP offsets; the 18-byte orders at BP-0x5c / BP-0x6e are only partly written
 * (the rest is stack junk the callee sees), and BP-0x18 is read before any write on one path (it is
 * stack junk the original reads too). DS is changed inside four loops and restored before the next call. */
DECOMP(19, 0x1594, ai3_turn, FAR_CDECL)
void ai3_turn(uint16_t param_1)
{
  FRAME_ENTER(0x72 + 4);
  uint16_t di = 0, si = 0, bx = 0, cx = 0, ax = 0, dx = 0, es = 0, ds = 0;
  uint32_t r;
  uint16_t flag = (drd16(0x0082) < 0x14) ? 0 : 0x0f;
  L32(-0x1a) = 0;
  L16(-0x16) = 0;
  L16(-0x2a) = (uint16_t)CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0, 0, 0, (uint16_t)(rt_bp - 0x14), flag);
  ai3_turn_nop(L16(-0x2a));
  ax = (uint16_t)((uint16_t)((uint16_t)drd16(0x4d96) * 0x38) + drd16(0x00b6));
  L16(-6) = drd16(0x00b8);
  cx = drd16(0x00b6);
  L16(-0x12) = cx;
  L16(-0x10) = drd16(0x00b8);
  if (ax <= cx) goto L1620;
  L16(-8) = ax;
  si = cx;
L15f2:
  es = L16(-0x10);
  if (RW(es, si, 2) != 0xffff) goto L1616;
  if (RB(es, si, 4) < 3) goto L1616;
  if ((int16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, si, es, drd16(0x014c)) > 0) di++;
L1616:
  si = (uint16_t)(si + 0x38);
  if (si < L16(-8)) goto L15f2;
L1620:
  L16(-0xe) = di;
  ax = drd16(0x00b6);
  di = ax;
  L16(-0x10) = drd16(0x00b8);
  ax = (uint16_t)(ax + (uint16_t)((uint16_t)drd16(0x4d96) * 0x38));
  L16(-6) = drd16(0x00b8);
  if (ax <= di) goto L1873;
  L16(-8) = ax;
L1642:
  es = L16(-0x10);
  si = (uint16_t)(rd16(es, di) << 4);
  wr8(INFO_SEG, INFO(si, 9), 1);
  es = L16(-0x10);
  if (RW(es, di, 2) != 0xffff) goto L16be;
  if (RB(es, di, 4) < 3) goto L16be;
  L16(-0x12) = di;
  si = 0;
  L8(-2) = 0;
  do {
    uint8_t b = rd8(L16(-0x10), (uint16_t)(L16(-0x12) + 9 + si));
    L8(-1) = (b > 0x42) ? 0x4b : (uint8_t)(b >> 1);
    L8(-2) = (uint8_t)(L8(-2) + L8(-1));
    si++;
  } while ((int16_t)si < 3);
  if ((L8(-2) & 0x80) != 0) L8(-2) = 0x7f;
  di = L16(-0x12);
  es = L16(-0x10);
  si = (uint16_t)(rd16(es, di) << 4);
  wr8(INFO_SEG, INFO(si, 1), L8(-2));
L16be:
  ax = drd16(0x014c);
  es = L16(-0x10);
  if (RW(es, di, 2) == ax) goto L1716;
  if (RW(es, di, 2) == 0xffff) goto L1716;
  ax = RW(es, di, 4);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 0xa), (uint8_t)(((ax >> 9) & 1) + 1));
  if ((int16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, di, L16(-0x10), drd16(0x014c)) <= 0) goto L1867;
  es = L16(-0x10);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 3), 1);
  goto L1867;
L1716:
  if (RW(es, di, 2) != ax) goto L1740;
  if ((int16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, di, es, ax) >= 0) goto L1740;
  es = L16(-0x10);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 2), 1);
  goto L1867;
L1740:
  es = L16(-0x10);
  if ((RB(es, di, 5) & 2) == 0) goto L1867;
  if ((int32_t)RD(es, di, 0x28) < 0x3e8) goto L1867;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, RW(es, di, 0));
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, param_1);
  L16(-0xc) = 0;
  L16(-0xa) = 0;
  dx = drd16(0x0cd4);
  L32(-4) = HIGHLOW(dx, (uint16_t)(drd16(0x0cd2) + 4));
  L8(-0x70) = RB(drd16(0x0cd4), drd16(0x0cd2), 3);
  if (L8(-0x70) == 0) {
    cx = L16(-0xa);
    goto L17c9;
  }
  L16(-0x12) = di;
  si = L16(-4);
  cx = L16(-0xa);
  ds = dx;
  for (;;) {
    if ((rd16(ds, (uint16_t)(si + 2)) & 0xe) == 4 && (rd32(ds, si) & 0x1fc00) > 0x4000) break;
    si = (uint16_t)(si + 4);
    cx++;
    if (!((int16_t)L8(-0x70) > (int16_t)cx)) break;
  }
  di = L16(-0x12);
L17c9:
  if (!((int16_t)L8(-0x70) <= (int16_t)cx)) {
    (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, 0);
    goto L1867;
  }
  if (drd16(0x0082) != 0) goto L181a;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0, 1, 2, 1);
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0, 1, 2, 1);
  goto L185c;
L181a:
  if (L16(-0x16) != 0) goto L185c;
  if (drd32(0x428a) != 0) {
    uint32_t t = (uint32_t)drd16(0x428a) + 1;
    uint16_t bhi = (uint16_t)(drd16(0x428c) + (uint16_t)(t >> 16));
    if (bhi > 0) goto L185c;
    if ((uint16_t)t >= L16(-0xe)) goto L185c;
  }
  L16(-0x16) = 1;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 1, 1, 2, 1);
  L16(-0xc) = 1;
L185c:
  (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, L16(-0xc));
L1867:
  di = (uint16_t)(di + 0x38);
  if (di < L16(-8)) goto L1642;
L1873:
  L32(-0x28) = 0;
  L32(-0x24) = 0;
  L16(-0x1a) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L1b7b;
  L16(-4) = 0;
L188c:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  di = L16(-4);
  ax = rd16(es, (uint16_t)(bx + di));
  dx = rd16(es, (uint16_t)(bx + di + 2));
  si = ax;
  L16(-0xc) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L1b7b;
  ax = drd16(0x014c);
  es = L16(-0xc);
  if (RW(es, si, 2) == ax) goto L18c4;
  WD(es, si, 0x68, L32(-0x24));
  L16(-0x24) = si;
  L16(-0x22) = es;
  goto L1b6a;
L18c4:
  if ((uint16_t)CALL_ORIG(18, 0x2e4e, FAR_CDECL, 2, si, es) == 0) goto L18e4;
  es = L16(-0xc);
  WD(es, si, 0x68, L32(-0x28));
  L16(-0x28) = si;
  L16(-0x26) = es;
L18e4:
  es = L16(-0xc);
  WB(es, si, 5, RB(es, si, 5) & 0x7f);
  if (RW(es, si, 0x1a) != 0) goto L18fd;
  es = L16(-0xc);
  if (RW(es, si, 0x1c) == 0) goto L1956;
L18fd:
  es = L16(-0xc);
  if ((int16_t)RW(es, si, 0x62) < 1) goto L1956;
  if (RW(es, si, 6) == 0xffff) goto L1934;
  L16(-0xe) = si;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, RW(es, si, 6));
  es = (uint16_t)(r >> 16);
  bx = (uint16_t)r;
  if (RW(es, bx, 2) != 0xffff) goto L1b30;
  es = L16(-0xc);
  bx = RW(es, si, 6);
  goto L1945;
L1934:
  if ((int16_t)RW(es, si, 0x62) <= 1) goto L1b6a;
  bx = RW(RW(es, si, 0x66), RW(es, si, 0x64), 0x1a);
L1945:
  si = (uint16_t)(bx << 4);
  wr8(INFO_SEG, INFO(si, 1), (uint8_t)(rd8(INFO_SEG, INFO(si, 1)) | 0x80));
  goto L1b6a;
L1956:
  if ((uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, si, es) == 0) goto L199a;
  L16(-2) = 0xffff;
  es = L16(-0xc);
  if ((int16_t)RW(es, si, 0x62) <= 1) goto L1994;
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  if ((RB(es, bx, 0x1d) & 0xf) != 1) goto L19f2;
  es = L16(-0xc);
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  L16(-2) = RW(es, bx, 0x1a);
  goto L19f2;
L1994:
  L16(-2) = RW(es, si, 6);
  goto L19f2;
L199a:
  es = L16(-0xc);
  if (RW(es, si, 0xe) == 0) goto L1b6a;
  L16(-2) = 0xffff;
  if ((int16_t)RW(es, si, 0x62) <= 1) goto L19d0;
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  if ((RB(es, bx, 0x1d) & 0xf) != 1) goto L19d7;
  es = L16(-0xc);
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  ax = RW(es, bx, 0x1a);
  goto L19d4;
L19d0:
  ax = RW(es, si, 6);
L19d4:
  L16(-2) = ax;
L19d7:
  if (L16(-2) == 0xffff) goto L1b6a;
  di = (uint16_t)(L16(-2) << 4);
  if (rd8(INFO_SEG, INFO(di, 3)) == 0) goto L1b06;
L19f2:
  if (L16(-2) == 0xffff) goto L1b6a;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-2));
  L16(-0x12) = (uint16_t)r;
  L16(-0x10) = (uint16_t)(r >> 16);
  if (r == 0) goto L1a26;
  bx = L16(-0x12);
  es = L16(-0x10);
  if (RW(es, bx, 2) == 0xffff) goto L1b6a;
  ax = drd16(0x014c);
  if (RW(es, bx, 2) == ax) goto L1b6a;
L1a26:
  L16(-0xe) = si;
  di = (uint16_t)(L16(-2) << 4);
  if (rd8(INFO_SEG, INFO(di, 3)) == 0) goto L1b30;
  es = L16(-0xc);
  if ((int32_t)RD(es, si, 0x58) <= 0) goto L1b30;
  bx = L16(-0x12);
  es = L16(-0x10);
  ax = (uint16_t)((uint16_t)(RW(es, bx, 2) * 0xc0) + 0x5a64);
  ax = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, ax, 0xe);
  if (ax == 8) goto L1b30;
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x5c))), 0, 18);
  bx = (uint16_t)(L16(-2) << 2);
  L32(-0x5c) = drd32((uint16_t)(bx + 0x31ba));
  L16(-0x56) = (uint16_t)((L16(-0x56) & 0xf1ff) | 0x0100);
  L16(-0x58) = L16(-2);
  L16(-0x56) = (uint16_t)((L16(-0x56) & 0xfff1) | 1);
  L8(-0x55) = (uint8_t)(L8(-0x55) | 0x10);
  L16(-0x4e) = (uint16_t)((L16(-0x4e) & 0x0fff) | 0x2000);
  es = L16(-0xc);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, si, 0));
  ax = L16(-2);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  di = (RW(es, bx, 8) == ax) ? (uint16_t)(bx + 4) : (uint16_t)(bx + 0x16);
  for (uint16_t k = 0; k < 18; k++) wr8(es, (uint16_t)(di + k), L8(-0x5c + (int16_t)k));
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  si = (uint16_t)(L16(-2) << 4);
  wr8(INFO_SEG, INFO(si, 3), (uint8_t)(rd8(INFO_SEG, INFO(si, 3)) | 0x80));
  goto L1b6a;
L1b06:
  L16(-0xe) = si;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-2));
  si = (uint16_t)r;
  L16(-0x10) = (uint16_t)(r >> 16);
  if (r == 0) goto L1b6a;
  es = L16(-0x10);
  if (RW(es, si, 2) == 0xffff) goto L1b6a;
  ax = drd16(0x014c);
  if (RW(es, si, 2) == ax) goto L1b6a;
L1b30:
  bx = L16(-0xe);
  es = L16(-0xc);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, bx, 0));
  dwr16(0x4bee, 1);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  WB(es, bx, 3, 1);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, L16(-0xe), L16(-0xc), 0);
L1b6a:
  L16(-4) = (uint16_t)(L16(-4) + 4);
  ax = drd16(0x553a);
  L16(-0x1a) = (uint16_t)(L16(-0x1a) + 1);
  if ((int16_t)L16(-0x1a) < (int16_t)ax) goto L188c;
L1b7b:
  dwr16(0x255c, 0);
  L16(-0x1a) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L210c;
  L16(-0x20) = 0;
L1b8e:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  si = L16(-0x20);
  ax = rd16(es, (uint16_t)(bx + si));
  dx = rd16(es, (uint16_t)(bx + si + 2));
  di = ax;
  L16(-0xc) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L210c;
  ax = drd16(0x014c);
  es = L16(-0xc);
  if (RW(es, di, 2) != ax) goto L20fb;
  if (RW(es, di, 0x1a) != 0) goto L203c;
  if (RW(es, di, 0x1c) != 0) goto L203c;
  if ((int16_t)RW(es, di, 0x62) > 1) goto L20fb;
  if (RW(es, di, 0xe) != 0) goto L1efc;
  ax = (uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, di, es);
  if (ax != 0) goto L1dd2;
  es = L16(-0xc);
  if (RW(es, di, 0x26) != ax) goto L1c4e;
  if (RW(es, di, 0x28) != ax) goto L1c4e;
  if (RW(es, di, 0xc) == ax) goto L20fb;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xc);
  if (RW(es, di, 0xc) == 0) goto L1c28;
  if (drd8(0x41b6) != 1) goto L1c28;
  if ((int32_t)RD(es, di, 0x5c) < 2) goto L1f5e;
L1c28:
  (void)CALL_ORIG(18, 0x3c64, FAR_CDECL, 8, di, L16(-0xc), L16(-0x28), L16(-0x26), L16(-0x24), L16(-0x22),
                  (uint16_t)((drd8(0x80) & 0x10) >> 4), (uint16_t)(rt_bp - 0x1e));
  goto L20fb;
L1c4e:
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xc);
  if (RW(es, di, 6) == 0xffff) goto L1d1c;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, RW(es, di, 6));
  si = (uint16_t)r;
  L16(-0x10) = (uint16_t)(r >> 16);
  ax = drd16(0x014c);
  es = (uint16_t)(r >> 16);
  if (RW(es, si, 2) != ax) goto L1cb6;
  if ((RB(es, si, 5) & 2) == 0) goto L1c9d;
  es = L16(-0xc);
  if ((int16_t)RW(es, di, 0x26) >= 2) goto L1c9d;
  if ((int16_t)RW(es, di, 0x28) < 2) goto L20fb;
L1c9d:
  es = L16(-0xc);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, RW(es, di, 0), 0x4b8c);
  L16(-0xe) = di;
  L16(-0x12) = si;
  goto L1d27;
L1cb6:
  L16(-0xe) = di;
  L16(-0x12) = si;
  if (RW(es, si, 2) == 0xffff) goto L1d27;
  ax = L16(-0x24);
  dx = L16(-0x22);
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L20fb;
  L16(-4) = ax;
  si = ax;
L1cd7:
  es = L16(-0xc);
  ax = RW(es, di, 8);
  es = L16(-2);
  if (RW(es, si, 8) != ax) goto L1d05;
  es = L16(-0xc);
  ax = RW(es, di, 0xa);
  es = L16(-2);
  if (RW(es, si, 0xa) != ax) goto L1d05;
  if ((uint16_t)CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, si, es) != 0) goto L1d27;
L1d05:
  es = L16(-2);
  ax = RW(es, si, 0x68);
  dx = RW(es, si, 0x6a);
  si = ax;
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) != 0) goto L1cd7;
  goto L20fb;
L1d1c:
  L16(-0xe) = di;
  L32(-0x12) = L32(-0x18);
L1d27:
  if ((drd8(0x80) & 0x10) == 0) {
    ax = 0;
    dx = 0;
  } else {
    r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-0x12), L16(-0x10), 0x2252, SEG_SEL(17));
    ax = (uint16_t)r;
    dx = (uint16_t)(r >> 16);
  }
  si = ax;
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) == 0) {
    r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-0x12), L16(-0x10), 0x216c, SEG_SEL(17));
    si = (uint16_t)r;
    L16(-2) = (uint16_t)(r >> 16);
  }
  ax = L16(-2);
  L16(-0x12) = si;
  L16(-0x10) = ax;
  if ((uint16_t)(ax | si) == 0) goto L20fb;
  bx = L16(-0x12);
  es = L16(-0x10);
  si = (uint16_t)(RW(es, bx, 0) << 4);
  wr8(INFO_SEG, INFO(si, 0xa), (uint8_t)(rd8(INFO_SEG, INFO(si, 0xa)) | 0x80));
  bx = L16(-0x12);
  es = L16(-0x10);
  L16(-0x6a) = RW(es, bx, 0);
  L16(-0x68) = (uint16_t)((L16(-0x68) & 0xf1ff) | 0x0100);
  bx = (uint16_t)(RW(es, bx, 0) << 2);
  L32(-0x6e) = drd32((uint16_t)(bx + 0x31ba));
  L8(-0x68) = (uint8_t)(L8(-0x68) & 0xf0);
  L8(-0x67) = (uint8_t)(L8(-0x67) | 0x10);
  L16(-0x68) = (uint16_t)((L16(-0x68) & 0xff4f) | 0x0040);
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, L16(-0xe), L16(-0xc), (uint16_t)(rt_bp - 0x6e), 0);
  goto L20fb;
L1dd2:
  L16(-0xe) = di;
  ax = drd16(0x00b6);
  dx = drd16(0x00b8);
  bx = ax;
  L16(-0x10) = dx;
  cx = (uint16_t)((uint16_t)drd16(0x4d96) * 0x38);
  ax = (uint16_t)(ax + cx);
  di = ax;
  L16(-6) = dx;
  if (ax <= bx) goto L1e07;
  L16(-8) = ax;
  for (;;) {
    ds = dx;
    ax = drd16(0x014c);
    if (rd16(ds, (uint16_t)(bx + 2)) == ax && (rd8(ds, (uint16_t)(bx + 5)) & 2) != 0) break;
    bx = (uint16_t)(bx + 0x38);
    if (!(bx < di)) break;
  }
L1e07:
  ax = L16(-6);
  if (di != bx) goto L1e2a;
  if (ax != L16(-0x10)) goto L1e2a;
  L32(-0x18) = 0;
  goto L1e33;
L1e2a:
  ax = L16(-0x10);
  L16(-0x18) = bx;
  L16(-0x16) = ax;
L1e33:
  if (L32(-0x18) == 0) goto L210c;
  L32(-0x12) = 0;
  L16(-0xa) = 0;
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) <= 0) goto L1ea1;
  si = (uint16_t)(bx + 6);
  L16(-6) = es;
L1e5a:
  cx = 0;
  es = L16(-6);
  ax = rd16(es, si);
  L16(-0x72) = ax;
  if ((int16_t)ax <= 0) goto L1e89;
  ax = (uint16_t)(si + 2);
  L16(-4) = ax;
  L16(-2) = es;
  L16(-8) = si;
  bx = ax;
  ds = L16(-0xc);
L1e7a:
  es = L16(-2);
  ax = rd16(es, bx);
  di = L16(-0xe);
  if (rd16(ds, di) != ax) {
    bx = (uint16_t)(bx + 2);
    cx++;
    if ((int16_t)L16(-0x72) > (int16_t)cx) goto L1e7a;
  }
L1e89:
  if ((int16_t)L16(-0x72) > (int16_t)cx) goto L1ea1;
  si = (uint16_t)(si + 0x14);
  L16(-0xa) = (uint16_t)(L16(-0xa) + 1);
  ax = L16(-0xa);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) > (int16_t)ax) goto L1e5a;
L1ea1:
  si = L16(-0x12);
  ax = L16(-0xa);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) <= (int16_t)ax) goto L1ec5;
  di = (uint16_t)((int16_t)ax * 0x14);
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, RW(es, (uint16_t)(bx + di), 4));
  si = (uint16_t)r;
  L16(-0x10) = (uint16_t)(r >> 16);
L1ec5:
  ax = L16(-0x10);
  if ((uint16_t)(ax | si) == 0) {
    cx = L16(-0x18);
    dx = L16(-0x16);
    L16(-2) = dx;
  } else {
    ax = L16(-0x10);
    cx = si;
    L16(-2) = ax;
  }
  (void)CALL_ORIG(18, 0x19c4, FAR_CDECL, 4, L16(-0xe), L16(-0xc), cx, L16(-2));
  goto L20fb;
L1efc:
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xc);
  if (RW(es, di, 6) == 0xffff) goto L1f24;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L1f24;
  if ((int32_t)drd32(0x4c30) >= 0x32) goto L1f8e;
L1f24:
  if (RD(es, di, 0x58) != 0) goto L1f8e;
  if (drd16(0x4b92) == 0xffff) goto L1f45;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L1f45;
  if ((drd8(0x4c0d) & 2) != 0) goto L20fb;
L1f45:
  if (drd8(0x4243) < 2) goto L1f5e;
  ax = (uint16_t)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, di, es, 0);
  if (ax != 0) goto L20fb;
L1f5e:
  es = L16(-0xc);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  WW(es, bx, 0xa, (RW(es, bx, 0xa) & 0xfff5) | 5);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  goto L20fb;
L1f8e:
  L32(-0x1e) = 0;
  ax = 0;
  if (drd8(0x4243) > 1) ax = (uint16_t)(rt_bp - 0x1e);
  si = (uint16_t)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, di, L16(-0xc), ax);
  es = L16(-0xc);
  if (RW(es, di, 6) == 0xffff) goto L1ffb;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L1ffb;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xc);
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, RW(es, di, 6), 2, RW(es, di, 0), 3, 0x19);
  es = L16(-0xc);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, RW(es, di, 0), 0x4b8c);
L1ffb:
  if (si == 0xffff) goto L2022;
  (void)CALL_ORIG(18, 0x0864, FAR_CDECL, 3, di, L16(-0xc), si);
  bx = (uint16_t)((uint16_t)(si << 4) + drd16(0x1600));
  es = drd16(0x1602);
  WB(es, bx, 0xf, 4);
  goto L20fb;
L2022:
  if (L32(-0x1e) == 0) goto L20fb;
  (void)CALL_ORIG(18, 0x08fe, FAR_CDECL, 4, di, L16(-0xc), L16(-0x1e), L16(-0x1c));
  goto L20fb;
L203c:
  if (RW(es, di, 6) == 0xffff) goto L20fb;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xc);
  si = (uint16_t)(RW(es, di, 6) << 4);
  if (rd8(INFO_SEG, INFO(si, 1)) >= 4) goto L20fb;
  r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, 0x4c08, MEM_SEL_DGROUP, 0x3c30, SEG_SEL(17));
  si = (uint16_t)r;
  L16(-0x10) = (uint16_t)(r >> 16);
  if (r == 0) goto L20fb;
  es = L16(-0x10);
  L16(-0x6a) = rd16(es, si);
  L16(-0x68) = (uint16_t)((L16(-0x68) & 0xf1ff) | 0x0100);
  bx = (uint16_t)(rd16(es, si) << 2);
  L32(-0x6e) = drd32((uint16_t)(bx + 0x31ba));
  L16(-0x68) = (uint16_t)((L16(-0x68) & 0xfff3) | 3);
  L8(-0x67) = (uint8_t)(L8(-0x67) | 0x10);
  L16(-0x68) = (uint16_t)((L16(-0x68) & 0xff6f) | 0x0060);
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, di, L16(-0xc), (uint16_t)(rt_bp - 0x6e), 1);
  es = L16(-0x10);
  bx = (uint16_t)(rd16(es, si) << 4);
  wr8(INFO_SEG, INFO(bx, 1), (uint8_t)(rd8(INFO_SEG, INFO(bx, 1)) | 0x80));
  es = L16(-0xc);
  si = (uint16_t)(RW(es, di, 6) << 4);
  wr8(INFO_SEG, INFO(si, 1), (uint8_t)(rd8(INFO_SEG, INFO(si, 1)) & 0x80));
L20fb:
  L16(-0x20) = (uint16_t)(L16(-0x20) + 4);
  ax = drd16(0x553a);
  L16(-0x1a) = (uint16_t)(L16(-0x1a) + 1);
  if ((int16_t)L16(-0x1a) < (int16_t)ax) goto L1b8e;
L210c:
  (void)CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-0x2a), param_1, L16(-0x14), (uint16_t)(rt_bp - 0x4a),
                  (uint16_t)(rt_bp - 0x3a));
  (void)rt_call_far(MK_FP(SEG_SEL(21), 0x1e2a), FAR_CDECL, NULL, 0);
  FRAME_LEAVE();
}

/* ai2_turn: 19:0x0400 (Ghidra FUN_1090_0400, alias fn_19_0400), 3823 bytes. ENTER 0x94,0; PUSH DI; PUSH SI.
 * FAR CDECL (param_1 at [BP+6], passed on to 26:0x0100 and 18:0x59ba). No result. The main turn of a
 * computer player, in six steps:
 *  1. 18:0x28fc saves state (flag 0x14 when the turn DS:0x82 >= 0xa); 18:0x36aa(0x1e0c, 0x40, 0x4000)
 *     sets option bits by turn (above 0x32, above 0x1e); the turn sets the byte limits DS:0x5340 (3 +
 *     (turn - 0x78) / 0x14, at most 0x32), DS:0x5341 (half of it), DS:0x5342 (6 + (turn - 0x64) / 0x16,
 *     at most 0xc) and DS:0x5343 (min(DS:0x5342 / 2 - 1, 3)); four calls of 18:0x5d3c pick targets into
 *     BP-0x14, BP-0x16, BP-0x18, BP-0x1a (BP-0x2c = the result of the second); 18:0x5bae(BP-0x4a) above
 *     turn 0x3c; 19:0x12f0 (through rt_call_far, it mirrors a frame).
 *  2. Counts the unowned planets of level >= 3 with 9:0x462a > 0 (BP-0x2a).
 *  3. Walks the planets: slot bytes of the info array (DS:0x1600), then for the player's own planet with
 *     flags bit 1 and a dword at +0x28 >= 1500: selects the planet (9:0x2a74(1, planet id)),
 *     calls 26:0x0100, adds production entries to the list at DS:0xcd2 with 18:0x2732 and commits
 *     or discards that list with 26:0x0810 (decided by the list at DS:0xcd2, the player's planet
 *     count, the table at DS:0x1604, 8:0x8730 random draws, 19:0x03a0 and the cost words of
 *     7:0x3b60), and calls 9:0x4de8 (its result is not used).
 *  4. Walks the fleets: other owners' fleets are chained through +0x68 (BP-0x2e), the player's
 *     through +0x68 (BP-0x32) when 18:0x2db2 says so; idle ones are examined (+0xe, +0x62, +6,
 *     +0x58, the slot byte +3) and update the waypoint list at DS:0x4bf0: they add a
 *     waypoint (18:0x2664) or clear the low nibble of the list's byte +0xa (18:0x3bb4).
 *  5. Walks the fleets once more (DS:0x255c = 0) and gives each of the player's idle fleets its order
 *     (18:0x3c64, 18:0x2664, 18:0x39f6, 18:0x19c4, 18:0x4510, 18:0x098a, 18:0x0864, 18:0x0750, ...).
 *  6. 18:0x59ba restores the state; 21:0x1e2a is called.
 * Locals follow the original BP offsets. The fleet being handled is BP-0xe:si (loop 4) or BP-0xe:di (loop 5);
 * the planet far pointer is BP-0x1e:BP-0x1c in loop 3. The 18-byte orders at BP-0x7c / BP-0x8e are only
 * partly written (the rest is stack junk the callee sees). DS is changed inside several loops and
 * restored before the next call. The dword-sized `cdq` of the listing is CWD (opcode 0x99 without a
 * size prefix). */
DECOMP(19, 0x0400, ai2_turn, FAR_CDECL)
void ai2_turn(uint16_t param_1)
{
  FRAME_ENTER(0x94 + 4);
  uint16_t di = 0, si = 0, bx = 0, cx = 0, ax = 0, dx = 0, es = 0, ds = 0;
  uint32_t r;
  L32(-0x30) = 0;
  L16(-0x2a) = 0;
  ax = (drd16(0x0082) < 0x0a) ? 0 : 0x14;
  L16(-0x90) = (uint16_t)CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, CS_TAB_BASE, SEG_SEL(19), 0x12, (uint16_t)(rt_bp - 0x12), ax);
  if (drd16(0x0082) > 0x32) {
    (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x1e0c);
    (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x40);
    goto L0457;
  }
  if (drd16(0x0082) <= 0x1e) goto L0462;
L0457:
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x4000);
L0462:
  si = 3;
  if (drd16(0x0082) > 0x82) si = (uint16_t)((uint16_t)(drd16(0x0082) - 0x78) / 0x14 + 3);
  if ((int16_t)si > 0x32) si = 0x32;
  dwr8(0x5340, (uint8_t)si);
  dwr8(0x5341, (uint8_t)((uint8_t)si >> 1));
  si = 6;
  if (drd16(0x0082) > 0x73) si = (uint16_t)((uint16_t)(drd16(0x0082) - 0x64) / 0x16 + 6);
  L16(-6) = si;
  if ((int16_t)si > 0xc) L16(-6) = 0xc;
  dwr8(0x5342, L8(-6));
  dx = ((int16_t)L16(-6) < 0) ? 0xffff : 0; /* CWD */
  ax = (uint16_t)((int16_t)(uint16_t)(L16(-6) - dx) >> 1);
  ax = (uint16_t)(ax - 1);
  if ((int16_t)ax > 3) ax = 3;
  dwr8(0x5343, (uint8_t)ax);
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x4a))), 0, 16);
  if (drd16(0x0082) < 0x78) si = 0x32;
  else if (drd16(0x0082) >= 0xc8) si = 0x64;
  else si = 0x46;
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x000b, 0x000c, si, (uint16_t)(rt_bp - 0x14), (uint16_t)(rt_bp - 0x4a));
  L16(-0x2c) = (uint16_t)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0004, 0x0005, si, (uint16_t)(rt_bp - 0x16),
                                   (uint16_t)(rt_bp - 0x4a));
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0002, 0x0003, si, (uint16_t)(rt_bp - 0x18), (uint16_t)(rt_bp - 0x4a));
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0009, 0x000a, si, (uint16_t)(rt_bp - 0x1a), (uint16_t)(rt_bp - 0x4a));
  if (drd16(0x0082) > 0x3c) (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x4a));
  {
    const uint16_t a12f0[1] = {L16(-0x90)};
    (void)rt_call_far(MK_FP(SEG_SEL(19), 0x12f0), FAR_CDECL, a12f0, 1);
  }
  /* 2. count the unowned planets of level >= 3 that 9:0x462a rates above 0 */
  ax = (uint16_t)((uint16_t)((uint16_t)drd16(0x4d96) * 0x38) + drd16(0x00b6));
  L16(-0x22) = drd16(0x00b8);
  cx = drd16(0x00b6);
  L16(-0x1c) = drd16(0x00b8);
  if (ax <= cx) goto L05d7;
  L16(-0x24) = ax;
  L16(-0x1e) = cx;
  si = cx;
  di = L16(-0x2a);
L05a6:
  es = L16(-0x1c);
  if (RW(es, si, 2) != 0xffff) goto L05ca;
  if (RB(es, si, 4) < 3) goto L05ca;
  if ((int16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, si, es, drd16(0x014c)) > 0) di++;
L05ca:
  si = (uint16_t)(si + 0x38);
  if (si < L16(-0x24)) goto L05a6;
  L16(-0x2a) = di;
L05d7:
  ax = drd16(0x00b6);
  dx = drd16(0x00b8);
  di = ax;
  L16(-0x1c) = dx;
  cx = (uint16_t)((uint16_t)drd16(0x4d96) * 0x38);
  ax = (uint16_t)(ax + cx);
  L16(-0x22) = dx;
  if (ax <= di) goto L0aaf;
  L16(-0x24) = ax;
L05f6:
  es = L16(-0x1c);
  si = (uint16_t)(rd16(es, di) << 4);
  wr8(INFO_SEG, INFO(si, 9), 1);
  ax = drd16(0x014c);
  es = L16(-0x1c);
  if (RW(es, di, 2) == ax) goto L0660;
  if (RW(es, di, 2) == 0xffff) goto L0660;
  ax = RW(es, di, 4);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 0xa), (uint8_t)(((ax >> 9) & 1) + 1));
  if ((int16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, di, L16(-0x1c), drd16(0x014c)) <= 0) goto L065a;
  es = L16(-0x1c);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 3), 1);
L065a:
  L16(-0x1e) = di;
  goto L0aa1;
L0660:
  if (RW(es, di, 2) != ax) goto L068a;
  if ((int16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, di, es, ax) >= 0) goto L068a;
  es = L16(-0x1c);
  si = (uint16_t)(RW(es, di, 0) << 4);
  wr8(INFO_SEG, INFO(si, 2), 1);
  goto L065a;
L068a:
  L16(-0x1e) = di;
  es = L16(-0x1c);
  if ((RB(es, di, 5) & 2) == 0) goto L0aa1;
  if ((int32_t)RD(es, di, 0x28) < 0x5dc) goto L0aa1;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, RW(es, di, 0));
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, param_1);
  L32(-0xc) = 0;
  dx = drd16(0x0cd4);
  L32(-4) = HIGHLOW(dx, (uint16_t)(drd16(0x0cd2) + 4));
  L16(-0x1e) = di;
  L8(-0x92) = RB(drd16(0x0cd4), drd16(0x0cd2), 3);
  if (L8(-0x92) == 0) {
    cx = L16(-0xc);
    goto L06e7;
  }
  si = L16(-4);
  cx = L16(-0xc);
  ds = L16(-2);
  for (;;) {
    if ((rd16(ds, (uint16_t)(si + 2)) & 0xe) == 4 && (rd32(ds, si) & 0x1fc00) < 0x4000) break;
    si = (uint16_t)(si + 4);
    cx++;
    if (!((int16_t)L8(-0x92) > (int16_t)cx)) break;
  }
L06e7:
  if (!((int16_t)L8(-0x92) <= (int16_t)cx)) {
    (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, 0);
    goto L0aa1;
  }
  bx = (uint16_t)((uint16_t)drd16(0x014c) * 0xc0);
  si = (uint16_t)((int16_t)drd16((uint16_t)(bx + 0x5a66)) / 10);
  es = drd16(0x1606);
  ax = (uint16_t)(rd16(es, (uint16_t)(drd16(0x1604) + 2)) * 2);
  if ((int16_t)si < (int16_t)ax) si = ax;
  if ((int8_t)drd8((uint16_t)(bx + 0x5a80)) < 5) goto L079e;
  if (L16(-0x16) == 0xffff) goto L079e;
  if ((int16_t)L16(-0x2c) < (int16_t)si) goto L0786;
  ax = (uint16_t)((int16_t)(uint16_t)(si * 10) / 7);
  if ((int16_t)ax <= (int16_t)L16(-0x2c)) goto L079e;
  if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 4) != 0) goto L079e;
L0786:
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x16), 1, 2, 1);
  L16(-0xa) = 1;
L079e:
  if (L16(-0x2a) == 0) goto L07cc;
  if (drd32(0x428a) != 0) goto L07cc;
  if (drd16(0x0082) <= 0x0a) goto L07cc;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 1, 1, 2, 1);
  L16(-0xa) = 1;
L07cc:
  (void)CALL_ORIG(28, 0x41d4, FAR_CDECL, 1, drd16(0x014c));
  (void)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, L16(-0x1e), L16(-0x1c), drd16(0x014c));
  if ((drd8(0x4544) & 2) != 0) goto L08a5;
  if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 3) != 0) goto L08a5;
  bx = L16(-0x1e);
  es = L16(-0x1c);
  L16(-6) = rd16(es, bx);
  cx = 0;
  L16(-8) = cx;
  if ((int16_t)drd16(0x553a) <= (int16_t)cx) goto L0859;
  L32(-4) = drd32(0x00ba);
  di = L16(-4);
  for (;;) {
    ds = L16(-2);
    bx = rd16(ds, di);
    es = rd16(ds, (uint16_t)(di + 2));
    if ((uint16_t)(es | bx) == 0) break;
    if (RW(es, bx, 6) == L16(-6) && (int16_t)RW(es, bx, 0x18) > 0 && RW(es, bx, 2) == drd16(0x014c)) {
      L16(-8) = RW(es, bx, 0x18);
      break;
    }
    di = (uint16_t)(di + 4);
    cx++;
    if (!((int16_t)drd16(0x553a) > (int16_t)cx)) break;
  }
L0859:
  if ((int16_t)L16(-8) < 0xa) goto L0879;
  if ((int16_t)L16(-8) >= 0x11) goto L08a5;
  if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 8) != 0) goto L08a5;
L0879:
  ax = (uint16_t)(L16(-8) + L16(-8) + 1);
  if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax) != 0) goto L08a5;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 6, 3, 2, 1);
  L16(-0xa) = 1;
L08a5:
  if (L16(-0x18) == 0xffff) goto L090a;
  es = L16(-0x1c);
  L16(-2) = RW(es, L16(-0x1e), 0);
  L16(-0x20) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L090a;
  di = 0;
L08c2:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  ax = rd16(es, (uint16_t)(bx + di));
  dx = rd16(es, (uint16_t)(bx + di + 2));
  si = ax;
  L16(-0xe) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L090a;
  es = L16(-0xe);
  if (RW(es, si, 6) != L16(-2)) goto L08fc;
  if (RW(es, si, 2) != drd16(0x014c)) goto L08fc;
  if (fleet_meets_ship_threshold_9_12(HIGHLOW(es, si), 2) != 0) goto L0b0c;
L08fc:
  di = (uint16_t)(di + 4);
  ax = drd16(0x553a);
  L16(-0x20) = (uint16_t)(L16(-0x20) + 1);
  if ((int16_t)L16(-0x20) < (int16_t)ax) goto L08c2;
L090a:
  if (L16(-0x14) == 0xffff) goto L09d2;
  ax = (uint16_t)((int16_t)drd16(0x007a) / 12);
  ax = (uint16_t)(ax + 8);
  bx = (uint16_t)((uint16_t)L16(-0x14) * 0x8d);
  if (drd16((uint16_t)(bx + 0x41ff)) > 0) goto L09d2;
  if (drd16((uint16_t)(bx + 0x41fd)) >= ax) goto L09d2;
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, L16(-0x1e), L16(-0x1c), (uint16_t)(rt_bp - 0x5a));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, L16(-0x1e), L16(-0x1c), (uint16_t)(rt_bp - 0x6a));
  for (si = 0; (int16_t)si < 0x10; si = (uint16_t)(si + 4)) {
    L32(-0x5a + (int16_t)si) = L32(-0x5a + (int16_t)si) - L32(-0x6a + (int16_t)si);
    if ((int16_t)L16(-0x58 + (int16_t)si) < 0) goto L0a96;
  }
  L16(-0xc) = 0;
  do {
    (void)CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x014c), (uint16_t)((uint16_t)((uint16_t)L16(-0x14) * 0x8d) + 0x417a),
                    MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x3a));
    di = (uint16_t)(rt_bp - 0x3a);
    for (si = (uint16_t)(rt_bp - 0x5a); si < (uint16_t)(rt_bp - 0x4a); si = (uint16_t)(si + 4)) {
      uint16_t w = rd16(rt_ss, di);
      uint32_t v = rd32(rt_ss, si) - (uint32_t)w;
      wr32(rt_ss, si, v);
      if ((int16_t)(v >> 16) < 0) goto L0a96;
      di = (uint16_t)(di + 2);
    }
    L16(-0xa) = 1;
    (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x14), 1, 2, 1);
    L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
  } while ((int16_t)L16(-0xc) < 5);
L09d2:
  if (L16(-0x1a) == 0xffff) goto L0a96;
  ax = (uint16_t)((int16_t)drd16(0x007a) / 0x18);
  ax = (uint16_t)(ax + 4);
  bx = (uint16_t)((uint16_t)L16(-0x1a) * 0x8d);
  if (drd16((uint16_t)(bx + 0x41ff)) > 0) goto L0a96;
  if (drd16((uint16_t)(bx + 0x41fd)) >= ax) goto L0a96;
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, L16(-0x1e), L16(-0x1c), (uint16_t)(rt_bp - 0x5a));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, L16(-0x1e), L16(-0x1c), (uint16_t)(rt_bp - 0x6a));
  for (si = 0; (int16_t)si < 0x10; si = (uint16_t)(si + 4)) {
    L32(-0x5a + (int16_t)si) = L32(-0x5a + (int16_t)si) - L32(-0x6a + (int16_t)si);
    if ((int16_t)L16(-0x58 + (int16_t)si) < 0) goto L0a96;
  }
  L16(-0xc) = 0;
  do {
    (void)CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x014c), (uint16_t)((uint16_t)((uint16_t)L16(-0x1a) * 0x8d) + 0x417a),
                    MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x3a));
    di = (uint16_t)(rt_bp - 0x3a);
    for (si = (uint16_t)(rt_bp - 0x5a); si < (uint16_t)(rt_bp - 0x4a); si = (uint16_t)(si + 4)) {
      uint16_t w = rd16(rt_ss, di);
      uint32_t v = rd32(rt_ss, si) - (uint32_t)w;
      wr32(rt_ss, si, v);
      if ((int16_t)(v >> 16) < 0) goto L0a96;
      di = (uint16_t)(di + 2);
    }
    L16(-0xa) = 1;
    (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x1a), 1, 2, 1);
    L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
  } while ((int16_t)L16(-0xc) < 5);
  goto L0a96;
L0b0c:
  if (L16(-0x18) == 0xffff) goto L090a;
  es = L16(-0xe);
  ax = (uint16_t)(RW(es, si, 0x10) + RW(es, si, 0x12));
  if ((int16_t)ax < (int16_t)drd8(0x5342)) goto L090a;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x18), 4, 2, 1);
  L16(-0xa) = 1;
L0a96:
  (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, L16(-0xa));
L0aa1:
  ax = L16(-0x24);
  L16(-0x1e) = (uint16_t)(L16(-0x1e) + 0x38);
  if (L16(-0x1e) < ax) {
    di = L16(-0x1e);
    goto L05f6;
  }
L0aaf:
  L32(-0x32) = 0;
  L32(-0x2e) = 0;
  L16(-0x20) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L0d9f;
  L16(-4) = 0;
L0ac8:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  di = L16(-4);
  ax = rd16(es, (uint16_t)(bx + di));
  dx = rd16(es, (uint16_t)(bx + di + 2));
  si = ax;
  L16(-0xe) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L0d9f;
  ax = drd16(0x014c);
  es = L16(-0xe);
  if (RW(es, si, 2) == ax) goto L0b4c;
  WD(es, si, 0x68, L32(-0x2e));
  L16(-0x2e) = si;
  L16(-0x2c) = es;
  goto L0d8e;
L0b4c:
  if ((uint16_t)CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, si, es) == 0) goto L0b6c;
  es = L16(-0xe);
  WD(es, si, 0x68, L32(-0x32));
  L16(-0x32) = si;
  L16(-0x30) = es;
L0b6c:
  es = L16(-0xe);
  WB(es, si, 5, RB(es, si, 5) & 0x7f);
  if ((uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, si, es) == 0) goto L0bb8;
  L16(-2) = 0xffff;
  es = L16(-0xe);
  if ((int16_t)RW(es, si, 0x62) <= 1) goto L0bb2;
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  if ((RB(es, bx, 0x1d) & 0xf) != 1) goto L0c10;
  es = L16(-0xe);
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  L16(-2) = RW(es, bx, 0x1a);
  goto L0c10;
L0bb2:
  L16(-2) = RW(es, si, 6);
  goto L0c10;
L0bb8:
  es = L16(-0xe);
  if (RW(es, si, 0xe) == 0) goto L0d8e;
  L16(-2) = 0xffff;
  if ((int16_t)RW(es, si, 0x62) <= 1) goto L0bee;
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  if ((RB(es, bx, 0x1d) & 0xf) != 1) goto L0bf5;
  es = L16(-0xe);
  bx = RW(es, si, 0x64);
  es = RW(es, si, 0x66);
  ax = RW(es, bx, 0x1a);
  goto L0bf2;
L0bee:
  ax = RW(es, si, 6);
L0bf2:
  L16(-2) = ax;
L0bf5:
  if (L16(-2) == 0xffff) goto L0d8e;
  di = (uint16_t)(L16(-2) << 4);
  if (rd8(INFO_SEG, INFO(di, 3)) == 0) goto L0d28;
L0c10:
  if (L16(-2) == 0xffff) goto L0d8e;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-2));
  L16(-0x1e) = (uint16_t)r;
  L16(-0x1c) = (uint16_t)(r >> 16);
  if (r == 0) goto L0c44;
  bx = L16(-0x1e);
  es = L16(-0x1c);
  if (RW(es, bx, 2) == 0xffff) goto L0d8e;
  ax = drd16(0x014c);
  if (RW(es, bx, 2) == ax) goto L0d8e;
L0c44:
  L16(-0x10) = si;
  di = (uint16_t)(L16(-2) << 4);
  if (rd8(INFO_SEG, INFO(di, 3)) == 0) goto L0cf4;
  es = L16(-0xe);
  if ((int32_t)RD(es, si, 0x58) <= 0) goto L0cf4;
  bx = L16(-0x1e);
  es = L16(-0x1c);
  ax = (uint16_t)((uint16_t)(RW(es, bx, 2) * 0xc0) + 0x5a64);
  ax = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, ax, 0xe);
  if (ax == 8) goto L0cf4;
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x7c))), 0, 18);
  bx = (uint16_t)(L16(-2) << 2);
  L32(-0x7c) = drd32((uint16_t)(bx + 0x31ba));
  L16(-0x76) = (uint16_t)((L16(-0x76) & 0xf1ff) | 0x0100);
  L16(-0x78) = L16(-2);
  L16(-0x76) = (uint16_t)((L16(-0x76) & 0xfff1) | 1);
  L8(-0x75) = (uint8_t)(L8(-0x75) | 0x10);
  L16(-0x6e) = (uint16_t)((L16(-0x6e) & 0x0fff) | 0x2000);
  es = L16(-0xe);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, si, 0));
  ax = L16(-2);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  di = (RW(es, bx, 8) == ax) ? (uint16_t)(bx + 4) : (uint16_t)(bx + 0x16);
  for (uint16_t k = 0; k < 18; k++) wr8(es, (uint16_t)(di + k), L8(-0x7c + (int16_t)k));
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  si = (uint16_t)(L16(-2) << 4);
  wr8(INFO_SEG, INFO(si, 3), (uint8_t)(rd8(INFO_SEG, INFO(si, 3)) | 0x80));
  goto L0d8e;
L0cf4:
  di = L16(-0x10);
  goto L0d54;
L0d28:
  L16(-0x10) = si;
  di = si;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-2));
  si = (uint16_t)r;
  L16(-0x1c) = (uint16_t)(r >> 16);
  if (r == 0) goto L0d8e;
  es = L16(-0x1c);
  if (RW(es, si, 2) == 0xffff) goto L0d8e;
  ax = drd16(0x014c);
  if (RW(es, si, 2) == ax) goto L0d8e;
L0d54:
  es = L16(-0xe);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  dwr16(0x4bee, 1);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  WB(es, bx, 3, 1);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, di, L16(-0xe), 0);
L0d8e:
  L16(-4) = (uint16_t)(L16(-4) + 4);
  ax = drd16(0x553a);
  L16(-0x20) = (uint16_t)(L16(-0x20) + 1);
  if ((int16_t)L16(-0x20) < (int16_t)ax) goto L0ac8;
L0d9f:
  dwr16(0x255c, 0);
  L16(-0x20) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L12cc;
  L16(-0x2a) = 0;
L0db2:
  bx = drd16(0x00ba);
  es = drd16(0x00bc);
  si = L16(-0x2a);
  ax = rd16(es, (uint16_t)(bx + si));
  dx = rd16(es, (uint16_t)(bx + si + 2));
  di = ax;
  L16(-0xe) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L12cc;
  ax = drd16(0x014c);
  es = L16(-0xe);
  if (RW(es, di, 2) != ax) goto L12bb;
  if ((int16_t)RW(es, di, 0x62) <= 1) goto L0dfa;
  if (drd8(0x41b6) >= 0x0a) goto L12bb;
  if ((int32_t)RD(es, di, 0x5c) >= 2) goto L12bb;
  L16(-0x10) = di;
  goto L128e;
L0dfa:
  if (RW(es, di, 0x18) == 0) goto L0e4e;
  if (RW(es, di, 0x62) != 1) goto L12bb;
  bx = RW(es, di, 0x64);
  es = RW(es, di, 0x66);
  if ((RB(es, bx, 0xa) & 0xf) != 0) goto L12bb;
  es = L16(-0xe);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  WW(es, bx, 0xa, (RW(es, bx, 0xa) & 0xfff6) | 6);
  WW(drd16(0x4bf2), drd16(0x4bf0), 0xc, 5);
  WW(drd16(0x4bf2), drd16(0x4bf0), 0xe, 5);
  goto L12ae;
L0e4e:
  if (RW(es, di, 0xe) != 0) goto L1190;
  ax = (uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, di, es);
  if (ax != 0) goto L1064;
  es = L16(-0xe);
  if (RW(es, di, 0x10) != ax) goto L0ec8;
  if (RW(es, di, 0x12) != ax) goto L0ec8;
  if (RW(es, di, 0xc) == ax) goto L12bb;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  if (drd8(0x41b6) >= 0x0a) goto L0ea2;
  L16(-0x10) = di;
  es = L16(-0xe);
  if ((int32_t)RD(es, di, 0x5c) < 2) goto L128e;
L0ea2:
  (void)CALL_ORIG(18, 0x3c64, FAR_CDECL, 8, di, L16(-0xe), L16(-0x32), L16(-0x30), L16(-0x2e), L16(-0x2c),
                  (uint16_t)((drd8(0x80) & 0x10) >> 4), (uint16_t)(rt_bp - 0x28));
  goto L12bb;
L0ec8:
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, di, 0));
  es = L16(-0xe);
  if (RW(es, di, 6) == 0xffff) goto L0fa6;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, RW(es, di, 6));
  si = (uint16_t)r;
  L16(-0x1c) = (uint16_t)(r >> 16);
  ax = drd16(0x014c);
  es = (uint16_t)(r >> 16);
  if (RW(es, si, 2) != ax) goto L0f40;
  if ((RB(es, si, 5) & 2) == 0) goto L0f27;
  es = L16(-0xe);
  if ((int16_t)RW(es, di, 0x10) >= 2) goto L0f17;
  if ((int16_t)RW(es, di, 0x12) < 2) goto L12bb;
L0f17:
  if ((int16_t)RW(es, di, 0x1e) >= 3) goto L0f27;
  if ((int16_t)RW(es, di, 0x20) < 3) goto L12bb;
L0f27:
  es = L16(-0xe);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, RW(es, di, 0), 0x4b8c);
  L16(-0x10) = di;
  L16(-0x1e) = si;
  goto L0fb1;
L0f40:
  L16(-0x10) = di;
  L16(-0x1e) = si;
  if (RW(es, si, 2) == 0xffff) goto L0fb1;
  ax = L16(-0x2e);
  dx = L16(-0x2c);
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) == 0) goto L12bb;
  L16(-4) = ax;
  si = ax;
L0f61:
  es = L16(-0xe);
  ax = RW(es, di, 8);
  es = L16(-2);
  if (RW(es, si, 8) != ax) goto L0f8f;
  es = L16(-0xe);
  ax = RW(es, di, 0xa);
  es = L16(-2);
  if (RW(es, si, 0xa) != ax) goto L0f8f;
  if ((uint16_t)CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, si, es) != 0) goto L0fb1;
L0f8f:
  es = L16(-2);
  ax = RW(es, si, 0x68);
  dx = RW(es, si, 0x6a);
  si = ax;
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) != 0) goto L0f61;
  goto L12bb;
L0fa6:
  L16(-0x10) = di;
  L32(-0x1e) = L32(-0x24);
L0fb1:
  if ((drd8(0x80) & 0x10) == 0) {
    ax = 0;
    dx = 0;
  } else {
    r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-0x1e), L16(-0x1c), 0x2252, SEG_SEL(17));
    ax = (uint16_t)r;
    dx = (uint16_t)(r >> 16);
  }
  si = ax;
  L16(-2) = dx;
  if ((uint16_t)(dx | ax) == 0) {
    r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-0x1e), L16(-0x1c), 0x216c, SEG_SEL(17));
    si = (uint16_t)r;
    L16(-2) = (uint16_t)(r >> 16);
  }
  ax = L16(-2);
  L16(-0x1e) = si;
  L16(-0x1c) = ax;
  if ((uint16_t)(ax | si) == 0) goto L12bb;
  bx = L16(-0x1e);
  es = L16(-0x1c);
  si = (uint16_t)(RW(es, bx, 0) << 4);
  wr8(INFO_SEG, INFO(si, 0xa), (uint8_t)(rd8(INFO_SEG, INFO(si, 0xa)) | 0x80));
  bx = L16(-0x1e);
  es = L16(-0x1c);
  L16(-0x8a) = RW(es, bx, 0);
  L16(-0x88) = (uint16_t)((L16(-0x88) & 0xf1ff) | 0x0100);
  bx = (uint16_t)(RW(es, bx, 0) << 2);
  L32(-0x8e) = drd32((uint16_t)(bx + 0x31ba));
  L8(-0x88) = (uint8_t)(L8(-0x88) & 0xf0);
  L8(-0x87) = (uint8_t)(L8(-0x87) | 0x10);
  L16(-0x88) = (uint16_t)((L16(-0x88) & 0xff4f) | 0x0040);
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, L16(-0x10), L16(-0xe), (uint16_t)(rt_bp - 0x8e), 0);
  goto L12bb;
L1064:
  L16(-0x10) = di;
  ax = drd16(0x00b6);
  dx = drd16(0x00b8);
  bx = ax;
  L16(-0x1c) = dx;
  cx = (uint16_t)((uint16_t)drd16(0x4d96) * 0x38);
  ax = (uint16_t)(ax + cx);
  di = ax;
  L16(-0x22) = dx;
  if (ax <= bx) goto L1099;
  L16(-0x24) = ax;
  for (;;) {
    ds = dx;
    ax = drd16(0x014c);
    if (rd16(ds, (uint16_t)(bx + 2)) == ax && (rd8(ds, (uint16_t)(bx + 5)) & 2) != 0) break;
    bx = (uint16_t)(bx + 0x38);
    if (!(bx < di)) break;
  }
L1099:
  ax = L16(-0x22);
  if (di != bx) goto L10bc;
  if (ax != L16(-0x1c)) goto L10bc;
  L32(-0x24) = 0;
  goto L10c5;
L10bc:
  ax = L16(-0x1c);
  L16(-0x24) = bx;
  L16(-0x22) = ax;
L10c5:
  if (L32(-0x24) == 0) goto L12cc;
  L32(-0x1e) = 0;
  L16(-0xc) = 0;
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) <= 0) goto L1135;
  si = (uint16_t)(bx + 6);
  L16(-8) = es;
L10ec:
  cx = 0;
  es = L16(-8);
  ax = rd16(es, si);
  L16(-0x94) = ax;
  if ((int16_t)ax <= 0) goto L111c;
  ax = (uint16_t)(si + 2);
  L16(-4) = ax;
  L16(-2) = es;
  L16(-0xa) = si;
  bx = ax;
  ds = L16(-0xe);
L110d:
  es = L16(-2);
  ax = rd16(es, bx);
  di = L16(-0x10);
  if (rd16(ds, di) != ax) {
    bx = (uint16_t)(bx + 2);
    cx++;
    if ((int16_t)L16(-0x94) > (int16_t)cx) goto L110d;
  }
L111c:
  if ((int16_t)L16(-0x94) > (int16_t)cx) goto L1135;
  si = (uint16_t)(si + 0x14);
  L16(-0xc) = (uint16_t)(L16(-0xc) + 1);
  ax = L16(-0xc);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) > (int16_t)ax) goto L10ec;
L1135:
  si = L16(-0x1e);
  ax = L16(-0xc);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)RW(es, bx, 2) <= (int16_t)ax) goto L1159;
  di = (uint16_t)((int16_t)ax * 0x14);
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, RW(es, (uint16_t)(bx + di), 4));
  si = (uint16_t)r;
  L16(-0x1c) = (uint16_t)(r >> 16);
L1159:
  ax = L16(-0x1c);
  if ((uint16_t)(ax | si) == 0) {
    cx = L16(-0x24);
    dx = L16(-0x22);
    L16(-2) = dx;
  } else {
    ax = L16(-0x1c);
    cx = si;
    L16(-2) = ax;
  }
  (void)CALL_ORIG(18, 0x19c4, FAR_CDECL, 4, L16(-0x10), L16(-0xe), cx, L16(-2));
  goto L12bb;
L1190:
  L16(-0x10) = di;
  if (drd16(0x4207) != 1) goto L128e;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, di));
  bx = L16(-0x10);
  es = L16(-0xe);
  if (RW(es, bx, 6) == 0xffff) goto L11c9;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L11c9;
  if ((int32_t)drd32(0x4c30) >= 0xc8) goto L120a;
L11c9:
  if (RD(es, bx, 0x58) != 0) goto L120a;
  if (drd16(0x4b92) == 0xffff) goto L11ea;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L11ea;
  if ((drd8(0x4c0d) & 2) != 0) goto L12bb;
L11ea:
  if (drd8(0x4243) < 2) goto L128e;
  ax = (uint16_t)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, bx, es, 0);
  if (ax != 0) goto L12bb;
  goto L128e;
L120a:
  L32(-0x28) = 0;
  si = (uint16_t)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, bx, es, 0);
  bx = L16(-0x10);
  es = L16(-0xe);
  if (RW(es, bx, 6) == 0xffff) goto L126b;
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L126b;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, bx, 0));
  bx = L16(-0x10);
  es = L16(-0xe);
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, RW(es, bx, 6), 2, RW(es, bx, 0), 3, 0x96);
  bx = L16(-0x10);
  es = L16(-0xe);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, RW(es, bx, 0), 0x4b8c);
L126b:
  if (si == 0xffff) goto L12bb;
  (void)CALL_ORIG(18, 0x0864, FAR_CDECL, 3, L16(-0x10), L16(-0xe), si);
  di = (uint16_t)(si << 4);
  WB(drd16(0x1602), (uint16_t)(drd16(0x1600) + di), 0xf, 4);
  goto L12bb;
L128e:
  bx = L16(-0x10);
  es = L16(-0xe);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, RW(es, bx, 0));
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  WW(es, bx, 0xa, (RW(es, bx, 0xa) & 0xfff5) | 5);
L12ae:
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
L12bb:
  L16(-0x2a) = (uint16_t)(L16(-0x2a) + 4);
  ax = drd16(0x553a);
  L16(-0x20) = (uint16_t)(L16(-0x20) + 1);
  if ((int16_t)L16(-0x20) < (int16_t)ax) goto L0db2;
L12cc:
  (void)CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-0x90), param_1, L16(-0x12), (uint16_t)(rt_bp - 0x5a),
                  (uint16_t)(rt_bp - 0x6a));
  (void)rt_call_far(MK_FP(SEG_SEL(21), 0x1e2a), FAR_CDECL, NULL, 0);
  FRAME_LEAVE();
}
