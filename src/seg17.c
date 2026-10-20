/* Decompiled code segment 17 (NE segment 17, selector 0x0088).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg17.c; Ghidra's pseudo-C in re/decomp/seg17 is only a reading aid.
 *
 * Segment 17 is the older half of the computer players' code (the other half is segment 19):
 *   17:0x0000  ai_player_turn: one computer player's turn: allocate the work tables, run the turn, free them
 *   17:0x0462  ai0_turn: the turn of the type-0 player
 *   17:0x1730  ai0_create_designs: the type-0 player's new ship designs
 *   17:0x1b70  ai0_fleet_orders: the order of one fleet of the type-0 player
 *   17:0x20f4  fleet_meets_ship_threshold: does a fleet hold enough ships (the twin of 19:0x03a0)
 *   17:0x2154  cb_owner_not_current_player: callback: is the record not the current player's
 *   17:0x216c, 0x2252, 0x3c30, 0x3c82  cb_score_*: scoring callbacks over pairs of planets / fleets
 *   17:0x234e  ai1_turn: the turn of the type-1 player
 *   17:0x38b0  ai1_create_designs: the type-1 player's new ship designs
 * The segment also holds byte tables (below, cs_tab) read through CS.
 *
 * Names: Ghidra's are all generic (FUN_1080_<off>), no function here has an export name and none
 * wraps a single Win16 API, so the C names describe what each function does (CONVENTIONS rule f);
 * they are descriptive, not the game's. "type-0" / "type-1" (ai0_, ai1_) are placeholders: they are
 * the values 0 and 1 of the player word DS:0x5a6a + 0xc0 * player shifted right by 13, by which
 * 17:0x0000 picks the turn function; which computer player kind each value is has not been
 * established. "Design" follows 18:0x0000, which (per its Ghidra pseudo-C) checks the hull through
 * 2:0x50d8, fills a 0x8d-byte design record from it and a list of part bytes, and stores it with
 * 18:0x0630 in the design slot its first argument names. The old fn_17_<off> names are kept as
 * aliases in the comment above each function.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with
 * n the ENTER size and one push for each PUSH SI / PUSH DI. Leaves that never expose a stack address
 * use ordinary C locals. The functions are written as the original's basic blocks where the control
 * flow is not a plain loop: the registers are C locals, the stack locals whose address a callee
 * sees are BP_LOCAL slots at the original offsets, and the labels carry the original offsets.
 * Memory is read and written in 16-bit pieces: an offset is a uint16_t and wraps like the
 * original's. DS = SS = DGROUP at every call; the few instructions that load another DS restore it
 * before the next call. Near calls inside the segment are PUSH CS; CALL near, i.e. far calls with a
 * 4-byte return address; a callee that mirrors a frame (rule d) is reached through rt_call_far, a
 * leaf by its C name. The callees in other segments are not decompiled yet: CALL_ORIG. */
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

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))


/* The 32-bit register pair of the original: lo is the 16-bit register, hi the upper half of the 32-bit
 * register (EAX = hi:lo). */
#define E32(lo, hi) ((((uint32_t)(hi)) << 16) | (lo))
#define SET32(lo, hi, v) \
  do { uint32_t v_ = (v); (lo) = (uint16_t)v_; (hi) = (uint16_t)(v_ >> 16); } while (0)

/* DIV / IDIV: the original traps (#DE) on a zero divisor and on a quotient that does not fit; the C
 * version stops with a message (CONVENTIONS rule b). *a is AX / EAX (quotient), *d is DX / EDX (remainder);
 * `where` is the offset in segment 17 of the instruction. */
static void div_trap(bool sgn, uint16_t bits, uint16_t where)
{
  fprintf(stderr, "17:0x%04x: divide error (%s%u)\n", where, sgn ? "IDIV" : "DIV", bits);
  abort();
}
static void idiv16_(uint16_t *a, uint16_t *d, uint16_t den, uint16_t where)
{
  int32_t num = (int32_t)(((uint32_t)*d << 16) | *a);
  int16_t dv = (int16_t)den;
  if (dv == 0 || (num == INT32_MIN && dv == -1)) div_trap(true, 16, where);
  int32_t q = num / dv;
  if (q > 32767 || q < -32768) div_trap(true, 16, where);
  *a = (uint16_t)q;
  *d = (uint16_t)(num % dv);
}
static void div16_(uint16_t *a, uint16_t *d, uint16_t den, uint16_t where)
{
  uint32_t num = ((uint32_t)*d << 16) | *a;
  if (den == 0 || num / den > 0xffff) div_trap(false, 16, where);
  *a = (uint16_t)(num / den);
  *d = (uint16_t)(num % den);
}
/* 64-bit pieces built from 32-bit operations (rule (a) has no 64-bit type). A 64-bit value is the pair
 * (hi, lo) of uint32_t, as in EDX:EAX. */

/* The high 32 bits of the unsigned product a * b: a = a1:a0, b = b1:b0 in 16-bit limbs; mid cannot
 * overflow (at most 0xffff + 0xffff + 0xffff). */
static uint32_t umulh32_(uint32_t a, uint32_t b)
{
  uint32_t a0 = a & 0xffffu, a1 = a >> 16, b0 = b & 0xffffu, b1 = b >> 16;
  uint32_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint32_t mid = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
  return p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);
}
/* IMUL r/m32 (one operand): EDX:EAX = EAX * b, signed. The low half is the wrapped product; the signed high
 * half is the unsigned one minus b when a is negative and minus a when b is negative (mod 2^32). */
static void imul32_(uint32_t *a, uint32_t *d, uint32_t b)
{
  uint32_t x = *a;
  uint32_t hi = umulh32_(x, b);
  if (x >> 31) hi -= b;
  if (b >> 31) hi -= x;
  *a = x * b;
  *d = hi;
}
/* The unsigned 64 by 32 division of (hi, lo) by den: the quotient has to fit 32 bits (hi < den, den != 0),
 * the shift-and-subtract division then runs 32 steps on the remainder in hi (a carry out of the shifted
 * remainder always means it is at least den). */
static void udivmod64_(uint32_t hi, uint32_t lo, uint32_t den, uint32_t *q, uint32_t *r)
{
  uint32_t quo = 0;
  for (uint16_t i = 0; i < 32; i++) {
    uint32_t carry = hi >> 31;
    hi = (hi << 1) | (lo >> 31);
    lo <<= 1;
    quo <<= 1;
    if (carry || hi >= den) { hi -= den; quo |= 1; }
  }
  *q = quo;
  *r = hi;
}
static void idiv32_(uint32_t *a, uint32_t *d, uint32_t den, uint16_t where)
{
  uint32_t lo = *a, hi = *d;
  bool nneg = (hi >> 31) != 0, dneg = (den >> 31) != 0;
  if (den == 0) div_trap(true, 32, where);
  if (nneg) { lo = ~lo + 1u; hi = ~hi + (lo == 0 ? 1u : 0u); } /* |num|; INT64_MIN stays 2^63 */
  uint32_t dv = dneg ? ~den + 1u : den; /* |den|, up to 2^31 */
  if (hi >= dv) div_trap(true, 32, where); /* |quotient| >= 2^32 (also INT64_MIN / -1) */
  uint32_t q, r;
  udivmod64_(hi, lo, dv, &q, &r);
  bool qneg = nneg != dneg;
  /* the quotient has to be in -2^31 .. 2^31 - 1 */
  if (qneg ? q > 0x80000000u : q > 0x7fffffffu) div_trap(true, 32, where);
  *a = qneg ? ~q + 1u : q;
  *d = nneg ? ~r + 1u : r; /* the remainder takes the sign of the dividend */
}
static void div32_(uint32_t *a, uint32_t *d, uint32_t den, uint16_t where)
{
  if (den == 0 || *d >= den) div_trap(false, 32, where);
  uint32_t q, r;
  udivmod64_(*d, *a, den, &q, &r);
  *a = q;
  *d = r;
}

/* The code segment's data at 17:0x0205 to 17:0x0461 (605 bytes): the part lists of the designs, their
 * index tables and the two byte tables handed to 18:0x28fc. 17:0x1730 passes 17:(table word + 0x224)
 * and 17:0x38b0 17:(table byte + 0x39d) to 18:0x0000 as a far pointer; 17:0x0462 passes 17:0x043d
 * (count 0x24) and 17:0x234e passes 17:0x0205 (count 0x1f) to 18:0x28fc. */
#define CS_TAB_BASE 0x0205
static const uint8_t cs_tab[605] = {
  0x42, 0x64, 0xa4, 0x04, 0x25, 0x46, 0x66, 0x28, 0x06, 0x86, 0x49, 0xa7, 0x68, 0x88, 0xa5, 0x69,
  0x07, 0x8a, 0x2a, 0x4c, 0x6b, 0x0a, 0x2c, 0x8d, 0x50, 0x2e, 0x6f, 0x8e, 0xaa, 0x30, 0x0e, 0x08,
  0x04, 0x0a, 0x0a, 0x0d, 0x09, 0x09, 0x08, 0x0a, 0x05, 0x04, 0x0d, 0x0c, 0x0f, 0x08, 0x0a, 0x04,
  0x07, 0x0d, 0x0c, 0x0e, 0x08, 0x0a, 0x03, 0x03, 0x0d, 0x0c, 0x0e, 0x08, 0x09, 0x01, 0x01, 0x0b,
  0x0b, 0x0c, 0x08, 0x00, 0x09, 0x0a, 0x0d, 0x0b, 0x0c, 0x08, 0x09, 0x00, 0x00, 0x0a, 0x0b, 0x0c,
  0x08, 0x01, 0x09, 0x0c, 0x0c, 0x0b, 0x0b, 0x08, 0x0a, 0x10, 0x10, 0x03, 0x0c, 0x02, 0x08, 0x10,
  0x04, 0x03, 0x0e, 0x0c, 0x0d, 0x08, 0x03, 0x10, 0x0a, 0x10, 0x0c, 0x0e, 0x08, 0x10, 0x01, 0x0b,
  0x0c, 0x0a, 0x0a, 0x08, 0x10, 0x0b, 0x0c, 0x10, 0x01, 0x00, 0x08, 0x0a, 0x10, 0x10, 0x0b, 0x00,
  0x00, 0x08, 0x0a, 0x0f, 0x04, 0x04, 0x08, 0x09, 0x0b, 0x00, 0x00, 0x08, 0x04, 0x04, 0x04, 0x11,
  0x12, 0x13, 0x08, 0x03, 0x03, 0x0e, 0x11, 0x12, 0x13, 0x08, 0x04, 0x03, 0x02, 0x11, 0x12, 0x14,
  0x08, 0x04, 0x04, 0x05, 0x11, 0x12, 0x14, 0x08, 0x00, 0x00, 0x0a, 0x11, 0x12, 0x13, 0x08, 0x00,
  0x00, 0x0b, 0x11, 0x12, 0x13, 0x08, 0x01, 0x01, 0x0b, 0x11, 0x12, 0x13, 0x08, 0x01, 0x01, 0x0b,
  0x11, 0x12, 0x0b, 0x18, 0x15, 0x17, 0x17, 0x17, 0x0c, 0x0a, 0x18, 0x15, 0x16, 0x16, 0x16, 0x0c,
  0x0a, 0x18, 0x1a, 0x19, 0x0a, 0x08, 0x0b, 0x0a, 0x01, 0x01, 0x01, 0x01, 0x02, 0x09, 0x0b, 0x13,
  0x08, 0x0d, 0x0a, 0x01, 0x01, 0x00, 0x00, 0x00, 0x09, 0x0b, 0x13, 0x08, 0x0d, 0x0a, 0x00, 0x00,
  0x01, 0x01, 0x00, 0x09, 0x0b, 0x13, 0x08, 0x0d, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x03, 0x09, 0x0b,
  0x13, 0x08, 0x0d, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x04, 0x09, 0x14, 0x13, 0x08, 0x0d, 0x0a, 0x04,
  0x03, 0x03, 0x07, 0x02, 0x09, 0x14, 0x13, 0x08, 0x0d, 0x0a, 0x02, 0x03, 0x07, 0x07, 0x03, 0x09,
  0x14, 0x13, 0x08, 0x0d, 0x0a, 0x04, 0x04, 0x03, 0x03, 0x05, 0x09, 0x14, 0x13, 0x08, 0x21, 0x0a,
  0x11, 0x0c, 0x21, 0x21, 0x08, 0x0d, 0x0a, 0x21, 0x21, 0x21, 0x21, 0x21, 0x11, 0x14, 0x13, 0x08,
  0x0a, 0x0a, 0x07, 0x05, 0x14, 0x14, 0x04, 0x04, 0x13, 0x04, 0x02, 0x03, 0x00, 0x00, 0x07, 0x00,
  0x0e, 0x00, 0x15, 0x00, 0x1c, 0x00, 0x23, 0x00, 0x2a, 0x00, 0x31, 0x00, 0x38, 0x00, 0x3f, 0x00,
  0x46, 0x00, 0x4d, 0x00, 0x54, 0x00, 0x5b, 0x00, 0x62, 0x00, 0x67, 0x00, 0x6c, 0x00, 0x73, 0x00,
  0x7a, 0x00, 0x81, 0x00, 0x88, 0x00, 0x8f, 0x00, 0x96, 0x00, 0x9d, 0x00, 0xa4, 0x00, 0xab, 0x00,
  0xb2, 0x00, 0xb6, 0x00, 0xc1, 0x00, 0xcc, 0x00, 0xd7, 0x00, 0xe2, 0x00, 0xed, 0x00, 0xf8, 0x00,
  0x03, 0x01, 0x0e, 0x01, 0x15, 0x01, 0x20, 0x01, 0x08, 0x1f, 0x08, 0x1a, 0x00, 0x25, 0x08, 0x00,
  0x00, 0x00, 0x09, 0x12, 0x0b, 0x08, 0x01, 0x01, 0x0b, 0x09, 0x12, 0x0b, 0x08, 0x25, 0x0f, 0x07,
  0x04, 0x06, 0x09, 0x08, 0x0b, 0x0d, 0x00, 0x00, 0x00, 0x09, 0x08, 0x25, 0x0f, 0x07, 0x03, 0x06,
  0x11, 0x08, 0x0b, 0x0d, 0x01, 0x01, 0x01, 0x11, 0x08, 0x0c, 0x25, 0x06, 0x03, 0x05, 0x03, 0x07,
  0x09, 0x14, 0x14, 0x08, 0x0c, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x13, 0x0b, 0x08, 0x0c,
  0x25, 0x06, 0x03, 0x04, 0x02, 0x07, 0x11, 0x14, 0x14, 0x08, 0x0c, 0x25, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x11, 0x13, 0x0b, 0x08, 0x0a, 0x10, 0x1b, 0x11, 0x00, 0x0d, 0x27, 0x0b, 0x08, 0x15, 0x16,
  0x0c, 0x27, 0x08, 0x0a, 0x0c, 0x19, 0x19, 0x08, 0x25, 0x11, 0x00, 0x0d, 0x0b, 0x10, 0x1b, 0x08,
  0x0d, 0x1c, 0x1c, 0x08, 0x0d, 0x1c, 0x1c, 0x1c, 0x1c, 0x08, 0x25, 0x10, 0x1a, 0x11, 0x0a, 0x0d,
  0x13, 0x27, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x0d, 0x14, 0x1b, 0x22, 0x29, 0x30, 0x3b, 0x46,
  0x51, 0x5c, 0x65, 0x6a, 0x6f, 0x77, 0x7b, 0x81, 0x42, 0x63, 0x23, 0x64, 0x02, 0x83, 0x46, 0x25,
  0x66, 0xa4, 0x85, 0x06, 0x27, 0x6a, 0x06, 0x87, 0x2a, 0x49, 0x4c, 0x6d, 0x2e, 0x70, 0x09, 0x8a,
  0x50, 0xaa, 0x0f, 0x34, 0x54, 0x90, 0xac, 0x38, 0x93, 0x78, 0x16, 0x7a, 0x90,
};
/* The byte / word at 17:`off` (MOV AL, CS:[off]); the callers' indexes stay inside the table. */
static inline uint8_t cs_b(uint16_t off) { return cs_tab[(uint16_t)(off - CS_TAB_BASE)]; }
static inline uint16_t cs_w(uint16_t off)
{
  return (uint16_t)(cs_tab[(uint16_t)(off - CS_TAB_BASE)] | (cs_tab[(uint16_t)(off + 1 - CS_TAB_BASE)] << 8));
}


/* fleet_meets_ship_threshold: 17:0x20f4 (Ghidra FUN_1080_20f4, alias fn_17_20f4), 95 bytes. ENTER 2,0; PUSH DI; PUSH SI (a leaf: no callee).
 * The twin of 19:0x03a0 over other ship-count words: does the fleet `fleet` (FAR CDECL, far pointer
 * at [BP+6]) hold enough ships. di = the 16-bit sum of the four words at +0x10 to +0x16 plus twice
 * the words at +0x18 and +0x1a (which design slots these words count is not established here). With `mode` ([BP+0xA], a signed compare) below 2 the
 * answer is 1; otherwise it is 1 when the byte DS:0x5340 is not above the sum (a signed 16-bit
 * compare), else 0. AX only (DX is the fleet's segment). */
DECOMP(17, 0x20f4, fleet_meets_ship_threshold, FAR_CDECL)
uint16_t fleet_meets_ship_threshold(uint32_t fleet, int16_t mode)
{
  uint16_t seg = (uint16_t)(fleet >> 16), off = (uint16_t)fleet;
  uint16_t di = 0;
  for (uint16_t k = 0; k < 4; k++) di = (uint16_t)(di + rd16(seg, (uint16_t)(off + 0x10 + 2 * k)));
  for (uint16_t k = 0; k < 2; k++) {
    uint16_t w = rd16(seg, (uint16_t)(off + 0x18 + 2 * k));
    di = (uint16_t)(di + (uint16_t)(w + w));
  }
  if (mode < 2) return 1;
  return ((int16_t)drd8(0x5340) <= (int16_t)di) ? 1 : 0;
}

/* cb_owner_not_current_player: 17:0x2154 (Ghidra FUN_1080_2154, alias fn_17_2154), 24 bytes. PUSH BP; MOV BP,SP (no ENTER; a leaf).
 * FAR CDECL callback (a, b): two far pointers, [BP+6] (ignored) and [BP+0xA]. Returns 1 when the
 * word at b + 2 (the owner) is not the current player DS:0x14c, else 0. AX only. */
DECOMP(17, 0x2154, cb_owner_not_current_player, FAR_CDECL)
uint16_t cb_owner_not_current_player(uint32_t a, uint32_t b)
{
  (void)a;
  return (rd16((uint16_t)(b >> 16), (uint16_t)((uint16_t)b + 2)) != drd16(0x014c)) ? 1 : 0;
}

/* The bonus 17:0x216c and 17:0x2252 add to the info byte for the squared distance r (8:0xbc50, DX:AX
 * as a signed 32-bit value): below 2500, 10000, 22500, 40000, 90000 and 250000 it is 7, 5, 4, 3, 2
 * and 1, else 0. (The original's pairs "or dx,dx / jg / jl / cmp ax / jae" are signed compares of
 * the 32-bit value.) */
static uint8_t dist_bonus(uint32_t r)
{
  int32_t d = (int32_t)r;
  if (d < 2500) return 7;
  if (d < 10000) return 5;
  if (d < 22500) return 4;
  if (d < 40000) return 3;
  if (d < 90000) return 2;
  if (d < 250000) return 1;
  return 0;
}

/* cb_score_planet_by_distance: 17:0x216c (Ghidra FUN_1080_216c, alias fn_17_216c), 230 bytes. ENTER 6,0; PUSH DI; PUSH SI.
 * FAR CDECL callback (a, b): two far pointers to words (planet ids), a at [BP+6], b at [BP+0xA].
 * Returns 0 when a and b are the same pointer, or when the info byte +0xa of b's planet (array
 * DS:0x1600, 16 bytes each) is 0. Otherwise the byte plus dist_bonus of the squared distance
 * 8:0xbc50(coords of a, coords of b); when the sum has bit 7 set, 8:0x8730(4) must give 0 (else the
 * result is 0). AX only. */
DECOMP(17, 0x216c, cb_score_planet_by_distance, FAR_CDECL)
uint16_t cb_score_planet_by_distance(uint32_t a, uint32_t b)
{
  FRAME_ENTER_REGS(6, 2);
  uint16_t asg = (uint16_t)(a >> 16), aoff = (uint16_t)a, bsg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t bx;
  uint8_t v;
  if (boff == aoff && bsg == asg) goto none;
  bx = (uint16_t)((uint16_t)(rd16(bsg, boff) << 4) + drd16(0x1600));
  v = rd8(drd16(0x1602), (uint16_t)(bx + 0x0a));
  if (v == 0) goto none;
  {
    uint32_t cb = drd32((uint16_t)((uint16_t)(rd16(bsg, boff) << 2) + 0x31ba));
    uint32_t ca = drd32((uint16_t)((uint16_t)(rd16(asg, aoff) << 2) + 0x31ba));
    uint32_t r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
    v = (uint8_t)(v + dist_bonus(r));
  }
  if ((v & 0x80) != 0) {
    if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 4) != 0) goto none;
  }
  FRAME_LEAVE();
  return v;
none:
  FRAME_LEAVE();
  return 0;
}

/* cb_score_planet_rec_by_distance: 17:0x2252 (Ghidra FUN_1080_2252, alias fn_17_2252), 252 bytes. ENTER 6,0; PUSH DI; PUSH SI.
 * FAR CDECL callback (a, b) like 17:0x216c, but b points to a record (id word, owner word): 0 when
 * a and b are the same pointer or the owner's player record (DS:0x5a64 + 0xc0 * owner) has bit 1 of
 * its byte +7 set. DS is b's segment while those two words are read and DGROUP again after. Then as
 * 17:0x216c: the info byte of b's id, + dist_bonus of 8:0xbc50(coords of a, coords of b), and bit 7
 * asks 8:0x8730(4). AX only. */
DECOMP(17, 0x2252, cb_score_planet_rec_by_distance, FAR_CDECL)
uint16_t cb_score_planet_rec_by_distance(uint32_t a, uint32_t b)
{
  FRAME_ENTER_REGS(6, 2);
  uint16_t asg = (uint16_t)(a >> 16), aoff = (uint16_t)a, bsg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t id, bx;
  uint8_t v;
  if (boff == aoff && bsg == asg) goto none;
  id = rd16(bsg, boff);
  bx = (uint16_t)(rd16(bsg, (uint16_t)(boff + 2)) * 0xc0);
  if ((drd8((uint16_t)(bx + 0x5a6b)) & 2) != 0) goto none;
  bx = (uint16_t)(id << 4);
  v = rd8(drd16(0x1602), (uint16_t)(bx + drd16(0x1600) + 0x0a));
  if (v == 0) goto none;
  {
    uint32_t cb = drd32((uint16_t)((uint16_t)(rd16(bsg, boff) << 2) + 0x31ba));
    uint32_t ca = drd32((uint16_t)((uint16_t)(rd16(asg, aoff) << 2) + 0x31ba));
    uint32_t r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
    v = (uint8_t)(v + dist_bonus(r));
  }
  if ((v & 0x80) != 0) {
    if ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 4) != 0) goto none;
  }
  FRAME_LEAVE();
  return v;
none:
  FRAME_LEAVE();
  return 0;
}

/* cb_score_planet_info1: 17:0x3c30 (Ghidra FUN_1080_3c30, alias fn_17_3c30), 82 bytes. ENTER 2,0; PUSH DI; PUSH SI.
 * FAR CDECL callback (a, b): 0 when a and b are the same pointer or the info byte +1 of b's id
 * (word at b, array DS:0x1600, 16 bytes each) is 0; else a random draw 8:0x8730(100): below 25 (a
 * signed compare) the byte is the result, otherwise the result is 0 when the byte has bit 7 set and
 * the byte if not. AX only. */
DECOMP(17, 0x3c30, cb_score_planet_info1, FAR_CDECL)
uint16_t cb_score_planet_info1(uint32_t a, uint32_t b)
{
  FRAME_ENTER_REGS(2, 2);
  uint8_t v;
  if ((uint16_t)b == (uint16_t)a && (uint16_t)(b >> 16) == (uint16_t)(a >> 16)) goto none;
  v = rd8(drd16(0x1602), (uint16_t)((uint16_t)(rd16((uint16_t)(b >> 16), (uint16_t)b) << 4) + drd16(0x1600) + 1));
  if (v == 0) goto none;
  if ((int16_t)(uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 100) >= 0x19 && (v & 0x80) != 0) goto none;
  FRAME_LEAVE();
  return v;
none:
  FRAME_LEAVE();
  return 0;
}

/* cb_score_planet_info3: 17:0x3c82 (Ghidra FUN_1080_3c82, alias fn_17_3c82), 121 bytes. ENTER 2,0; PUSH SI (word locals BP-1 and BP-2 hold
 * the bytes +0xa and +3 of b's info record).
 * FAR CDECL callback (a, b): 0 when a and b are the same pointer, when the info byte +0xa of b's id
 * is not exactly 1, or when the byte +3 is 0. The test of bit 7 of the byte +0xa (with a random
 * draw 8:0x8730(100)) that follows cannot be reached, because the byte is 1 at that point; it is
 * kept as the original has it. Then 28:0x21ee(DS:0x5a64 + 0xc0 * (word at b + 2), 0xe) must not be
 * 8, and the result is the byte +3. AX only. */
DECOMP(17, 0x3c82, cb_score_planet_info3, FAR_CDECL)
uint16_t cb_score_planet_info3(uint32_t a, uint32_t b)
{
  FRAME_ENTER_REGS(2, 1);
  uint16_t bsg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t bx;
  uint8_t b0a, b3;
  if (boff == (uint16_t)a && bsg == (uint16_t)(a >> 16)) goto none;
  bx = (uint16_t)((uint16_t)(rd16(bsg, boff) << 4) + drd16(0x1600));
  b0a = rd8(drd16(0x1602), (uint16_t)(bx + 0x0a));
  b3 = rd8(drd16(0x1602), (uint16_t)(bx + 3));
  if ((uint8_t)(b0a - 1) != 0) goto none;
  if (b3 == 0) goto none;
  if ((b0a & 0x80) != 0) {
    if ((int16_t)(uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 100) >= 0x19) goto none;
  }
  if ((uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(0x5a64 + (uint16_t)(rd16(bsg, (uint16_t)(boff + 2)) * 0xc0)), 0x0e) == 8)
    goto none;
  FRAME_LEAVE();
  return b3;
none:
  FRAME_LEAVE();
  return 0;
}

/* A call through rt_call_far with the word arguments in declaration order (CDECL, far). */
static uint32_t far_call(uint16_t seg, uint16_t off, const uint16_t *args, int16_t n)
{
  return rt_call_far(MK_FP(SEG_SEL(seg), off), FAR_CDECL, args, n);
}

/* ai_player_turn: 17:0x0000 (Ghidra FUN_1080_0000, alias fn_17_0000), 297 bytes. ENTER 0x106,0; PUSH DI; PUSH SI.
 * FAR CDECL (player [BP+6], type [BP+8]), no result. Runs the turn of one computer player: saves the
 * current player word DS:0x14c in BP-2 and restores it at the end (the original never stores `player`
 * in DS:0x14c itself; the real 14:0x0508 writes DS:0x14c from the parsed player string, so after that
 * call it holds the parsed player, or 0xffff when the string starts with "hs"), sets the busy flag DS:0x15fe, formats the
 * player number + 1 with _wsprintf(BP-6, format DS:[0x190], player + 1), calls 14:0x2992 (no arguments) and asks 14:0x0508
 * (load the player's data: DS:0x5764, BP-6). When that succeeds and bit 0 of the current player's
 * byte DS:0x5ab8 + 0xc0 * DS:0x14c is clear it allocates the work tables with 12:0x0232 (heap 6):
 * the info array DS:0x1600 (16 bytes a planet, DS:0x7a planets), the pointer array DS:0x1608 (4 bytes a
 * planet) and, when DS:0x1604 is still empty, the 0x2000-byte table whose first word is set to 2. With
 * all three present it clears the info array (DS:0x7a * 8 words), runs 7:0x09ae, 18:0x40ba, 18:0x5e0e
 * and 18:0x60b2, stores `type` (unless -1) in the word DS:0x5a6a + 0xc0 * player and calls the turn
 * function chosen by that word shifted right by 13: 0 = 17:0x0462, 1 = 17:0x234e, 2 = 19:0x0400,
 * 3 = 19:0x1594, 4 = 21:0x0000, 5 = 20:0x0000, 7 = 19:0x0000 (6 and above 7 do nothing), each with the
 * address of the 0x100-byte scratch buffer BP-0x106. Then 9:0x7d1e(DS:0x5764, player), 9:0x80da(player)
 * and the tables are freed with 12:0x04d0 (heap 6) and cleared. */
DECOMP(17, 0x0000, ai_player_turn, FAR_CDECL)
void ai_player_turn(uint16_t player, uint16_t type)
{
  FRAME_ENTER_REGS(0x106, 2);
  uint16_t si = player;
  L16(-0x2) = drd16(0x014c);
  dwr16(0x15fe, 1);
  {
    /* _wsprintf(BP-6, DS:[0x190], si + 1): the variable word is pushed first, below the two far pointers */
    uint16_t sp0 = rt_sp;
    wr16(rt_ss, (uint16_t)(sp0 - 2), (uint16_t)(si + 1));
    rt_sp = (uint16_t)(sp0 - 2);
    (void)_wsprintf(MK_FP(rt_ss, (uint16_t)(rt_bp - 6)), MK_FP(MEM_SEL_DGROUP, drd16(0x0190)),
                    MK_FP(rt_ss, (uint16_t)(sp0 - 2)));
    rt_sp = sp0;
  }
  (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0);
  if ((uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, 0x5764, (uint16_t)(rt_bp - 6)) != 0) {
    if ((drd8((uint16_t)(0x5ab8 + (uint16_t)(drd16(0x014c) * 0xc0))) & 1) == 0) {
      uint32_t r;
      {
        const uint16_t a[] = {(uint16_t)(drd16(0x007a) << 4), 6};
        r = far_call(12, 0x0232, a, 2);
        dwr16(0x1600, (uint16_t)r);
        dwr16(0x1602, (uint16_t)(r >> 16));
      }
      {
        const uint16_t a[] = {(uint16_t)(drd16(0x007a) << 2), 6};
        r = far_call(12, 0x0232, a, 2);
        dwr16(0x1608, (uint16_t)r);
        dwr16(0x160a, (uint16_t)(r >> 16));
      }
      if (drd16(0x1604) == 0 && drd16(0x1606) == 0) {
        const uint16_t a[] = {0x2000, 6};
        r = far_call(12, 0x0232, a, 2);
        dwr16(0x1604, (uint16_t)r);
        dwr16(0x1606, (uint16_t)(r >> 16));
        if ((uint16_t)(r >> 16) != 0 || drd16(0x1604) != 0) wr16(drd16(0x1606), drd16(0x1604), 2);
      }
      if ((drd16(0x1600) | drd16(0x1602)) != 0 && (drd16(0x1604) | drd16(0x1606)) != 0 &&
          (drd16(0x1608) | drd16(0x160a)) != 0) {
        /* clear the info array: (DS:0x7a << 4) / 2 words */
        uint16_t n = (uint16_t)((uint16_t)(drd16(0x007a) << 4) >> 1), seg = drd16(0x1602), off = drd16(0x1600);
        while (n != 0) {
          wr16(seg, off, 0);
          off = (uint16_t)(off + 2);
          n--;
        }
        (void)CALL_ORIG(7, 0x09ae, FAR_CDECL, 0);
        (void)CALL_ORIG(18, 0x40ba, FAR_CDECL, 0);
        (void)CALL_ORIG(18, 0x5e0e, FAR_CDECL, 0);
        (void)CALL_ORIG(18, 0x60b2, FAR_CDECL, 0);
        if (type != 0xffff) dwr16((uint16_t)(0x5a6a + (uint16_t)(si * 0xc0)), type);
        uint16_t sel = (uint16_t)(drd16((uint16_t)(0x5a6a + (uint16_t)(si * 0xc0))) >> 13);
        const uint16_t buf[] = {(uint16_t)(rt_bp - 0x106)};
        switch (sel) {
          case 0: (void)far_call(17, 0x0462, buf, 1); break;
          case 1: (void)far_call(17, 0x234e, buf, 1); break;
          case 2: (void)far_call(19, 0x0400, buf, 1); break;
          case 3: (void)far_call(19, 0x1594, buf, 1); break;
          case 4: (void)far_call(21, 0x0000, buf, 1); break;
          case 5: (void)far_call(20, 0x0000, buf, 1); break;
          case 7: (void)far_call(19, 0x0000, buf, 1); break;
          default: break;
        }
      }
    }
    (void)CALL_ORIG(9, 0x7d1e, FAR_CDECL, 2, 0x5764, si);
    (void)CALL_ORIG(9, 0x80da, FAR_CDECL, 1, si);
    if ((drd16(0x1608) | drd16(0x160a)) != 0) {
      const uint16_t a[] = {drd16(0x1608), drd16(0x160a), 6};
      (void)far_call(12, 0x04d0, a, 3);
      dwr16(0x1608, 0);
      dwr16(0x160a, 0);
    }
    if ((drd16(0x1604) | drd16(0x1606)) != 0) {
      const uint16_t a[] = {drd16(0x1600), drd16(0x1602), 6};
      const uint16_t b[] = {drd16(0x1604), drd16(0x1606), 6};
      (void)far_call(12, 0x04d0, a, 3);
      (void)far_call(12, 0x04d0, b, 3);
      dwr16(0x1600, 0);
      dwr16(0x1602, 0);
      dwr16(0x1604, 0);
      dwr16(0x1606, 0);
    }
  }
  dwr16(0x014c, L16(-0x2));
  dwr16(0x15fe, 0);
  FRAME_LEAVE();
}


/* ai0_create_designs: 17:0x1730 (Ghidra FUN_1080_1730, alias fn_17_1730), 1088 bytes. ENTER 0x90,0; PUSH DI; PUSH SI.
 * FAR CDECL, no arguments, no result. Builds ship designs for the current player (DS:0x14c; its record
 * is DS:0x5a64 + 0xc0 * player, whose bytes +0x1a to +0x1f are read signed and compared with minimum
 * levels) in the design slots of the 0x8d-byte record array DS:0x417a (slot k at DS:0x417a + 0x8d * k).
 * A slot is considered only when bit 1 of its byte +0x7c is set (by 18:0x0000 and 18:0x0630's Ghidra
 * pseudo-C, a free slot); in a group of slots a later one is skipped while the turn DS:0x82 minus the
 * turn stamp word +0x7d of the previous record is not above a limit (and, for the groups from slot 2
 * on, while the previous slot's bit 1 is set). Every design is 18:0x0000(slot | hull word << 16,
 * 17:(word of the index table + 0x224), the part list); the loops that pick a random table entry
 * (8:0x8730) stop at the first one 18:0x0000 accepts (at most 5 tries). Last, when slot 0's first word
 * DS:0x417a is not 5, the player's bits (byte +7 & 0x1c) are above 4, the dword DS:0x41fd is 0 and the
 * levels are high enough, slot 0's record is copied to the local BP-0x8e, bit 1 of its byte +0x7c is
 * set, it is handed to 18:0x0630(record, 0) and slot 0 is built again with hull word 5. Locals are
 * BP_LOCAL slots at their original offsets (the callees see their addresses). */
DECOMP(17, 0x1730, ai0_create_designs, FAR_CDECL)
void ai0_create_designs(void)
{
  __attribute__((unused)) uint16_t ax = 0, bx = 0, cx = 0, di = 0, dx = 0, si = 0, cx_h = 0;
  __attribute__((unused)) uint16_t es = 0;
  uint16_t t1 = 0, t2 = 0, t3 = 0;
  FRAME_ENTER_REGS(0x90, 2);
  di = (uint16_t)(0xb);
  L16(-0x8) = (uint16_t)(0x4804);
  L16(-0x6) = (uint16_t)(0x4);
L1743:
  bx = (uint16_t)(L16(-0x8));
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx + 0x1))) & (uint8_t)(0x2)) == 0) goto L17e9;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x2)) goto L17e9;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8((uint16_t)(bx + 0x5a81))));
  ax = (uint16_t)(int16_t)(int8_t)ax;
  if ((int16_t)(ax) < (int16_t)(L16(-0x6))) goto L17e9;
  if ((uint16_t)(L16(-0x6)) == (uint16_t)(0x4)) goto L1780;
  bx = (uint16_t)(L16(-0x8));
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - drd16((uint16_t)(bx - 0x8b))));
  if ((uint16_t)(ax) <= (uint16_t)(0xe)) goto L17e9;
L1780:
  bx = (uint16_t)(L16(-0x90));
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) >= (int8_t)(0xa)) goto L17b2;
  if ((uint16_t)(di) != (uint16_t)(0xb)) goto L1796;
  bx = (uint16_t)(0xe);
  goto L1799;
L1796:
  bx = (uint16_t)(0xf);
L1799:
  bx = (uint16_t)((uint16_t)(bx + bx));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x351)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0xb, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L17e9;
L17b2:
  dx = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(dx);
  L16(-0x4) = (uint16_t)(di);
  si = (uint16_t)(dx);
L17bc:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x6); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  bx = (uint16_t)((uint16_t)(bx + ax));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x361)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0x1f, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L17e9;
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L17bc;
L17e9:
  L16(-0x8) = (uint16_t)((uint16_t)(L16(-0x8) + 0x8d));
  di = (uint16_t)((uint16_t)(di + 1));
  L16(-0x6) = (uint16_t)((uint16_t)(L16(-0x6) + 0x3));
  if ((int16_t)(L16(-0x6)) <= (int16_t)(0xa)) goto L1743;
  if ((uint8_t)((uint8_t)(drd8(0x49ac)) & (uint8_t)(0x2)) == 0) goto L187c;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0x5)) goto L187c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x6)) goto L187c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x6)) goto L187c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x6)) goto L187c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7e))) < (int8_t)(0x2)) goto L187c;
  si = (uint16_t)(0);
L1831:
  ax = (uint16_t)(cs_w(0x39b));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xe, 0x1d, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L1876;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  bx = (uint16_t)((uint16_t)(bx + ax));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x371)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xe, 0x6, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L187c;
L1876:
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L1831;
L187c:
  if ((uint8_t)((uint8_t)(drd8(0x4a39)) & (uint8_t)(0x2)) == 0) goto L18f6;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0xa)) goto L18f6;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x8)) goto L18f6;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x9)) goto L18f6;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0xe)) goto L18f6;
  ax = (uint16_t)(cs_w(0x39b));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xf, 0x1d, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L18f6;
  si = (uint16_t)(0);
L18c6:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  bx = (uint16_t)((uint16_t)(bx + ax));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x379)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xf, 0x6, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L18f6;
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L18c6;
L18f6:
  di = (uint16_t)(0x2);
  bx = (uint16_t)(0x430f);
L18fc:
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx + 0x1))) & (uint8_t)(0x2)) == 0) goto L198e;
  si = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(si);
  if ((int8_t)(drd8((uint16_t)(si + 0x5a7f))) < (int8_t)(0xa)) goto L198e;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a81))) < (int8_t)(0xa)) goto L198e;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a80))) < (int8_t)(0x9)) goto L198e;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a7e))) < (int8_t)(0x6)) goto L198e;
  if ((uint16_t)(bx) == (uint16_t)(0x430f)) goto L1943;
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx - 0x8c))) & (uint8_t)(0x2)) != 0) goto L198e;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - drd16((uint16_t)(bx - 0x8b))));
  if ((uint16_t)(ax) <= (uint16_t)(0xc)) goto L198e;
L1943:
  L16(-0x8) = (uint16_t)(bx);
  ax = (uint16_t)(di);
  ax = (uint16_t)((uint16_t)(ax & 0x1));
  t1 = (!((uint16_t)(ax) < (uint16_t)(0x1))) ? 1 : 0;
  t2 = ax; t3 = ax;
  ax = (uint16_t)((uint16_t)(t2 - t3 - t1));
  ax = (uint16_t)((uint16_t)(ax & 0x4));
  L16(-0x6) = (uint16_t)(ax);
  dx = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(dx);
  L16(-0x4) = (uint16_t)(di);
  si = (uint16_t)(dx);
L1961:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  bx = (uint16_t)((uint16_t)(bx + L16(-0x6)));
  bx = (uint16_t)((uint16_t)(bx + bx));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x351)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0x1f, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L1a0a;
L198b:
  bx = (uint16_t)(L16(-0x8));
L198e:
  di = (uint16_t)((uint16_t)(di + 1));
  bx = (uint16_t)((uint16_t)(bx + 0x8d));
  if ((uint16_t)(bx) <= (uint16_t)(0x44b6)) goto L18fc;
  di = (uint16_t)(0x6);
  bx = (uint16_t)(0x4543);
L19a1:
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx + 0x1))) & (uint8_t)(0x2)) == 0) goto L1a53;
  si = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(si);
  if ((int8_t)(drd8((uint16_t)(si + 0x5a83))) < (int8_t)(0x4)) goto L1a53;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a82))) < (int8_t)(0xa)) goto L1a53;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a81))) < (int8_t)(0xc)) goto L1a53;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a80))) < (int8_t)(0xc)) goto L1a53;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a7e))) < (int8_t)(0x6)) goto L1a53;
  if ((int8_t)(drd8((uint16_t)(si + 0x5a7f))) < (int8_t)(0xf)) goto L1a53;
  if ((uint16_t)(bx) == (uint16_t)(0x4543)) goto L19fc;
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx - 0x8c))) & (uint8_t)(0x2)) != 0) goto L1a53;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - drd16((uint16_t)(bx - 0x8b))));
  if ((uint16_t)(ax) <= (uint16_t)(0x14)) goto L1a53;
L19fc:
  L16(-0x8) = (uint16_t)(bx);
  if ((uint16_t)(di) != (uint16_t)(0x6)) goto L1a16;
  ax = (uint16_t)(0x1b);
  goto L1a19;
L1a0a:
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L1961;
  goto L198b;
L1a16:
  ax = (uint16_t)(0x1f);
L1a19:
  L16(-0x6) = (uint16_t)(ax);
  dx = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(dx);
  L16(-0x4) = (uint16_t)(di);
  si = (uint16_t)(dx);
L1a26:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  bx = (uint16_t)((uint16_t)(bx + L16(-0x6)));
  bx = (uint16_t)((uint16_t)(bx + bx));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x351)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0x9, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L1ab4;
L1a50:
  bx = (uint16_t)(L16(-0x8));
L1a53:
  di = (uint16_t)((uint16_t)(di + 1));
  bx = (uint16_t)((uint16_t)(bx + 0x8d));
  if ((uint16_t)(bx) <= (uint16_t)(0x45d0)) goto L19a1;
  di = (uint16_t)(0x9);
  si = (uint16_t)(0x46ea);
L1a66:
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(si + 0x1))) & (uint8_t)(0x2)) == 0) goto L1ad8;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0xe)) goto L1ad8;
  if ((uint16_t)(si) == (uint16_t)(0x46ea)) goto L1a92;
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(si - 0x8c))) & (uint8_t)(0x2)) != 0) goto L1ad8;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - drd16((uint16_t)(si - 0x8b))));
  if ((uint16_t)(ax) <= (uint16_t)(0xf)) goto L1ad8;
L1a92:
  ax = (uint16_t)(cs_w(0x399));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0x9, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L1ad8;
  if ((uint16_t)(di) != (uint16_t)(0x9)) goto L1abe;
  bx = (uint16_t)(0x18);
  goto L1ac1;
L1ab4:
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L1a26;
  goto L1a50;
L1abe:
  bx = (uint16_t)(0x19);
L1ac1:
  bx = (uint16_t)((uint16_t)(bx + bx));
  ax = (uint16_t)(cs_w((uint16_t)(bx + 0x351)));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, di, 0x13, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1ad8:
  di = (uint16_t)((uint16_t)(di + 1));
  si = (uint16_t)((uint16_t)(si + 0x8d));
  if ((uint16_t)(si) <= (uint16_t)(0x4777)) goto L1a66;
  if ((uint16_t)(drd16(0x417a)) == (uint16_t)(0x5)) goto L1b6c;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8((uint16_t)(bx + 0x5a6b))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x1c)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) <= (uint8_t)(0x4)) goto L1b6c;
  if ((uint32_t)(drd32(0x41fd)) != (uint32_t)(0x0)) goto L1b6c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a83))) < (int8_t)(0x4)) goto L1b6c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x5)) goto L1b6c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x6)) goto L1b6c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x6)) goto L1b6c;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7e))) < (int8_t)(0x6)) goto L1b6c;
  di = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  si = (uint16_t)(0x417a);
  ax = (uint16_t)(rt_ss);
  es = ax;
  SET32(cx, cx_h, 0x23);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr8(es, di, rd8(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 1); si = (uint16_t)(si + 1);
  L8(-0x12) = (uint8_t)((uint8_t)(L8(-0x12) | 0x2));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  { uint32_t r_ = CALL_ORIG(18, 0x630, FAR_CDECL, 2, ax, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)(cs_w(0x385));
  ax = (uint16_t)((uint16_t)(ax + 0x224));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x0, 0x5, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1b6c:
  FRAME_LEAVE();
  return;
}

/* ai1_create_designs: 17:0x38b0 (Ghidra FUN_1080_38b0, alias fn_17_38b0), 895 bytes. ENTER 0x90,0; PUSH DI; PUSH SI.
 * FAR CDECL, no arguments, no result. The twin of 17:0x1730 for the type-1 player: builds designs in
 * the slots 8, 9, 10, 1, 0, 2, 12, 13, 14, 4 and 15 (in that order) of the record array DS:0x417a whose
 * bit 1 of byte +0x7c (DS:0x465e, 0x46eb, 0x4778, 0x4283, 0x41f6, 0x4310, 0x4892, 0x491f, 0x49ac,
 * 0x442a, 0x4a39) is set and (for all but slots 1 and 0) whose minimum levels (the player's bytes
 * DS:0x5a7e + 0xc0 * player and on, signed) are met, through 18:0x0000(slot | hull word << 16, 17:(table byte + 0x39d), the part list).
 * The slots 1, 0, 2 and 12 are also taken when the bit is clear but the dword +0x83 of the record
 * (DS:0x428a, 0x41fd, 0x4317, 0x4899) is 0; then the records of slots 1 (unless its first word DS:0x4207
 * is 0xb), 0 and 2 are copied to BP-0x8e, given bit 1 of byte +0x7c and handed to 18:0x0630(record,
 * slot) before the new design. */
DECOMP(17, 0x38b0, ai1_create_designs, FAR_CDECL)
void ai1_create_designs(void)
{
  __attribute__((unused)) uint16_t ax = 0, bx = 0, cx = 0, di = 0, dx = 0, si = 0, cx_h = 0;
  __attribute__((unused)) uint16_t es = 0;
  FRAME_ENTER_REGS(0x90, 2);
  if ((uint8_t)((uint8_t)(drd8(0x465e)) & (uint8_t)(0x2)) == 0) goto L38ee;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x5)) goto L38ee;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x8)) goto L38ee;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x436)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x8, 0xc, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L38ee:
  if ((uint8_t)((uint8_t)(drd8(0x46eb)) & (uint8_t)(0x2)) == 0) goto L3926;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x7)) goto L3926;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0xb)) goto L3926;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x439)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x9, 0xd, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3926:
  if ((uint8_t)((uint8_t)(drd8(0x4778)) & (uint8_t)(0x2)) == 0) goto L3985;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0x5)) goto L3985;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x5)) goto L3985;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x4)) goto L3985;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x5)) goto L3985;
  si = (uint16_t)(0);
L3955:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b((uint16_t)(bx + 0x42c))));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xa, 0x6, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L3985;
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x4)) goto L3955;
L3985:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x4283)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L3996;
  if ((uint32_t)(drd32(0x428a)) != (uint32_t)(0x0)) goto L39e7;
L3996:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x4283)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L39ce;
  if ((uint16_t)(drd16(0x4207)) == (uint16_t)(0xb)) goto L39ce;
  di = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  si = (uint16_t)(0x4207);
  ax = (uint16_t)(rt_ss);
  es = ax;
  SET32(cx, cx_h, 0x23);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr8(es, di, rd8(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 1); si = (uint16_t)(si + 1);
  L8(-0x12) = (uint8_t)((uint8_t)(L8(-0x12) | 0x2));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  { uint32_t r_ = CALL_ORIG(18, 0x630, FAR_CDECL, 2, ax, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L39ce:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x42a)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x1, 0xf, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L39e7:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x41f6)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L39f8;
  if ((uint32_t)(drd32(0x41fd)) != (uint32_t)(0x0)) goto L3a42;
L39f8:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x41f6)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L3a29;
  di = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  si = (uint16_t)(0x417a);
  ax = (uint16_t)(rt_ss);
  es = ax;
  SET32(cx, cx_h, 0x23);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr8(es, di, rd8(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 1); si = (uint16_t)(si + 1);
  L8(-0x12) = (uint8_t)((uint8_t)(L8(-0x12) | 0x2));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  { uint32_t r_ = CALL_ORIG(18, 0x630, FAR_CDECL, 2, ax, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3a29:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x42b)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x0, 0x5, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3a42:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x4310)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L3a53;
  if ((uint32_t)(drd32(0x4317)) != (uint32_t)(0x0)) goto L3ab5;
L3a53:
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x7)) goto L3ab5;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x4)) goto L3ab5;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8(0x4310)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x2)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != 0) goto L3a9c;
  di = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  si = (uint16_t)(0x4294);
  ax = (uint16_t)(rt_ss);
  es = ax;
  SET32(cx, cx_h, 0x23);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr8(es, di, rd8(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 1); si = (uint16_t)(si + 1);
  L8(-0x12) = (uint8_t)((uint8_t)(L8(-0x12) | 0x2));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x8e));
  { uint32_t r_ = CALL_ORIG(18, 0x630, FAR_CDECL, 2, ax, 0x2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3a9c:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x43b)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x2, 0x16, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3ab5:
  if ((uint8_t)((uint8_t)(drd8(0x4892)) & (uint8_t)(0x2)) != 0) goto L3ac4;
  if ((uint32_t)(drd32(0x4899)) != (uint32_t)(0x0)) goto L3af5;
L3ac4:
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x4)) goto L3af5;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a83))) < (int8_t)(0x4)) goto L3af5;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x438)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xc, 0xb, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3af5:
  if ((uint8_t)((uint8_t)(drd8(0x491f)) & (uint8_t)(0x2)) == 0) goto L3b34;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0x8)) goto L3b34;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x7)) goto L3b34;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x6)) goto L3b34;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x437)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xd, 0x12, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3b34:
  if ((uint8_t)((uint8_t)(drd8(0x49ac)) & (uint8_t)(0x2)) == 0) goto L3b7a;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0xb)) goto L3b7a;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0xc)) goto L3b7a;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0xf)) goto L3b7a;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x9)) goto L3b7a;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x437)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xe, 0x12, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L3b7a:
  if ((uint8_t)((uint8_t)(drd8(0x442a)) & (uint8_t)(0x2)) == 0) goto L3bd9;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0x5)) goto L3bd9;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x6)) goto L3bd9;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0xd)) goto L3bd9;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x7)) goto L3bd9;
  si = (uint16_t)(0);
L3ba9:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)(ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b((uint16_t)(bx + 0x432))));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0x4, 0x9, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L3bd9;
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L3ba9;
L3bd9:
  if ((uint8_t)((uint8_t)(drd8(0x4a39)) & (uint8_t)(0x2)) == 0) goto L3c2b;
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  L16(-0x90) = (uint16_t)(bx);
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a7f))) < (int8_t)(0x5)) goto L3c2b;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a82))) < (int8_t)(0x6)) goto L3c2b;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0xd)) goto L3c2b;
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x7)) goto L3c2b;
  si = (uint16_t)(0);
L3c08:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(cs_b(0x436)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + 0x39d));
  { uint32_t r_ = CALL_ORIG(18, 0x0, FAR_CDECL, 4, 0xf, 0xc, ax, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L3c2b;
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x5)) goto L3c08;
L3c2b:
  FRAME_LEAVE();
  return;
}

/* ai0_fleet_orders: 17:0x1b70 (Ghidra FUN_1080_1b70, alias fn_17_1b70), 1412 bytes. ENTER 0x22,0; PUSH DI; PUSH SI.
 * FAR CDECL (fleet [BP+6], a far pointer), AX = 0xffff or 0. Decides what one fleet of the type-0 player does:
 * checks the distance (8:0xbc50) from the fleet to its last waypoint (an 18-byte record copied from the
 * waypoint list +0x64 to BP-0x1e), sums the ship counts of the fleet with 17:0x20f4-style loops, compares
 * them with the thresholds DS:0x5340 to 0x5343 and the cursor value DS:0x4c30, then either queues an order
 * (18:0x0750, 18:0x2664, 18:0x3bb4, 18:0x07ee, 18:0x457a), picks a target planet with the callbacks
 * 17:0x2154 / 0x216c / 0x2252 / 18:0x3be2 / 17:0x3c82 through 18:0x3854 / 18:0x39f6 / 18:0x3922 (called with
 * far function pointers 17:cb), or does nothing (AX 0). Returns 0xffff when 18:0x2664 refused the order. */
DECOMP(17, 0x1b70, ai0_fleet_orders, FAR_CDECL)
uint16_t ai0_fleet_orders(uint32_t fleet)
{
  __attribute__((unused)) uint16_t ax = 0, bx = 0, cx = 0, di = 0, dx = 0, si = 0, ax_h = 0, cx_h = 0, dx_h = 0;
  __attribute__((unused)) uint16_t es = 0;
  __attribute__((unused)) uint16_t ds = MEM_SEL_DGROUP;
  uint16_t t3 = 0, t4 = 0, t12 = 0, t13 = 0, t14 = 0, t15 = 0, t16 = 0, t19 = 0, t20 = 0, t21 = 0, t22 = 0, t23 = 0, t24 = 0, t25 = 0, t26 = 0, t27 = 0, t28 = 0, t29 = 0, t30 = 0, t31 = 0, t32 = 0, t33 = 0, t36 = 0;
  uint32_t st0 = 0, t17 = 0;
  FRAME_ENTER_REGS(0x22, 2);
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L1c23;
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x64)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + 0x66)));
  ax = (uint16_t)((uint16_t)(ax + 0x16));
  di = (uint16_t)((uint16_t)(rt_bp - 0x1e));
  si = (uint16_t)(ax);
  es = rt_ss;
  ds = dx;
  SET32(cx, cx_h, 0x4);
  while (cx != 0) { wr32(es, di, rd32(ds, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr16(es, di, rd16(ds, si)); di = (uint16_t)(di + 2); si = (uint16_t)(si + 2);
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  { uint32_t r_ = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)(rd32(es, (uint16_t)(bx + 0x8))), (uint16_t)((rd32(es, (uint16_t)(bx + 0x8))) >> 16), (uint16_t)(L32(-0x1e)), (uint16_t)((L32(-0x1e)) >> 16)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dx = (uint16_t)((uint16_t)(dx | dx));
  if ((int16_t)(dx) < 0) goto L1bcc;
  if ((int16_t)(dx) > 0) goto L1bc1;
  if ((uint16_t)(ax) <= (uint16_t)(0xf424)) goto L1bcc;
L1bc1:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(L8(-0x17)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) == (uint8_t)(0x2)) goto L1c23;
L1bcc:
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(L8(-0x17)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) == (uint8_t)(0x2)) goto L20ee;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(L8(-0x17)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L1c23;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, L16(-0x1a)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L20ee;
  es = L16(-0x2);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L1c18;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L20ee;
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) != 0) goto L20ee;
L1c18:
  ax = (uint16_t)(drd16(0x82));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x32))) != (uint16_t)(ax)) goto L20ee;
L1c23:
  di = (uint16_t)(0);
  ax = (uint16_t)((uint16_t)fleet);
  dx = (uint16_t)((uint16_t)(fleet >> 16));
  ax = (uint16_t)((uint16_t)(ax + 0x10));
  bx = (uint16_t)(ax);
  L16(-0x4) = (uint16_t)(dx);
  L16(-0x2) = (uint16_t)(0x4);
  cx = (uint16_t)(L16(-0x2));
  es = L16(-0x4);
L1c3e:
  si = (uint16_t)(bx);
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  di = (uint16_t)((uint16_t)(di + rd16(es, si)));
  t3 = cx;
  cx = (uint16_t)((uint16_t)(t3 - 1));
  if ((uint16_t)(t3) != (uint16_t)(1)) goto L1c3e;
  L16(-0x8) = (uint16_t)(di);
  ax = (uint16_t)((uint16_t)fleet);
  dx = (uint16_t)((uint16_t)(fleet >> 16));
  ax = (uint16_t)((uint16_t)(ax + 0x18));
  bx = (uint16_t)(ax);
  ds = dx;
  cx = (uint16_t)(0x2);
L1c5c:
  si = (uint16_t)(bx);
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  ax = (uint16_t)(rd16(ds, si));
  ax = (uint16_t)((uint16_t)(ax + ax));
  di = (uint16_t)((uint16_t)(di + ax));
  t4 = cx;
  cx = (uint16_t)((uint16_t)(t4 - 1));
  if ((uint16_t)(t4) != (uint16_t)(1)) goto L1c5c;
  /* ds = ss */
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  si = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1e)));
  si = (uint16_t)((uint16_t)(si + rd16(es, (uint16_t)(bx + 0x20))));
  wr8(es, (uint16_t)(bx + 0x5), (uint8_t)(rd8(es, (uint16_t)(bx + 0x5)) | 0x80));
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) != (uint16_t)(0xffff)) goto L1ca4;
  { uint32_t r_ = CALL_ORIG(18, 0x457a, FAR_CDECL, 3, bx, es, 0x96); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L20ee;
L1ca4:
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(bx + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  ax = (uint16_t)(drd16(0x14c));
  bx = L16(-0x4);
  es = L16(-0x2);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) != (uint16_t)(ax)) goto L1dd6;
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + 0x5))) & (uint8_t)(0x2)) == 0) goto L1dd6;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5340)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) > (int16_t)(di)) goto L1d64;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5342)));
  if ((int16_t)(ax) > (int16_t)(si)) goto L1d64;
  if ((int32_t)(drd32(0x4c30)) <= (int32_t)(0xbb8)) goto L1d00;
  st0 = 0xa;
L1cef:
  SET32(ax, ax_h, drd32(0x4c30));
  SET32(cx, cx_h, st0);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1cf7); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  L32(-0x8) = (uint32_t)(E32(ax, ax_h));
  goto L1d27;
L1d00:
  if ((int32_t)(drd32(0x4c30)) <= (int32_t)(0x7d0)) goto L1d10;
  st0 = 0xf;
  goto L1cef;
L1d10:
  if ((int32_t)(drd32(0x4c30)) <= (int32_t)(0x3e8)) goto L1d20;
  st0 = 0x14;
  goto L1cef;
L1d20:
  SET32(ax, ax_h, 0);
  L32(-0x8) = (uint32_t)(E32(ax, ax_h));
L1d27:
  if ((int16_t)(L16(-0x6)) < (int16_t)(0x0)) goto L1ff1;
  if ((int16_t)(L16(-0x6)) > (int16_t)(0x0)) goto L1d37;
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L1ff1;
L1d37:
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  { uint32_t r_ = CALL_ORIG(18, 0x750, FAR_CDECL, 6, 0x1, rd16(es, (uint16_t)(bx + 0x6)), 0x2, rd16(es, bx), 0x3, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L1ff1;
L1d64:
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8((uint16_t)(bx + 0x5a6b))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x1c)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) <= (uint8_t)(0x4)) goto L20ee;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5340)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + ax));
  if ((int16_t)(ax) < (int16_t)(di)) goto L1d8a;
  if ((int16_t)(di) < (int16_t)(0x3c)) goto L20ee;
L1d8a:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) < (int16_t)(0x5)) goto L1ff1;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5340)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  cx = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(ax + ax));
  ax = (uint16_t)((uint16_t)(ax + cx));
  if ((int16_t)(ax) >= (int16_t)(di)) goto L1dbb;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) < (int16_t)(0x7)) goto L1ff1;
L1dbb:
  if ((int16_t)(di) <= (int16_t)(0x78)) goto L20ee;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) >= (int16_t)(0x7)) goto L20ee;
  goto L1ff1;
L1dd6:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5341)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) > (int16_t)(di)) goto L1f86;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5343)));
  if ((int16_t)(ax) > (int16_t)(si)) goto L1f86;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(0xffff)) goto L1ff1;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) != (uint16_t)(ax)) goto L1e16;
  if ((int32_t)(rd32(es, (uint16_t)(bx + 0x28))) <= (int32_t)(0x3e8)) goto L1ff1;
  cx = (uint16_t)(0x5);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x28)));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x1e11); ax = a_; dx = d_; }
  goto L1d37;
L1e16:
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(ax)) goto L20ee;
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  SET32(ax, ax_h, rd32(es, (uint16_t)(bx + 0x58)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + 0x5a)));
  L32(-0xc) = (uint32_t)(E32(ax, ax_h));
  bx = (uint16_t)(0xffff);
  cx = (uint16_t)(bx);
  si = L16(-0x4);
  es = L16(-0x2);
  di = (uint16_t)(rd16(es, (uint16_t)(si + 0x12)));
  si = (uint16_t)(di);
  di = (uint16_t)((uint16_t)((uint16_t)di >> 12));
  t12 = cx; t13 = di;
  cx = (uint16_t)((uint16_t)(t12 - t13));
  t14 = ((uint16_t)(t12) < (uint16_t)(t13)) ? 1 : 0;
  t15 = bx; t16 = 0x0;
  bx = (uint16_t)((uint16_t)(t15 - t16 - t14));
  L16(-0x22) = (uint16_t)(ax);
  L16(-0x20) = (uint16_t)(dx);
  SET32(ax, ax_h, (((uint32_t)(bx) << 16) | (uint16_t)(cx)));
  SET32(cx, cx_h, 0x12);
  { uint32_t a_ = E32(ax, ax_h), d_; imul32_(&a_, &d_, E32(cx, cx_h)); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(cx, cx_h, 0x4);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1e5d); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(ax, ax_h, (uint32_t)(E32(ax, ax_h) + 0x64));
  t17 = E32(ax, ax_h);
  ax = (uint16_t)(si);
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  cx = (uint16_t)(0x190);
  { uint32_t p_ = (uint32_t)ax * (uint32_t)cx; ax = (uint16_t)p_; dx = (uint16_t)(p_ >> 16); }
  SET32(ax, ax_h, (((uint32_t)(dx) << 16) | (uint16_t)(ax)));
  SET32(cx, cx_h, t17);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1e7a); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  L32(-0x8) = (uint32_t)(E32(ax, ax_h));
  dx = (uint16_t)(L16(-0x6));
  si = (uint16_t)(ax);
  di = (uint16_t)(dx);
  SET32(ax, ax_h, L32(-0x22));
  SET32(cx, cx_h, 0x5);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1e94); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(dx, dx_h, E32(ax, ax_h));
  SET32(dx, dx_h, (uint32_t)((uint32_t)E32(dx, dx_h) >> 16));
  if ((int16_t)(di) < (int16_t)(dx)) goto L1ed1;
  if ((int16_t)(di) > (int16_t)(dx)) goto L1ea8;
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L1ed1;
L1ea8:
  if ((int32_t)(L32(-0x8)) >= (int32_t)(0xc8)) goto L1ebc;
  if ((int32_t)(L32(-0xc)) > (int32_t)(0x15e)) goto L1ed1;
L1ebc:
  if ((int32_t)(L32(-0x8)) >= (int32_t)(0xa)) goto L20ee;
  if ((int32_t)(L32(-0xc)) <= (int32_t)(0x96)) goto L20ee;
L1ed1:
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  SET32(ax, ax_h, rd32(es, (uint16_t)(bx + 0x58)));
  SET32(cx, cx_h, 0x2);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1ee1); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(dx, dx_h, E32(ax, ax_h));
  SET32(dx, dx_h, (uint32_t)((uint32_t)E32(dx, dx_h) >> 16));
  cx = (uint16_t)(L16(-0x8));
  bx = (uint16_t)(L16(-0x6));
  si = (uint16_t)(cx);
  di = (uint16_t)(bx);
  t19 = cx; t20 = cx;
  cx = (uint16_t)((uint16_t)(t19 + t20));
  t21 = ((uint16_t)(cx) < (uint16_t)(t19)) ? 1 : 0;
  t22 = bx; t23 = bx;
  bx = (uint16_t)((uint16_t)(t22 + t23 + t21));
  t24 = cx; t25 = cx;
  cx = (uint16_t)((uint16_t)(t24 + t25));
  t26 = ((uint16_t)(cx) < (uint16_t)(t24)) ? 1 : 0;
  t27 = bx; t28 = bx;
  bx = (uint16_t)((uint16_t)(t27 + t28 + t26));
  t29 = cx; t30 = si;
  cx = (uint16_t)((uint16_t)(t29 + t30));
  t31 = ((uint16_t)(cx) < (uint16_t)(t29)) ? 1 : 0;
  t32 = bx; t33 = di;
  bx = (uint16_t)((uint16_t)(t32 + t33 + t31));
  si = (uint16_t)(ax);
  di = (uint16_t)(dx);
  SET32(ax, ax_h, (((uint32_t)(bx) << 16) | (uint16_t)(cx)));
  SET32(cx, cx_h, 0x4);
  SET32(dx, dx_h, ((int32_t)E32(ax, ax_h) < 0) ? 0xffffffffu : 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); idiv32_(&a_, &d_, E32(cx, cx_h), 0x1f10); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(dx, dx_h, E32(ax, ax_h));
  SET32(dx, dx_h, (uint32_t)((uint32_t)E32(dx, dx_h) >> 16));
  if ((int16_t)(di) > (int16_t)(dx)) goto L1f28;
  if ((int16_t)(di) < (int16_t)(dx)) goto L1f24;
  if ((uint16_t)(si) >= (uint16_t)(ax)) goto L1f28;
L1f24:
  di = (uint16_t)(dx);
  si = (uint16_t)(ax);
L1f28:
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  if ((int16_t)(di) < (int16_t)(rd16(es, (uint16_t)(bx + 0x5a)))) goto L1f41;
  if ((int16_t)(di) > (int16_t)(rd16(es, (uint16_t)(bx + 0x5a)))) goto L1f39;
  if ((uint16_t)(si) <= (uint16_t)(rd16(es, (uint16_t)(bx + 0x58)))) goto L1f41;
L1f39:
  di = (uint16_t)(rd16(es, (uint16_t)(bx + 0x5a)));
  si = (uint16_t)(rd16(es, (uint16_t)(bx + 0x58)));
L1f41:
  L16(-0xc) = (uint16_t)(si);
  L16(-0xa) = (uint16_t)(di);
  di = (uint16_t)((uint16_t)(di | di));
  if ((int16_t)(di) < 0) goto L1f5b;
  if ((int16_t)(di) > 0) goto L1f53;
  if ((uint16_t)(si) <= (uint16_t)(0x7530)) goto L1f5b;
L1f53:
  L32(-0xc) = (uint32_t)(0x7530);
L1f5b:
  bx = L16(-0x4);
  es = L16(-0x2);
  t36 = rd16(es, bx);
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  { uint32_t r_ = CALL_ORIG(18, 0x7ee, FAR_CDECL, 3, rd16(es, bx), t36, L16(-0xc)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)fleet;
  es = (uint16_t)(fleet >> 16);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L20ee;
L1f86:
  { uint32_t r_ = CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, (uint16_t)(fleet), (uint16_t)((fleet) >> 16), 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8((uint16_t)(bx + 0x5a6b))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0x1c)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) <= (uint8_t)(0x4)) goto L202c;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5340)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)(ax + ax));
  if ((int16_t)(ax) >= (int16_t)(di)) goto L1fc2;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) < (int16_t)(0x5)) goto L1ff1;
L1fc2:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x5340)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 2));
  if ((int16_t)(ax) >= (int16_t)(di)) goto L1fdd;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) < (int16_t)(0x7)) goto L1ff1;
L1fdd:
  if ((int16_t)(di) <= (int16_t)(0x78)) goto L202c;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) >= (int16_t)(0x7)) goto L202c;
L1ff1:
  if ((uint8_t)((uint8_t)(drd8(0x80)) & (uint8_t)(0x10)) == 0) goto L200c;
  { uint32_t r_ = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, (uint16_t)(L32(-0x4)), (uint16_t)((L32(-0x4)) >> 16), 0x2252, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L200f;
L200c:
  ax = (uint16_t)(0);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
L200f:
  si = (uint16_t)(ax);
  L16(-0x6) = (uint16_t)(dx);
  ax = (uint16_t)(dx);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) != 0) goto L2043;
  { uint32_t r_ = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, (uint16_t)(L32(-0x4)), (uint16_t)((L32(-0x4)) >> 16), 0x216c, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L203b;
L202c:
  { uint32_t r_ = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, (uint16_t)(L32(-0x4)), (uint16_t)((L32(-0x4)) >> 16), 0x3be2, SEG_SEL(18)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L203b:
  si = (uint16_t)(ax);
  L16(-0x6) = (uint16_t)(dx);
L2043:
  ax = (uint16_t)(dx);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L2080;
  es = dx;
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + di + 0xa), (uint8_t)(rd8(es, (uint16_t)(bx + di + 0xa)) | 0x80));
  es = L16(-0x6);
  ax = (uint16_t)(rd16(es, si));
  L16(-0x1a) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x18));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x18) = (uint16_t)(ax);
  bx = (uint16_t)(rd16(es, si));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  ax = (uint16_t)(drd16((uint16_t)(bx + 0x31ba)));
  dx = (uint16_t)(drd16((uint16_t)(bx + 0x31bc)));
  goto L20b8;
L2080:
  { uint32_t r_ = CALL_ORIG(18, 0x3854, FAR_CDECL, 4, (uint16_t)(fleet), (uint16_t)((fleet) >> 16), 0x2154, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L20ee;
  es = L16(-0x2);
  ax = (uint16_t)(rd16(es, si));
  L16(-0x1a) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x18));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf2)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x2)) << 8));
  L16(-0x18) = (uint16_t)(ax);
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x8)));
  dx = (uint16_t)(rd16(es, (uint16_t)(si + 0xa)));
L20b8:
  L16(-0x1e) = (uint16_t)(ax);
  L16(-0x1c) = (uint16_t)(dx);
  L8(-0x18) = (uint8_t)((uint8_t)(L8(-0x18) & 0xf0));
  L8(-0x17) = (uint8_t)((uint8_t)(L8(-0x17) | 0x10));
  ax = (uint16_t)(L16(-0x18));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0x4f)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x40)));
  L16(-0x18) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x1e));
  { uint32_t r_ = CALL_ORIG(18, 0x2664, FAR_CDECL, 4, (uint16_t)(fleet), (uint16_t)((fleet) >> 16), ax, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L20ee;
  ax = (uint16_t)(0xffff);
  FRAME_LEAVE();
  return ax;
L20ee:
  ax = (uint16_t)(0);
  FRAME_LEAVE();
  return ax;
}

/* ai0_turn: 17:0x0462 (Ghidra FUN_1080_0462, alias fn_17_0462), 4813 bytes. ENTER 0x92,0; PUSH DI; PUSH SI.
 * FAR CDECL (buf [BP+6], a near pointer to the 0x100-byte scratch buffer of 17:0x0000), no result. The turn of
 * the type-0 computer player: sets the thresholds DS:0x5340 to 0x5343 from the turn DS:0x82, asks 18:0x5d3c
 * for the best ship designs of each kind, builds new designs (17:0x1730), scores the planets (info array
 * DS:0x1600, +0x9 / +0xa / +0x1 / +0x3), walks the planet pointer array DS:0x1608 to queue colony and
 * production orders (18:0x2732, 9:0x2a74, 26:0x0100 / 0x0810) and then walks the fleets: for each fleet of
 * the player it picks the order for it (17:0x1b70, 17:0x20f4, 18:0x2db2, 18:0x2e90, 18:0x098a, 18:0x4510,
 * 18:0x3bb4, 18:0x2664, ...). Finishes with 18:0x59ba and 21:0x1e2a. All locals are BP_LOCAL slots: the
 * callees see their addresses and the diff compares the doubles' hash of them. */
DECOMP(17, 0x0462, ai0_turn, FAR_CDECL)
void ai0_turn(uint16_t buf)
{
  __attribute__((unused)) uint16_t ax = 0, bx = 0, cx = 0, di = 0, dx = 0, si = 0, ax_h = 0, cx_h = 0, dx_h = 0;
  __attribute__((unused)) uint16_t es = 0;
  __attribute__((unused)) uint16_t ds = MEM_SEL_DGROUP;
  uint16_t t1 = 0, t2 = 0, t3 = 0, t4 = 0, st0 = 0, t16 = 0, t17 = 0, t18 = 0, t19 = 0, t20 = 0, t21 = 0, t22 = 0, t23 = 0, t24 = 0, t25 = 0, t26 = 0, t27 = 0, t28 = 0, t29 = 0, t30 = 0, t31 = 0, t35 = 0, t37 = 0, t48 = 0, t50 = 0, t52 = 0, t66 = 0, t67 = 0, t73 = 0, t82 = 0, t84 = 0;
  uint32_t t13 = 0;
  FRAME_ENTER_REGS(0x92, 2);
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(drd8((uint16_t)(bx + 0x5a6b))) << 8));
  ax = (uint16_t)((uint16_t)(ax & 0x1c00));
  ax = (uint16_t)((uint16_t)((uint16_t)ax >> 10));
  L16(-0x2a) = (uint16_t)(ax);
  if ((uint16_t)(drd16(0x82)) < (uint16_t)(0xa)) goto L0488;
  ax = (uint16_t)(0xf);
  goto L048a;
L0488:
  ax = (uint16_t)(0);
L048a:
  t1 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x16));
  { uint32_t r_ = CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0x43d, SEG_SEL(17), 0x24, ax, t1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x86) = (uint16_t)(ax);
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x32)) goto L04c8;
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x6fc); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xc000); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L04c8:
  si = (uint16_t)(0x4);
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x82)) goto L04e5;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - 0x78));
  cx = (uint16_t)(0x14);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x04de); ax = a_; dx = d_; }
  si = (uint16_t)(ax);
  si = (uint16_t)((uint16_t)(si + 0x4));
L04e5:
  if ((int16_t)(si) <= (int16_t)(0x32)) goto L04ed;
  si = (uint16_t)(0x32);
L04ed:
  ax = (uint16_t)(si);
  dwr8(0x5340, (uint8_t)ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)(uint8_t)ax >> 1)));
  dwr8(0x5341, (uint8_t)ax);
  si = (uint16_t)(0x6);
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x73)) goto L0513;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - 0x64));
  cx = (uint16_t)(0x16);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x050c); ax = a_; dx = d_; }
  si = (uint16_t)(ax);
  si = (uint16_t)((uint16_t)(si + 0x6));
L0513:
  L16(-0xa) = (uint16_t)(si);
  if ((int16_t)(si) <= (int16_t)(0xc)) goto L0520;
  L16(-0xa) = (uint16_t)(0xc);
L0520:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0xa)));
  dwr8(0x5342, (uint8_t)ax);
  ax = (uint16_t)(L16(-0xa));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  ax = (uint16_t)((uint16_t)(ax - dx));
  ax = (uint16_t)((uint16_t)((int16_t)ax >> 1));
  ax = (uint16_t)((uint16_t)(ax - 1));
  if ((int16_t)(ax) <= (int16_t)(0x3)) goto L0537;
  ax = (uint16_t)(0x3);
L0537:
  dwr8(0x5343, (uint8_t)ax);
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x52));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  if ((uint16_t)(drd16(0x82)) >= (uint16_t)(0x78)) goto L0558;
  si = (uint16_t)(0x32);
  goto L0569;
L0558:
  if ((uint16_t)(drd16(0x82)) < (uint16_t)(0xc8)) goto L0566;
  si = (uint16_t)(0x64);
  goto L0569;
L0566:
  si = (uint16_t)(0x46);
L0569:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  t2 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x18));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xe, 0xf, si, ax, t2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(0xe);
  L16(-0x2) = (uint16_t)(0x49ab);
  L16(-0x4) = (uint16_t)(si);
  bx = (uint16_t)(L16(-0x2));
L058e:
  if ((uint8_t)(rd8(rt_ss, (uint16_t)(rt_bp + di - 0x52))) == (uint8_t)(0x0)) goto L05a4;
  if ((uint8_t)((uint8_t)(drd8((uint16_t)(bx + 0x1))) & (uint8_t)(0x2)) != 0) goto L05a4;
  if ((uint16_t)(drd16((uint16_t)(bx - 0x7b))) != (uint16_t)(0x1d)) goto L05a4;
  wr8(rt_ss, (uint16_t)(rt_bp + di - 0x52), 0x0);
L05a4:
  di = (uint16_t)((uint16_t)(di + 1));
  bx = (uint16_t)((uint16_t)(bx + 0x8d));
  if ((uint16_t)(bx) <= (uint16_t)(0x4a38)) goto L058e;
  si = (uint16_t)(L16(-0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1a));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xb, 0xd, si, cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x34) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1c));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x9, 0xa, si, cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1e));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x2, 0x5, si, cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  t3 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x20));
  t4 = ax;
  ax = (uint16_t)(si);
  ax = (uint16_t)((uint16_t)(ax + si));
  ax = (uint16_t)((uint16_t)(ax + si));
  ax = (uint16_t)((uint16_t)((uint16_t)ax >> 1));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x6, 0x7, ax, t4, t3); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x50)) goto L0696;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  { uint32_t r_ = CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x10));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  L8(-0x10) = (uint8_t)(0x2);
  cx = (uint16_t)((uint16_t)(rt_bp - 0x10));
  { uint32_t r_ = CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, cx); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x10));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  L8(-0xf) = (uint8_t)(0x2);
  cx = (uint16_t)((uint16_t)(rt_bp - 0x10));
  { uint32_t r_ = CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, cx); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x10));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(0x2));
  L8(-0x3) = (uint8_t)((uint8_t)ax);
  L8(-0x4) = (uint8_t)((uint8_t)ax);
  L8(-0x5) = (uint8_t)((uint8_t)ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x10));
  { uint32_t r_ = CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L0696:
  { const uint16_t a_[] = {0}; uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(17), 0x1730), FAR_CDECL, a_, 0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(0);
  L16(-0x42) = (uint16_t)(di);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(di)) goto L06f3;
  ax = (uint16_t)(drd16(0xba));
  dx = (uint16_t)(drd16(0xbc));
  cx = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
L06b1:
  si = (uint16_t)(cx);
  es = L16(-0x2);
  ax = (uint16_t)(rd16(es, si));
  dx = (uint16_t)(rd16(es, (uint16_t)(si + 0x2)));
  bx = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L06f3;
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) != (uint16_t)(ax)) goto L06e9;
  if ((uint16_t)(L16(-0x18)) == (uint16_t)(0xffff)) goto L06e9;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x28))) != (uint16_t)(0x0)) goto L06e6;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2a))) == (uint16_t)(0x0)) goto L06e9;
L06e6:
  L16(-0x42) = (uint16_t)((uint16_t)(L16(-0x42) + 1));
L06e9:
  cx = (uint16_t)((uint16_t)(cx + 0x4));
  di = (uint16_t)((uint16_t)(di + 1));
  if ((int16_t)(di) < (int16_t)(drd16(0x553a))) goto L06b1;
L06f3:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x22));
  { uint32_t r_ = CALL_ORIG(18, 0x2ba6, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x32) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)((uint16_t)drd16(0x4d96) * (uint16_t)0x38));
  ax = (uint16_t)((uint16_t)(ax + drd16(0xb6)));
  dx = (uint16_t)(drd16(0xb8));
  L16(-0x2) = (uint16_t)(dx);
  cx = (uint16_t)(drd16(0xb6));
  L16(-0x26) = (uint16_t)(cx);
  L16(-0x24) = (uint16_t)(dx);
  if ((uint16_t)(ax) <= (uint16_t)(cx)) goto L078e;
  L16(-0x4) = (uint16_t)(ax);
  si = (uint16_t)(cx);
  ds = dx;
L0732:
  ax = (uint16_t)(rd16(rt_ss, 0x14c));
  if ((uint16_t)(rd16(ds, (uint16_t)(si + 0x2))) == (uint16_t)(ax)) goto L077f;
  if ((uint16_t)(rd16(ds, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L077f;
  ax = (uint16_t)(rd16(ds, (uint16_t)(si + 0x12)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  cx = (uint16_t)(0xfa);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x074c); ax = a_; dx = d_; }
  di = (uint16_t)(ax);
  di = (uint16_t)((uint16_t)(di + 1));
  if ((int16_t)(di) <= (int16_t)(0x6)) goto L0759;
  di = (uint16_t)(0x6);
L0759:
  if ((uint8_t)((uint8_t)(rd8(ds, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) == 0) goto L0760;
  di = (uint16_t)((uint16_t)(di + 1));
L0760:
  ax = (uint16_t)(di);
  di = (uint16_t)(rd16(ds, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = rd16(rt_ss, 0x1600);
  es = rd16(rt_ss, 0x1602);
  wr8(es, (uint16_t)(bx + di + 0xa), (uint8_t)ax);
  di = (uint16_t)(rd16(ds, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = rd16(rt_ss, 0x1600);
  es = rd16(rt_ss, 0x1602);
  wr8(es, (uint16_t)(bx + di + 0x9), 0x1);
L077f:
  si = (uint16_t)((uint16_t)(si + 0x38));
  ax = (uint16_t)(si);
  if ((uint16_t)(ax) < (uint16_t)(L16(-0x4))) goto L0732;
  L16(-0x26) = (uint16_t)(si);
  /* ds = ss */
L078e:
  L16(-0x30) = (uint16_t)(0x0);
  if ((int16_t)(drd16(0x4b62)) <= (int16_t)(0x0)) goto L0d29;
  L16(-0x28) = (uint16_t)(0x0);
L07a1:
  bx = drd16(0x1608);
  es = drd16(0x160a);
  si = (uint16_t)(L16(-0x28));
  SET32(ax, ax_h, rd32(es, (uint16_t)(bx + si)));
  L32(-0x26) = (uint32_t)(E32(ax, ax_h));
  SET32(ax, ax_h, (uint32_t)(E32(ax, ax_h) | E32(ax, ax_h)));
  if ((uint32_t)(E32(ax, ax_h)) == 0) goto L0d29;
  bx = L16(-0x26);
  es = L16(-0x24);
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + 0x5))) & (uint8_t)(0x2)) == 0) goto L0d18;
  if ((int32_t)(rd32(es, (uint16_t)(bx + 0x28))) < (int32_t)(0xc8)) goto L0d18;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x1, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(26), 0x100), FAR_CDECL, (const uint16_t[]){buf}, 1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)(0);
  L16(-0x14) = (uint16_t)(ax);
  L16(-0xc) = (uint16_t)(ax);
  SET32(ax, ax_h, drd32(0xcd2));
  ax = (uint16_t)((uint16_t)(ax + 0x4));
  L32(-0x4) = (uint32_t)(E32(ax, ax_h));
  bx = drd16(0xcd2);
  es = drd16(0xcd4);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(rd8(es, (uint16_t)(bx + 0x3))));
  L8(-0x88) = (uint8_t)((uint8_t)ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | (uint8_t)ax)));
  if ((uint8_t)((uint8_t)ax) != 0) goto L081e;
  cx = (uint16_t)(L16(-0xc));
L080e:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x88)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) <= (int16_t)(cx)) goto L0856;
  st0 = 0x0;
  goto L0d10;
L081e:
  si = (uint16_t)(L16(-0x4));
  cx = (uint16_t)(L16(-0xc));
  ds = L16(-0x2);
L0827:
  ax = (uint16_t)(rd16(ds, (uint16_t)(si + 0x2)));
  ax = (uint16_t)((uint16_t)(ax & 0xe));
  if ((uint16_t)(ax) != (uint16_t)(0x4)) goto L0843;
  SET32(ax, ax_h, rd32(ds, si));
  SET32(ax, ax_h, (uint32_t)(E32(ax, ax_h) & 0x1fc00));
  if ((uint32_t)(E32(ax, ax_h)) < (uint32_t)(0x4000)) goto L0851;
L0843:
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x88)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(ax) > (int16_t)(cx)) goto L0827;
L0851:
  /* ds = ss */
  goto L080e;
L0856:
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  ax = (uint16_t)(drd16((uint16_t)(bx + 0x5a66)));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  dx = (uint16_t)((uint16_t)(dx & 0x7));
  ax = (uint16_t)((uint16_t)(ax + dx));
  ax = (uint16_t)((uint16_t)((int16_t)ax >> 3));
  bx = (uint16_t)(ax);
  si = drd16(0x1604);
  es = drd16(0x1606);
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x2)));
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 2));
  if ((int16_t)(bx) >= (int16_t)(ax)) goto L087c;
  bx = (uint16_t)(ax);
L087c:
  if ((uint16_t)(L16(-0x1a)) == (uint16_t)(0xffff)) goto L08bd;
  ax = (uint16_t)(bx);
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 3));
  cx = (uint16_t)(0xa);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x088b); ax = a_; dx = d_; }
  if ((int16_t)(ax) > (int16_t)(L16(-0x34))) goto L08a5;
  if ((int16_t)(L16(-0x34)) >= (int16_t)(bx)) goto L08bd;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x3); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L08bd;
L08a5:
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x1a), 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x14) = (uint16_t)(0x1);
L08bd:
  if ((uint16_t)(L16(-0x32)) != (uint16_t)(0x0)) goto L08e5;
  if ((int16_t)(L16(-0x22)) > (int16_t)(0x19)) goto L09a8;
  bx = drd16(0x1604);
  es = drd16(0x1606);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x2)));
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 3));
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L09a8;
L08e5:
  if ((uint16_t)(drd16(0x82)) < (uint16_t)(0x5)) goto L09a8;
  if ((uint16_t)(drd16(0x82)) > (uint16_t)(0x14)) goto L0909;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L0909:
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x14) = (uint16_t)(0x1);
  bx = L16(-0x26);
  es = L16(-0x24);
  t13 = rd32(es, (uint16_t)(bx + 0x28));
  { uint32_t r_ = CALL_ORIG(28, 0x41d4, FAR_CDECL, 1, drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  SET32(ax, ax_h, (((uint32_t)(dx) << 16) | (uint16_t)(ax)));
  SET32(cx, cx_h, t13);
  { uint32_t a_ = E32(ax, ax_h), d_; imul32_(&a_, &d_, E32(cx, cx_h)); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  L32(-0x4) = (uint32_t)(E32(ax, ax_h));
  { uint32_t r_ = CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, (uint16_t)(L32(-0x26)), (uint16_t)((L32(-0x26)) >> 16), drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(ax);
  if ((int32_t)(L32(-0x4)) <= (int32_t)(0x8fc)) goto L09a8;
  if ((int16_t)(di) <= (int16_t)(0x23)) goto L09a8;
  if ((int16_t)(L16(-0x2a)) <= (int16_t)(0x0)) goto L09a8;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int32_t)(L32(-0x4)) <= (int32_t)(0xe10)) goto L09a8;
  if ((int16_t)(di) <= (int16_t)(0x32)) goto L09a8;
  if ((int16_t)(L16(-0x2a)) <= (int16_t)(0x1)) goto L09a8;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L09a8:
  if ((uint16_t)(drd16(0x417a)) != (uint16_t)(0x5)) goto L0a66;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0a66;
  bx = L16(-0x26);
  es = L16(-0x24);
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x2) = (uint16_t)(ax);
  cx = (uint16_t)(0);
  L16(-0x8) = (uint16_t)(cx);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(cx)) goto L0a23;
  ax = (uint16_t)(drd16(0xba));
  dx = (uint16_t)(drd16(0xbc));
  di = (uint16_t)(ax);
  L16(-0x4) = (uint16_t)(dx);
L09e1:
  es = L16(-0x4);
  ax = (uint16_t)(rd16(es, di));
  dx = (uint16_t)(rd16(es, (uint16_t)(di + 0x2)));
  bx = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L0a23;
  ax = (uint16_t)(L16(-0x2));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) != (uint16_t)(ax)) goto L0a10;
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0xc))) <= (int16_t)(0x0)) goto L0a10;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(ax)) goto L0a1c;
L0a10:
  di = (uint16_t)((uint16_t)(di + 0x4));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(cx) < (int16_t)(drd16(0x553a))) goto L09e1;
  goto L0a23;
L0a1c:
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xc)));
  L16(-0x8) = (uint16_t)(ax);
L0a23:
  if ((int16_t)(L16(-0x8)) < (int16_t)(0xa)) goto L0a3d;
  if ((int16_t)(L16(-0x8)) >= (int16_t)(0x11)) goto L0a66;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0a66;
L0a3d:
  ax = (uint16_t)(L16(-0x8));
  ax = (uint16_t)((uint16_t)(ax + ax));
  ax = (uint16_t)((uint16_t)(ax + 1));
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0a66;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, ax, 0x4, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x14) = (uint16_t)(0x1);
L0a66:
  cx = (uint16_t)(0);
  ax = (uint16_t)(L16(-0x26));
  dx = (uint16_t)(L16(-0x24));
  ax = (uint16_t)((uint16_t)(ax + 0x1c));
  bx = (uint16_t)(ax);
  ds = dx;
  es = dx;
L0a77:
  if ((int32_t)(rd32(ds, bx)) < (int32_t)(0x1388)) goto L0a89;
  bx = (uint16_t)((uint16_t)(bx + 0x4));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(cx) <= (int16_t)(0x2)) goto L0a77;
L0a89:
  /* ds = ss */
  if ((uint16_t)(cx) != (uint16_t)(0x2)) goto L0a96;
  ax = (uint16_t)(0x1);
  goto L0a98;
L0a96:
  ax = (uint16_t)(0);
L0a98:
  L16(-0x6) = (uint16_t)(ax);
  if ((uint16_t)(L16(-0x1c)) == (uint16_t)(0xffff)) goto L0afe;
  bx = (uint16_t)(L16(-0x26));
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x2) = (uint16_t)(ax);
  L16(-0xe) = (uint16_t)(0x0);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(0x0)) goto L0afe;
  di = (uint16_t)(0);
L0ab8:
  bx = drd16(0xba);
  es = drd16(0xbc);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + di)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + di + 0x2)));
  si = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L0afe;
  ax = (uint16_t)(L16(-0x2));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x6))) != (uint16_t)(ax)) goto L0af0;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L0af0;
  ax = fleet_meets_ship_threshold(MK_FP(es, si), 0x2);
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0b50;
L0af0:
  di = (uint16_t)((uint16_t)(di + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0xe) = (uint16_t)((uint16_t)(L16(-0xe) + 1));
  if ((int16_t)(L16(-0xe)) < (int16_t)(ax)) goto L0ab8;
L0afe:
  if ((uint16_t)(L16(-0x1e)) == (uint16_t)(0xffff)) goto L0c49;
  ax = (uint16_t)(drd16(0x7a));
  cx = (uint16_t)(0x7);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x0b0d); ax = a_; dx = d_; }
  ax = (uint16_t)((uint16_t)(ax + 0x6));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x1e) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L0b37;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L0b27;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) < (uint16_t)(ax)) goto L0b37;
L0b27:
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L0c49;
L0b37:
  if ((uint16_t)(L16(-0x20)) == (uint16_t)(0xffff)) goto L0b90;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0b90;
  di = (uint16_t)(L16(-0x20));
  goto L0b93;
L0b50:
  if ((uint16_t)(L16(-0x1c)) == (uint16_t)(0xffff)) goto L0afe;
  es = L16(-0x10);
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x1e)));
  ax = (uint16_t)((uint16_t)(ax + rd16(es, (uint16_t)(si + 0x20))));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)(drd8(0x5342)));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) >= (int16_t)(cx)) goto L0afe;
  t16 = ((uint16_t)(L16(-0x6)) < (uint16_t)(0x1)) ? 1 : 0;
  t17 = ax; t18 = ax;
  ax = (uint16_t)((uint16_t)(t17 - t18 - t16));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xfe)));
  ax = (uint16_t)((uint16_t)(ax + 0x6));
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x1c), ax, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x14) = (uint16_t)(0x1);
  goto L0d0d;
L0b90:
  di = (uint16_t)(L16(-0x1e));
L0b93:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, (uint16_t)(L32(-0x26)), (uint16_t)((L32(-0x26)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, (uint16_t)(L32(-0x26)), (uint16_t)((L32(-0x26)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x2) = (uint16_t)(0x0);
  bx = (uint16_t)(L16(-0x2));
  cx = (uint16_t)(L16(-0xc));
  L16(-0x4) = (uint16_t)(di);
L0bc1:
  si = (uint16_t)(bx);
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L0d0d;
  bx = (uint16_t)((uint16_t)(bx + 0x4));
  if ((int16_t)(bx) < (int16_t)(0x10)) goto L0bc1;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x40));
  cx = (uint16_t)((uint16_t)((uint16_t)di * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)((uint16_t)(rt_bp - 0x40));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  L16(-0x2) = (uint16_t)(ax);
  bx = (uint16_t)(ax);
L0c00:
  ax = (uint16_t)(drd16(si));
  cx = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(ax + ax));
  ax = (uint16_t)((uint16_t)(ax + cx));
  cx = (uint16_t)(0x5);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x0c0d); ax = a_; dx = d_; }
  dx = (uint16_t)(0);
  t19 = drd16(bx); t20 = ax;
  dwr16(bx, (uint16_t)(t19 - t20));
  t21 = ((uint16_t)(t19) < (uint16_t)(t20)) ? 1 : 0;
  t22 = drd16((uint16_t)(bx + 0x2)); t23 = dx;
  dwr16((uint16_t)(bx + 0x2), (uint16_t)(t22 - t23 - t21));
  if ((int16_t)(drd16((uint16_t)(bx + 0x2))) < (int16_t)(dx)) goto L0c49;
  si = (uint16_t)((uint16_t)(si + 0x2));
  bx = (uint16_t)((uint16_t)(bx + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(bx) < (uint16_t)(ax)) goto L0c00;
  t24 = ((uint16_t)(L16(-0x6)) < (uint16_t)(0x1)) ? 1 : 0;
  t25 = ax; t26 = ax;
  ax = (uint16_t)((uint16_t)(t25 - t26 - t24));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xfc)));
  ax = (uint16_t)((uint16_t)(ax + cx));
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x4), ax, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x14) = (uint16_t)(0x1);
L0c49:
  if ((uint16_t)(L16(-0x18)) == (uint16_t)(0xffff)) goto L0d0d;
  ax = (uint16_t)(drd16(0x7a));
  cx = (uint16_t)(0xc);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x0c58); ax = a_; dx = d_; }
  ax = (uint16_t)((uint16_t)(ax + 0x8));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x18) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L0d0d;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L0c76;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) >= (uint16_t)(ax)) goto L0d0d;
L0c76:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, (uint16_t)(L32(-0x26)), (uint16_t)((L32(-0x26)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, (uint16_t)(L32(-0x26)), (uint16_t)((L32(-0x26)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(0);
L0c98:
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L0d0d;
  si = (uint16_t)((uint16_t)(si + 0x4));
  if ((int16_t)(si) < (int16_t)(0x10)) goto L0c98;
  L16(-0xc) = (uint16_t)(0x0);
L0cb3:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x40));
  cx = (uint16_t)((uint16_t)((uint16_t)L16(-0x18) * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)((uint16_t)(rt_bp - 0x40));
  si = (uint16_t)((uint16_t)(rt_bp - 0x62));
L0cd4:
  ax = (uint16_t)(drd16(di));
  dx = (uint16_t)(0);
  t27 = drd16(si); t28 = ax;
  dwr16(si, (uint16_t)(t27 - t28));
  t29 = ((uint16_t)(t27) < (uint16_t)(t28)) ? 1 : 0;
  t30 = drd16((uint16_t)(si + 0x2)); t31 = dx;
  dwr16((uint16_t)(si + 0x2), (uint16_t)(t30 - t31 - t29));
  if ((int16_t)(drd16((uint16_t)(si + 0x2))) < (int16_t)(dx)) goto L0d0d;
  di = (uint16_t)((uint16_t)(di + 0x2));
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L0cd4;
  ax = (uint16_t)(0x1);
  L16(-0x14) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x18), ax, 0x2, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xc) = (uint16_t)((uint16_t)(L16(-0xc) + 1));
  if ((int16_t)(L16(-0xc)) < (int16_t)(0x5)) goto L0cb3;
L0d0d:
  st0 = L16(-0x14);
L0d10:
  { uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(26), 0x810), FAR_CDECL, (const uint16_t[]){st0}, 1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L0d18:
  L16(-0x28) = (uint16_t)((uint16_t)(L16(-0x28) + 0x4));
  ax = (uint16_t)(drd16(0x4b62));
  L16(-0x30) = (uint16_t)((uint16_t)(L16(-0x30) + 1));
  if ((int16_t)(L16(-0x30)) < (int16_t)(ax)) goto L07a1;
L0d29:
  { uint32_t r_ = CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  SET32(ax, ax_h, 0);
  L32(-0x34) = (uint32_t)(E32(ax, ax_h));
  L32(-0x38) = (uint32_t)(E32(ax, ax_h));
  L16(-0xe) = (uint16_t)(ax);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(ax)) goto L133b;
  L16(-0x6) = (uint16_t)(ax);
L0d4d:
  bx = drd16(0xba);
  es = drd16(0xbc);
  si = (uint16_t)(L16(-0x6));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + si)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + si + 0x2)));
  di = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L133b;
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) <= (int16_t)(0x1)) goto L0e0e;
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x8)) goto L0e0e;
  es = L16(-0x10);
  ax = (uint16_t)(rd16(es, (uint16_t)(di + 0x8)));
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((uint16_t)(ax - rd16(es, (uint16_t)(bx + 0x16))));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  L16(-0x8c) = (uint16_t)(bx);
  L16(-0x8a) = (uint16_t)(es);
  t35 = ax;
  SET32(ax, ax_h, (((uint32_t)(dx) << 16) | (uint16_t)(ax)));
  SET32(cx, cx_h, (((uint32_t)(dx) << 16) | (uint16_t)(t35)));
  { uint32_t a_ = E32(ax, ax_h), d_; imul32_(&a_, &d_, E32(cx, cx_h)); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(dx, dx_h, E32(ax, ax_h));
  SET32(dx, dx_h, (uint32_t)((uint32_t)E32(dx, dx_h) >> 16));
  es = L16(-0x10);
  cx = (uint16_t)(ax);
  ax = (uint16_t)(rd16(es, (uint16_t)(di + 0xa)));
  bx = L16(-0x8c);
  es = L16(-0x8a);
  ax = (uint16_t)((uint16_t)(ax - rd16(es, (uint16_t)(bx + 0x18))));
  bx = (uint16_t)(dx);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  L16(-0x90) = (uint16_t)(cx);
  L16(-0x8e) = (uint16_t)(bx);
  t37 = ax;
  SET32(ax, ax_h, (((uint32_t)(dx) << 16) | (uint16_t)(ax)));
  SET32(cx, cx_h, (((uint32_t)(dx) << 16) | (uint16_t)(t37)));
  { uint32_t a_ = E32(ax, ax_h), d_; imul32_(&a_, &d_, E32(cx, cx_h)); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  SET32(ax, ax_h, (uint32_t)(E32(ax, ax_h) + L32(-0x90)));
  if ((int32_t)(E32(ax, ax_h)) <= (int32_t)(0x9c40)) goto L0e0e;
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dwr16(0x4bee, 0x1);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr8(es, (uint16_t)(bx + 0x3), 0x1);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L0e0e:
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x2))) == (uint16_t)(ax)) goto L0e2c;
  SET32(ax, ax_h, L32(-0x38));
  wr32(es, (uint16_t)(di + 0x68), E32(ax, ax_h));
  L16(-0x38) = (uint16_t)(di);
  L16(-0x36) = (uint16_t)(es);
  goto L132a;
L0e2c:
  if ((int16_t)(rd16(es, (uint16_t)(di + 0xc))) <= (int16_t)(0x0)) goto L0f32;
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x28)) goto L0f32;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x62))) != (uint16_t)(0x1)) goto L0f32;
  if ((int16_t)(rd16(es, (uint16_t)(di + 0xc))) < (int16_t)(0x7)) goto L0eda;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x5); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L0eda;
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(18, 0x3ad8, FAR_CDECL, 4, (uint16_t)(rd32(es, (uint16_t)(di + 0x8))), (uint16_t)((rd32(es, (uint16_t)(di + 0x8))) >> 16), 0x69, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  if ((uint16_t)(si) == (uint16_t)(0xffff)) goto L0eda;
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x6))) == (uint16_t)(ax)) goto L0eda;
  { uint32_t r_ = CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, di, es, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x80) = (uint16_t)(si);
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(ax);
  bx = (uint16_t)(si);
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(ax, ax_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(ax, ax_h));
  L8(-0x7e) = (uint8_t)((uint8_t)(L8(-0x7e) & 0xf0));
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0x4f)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x40)));
  L16(-0x7e) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x84));
  { uint32_t r_ = CALL_ORIG(18, 0x2664, FAR_CDECL, 4, di, L16(-0x10), ax, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x12) = (uint16_t)(di);
  goto L10cf;
L0eda:
  L16(-0x12) = (uint16_t)(di);
  es = L16(-0x10);
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + 0xa))) & (uint8_t)(0xf)) != 0) goto L10cf;
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xa)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf6)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x6)));
  wr16(es, (uint16_t)(bx + 0xa), ax);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr16(es, (uint16_t)(bx + 0xc), 0x5);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr16(es, (uint16_t)(bx + 0xe), 0x5);
L0f21:
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L132a;
L0f32:
  { uint32_t r_ = CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, di, es); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L100c;
  SET32(ax, ax_h, L32(-0x34));
  es = L16(-0x10);
  wr32(es, (uint16_t)(di + 0x68), E32(ax, ax_h));
  L16(-0x34) = (uint16_t)(di);
  L16(-0x32) = (uint16_t)(es);
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(rd8(es, (uint16_t)(bx + 0xa))));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf)));
  if ((uint8_t)((uint8_t)ax) != (uint8_t)(0x6)) goto L0f70;
  { uint32_t r_ = CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, di, L16(-0x10), 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L0f70:
  cx = (uint16_t)(0x2);
  ax = (uint16_t)((uint16_t)(di + 0x10));
  dx = (uint16_t)(L16(-0x10));
  bx = (uint16_t)(ax);
  ds = dx;
  L16(-0x12) = (uint16_t)(di);
L0f80:
  if ((int16_t)(rd16(ds, bx)) > (int16_t)(0x0)) goto L0f8e;
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(cx) <= (int16_t)(0x7)) goto L0f80;
L0f8e:
  /* ds = ss */
  if ((int16_t)(cx) > (int16_t)(0x7)) goto L10cf;
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) <= (int16_t)(0x1)) goto L0fb1;
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) == (uint8_t)(0x1)) goto L0fbd;
L0fb1:
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x6))) == (uint16_t)(0xffff)) goto L10cf;
L0fbd:
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) <= (int16_t)(0x1)) goto L0fe4;
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L0fe4;
  es = L16(-0x10);
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  bx = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1a)));
  goto L0feb;
L0fe4:
  es = L16(-0x10);
  bx = (uint16_t)(rd16(es, (uint16_t)(di + 0x6)));
L0feb:
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 4));
  es = drd16(0x1602);
  bx = (uint16_t)((uint16_t)(bx + drd16(0x1600)));
  bx = (uint16_t)((uint16_t)(bx + 0xa));
  L16(-0x2) = (uint16_t)(es);
  if ((uint8_t)(rd8(es, bx)) == (uint8_t)(0x0)) goto L10cf;
  wr8(es, bx, (uint8_t)(rd8(es, bx) | 0x80));
  goto L10cf;
L100c:
  L16(-0x12) = (uint16_t)(di);
  { uint32_t r_ = CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, di, L16(-0x10)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L10cf;
  L16(-0x2) = (uint16_t)(0xffff);
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L105c;
  t48 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t48;
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + 0xa))) & (uint8_t)(0xf)) != 0) goto L105c;
  bx = L16(-0x12);
  es = L16(-0x10);
  t50 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t50;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L1066;
  bx = L16(-0x12);
  es = L16(-0x10);
  t52 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t52;
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1a)));
  goto L1063;
L105c:
  bx = L16(-0x12);
  es = L16(-0x10);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x6)));
L1063:
  L16(-0x2) = (uint16_t)(ax);
L1066:
  if ((uint16_t)(L16(-0x2)) == (uint16_t)(0xffff)) goto L10cf;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, L16(-0x2)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x26) = (uint16_t)(ax);
  L16(-0x24) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L108d;
  ax = (uint16_t)(drd16(0x14c));
  bx = L16(-0x26);
  es = L16(-0x24);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(ax)) goto L10cf;
L108d:
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((uint32_t)(rd32(es, (uint16_t)(bx + 0x58))) != (uint32_t)(0x0)) goto L10cf;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dwr16(0x4bee, 0x1);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr8(es, (uint16_t)(bx + 0x3), 0x1);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L10cf:
  if ((uint16_t)(drd16(0x82)) > (uint16_t)(0x14)) goto L1100;
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0xc))) <= (int16_t)(0x0)) goto L1100;
L10e0:
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xa)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf5)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x5)));
  wr16(es, (uint16_t)(bx + 0xa), ax);
  goto L0f21;
L1100:
  es = L16(-0x10);
  bx = (uint16_t)(L16(-0x12));
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) > (int16_t)(0x1)) goto L132a;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0xe))) == (uint16_t)(0x0)) goto L132a;
  if ((uint16_t)(drd16(0x82)) >= (uint16_t)(0x5)) goto L1128;
  if ((uint16_t)(drd16(0x7c)) != (uint16_t)(0x0)) goto L132a;
L1128:
  if ((int16_t)(L16(-0x2a)) <= (int16_t)(0x1)) goto L1294;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L1294;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(bx + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L1294;
  bx = L16(-0x4);
  es = L16(-0x2);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(0xffff)) goto L1294;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(ax)) goto L1294;
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((int32_t)(rd32(es, (uint16_t)(bx + 0x58))) <= (int32_t)(0x0)) goto L1294;
  bx = L16(-0x4);
  es = L16(-0x2);
  ax = (uint16_t)((uint16_t)((uint16_t)rd16(es, (uint16_t)(bx + 0x2)) * (uint16_t)0xc0));
  ax = (uint16_t)((uint16_t)(ax + 0x5a64));
  { uint32_t r_ = CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, ax, 0xe); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((uint16_t)(ax) == (uint16_t)(0x8)) goto L1294;
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x84));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  wr16(es, di, ax); di = (uint16_t)(di + 2);
  bx = L16(-0x4);
  es = L16(-0x2);
  si = (uint16_t)(rd16(es, bx));
  si = (uint16_t)((uint16_t)((uint16_t)si << 2));
  SET32(ax, ax_h, drd32((uint16_t)(si + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(ax, ax_h));
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x80) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(ax);
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf1)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x1)));
  L16(-0x7e) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x76));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x20)) << 8));
  L16(-0x76) = (uint16_t)(ax);
  bx = L16(-0x12);
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  di = (uint16_t)((uint16_t)(bx + 0x4));
  si = (uint16_t)((uint16_t)(rt_bp - 0x84));
  SET32(cx, cx_h, 0x4);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr16(es, di, rd16(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 2); si = (uint16_t)(si + 2);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, (uint16_t)(L32(-0x4)), (uint16_t)((L32(-0x4)) >> 16), 0x3be2, SEG_SEL(18)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L132a;
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x84));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  wr16(es, di, ax); di = (uint16_t)(di + 2);
  bx = L16(-0x4);
  es = L16(-0x2);
  cx = (uint16_t)(rd16(es, bx));
  L16(-0x80) = (uint16_t)(cx);
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) & 0xf1)) << 8));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(cx);
  bx = (uint16_t)(rd16(es, bx));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(cx, cx_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(cx, cx_h));
  L8(-0x7e) = (uint8_t)((uint8_t)(L8(-0x7e) & 0xf0));
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx & 0x4f)));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx | 0x40)));
  L16(-0x7e) = (uint16_t)(cx);
  t66 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x84));
  { uint32_t r_ = CALL_ORIG(18, 0x2664, FAR_CDECL, 4, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), ax, t66); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L1327;
L1294:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x2e));
  { uint32_t r_ = CALL_ORIG(18, 0x98a, FAR_CDECL, 3, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x2) = (uint16_t)(ax);
  t67 = ax;
  ax = (uint16_t)((uint16_t)(t67 + 1));
  if ((uint16_t)(ax) != 0) goto L12be;
  if ((uint32_t)(L32(-0x2e)) != (uint32_t)(0x0)) goto L12be;
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L132a;
  goto L10e0;
L12be:
  bx = L16(-0x12);
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L1302;
  { uint32_t r_ = CALL_ORIG(18, 0x750, FAR_CDECL, 6, 0x1, rd16(es, (uint16_t)(bx + 0x6)), 0x2, rd16(es, bx), 0x3, 0xa); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0x12);
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1302:
  if ((uint16_t)(L16(-0x2)) == (uint16_t)(0xffff)) goto L131a;
  { uint32_t r_ = CALL_ORIG(18, 0x864, FAR_CDECL, 3, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), L16(-0x2)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L132a;
L131a:
  { uint32_t r_ = CALL_ORIG(18, 0x8fe, FAR_CDECL, 4, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), (uint16_t)(L32(-0x2e)), (uint16_t)((L32(-0x2e)) >> 16)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1327:
L132a:
  L16(-0x6) = (uint16_t)((uint16_t)(L16(-0x6) + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0xe) = (uint16_t)((uint16_t)(L16(-0xe) + 1));
  if ((int16_t)(L16(-0xe)) < (int16_t)(ax)) goto L0d4d;
L133b:
  cx = (uint16_t)(0);
  if ((int16_t)(drd16(0x4b62)) <= (int16_t)(cx)) goto L1358;
  bx = drd16(0x1608);
  es = drd16(0x160a);
L1347:
  t73 = rd16(es, (uint16_t)(bx + 0x2));
  si = rd16(es, bx);
  ds = t73;
  ax = (uint16_t)(ds);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L1371;
  if ((uint8_t)((uint8_t)(rd8(ds, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) == 0) goto L1366;
  goto L1371;
L1358:
  si = (uint16_t)(L16(-0x26));
L135b:
  if ((uint16_t)(drd16(0x4b62)) != (uint16_t)(cx)) goto L1378;
  ax = (uint16_t)(0);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  goto L137d;
L1366:
  bx = (uint16_t)((uint16_t)(bx + 0x4));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(rd16(rt_ss, 0x4b62)) > (int16_t)(cx)) goto L1347;
L1371:
  L16(-0x24) = (uint16_t)(ds);
  /* ds = ss */
  goto L135b;
L1378:
  ax = (uint16_t)(si);
  dx = (uint16_t)(L16(-0x24));
L137d:
  L16(-0x2a) = (uint16_t)(ax);
  L16(-0x28) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L14e8;
  L16(-0xe) = (uint16_t)(0x0);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(0x0)) goto L14dd;
  L16(-0x14) = (uint16_t)(0x0);
L139c:
  bx = drd16(0xba);
  es = drd16(0xbc);
  si = (uint16_t)(L16(-0x14));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + si)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + si + 0x2)));
  di = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L14dd;
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x2))) != (uint16_t)(ax)) goto L14cc;
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) > (int16_t)(0x1)) goto L14cc;
  { uint32_t r_ = CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, di, es); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L14cc;
  es = L16(-0x10);
  if ((uint8_t)(rd8(es, (uint16_t)(di + 0x60))) == (uint8_t)(0x4)) goto L1405;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dwr8(0x4bec, 0x4);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1405:
  L16(-0x12) = (uint16_t)(di);
  SET32(ax, ax_h, 0);
  L32(-0x26) = (uint32_t)(E32(ax, ax_h));
  L16(-0xc) = (uint16_t)(ax);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) <= (int16_t)(ax)) goto L1472;
  ax = (uint16_t)(bx);
  ax = (uint16_t)((uint16_t)(ax + 0x6));
  L16(-0x8) = (uint16_t)(ax);
  L16(-0x6) = (uint16_t)(es);
  di = (uint16_t)(ax);
L1429:
  cx = (uint16_t)(0);
  es = L16(-0x6);
  ax = (uint16_t)(rd16(es, di));
  L16(-0x92) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((int16_t)(ax) <= 0) goto L1459;
  ax = (uint16_t)((uint16_t)(di + 0x2));
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(es);
  L16(-0x8) = (uint16_t)(di);
  bx = (uint16_t)(ax);
  ds = L16(-0x10);
L144a:
  es = L16(-0x2);
  ax = (uint16_t)(rd16(es, bx));
  si = (uint16_t)(L16(-0x12));
  if ((uint16_t)(rd16(ds, si)) != (uint16_t)(ax)) goto L14a8;
L1457:
  /* ds = ss */
L1459:
  if ((int16_t)(L16(-0x92)) > (int16_t)(cx)) goto L1472;
  di = (uint16_t)((uint16_t)(di + 0x14));
  L16(-0xc) = (uint16_t)((uint16_t)(L16(-0xc) + 1));
  ax = (uint16_t)(L16(-0xc));
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) > (int16_t)(ax)) goto L1429;
L1472:
  si = (uint16_t)(L16(-0x26));
  ax = (uint16_t)(L16(-0xc));
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) <= (int16_t)(ax)) goto L1496;
  di = (uint16_t)((uint16_t)((uint16_t)ax * (uint16_t)0x14));
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(bx + di + 0x4))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x24) = (uint16_t)(dx);
L1496:
  ax = (uint16_t)(L16(-0x24));
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L14b4;
  ax = (uint16_t)(L16(-0x24));
  L16(-0x4) = (uint16_t)(si);
  L16(-0x2) = (uint16_t)(ax);
  goto L14bc;
L14a8:
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(L16(-0x92)) > (int16_t)(cx)) goto L144a;
  goto L1457;
L14b4:
  SET32(ax, ax_h, L32(-0x2a));
  L32(-0x4) = (uint32_t)(E32(ax, ax_h));
L14bc:
  { uint32_t r_ = CALL_ORIG(18, 0x19c4, FAR_CDECL, 4, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), (uint16_t)(L32(-0x4)), (uint16_t)((L32(-0x4)) >> 16)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L14cc:
  L16(-0x14) = (uint16_t)((uint16_t)(L16(-0x14) + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0xe) = (uint16_t)((uint16_t)(L16(-0xe) + 1));
  if ((int16_t)(L16(-0xe)) < (int16_t)(ax)) goto L139c;
L14dd:
  { uint32_t r_ = CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L14e8:
  L16(-0xe) = (uint16_t)(0x0);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(0x0)) goto L170c;
  L16(-0x6) = (uint16_t)(0x0);
L14fb:
  bx = drd16(0xba);
  es = drd16(0xbc);
  si = (uint16_t)(L16(-0x6));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + si)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + si + 0x2)));
  di = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L170c;
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x2))) != (uint16_t)(ax)) goto L16fb;
  L16(-0xc) = (uint16_t)(0x0);
  ax = (uint16_t)((uint16_t)(di + 0xc));
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(es);
  L16(-0x12) = (uint16_t)(di);
  bx = (uint16_t)(L16(-0xc));
  di = (uint16_t)(ax);
L1538:
  es = L16(-0x2);
  if ((int16_t)(rd16(es, di)) <= (int16_t)(0x0)) goto L1549;
  si = (uint16_t)(bx);
  if ((uint8_t)(rd8(rt_ss, (uint16_t)(rt_bp + si - 0x52))) == (uint8_t)(0x0)) goto L1552;
L1549:
  di = (uint16_t)((uint16_t)(di + 0x2));
  bx = (uint16_t)((uint16_t)(bx + 1));
  if ((int16_t)(bx) < (int16_t)(0x10)) goto L1538;
L1552:
  di = (uint16_t)(L16(-0x12));
  if ((uint16_t)(bx) != (uint16_t)(0x10)) goto L1603;
  es = L16(-0x10);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x6))) == (uint16_t)(0xffff)) goto L15cc;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(di + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x24) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L15cc;
  es = L16(-0x24);
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L15cc;
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) != 0) goto L159c;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x5); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L15cc;
L159c:
  es = L16(-0x10);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xa)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf5)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x5)));
  wr16(es, (uint16_t)(bx + 0xa), ax);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L16fb;
L15cc:
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) <= (int16_t)(0x1)) goto L15ef;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x6))) != (uint16_t)(0xffff)) goto L15ef;
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) == (uint8_t)(0x1)) goto L16fb;
L15ef:
  { uint32_t r_ = CALL_ORIG(18, 0x4510, FAR_CDECL, 3, di, L16(-0x10), 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L16fb;
L1603:
  cx = (uint16_t)(0x2);
  ax = (uint16_t)((uint16_t)(di + 0x10));
  dx = (uint16_t)(L16(-0x10));
  bx = (uint16_t)(ax);
  ds = dx;
L1610:
  if ((int16_t)(rd16(ds, bx)) > (int16_t)(0x0)) goto L1626;
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(cx) <= (int16_t)(0xa)) goto L1610;
  L16(-0xc) = (uint16_t)(cx);
  /* ds = ss */
  goto L1636;
L1626:
  L16(-0xc) = (uint16_t)(cx);
  /* ds = ss */
  { const uint16_t a_[] = {(uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16)}; uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(17), 0x1b70), FAR_CDECL, a_, 2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L1636:
  if ((int16_t)(L16(-0xc)) <= (int16_t)(0xa)) goto L16fb;
  { uint32_t r_ = CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L16fb;
  bx = L16(-0x12);
  es = L16(-0x10);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L166c;
  t82 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t82;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) == (uint8_t)(0x2)) goto L16fb;
L166c:
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x78)) goto L1678;
  ax = (uint16_t)(0x32);
  goto L167b;
L1678:
  ax = (uint16_t)(0x46);
L167b:
  if ((int16_t)(ax) < (int16_t)(L16(-0x42))) goto L16a2;
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x78)) goto L168c;
  ax = (uint16_t)(0x28);
  goto L168f;
L168c:
  ax = (uint16_t)(0x3c);
L168f:
  if ((int16_t)(ax) >= (int16_t)(L16(-0x42))) goto L16dd;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x3); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L16dd;
L16a2:
  bx = L16(-0x12);
  es = L16(-0x10);
  bx = (uint16_t)((uint16_t)(bx + 0xc));
  si = (uint16_t)(L16(-0x18));
  si = (uint16_t)((uint16_t)(si + si));
  if ((int16_t)(rd16(es, (uint16_t)(bx + si))) < (int16_t)(0x14)) goto L16c1;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x14); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L16dd;
L16c1:
  { uint32_t r_ = CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), 0xe, 0xf, 0x24, 0x0, 0x48, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L16fb;
L16dd:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x80)));
  ax = (uint16_t)((uint16_t)(ax & 0x10));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)(uint8_t)ax >> 4)));
  { uint32_t r_ = CALL_ORIG(18, 0x1434, FAR_CDECL, 7, (uint16_t)(L32(-0x12)), (uint16_t)((L32(-0x12)) >> 16), (uint16_t)(L32(-0x34)), (uint16_t)((L32(-0x34)) >> 16), (uint16_t)(L32(-0x38)), (uint16_t)((L32(-0x38)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L16fb:
  L16(-0x6) = (uint16_t)((uint16_t)(L16(-0x6) + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0xe) = (uint16_t)((uint16_t)(L16(-0xe) + 1));
  if ((int16_t)(L16(-0xe)) < (int16_t)(ax)) goto L14fb;
L170c:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  t84 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-0x86), buf, L16(-0x16), ax, t84); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = far_call(21, 0x1e2a, NULL, 0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  FRAME_LEAVE();
  return;
}

/* ai1_turn: 17:0x234e (Ghidra FUN_1080_234e, alias fn_17_234e), 5473 bytes. ENTER 0x8a,0; PUSH DI; PUSH SI.
 * FAR CDECL (buf [BP+6]), no result. The turn of the type-1 computer player, the sibling of 17:0x0462: the
 * same threshold, planet and fleet walks with its own design builder (17:0x38b0), the cs table at
 * 17:0x0205 passed to 18:0x28fc, the cost checks against the design records (7:0x3b60) before 18:0x2732 and the
 * fleet walk that ends with 18:0x59ba. All locals are BP_LOCAL slots. */
DECOMP(17, 0x234e, ai1_turn, FAR_CDECL)
void ai1_turn(uint16_t buf)
{
  __attribute__((unused)) uint16_t ax = 0, bx = 0, cx = 0, di = 0, dx = 0, si = 0, ax_h = 0, cx_h = 0, dx_h = 0;
  __attribute__((unused)) uint16_t es = 0;
  __attribute__((unused)) uint16_t ds = MEM_SEL_DGROUP;
  uint16_t t1 = 0, t2 = 0, t3 = 0, st0 = 0, t13 = 0, t14 = 0, t15 = 0, t16 = 0, t17 = 0, t18 = 0, t19 = 0, t20 = 0, t21 = 0, t22 = 0, t23 = 0, t24 = 0, t25 = 0, t26 = 0, t27 = 0, t28 = 0, t29 = 0, t30 = 0, t31 = 0, t32 = 0, t34 = 0, t41 = 0, t43 = 0, t45 = 0, t47 = 0, t54 = 0, t62 = 0, t64 = 0, t87 = 0;
  FRAME_ENTER_REGS(0x8a, 2);
  SET32(ax, ax_h, 0);
  L16(-0x2e) = (uint16_t)(ax);
  L16(-0x34) = (uint16_t)(ax);
  L32(-0x3a) = (uint32_t)(E32(ax, ax_h));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x14));
  { uint32_t r_ = CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0x205, SEG_SEL(17), 0x1f, ax, 0xf); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x86) = (uint16_t)(ax);
  if ((uint8_t)((uint8_t)(drd8(0x491f)) & (uint8_t)(0x2)) != 0) goto L238b;
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xe0f0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L238b:
  if ((uint8_t)((uint8_t)(drd8(0x4892)) & (uint8_t)(0x2)) != 0) goto L239d;
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x1000); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L239d:
  if ((uint8_t)((uint8_t)(drd8(0x4778)) & (uint8_t)(0x2)) != 0) goto L23af;
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xc00); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L23af:
  if ((uint8_t)((uint8_t)(drd8(0x4310)) & (uint8_t)(0x2)) != 0) goto L23c0;
  { uint32_t r_ = CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xc); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L23c0:
  di = (uint16_t)(0x3);
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x82)) goto L23dd;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - 0x78));
  cx = (uint16_t)(0x14);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x23d6); ax = a_; dx = d_; }
  di = (uint16_t)(ax);
  di = (uint16_t)((uint16_t)(di + 0x3));
L23dd:
  if ((int16_t)(di) <= (int16_t)(0x32)) goto L23e5;
  di = (uint16_t)(0x32);
L23e5:
  ax = (uint16_t)(di);
  dwr8(0x5340, (uint8_t)ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)(uint8_t)ax >> 1)));
  dwr8(0x5341, (uint8_t)ax);
  di = (uint16_t)(0x6);
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x73)) goto L240b;
  ax = (uint16_t)(drd16(0x82));
  ax = (uint16_t)((uint16_t)(ax - 0x64));
  cx = (uint16_t)(0x16);
  dx = (uint16_t)(0);
  { uint16_t a_ = ax, d_ = dx; div16_(&a_, &d_, cx, 0x2404); ax = a_; dx = d_; }
  di = (uint16_t)(ax);
  di = (uint16_t)((uint16_t)(di + 0x6));
L240b:
  L16(-0x6) = (uint16_t)(di);
  if ((int16_t)(di) <= (int16_t)(0xc)) goto L2418;
  L16(-0x6) = (uint16_t)(0xc);
L2418:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x6)));
  dwr8(0x5342, (uint8_t)ax);
  ax = (uint16_t)(L16(-0x6));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  ax = (uint16_t)((uint16_t)(ax - dx));
  ax = (uint16_t)((uint16_t)((int16_t)ax >> 1));
  ax = (uint16_t)((uint16_t)(ax - 1));
  if ((int16_t)(ax) <= (int16_t)(0x3)) goto L242f;
  ax = (uint16_t)(0x3);
L242f:
  dwr8(0x5343, (uint8_t)ax);
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x52));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  if ((uint16_t)(drd16(0x82)) >= (uint16_t)(0x78)) goto L2452;
  L16(-0x2) = (uint16_t)(0x32);
  goto L2466;
L2452:
  if ((uint16_t)(drd16(0x82)) < (uint16_t)(0xc8)) goto L2460;
  ax = (uint16_t)(0x64);
  goto L2463;
L2460:
  ax = (uint16_t)(0x46);
L2463:
  L16(-0x2) = (uint16_t)(ax);
L2466:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x16));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x6, 0x7, L16(-0x2), cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x18));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x8, 0x9, L16(-0x2), cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x36) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1a));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xd, 0xe, L16(-0x2), cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1c));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x4, 0x5, L16(-0x2), cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  cx = (uint16_t)((uint16_t)(rt_bp - 0x1e));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xc, 0xc, L16(-0x2), cx, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  t1 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x20));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xf, 0xf, L16(-0x2), ax, t1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x7)) goto L2528;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  t2 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x22));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x2, 0x3, L16(-0x2), ax, t2); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L252d;
L2528:
  L16(-0x22) = (uint16_t)(0xffff);
L252d:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  t3 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x24));
  { uint32_t r_ = CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xa, 0xb, L16(-0x2), ax, t3); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((uint16_t)(drd16(0x82)) <= (uint16_t)(0x3c)) goto L2559;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  { uint32_t r_ = CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L2559:
  { const uint16_t a_[] = {L16(-0x86)}; uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(17), 0x38b0), FAR_CDECL, a_, 1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)((uint16_t)drd16(0x4d96) * (uint16_t)0x38));
  ax = (uint16_t)((uint16_t)(ax + drd16(0xb6)));
  dx = (uint16_t)(drd16(0xb8));
  L16(-0x28) = (uint16_t)(ax);
  L16(-0x26) = (uint16_t)(dx);
  cx = (uint16_t)(drd16(0xb6));
  si = (uint16_t)(cx);
  L16(-0x2a) = (uint16_t)(dx);
  if ((uint16_t)(ax) <= (uint16_t)(cx)) goto L25b8;
  di = (uint16_t)(L16(-0x2e));
L2587:
  es = L16(-0x2a);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(0xffff)) goto L25ab;
  if ((uint8_t)(rd8(es, (uint16_t)(si + 0x4))) < (uint8_t)(0x3)) goto L25ab;
  { uint32_t r_ = CALL_ORIG(9, 0x462a, FAR_CDECL, 3, si, es, drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((int16_t)(ax) <= 0) goto L25ab;
  di = (uint16_t)((uint16_t)(di + 1));
L25ab:
  si = (uint16_t)((uint16_t)(si + 0x38));
  ax = (uint16_t)(si);
  if ((uint16_t)(ax) < (uint16_t)(L16(-0x28))) goto L2587;
  L16(-0x2e) = (uint16_t)(di);
L25b8:
  ax = (uint16_t)(drd16(0xb6));
  dx = (uint16_t)(drd16(0xb8));
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  cx = (uint16_t)((uint16_t)((uint16_t)drd16(0x4d96) * (uint16_t)0x38));
  ax = (uint16_t)((uint16_t)(ax + cx));
  L16(-0x28) = (uint16_t)(ax);
  L16(-0x26) = (uint16_t)(dx);
  if ((uint16_t)(ax) <= (uint16_t)(si)) goto L2e2b;
L25d7:
  es = L16(-0x2a);
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + di + 0x9), 0x1);
  es = L16(-0x2a);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(0xffff)) goto L2658;
  if ((uint8_t)(rd8(es, (uint16_t)(si + 0x4))) < (uint8_t)(0x3)) goto L2658;
  L16(-0x2c) = (uint16_t)(si);
  di = (uint16_t)(0);
  L8(-0x2) = (uint8_t)(0x0);
  L16(-0x10) = (uint16_t)(di);
  si = (uint16_t)(di);
  ds = L16(-0x2a);
L260b:
  bx = (uint16_t)(L16(-0x2c));
  bx = (uint16_t)((uint16_t)(bx + 0x9));
  if ((uint8_t)(rd8(ds, (uint16_t)(bx + si))) <= (uint8_t)(0x42)) goto L261c;
  L8(-0x1) = (uint8_t)(0x4b);
  goto L2629;
L261c:
  bx = (uint16_t)(L16(-0x2c));
  bx = (uint16_t)((uint16_t)(bx + 0x9));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(rd8(ds, (uint16_t)(bx + si))));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)(uint8_t)ax >> 1)));
  L8(-0x1) = (uint8_t)((uint8_t)ax);
L2629:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x1)));
  L8(-0x2) = (uint8_t)((uint8_t)(L8(-0x2) + (uint8_t)ax));
  si = (uint16_t)((uint16_t)(si + 1));
  if ((int16_t)(si) < (int16_t)(0x3)) goto L260b;
  /* ds = ss */
  if ((uint8_t)((uint8_t)(L8(-0x2)) & (uint8_t)(0x80)) == 0) goto L2641;
  L8(-0x2) = (uint8_t)(0x7f);
L2641:
  si = (uint16_t)(L16(-0x2c));
  es = L16(-0x2a);
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x2)));
  wr8(es, (uint16_t)(bx + di + 0x1), (uint8_t)ax);
L2658:
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0x2a);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(ax)) goto L26b8;
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L26b8;
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x4)));
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  ax = (uint16_t)((uint16_t)((uint16_t)ax >> 9));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0x1)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax + 1)));
  wr8(es, (uint16_t)(bx + di + 0xa), (uint8_t)ax);
  { uint32_t r_ = CALL_ORIG(9, 0x462a, FAR_CDECL, 3, si, L16(-0x2a), drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(ax);
  di = (uint16_t)((uint16_t)(di | ax));
  if ((int16_t)(di) <= 0) goto L2e1f;
  es = L16(-0x2a);
  bx = (uint16_t)(rd16(es, si));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 4));
  bx = (uint16_t)((uint16_t)(bx + drd16(0x1600)));
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + 0x3), (uint8_t)ax);
  L16(-0x34) = (uint16_t)((uint16_t)(L16(-0x34) + 1));
  goto L2e1f;
L26b8:
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L2e1f;
  { uint32_t r_ = CALL_ORIG(9, 0x474c, FAR_CDECL, 3, si, es, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((int16_t)(ax) >= 0) goto L26e4;
  es = L16(-0x2a);
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + di + 0x2), 0x1);
  goto L2e1f;
L26e4:
  es = L16(-0x2a);
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) == 0) goto L2dce;
  if ((int32_t)(rd32(es, (uint16_t)(si + 0x28))) < (int32_t)(0xc8)) goto L2dce;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x1, rd16(es, si)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(26), 0x100), FAR_CDECL, (const uint16_t[]){buf}, 1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)(0);
  L16(-0xa) = (uint16_t)(ax);
  L16(-0x10) = (uint16_t)(ax);
  SET32(ax, ax_h, drd32(0xcd2));
  ax = (uint16_t)((uint16_t)(ax + 0x4));
  L32(-0x4) = (uint32_t)(E32(ax, ax_h));
  L16(-0x2c) = (uint16_t)(si);
  bx = drd16(0xcd2);
  es = drd16(0xcd4);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(rd8(es, (uint16_t)(bx + 0x3))));
  L8(-0x88) = (uint8_t)((uint8_t)ax);
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | (uint8_t)ax)));
  if ((uint8_t)((uint8_t)ax) != 0) goto L274e;
  cx = (uint16_t)(L16(-0x10));
L273e:
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x88)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) <= (int16_t)(cx)) goto L2786;
  st0 = 0x0;
  goto L2d85;
L274e:
  si = (uint16_t)(L16(-0x4));
  cx = (uint16_t)(L16(-0x10));
  ds = L16(-0x2);
L2757:
  ax = (uint16_t)(rd16(ds, (uint16_t)(si + 0x2)));
  ax = (uint16_t)((uint16_t)(ax & 0xe));
  if ((uint16_t)(ax) != (uint16_t)(0x4)) goto L2773;
  SET32(ax, ax_h, rd32(ds, si));
  SET32(ax, ax_h, (uint32_t)(E32(ax, ax_h) & 0x1fc00));
  if ((uint32_t)(E32(ax, ax_h)) < (uint32_t)(0x4000)) goto L2781;
L2773:
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(L8(-0x88)));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(ax) > (int16_t)(cx)) goto L2757;
L2781:
  /* ds = ss */
  goto L273e;
L2786:
  if ((uint16_t)(drd16(0x82)) != (uint16_t)(0x0)) goto L27c8;
  si = (uint16_t)(drd16(0x7a));
  si = (uint16_t)((uint16_t)(si | si));
  if ((int16_t)(si) <= 0) goto L2829;
  L16(-0xa) = (uint16_t)(0x1);
L279c:
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x0, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(si) <= (int16_t)(0xbe)) goto L27bc;
  ax = (uint16_t)(0x64);
  goto L27bf;
L27bc:
  ax = (uint16_t)(0x1e);
L27bf:
  si = (uint16_t)((uint16_t)(si - ax));
  si = (uint16_t)((uint16_t)(si | si));
  if ((int16_t)(si) > 0) goto L279c;
  goto L2829;
L27c8:
  if ((uint8_t)((uint8_t)(drd8(0x41f6)) & (uint8_t)(0x2)) != 0) goto L2829;
  if ((uint16_t)(drd16(0x417a)) != (uint16_t)(0x5)) goto L2829;
  ax = (uint16_t)(drd16(0x7a));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  dx = (uint16_t)((uint16_t)(dx & 0x3));
  ax = (uint16_t)((uint16_t)(ax + dx));
  ax = (uint16_t)((uint16_t)((int16_t)ax >> 2));
  if ((int16_t)(ax) <= (int16_t)(0x20)) goto L27ea;
  ax = (uint16_t)(0x20);
L27ea:
  dx = (uint16_t)(0);
  if ((uint16_t)(dx) < (uint16_t)(drd16(0x41ff))) goto L2829;
  if ((uint16_t)(dx) > (uint16_t)(drd16(0x41ff))) goto L27fa;
  if ((uint16_t)(ax) <= (uint16_t)(drd16(0x41fd))) goto L2829;
L27fa:
  SET32(ax, ax_h, drd32(0x41f9));
  SET32(cx, cx_h, (((uint32_t)(dx) << 16) | (uint16_t)(0xa)));
  SET32(dx, dx_h, 0);
  { uint32_t a_ = E32(ax, ax_h), d_ = E32(dx, dx_h); div32_(&a_, &d_, E32(cx, cx_h), 0x2806); SET32(ax, ax_h, a_); SET32(dx, dx_h, d_); }
  if ((uint32_t)(E32(ax, ax_h)) >= (uint32_t)(drd32(0x41fd))) goto L2829;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x0, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xa) = (uint16_t)(0x1);
L2829:
  bx = (uint16_t)((uint16_t)((uint16_t)drd16(0x14c) * (uint16_t)0xc0));
  ax = (uint16_t)(drd16((uint16_t)(bx + 0x5a66)));
  cx = (uint16_t)(0xa);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x2837); ax = a_; dx = d_; }
  di = (uint16_t)(ax);
  si = drd16(0x1604);
  es = drd16(0x1606);
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x2)));
  ax = (uint16_t)((uint16_t)(ax + ax));
  if ((int16_t)(di) >= (int16_t)(ax)) goto L284b;
  di = (uint16_t)(ax);
L284b:
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a80))) < (int8_t)(0x5)) goto L2899;
  if ((uint16_t)(L16(-0x18)) == (uint16_t)(0xffff)) goto L2899;
  if ((int16_t)(L16(-0x36)) < (int16_t)(di)) goto L2881;
  ax = (uint16_t)(di);
  cx = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 2));
  ax = (uint16_t)((uint16_t)(ax + cx));
  ax = (uint16_t)((uint16_t)(ax + ax));
  cx = (uint16_t)(0x7);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x286c); ax = a_; dx = d_; }
  if ((int16_t)(ax) <= (int16_t)(L16(-0x36))) goto L2899;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x4); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L2899;
L2881:
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x18), 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xa) = (uint16_t)(0x1);
L2899:
  if ((uint16_t)(L16(-0x2e)) != (uint16_t)(0x0)) goto L28a5;
  if ((uint16_t)(L16(-0x34)) == (uint16_t)(0x0)) goto L2902;
L28a5:
  if ((uint32_t)(drd32(0x428a)) >= (uint32_t)(0x2)) goto L2902;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x1, 0x1, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xa) = (uint16_t)(0x1);
L2902:
  { uint32_t r_ = CALL_ORIG(28, 0x41d4, FAR_CDECL, 1, drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, (uint16_t)(L32(-0x2c)), (uint16_t)((L32(-0x2c)) >> 16), drd16(0x14c)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((uint8_t)((uint8_t)(drd8(0x4892)) & (uint8_t)(0x2)) != 0) goto L29dd;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x3); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L29dd;
  bx = L16(-0x2c);
  es = L16(-0x2a);
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x6) = (uint16_t)(ax);
  cx = (uint16_t)(0);
  L16(-0x8) = (uint16_t)(cx);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(cx)) goto L2997;
  SET32(ax, ax_h, drd32(0xba));
  L32(-0x4) = (uint32_t)(E32(ax, ax_h));
  di = (uint16_t)(ax);
L2955:
  es = L16(-0x2);
  ax = (uint16_t)(rd16(es, di));
  dx = (uint16_t)(rd16(es, (uint16_t)(di + 0x2)));
  bx = (uint16_t)(ax);
  L16(-0xc) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L2997;
  ax = (uint16_t)(L16(-0x6));
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) != (uint16_t)(ax)) goto L2984;
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x24))) <= (int16_t)(0x0)) goto L2984;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) == (uint16_t)(ax)) goto L2990;
L2984:
  di = (uint16_t)((uint16_t)(di + 0x4));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(cx) < (int16_t)(drd16(0x553a))) goto L2955;
  goto L2997;
L2990:
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x24)));
  L16(-0x8) = (uint16_t)(ax);
L2997:
  if ((int16_t)(L16(-0x8)) < (int16_t)(0xa)) goto L29b1;
  if ((int16_t)(L16(-0x8)) >= (int16_t)(0x11)) goto L29dd;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x8); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L29dd;
L29b1:
  ax = (uint16_t)(L16(-0x8));
  ax = (uint16_t)((uint16_t)(ax + ax));
  ax = (uint16_t)((uint16_t)(ax + 1));
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L29dd;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xc, 0x3, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xa) = (uint16_t)(0x1);
L29dd:
  if ((uint16_t)(L16(-0x1a)) == (uint16_t)(0xffff)) goto L2a42;
  bx = L16(-0x2c);
  es = L16(-0x2a);
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x2) = (uint16_t)(ax);
  L16(-0x12) = (uint16_t)(0x0);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(0x0)) goto L2a42;
  di = (uint16_t)(0);
L29fa:
  bx = drd16(0xba);
  es = drd16(0xbc);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + di)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + di + 0x2)));
  si = (uint16_t)(ax);
  L16(-0xc) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L2a42;
  ax = (uint16_t)(L16(-0x2));
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x6))) != (uint16_t)(ax)) goto L2a34;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L2a34;
  ax = fleet_meets_ship_threshold(MK_FP(es, si), 0x2);
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L2d94;
L2a34:
  di = (uint16_t)((uint16_t)(di + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0x12) = (uint16_t)((uint16_t)(L16(-0x12) + 1));
  if ((int16_t)(L16(-0x12)) < (int16_t)(ax)) goto L29fa;
L2a42:
  if ((uint16_t)(L16(-0x1c)) == (uint16_t)(0xffff)) goto L2b0a;
  ax = (uint16_t)(drd16(0x7a));
  cx = (uint16_t)(0x18);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x2a51); ax = a_; dx = d_; }
  ax = (uint16_t)((uint16_t)(ax + 0x4));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x1c) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L2b0a;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L2a6f;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) >= (uint16_t)(ax)) goto L2b0a;
L2a6f:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, (uint16_t)(L32(-0x2c)), (uint16_t)((L32(-0x2c)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, (uint16_t)(L32(-0x2c)), (uint16_t)((L32(-0x2c)) >> 16), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(0);
L2a91:
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L2d82;
  si = (uint16_t)((uint16_t)(si + 0x4));
  if ((int16_t)(si) < (int16_t)(0x10)) goto L2a91;
  L16(-0x10) = (uint16_t)(0x0);
L2aae:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x42));
  cx = (uint16_t)((uint16_t)((uint16_t)L16(-0x1c) * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)((uint16_t)(rt_bp - 0x42));
  si = (uint16_t)((uint16_t)(rt_bp - 0x62));
L2acf:
  ax = (uint16_t)(drd16(di));
  dx = (uint16_t)(0);
  t13 = drd16(si); t14 = ax;
  dwr16(si, (uint16_t)(t13 - t14));
  t15 = ((uint16_t)(t13) < (uint16_t)(t14)) ? 1 : 0;
  t16 = drd16((uint16_t)(si + 0x2)); t17 = dx;
  dwr16((uint16_t)(si + 0x2), (uint16_t)(t16 - t17 - t15));
  if ((int16_t)(drd16((uint16_t)(si + 0x2))) < (int16_t)(dx)) goto L2d82;
  di = (uint16_t)((uint16_t)(di + 0x2));
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L2acf;
  ax = (uint16_t)(0x1);
  L16(-0xa) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x1c), ax, 0x2, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x10) = (uint16_t)((uint16_t)(L16(-0x10) + 1));
  if ((int16_t)(L16(-0x10)) < (int16_t)(0x5)) goto L2aae;
L2b0a:
  si = (uint16_t)(L16(-0x2c));
  if ((uint16_t)(L16(-0x16)) == (uint16_t)(0xffff)) goto L2bdd;
  ax = (uint16_t)(drd16(0x7a));
  cx = (uint16_t)(0xc);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x2b1c); ax = a_; dx = d_; }
  ax = (uint16_t)((uint16_t)(ax + 0x8));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x16) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L2bdd;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L2b3a;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) >= (uint16_t)(ax)) goto L2bdd;
L2b3a:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(di);
  si = (uint16_t)(di);
L2b61:
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L2d82;
  si = (uint16_t)((uint16_t)(si + 0x4));
  if ((int16_t)(si) < (int16_t)(0x10)) goto L2b61;
  L16(-0x10) = (uint16_t)(0x0);
L2b7e:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x42));
  cx = (uint16_t)((uint16_t)((uint16_t)L16(-0x16) * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)((uint16_t)(rt_bp - 0x42));
  si = (uint16_t)((uint16_t)(rt_bp - 0x62));
L2b9f:
  ax = (uint16_t)(drd16(di));
  dx = (uint16_t)(0);
  t18 = drd16(si); t19 = ax;
  dwr16(si, (uint16_t)(t18 - t19));
  t20 = ((uint16_t)(t18) < (uint16_t)(t19)) ? 1 : 0;
  t21 = drd16((uint16_t)(si + 0x2)); t22 = dx;
  dwr16((uint16_t)(si + 0x2), (uint16_t)(t21 - t22 - t20));
  if ((int16_t)(drd16((uint16_t)(si + 0x2))) < (int16_t)(dx)) goto L2d82;
  di = (uint16_t)((uint16_t)(di + 0x2));
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L2b9f;
  ax = (uint16_t)(0x1);
  L16(-0xa) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x16), ax, 0x2, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x10) = (uint16_t)((uint16_t)(L16(-0x10) + 1));
  if ((int16_t)(L16(-0x10)) < (int16_t)(0x5)) goto L2b7e;
  si = (uint16_t)(L16(-0x2c));
L2bdd:
  if ((uint16_t)(L16(-0x24)) == (uint16_t)(0xffff)) goto L2cb0;
  ax = (uint16_t)(drd16(0x7a));
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  dx = (uint16_t)((uint16_t)(dx & 0x3));
  ax = (uint16_t)((uint16_t)(ax + dx));
  ax = (uint16_t)((uint16_t)((int16_t)ax >> 2));
  ax = (uint16_t)((uint16_t)(ax + 0xc));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x24) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L2cb0;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L2c0d;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) >= (uint16_t)(ax)) goto L2cb0;
L2c0d:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(di);
  si = (uint16_t)(di);
L2c34:
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L2d82;
  si = (uint16_t)((uint16_t)(si + 0x4));
  if ((int16_t)(si) < (int16_t)(0x10)) goto L2c34;
  L16(-0x10) = (uint16_t)(0x0);
L2c51:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x42));
  cx = (uint16_t)((uint16_t)((uint16_t)L16(-0x24) * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)((uint16_t)(rt_bp - 0x42));
  si = (uint16_t)((uint16_t)(rt_bp - 0x62));
L2c72:
  ax = (uint16_t)(drd16(di));
  dx = (uint16_t)(0);
  t23 = drd16(si); t24 = ax;
  dwr16(si, (uint16_t)(t23 - t24));
  t25 = ((uint16_t)(t23) < (uint16_t)(t24)) ? 1 : 0;
  t26 = drd16((uint16_t)(si + 0x2)); t27 = dx;
  dwr16((uint16_t)(si + 0x2), (uint16_t)(t26 - t27 - t25));
  if ((int16_t)(drd16((uint16_t)(si + 0x2))) < (int16_t)(dx)) goto L2d82;
  di = (uint16_t)((uint16_t)(di + 0x2));
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L2c72;
  ax = (uint16_t)(0x1);
  L16(-0xa) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x24), ax, 0x2, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x10) = (uint16_t)((uint16_t)(L16(-0x10) + 1));
  if ((int16_t)(L16(-0x10)) < (int16_t)(0x5)) goto L2c51;
  si = (uint16_t)(L16(-0x2c));
L2cb0:
  if ((uint16_t)(L16(-0x20)) == (uint16_t)(0xffff)) goto L2d82;
  if ((uint8_t)((uint8_t)(drd8(0x4a39)) & (uint8_t)(0x2)) != 0) goto L2d82;
  ax = (uint16_t)(drd16(0x7a));
  cx = (uint16_t)(0xc);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  { uint16_t a_ = ax, d_ = dx; idiv16_(&a_, &d_, cx, 0x2cc8); ax = a_; dx = d_; }
  ax = (uint16_t)((uint16_t)(ax + 0x8));
  dx = (uint16_t)(0);
  bx = (uint16_t)((uint16_t)((uint16_t)L16(-0x20) * (uint16_t)0x8d));
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) > (uint16_t)(dx)) goto L2d82;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41ff))) < (uint16_t)(dx)) goto L2ce6;
  if ((uint16_t)(drd16((uint16_t)(bx + 0x41fd))) >= (uint16_t)(ax)) goto L2d82;
L2ce6:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x3558, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  { uint32_t r_ = CALL_ORIG(18, 0x3600, FAR_CDECL, 3, si, L16(-0x2a), ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(0);
  L16(-0x2) = (uint16_t)(di);
  si = (uint16_t)(di);
L2d0d:
  SET32(ax, ax_h, rd32(rt_ss, (uint16_t)(rt_bp + si - 0x72)));
  wr32(rt_ss, (uint16_t)(rt_bp + si - 0x62), (uint32_t)(rd32(rt_ss, (uint16_t)(rt_bp + si - 0x62)) - E32(ax, ax_h)));
  if ((int16_t)(rd16(rt_ss, (uint16_t)(rt_bp + si - 0x60))) < (int16_t)(0x0)) goto L2d82;
  si = (uint16_t)((uint16_t)(si + 0x4));
  if ((int16_t)(si) < (int16_t)(0x10)) goto L2d0d;
  L16(-0x10) = (uint16_t)(0x0);
L2d28:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x42));
  cx = (uint16_t)((uint16_t)((uint16_t)L16(-0x20) * (uint16_t)0x8d));
  cx = (uint16_t)((uint16_t)(cx + 0x417a));
  { uint32_t r_ = CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, drd16(0x14c), cx, MEM_SEL_DGROUP, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)((uint16_t)(rt_bp - 0x42));
  si = (uint16_t)((uint16_t)(rt_bp - 0x62));
L2d49:
  ax = (uint16_t)(drd16(di));
  dx = (uint16_t)(0);
  t28 = drd16(si); t29 = ax;
  dwr16(si, (uint16_t)(t28 - t29));
  t30 = ((uint16_t)(t28) < (uint16_t)(t29)) ? 1 : 0;
  t31 = drd16((uint16_t)(si + 0x2)); t32 = dx;
  dwr16((uint16_t)(si + 0x2), (uint16_t)(t31 - t32 - t30));
  if ((int16_t)(drd16((uint16_t)(si + 0x2))) < (int16_t)(dx)) goto L2d82;
  di = (uint16_t)((uint16_t)(di + 0x2));
  si = (uint16_t)((uint16_t)(si + 0x4));
  ax = (uint16_t)((uint16_t)(rt_bp - 0x52));
  if ((uint16_t)(si) < (uint16_t)(ax)) goto L2d49;
  ax = (uint16_t)(0x1);
  L16(-0xa) = (uint16_t)(ax);
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x20), ax, 0x2, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0x10) = (uint16_t)((uint16_t)(L16(-0x10) + 1));
  if ((int16_t)(L16(-0x10)) < (int16_t)(0x5)) goto L2d28;
L2d82:
  st0 = L16(-0xa);
L2d85:
  { uint32_t r_ = rt_call_far(MK_FP(SEG_SEL(26), 0x810), FAR_CDECL, (const uint16_t[]){st0}, 1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(L16(-0x2c));
  goto L2e1f;
L2d94:
  if ((uint16_t)(L16(-0x1a)) == (uint16_t)(0xffff)) goto L2a42;
  es = L16(-0xc);
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x26)));
  ax = (uint16_t)((uint16_t)(ax + rd16(es, (uint16_t)(si + 0x28))));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)(drd8(0x5342)));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) < (int16_t)(cx)) goto L2a42;
  { uint32_t r_ = CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x1a), 0x4, 0x2, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xa) = (uint16_t)(0x1);
  goto L2d82;
L2dce:
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x1, rd16(es, si)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  es = L16(-0x2a);
  if ((int32_t)(rd32(es, (uint16_t)(si + 0x28))) >= (int32_t)(0xc8)) goto L2dee;
  ax = (uint16_t)(0x1);
  goto L2df0;
L2dee:
  ax = (uint16_t)(0);
L2df0:
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
  ax = (uint16_t)((uint16_t)((uint16_t)ax << 7));
  ax = (uint16_t)((uint16_t)(ax ^ drd16(0x4c22)));
  ax = (uint16_t)((uint16_t)(ax & 0x80));
  cx = (uint16_t)(0);
  dwr16(0x4c20, (uint16_t)(drd16(0x4c20) ^ cx));
  dwr16(0x4c22, (uint16_t)(drd16(0x4c22) ^ ax));
  ax = (uint16_t)(rd16(es, (uint16_t)(si + 0x1a)));
  ax = (uint16_t)((uint16_t)(ax ^ drd16(0x4c22)));
  if ((uint16_t)((uint16_t)(ax) & (uint16_t)(0x80)) == 0) goto L2e1f;
  { uint32_t r_ = CALL_ORIG(7, 0x358, FAR_CDECL, 2, 0xffff, 0x4c08); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L2e1f:
  si = (uint16_t)((uint16_t)(si + 0x38));
  ax = (uint16_t)(si);
  if ((uint16_t)(ax) < (uint16_t)(L16(-0x28))) goto L25d7;
L2e2b:
  cx = (uint16_t)(0);
  if ((int16_t)(drd16(0x4b62)) <= (int16_t)(cx)) goto L2e56;
  bx = drd16(0x1608);
  es = drd16(0x160a);
L2e37:
  t34 = rd16(es, (uint16_t)(bx + 0x2));
  si = rd16(es, bx);
  ds = t34;
  ax = (uint16_t)(ds);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L2e51;
  if ((uint8_t)((uint8_t)(rd8(ds, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) != 0) goto L2e51;
  bx = (uint16_t)((uint16_t)(bx + 0x4));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(rd16(rt_ss, 0x4b62)) > (int16_t)(cx)) goto L2e37;
L2e51:
  L16(-0x2a) = (uint16_t)(ds);
  /* ds = ss */
L2e56:
  ax = (uint16_t)(L16(-0x2a));
  if ((uint16_t)(si) != (uint16_t)(L16(-0x28))) goto L2e6e;
  if ((uint16_t)(ax) != (uint16_t)(L16(-0x26))) goto L2e6e;
  L32(-0x28) = (uint32_t)(0x0);
  goto L2e74;
L2e6e:
  L16(-0x28) = (uint16_t)(si);
  L16(-0x26) = (uint16_t)(ax);
L2e74:
  SET32(ax, ax_h, 0);
  L32(-0x3a) = (uint32_t)(E32(ax, ax_h));
  L32(-0x36) = (uint32_t)(E32(ax, ax_h));
  L16(-0x12) = (uint16_t)(ax);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(ax)) goto L31dd;
  L16(-0x4) = (uint16_t)(ax);
L2e8d:
  bx = drd16(0xba);
  es = drd16(0xbc);
  di = (uint16_t)(L16(-0x4));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + di)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + di + 0x2)));
  si = (uint16_t)(ax);
  L16(-0xc) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L31dd;
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(ax)) goto L2ec4;
  SET32(ax, ax_h, L32(-0x36));
  wr32(es, (uint16_t)(si + 0x68), E32(ax, ax_h));
  L16(-0x36) = (uint16_t)(si);
  L16(-0x34) = (uint16_t)(es);
  goto L31cc;
L2ec4:
  { uint32_t r_ = CALL_ORIG(18, 0x2e4e, FAR_CDECL, 2, si, es); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) == 0) goto L2ee4;
  SET32(ax, ax_h, L32(-0x3a));
  es = L16(-0xc);
  wr32(es, (uint16_t)(si + 0x68), E32(ax, ax_h));
  L16(-0x3a) = (uint16_t)(si);
  L16(-0x38) = (uint16_t)(es);
L2ee4:
  es = L16(-0xc);
  wr8(es, (uint16_t)(si + 0x5), (uint8_t)(rd8(es, (uint16_t)(si + 0x5)) & 0x7f));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x10))) != (uint16_t)(0x0)) goto L2efd;
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x12))) == (uint16_t)(0x0)) goto L2f76;
L2efd:
  es = L16(-0xc);
  di = (uint16_t)((uint16_t)((uint16_t)rd16(es, (uint16_t)(si + 0x62)) * (uint16_t)0x12));
  bx = rd16(es, (uint16_t)(si + 0x64));
  es = rd16(es, (uint16_t)(si + 0x66));
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + di - 0x8))) & (uint8_t)(0xf)) == 0) goto L2f76;
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x6))) == (uint16_t)(0xffff)) goto L2f40;
  L16(-0xe) = (uint16_t)(si);
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(si + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  es = dx;
  bx = (uint16_t)(ax);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x2))) != (uint16_t)(0xffff)) goto L3192;
  es = L16(-0xc);
  bx = (uint16_t)(rd16(es, (uint16_t)(si + 0x6)));
  goto L2f64;
L2f40:
  if ((int16_t)(rd16(es, (uint16_t)(si + 0x62))) <= (int16_t)(0x1)) goto L31cc;
  bx = rd16(es, (uint16_t)(si + 0x64));
  es = rd16(es, (uint16_t)(si + 0x66));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1c)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf3)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x3)));
  wr16(es, (uint16_t)(bx + 0x1c), ax);
  es = L16(-0xc);
  bx = rd16(es, (uint16_t)(si + 0x64));
  es = rd16(es, (uint16_t)(si + 0x66));
  bx = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1a)));
L2f64:
  si = (uint16_t)(bx);
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + si + 0x1), (uint8_t)(rd8(es, (uint16_t)(bx + si + 0x1)) | 0x80));
  goto L31cc;
L2f76:
  L16(-0xe) = (uint16_t)(si);
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x1c))) != (uint16_t)(0x0)) goto L3010;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x1e))) != (uint16_t)(0x0)) goto L3010;
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0xe))) == (uint16_t)(0x0)) goto L31cc;
  L16(-0x2) = (uint16_t)(0xffff);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L2fc0;
  t41 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t41;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L2fc7;
  bx = L16(-0xe);
  es = L16(-0xc);
  t43 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t43;
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1a)));
  goto L2fc4;
L2fc0:
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x6)));
L2fc4:
  L16(-0x2) = (uint16_t)(ax);
L2fc7:
  if ((uint16_t)(L16(-0x2)) == (uint16_t)(0xffff)) goto L31cc;
  si = (uint16_t)(L16(-0x2));
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  if ((uint8_t)(rd8(es, (uint16_t)(bx + si + 0x3))) != (uint8_t)(0x0)) goto L3041;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, L16(-0x2)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L31cc;
  es = L16(-0x2a);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L31cc;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(ax)) goto L31cc;
  goto L3192;
L3010:
  L16(-0x2) = (uint16_t)(0xffff);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L303a;
  t45 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t45;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L3041;
  bx = L16(-0xe);
  es = L16(-0xc);
  t47 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t47;
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x1a)));
  goto L303e;
L303a:
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0x6)));
L303e:
  L16(-0x2) = (uint16_t)(ax);
L3041:
  if ((uint16_t)(L16(-0x2)) == (uint16_t)(0xffff)) goto L3178;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, L16(-0x2)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L3074;
  es = L16(-0x2a);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L31cc;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(ax)) goto L31cc;
L3074:
  di = (uint16_t)(L16(-0x2));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  if ((uint8_t)(rd8(es, (uint16_t)(bx + di + 0x3))) == (uint8_t)(0x0)) goto L3192;
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((int32_t)(rd32(es, (uint16_t)(bx + 0x58))) <= (int32_t)(0x0)) goto L3192;
  es = L16(-0x2a);
  ax = (uint16_t)((uint16_t)((uint16_t)rd16(es, (uint16_t)(si + 0x2)) * (uint16_t)0xc0));
  ax = (uint16_t)((uint16_t)(ax + 0x5a64));
  { uint32_t r_ = CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, ax, 0xe); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((uint16_t)(ax) == (uint16_t)(0x8)) goto L3192;
  es = L16(-0x2a);
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) != 0) goto L3192;
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x84));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  wr16(es, di, ax); di = (uint16_t)(di + 2);
  bx = (uint16_t)(L16(-0x2));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(ax, ax_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(ax, ax_h));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x2));
  L16(-0x80) = (uint16_t)(ax);
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx & 0xf1)));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx | 0x1)));
  L16(-0x7e) = (uint16_t)(cx);
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  cx = (uint16_t)(L16(-0x76));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) & 0xf)) << 8));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) | 0x20)) << 8));
  L16(-0x76) = (uint16_t)(cx);
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)(L16(-0x2));
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x8))) != (uint16_t)(ax)) goto L313e;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0xb))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x1)) goto L313e;
  di = (uint16_t)((uint16_t)(bx + 0x4));
  goto L3141;
L313e:
  di = (uint16_t)((uint16_t)(bx + 0x16));
L3141:
  si = (uint16_t)((uint16_t)(rt_bp - 0x84));
  SET32(cx, cx_h, 0x4);
  while (cx != 0) { wr32(es, di, rd32(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 4); si = (uint16_t)(si + 4); cx--; }
  wr16(es, di, rd16(MEM_SEL_DGROUP, si)); di = (uint16_t)(di + 2); si = (uint16_t)(si + 2);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(L16(-0x2));
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + si + 0x3), (uint8_t)(rd8(es, (uint16_t)(bx + si + 0x3)) | 0x80));
  { uint32_t r_ = CALL_ORIG(18, 0x4510, FAR_CDECL, 3, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L31c9;
L3178:
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x62))) <= (int16_t)(0x1)) goto L31cc;
  t54 = rd16(es, (uint16_t)(bx + 0x66));
  bx = rd16(es, (uint16_t)(bx + 0x64));
  es = t54;
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)(rd8(es, (uint16_t)(bx + 0x1d))) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf)) << 8));
  if ((uint8_t)((uint8_t)(ax >> 8)) != (uint8_t)(0x4)) goto L31cc;
L3192:
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  dwr16(0x4bee, 0x1);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr8(es, (uint16_t)(bx + 0x3), 0x1);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L31c9:
L31cc:
  L16(-0x4) = (uint16_t)((uint16_t)(L16(-0x4) + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0x12) = (uint16_t)((uint16_t)(L16(-0x12) + 1));
  if ((int16_t)(L16(-0x12)) < (int16_t)(ax)) goto L2e8d;
L31dd:
  ax = (uint16_t)(0);
  dwr16(0x255c, ax);
  L16(-0x12) = (uint16_t)(ax);
  if ((int16_t)(drd16(0x553a)) <= (int16_t)(ax)) goto L388c;
  L16(-0x2e) = (uint16_t)(ax);
L31f0:
  bx = drd16(0xba);
  es = drd16(0xbc);
  si = (uint16_t)(L16(-0x2e));
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + si)));
  dx = (uint16_t)(rd16(es, (uint16_t)(bx + si + 0x2)));
  di = (uint16_t)(ax);
  L16(-0xc) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L388c;
  ax = (uint16_t)(drd16(0x14c));
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x2))) != (uint16_t)(ax)) goto L387b;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x10))) != (uint16_t)(0x0)) goto L376a;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x12))) != (uint16_t)(0x0)) goto L376a;
  if ((int16_t)(rd16(es, (uint16_t)(di + 0x62))) > (int16_t)(0x1)) goto L387b;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0xe))) != (uint16_t)(0x0)) goto L359a;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x1c))) != (uint16_t)(0x0)) goto L34b0;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x1e))) != (uint16_t)(0x0)) goto L34b0;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x26))) != (uint16_t)(0x0)) goto L331a;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x28))) != (uint16_t)(0x0)) goto L331a;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0xc))) != (uint16_t)(0x0)) goto L32ca;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x20))) != (uint16_t)(0x0)) goto L32ca;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x22))) != (uint16_t)(0x0)) goto L32ca;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x24))) == (uint16_t)(0x0)) goto L387b;
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x62))) != (uint16_t)(0x1)) goto L387b;
  bx = rd16(es, (uint16_t)(di + 0x64));
  es = rd16(es, (uint16_t)(di + 0x66));
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(bx + 0xa))) & (uint8_t)(0xf)) != 0) goto L387b;
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xa)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf6)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x6)));
  wr16(es, (uint16_t)(bx + 0xa), ax);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr16(es, (uint16_t)(bx + 0xc), 0x5);
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  wr16(es, (uint16_t)(bx + 0xe), 0x5);
  goto L386e;
L32ca:
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0xc))) == (uint16_t)(0x0)) goto L32e8;
  bx = (uint16_t)((uint16_t)((uint16_t)ax * (uint16_t)0xc0));
  if ((int8_t)(drd8((uint16_t)(bx + 0x5a81))) < (int8_t)(0x6)) goto L32e8;
  L16(-0xe) = (uint16_t)(di);
  if ((uint16_t)(drd16(0x417a)) == (uint16_t)(0x4)) goto L384e;
L32e8:
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(rt_bp - 0x32));
  t62 = ax;
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)(drd8(0x80)));
  ax = (uint16_t)((uint16_t)(ax & 0x10));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)(uint8_t)ax >> 4)));
  { uint32_t r_ = CALL_ORIG(18, 0x3c64, FAR_CDECL, 8, di, L16(-0xc), (uint16_t)(L32(-0x3a)), (uint16_t)((L32(-0x3a)) >> 16), (uint16_t)(L32(-0x36)), (uint16_t)((L32(-0x36)) >> 16), ax, t62); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L387b;
L331a:
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, di)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(di + 0x6))) == (uint16_t)(0xffff)) goto L33f8;
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(di + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  ax = (uint16_t)(drd16(0x14c));
  es = dx;
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) != (uint16_t)(ax)) goto L3398;
  if ((uint8_t)((uint8_t)(rd8(es, (uint16_t)(si + 0x5))) & (uint8_t)(0x2)) == 0) goto L337f;
  es = L16(-0xc);
  ax = (uint16_t)(rd16(es, (uint16_t)(di + 0x26)));
  ax = (uint16_t)((uint16_t)(ax + rd16(es, (uint16_t)(di + 0x28))));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)(drd8(0x5342)));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)(0) << 8));
  if ((int16_t)(ax) < (int16_t)(cx)) goto L387b;
  ax = (uint16_t)(rd16(es, (uint16_t)(di + 0x14)));
  ax = (uint16_t)((uint16_t)(ax + rd16(es, (uint16_t)(di + 0x16))));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)(drd8(0x5341)));
  if ((int16_t)(ax) < (int16_t)(cx)) goto L387b;
L337f:
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, di), 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  L16(-0xe) = (uint16_t)(di);
  L16(-0x2c) = (uint16_t)(si);
  goto L3403;
L3398:
  L16(-0xe) = (uint16_t)(di);
  L16(-0x2c) = (uint16_t)(si);
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x2))) == (uint16_t)(0xffff)) goto L3403;
  ax = (uint16_t)(L16(-0x36));
  dx = (uint16_t)(L16(-0x34));
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L387b;
  L16(-0x4) = (uint16_t)(ax);
  si = L16(-0x4);
  es = L16(-0x2);
L33ba:
  ds = L16(-0xc);
  ax = (uint16_t)(rd16(ds, (uint16_t)(di + 0x8)));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0x8))) == (uint16_t)(ax)) goto L33d6;
L33c6:
  /* ds = ss */
L33c8:
  t64 = rd16(es, (uint16_t)(si + 0x6a));
  si = rd16(es, (uint16_t)(si + 0x68));
  es = t64;
  ax = (uint16_t)(es);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) != 0) goto L33ba;
  goto L387b;
L33d6:
  ax = (uint16_t)(rd16(ds, (uint16_t)(di + 0xa)));
  if ((uint16_t)(rd16(es, (uint16_t)(si + 0xa))) != (uint16_t)(ax)) goto L33c6;
  L16(-0x2) = (uint16_t)(es);
  /* ds = ss */
  { uint32_t r_ = CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, si, es); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L3403;
  es = L16(-0x2);
  goto L33c8;
L33f8:
  L16(-0xe) = (uint16_t)(di);
  SET32(ax, ax_h, L32(-0x28));
  L32(-0x2c) = (uint32_t)(E32(ax, ax_h));
L3403:
  if ((uint8_t)((uint8_t)(drd8(0x80)) & (uint8_t)(0x10)) == 0) goto L341e;
  { uint32_t r_ = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, (uint16_t)(L32(-0x2c)), (uint16_t)((L32(-0x2c)) >> 16), 0x2252, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L3421;
L341e:
  ax = (uint16_t)(0);
  dx = ((int16_t)ax < 0) ? 0xffff : 0;
L3421:
  si = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) != 0) goto L3441;
  { uint32_t r_ = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, (uint16_t)(L32(-0x2c)), (uint16_t)((L32(-0x2c)) >> 16), 0x216c, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
L3441:
  ax = (uint16_t)(L16(-0x2));
  L16(-0x2c) = (uint16_t)(si);
  L16(-0x2a) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L387b;
  bx = L16(-0x2c);
  es = L16(-0x2a);
  si = (uint16_t)(rd16(es, bx));
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + si + 0xa), (uint8_t)(rd8(es, (uint16_t)(bx + si + 0xa)) | 0x80));
  bx = L16(-0x2c);
  es = L16(-0x2a);
  ax = (uint16_t)(rd16(es, bx));
  L16(-0x80) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(ax);
  bx = (uint16_t)(rd16(es, bx));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(ax, ax_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(ax, ax_h));
  L8(-0x7e) = (uint8_t)((uint8_t)(L8(-0x7e) & 0xf0));
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0x4f)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x40)));
  L16(-0x7e) = (uint16_t)(ax);
L3499:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x84));
  { uint32_t r_ = CALL_ORIG(18, 0x2664, FAR_CDECL, 4, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), ax, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L34a9:
  goto L387b;
L34b0:
  L16(-0xe) = (uint16_t)(di);
  if ((uint16_t)(drd16(0x82)) != (uint16_t)(0x0)) goto L34ce;
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L384e;
L34ce:
  if ((uint32_t)(L32(-0x28)) == (uint32_t)(0x0)) goto L388c;
  SET32(ax, ax_h, 0);
  L32(-0x2c) = (uint32_t)(E32(ax, ax_h));
  L16(-0x10) = (uint16_t)(ax);
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) <= (int16_t)(ax)) goto L353e;
  ax = (uint16_t)(bx);
  ax = (uint16_t)((uint16_t)(ax + 0x6));
  si = (uint16_t)(ax);
  L16(-0x8) = (uint16_t)(es);
L34f5:
  cx = (uint16_t)(0);
  es = L16(-0x8);
  ax = (uint16_t)(rd16(es, si));
  L16(-0x8a) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((int16_t)(ax) <= 0) goto L3525;
  ax = (uint16_t)((uint16_t)(si + 0x2));
  L16(-0x4) = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(es);
  L16(-0xa) = (uint16_t)(si);
  bx = (uint16_t)(ax);
  ds = L16(-0xc);
L3516:
  es = L16(-0x2);
  ax = (uint16_t)(rd16(es, bx));
  di = (uint16_t)(L16(-0xe));
  if ((uint16_t)(rd16(ds, di)) != (uint16_t)(ax)) goto L3574;
L3523:
  /* ds = ss */
L3525:
  if ((int16_t)(L16(-0x8a)) > (int16_t)(cx)) goto L353e;
  si = (uint16_t)((uint16_t)(si + 0x14));
  L16(-0x10) = (uint16_t)((uint16_t)(L16(-0x10) + 1));
  ax = (uint16_t)(L16(-0x10));
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) > (int16_t)(ax)) goto L34f5;
L353e:
  si = (uint16_t)(L16(-0x2c));
  ax = (uint16_t)(L16(-0x10));
  bx = drd16(0x1604);
  es = drd16(0x1606);
  if ((int16_t)(rd16(es, (uint16_t)(bx + 0x2))) <= (int16_t)(ax)) goto L3562;
  di = (uint16_t)((uint16_t)((uint16_t)ax * (uint16_t)0x14));
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(bx + di + 0x4))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
L3562:
  ax = (uint16_t)(L16(-0x2a));
  ax = (uint16_t)((uint16_t)(ax | si));
  if ((uint16_t)(ax) == 0) goto L3580;
  ax = (uint16_t)(L16(-0x2a));
  cx = (uint16_t)(si);
  L16(-0x2) = (uint16_t)(ax);
  goto L3589;
L3574:
  bx = (uint16_t)((uint16_t)(bx + 0x2));
  cx = (uint16_t)((uint16_t)(cx + 1));
  if ((int16_t)(L16(-0x8a)) > (int16_t)(cx)) goto L3516;
  goto L3523;
L3580:
  cx = (uint16_t)(L16(-0x28));
  dx = (uint16_t)(L16(-0x26));
  L16(-0x2) = (uint16_t)(dx);
L3589:
  { uint32_t r_ = CALL_ORIG(18, 0x19c4, FAR_CDECL, 4, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), cx, L16(-0x2)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L34a9;
L359a:
  L16(-0xe) = (uint16_t)(di);
  bx = (uint16_t)(di);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L35c7;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(drd16(0x4c0a)) != (uint16_t)(ax)) goto L35c7;
  if ((int32_t)(drd32(0x4c30)) >= (int32_t)(0x32)) goto L3606;
L35c7:
  if ((uint32_t)(rd32(es, (uint16_t)(bx + 0x58))) != (uint32_t)(0x0)) goto L3606;
  if ((uint16_t)(drd16(0x4b92)) == (uint16_t)(0xffff)) goto L35e8;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(drd16(0x4c0a)) != (uint16_t)(ax)) goto L35e8;
  if ((uint8_t)((uint8_t)(drd8(0x4c0d)) & (uint8_t)(0x2)) != 0) goto L387b;
L35e8:
  if ((uint8_t)(drd8(0x4243)) <= (uint8_t)(0x2)) goto L384e;
  { uint32_t r_ = CALL_ORIG(18, 0x4510, FAR_CDECL, 3, bx, es, 0x0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  ax = (uint16_t)((uint16_t)(ax | ax));
  if ((uint16_t)(ax) != 0) goto L387b;
  goto L384e;
L3606:
  SET32(ax, ax_h, 0);
  L32(-0x32) = (uint32_t)(E32(ax, ax_h));
  { uint32_t r_ = CALL_ORIG(18, 0x98a, FAR_CDECL, 3, bx, es, ax); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L3678;
  ax = (uint16_t)(drd16(0x14c));
  if ((uint16_t)(drd16(0x4c0a)) != (uint16_t)(ax)) goto L3678;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(18, 0x750, FAR_CDECL, 6, 0x1, rd16(es, (uint16_t)(bx + 0x6)), 0x2, rd16(es, bx), 0x3, 0x19); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(es, bx), 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(7, 0x1e4, FAR_CDECL, 1, rd16(es, (uint16_t)(bx + 0x6))); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L367e;
L3678:
  ax = (uint16_t)(L16(-0x28));
  dx = (uint16_t)(L16(-0x26));
L367e:
  di = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  if ((uint16_t)(si) == (uint16_t)(0xffff)) goto L36a6;
  { uint32_t r_ = CALL_ORIG(18, 0x864, FAR_CDECL, 3, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), si); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  di = (uint16_t)(si);
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + di + 0xf), 0x4);
  goto L387b;
L36a6:
  L16(-0x2c) = (uint16_t)(di);
  ax = (uint16_t)(dx);
  ax = (uint16_t)((uint16_t)(ax | di));
  if ((uint16_t)(ax) == 0) goto L3740;
  { uint32_t r_ = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, di, dx, 0x3c82, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L3740;
  es = L16(-0x2);
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  SET32(ax, ax_h, 0);
  wr8(es, (uint16_t)(bx + di + 0xa), (uint8_t)(rd8(es, (uint16_t)(bx + di + 0xa)) | 0x80));
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x84));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  wr16(es, di, ax); di = (uint16_t)(di + 2);
  es = L16(-0x2);
  cx = (uint16_t)(rd16(es, si));
  L16(-0x80) = (uint16_t)(cx);
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) & 0xf1)) << 8));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(cx);
  bx = (uint16_t)(rd16(es, si));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(cx, cx_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(cx, cx_h));
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx & 0xf1)));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx | 0x1)));
  L16(-0x7e) = (uint16_t)(cx);
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  cx = (uint16_t)(L16(-0x7e));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx & 0x6f)));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)((uint8_t)cx | 0x60)));
  L16(-0x7e) = (uint16_t)(cx);
  cx = (uint16_t)(L16(-0x76));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) & 0xf)) << 8));
  cx = (uint16_t)((cx & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(cx >> 8) | 0x20)) << 8));
  L16(-0x76) = (uint16_t)(cx);
  goto L3499;
L3740:
  if ((uint32_t)(L32(-0x32)) == (uint32_t)(0x0)) goto L387b;
  { uint32_t r_ = CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  if ((int16_t)(ax) >= (int16_t)(0xa)) goto L387b;
  { uint32_t r_ = CALL_ORIG(18, 0x8fe, FAR_CDECL, 4, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), (uint16_t)(L32(-0x32)), (uint16_t)((L32(-0x32)) >> 16)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  goto L34a9;
L376a:
  L16(-0xe) = (uint16_t)(di);
  if ((uint16_t)(drd16(0x82)) == (uint16_t)(0x0)) goto L384e;
  bx = L16(-0xe);
  es = L16(-0xc);
  if ((uint16_t)(rd16(es, (uint16_t)(bx + 0x6))) == (uint16_t)(0xffff)) goto L387b;
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = L16(-0xe);
  es = L16(-0xc);
  si = (uint16_t)(rd16(es, (uint16_t)(bx + 0x6)));
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  if ((uint8_t)(rd8(es, (uint16_t)(bx + si + 0x1))) >= (uint8_t)(0x4)) goto L387b;
  { uint32_t r_ = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, 0x4c08, MEM_SEL_DGROUP, 0x3c30, SEG_SEL(17)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  si = (uint16_t)(ax);
  L16(-0x2a) = (uint16_t)(dx);
  dx = (uint16_t)((uint16_t)(dx | ax));
  if ((uint16_t)(dx) == 0) goto L387b;
  SET32(ax, ax_h, 0);
  SET32(cx, cx_h, 0x4);
  di = (uint16_t)((uint16_t)(rt_bp - 0x84));
  es = rt_ss;
  while (cx != 0) { wr32(es, di, E32(ax, ax_h)); di = (uint16_t)(di + 4); cx--; }
  wr16(es, di, ax); di = (uint16_t)(di + 2);
  es = L16(-0x2a);
  ax = (uint16_t)(rd16(es, si));
  L16(-0x80) = (uint16_t)(ax);
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) & 0xf1)) << 8));
  ax = (uint16_t)((ax & 0x00ff) | ((uint16_t)(uint8_t)((uint8_t)((uint8_t)(ax >> 8) | 0x1)) << 8));
  L16(-0x7e) = (uint16_t)(ax);
  bx = (uint16_t)(rd16(es, si));
  bx = (uint16_t)((uint16_t)((uint16_t)bx << 2));
  SET32(ax, ax_h, drd32((uint16_t)(bx + 0x31ba)));
  L32(-0x84) = (uint32_t)(E32(ax, ax_h));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf3)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x3)));
  L16(-0x7e) = (uint16_t)(ax);
  L8(-0x7d) = (uint8_t)((uint8_t)(L8(-0x7d) | 0x10));
  ax = (uint16_t)(L16(-0x7e));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0x6f)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x60)));
  L16(-0x7e) = (uint16_t)(ax);
  ax = (uint16_t)((uint16_t)(rt_bp - 0x84));
  { uint32_t r_ = CALL_ORIG(18, 0x2664, FAR_CDECL, 4, (uint16_t)(L32(-0xe)), (uint16_t)((L32(-0xe)) >> 16), ax, 0x1); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  es = L16(-0x2a);
  di = (uint16_t)(rd16(es, si));
  di = (uint16_t)((uint16_t)((uint16_t)di << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + di + 0x1), (uint8_t)(rd8(es, (uint16_t)(bx + di + 0x1)) | 0x80));
  bx = L16(-0xe);
  es = L16(-0xc);
  si = (uint16_t)(rd16(es, (uint16_t)(bx + 0x6)));
  si = (uint16_t)((uint16_t)((uint16_t)si << 4));
  bx = drd16(0x1600);
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + si + 0x1), (uint8_t)(rd8(es, (uint16_t)(bx + si + 0x1)) & 0x80));
  goto L387b;
L384e:
  bx = L16(-0xe);
  es = L16(-0xc);
  { uint32_t r_ = CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 0x2, rd16(es, bx)); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  bx = drd16(0x4bf0);
  es = drd16(0x4bf2);
  ax = (uint16_t)(rd16(es, (uint16_t)(bx + 0xa)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax & 0xf5)));
  ax = (uint16_t)((ax & 0xff00) | (uint8_t)((uint8_t)((uint8_t)ax | 0x5)));
  wr16(es, (uint16_t)(bx + 0xa), ax);
L386e:
  { uint32_t r_ = CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
L387b:
  L16(-0x2e) = (uint16_t)((uint16_t)(L16(-0x2e) + 0x4));
  ax = (uint16_t)(drd16(0x553a));
  L16(-0x12) = (uint16_t)((uint16_t)(L16(-0x12) + 1));
  if ((int16_t)(L16(-0x12)) < (int16_t)(ax)) goto L31f0;
L388c:
  ax = (uint16_t)((uint16_t)(rt_bp - 0x72));
  t87 = ax;
  ax = (uint16_t)((uint16_t)(rt_bp - 0x62));
  { uint32_t r_ = CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-0x86), buf, L16(-0x14), ax, t87); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  { uint32_t r_ = far_call(21, 0x1e2a, NULL, 0); ax = (uint16_t)r_; dx = (uint16_t)(r_ >> 16); }
  FRAME_LEAVE();
  return;
}
