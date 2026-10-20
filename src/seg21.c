/* Decompiled code segment 21 (NE segment 21, selector 0x00A8).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg21.c; Ghidra's pseudo-C in re/decomp/seg21 is only a reading aid.
 *
 * Segment 21 is the older computer-player turn ("AI") together with its helpers:
 *   21:0x0000  the turn of a computer player (scores, planet and fleet walks, orders)
 *   21:0x123e  walk the fleets and spend the cargo of the order lists (uses the C runtime's pow)
 *   21:0x1aec  pick a planet near a given one (direction switch plus random jitter)
 *   21:0x1d76  does a fleet's cargo differ enough from its capacity
 *   21:0x1e2a, 21:0x1e8a  the final walk over the order list and the colonist/cargo shipments
 *   21:0x23d0  react to one fleet order (find a target with the callbacks 0x272c, 0x27ba, 0x2858)
 *   21:0x272c, 0x27ba, 0x2858, 0x2882, 0x29da, 0x2bd8  callbacks handed to 18:0x3922 (a search)
 *   21:0x2bf8  a short helper that queues one order
 *   21:0x2c74  queue the messages of the techs the computer player has reached
 *   21:0x30c2, 21:0x32aa  pick one of three target planets / one fleet order
 * The code segment also holds data: a byte table at 21:0x109e (passed to 18:0x28fc) and the string
 * pool 21:0x10c8 to 21:0x11f4 with the word tables 21:0x11f5 to 21:0x123d (string offsets) that
 * 21:0x2c74 reads through CS; they are copied below as constant tables.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/fp80.h"
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
static inline __attribute__((unused)) void wr32(uint16_t seg, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 4); }
/* DGROUP (DS) bytes, words and dwords. */
static inline __attribute__((unused)) uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline __attribute__((unused)) void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline __attribute__((unused)) void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }
static inline double drd_f64(uint16_t off) {
  double d;
  memcpy(&d, LIN(MK_FP(MEM_SEL_DGROUP, off)), 8);
  return d;
}

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
#define LF64(o) BP_LOCAL(double, (o))

/* The two DGROUP tables the callbacks index: DS:0x31ba is the planet coordinate table (4 bytes per
 * planet id: x, y words), the far pointer DS:0x1604/0x1606 the per-planet flag table. */
#define PLANET_XY(id) drd32((uint16_t)(((uint16_t)(id) << 2) + 0x31ba))

/* IDIV r/m32 and IDIV r/m16: #DE when the divisor is 0 or the quotient does not fit. The original
 * traps there; the recorded-error hook is a hard stop here (rule b). */
static __attribute__((unused)) int32_t idiv32(int32_t a, int32_t b) {
  if (b == 0 || (a == INT32_MIN && b == -1)) {
    fprintf(stderr, "seg21: divide error (idiv32 %d / %d)\n", (int32_t)a, (int32_t)b);
    abort();
  }
  return a / b;
}
static __attribute__((unused)) int16_t idiv16(int32_t a, int16_t b) {
  if (b == 0 || a / b > 32767 || a / b < -32768) {
    fprintf(stderr, "seg21: divide error (idiv16 %d / %d)\n", (int32_t)a, (int32_t)b);
    abort();
  }
  return (int16_t)(a / b);
}

/* 21:0x2bd8 (Ghidra FUN_10a0_2bd8), 32 bytes, a leaf. PUSH BP; MOV BP,SP (no locals). FAR CDECL,
 * two far pointers (the first, at [BP+6], is not read; the second at [BP+0xA] is the record). A
 * filter callback of 18:0x3922: 1 when the record's owner word (+2) is neither the current player
 * (DS:0x14c) nor 0xffff, else 0. AX only. */
DECOMP(21, 0x2bd8, fn_21_2bd8, FAR_CDECL)
uint16_t fn_21_2bd8(uint32_t unused, uint32_t rec)
{
  (void)unused;
  uint16_t owner = rd16((uint16_t)(rec >> 16), (uint16_t)((uint16_t)rec + 2));
  if (owner == drd16(0x014c)) return 0;
  if (owner == 0xffff) return 0;
  return 1;
}

/* 21:0x2858 (Ghidra FUN_10a0_2858), 42 bytes, a leaf. FAR CDECL (far pointer ignored, far pointer
 * to the record at [BP+0xA]). A filter callback: 1 when the record belongs to the current player
 * (+2 == DS:0x14c), the signed dword at +0x28 is above 0x898 and bit 1 of the byte +5 is set. */
DECOMP(21, 0x2858, fn_21_2858, FAR_CDECL)
uint16_t fn_21_2858(uint32_t unused, uint32_t rec)
{
  (void)unused;
  uint16_t seg = (uint16_t)(rec >> 16), off = (uint16_t)rec;
  if (rd16(seg, (uint16_t)(off + 2)) != drd16(0x014c)) return 0;
  if ((int32_t)rd32(seg, (uint16_t)(off + 0x28)) <= 0x898) return 0;
  if ((rd8(seg, (uint16_t)(off + 5)) & 2) == 0) return 0;
  return 1;
}

/* 21:0x272c (Ghidra FUN_10a0_272c), 141 bytes. ENTER 6,0; PUSH SI (non-leaf: 8:0xbc50).
 * FAR CDECL (a at [BP+6], b at [BP+0xA], both far pointers to planet records). A filter callback
 * of 18:0x3922. With tab = DS:0x1604/0x1606 plus 2 * (DS:0x7a + 1) (the offset wraps in 16 bits),
 * flags = tab[2 * b.id]: 0 when (flags & 0x18) == 0x18; 0 unless b.owner (+2) == DS:0x14c, the signed
 * dword b+0x28 < 200 and (flags & 0x18) == 0; otherwise 1 when the squared distance (8:0xbc50 of
 * the DS:0x31ba entries of a and b, DX:AX) is at most 0x27100 (DX compared signed, AX unsigned). */
DECOMP(21, 0x272c, fn_21_272c, FAR_CDECL)
uint16_t fn_21_272c(uint32_t a, uint32_t b)
{
  FRAME_ENTER(6 + 2);
  uint16_t bseg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t aseg = (uint16_t)(a >> 16), aoff = (uint16_t)a;
  uint16_t ax, dx;
  uint32_t r;
  L16(-2) = (uint16_t)((uint16_t)(drd16(0x007a) + 1) * 2);
  L32(-6) = drd32(0x1604);
  L16(-6) = (uint16_t)(L16(-6) + L16(-2));
  {
    uint16_t bx = (uint16_t)(rd16(bseg, boff) * 2);
    if ((rd8(L16(-4), (uint16_t)(bx + L16(-6))) & 0x18) == 0x18) goto L0;
  }
  if (rd16(bseg, (uint16_t)(boff + 2)) != drd16(0x014c)) goto L0;
  if ((int32_t)rd32(bseg, (uint16_t)(boff + 0x28)) >= 200) goto L0;
  {
    uint16_t bx = (uint16_t)(rd16(bseg, boff) * 2);
    if ((rd8(L16(-4), (uint16_t)(bx + L16(-6))) & 0x18) != 0) goto L0;
  }
  {
    uint32_t cb = PLANET_XY(rd16(bseg, boff));
    uint32_t ca = PLANET_XY(rd16(aseg, aoff));
    r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
  }
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  if ((int16_t)dx > 2) goto L0;
  if ((int16_t)dx < 2) goto L1;
  if (ax > 0x7100) goto L0;
L1:
  FRAME_LEAVE();
  return 1;
L0:
  FRAME_LEAVE();
  return 0;
}

/* 21:0x27ba (Ghidra FUN_10a0_27ba), 157 bytes. ENTER 6,0; PUSH SI (non-leaf: 8:0xbc50).
 * FAR CDECL (a at [BP+6], b at [BP+0xA]). Like 21:0x272c with the same flag table: 0 when
 * (flags & 0x18) == 0x18 or b.owner != DS:0x14c; then w = ((flags & 0x18) >> 3) * 0xd2 as an 8-bit
 * MUL (AL * 0xd2, 16-bit result) added to the dword b+0x28: 0 unless the sum is below 1000 (DX
 * signed, AX unsigned); finally 1 when the squared distance is at most 0x27100 as in 21:0x272c. */
DECOMP(21, 0x27ba, fn_21_27ba, FAR_CDECL)
uint16_t fn_21_27ba(uint32_t a, uint32_t b)
{
  FRAME_ENTER(6 + 2);
  uint16_t bseg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t aseg = (uint16_t)(a >> 16), aoff = (uint16_t)a;
  uint16_t ax, dx;
  uint32_t r, sum;
  L16(-2) = (uint16_t)((uint16_t)(drd16(0x007a) + 1) * 2);
  L32(-6) = drd32(0x1604);
  L16(-6) = (uint16_t)(L16(-6) + L16(-2));
  {
    uint16_t bx = (uint16_t)(rd16(bseg, boff) * 2);
    if ((rd8(L16(-4), (uint16_t)(bx + L16(-6))) & 0x18) == 0x18) goto L0;
  }
  if (rd16(bseg, (uint16_t)(boff + 2)) != drd16(0x014c)) goto L0;
  {
    uint16_t bx = (uint16_t)(rd16(bseg, boff) * 2);
    uint8_t al = (uint8_t)((rd8(L16(-4), (uint16_t)(bx + L16(-6))) & 0x18) >> 3);
    uint16_t mul = (uint16_t)(al * 0xd2);
    sum = (uint32_t)mul + rd32(bseg, (uint16_t)(boff + 0x28));
  }
  dx = (uint16_t)(sum >> 16);
  ax = (uint16_t)sum;
  if ((int16_t)dx > 0) goto L0;
  if ((int16_t)dx >= 0 && ax >= 1000) goto L0;
  {
    uint32_t cb = PLANET_XY(rd16(bseg, boff));
    uint32_t ca = PLANET_XY(rd16(aseg, aoff));
    r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
  }
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  if ((int16_t)dx > 2) goto L0;
  if ((int16_t)dx < 2) goto L1;
  if (ax > 0x7100) goto L0;
L1:
  FRAME_LEAVE();
  return 1;
L0:
  FRAME_LEAVE();
  return 0;
}

/* FICOM m32 followed by FNSTSW AX; SAHF; JB: true when ST(0) < v (CF = C0) or the operands are
 * unordered (C0 is set then too). The value ST(0) is popped by the caller's FSTP ST(0). */
static bool ficom_below(Fp80 st, int32_t v)
{
  return !extF80_le(fp80_from_i32(v), st);
}

/* 21:0x2882 (Ghidra FUN_10a0_2882), 344 bytes. ENTER 0x2c,0; PUSH DI; PUSH SI (non-leaf: 9:0x4f76,
 * 8:0xbc50, x87). FAR CDECL (a at [BP+6], b at [BP+0xA], far pointers to fleet records). A filter
 * callback of 18:0x3922: 1 when b is the current player's (+2 == DS:0x14c) with bit 1 of +5 set and
 * flags1 = tab1[2 * b.id] has no bit 5/6 (& 0x60), a has more than 0x2bc of cargo kind 1, 2 or 3 (signed
 * dwords at +0x1c, +0x20, +0x24) for which b's second table byte tab2[2 * b.id + 1] has bit 0, 1
 * or 2 set (tab1 = DS:0x1604 + 2, tab2 = tab1 + 2 * DS:0x7a), and t * t >= the squared distance
 * (8:0xbc50) where n = min(9:0x4f76(a) + carry, 9:0x4f76(b) + carry) and t = n * n * K (K = the
 * double at DS:0x20bc); the x87 steps are FILD, FST, FMUL, FMUL, FST, FMUL, FSTP and the final
 * FICOM against the int32 distance. The local at BP-0xa receives the second result of 9:0x4f76. */
DECOMP(21, 0x2882, fn_21_2882, FAR_CDECL)
uint16_t fn_21_2882(uint32_t a, uint32_t b)
{
  FRAME_ENTER(0x2c + 4);
  uint16_t aseg = (uint16_t)(a >> 16), aoff = (uint16_t)a;
  uint16_t bseg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t si, di, bx, ax, dx;
  uint32_t r;
  Fp80 st;

  dx = drd16(0x1606);
  si = (uint16_t)(drd16(0x1604) + 2);
  L16(-6) = dx;
  bx = (uint16_t)((uint16_t)(drd16(0x007a) * 2) + drd16(0x1604) + 2);
  L16(-2) = dx;
  L16(-0xc) = (uint16_t)(rd8(bseg, (uint16_t)(boff + 0x2c)) & 0xf);
  if (rd16(bseg, (uint16_t)(boff + 2)) != drd16(0x014c)) goto Lfail;
  if ((rd8(bseg, (uint16_t)(boff + 5)) & 2) == 0) goto Lfail;
  di = (uint16_t)(rd16(bseg, boff) * 2);
  L16(-0xe) = di;
  di = (uint16_t)(di + si);
  if ((rd8(dx, di) & 0x60) != 0) goto Lfail;
  if ((int32_t)rd32(aseg, (uint16_t)(aoff + 0x1c)) <= 0x2bc) goto L28fb;
  if ((rd8(L16(-2), (uint16_t)((uint16_t)(L16(-0xe) + bx) + 1)) & 1) != 0) goto L2937;
L28fb:
  if ((int32_t)rd32(aseg, (uint16_t)(aoff + 0x20)) <= 0x2bc) goto L2918;
  if ((rd8(L16(-2), (uint16_t)((uint16_t)(L16(-0xe) + bx) + 1)) & 2) != 0) goto L2937;
L2918:
  if ((int32_t)rd32(aseg, (uint16_t)(aoff + 0x24)) <= 0x2bc) goto Lfail;
  bx = (uint16_t)(bx + L16(-0xe));
  if ((rd8(L16(-2), (uint16_t)(bx + 1)) & 4) == 0) goto Lfail;
L2937:
  si = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, aoff, aseg, (uint16_t)(rt_bp - 0xa));
  if (L16(-0xa) != 0) si++;
  di = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, boff, bseg, (uint16_t)(rt_bp - 0xa));
  if (L16(-0xa) != 0) di++;
  ax = si;
  if ((int16_t)si > (int16_t)di) ax = di;
  L16(-0x10) = ax;
  st = fp80_from_i32((int16_t)L16(-0x10));
  LF64(-0x18) = fp80_to_f64(st);
  st = fp80_mul(st, fp80_from_f64(LF64(-0x18)));
  st = fp80_mul(st, fp80_from_f64(drd_f64(0x20bc)));
  LF64(-0x20) = fp80_to_f64(st);
  st = fp80_mul(st, fp80_from_f64(LF64(-0x20)));
  LF64(-0x28) = fp80_to_f64(st);
  {
    uint32_t cb = PLANET_XY(rd16(bseg, boff));
    uint32_t ca = PLANET_XY(rd16(aseg, aoff));
    r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
  }
  st = fp80_from_f64(LF64(-0x28));
  L16(-0x2a) = (uint16_t)(r >> 16);
  L16(-0x2c) = (uint16_t)r;
  if (ficom_below(st, (int32_t)L32(-0x2c))) goto Lfail;
  FRAME_LEAVE();
  return 1;
Lfail:
  FRAME_LEAVE();
  return 0;
}

/* 21:0x29da (Ghidra FUN_10a0_29da), 510 bytes. ENTER 0x30,0; PUSH DI; PUSH SI (non-leaf: 28:0x21ee,
 * 9:0x4f76, 7:0x28ea, the C runtime's pow 34:0x0d84 and ftol 34:0x0de6, x87).
 * FAR CDECL (a at [BP+6], b at [BP+0xA], far pointers to fleet records). A filter callback of
 * 18:0x3922: 0 when b belongs to the current player or to nobody (+2 == DS:0x14c or 0xffff), when
 * tab1[2 * b.id] & 0x60 (tab1 = DS:0x1604 + 2), when 28:0x21ee(its player record, 0xe) is 8, when b has
 * bit 1 of +5 set and the byte of its design (hull class +0x7b) is not 7; si = 9:0x4f76(a) + 3 and
 * di = 9:0x4f76(b) + carry (0 when b has no bit 1 of +5); 0 when they are equal. D = the double
 * 7:0x28ea returns a pointer to (the distance), T = (double)(si * si); 0 when 2.5 * T < D. Then
 * V = ftol(pow(0.875 or 0.75, D / T) * the int32 at tab3) (the base is 0.875 when the carry of the
 * second 9:0x4f76 call is not zero, tab3 = DS:0x1604 + 4 * DS:0x7a + 2), 0 when (b.w12 >> 12) > 99 or
 * (b.w12 & 0xfff) == 0, m = min((b.w12 & 0xfff) + 0x19) * 4, 0x3e8) (an unsigned 16-bit minimum) and
 * Y = the 16-bit product (si * si - di * di) * (0x5f - (b.w12 >> 12)) (only its low word is stored
 * into the 64-bit FILD operand); the result is 1 when ftol(m / (Y * 6.25e-5)) <= V (a signed
 * 32-bit compare), else 0. A zero Y would divide by zero (a trap in the original, not modelled). */
DECOMP(21, 0x29da, fn_21_29da, FAR_CDECL)
uint16_t fn_21_29da(uint32_t a, uint32_t b)
{
  FRAME_ENTER(0x30 + 4);
  uint16_t aseg = (uint16_t)(a >> 16), aoff = (uint16_t)a;
  uint16_t bseg = (uint16_t)(b >> 16), boff = (uint16_t)b;
  uint16_t si = boff, di = 0, cx, dx, ax, bx, es, esoff;
  uint32_t r;
  Fp80 st, st1;
  (void)aseg;
  (void)aoff;

  ax = drd16(0x1604);
  dx = drd16(0x1606);
  ax = (uint16_t)(ax + 2);
  cx = ax;
  L16(-4) = dx;
  L16(-0x14) = (uint16_t)((uint16_t)(drd16(0x007a) << 2) + drd16(0x1604) + 2);
  L16(-0x12) = dx;
  es = bseg;
  if (rd16(es, (uint16_t)(si + 2)) == drd16(0x014c)) goto Lfail;
  if (rd16(es, (uint16_t)(si + 2)) == 0xffff) goto Lfail;
  bx = (uint16_t)((uint16_t)(rd16(es, si) * 2) + cx);
  if ((rd8(dx, bx) & 0x60) != 0) goto Lfail;
  ax = (uint16_t)(rd16(bseg, (uint16_t)(si + 2)) * 0xc0 + 0x5a64);
  ax = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, ax, 0xe);
  if (ax == 8) goto Lfail;
  es = bseg;
  if ((rd8(es, (uint16_t)(si + 5)) & 2) == 0) goto L2a90;
  ax = (uint16_t)(rd8(es, (uint16_t)(si + 0x2c)) & 0xf);
  bx = (uint16_t)(ax * 0x8d);
  di = (uint16_t)(rd16(es, (uint16_t)(si + 2)) << 2);
  esoff = drd16((uint16_t)(di + 0x10c));
  es = drd16((uint16_t)(di + 0x10e));
  di = esoff;
  if (rd8(es, (uint16_t)(bx + di + 0x7b)) != 7) goto Lfail;
  di = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, boff, bseg, (uint16_t)(rt_bp - 0x1e));
  if (L16(-0x1e) != 0) di++;
L2a90:
  si = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, aoff, aseg, (uint16_t)(rt_bp - 0x1e));
  si = (uint16_t)(si + 3);
  L16(-0x20) = si;
  LF64(-0x1c) = fp80_to_f64(fp80_from_i32((int16_t)L16(-0x20)));
  if (si == di) goto Lfail;
  L16(-4) = si;
  L16(-2) = di;
  {
    uint32_t cb = PLANET_XY(rd16(bseg, boff));
    uint32_t ca = PLANET_XY(rd16(aseg, aoff));
    ax = (uint16_t)CALL_ORIG(7, 0x28ea, FAR_CDECL, 4, (uint16_t)ca, (uint16_t)(ca >> 16), (uint16_t)cb, (uint16_t)(cb >> 16));
  }
  bx = ax;
  st = fp80_from_f64(drd_f64(bx));
  LF64(-0xc) = fp80_to_f64(st);
  st1 = st;
  st = fp80_from_f64(LF64(-0x1c));
  st = fp80_mul(st, fp80_from_f64(LF64(-0x1c)));
  LF64(-0x1c) = fp80_to_f64(st);
  st = fp80_mul(st, fp80_from_f64(drd_f64(0x20c4)));
  if (!extF80_le(st1, st)) goto Lfail; /* FCOM ST(1); JB: ST0 < D or unordered */
  if (L16(-0x1e) != 0) st = fp80_from_f64(drd_f64(0x20cc));
  else st = fp80_from_f64(drd_f64(0x20d4));
  st1 = st; /* the base */
  st = fp80_div(fp80_from_f64(LF64(-0xc)), fp80_from_f64(LF64(-0x1c)));
  st = crt_pow_34_0d84(st1, st);
  st = fp80_mul(st, fp80_from_i32((int32_t)rd32(L16(-0x12), L16(-0x14))));
  r = (uint32_t)fp80_to_i32_ftol(st);
  L16(-0x10) = (uint16_t)r;
  L16(-0xe) = (uint16_t)(r >> 16);
  if ((rd16(bseg, (uint16_t)(boff + 0x12)) >> 12) > 0x63) goto Lfail;
  if ((rd16(bseg, (uint16_t)(boff + 0x12)) & 0xfff) == 0) goto Lfail;
  ax = rd16(bseg, (uint16_t)(boff + 0x12));
  cx = ax;
  ax = (uint16_t)((ax & 0x0fff) + 0x19);
  ax = (uint16_t)(ax << 2);
  {
    uint16_t sub = (uint16_t)(ax - 0x3e8);
    dx = (uint16_t)(ax < 0x3e8 ? 0xffff : 0); /* SBB DX,DX after the compare */
    ax = (uint16_t)(sub & dx);
    ax = (uint16_t)(ax + 0x3e8);
  }
  L32(-0x28) = ax;
  L32(-0x24) = 0;
  st = fp80_from_i32((int32_t)(uint16_t)L16(-0x28)); /* FILD qword [BP-0x28] (high dwords are 0) */
  {
    uint16_t dd = L16(-2), ss = L16(-4);
    uint16_t sq_di = (uint16_t)(dd * dd);
    uint16_t sq_si = (uint16_t)(ss * ss);
    uint16_t diff = (uint16_t)(sq_si - sq_di);
    uint16_t k = (uint16_t)(0x5f - (cx >> 12));
    uint32_t prod = (uint32_t)diff * k;
    L32(-0x30) = (uint16_t)prod; /* SUB EDX,EDX clears the high half before it is stored */
    L32(-0x2c) = 0;
    st1 = fp80_from_i32((int32_t)(uint16_t)L16(-0x30));
  }
  st1 = fp80_mul(st1, fp80_from_f64(drd_f64(0x2094)));
  st = fp80_div(st, st1);
  r = (uint32_t)fp80_to_i32_ftol(st);
  dx = (uint16_t)(r >> 16);
  ax = (uint16_t)r;
  if ((int16_t)dx > (int16_t)L16(-0xe)) goto Lfail;
  if ((int16_t)dx < (int16_t)L16(-0xe)) goto Lok;
  if (ax > L16(-0x10)) goto Lfail;
Lok:
  FRAME_LEAVE();
  return 1;
Lfail:
  FRAME_LEAVE();
  return 0;
}

/* 21:0x1d76 (Ghidra FUN_10a0_1d76), 180 bytes. ENTER 0x24,0; PUSH DI; PUSH SI (non-leaf: 18:0x3558,
 * 18:0x3600, 18:0x2732). FAR CDECL (fleet, a far pointer at [BP+6]). 18:0x3558 fills the four dwords
 * A[0..3] at BP-0x10 and 18:0x3600 the four dwords B[0..3] at BP-0x20 (the amounts of the fleet and its
 * capacity, per kind); the differences A[k] - B[k] are signed 32-bit values. si is the index of the
 * smallest of the first two (1 when A[0]-B[0] < A[1]-B[1]), then 2 when the difference at si is below
 * A[2]-B[2]. When A[si] - B[si] is below 0xaa (a signed compare) the answer is 0, else the order
 * 18:0x2732(si + 0xe, 1, 1 (a dword)) is queued and the answer is 1. AX only. */
DECOMP(21, 0x1d76, fn_21_1d76, FAR_CDECL)
uint16_t fn_21_1d76(uint32_t fleet)
{
  FRAME_ENTER(0x24 + 4);
  uint16_t di = (uint16_t)fleet, cx = (uint16_t)(fleet >> 16), si = 0;
  int32_t d0, d1, d2, dsi;
  L16(-0x24) = di;
  L16(-0x22) = cx;
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, di, cx, (uint16_t)(rt_bp - 0x10));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, L16(-0x24), L16(-0x22), (uint16_t)(rt_bp - 0x20));
  d1 = (int32_t)(L32(-0xc) - L32(-0x1c));
  d0 = (int32_t)(L32(-0x10) - L32(-0x20));
  if (d0 < d1) si = 1;
  di = (uint16_t)(si << 2);
  dsi = (int32_t)(L32((int16_t)(di - 0x10)) - L32((int16_t)(di - 0x20)));
  d2 = (int32_t)(L32(-8) - L32(-0x18));
  if (dsi < d2) si = 2;
  di = (uint16_t)(si << 2);
  if ((int32_t)(L32((int16_t)(di - 0x10)) - L32((int16_t)(di - 0x20))) < 0xaa) {
    FRAME_LEAVE();
    return 0;
  }
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)(si + 0xe), 1, 1, 0);
  FRAME_LEAVE();
  return 1;
}

/* 21:0x2bf8 (Ghidra FUN_10a0_2bf8), 124 bytes. ENTER 0x100,0; PUSH SI (non-leaf: 9:0x474c, 9:0x2a74,
 * 26:0x0100, 18:0x50c8, 18:0x50a0). FAR CDECL (rec, a far pointer to a fleet record at [BP+6]).
 * Returns 0xffff unless bit 1 of byte +5 is clear, 9:0x474c(rec, DS:0x14c) is at least 15 (a signed
 * compare) and the signed dword +0x28 is at least 0x1f4. Otherwise it queues 9:0x2a74(1, rec.id), calls
 * 26:0x0100 with the 0x100-byte local buffer at BP-0x100, and returns the result of 18:0x50c8 when the
 * three bytes +9, +0xa and +0xb are all above 0xf, else the result of 18:0x50a0. */
DECOMP(21, 0x2bf8, fn_21_2bf8, FAR_CDECL)
uint16_t fn_21_2bf8(uint32_t rec)
{
  FRAME_ENTER(0x100 + 2);
  uint16_t si = (uint16_t)rec, es = (uint16_t)(rec >> 16), ax;
  uint16_t r;
  if ((rd8(es, (uint16_t)(si + 5)) & 2) != 0) goto Lm1;
  ax = (uint16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, si, es, drd16(0x014c));
  if ((int16_t)ax < 0xf) goto Lm1;
  if ((int32_t)rd32(es, (uint16_t)(si + 0x28)) < 0x1f4) goto Lm1;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, rd16(es, si));
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x100));
  if (rd8(es, (uint16_t)(si + 9)) <= 0xf) goto L2c66;
  if (rd8(es, (uint16_t)(si + 0xa)) <= 0xf) goto L2c66;
  if (rd8(es, (uint16_t)(si + 0xb)) <= 0xf) goto L2c66;
  r = (uint16_t)CALL_ORIG(18, 0x50c8, FAR_CDECL, 0);
  FRAME_LEAVE();
  return r;
L2c66:
  r = (uint16_t)CALL_ORIG(18, 0x50a0, FAR_CDECL, 0);
  FRAME_LEAVE();
  return r;
Lm1:
  FRAME_LEAVE();
  return 0xffff;
}

/* 21:0x30c2 (Ghidra FUN_10a0_30c2), 488 bytes. ENTER 6,0; PUSH DI; PUSH SI (non-leaf: 8:0x8730,
 * 9:0x4a62, 9:0x4b0c, 9:0x4c64, 9:0x4b8e, 18:0x2732). FAR CDECL (fleet, a far pointer at [BP+6]; a
 * word at [BP+0xA] that is not read; b at [BP+0xC], c at [BP+0xE] and d at [BP+0x10], planet ids or -1).
 * r1 = random(100). e1 = 9:0x4a62(fleet, DS:0x14c, 0) - 9:0x4b0c(fleet) is stored at BP-2 (0 when it is
 * negative, in which case the second pair of calls is skipped), e2 = 9:0x4c64(fleet, DS:0x14c, 0) -
 * 9:0x4b8e(fleet) (0 when negative). With both e1 and e2 at least 100 the threshold is 0x3c, else 0x5a;
 * the answer is 0 when random(100) is below it. Then: with c != -1 and r1 > 0x32 it queues
 * 18:0x2732(c * 4 + 6, 2, 0x10002) when bit 1 of the byte DS:0x4544 + 0x234 * c is clear, the same
 * with + 7 for DS:0x45d1, with (c + 2) * 4 and 1 (when random(100) < 0x4b) for DS:0x465e and with
 * c * 4 + 9 and 1 (when random(100) < 0x32) for DS:0x46eb, and returns 1; else with d != -1 and
 * r1 > 0x19 it queues 18:0x2732(d, 1, 0x10002) and returns 2; else with b != -1 it queues
 * 18:0x2732(b, 1, 0x10002) and returns 3; else 0. */
DECOMP(21, 0x30c2, fn_21_30c2, FAR_CDECL)
uint16_t fn_21_30c2(uint32_t fleet, uint16_t unused, int16_t b, int16_t c, int16_t d)
{
  FRAME_ENTER(6 + 4);
  uint16_t si = (uint16_t)fleet, hi = (uint16_t)(fleet >> 16), di, ax, dx;
  (void)unused;
  L16(-4) = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  di = (uint16_t)CALL_ORIG(9, 0x4a62, FAR_CDECL, 4, si, hi, drd16(0x014c), 0);
  ax = (uint16_t)CALL_ORIG(9, 0x4b0c, FAR_CDECL, 2, si, hi);
  di = (uint16_t)(di - ax);
  if ((int16_t)di >= 0) goto L3104;
  L16(-2) = 0;
  goto L3129;
L3104:
  di = (uint16_t)CALL_ORIG(9, 0x4a62, FAR_CDECL, 4, si, hi, drd16(0x014c), 0);
  ax = (uint16_t)CALL_ORIG(9, 0x4b0c, FAR_CDECL, 2, si, hi);
  di = (uint16_t)(di - ax);
  L16(-2) = di;
L3129:
  di = (uint16_t)CALL_ORIG(9, 0x4c64, FAR_CDECL, 4, si, hi, drd16(0x014c), 0);
  ax = (uint16_t)CALL_ORIG(9, 0x4b8e, FAR_CDECL, 2, si, hi);
  di = (uint16_t)(di - ax);
  if ((int16_t)di >= 0) goto L3152;
  di = 0;
  goto L3174;
L3152:
  di = (uint16_t)CALL_ORIG(9, 0x4c64, FAR_CDECL, 4, (uint16_t)fleet, (uint16_t)(fleet >> 16), drd16(0x014c), 0);
  ax = (uint16_t)CALL_ORIG(9, 0x4b8e, FAR_CDECL, 2, (uint16_t)fleet, (uint16_t)(fleet >> 16));
  di = (uint16_t)(di - ax);
L3174:
  if ((int16_t)L16(-2) < 0x64) goto L3184;
  if ((int16_t)di < 0x64) goto L3184;
  si = 0x3c;
  goto L3187;
L3184:
  si = 0x5a;
L3187:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax < (int16_t)si) goto L32a4;
  si = (uint16_t)c;
  dx = L16(-4);
  if ((int16_t)si == -1) goto L325e;
  if ((int16_t)dx <= 0x32) goto L325e;
  L16(-6) = (uint16_t)((int16_t)si * 0x234);
  if ((drd8((uint16_t)(L16(-6) + 0x4544)) & 2) != 0) goto L31d2;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)((si << 2) + 6), 2, 2, 1);
L31d2:
  if ((drd8((uint16_t)(L16(-6) + 0x45d1)) & 2) != 0) goto L31f5;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)((si << 2) + 7), 2, 2, 1);
L31f5:
  if ((drd8((uint16_t)(L16(-6) + 0x465e)) & 2) != 0) goto L3225;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax >= 0x4b) goto L3225;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)((si + 2) << 2), 1, 2, 1);
L3225:
  if ((drd8((uint16_t)(L16(-6) + 0x46eb)) & 2) != 0) goto L3257;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax >= 0x32) goto L3257;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)((si << 2) + 9), 1, 2, 1);
L3257:
  FRAME_LEAVE();
  return 1;
L325e:
  if (d == -1) goto L3284;
  if ((int16_t)dx <= 0x19) goto L3284;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)d, 1, 2, 1);
  FRAME_LEAVE();
  return 2;
L3284:
  if (b == -1) goto L32a4;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)b, 1, 2, 1);
  FRAME_LEAVE();
  return 3;
L32a4:
  FRAME_LEAVE();
  return 0;
}

/* 21:0x1aec (Ghidra FUN_10a0_1aec), 225 bytes. ENTER 0x20,0; PUSH SI (non-leaf: 9:0x4f76, 8:0x8730,
 * 8:0x8730, 34:0x0de6, 7:0x294e, 7:0x01e4, 8:0xbc50, x87). FAR CDECL (rec, a far pointer to a fleet
 * record at [BP+6]; dir at [BP+0xA], a direction code 0 to 7 handled by a jump table at 21:0x1bce,
 * anything above 7 returns 0xffff). X, Y (words at BP-4, BP-2) start as the coordinates of the fleet
 * (DS:0x31ba[rec.id]) minus 1000 and S (BP-8) is (DS:0x74 + 1) * 400. The direction moves (X, Y) to a
 * corner or an edge of the square 0..S (the cases of the table write X or Y as S or 0 and carry the
 * rest over to the other coordinate), then both are jittered: R = random(ftol(S * 0.3)) -
 * ftol(S * 0.15) moves X or Y along the edge it is on (clamped to 0..S with the overflow carried over),
 * and a second R = random(N2), N2 = (9:0x4f76(rec) + 3) squared (16 bits), is added to or subtracted
 * from the coordinate that sits at 0 or S. After adding 1000 back, 7:0x294e(X, Y, 0x21, the 16-byte
 * answer at BP-0x18) finds a planet (its id is the word at BP-0x10); the result is -1 when it finds
 * none, when 7:0x01e4(id) returns a planet of the current player, or when (int16)(N2 * N2) is above
 * the squared distance (8:0xbc50) between the fleet and the planet; else id + 1. The final product
 * keeps only the sign extension of AX (CWD after IMUL). */
DECOMP(21, 0x1aec, fn_21_1aec, FAR_CDECL)
uint16_t fn_21_1aec(uint32_t rec, uint16_t dir)
{
  FRAME_ENTER(0x20 + 2);
  uint16_t seg = (uint16_t)(rec >> 16), off = (uint16_t)rec;
  uint16_t ax, dx, bx;
  uint32_t r;
  Fp80 st;

  L32(-4) = PLANET_XY(rd16(seg, off));
  L16(-8) = (uint16_t)((uint16_t)(drd16(0x0074) + 1) * 0x190);
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, off, seg, 0);
  ax = (uint16_t)(ax + 3);
  L16(-0x1e) = ax;
  L16(-0x20) = (uint16_t)(ax * ax);
  L16(-4) = (uint16_t)(L16(-4) - 0x3e8);
  L16(-2) = (uint16_t)(L16(-2) - 0x3e8);
  if (dir > 7) goto Lm1;
  switch (dir) {
    case 0: goto L1b57;
    case 1: goto L1b3a;
    case 2: goto L1b6e;
    case 3: goto L1b60;
    case 4: goto L1b79;
    case 5: goto L1b80;
    case 6: goto L1b9d;
    default: goto L1ba6;
  }
L1b3a:
  ax = (uint16_t)(L16(-8) - L16(-4));
  if ((int16_t)ax <= (int16_t)L16(-2)) goto L1b4e;
  ax = L16(-2);
  L16(-4) = (uint16_t)(L16(-4) + ax);
  goto L1b6e;
L1b4e:
  ax = (uint16_t)(L16(-4) - L16(-8));
L1b54:
  L16(-2) = (uint16_t)(L16(-2) + ax);
L1b57:
  L16(-4) = L16(-8);
  goto L1bde;
L1b60:
  ax = L16(-4);
  if ((int16_t)L16(-2) >= (int16_t)ax) goto L1b76;
  ax = L16(-2);
  L16(-4) = (uint16_t)(L16(-4) - ax);
L1b6e:
  L16(-2) = 0;
  goto L1bde;
L1b76:
  L16(-2) = (uint16_t)(L16(-2) - ax);
L1b79:
  L16(-4) = 0;
  goto L1bde;
L1b80:
  ax = (uint16_t)(L16(-8) - L16(-4));
  if ((int16_t)ax <= (int16_t)L16(-2)) goto L1b94;
  ax = L16(-4);
  L16(-2) = (uint16_t)(L16(-2) + ax);
  goto L1b79;
L1b94:
  ax = (uint16_t)(L16(-2) - L16(-8));
L1b9a:
  L16(-4) = (uint16_t)(L16(-4) + ax);
L1b9d:
  L16(-2) = L16(-8);
  goto L1bde;
L1ba6:
  ax = L16(-4);
  if ((int16_t)L16(-2) >= (int16_t)ax) goto L1bb6;
  ax = (uint16_t)(L16(-8) - L16(-4));
  goto L1b54;
L1bb6:
  ax = (uint16_t)(L16(-8) - L16(-2));
  goto L1b9a;
L1bde:
  st = fp80_mul(fp80_from_i32((int16_t)L16(-8)), fp80_from_f64(drd_f64(0x20ac)));
  ax = (uint16_t)fp80_to_i32_ftol(st);
  L16(-6) = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax);
  st = fp80_mul(fp80_from_i32((int16_t)L16(-8)), fp80_from_f64(drd_f64(0x20b4)));
  ax = (uint16_t)fp80_to_i32_ftol(st);
  L16(-6) = (uint16_t)(L16(-6) - ax);
  if (L16(-4) == 0) goto L1c17;
  ax = L16(-4);
  if (L16(-8) != ax) goto L1c60;
L1c17:
  ax = L16(-6);
  L16(-2) = (uint16_t)(L16(-2) + ax);
  ax = L16(-2);
  if ((int16_t)L16(-8) >= (int16_t)ax) goto L1c34;
  ax = (uint16_t)(ax - L16(-8));
  L16(-6) = ax;
  L16(-2) = L16(-8);
  goto L1c49;
L1c34:
  if ((int16_t)ax >= 0) goto L1c44;
  ax = (uint16_t)(-(int16_t)ax);
  L16(-6) = ax;
  L16(-2) = 0;
  goto L1c49;
L1c44:
  L16(-6) = 0;
L1c49:
  if (L16(-4) != 0) goto L1c58;
  ax = L16(-6);
  L16(-4) = (uint16_t)(L16(-4) + ax);
  goto L1ca6;
L1c58:
  ax = L16(-6);
  L16(-4) = (uint16_t)(L16(-4) - ax);
  goto L1ca6;
L1c60:
  ax = L16(-6);
  L16(-4) = (uint16_t)(L16(-4) + ax);
  ax = L16(-4);
  if ((int16_t)L16(-8) >= (int16_t)ax) goto L1c7c;
  ax = (uint16_t)(ax - L16(-8));
  L16(-6) = ax;
  L16(-4) = L16(-8);
  goto L1c91;
L1c7c:
  if ((int16_t)ax >= 0) goto L1c8c;
  ax = (uint16_t)(-(int16_t)ax);
  L16(-6) = ax;
  L16(-4) = 0;
  goto L1c91;
L1c8c:
  L16(-6) = 0;
L1c91:
  if (L16(-2) != 0) goto L1ca0;
  ax = L16(-6);
  L16(-2) = (uint16_t)(L16(-2) + ax);
  goto L1ca6;
L1ca0:
  ax = L16(-6);
  L16(-2) = (uint16_t)(L16(-2) - ax);
L1ca6:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, L16(-0x20));
  L16(-6) = ax;
  if (L16(-2) != 0) goto L1cc0;
  L16(-2) = (uint16_t)(L16(-2) + ax);
  goto L1cec;
L1cc0:
  ax = L16(-2);
  if (L16(-8) != ax) goto L1cd0;
  ax = L16(-6);
  L16(-2) = (uint16_t)(L16(-2) - ax);
  goto L1cec;
L1cd0:
  if (L16(-4) != 0) goto L1cde;
  ax = L16(-6);
  L16(-4) = (uint16_t)(L16(-4) + ax);
  goto L1cec;
L1cde:
  ax = L16(-4);
  if (L16(-8) != ax) goto L1cec;
  ax = L16(-6);
  L16(-4) = (uint16_t)(L16(-4) - ax);
L1cec:
  L16(-2) = (uint16_t)(L16(-2) + 0x3e8);
  L16(-4) = (uint16_t)(L16(-4) + 0x3e8);
  ax = (uint16_t)CALL_ORIG(7, 0x294e, FAR_CDECL, 4, L16(-4), L16(-2), 0x21, (uint16_t)(rt_bp - 0x18));
  if (ax == 0) goto Lm1;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-0x10));
  L16(-0x1c) = (uint16_t)r;
  L16(-0x1a) = (uint16_t)(r >> 16);
  if (((uint16_t)(r >> 16) | (uint16_t)r) == 0) goto L1d30;
  if (rd16(L16(-0x1a), (uint16_t)(L16(-0x1c) + 2)) == drd16(0x014c)) goto Lm1;
L1d30:
  {
    uint32_t cp = PLANET_XY(L16(-0x10));
    uint32_t cr = PLANET_XY(rd16(seg, off));
    r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)cp, (uint16_t)(cp >> 16), (uint16_t)cr, (uint16_t)(cr >> 16));
  }
  {
    uint16_t cx = (uint16_t)r, si = (uint16_t)(r >> 16);
    bx = L16(-0x20);
    ax = (uint16_t)((int16_t)bx * (int16_t)bx);
    dx = (uint16_t)((int16_t)ax < 0 ? 0xffff : 0); /* CWD after IMUL: only AX survives */
    if ((int16_t)dx < (int16_t)si) goto L1d6e;
    if ((int16_t)dx > (int16_t)si) goto Lm1;
    if (ax > cx) goto Lm1;
  }
L1d6e:
  ax = (uint16_t)(L16(-0x10) + 1);
  FRAME_LEAVE();
  return ax;
Lm1:
  FRAME_LEAVE();
  return 0xffff;
}

/* 21:0x32aa (Ghidra FUN_10a0_32aa), 806 bytes. ENTER 0x1a,0; PUSH DI; PUSH SI (non-leaf: 8:0xbc50,
 * 7:0x01e4, 9:0x2a74, 18:0x457a, 8:0x8730, 18:0x3bb4, 18:0x39f6, 18:0x3922, 18:0x3854, 18:0x2664).
 * FAR CDECL (rec, a far pointer to a fleet record at [BP+6]), no result. When the fleet has more
 * than one waypoint (+0x62 > 1) the 18-byte order record at far pointer (+0x64) + 0x16 is copied to
 * BP-0x1a (its dword is a position, the word at BP-0x16 a target id, the high byte at BP-0x13 holds
 * the target kind in its low nibble). The function returns early when the fleet is already close to
 * that position and the kind is 2, when it is 2 and far, or when the kind is 1 and the planet
 * 7:0x01e4(id) is neither owned by the current player nor unowned with turn +0x32 == DS:0x82. It then
 * sums the cargo (si = +0x1c + +0x24 twice + +0x18 + +0x1a + +0x20 + +0x22, di = +0x1e + +0x26), sets bit 7
 * of +5, queues 9:0x2a74(2, id) and, when +6 is -1, 18:0x457a(rec, 0x1c2). Otherwise it looks the
 * target planet up (7:0x01e4(+6)) and decides, from the thresholds DS:0x5340 to 0x5343, the player's
 * flag bits (DS:0x5a6b[player] & 0x1c) and up to three draws random(10), whether to search a target
 * (18:0x39f6 with the callbacks 17:0x2252 and 17:0x216c when DS:0x80 bit 4 is set, else 18:0x3922 with
 * 18:0x3be2, or 18:0x3854 with 17:0x2154), mark its info byte (DS:0x1600 + 16 * id + 10 bit 7) and build a
 * new 18-byte order at BP-0x1a for 18:0x2664(rec, &order, 0). */
DECOMP(21, 0x32aa, fn_21_32aa, FAR_CDECL)
void fn_21_32aa(uint32_t rec)
{
  FRAME_ENTER(0x1a + 4);
  uint16_t rseg = (uint16_t)(rec >> 16), roff = (uint16_t)rec;
  uint16_t si, di, ax, dx, bx, cx, es;
  uint32_t r;

  if ((int16_t)rd16(rseg, (uint16_t)(roff + 0x62)) <= 1) goto L335d;
  ax = rd16(rseg, (uint16_t)(roff + 0x64));
  dx = rd16(rseg, (uint16_t)(roff + 0x66));
  ax = (uint16_t)(ax + 0x16);
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x1a))), LIN(MK_FP(dx, ax)), 18);
  r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, rd16(rseg, (uint16_t)(roff + 8)), rd16(rseg, (uint16_t)(roff + 0xa)), L16(-0x1a), L16(-0x18));
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  if ((int16_t)dx < 0) goto L3306;
  if ((int16_t)dx > 0) goto L32fb;
  if (ax <= 0xf424) goto L3306;
L32fb:
  if ((L8(-0x13) & 0xf) == 2) goto L335d;
L3306:
  if ((L8(-0x13) & 0xf) == 2) goto L35cc;
  if ((L8(-0x13) & 0xf) != 1) goto L335d;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-0x16));
  si = (uint16_t)r;
  L16(-2) = (uint16_t)(r >> 16);
  if (((uint16_t)(r >> 16) | si) == 0) goto L35cc;
  es = L16(-2);
  if (rd16(es, (uint16_t)(si + 2)) == 0xffff) goto L3352;
  if (rd16(es, (uint16_t)(si + 2)) != drd16(0x014c)) goto L35cc;
  if ((rd8(es, (uint16_t)(si + 5)) & 2) != 0) goto L35cc;
L3352:
  if (rd16(es, (uint16_t)(si + 0x32)) != drd16(0x0082)) goto L35cc;
L335d:
  si = rd16(rseg, (uint16_t)(roff + 0x1c));
  si = (uint16_t)(si + rd16(rseg, (uint16_t)(roff + 0x24)));
  si = (uint16_t)(si + si);
  si = (uint16_t)(si + rd16(rseg, (uint16_t)(roff + 0x18)));
  si = (uint16_t)(si + rd16(rseg, (uint16_t)(roff + 0x1a)));
  si = (uint16_t)(si + rd16(rseg, (uint16_t)(roff + 0x20)));
  si = (uint16_t)(si + rd16(rseg, (uint16_t)(roff + 0x22)));
  di = rd16(rseg, (uint16_t)(roff + 0x1e));
  di = (uint16_t)(di + rd16(rseg, (uint16_t)(roff + 0x26)));
  wr8(rseg, (uint16_t)(roff + 5), (uint8_t)(rd8(rseg, (uint16_t)(roff + 5)) | 0x80));
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(rseg, roff));
  if (rd16(rseg, (uint16_t)(roff + 6)) != 0xffff) goto L33b0;
  (void)CALL_ORIG(18, 0x457a, FAR_CDECL, 3, roff, rseg, 0x1c2);
  goto L35cc;
L33b0:
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, rd16(rseg, (uint16_t)(roff + 6)));
  L16(-4) = (uint16_t)r;
  L16(-2) = (uint16_t)(r >> 16);
  if (rd16(L16(-2), (uint16_t)(L16(-4) + 2)) != drd16(0x014c)) goto L3454;
  ax = drd8(0x5340);
  if ((int16_t)ax > (int16_t)si) goto L33e2;
  ax = drd8(0x5342);
  if ((int16_t)ax <= (int16_t)di) goto L34db;
L33e2:
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if (((drd8((uint16_t)(bx + 0x5a6b)) & 0x1c)) <= 4) goto L35cc;
  ax = drd8(0x5340);
  ax = (uint16_t)(ax + ax);
  if ((int16_t)ax < (int16_t)si) goto L3408;
  if ((int16_t)si < 0x3c) goto L35cc;
L3408:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax < 5) goto L34db;
  ax = drd8(0x5340);
  cx = ax;
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + cx);
  if ((int16_t)ax >= (int16_t)si) goto L3439;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax < 7) goto L34db;
L3439:
  if ((int16_t)si <= 0x78) goto L35cc;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax >= 7) goto L35cc;
  goto L34db;
L3454:
  ax = drd8(0x5341);
  if ((int16_t)ax > (int16_t)si) goto L3470;
  ax = drd8(0x5343);
  if ((int16_t)ax > (int16_t)di) goto L3470;
  if (rd16(L16(-2), (uint16_t)(L16(-4) + 2)) != 0xffff) goto L35cc;
  goto L34db;
L3470:
  (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, roff, rseg, 0);
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if (((drd8((uint16_t)(bx + 0x5a6b)) & 0x1c)) <= 4) goto L3516;
  ax = drd8(0x5340);
  ax = (uint16_t)(ax + ax);
  if ((int16_t)ax >= (int16_t)si) goto L34ac;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax < 5) goto L34db;
L34ac:
  ax = drd8(0x5340);
  ax = (uint16_t)(ax << 2);
  if ((int16_t)ax >= (int16_t)si) goto L34c7;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax < 7) goto L34db;
L34c7:
  if ((int16_t)si <= 0x78) goto L3516;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if ((int16_t)ax >= 7) goto L3516;
L34db:
  if ((drd8(0x0080) & 0x10) == 0) goto L34f6;
  r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-4), L16(-2), 0x2252, SEG_SEL(17));
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  goto L34f9;
L34f6:
  ax = 0;
  dx = 0;
L34f9:
  si = ax;
  L16(-6) = dx;
  if ((dx | si) != 0) goto L352d;
  r = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, L16(-4), L16(-2), 0x216c, SEG_SEL(17));
  goto L3525;
L3516:
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-4), L16(-2), 0x3be2, SEG_SEL(18));
L3525:
  si = (uint16_t)r;
  L16(-6) = (uint16_t)(r >> 16);
  dx = L16(-6);
L352d:
  if ((dx | si) == 0) goto L356a;
  di = (uint16_t)(rd16(dx, si) << 4);
  wr8(drd16(0x1602), (uint16_t)(drd16(0x1600) + di + 0xa), (uint8_t)(rd8(drd16(0x1602), (uint16_t)(drd16(0x1600) + di + 0xa)) | 0x80));
  ax = rd16(L16(-6), si);
  L16(-0x16) = ax;
  ax = L16(-0x14);
  ax = (uint16_t)(((ax & 0xf1ff) | 0x0100));
  L16(-0x14) = ax;
  bx = (uint16_t)(rd16(L16(-6), si) << 2);
  ax = drd16((uint16_t)(bx + 0x31ba));
  dx = drd16((uint16_t)(bx + 0x31bc));
  goto L35a2;
L356a:
  r = CALL_ORIG(18, 0x3854, FAR_CDECL, 4, roff, rseg, 0x2154, SEG_SEL(17));
  si = (uint16_t)r;
  L16(-2) = (uint16_t)(r >> 16);
  if ((L16(-2) | si) == 0) goto L35cc;
  es = L16(-2);
  ax = rd16(es, si);
  L16(-0x16) = ax;
  ax = L16(-0x14);
  ax = (uint16_t)((ax & 0xf2ff) | 0x0200);
  L16(-0x14) = ax;
  ax = rd16(es, (uint16_t)(si + 8));
  dx = rd16(es, (uint16_t)(si + 0xa));
L35a2:
  L16(-0x1a) = ax;
  L16(-0x18) = dx;
  L8(-0x14) = (uint8_t)(L8(-0x14) & 0xf0);
  L8(-0x13) = (uint8_t)(L8(-0x13) | 0x10);
  ax = L16(-0x14);
  ax = (uint16_t)((ax & 0xff4f) | 0x0040);
  L16(-0x14) = ax;
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, roff, rseg, (uint16_t)(rt_bp - 0x1a), 0);
L35cc:
  FRAME_LEAVE();
}

/* The word table in the code segment at 21:0x11f5 to 21:0x123c (36 words): offsets of the 7-byte
 * records of the byte pool at 21:0x10c8 that 21:0x2c74 passes (as cs:(word + 0x10c8)) to 18:0x0000.
 * It is read through CS in the original; addr is the original CS offset of the word (an odd offset
 * from 0x11f5 on, in steps of 2). */
static const uint16_t cs_t11f5[36] = {
  0x0000, 0x0007, 0x000e, 0x0015, 0x001c, 0x0023, 0x002a, 0x0031, 0x0038, 0x003f, 0x0046, 0x004b,
  0x0050, 0x0054, 0x005b, 0x0062, 0x006d, 0x007a, 0x0081, 0x0088, 0x008f, 0x0096, 0x009d, 0x00a4,
  0x00ab, 0x00b2, 0x00b9, 0x00c4, 0x00cf, 0x00da, 0x00e5, 0x00f0, 0x00fb, 0x0106, 0x0113, 0x0120,
};
static uint16_t cs_w(uint16_t addr)
{
  uint16_t i = (uint16_t)(addr - 0x11f5);
  if ((i & 1) != 0 || (i >> 1) >= 36) {
    fprintf(stderr, "seg21: CS word table read outside 0x11f5..0x123c (0x%04x)\n", addr);
    abort();
  }
  return cs_t11f5[i >> 1];
}
/* 18:0x0000(kind dword (lo, hi), far pointer cs:(table word at addr + 0x10c8)): queue one message. */
#define MSG18(lo, hi, addr) \
  ((uint16_t)CALL_ORIG(18, 0x0000, FAR_CDECL, 4, (lo), (hi), (uint16_t)(cs_w((uint16_t)(addr)) + 0x10c8), SEG_SEL(21)))

/* 21:0x2c74 (Ghidra FUN_10a0_2c74), 1102 bytes. ENTER 0x90,0; PUSH DI; PUSH SI (non-leaf: 18:0x0630,
 * 18:0x0000, 8:0x8730). FAR CDECL, no arguments, no result. Queues, through 18:0x0000(kind | tech << 16,
 * cs:string-record), the messages of the techs the computer player has reached. First, when the byte
 * DS:0x41f6 has no bit 1, DS:0x417a is not 5, the player's flag bits (DS:0x5a6b[player] & 0x1c) are above
 * 4, the dword DS:0x41fd is 0 and the turn DS:0x82 is above 5, it copies the 0x8d-byte record at DS:0x417a
 * to BP-0x8e, sets bit 1 of its byte +0x7c and passes it to 18:0x0630(record, 0). Then it walks the flag
 * bytes (bit 1) of DS:0x41f6, 0x442a, 0x44b7, 0x4310, 0x439d, the two 0x234-byte records from DS:0x4543
 * and the two 0x8d-byte records from DS:0x49ab, each against the turn DS:0x82 and the turn word stored
 * behind it, and for each queues one message, for the later ones trying up to 5 (or 3, 4, 7, 9, 10)
 * random table entries (8:0x8730) until 18:0x0000 answers non-zero. The records of the second group
 * also adjust the table index (BP-8, BP-6, BP-4, DI) from the number of messages already queued. */
DECOMP(21, 0x2c74, fn_21_2c74, FAR_CDECL)
void fn_21_2c74(void)
{
  FRAME_ENTER(0x90 + 4);
  uint16_t si, di, ax, cx, bx;

  if ((drd8(0x41f6) & 2) != 0) goto L2cd1;
  if (drd16(0x417a) == 5) goto L2cd1;
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if ((drd8((uint16_t)(bx + 0x5a6b)) & 0x1c) <= 4) goto L2cd1;
  if (drd32(0x41fd) != 0) goto L2cd1;
  if (drd16(0x0082) <= 5) goto L2cd1;
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x8e))), LINN(0x417a), 0x8d);
  L8(-0x12) = (uint8_t)(L8(-0x12) | 2);
  (void)CALL_ORIG(18, 0x0630, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x8e), 0);
L2cd1:
  if ((drd8(0x41f6) & 2) == 0) goto L2cef;
  (void)MSG18(0, 5, 0x120d);
L2cef:
  if ((drd8(0x442a) & 2) == 0) goto L2d50;
  if (drd16(0x0082) <= 0x1e) goto L2d50;
  if (drd16(0x0082) > 0x4b) goto L2d1f;
  if (MSG18(4, 6, 0x11f5) != 0) goto L2d50;
L2d1f:
  si = 5;
L2d22:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(4, 6, (uint16_t)(ax * 2 + 0x11f5)) != 0) goto L2d50;
  si--;
  if ((int16_t)si > 0) goto L2d22;
L2d50:
  if ((drd8(0x44b7) & 2) == 0) goto L2dbd;
  if ((drd8(0x442a) & 2) != 0) goto L2dbd;
  if ((uint16_t)(drd16(0x442b) + 0x14) >= drd16(0x0082)) goto L2dbd;
  if (drd16(0x0082) > 0x4b) goto L2d8c;
  if (MSG18(5, 6, 0x11ff) != 0) goto L2dbd;
L2d8c:
  si = 5;
L2d8f:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(5, 6, (uint16_t)(ax * 2 + 0x11ff)) != 0) goto L2dbd;
  si--;
  if ((int16_t)si > 0) goto L2d8f;
L2dbd:
  if ((drd8(0x4310) & 2) == 0) goto L2de2;
  if (drd16(0x0082) <= 0x14) goto L2de2;
  (void)MSG18(2, 0x0b, 0x1209);
L2de2:
  if ((drd8(0x439d) & 2) == 0) goto L2e13;
  if ((drd8(0x4310) & 2) != 0) goto L2e13;
  if ((uint16_t)(drd16(0x4311) + 0x14) >= drd16(0x0082)) goto L2e13;
  (void)MSG18(3, 0x0b, 0x120b);
L2e13:
  si = 6;
  L16(-0xa) = 0x4543;
L2e1b:
  bx = L16(-0xa);
  if ((drd8((uint16_t)(bx + 1)) & 2) == 0) goto L2fd2;
  if (bx != 0x4543) goto L2e33;
  if (drd16(0x0082) > 0x28) goto L2e4a;
L2e33:
  if ((drd8(0x4544) & 2) != 0) goto L2fd2;
  if ((uint16_t)(drd16(0x4545) + 0x1e) >= drd16(0x0082)) goto L2fd2;
L2e4a:
  cx = (uint16_t)(si + 2);
  L16(-8) = si;
  L16(-6) = cx;
  L16(-2) = 3;
  si = 3;
  di = cx;
L2e5d:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 0x1d, (uint16_t)(ax * 2 + 0x1237)) != 0) goto L2e8a;
  si--;
  if ((int16_t)si > 0) goto L2e5d;
  goto L2e8b;
L2e8a:
  di--;
L2e8b:
  si = 4;
  L16(-6) = di;
L2e91:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 9, (uint16_t)(ax * 2 + 0x122f)) != 0) goto L2ebe;
  si--;
  if ((int16_t)si > 0) goto L2e91;
  goto L2ebf;
L2ebe:
  di--;
L2ebf:
  si = 3;
  L16(-6) = di;
L2ec5:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 9, (uint16_t)(ax * 2 + 0x1229)) != 0) goto L2ef2;
  si--;
  if ((int16_t)si > 0) goto L2ec5;
  goto L2ef3;
L2ef2:
  di--;
L2ef3:
  si = L16(-8);
  if ((int16_t)di < (int16_t)si) goto L2f66;
  cx = (uint16_t)(si - di);
  cx = (uint16_t)(cx + 3);
  ax = cx;
  cx = (uint16_t)(cx + cx);
  cx = (uint16_t)(cx + ax);
  ax = (uint16_t)(0x1a - cx);
L2f0c:
  L16(-4) = ax;
  si = cx;
  if ((int16_t)cx <= 0) goto L2f46;
  L16(-6) = di;
L2f18:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  bx = (uint16_t)(ax + L16(-4));
  bx = (uint16_t)(bx + bx);
  if (MSG18(di, 7, (uint16_t)(bx + 0x11f5)) != 0) goto L2f46;
  si--;
  if ((int16_t)si > 0) goto L2f18;
L2f46:
  di--;
  if ((int16_t)di < (int16_t)L16(-8)) goto L2f5a;
  cx = 3;
  if (L16(-8) != di) goto L2f60;
  ax = 0x11;
  goto L2f0c;
L2f60:
  ax = 0x14;
  goto L2f0c;
L2f5a:
  si = L16(-8);
L2f66:
  ax = (uint16_t)(si + 3);
  L16(-0x90) = ax;
  if (MSG18(ax, 0x1d, 0x1215) != 0) goto L2fd2;
  if (MSG18(L16(-0x90), 9, 0x1213) != 0) goto L2fd2;
  if (MSG18(L16(-0x90), 0x13, 0x1211) != 0) goto L2fd2;
  (void)MSG18(L16(-0x90), 0x13, 0x120f);
L2fd2:
  si = (uint16_t)(si + 4);
  L16(-0xa) = (uint16_t)(L16(-0xa) + 0x234);
  if (L16(-0xa) <= 0x4777) goto L2e1b;
  di = 0xe;
  L16(-4) = 0x49ab;
L2feb:
  bx = L16(-4);
  if ((drd8((uint16_t)(bx + 1)) & 2) == 0) goto L30af;
  if (bx != 0x49ab) goto L3003;
  if (drd16(0x0082) > 0x1e) goto L301a;
L3003:
  if ((drd8(0x49ac) & 2) != 0) goto L30af;
  if ((uint16_t)(drd16(0x49ad) + 0x14) >= drd16(0x0082)) goto L30af;
L301a:
  si = 7;
  L16(-8) = di;
L3020:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 9, (uint16_t)(ax * 2 + 0x1229)) != 0) goto L304b;
  si--;
  if ((int16_t)si > 0) goto L3020;
L304b:
  if (si != 0) goto L307d;
  si = 9;
L3052:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 7, (uint16_t)(ax * 2 + 0x1217)) != 0) goto L307d;
  si--;
  if ((int16_t)si > 0) goto L3052;
L307d:
  if (si != 0) goto L30af;
  si = 0xa;
L3084:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, si);
  if (MSG18(di, 6, (uint16_t)(ax * 2 + 0x11f5)) != 0) goto L30af;
  si--;
  if ((int16_t)si > 0) goto L3084;
L30af:
  di++;
  L16(-4) = (uint16_t)(L16(-4) + 0x8d);
  if (L16(-4) <= 0x4a38) goto L2feb;
  FRAME_LEAVE();
}

/* The 18-byte order record 21:0x23d0 and 21:0x32aa build at BP-0x2a / BP-0x1a: it is zeroed first. */
#define ORDER_ZERO(bpoff) memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + (bpoff)))), 0, 18)

/* 21:0x23d0 (Ghidra FUN_10a0_23d0), 860 bytes. ENTER 0x2a,0; PUSH DI; PUSH SI (non-leaf: 7:0x01e4,
 * 7:0x294e, 18:0x0750, 7:0x153e, 28:0x21ee, 9:0x2a74, 18:0x4510, 18:0x3922, 10:0x678c, 18:0x2664).
 * FAR CDECL (rec, a far pointer to a fleet record at [BP+6]; tab, a far pointer at [BP+0xA]: the table of
 * planet flag words, one word per planet id, whose bits 1-2 and bits 3-4 are two-bit counters).
 * P = 7:0x01e4(rec.+6), the fleet's target planet. When P is not found it asks 7:0x294e(rec+8, 0x21, answer
 * at BP-0x18) for a planet and goes on with 7:0x01e4(the id at BP-0x10). When P is the current player's
 * (DS:0x14c) it queues 18:0x0750(1, rec.+6, 2, rec.id, 3, 1000 or -1000) (1000 when the signed dword
 * P+0x28 is above 0x7d0) and 7:0x153e(rec.id, 0x4b8c) and goes on with the flag si = 1 or 0. Otherwise,
 * unless P has no owner, its owner's attribute 28:0x21ee(record, 0xe) is 8 or bit 1 of P+5 is set (then si is
 * 1 when the dword rec+0x58 is above 0), it writes an 18-byte order for P (position DS:0x31ba[P.id], id, kind
 * bits) into the cursor struct DS:0x4bf0 (at +4 when its word +8 is P.id and its byte +0xb & 0xf is 1, else at
 * +0x16), brackets it with 7:0x153e calls and queues 9:0x2a74(2, rec.id) and 18:0x4510(rec, 0). With si set it searches a
 * target with 18:0x3922 and the callback 21:0x272c, then 21:0x27ba; none found and P is the player's: it
 * queues 18:0x0750 again and raises the counter in bits 1-2 of the word tab[2 * P.id] (when it is below 3);
 * found: it raises the counter in bits 3-4 of tab[2 * found.id] (when below 3). With si clear it searches
 * with 18:0x3922 and 21:0x2858. Any target found is turned into an 18-byte order at BP-0x2a for
 * 18:0x2664(rec, &order, 0) (kind bits from 10:0x678c(rec, 1)); when that answers non-zero bit 7 of rec+5 is set. */
DECOMP(21, 0x23d0, fn_21_23d0, FAR_CDECL)
void fn_21_23d0(uint32_t rec, uint32_t tab)
{
  FRAME_ENTER(0x2a + 4);
  uint16_t rseg = (uint16_t)(rec >> 16), roff = (uint16_t)rec;
  uint16_t tseg = (uint16_t)(tab >> 16), toff = (uint16_t)tab;
  uint16_t si = 0, di, ax, bx, cx, es;
  uint32_t r;

  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, rd16(rseg, (uint16_t)(roff + 6)));
  L16(-4) = (uint16_t)r;
  L16(-2) = (uint16_t)(r >> 16);
  if (((uint16_t)(r >> 16) | (uint16_t)r) != 0) goto L2420;
  ax = (uint16_t)CALL_ORIG(7, 0x294e, FAR_CDECL, 4, rd16(rseg, (uint16_t)(roff + 8)), rd16(rseg, (uint16_t)(roff + 0xa)), 0x21, (uint16_t)(rt_bp - 0x18));
  if (ax == 0) goto L241a;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, L16(-0x10));
  goto L26a6;
L241a:
  r = 0;
  goto L26a6;
L2420:
  es = L16(-2);
  ax = drd16(0x014c);
  bx = L16(-4);
  if (rd16(es, (uint16_t)(bx + 2)) != ax) goto L24a2;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x2a)) < (int16_t)si) goto L2472;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x2a)) > (int16_t)si) goto L243f;
  if (rd16(es, (uint16_t)(bx + 0x28)) <= 0x7d0) goto L2472;
L243f:
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(rseg, (uint16_t)(roff + 6)), 2, rd16(rseg, roff), 3, 0x3e8);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(rseg, roff), 0x4b8c);
  si = 1;
  goto L25b3;
L2472:
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(rseg, (uint16_t)(roff + 6)), 2, rd16(rseg, roff), 3, 0xfc18);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(rseg, roff), 0x4b8c);
  goto L25b3;
L24a2:
  L16(-6) = si;
  if (rd16(es, (uint16_t)(bx + 2)) == 0xffff) goto L25a4;
  ax = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(rd16(es, (uint16_t)(bx + 2)) * 0xc0 + 0x5a64), 0xe);
  if (ax == 8) goto L25a4;
  if ((rd8(L16(-2), (uint16_t)(L16(-4) + 5)) & 2) != 0) goto L25a4;
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(rseg, roff), 0x4b8c);
  es = L16(-2);
  bx = L16(-4);
  ORDER_ZERO(-0x2a);
  si = rd16(es, bx);
  ax = si;
  si = (uint16_t)(si << 2);
  L32(-0x2a) = drd32((uint16_t)(si + 0x31ba));
  cx = L16(-0x24);
  cx = (uint16_t)((cx & 0xf1ff) | 0x0100);
  L16(-0x24) = cx;
  L16(-0x26) = ax;
  cx = (uint16_t)((cx & 0xfff1) | 0x0001);
  L16(-0x24) = cx;
  L8(-0x23) = (uint8_t)(L8(-0x23) | 0x10);
  cx = L16(-0x1c);
  cx = (uint16_t)((cx & 0x0fff) | 0x2000);
  L16(-0x1c) = cx;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(rseg, roff));
  si = ax;
  es = drd16(0x4bf2);
  bx = drd16(0x4bf0);
  if (rd16(es, (uint16_t)(bx + 8)) != si) goto L2562;
  if ((rd8(es, (uint16_t)(bx + 0xb)) & 0xf) != 1) goto L2562;
  di = (uint16_t)(bx + 4);
  goto L2565;
L2562:
  di = (uint16_t)(bx + 0x16);
L2565:
  memcpy(LIN(MK_FP(es, di)), LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x2a))), 18);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  (void)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, roff, rseg, 0);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(rseg, roff), 0x4b8c);
  si = L16(-6);
  goto L25b3;
L25a4:
  if ((int32_t)rd32(rseg, (uint16_t)(roff + 0x58)) > 0) goto L246c;
  si = 0;
  goto L25b3;
L246c:
  si = 1;
L25b3:
  if (si == 0) goto L2694;
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-4), L16(-2), 0x272c, SEG_SEL(21));
  si = (uint16_t)r;
  L16(-6) = (uint16_t)(r >> 16);
  if (((uint16_t)(r >> 16) | si) != 0) goto L25eb;
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-4), L16(-2), 0x27ba, SEG_SEL(21));
  si = (uint16_t)r;
  L16(-6) = (uint16_t)(r >> 16);
L25eb:
  ax = L16(-6);
  if ((ax | si) != 0) goto L2668;
  ax = drd16(0x014c);
  if (rd16(L16(-2), (uint16_t)(L16(-4) + 2)) != ax) goto L26ab;
  di = toff;
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(rseg, (uint16_t)(roff + 6)), 2, rd16(rseg, roff), 3, 0xfc18);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(rseg, roff), 0x4b8c);
  bx = rd16(L16(-2), L16(-4));
  bx = (uint16_t)(bx + bx);
  bx = (uint16_t)(bx + di);
  if ((rd8(tseg, bx) & 6) >= 6) goto L26ab;
  bx = rd16(L16(-2), L16(-4));
  bx = (uint16_t)(bx + bx);
  bx = (uint16_t)(bx + di);
  ax = rd16(tseg, bx);
  cx = ax;
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx + 2));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx ^ (uint8_t)ax));
  cx = (uint16_t)(cx & 6);
  wr16(tseg, bx, (uint16_t)(rd16(tseg, bx) ^ cx));
  goto L26ab;
L2668:
  bx = toff;
  es = L16(-6);
  di = rd16(es, si);
  di = (uint16_t)(di + di);
  di = (uint16_t)(di + bx);
  if ((rd8(tseg, di) & 0x18) >= 0x18) goto L26ab;
  di = rd16(es, si);
  di = (uint16_t)(di + di);
  di = (uint16_t)(di + bx);
  ax = rd16(tseg, di);
  cx = ax;
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx + 8));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx ^ (uint8_t)ax));
  cx = (uint16_t)(cx & 0x18);
  wr16(tseg, di, (uint16_t)(rd16(tseg, di) ^ cx));
  goto L26ab;
L2694:
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-4), L16(-2), 0x2858, SEG_SEL(21));
L26a6:
  si = (uint16_t)r;
  L16(-6) = (uint16_t)(r >> 16);
L26ab:
  ax = L16(-6);
  if ((ax | si) == 0) goto L2728;
  ORDER_ZERO(-0x2a);
  es = L16(-6);
  bx = (uint16_t)(rd16(es, si) << 2);
  L32(-0x2a) = drd32((uint16_t)(bx + 0x31ba));
  cx = L16(-0x24);
  cx = (uint16_t)((cx & 0xf1ff) | 0x0100);
  L16(-0x24) = cx;
  cx = rd16(es, si);
  L16(-0x26) = cx;
  L8(-0x24) = (uint8_t)(L8(-0x24) & 0xf0);
  L8(-0x23) = (uint8_t)(L8(-0x23) | 0x10);
  ax = (uint16_t)CALL_ORIG(10, 0x678c, FAR_CDECL, 3, roff, rseg, 1);
  {
    uint8_t al = (uint8_t)((uint8_t)ax << 4);
    al = (uint8_t)(al ^ L8(-0x24));
    ax = (uint16_t)(al & 0xf0);
    L16(-0x24) = (uint16_t)(L16(-0x24) ^ ax);
  }
  ax = (uint16_t)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, roff, rseg, (uint16_t)(rt_bp - 0x2a), 0);
  if (ax == 0) goto L2728;
  wr8(rseg, (uint16_t)(roff + 5), (uint8_t)(rd8(rseg, (uint16_t)(roff + 5)) | 0x80));
L2728:
  FRAME_LEAVE();
}

/* 21:0x1e8a (Ghidra FUN_10a0_1e8a), 1350 bytes. ENTER 0x82,0; PUSH DI; PUSH SI (non-leaf: 18:0x3558,
 * 18:0x3600, 9:0x4a62, 9:0x4b0c, 9:0x4c64, 9:0x4b8e, 26:0x21e4, 18:0x2732; x86 32-bit IDIV).
 * FAR CDECL (rec, a far pointer to a fleet record at [BP+6]); returns AX = 1 when it queued orders, else 0.
 * A[0..3] (dwords at BP-0x28, 18:0x3558) and B[0..3] (BP-0x38, 18:0x3600) are what the fleet holds and what
 * it can carry; D[k] = A[k] - B[k] is stored at BP-0x48 (0 if D[3] < 0 ends it). D8 and D7 (BP-8, BP-0xa)
 * are the sums of the low 10 bits of the entries of the list struct at DS:0xcd2 (count byte +3, dword
 * entries from +4, a (word & 0xe) == 2 and the type bits (dword & 0x1fc00) 0x2000 or 0x1c00). The queue
 * DS:0x5a62 (near pointer, count DS:0x6686, walked from the last entry) gives, per entry: type 8 (0x2000):
 * BP-0x10 = 9:0x4a62(rec, me, 0) - 9:0x4b0c(rec) - D8 (0 when negative) and 26:0x21e4 fills the cost
 * vector at BP-0x58; type 7 (0x1c00): BP-0x12 likewise with 9:0x4c64 / 9:0x4b8e and D7, vector BP-0x68;
 * type 0xb (0x2c00): vector BP-0x78; type 0xc (0x3000): BP-0xe = the entry's low 10 bits. The dwords BP-0x4c,
 * BP-0x5c and BP-0x6c (index 3 of the three vectors) start at 0. Then (1) with BP-0xe > 0 and the player's
 * flag byte (DS:0x5a6b[player] & 0xe0) == 0xa0 it queues 18:0x2732(0xc, n, 1, 0), n = min((D[3] + 0x45) / 0x46,
 * BP-0xe), and returns 1; (2) when D[0], D[1] and D[2] are all positive it limits the counts of type 7
 * (BP-0x12, divided by the vector, with DS:0x791 bit 3 selecting the min over the three kinds or only
 * the third) and of type 8, subtracting their costs from D[3]; (3) otherwise it returns 0 when rec+0x34 points
 * to a record whose dword +4 (type bits, (w2 & 0xe) == 2) is 0xc00 with bit 16 clear, or 0x2c00, and else
 * counts type 8 only. Finally it queues 18:0x2732 for the type 7 count (7), the type 8 count (8) and, when
 * all six attributes of the player (DS:0x5a7e to DS:0x5a83) are 0x1a and D[3] / vector[3] of type 0xb
 * is positive, the count of type 0xb (0xb) when the turn DS:0x82 is above 100, and returns 1 when any
 * count is positive. The divisions trap (divide error) on a zero divisor or an overflow, which stops the
 * run here. */
DECOMP(21, 0x1e8a, fn_21_1e8a, FAR_CDECL)
uint16_t fn_21_1e8a(uint32_t rec)
{
  FRAME_ENTER(0x82 + 4);
  uint16_t rseg = (uint16_t)(rec >> 16), roff = (uint16_t)rec;
  uint16_t si, di, ax, dx, bx, cx, ls;
  uint32_t eax;
  int32_t q;

  di = roff;
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, di, rseg, (uint16_t)(rt_bp - 0x28));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, di, rseg, (uint16_t)(rt_bp - 0x38));
  L16(-0x18) = 0;
  L16(-2) = 0;
  bx = 0;
  do {
    si = bx;
    L32((int16_t)(si - 0x48)) = L32((int16_t)(si - 0x28)) - L32((int16_t)(si - 0x38));
    bx = (uint16_t)(bx + 4);
  } while ((int16_t)bx < 0x10);
  if ((int16_t)L16(-0x3a) < 0) goto L23ca;
  L32(-0x4c) = 0;
  L32(-0x5c) = 0;
  L32(-0x6c) = 0;
  L32(-0x10) = 0;
  di = 0;
  cx = 0;
  L16(-0x12) = 0;
  L16(-0xc) = 0;
  ls = drd16(0xcd4);
  L16(-4) = (uint16_t)(drd16(0xcd2) + 4);
  L16(-2) = ls;
  if (rd8(ls, (uint16_t)(drd16(0xcd2) + 3)) == 0) goto L1f78;
  L16(-6) = rd8(ls, (uint16_t)(drd16(0xcd2) + 3));
  bx = L16(-4);
L1f20:
  if ((rd16(ls, (uint16_t)(bx + 2)) & 0xe) != 2) goto L1f43;
  if ((rd32(ls, bx) & 0x1fc00) != 0x2000) goto L1f43;
  di = (uint16_t)(di + (rd16(ls, bx) & 0x3ff));
L1f43:
  if ((rd16(ls, (uint16_t)(bx + 2)) & 0xe) != 2) goto L1f66;
  if ((rd32(ls, bx) & 0x1fc00) != 0x1c00) goto L1f66;
  cx = (uint16_t)(cx + (rd16(ls, bx) & 0x3ff));
L1f66:
  bx = (uint16_t)(bx + 4);
  L16(-6) = (uint16_t)(L16(-6) - 1);
  if (L16(-6) != 0) goto L1f20;
  L16(-8) = di;
  L16(-0xa) = cx;
  goto L1f7e;
L1f78:
  L16(-0xa) = cx;
  L16(-8) = di;
L1f7e:
  dx = (uint16_t)(drd16(0x6686) - 1);
  if ((int16_t)dx < 0) goto L2120;
  si = (uint16_t)(dx << 2);
  di = roff;
  L16(-0xc) = dx;
L1f92:
  bx = drd16(0x5a62);
  ax = (uint16_t)(drd16((uint16_t)(bx + si + 2)) & 0xe);
  if (ax != 2) goto L2119;
  eax = drd32((uint16_t)(bx + si)) & 0x1fc00;
  dx = (uint16_t)(eax >> 16);
  if (eax != 0x2000) goto L2035;
  ax = (uint16_t)CALL_ORIG(9, 0x4a62, FAR_CDECL, 4, di, rseg, drd16(0x014c), dx);
  L16(-0x7a) = ax;
  ax = (uint16_t)CALL_ORIG(9, 0x4b0c, FAR_CDECL, 2, di, rseg);
  ax = (uint16_t)(ax - L16(-0x7a));
  ax = (uint16_t)(-(int16_t)ax);
  ax = (uint16_t)(ax - L16(-8));
  if ((int16_t)ax >= 0) goto L1fec;
  L16(-0x10) = 0;
  goto L2018;
L1fec:
  ax = (uint16_t)CALL_ORIG(9, 0x4a62, FAR_CDECL, 4, di, rseg, drd16(0x014c), 0);
  L16(-0x7c) = ax;
  ax = (uint16_t)CALL_ORIG(9, 0x4b0c, FAR_CDECL, 2, di, rseg);
  ax = (uint16_t)(ax - L16(-0x7c));
  ax = (uint16_t)(-(int16_t)ax);
  ax = (uint16_t)(ax - L16(-8));
  L16(-0x10) = ax;
L2018:
  (void)CALL_ORIG(26, 0x21e4, FAR_CDECL, 7, di, rseg, (uint16_t)(drd16(0x5a62) + si), MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x58), drd16(0x014c), 1);
L2035:
  bx = drd16(0x5a62);
  eax = drd32((uint16_t)(bx + si)) & 0x1fc00;
  dx = (uint16_t)(eax >> 16);
  if (eax != 0x1c00) goto L20cb;
  ax = (uint16_t)CALL_ORIG(9, 0x4c64, FAR_CDECL, 4, di, rseg, drd16(0x014c), dx);
  L16(-0x7e) = ax;
  ax = (uint16_t)CALL_ORIG(9, 0x4b8e, FAR_CDECL, 2, di, rseg);
  ax = (uint16_t)(ax - L16(-0x7e));
  ax = (uint16_t)(-(int16_t)ax);
  ax = (uint16_t)(ax - L16(-0xa));
  if ((int16_t)ax >= 0) goto L2082;
  L16(-0x12) = 0;
  goto L20ae;
L2082:
  ax = (uint16_t)CALL_ORIG(9, 0x4c64, FAR_CDECL, 4, di, rseg, drd16(0x014c), 0);
  L16(-0x80) = ax;
  ax = (uint16_t)CALL_ORIG(9, 0x4b8e, FAR_CDECL, 2, di, rseg);
  ax = (uint16_t)(ax - L16(-0x80));
  ax = (uint16_t)(-(int16_t)ax);
  ax = (uint16_t)(ax - L16(-0xa));
  L16(-0x12) = ax;
L20ae:
  (void)CALL_ORIG(26, 0x21e4, FAR_CDECL, 7, di, rseg, (uint16_t)(drd16(0x5a62) + si), MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x68), drd16(0x014c), 1);
L20cb:
  bx = drd16(0x5a62);
  eax = drd32((uint16_t)(bx + si)) & 0x1fc00;
  if (eax != 0x2c00) goto L20fc;
  (void)CALL_ORIG(26, 0x21e4, FAR_CDECL, 7, di, rseg, (uint16_t)(bx + si), MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x78), drd16(0x014c), 1);
L20fc:
  bx = drd16(0x5a62);
  eax = drd32((uint16_t)(bx + si)) & 0x1fc00;
  if (eax != 0x3000) goto L2119;
  L16(-0xe) = (uint16_t)(drd16((uint16_t)(bx + si)) & 0x3ff);
L2119:
  si = (uint16_t)(si - 4);
  if ((int16_t)si >= 0) goto L1f92;
L2120:
  di = L16(-0xe);
  if ((int16_t)di <= 0) goto L2176;
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if ((drd8((uint16_t)(bx + 0x5a6b)) & 0xe0) != 0xa0) goto L2176;
  q = idiv32((int32_t)(L32(-0x3c) + 0x45), 0x46);
  si = (uint16_t)q;
  if ((int16_t)si > (int16_t)di) si = di;
  if ((int16_t)si <= 0) goto L2176;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xc, si, 1, 0);
  goto L216e;
L2176:
  if ((int32_t)L32(-0x48) <= 0) goto L228c;
  if ((int32_t)L32(-0x44) <= 0) goto L228c;
  if ((int32_t)L32(-0x40) <= 0) goto L228c;
  si = L16(-0x12);
  if ((int16_t)si <= 0) goto L2214;
  if ((drd8(0x0791) & 8) != 0) goto L21be;
  ax = (uint16_t)idiv32((int32_t)L32(-0x40), (int32_t)L32(-0x60));
  if ((int16_t)ax > (int16_t)si) ax = si;
  si = ax;
  goto L2214;
L21be:
  ax = (uint16_t)idiv32((int32_t)L32(-0x48), (int32_t)L32(-0x68));
  if ((int16_t)ax > (int16_t)si) ax = si;
  di = ax;
  ax = (uint16_t)idiv32((int32_t)L32(-0x44), (int32_t)L32(-0x64));
  if ((int16_t)di > (int16_t)ax) di = ax;
  ax = (uint16_t)idiv32((int32_t)L32(-0x40), (int32_t)L32(-0x60));
  if ((int16_t)di > (int16_t)ax) di = ax;
  si = di;
L2214:
  if ((int16_t)si <= 0) goto L2238;
  ax = (uint16_t)idiv32((int32_t)L32(-0x3c), (int32_t)L32(-0x5c));
  if ((int16_t)ax > (int16_t)si) ax = si;
  L16(-0xa) = ax;
  goto L223d;
L2238:
  L16(-0xa) = 0;
L223d:
  L32(-0x3c) = L32(-0x3c) - (uint32_t)(int32_t)(int16_t)L16(-0xa) * L32(-0x5c);
  if ((int16_t)L16(-0x10) <= 0) goto L2274;
  ax = (uint16_t)idiv32((int32_t)L32(-0x3c), (int32_t)L32(-0x4c));
  if ((int16_t)ax > (int16_t)L16(-0x10)) ax = L16(-0x10);
  L16(-8) = ax;
  si = ax;
  goto L2276;
L2274:
  si = 0;
L2276:
  L32(-0x3c) = L32(-0x3c) - (uint32_t)(int32_t)(int16_t)si * L32(-0x4c);
  goto L231e;
L228c:
  if (rd32(rseg, (uint16_t)(roff + 0x34)) == 0) goto L22da;
  {
    uint32_t p = rd32(rseg, (uint16_t)(roff + 0x34));
    uint16_t pseg = (uint16_t)(p >> 16), poff = (uint16_t)p;
    L32(-0x16) = rd32(pseg, (uint16_t)(poff + 4));
    dx = rd16(pseg, (uint16_t)(poff + 6));
  }
  ax = (uint16_t)(dx & 0xe);
  if (ax != 2) goto L22da;
  ax = (uint16_t)(L16(-0x16) & 0xfc00);
  dx = (uint16_t)(dx & 1);
  if (ax != 0xc00) goto L22c6;
  if (dx == 0) goto L23ca;
L22c6:
  if ((L32(-0x16) & 0x1fc00) == 0x2c00) goto L23ca;
L22da:
  if ((int16_t)L16(-0x10) <= 0) goto L2300;
  si = (uint16_t)idiv32((int32_t)L32(-0x3c), (int32_t)L32(-0x4c));
  if ((int16_t)si > (int16_t)L16(-0x10)) si = L16(-0x10);
  goto L2302;
L2300:
  si = 0;
L2302:
  L32(-0x3c) = L32(-0x3c) - (uint32_t)(int32_t)(int16_t)si * L32(-0x4c);
  L16(-0xa) = 0;
  L16(-0x18) = 1;
L231e:
  if ((int32_t)L32(-0x6c) <= 0) goto L236f;
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  L16(-0x82) = bx;
  if (drd8((uint16_t)(bx + 0x5a81)) != 0x1a) goto L236f;
  if (drd8((uint16_t)(bx + 0x5a83)) != 0x1a) goto L236f;
  if (drd8((uint16_t)(bx + 0x5a7e)) != 0x1a) goto L236f;
  if (drd8((uint16_t)(bx + 0x5a7f)) != 0x1a) goto L236f;
  if (drd8((uint16_t)(bx + 0x5a82)) != 0x1a) goto L236f;
  if (drd8((uint16_t)(bx + 0x5a80)) != 0x1a) goto L236f;
  di = (uint16_t)idiv32((int32_t)L32(-0x3c), (int32_t)L32(-0x6c));
  di = (uint16_t)(di + 1);
  if ((int16_t)di >= 0) goto L2371;
L236f:
  di = 0;
L2371:
  if ((int16_t)L16(-0xa) <= 0) goto L238a;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 7, L16(-0xa), 1, 1);
L238a:
  if ((int16_t)si <= 0) goto L239c;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 8, si, 1, 0);
L239c:
  if ((int16_t)di <= 0) goto L23bd;
  if (drd16(0x0082) <= 0x64) goto L23bd;
  ax = (uint16_t)(L16(-0x18) < 1 ? 1 : 0);
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xb, di, 1, ax);
L23bd:
  ax = (uint16_t)(L16(-0xa) + di + si);
  if ((int16_t)ax > 0) goto L216e;
L23ca:
  FRAME_LEAVE();
  return 0;
L216e:
  FRAME_LEAVE();
  return 1;
}

/* 21:0x1e2a (Ghidra FUN_10a0_1e2a), 96 bytes. ENTER 0x104,0; PUSH DI; PUSH SI (non-leaf: 9:0x2a74,
 * 26:0x0100, 21:0x1e8a, 26:0x0810). FAR CDECL, no arguments, no result. Walks the far pointers at
 * DS:0x1608 (DS:0x4b62 of them, each to a fleet record, stopping at a null pointer): for each it queues
 * 9:0x2a74(1, rec.id), calls 26:0x0100 with the 0x104-byte local buffer at BP-0x104, calls 21:0x1e8a(rec)
 * (it mirrors a frame: through rt_call_far) and passes its answer to 26:0x0810. */
DECOMP(21, 0x1e2a, fn_21_1e2a, FAR_CDECL)
void fn_21_1e2a(void)
{
  FRAME_ENTER(0x104 + 4);
  uint16_t si, di = 0;
  uint32_t r;

  if ((int16_t)drd16(0x4b62) <= (int16_t)di) goto L1e86;
  si = 0;
L1e3a:
  L32(-4) = rd32(drd16(0x160a), (uint16_t)(drd16(0x1608) + si));
  if (L32(-4) == 0) goto L1e86;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, rd16(L16(-2), L16(-4)));
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x104));
  {
    const uint16_t a_[] = {L16(-4), L16(-2)};
    r = rt_call_far(MK_FP(SEG_SEL(21), 0x1e8a), FAR_CDECL, a_, 2);
  }
  (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, (uint16_t)r);
  si = (uint16_t)(si + 4);
  di++;
  if ((int16_t)di < (int16_t)drd16(0x4b62)) goto L1e3a;
L1e86:
  FRAME_LEAVE();
}

/* The bit field word at DS:0x4c36 (the cursor's "what" word): the two updates 21:0x123e makes before it
 * calls 7:0x0358(-1, 0x4c08). Step 1 sets bits 10-13 from (v << 10) (an XOR with the high byte keeps the
 * other bits), step 2 sets bits 0-9 from an id. */
static void w4c36_step1(uint16_t v)
{
  uint16_t ax = (uint16_t)(v << 10);
  ax = (uint16_t)(ax ^ (uint16_t)((uint16_t)drd8(0x4c37) << 8));
  ax = (uint16_t)(ax & 0x3c00);
  dwr16(0x4c36, (uint16_t)(drd16(0x4c36) ^ ax));
}
static void w4c36_step2(uint16_t v)
{
  uint16_t ax = (uint16_t)(v ^ drd16(0x4c36));
  ax = (uint16_t)(ax & 0x03ff);
  dwr16(0x4c36, (uint16_t)(drd16(0x4c36) ^ ax));
}

/* 21:0x123e (Ghidra FUN_10a0_123e), 2222 bytes. ENTER 0x188,0; PUSH DI; PUSH SI (non-leaf: 9:0x2a74,
 * 26:0x0100, 18:0x3558, 18:0x3600, 18:0x3922, 18:0x2732, 9:0x4f76, 7:0x0358, 8:0x8730, 7:0x28ea,
 * 21:0x1aec, 21:0x1d76, 26:0x0810, the C runtime's pow 34:0x0d84 and ftol 34:0x0de6, x87; x86 32-bit IDIV).
 * FAR CDECL, no arguments, no result. The spend phase of the older computer player: for each fleet P of
 * the far pointer list DS:0x1608 (DS:0x4b62 of them) that has bit 1 in +5 it queues 9:0x2a74(1, P.id) and
 * 26:0x0100, reads A (BP-0x11a) and B (BP-0x12a) from 18:0x3558 / 18:0x3600 and works with the flag byte
 * T1 = tab1[2 * P.id] (tab1 = DS:0x1604 + 2) and the byte pair T2 = tab2[2 * P.id] (tab2 = tab1 + 2 * DS:0x7a):
 *  1. when T1 has no bit 7, P's hull class (+0x2c & 0xf) is 1, 3, 6 or 8 and one of its three cargo dwords
 *     (+0x1c, +0x20, +0x24) is above 0x2bc: N = (int16)(A[3] / 2) / 5; with N >= 7 the search
 *     18:0x3922(P, 21:0x2882) gives Q; for each cargo kind where Q has less than 10 (1000 when Q's hull is not
 *     one of those four) and P more than 0x2bc it queues 18:0x2732(0xe + k, min(N, 7), 1, 0), clears the kind's
 *     bit in T2 + 1 and N -= that; when any was queued it combines min(power(P), power(Q)) - 4 into the
 *     word DS:0x4c36 and calls 7:0x0358(-1, 0x4c08);
 *  2. otherwise (no Q or nothing queued), and when T1 bit 7 is clear and the player's flag bits are above
 *     4 or are 4 with random(3) == 0, S = A[0] + A[1] + A[2] - B[...] - 0xd2 is combined with
 *     M * 0x46, M = ((int16)(A[3] / 2) - 5) / 5 into the dword at tab3 (DS:0x1604 + 4 * DS:0x7a + 2) and, when S
 *     is above 0x96, 18:0x3922(P, 21:0x29da) gives Q; then pow, sqrt and ftol work out how many ships of the
 *     three kinds it can afford (idx = the largest of A[0..2], 70 each), queues them with 18:0x2732(0xe + k, n, 1,
 *     0) and updates DS:0x4c36 and 7:0x0358;
 *  3. otherwise it works on the flag bits of T1 (random(7) in bits 0-2, 21:0x1aec(P, bits) picks a planet,
 *     21:0x1d76(P) decides about bit 4 and the second table). Every pass ends with 26:0x0810(flag BP-0x10a). */
DECOMP(21, 0x123e, fn_21_123e, FAR_CDECL)
void fn_21_123e(void)
{
  FRAME_ENTER(0x188 + 4);
  uint16_t ax, dx, bx, cx, si, es, tseg, toff;
  uint32_t r;
  Fp80 st, st1;

  L16(-0x13e) = (uint16_t)((uint16_t)(drd16(0x007a) + 1) * 2);
  L16(-0x140) = 0;
  goto L19b0;
L1258:
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-0x104), L16(-0x102), 0x2882, SEG_SEL(21));
  L16(-0x108) = (uint16_t)r;
  L16(-0x106) = (uint16_t)(r >> 16);
L1273:
  if (L32(-0x108) == 0) goto L1476;
  es = L16(-0x106);
  bx = L16(-0x108);
  ax = rd16(es, bx);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  ax = (uint16_t)(ax + 2);
  L16(-0x146) = ax;
  L16(-0x144) = dx;
  ax = rd16(es, bx);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  ax = (uint16_t)(ax + L16(-0x13e));
  L16(-0x14a) = ax;
  L16(-0x148) = dx;
  L16(-0x14c) = 0;
  {
    uint8_t al = (uint8_t)(rd8(es, (uint16_t)(bx + 0x2c)) & 0xf);
    if (al == 1 || al == 3 || al == 6 || al == 8) L16(-0x14e) = 0xa;
    else L16(-0x14e) = 0x3e8;
  }
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x1c)) >= (int32_t)(int16_t)L16(-0x14e)) goto L133b;
  if ((int32_t)rd32(L16(-0x102), (uint16_t)(L16(-0x104) + 0x1c)) <= 0x2bc) goto L133b;
  ax = L16(-0x142);
  if ((int16_t)ax > 7) ax = 7;
  L16(-0x14c) = ax;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xe, ax, 1, 0);
  ax = L16(-0x14c);
  L16(-0x142) = (uint16_t)(L16(-0x142) - ax);
  wr8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1), (uint8_t)(rd8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1)) & 0xfe));
  L16(-0x10a) = 1;
L133b:
  es = L16(-0x106);
  bx = L16(-0x108);
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x20)) >= (int32_t)(int16_t)L16(-0x14e)) goto L1390;
  if ((int32_t)rd32(L16(-0x102), (uint16_t)(L16(-0x104) + 0x20)) <= 0x2bc) goto L1390;
  ax = L16(-0x142);
  if ((int16_t)ax > 7) ax = 7;
  L16(-0x14c) = ax;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xf, ax, 1, 0);
  ax = L16(-0x14c);
  L16(-0x142) = (uint16_t)(L16(-0x142) - ax);
  wr8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1), (uint8_t)(rd8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1)) & 0xfd));
  L16(-0x10a) = 1;
L1390:
  es = L16(-0x106);
  bx = L16(-0x108);
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x24)) >= (int32_t)(int16_t)L16(-0x14e)) goto L13e5;
  if ((int32_t)rd32(L16(-0x102), (uint16_t)(L16(-0x104) + 0x24)) <= 0x2bc) goto L13e5;
  ax = L16(-0x142);
  if ((int16_t)ax > 7) ax = 7;
  L16(-0x14c) = ax;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x10, ax, 1, 0);
  ax = L16(-0x14c);
  L16(-0x142) = (uint16_t)(L16(-0x142) - ax);
  wr8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1), (uint8_t)(rd8(L16(-0x148), (uint16_t)(L16(-0x14a) + 1)) & 0xfb));
  L16(-0x10a) = 1;
L13e5:
  if (L16(-0x10a) == 0) goto L19a0;
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, L16(-0x104), L16(-0x102), (uint16_t)(rt_bp - 0x134));
  ax = (uint16_t)(ax - 4);
  L16(-0x150) = ax;
  if (L16(-0x134) != 0) L16(-0x150) = (uint16_t)(L16(-0x150) + 1);
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, L16(-0x108), L16(-0x106), (uint16_t)(rt_bp - 0x134));
  ax = (uint16_t)(ax - 4);
  L16(-0x152) = ax;
  if (L16(-0x134) != 0) L16(-0x152) = (uint16_t)(L16(-0x152) + 1);
  ax = L16(-0x152);
  if ((int16_t)ax > (int16_t)L16(-0x150)) ax = L16(-0x150);
  w4c36_step1(ax);
  ax = rd16(L16(-0x106), L16(-0x108));
  ax = (uint16_t)(ax + 1);
L145a:
  w4c36_step2(ax);
L1465:
  (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, 0xffff, 0x4c08);
  goto L19a0;
L1476:
  if ((rd8(L16(-0x12c), L16(-0x12e)) & 0x80) != 0) goto L1878;
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if ((drd8((uint16_t)(bx + 0x5a6b)) & 0x1c) > 4) goto L14b8;
  bx = (uint16_t)(drd16(0x014c) * 0xc0);
  if ((drd8((uint16_t)(bx + 0x5a6b)) & 0x1c) != 4) goto L1878;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 3);
  if (ax != 0) goto L1878;
L14b8:
  ax = (uint16_t)(drd16(0x007a) * 2);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  ax = (uint16_t)(ax + L16(-0x13e));
  L16(-0x156) = ax;
  L16(-0x154) = dx;
  L32(-0x11a) = L32(-0x11a) - L32(-0x12a);
  L32(-0x116) = L32(-0x116) - L32(-0x126);
  L32(-0x112) = L32(-0x112) - L32(-0x122);
  L32(-0x14a) = L32(-0x116) + L32(-0x112) + L32(-0x11a) - 0xd2u;
  ax = (uint16_t)idiv32((int32_t)L32(-0x10e), 2);
  L16(-0x152) = ax;
  ax = (uint16_t)(ax - 5);
  ax = (uint16_t)idiv16((int32_t)(int16_t)ax, 5);
  dx = (uint16_t)((int16_t)ax < 0 ? 0xffff : 0);
  L16(-0x146) = ax;
  L16(-0x144) = dx;
  {
    int32_t mm = (int32_t)(((uint32_t)dx << 16) | ax);
    int32_t prod = (int32_t)((uint32_t)mm * 0x46u);
    if (prod > (int32_t)L32(-0x14a)) prod = (int32_t)L32(-0x14a);
    ax = (uint16_t)prod;
    dx = (uint16_t)((uint32_t)prod >> 16);
  }
  wr16(L16(-0x154), L16(-0x156), ax);
  wr16(L16(-0x154), (uint16_t)(L16(-0x156) + 2), dx);
  if ((int32_t)L32(-0x14a) <= 0x96) goto L158a;
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-0x104), L16(-0x102), 0x29da, SEG_SEL(21));
  L16(-0x108) = (uint16_t)r;
  L16(-0x106) = (uint16_t)(r >> 16);
  goto L1593;
L158a:
  L32(-0x108) = 0;
L1593:
  if (L32(-0x108) == 0) goto L1878;
  L16(-0x15c) = 0;
  L16(-0x15a) = 0;
  L16(-0x158) = 0;
  es = L16(-0x106);
  bx = L16(-0x108);
  cx = rd16(es, bx);
  cx = (uint16_t)(cx + cx);
  cx = (uint16_t)(cx + drd16(0x1604));
  dx = drd16(0x1606);
  cx = (uint16_t)(cx + 2);
  L16(-0x160) = cx;
  L16(-0x15e) = dx;
  cx = rd16(es, bx);
  cx = (uint16_t)(cx + cx);
  cx = (uint16_t)(cx + drd16(0x1604));
  cx = (uint16_t)(cx + L16(-0x13e));
  L16(-0x164) = cx;
  L16(-0x162) = dx;
  L16(-0x150) = 0;
  if ((rd8(es, (uint16_t)(bx + 5)) & 2) == 0) goto L160a;
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, L16(-0x108), L16(-0x106), (uint16_t)(rt_bp - 0x134));
  L16(-0x13c) = ax;
  if (L16(-0x134) == 0) goto L160e;
  L16(-0x13c) = (uint16_t)(L16(-0x13c) + 1);
  goto L160e;
L160a:
  L16(-0x13c) = 0;
L160e:
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, L16(-0x104), L16(-0x102), (uint16_t)(rt_bp - 0x134));
  ax = (uint16_t)(ax + 3);
  L16(-0x136) = ax;
  L16(-0x14e) = (uint16_t)(ax * ax);
  LF64(-0x16c) = fp80_to_f64(fp80_from_i32((int16_t)L16(-0x14e)));
  {
    uint32_t cq = PLANET_XY(rd16(L16(-0x106), L16(-0x108)));
    uint32_t cp = PLANET_XY(rd16(L16(-0x102), L16(-0x104)));
    ax = (uint16_t)CALL_ORIG(7, 0x28ea, FAR_CDECL, 4, (uint16_t)cp, (uint16_t)(cp >> 16), (uint16_t)cq, (uint16_t)(cq >> 16));
  }
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x174))), LIN(MK_FP(MEM_SEL_DGROUP, ax)), 8);
  ax = L16(-0x13c);
  cx = (uint16_t)(ax * ax);
  ax = L16(-0x136);
  ax = (uint16_t)(ax * ax);
  ax = (uint16_t)(ax - cx);
  es = L16(-0x106);
  bx = L16(-0x108);
  dx = rd16(es, (uint16_t)(bx + 0x12));
  dx = (uint16_t)(dx >> 12);
  cx = (uint16_t)(0x5f - dx);
  ax = (uint16_t)((uint32_t)ax * cx);
  L32(-0x184) = 0;
  L16(-0x186) = 0;
  L16(-0x188) = ax;
  st = fp80_from_i32((int32_t)(uint16_t)L16(-0x188));
  st = fp80_mul(st, fp80_from_f64(drd_f64(0x2094)));
  LF64(-0x17c) = fp80_to_f64(st);
  ax = rd16(es, (uint16_t)(bx + 0x12));
  ax = (uint16_t)(ax & 0x0fff);
  ax = (uint16_t)(ax << 2);
  ax = (uint16_t)(ax + 0x64);
  {
    uint16_t sub = (uint16_t)(ax - 0x3e8);
    cx = (uint16_t)(ax < 0x3e8 ? 0xffff : 0);
    ax = (uint16_t)(sub & cx);
    ax = (uint16_t)(ax + 0x3e8);
  }
  L16(-0x188) = ax;
  st = fp80_from_i32((int32_t)(uint16_t)L16(-0x188));
  st = fp80_div(st, fp80_from_f64(LF64(-0x17c)));
  r = (uint32_t)fp80_to_i32_ftol(st);
  L16(-0x180) = (uint16_t)r;
  L16(-0x17e) = (uint16_t)(r >> 16);
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  if ((int16_t)dx < (int16_t)L16(-0x148)) goto L1716;
  if ((int16_t)dx > (int16_t)L16(-0x148)) goto L170e;
  if (ax <= L16(-0x14a)) goto L1716;
L170e:
  ax = L16(-0x14a);
  dx = L16(-0x148);
L1716:
  L16(-0x14a) = ax;
  L16(-0x148) = dx;
  if (L16(-0x134) == 0) goto L172c;
  st1 = fp80_from_f64(drd_f64(0x209c));
  goto L1731;
L172c:
  st1 = fp80_from_f64(drd_f64(0x20a4));
L1731:
  st = fp80_div(fp80_from_f64(LF64(-0x174)), fp80_from_f64(LF64(-0x16c)));
  st = crt_pow_34_0d84(st1, st);
  st = fp80_mul(st, fp80_from_i32((int32_t)L32(-0x14a)));
  r = (uint32_t)fp80_to_i32_ftol(st);
  L16(-0x14a) = (uint16_t)r;
  L16(-0x148) = (uint16_t)(r >> 16);
L1752:
  if ((int32_t)L32(-0x14a) <= 0) goto L17c6;
  L16(-0x188) = 0;
  ax = L16(-0x116);
  dx = L16(-0x114);
  si = (uint16_t)(L16(-0x188) << 2);
  if ((int16_t)L16((int16_t)(si - 0x118)) > (int16_t)dx) goto L1783;
  if ((int16_t)L16((int16_t)(si - 0x118)) < (int16_t)dx) goto L177d;
  if (L16((int16_t)(si - 0x11a)) >= ax) goto L1783;
L177d:
  L16(-0x188) = 1;
L1783:
  ax = L16(-0x112);
  dx = L16(-0x110);
  si = (uint16_t)(L16(-0x188) << 2);
  if ((int16_t)L16((int16_t)(si - 0x118)) > (int16_t)dx) goto L17a6;
  if ((int16_t)L16((int16_t)(si - 0x118)) < (int16_t)dx) goto L17a0;
  if (L16((int16_t)(si - 0x11a)) >= ax) goto L17a6;
L17a0:
  L16(-0x188) = 2;
L17a6:
  si = (uint16_t)(L16(-0x188) + L16(-0x188));
  L16((int16_t)(si - 0x15c)) = (uint16_t)(L16((int16_t)(si - 0x15c)) + 1);
  si = (uint16_t)(L16(-0x188) << 2);
  L32((int16_t)(si - 0x11a)) = L32((int16_t)(si - 0x11a)) - 0x46u;
  L32(-0x14a) = L32(-0x14a) - 0x46u;
  goto L1752;
L17c6:
  if ((int16_t)L16(-0x15c) <= 0) goto L17de;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xe, L16(-0x15c), 1, 0);
L17de:
  if ((int16_t)L16(-0x15a) <= 0) goto L17f6;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xf, L16(-0x15a), 1, 0);
L17f6:
  if ((int16_t)L16(-0x158) <= 0) goto L180e;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x10, L16(-0x158), 1, 0);
L180e:
  ax = L16(-0x136);
  ax = (uint16_t)(ax - 4);
  w4c36_step1(ax);
  ax = rd16(L16(-0x106), L16(-0x108));
  ax = (uint16_t)(ax + 1);
  w4c36_step2(ax);
  (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, 0xffff, 0x4c08);
  wr8(L16(-0x15e), L16(-0x160), (uint8_t)(rd8(L16(-0x15e), L16(-0x160)) | 0x60));
  st1 = fp80_from_f64(LF64(-0x174));
  /* FCOM m64; FSTP ST(0); FNSTSW; SAHF; JBE: taken when ST0 <= mem (or unordered) */
  if (!extF80_lt(fp80_from_f64(LF64(-0x16c)), st1)) goto L1866;
  wr8(L16(-0x12c), L16(-0x12e), (uint8_t)(rd8(L16(-0x12c), L16(-0x12e)) | 0x80));
L1866:
  wr8(L16(-0x12c), L16(-0x12e), (uint8_t)(rd8(L16(-0x12c), L16(-0x12e)) & 0xef));
  L16(-0x10a) = 1;
  goto L19a0;
L1878:
  es = L16(-0x12c);
  bx = L16(-0x12e);
  if ((rd8(es, bx) & 0x10) == 0) goto L188a;
  if ((rd8(es, bx) & 0x80) == 0) goto L199c;
L188a:
  if ((rd8(es, bx) & 0x80) != 0) goto L18e2;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 7);
  L16(-0x138) = ax;
  es = L16(-0x12c);
  bx = L16(-0x12e);
  ax = (uint16_t)(rd8(es, bx) & 7);
  if (ax != L16(-0x138)) goto L18ba;
  {
    uint8_t al = (uint8_t)((uint8_t)L16(-0x138) + 1);
    al = (uint8_t)(al ^ rd8(es, bx));
    ax = al;
  }
  goto L18c1;
L18ba:
  {
    uint8_t al = (uint8_t)(rd8(es, bx) ^ (uint8_t)L16(-0x138));
    ax = al;
  }
L18c1:
  ax = (uint16_t)(ax & 7);
  wr16(es, bx, (uint16_t)(rd16(es, bx) ^ ax));
  es = L16(-0x12c);
  bx = L16(-0x12e);
  ax = (uint16_t)(rd8(es, bx) & 7);
  {
    const uint16_t a_[] = {L16(-0x104), L16(-0x102), ax};
    r = rt_call_far(MK_FP(SEG_SEL(21), 0x1aec), FAR_CDECL, a_, 3);
  }
  L16(-0x13a) = (uint16_t)r;
L18e2:
  es = L16(-0x12c);
  bx = L16(-0x12e);
  if ((rd8(es, bx) & 0x80) != 0) goto L18f5;
  if ((int16_t)L16(-0x13a) == -1) goto L19a0;
L18f5:
  ax = L16(-0x13a);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  ax = (uint16_t)(ax + 2);
  L16(-0x188) = ax;
  L16(-0x186) = dx;
  if ((rd8(L16(-0x186), L16(-0x188)) & 0x60) != 0) goto L1956;
  {
    const uint16_t a_[] = {L16(-0x104), L16(-0x102)};
    r = rt_call_far(MK_FP(SEG_SEL(21), 0x1d76), FAR_CDECL, a_, 2);
  }
  ax = (uint16_t)r;
  if (ax == 0) goto L1956;
  wr8(L16(-0x186), L16(-0x188), (uint8_t)(rd8(L16(-0x186), L16(-0x188)) | 0x60));
  es = L16(-0x12c);
  bx = L16(-0x12e);
  ax = (uint16_t)(rd8(es, bx) & 0x80);
  ax = (uint16_t)(ax < 1 ? 0xffff : 0);
  ax = (uint16_t)(-(int16_t)ax);
  {
    uint8_t al = (uint8_t)((uint8_t)ax << 4);
    al = (uint8_t)(al ^ rd8(es, bx));
    ax = (uint16_t)(al & 0x10);
  }
  wr16(es, bx, (uint16_t)(rd16(es, bx) ^ ax));
  L16(-0x10a) = 1;
  goto L195e;
L1956:
  wr8(L16(-0x12c), L16(-0x12e), (uint8_t)(rd8(L16(-0x12c), L16(-0x12e)) & 0xef));
L195e:
  ax = (uint16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, L16(-0x104), L16(-0x102), (uint16_t)(rt_bp - 0x134));
  ax = (uint16_t)(ax - 1);
  L16(-0x136) = ax;
  w4c36_step1(ax);
  es = L16(-0x12c);
  bx = L16(-0x12e);
  if ((rd8(es, bx) & 0x80) != 0) goto L1994;
  ax = L16(-0x13a);
  goto L145a;
L1994:
  wr8(es, bx, (uint8_t)(rd8(es, bx) & 0x7f));
  goto L1465;
L199c:
  wr8(es, bx, (uint8_t)(rd8(es, bx) & 0xef));
L19a0:
  (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, L16(-0x10a));
L19ac:
  L16(-0x140) = (uint16_t)(L16(-0x140) + 1);
L19b0:
  ax = drd16(0x4b62);
  if ((int16_t)L16(-0x140) >= (int16_t)ax) goto L1ae8;
  bx = (uint16_t)(L16(-0x140) << 2);
  tseg = drd16(0x160a);
  toff = drd16(0x1608);
  L32(-0x104) = rd32(tseg, (uint16_t)(bx + toff));
  if (L32(-0x104) == 0) goto L1ae8;
  es = L16(-0x102);
  bx = L16(-0x104);
  if ((rd8(es, (uint16_t)(bx + 5)) & 2) == 0) goto L19ac;
  ax = rd16(es, bx);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  ax = (uint16_t)(ax + 2);
  L16(-0x12e) = ax;
  L16(-0x12c) = dx;
  ax = rd16(es, bx);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  ax = (uint16_t)(ax + L16(-0x13e));
  L16(-0x132) = ax;
  L16(-0x130) = dx;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, rd16(es, bx));
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x100));
  L16(-0x10a) = 0;
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, L16(-0x104), L16(-0x102), (uint16_t)(rt_bp - 0x11a));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, L16(-0x104), L16(-0x102), (uint16_t)(rt_bp - 0x12a));
  if ((rd8(L16(-0x12c), L16(-0x12e)) & 0x80) != 0) goto L1476;
  es = L16(-0x102);
  bx = L16(-0x104);
  {
    uint8_t al = (uint8_t)(rd8(es, (uint16_t)(bx + 0x2c)) & 0xf);
    if (al != 1 && al != 3 && al != 6 && al != 8) goto L1476;
  }
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x1c)) > 0x2bc) goto L1aaf;
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x20)) > 0x2bc) goto L1aaf;
  if ((int32_t)rd32(es, (uint16_t)(bx + 0x24)) <= 0x2bc) goto L1476;
L1aaf:
  ax = (uint16_t)idiv32((int32_t)L32(-0x10e), 2);
  L16(-0x142) = ax;
  ax = (uint16_t)idiv16((int32_t)(int16_t)ax, 5);
  L16(-0x142) = ax;
  if ((int16_t)ax >= 7) goto L1258;
  L32(-0x108) = 0;
  goto L1273;
L1ae8:
  FRAME_LEAVE();
}

/* A call of another function of this segment: the callee mirrors a frame, so it is reached through rt_call_far
 * with the words in declaration order (CDECL). */
static uint32_t call21(uint16_t off, int16_t nargs, const uint16_t *args)
{
  return rt_call_far(MK_FP(SEG_SEL(21), off), FAR_CDECL, args, nargs);
}

/* 21:0x0000 (Ghidra FUN_10a0_0000), 4254 bytes. ENTER 0x122,0; PUSH DI; PUSH SI. FAR CDECL, one word
 * argument (param_1 at [BP+6], passed on to 26:0x0100 and 18:0x59ba). No result. The turn of the older
 * computer player ("the AI turn"):
 *  1. prepares the work tables: it clears the 0x2000-byte table at DS:0x1604 when its first word is 2
 *     (and stores K = 2 * (DS:0x7a + 1) there), saves the state with 18:0x28fc, queues the tech messages
 *     (21:0x2c74) and five flags (18:0x36aa), derives the limits from the turn DS:0x82 (BP-0x34, the bytes
 *     DS:0x4a60 to 0x4a63, BP-0x1c, si) and asks 18:0x5d3c for three 16-slot lists (the local flags at BP-0x64);
 *  2. walks the two research/tech records (DS:0x4543, 0x4777: 0x234 bytes each) and the groups of four
 *     0x8d-byte designs behind them (DS:0x41f5 + 0x8d * k), flagging free slots and queueing 18:0x0630
 *     orders; for turns above 0x50 it also queues three 18:0x5bae flag sets;
 *  3. clears the second table (DS:0x1604 + K, 2 * DS:0x7a bytes) and walks the planet array (DS:0xb6, DS:0x4d96
 *     records of 0x38 bytes): it decrements the two-bit counter in bits 5-6 of each planet's table byte,
 *     and sets the bits 0, 1, 2 of the second byte from the planet's loads and cargo;
 *  4. walks the fleet pointer array (DS:0xba, count DS:0x553a): the other players' fleets are linked through
 *     +0x68 (list BP-0x28), the player's own idle fleets (+0x28, +0x2a not positive) are counted (BP-0x3c,
 *     BP-0x36, BP-0x42) and linked into the list BP-0x30, loaded ones flag their target in the info array
 *     (DS:0x1600) and count in BP-0x3e / BP-0x40, others bump the two-bit counter of their waypoint planet;
 *  5. walks the player's own fleets again and, by their state, queues orders (18:0x098a, 18:0x4510, 18:0x0864,
 *     18:0x0750, 18:0x3bb4, 18:0x3ad8, 18:0x2664, 18:0x5e82, 18:0x1434, 21:0x23d0, 21:0x32aa) or marks flags;
 *  6. walks the list DS:0x1608 (DS:0x4b62 far pointers) deciding about the building orders (18:0x3558, 18:0x3600,
 *     9:0x474c, 9:0x462a, 18:0x2732, 18:0x5fe6, 18:0x3922, 21:0x30c2, 26:0x0810);
 *  7. restores the state with 18:0x59ba and runs 21:0x123e and 21:0x1e2a.
 * 8:0xb90e(0xfc62) (a progress step; a no-op while DS:0x15ba is 0) is called between the phases.
 * The planet walk leaves di (the last planet's second-table byte) in the low word of BP-0x2c: the
 * pointer 21:0x23d0 and the later code use is that slot and not the table base, exactly as the original. */
DECOMP(21, 0x0000, fn_21_0000, FAR_CDECL)
void fn_21_0000(uint16_t param_1)
{
  FRAME_ENTER(0x122 + 4);
  uint16_t ax, bx, cx, dx, si, di, es, ls;
  uint32_t r, eax;
  int32_t q;

  L16(-0x3a) = (uint16_t)((uint16_t)(drd16(0x007a) + 1) * 2);
  L16(-0x3c) = 0;
  L16(-0x36) = 0;
  L16(-0x42) = 0;
  L32(-0x40) = 0;
  ax = drd16(0x1604);
  dx = drd16(0x1606);
  bx = ax;
  L16(-2) = dx;
  es = dx;
  si = ax;
  if (rd16(es, si) != 2) goto L004c;
  for (uint16_t k = 0; k < 0x800; k++) wr32(es, (uint16_t)(bx + 4 * k), 0);
  ax = (uint16_t)((uint16_t)(drd16(0x007a) + 1) * 2);
  wr16(es, bx, ax);
L004c:
  dwr16(0x255c, 0);
  r = CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0x109e, SEG_SEL(21), 0x2a, (uint16_t)(rt_bp - 0x1e), 0x11);
  L16(-0x44) = (uint16_t)r;
  {
    const uint16_t a_[] = {L16(-0x44)};
    (void)call21(0x2c74, 1, a_);
  }
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 1);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x30);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xc000);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x3c0);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x3c00);
  L16(-0x34) = 1;
  if (drd16(0x0082) <= 0x32) goto L00c3;
  ax = (uint16_t)((uint16_t)(drd16(0x0082) - 0x32) / 0xa);
  L16(-0x34) = (uint16_t)(ax + 1);
L00c3:
  if (drd16(0x0082) <= 0x64) goto L00e9;
  dx = (uint16_t)(drd16(0x0082) / 0x64);
  ax = (uint16_t)((uint16_t)(drd16(0x0082) - 0x64) / 0xa);
  bx = dx;
  ax = (uint16_t)(ax * bx);
  L16(-0x34) = (uint16_t)(L16(-0x34) + ax);
L00e9:
  si = 3;
  if (drd16(0x0082) <= 0x82) goto L0106;
  ax = (uint16_t)((uint16_t)(drd16(0x0082) - 0x78) / 0x14);
  si = (uint16_t)(ax + 3);
L0106:
  if ((int16_t)si <= 0x32) goto L010e;
  si = 0x32;
L010e:
  ax = si;
  dwr8(0x4a60, (uint8_t)ax);
  dwr8(0x4a61, (uint8_t)((uint8_t)ax >> 1));
  si = 6;
  if (drd16(0x0082) <= 0x73) goto L0134;
  ax = (uint16_t)((uint16_t)(drd16(0x0082) - 0x64) / 0x16);
  si = (uint16_t)(ax + 6);
L0134:
  L16(-0x1c) = si;
  if ((int16_t)si <= 0xc) goto L0141;
  L16(-0x1c) = 0xc;
L0141:
  dwr8(0x4a62, (uint8_t)L16(-0x1c));
  ax = L16(-0x1c);
  dx = (uint16_t)((int16_t)ax < 0 ? 0xffff : 0);
  ax = (uint16_t)(ax - dx);
  ax = (uint16_t)((int16_t)ax >> 1);
  ax = (uint16_t)(ax - 1);
  if ((int16_t)ax <= 3) goto L0158;
  ax = 3;
L0158:
  dwr8(0x4a63, (uint8_t)ax);
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x64))), 0, 16);
  if (drd16(0x0082) >= 0x78) goto L0178;
  si = 0x32;
  goto L0197;
L0178:
  if (drd16(0x0082) < 0xc8) goto L0194;
  if (drd16(0x0082) < 0x190) goto L018e;
  si = 0x12c;
  goto L0197;
L018e:
  si = 0x64;
  goto L0197;
L0194:
  si = 0x46;
L0197:
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 4, 5, si, (uint16_t)(rt_bp - 0x20), (uint16_t)(rt_bp - 0x64));
  di = L16(-0x20);
  wr8(rt_ss, (uint16_t)(rt_bp + di - 0x64), 0);
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0xe, 0xf, si, (uint16_t)(rt_bp - 0x22), (uint16_t)(rt_bp - 0x64));
  di = L16(-0x20);
  wr8(rt_ss, (uint16_t)(rt_bp + di - 0x64), 0);
  ax = (uint16_t)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 2, 3, si, (uint16_t)(rt_bp - 0x24), (uint16_t)(rt_bp - 0x64));
  dx = (uint16_t)((int16_t)ax < 0 ? 0xffff : 0);
  L16(-0xc) = ax;
  L16(-0xa) = dx;
  di = L16(-0x24);
  wr8(rt_ss, (uint16_t)(rt_bp + di - 0x64), 0);
  L16(-0x38) = 0xffff;
  L32(-8) = 0x90000;
  L16(-0xe) = 0x4543;
  L16(-0x32) = si;
  bx = L16(-0xe);
L0210:
  if ((drd8((uint16_t)(bx + 1)) & 2) != 0) goto L02b6;
  ax = (uint16_t)(drd16(0x0082) - drd16((uint16_t)(bx + 2)));
  if (ax <= L16(-0x32)) goto L02b6;
  L16(-4) = 1;
  dx = L16(-6);
  L16(-0x1c) = dx;
  ax = (uint16_t)(dx - 3);
  if ((int16_t)ax > (int16_t)dx) goto L02b6;
  L16(-0xe) = bx;
  ax = (uint16_t)(dx * 0x8d);
  ax = (uint16_t)(ax + 0x41f5);
  L16(-2) = ax;
L0246:
  bx = L16(-2);
  if ((drd8((uint16_t)(bx + 1)) & 2) != 0) goto L02a0;
  if (drd32((uint16_t)(bx + 8)) != 0) goto L0294;
  if (L16(-4) != 0) goto L0267;
  ax = (uint16_t)(L16(-6) - L16(-0x1c));
  if (ax == 3) goto L0294;
L0267:
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x112))), LIN(MK_FP(MEM_SEL_DGROUP, (uint16_t)(bx - 0x7b))), 0x8d);
  L8(-0x96) = (uint8_t)(L8(-0x96) | 2);
  (void)CALL_ORIG(18, 0x0630, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x112), L16(-0x1c));
  goto L02a0;
L0294:
  si = L16(-0x1c);
  wr8(rt_ss, (uint16_t)(rt_bp + si - 0x64), 1);
  L16(-4) = 0;
L02a0:
  L16(-2) = (uint16_t)(L16(-2) - 0x8d);
  ax = (uint16_t)(L16(-6) - 3);
  L16(-0x1c) = (uint16_t)(L16(-0x1c) - 1);
  if ((int16_t)ax <= (int16_t)L16(-0x1c)) goto L0246;
  bx = L16(-0xe);
L02b6:
  if ((drd8((uint16_t)(bx + 1)) & 2) != 0) goto L02d6;
  if ((int16_t)L16(-0x38) == -1) goto L02d0;
  ax = drd16((uint16_t)(bx + 2));
  si = (uint16_t)((int16_t)L16(-0x38) * 0x234);
  if (drd16((uint16_t)(si + 0x4545)) >= ax) goto L02d6;
L02d0:
  ax = L16(-8);
  L16(-0x38) = ax;
L02d6:
  L16(-6) = (uint16_t)(L16(-6) + 4);
  L16(-8) = (uint16_t)(L16(-8) + 1);
  bx = (uint16_t)(bx + 0x234);
  if (bx <= 0x4777) goto L0210;
  L32(-0x10) = drd32(0x428a);
  if (drd16(0x0082) <= 0x50) goto L036b;
  (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x64));
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x54))), 0, 16);
  L8(-0x54) = 2;
  (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x54));
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x54))), 0, 16);
  L8(-0x53) = 2;
  (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x54));
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x54))), 0, 16);
  L8(-0x51) = 2;
  L8(-0x52) = 2;
  (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x54));
L036b:
  bx = L16(-0x3a);
  eax = drd32(0x1604);
  dx = drd16(0x1606);
  eax = (eax & 0xffff0000u) | (uint16_t)((uint16_t)eax + bx);
  L32(-0x2c) = eax;
  cx = (uint16_t)eax;
  si = cx;
  cx = (uint16_t)(drd16(0x007a) * 2);
  for (uint16_t k = 0; k < cx; k++) wr8(dx, (uint16_t)(si + k), 0);
  ax = (uint16_t)((uint16_t)(drd16(0x4d96) * 0x38) + drd16(0x00b6));
  dx = drd16(0x00b8);
  L16(-4) = ax;
  L16(-2) = dx;
  cx = drd16(0x00b6);
  L16(-0x16) = cx;
  L16(-0x14) = dx;
  if (ax <= cx) goto L050f;
  si = cx;
  ls = dx;
L03ba:
  ax = rd16(ls, si);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  bx = ax;
  ax = (uint16_t)(ax + 2);
  cx = ax;
  L16(-0x18) = dx;
  L16(-0x116) = bx;
  L16(-0x114) = dx;
  L16(-0x11a) = ax;
  L16(-0x118) = dx;
  ax = bx;
  ax = (uint16_t)(ax + L16(-0x3a));
  di = ax;
  L16(-0x2a) = dx;
  if ((rd8(L16(-0x118), L16(-0x11a)) & 0x60) == 0) goto L0409;
  bx = cx;
  ax = rd16(L16(-0x118), bx);
  cx = ax;
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx - 0x20));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx ^ (uint8_t)ax));
  cx = (uint16_t)(cx & 0x60);
  wr16(L16(-0x118), bx, (uint16_t)(rd16(L16(-0x118), bx) ^ cx));
L0409:
  ax = drd16(0x014c);
  if (rd16(ls, (uint16_t)(si + 2)) == ax) goto L0466;
  if (rd16(ls, (uint16_t)(si + 2)) == 0xffff) goto L0466;
  ax = (uint16_t)(rd16(ls, (uint16_t)(si + 0x12)) & 0x0fff);
  ax = (uint16_t)(ax / 0xfa);
  ax = (uint16_t)(ax + 1);
  L16(-8) = ax;
  if ((int16_t)ax <= 6) goto L0462;
  cx = 6;
L0431:
  if ((rd8(ls, (uint16_t)(si + 5)) & 2) != 0) cx++;
  bx = (uint16_t)(rd16(ls, si) << 4);
  bx = (uint16_t)(bx + drd16(0x1600));
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + 0xa), (uint8_t)cx);
  bx = (uint16_t)(rd16(ls, si) << 4);
  bx = (uint16_t)(bx + drd16(0x1600));
  es = drd16(0x1602);
  wr8(es, (uint16_t)(bx + 9), 1);
  goto L04fe;
L0462:
  cx = ax;
  goto L0431;
L0466:
  if (rd16(ls, (uint16_t)(si + 2)) != ax) goto L04fe;
  if ((rd8(ls, (uint16_t)(si + 5)) & 2) == 0) goto L04fe;
  ax = (uint16_t)(rd8(ls, (uint16_t)(si + 0x2c)) & 0xf);
  L16(-0x11c) = ax;
  if (ax == 1) goto L04ca;
  if (L16(-0x11c) == 3) goto L04ca;
  if (L16(-0x11c) == 6) goto L04ca;
  if (L16(-0x11c) == 8) goto L04ca;
  if ((int32_t)rd32(ls, (uint16_t)(si + 0x1c)) >= 0x3e8) goto L04a9;
  es = L16(-0x2a);
  wr8(es, (uint16_t)(di + 1), (uint8_t)(rd8(es, (uint16_t)(di + 1)) | 1));
L04a9:
  if ((int32_t)rd32(ls, (uint16_t)(si + 0x20)) >= 0x3e8) goto L04bb;
  es = L16(-0x2a);
  wr8(es, (uint16_t)(di + 1), (uint8_t)(rd8(es, (uint16_t)(di + 1)) | 2));
L04bb:
  if ((int16_t)rd16(ls, (uint16_t)(si + 0x26)) > 0) goto L04fe;
  if ((int16_t)rd16(ls, (uint16_t)(si + 0x26)) < 0) goto L04f6;
  ax = rd16(ls, (uint16_t)(si + 0x24));
  if (ax >= 0x3e8) goto L04fe;
  goto L04f6;
L04ca:
  if ((int32_t)rd32(ls, (uint16_t)(si + 0x1c)) >= 0xa) goto L04d9;
  es = L16(-0x2a);
  wr8(es, (uint16_t)(di + 1), (uint8_t)(rd8(es, (uint16_t)(di + 1)) | 1));
L04d9:
  if ((int32_t)rd32(ls, (uint16_t)(si + 0x20)) >= 0xa) goto L04e8;
  es = L16(-0x2a);
  wr8(es, (uint16_t)(di + 1), (uint8_t)(rd8(es, (uint16_t)(di + 1)) | 2));
L04e8:
  if ((int16_t)rd16(ls, (uint16_t)(si + 0x26)) > 0) goto L04fe;
  if ((int16_t)rd16(ls, (uint16_t)(si + 0x26)) < 0) goto L04f6;
  if (rd16(ls, (uint16_t)(si + 0x24)) >= 0xa) goto L04fe;
L04f6:
  es = L16(-0x2a);
  wr8(es, (uint16_t)(di + 1), (uint8_t)(rd8(es, (uint16_t)(di + 1)) | 4));
L04fe:
  si = (uint16_t)(si + 0x38);
  if (si < L16(-4)) goto L03ba;
  L16(-0x2c) = di;
L050f:
  L32(-0x30) = 0;
  L32(-0x28) = 0;
  L16(-0x12) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L06ba;
  L16(-6) = 0;
L0528:
  r = rd32(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + L16(-6)));
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  di = ax;
  L16(-0x18) = dx;
  if ((dx | ax) == 0) goto L06ba;
  if (drd16(0x255c) != 0) goto L0556;
  (void)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, ax, L16(-0x18), 0);
L0556:
  ax = drd16(0x014c);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(di + 2)) == ax) goto L0574;
  wr32(es, (uint16_t)(di + 0x68), L32(-0x28));
  L16(-0x28) = di;
  L16(-0x26) = es;
  goto L06a9;
L0574:
  if ((int16_t)rd16(es, (uint16_t)(di + 0x28)) > 0) goto L06a6;
  if ((int16_t)rd16(es, (uint16_t)(di + 0x2a)) > 0) goto L06a6;
  if ((int16_t)rd16(es, (uint16_t)(di + 0xc)) <= 0) goto L0597;
  L16(-0x36) = (uint16_t)(L16(-0x36) + 1);
  ax = rd16(es, (uint16_t)(di + 0xc));
  L16(-0x42) = (uint16_t)(L16(-0x42) + ax);
L0597:
  cx = 4;
  ax = (uint16_t)(di + 0x14);
  dx = es;
  L16(-4) = ax;
  L16(-2) = es;
  bx = ax;
  L16(-0x1a) = di;
  ls = dx;
L05ac:
  if ((int16_t)rd16(L16(-2), bx) > 0) goto L05be;
  bx = (uint16_t)(bx + 2);
  cx++;
  if ((int16_t)cx <= 0xd) goto L05ac;
L05be:
  if ((int16_t)cx > 0xd) goto L063e;
  wr32(ls, (uint16_t)(di + 0x68), L32(-0x30));
  L16(-0x30) = di;
  L16(-0x2e) = ls;
  if ((int16_t)L16(-0x1c) <= 5) goto L0638;
  if ((int16_t)rd16(ls, (uint16_t)(di + 0x62)) <= 1) goto L05ec;
  es = (uint16_t)(rd32(ls, (uint16_t)(di + 0x64)) >> 16);
  bx = (uint16_t)rd32(ls, (uint16_t)(di + 0x64));
  if ((rd8(es, (uint16_t)(bx + 0x1d)) & 0xf) == 1) goto L05f2;
L05ec:
  if (rd16(ls, (uint16_t)(di + 6)) == 0xffff) goto L0638;
L05f2:
  if ((int16_t)rd16(ls, (uint16_t)(di + 0x62)) <= 1) goto L060e;
  es = (uint16_t)(rd32(ls, (uint16_t)(di + 0x64)) >> 16);
  bx = (uint16_t)rd32(ls, (uint16_t)(di + 0x64));
  if ((rd8(es, (uint16_t)(bx + 0x1d)) & 0xf) != 1) goto L060e;
  cx = rd16(es, (uint16_t)(bx + 0x1a));
  goto L0611;
L060e:
  cx = rd16(ls, (uint16_t)(di + 6));
L0611:
  ax = drd16(0x1600);
  dx = drd16(0x1602);
  cx = (uint16_t)(cx << 4);
  ax = (uint16_t)(ax + cx);
  ax = (uint16_t)(ax + 0xa);
  bx = ax;
  ls = dx;
  si = ax;
  if (rd8(ls, si) == 0) goto L0630;
  wr8(ls, bx, (uint8_t)(rd8(ls, bx) | 0x80));
L0630:
  L16(-0x3e) = (uint16_t)(L16(-0x3e) + 1);
  goto L06a9;
L0638:
  L16(-0x40) = (uint16_t)(L16(-0x40) + 1);
  goto L06a9;
L063e:
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(di + 0x10)) > 0) goto L064f;
  if ((int16_t)rd16(es, (uint16_t)(di + 0x12)) <= 0) goto L06a9;
L064f:
  if ((int16_t)rd16(es, (uint16_t)(di + 0x62)) <= 1) goto L06a9;
  if ((int32_t)rd32(es, (uint16_t)(di + 0x58)) <= 0) goto L06a9;
  {
    uint32_t wp = rd32(es, (uint16_t)(di + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  if ((rd8(es, (uint16_t)(bx + 0x1d)) & 0xf) != 1) goto L06a9;
  si = L16(-0x2c);
  es = L16(-0x18);
  {
    uint32_t wp = rd32(es, (uint16_t)(di + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  bx = rd16(es, (uint16_t)(bx + 0x1a));
  bx = (uint16_t)(bx + bx);
  bx = (uint16_t)(bx + si);
  es = L16(-0x2a);
  L16(-0x120) = bx;
  L16(-0x11e) = es;
  if ((rd8(es, bx) & 0x18) >= 0x18) goto L06a9;
  ax = rd16(es, bx);
  cx = ax;
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx + 8));
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx ^ (uint8_t)ax));
  cx = (uint16_t)(cx & 0x18);
  wr16(es, bx, (uint16_t)(rd16(es, bx) ^ cx));
  goto L06a9;
L06a6:
  L16(-0x3c) = (uint16_t)(L16(-0x3c) + 1);
L06a9:
  L16(-6) = (uint16_t)(L16(-6) + 4);
  ax = drd16(0x553a);
  L16(-0x12) = (uint16_t)(L16(-0x12) + 1);
  if ((int16_t)L16(-0x12) < (int16_t)ax) goto L0528;
L06ba:
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  L16(-0x12) = 0;
  if ((int16_t)drd16(0x553a) <= 0) goto L0c02;
  L16(-6) = 0;
L06d8:
  r = rd32(drd16(0x00bc), (uint16_t)(drd16(0x00ba) + L16(-6)));
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  di = ax;
  L16(-0x18) = dx;
  if ((dx | ax) == 0) goto L0c02;
  ax = drd16(0x014c);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(di + 2)) != ax) goto L0bf1;
  L16(-8) = 0;
  ax = (uint16_t)(di + 0xc);
  L16(-4) = ax;
  L16(-2) = es;
  L16(-0x1a) = di;
  bx = L16(-8);
  di = ax;
  es = L16(-2);
L0715:
  if ((int16_t)rd16(es, di) <= 0) goto L0726;
  si = bx;
  if (rd8(rt_ss, (uint16_t)(rt_bp + si - 0x64)) == 0) goto L072f;
L0726:
  di = (uint16_t)(di + 2);
  bx++;
  if ((int16_t)bx < 0x10) goto L0715;
L072f:
  di = L16(-0x1a);
  if (bx != 0x10) goto L07cf;
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(di + 6)) == 0xffff) goto L07aa;
  r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, rd16(es, (uint16_t)(di + 6)));
  si = (uint16_t)r;
  L16(-0x14) = (uint16_t)(r >> 16);
  if ((L16(-0x14) | si) == 0) goto L07aa;
  ax = drd16(0x014c);
  es = L16(-0x14);
  if (rd16(es, (uint16_t)(si + 2)) != ax) goto L07aa;
  if ((rd8(es, (uint16_t)(si + 5)) & 2) != 0) goto L0779;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 5);
  if (ax != 0) goto L07aa;
L0779:
  es = L16(-0x18);
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, di));
L0789:
  es = drd16(0x4bf2);
  bx = drd16(0x4bf0);
  ax = rd16(es, (uint16_t)(bx + 0xa));
  ax = (uint16_t)((ax & 0xfff5) | 5);
  wr16(es, (uint16_t)(bx + 0xa), ax);
L0799:
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
  goto L0bf1;
L07aa:
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(di + 0x62)) <= 1) goto L07bd;
  if (rd16(es, (uint16_t)(di + 6)) == 0xffff) goto L0bf1;
L07bd:
  ax = (uint16_t)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, di, es, 0);
  if (ax != 0) goto L0bf1;
L07cf:
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(di + 0x28)) > 0) goto L07e0;
  if ((int16_t)rd16(es, (uint16_t)(di + 0x2a)) <= 0) goto L085a;
L07e0:
  if (rd16(es, (uint16_t)(di + 6)) == 0xffff) goto L085a;
  bx = 0;
  ax = (uint16_t)(drd16(0x0082) - drd16(0x49ad));
  cx = (uint16_t)(L16(-0x32) - 0xa);
  if (ax >= cx) goto L07fe;
  bx = rd16(es, (uint16_t)(di + 0x28));
L07fe:
  ax = (uint16_t)(drd16(0x0082) - drd16(0x4a3a));
  cx = (uint16_t)(L16(-0x32) - 0xa);
  if (ax >= cx) goto L0813;
  bx = (uint16_t)(bx + rd16(es, (uint16_t)(di + 0x2a)));
L0813:
  if ((int16_t)bx <= 0) goto L0bf1;
  si = rd16(es, (uint16_t)(di + 6));
  si = (uint16_t)(si + si);
  si = (uint16_t)(si + L16(-0x2c));
  es = L16(-0x2a);
  wr8(es, si, (uint8_t)(rd8(es, si) | 0x40));
  ax = L16(-0x34);
  ax = (uint16_t)(ax + ax);
  if ((int16_t)ax <= (int16_t)bx) goto L0838;
  ax = 1;
  goto L083a;
L0838:
  ax = 0;
L083a:
  ax = (uint16_t)(ax << 5);
  es = L16(-0x18);
  si = rd16(es, (uint16_t)(di + 6));
  si = (uint16_t)(si + si);
  si = (uint16_t)(si + L16(-0x2c));
  es = L16(-0x2a);
  cx = rd8(es, si);
  cx = (uint16_t)((cx & 0xff00) | (uint8_t)((uint8_t)cx ^ (uint8_t)ax));
  cx = (uint16_t)(cx & 0x20);
  wr16(es, si, (uint16_t)(rd16(es, si) ^ cx));
  goto L0bf1;
L085a:
  cx = 4;
  ax = (uint16_t)(di + 0x14);
  bx = ax;
  L16(-2) = es;
  L16(-8) = cx;
  di = cx;
L086a:
  es = L16(-2);
  if ((int16_t)rd16(es, bx) > 0) goto L087c;
  bx = (uint16_t)(bx + 2);
  di++;
  if ((int16_t)di <= 0xd) goto L086a;
L087c:
  if ((int16_t)di > 0xd) goto L0944;
  bx = L16(-0x1a);
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x14)) > 0) goto L08e8;
  {
    const uint16_t a_[] = {bx, es};
    (void)call21(0x32aa, 2, a_);
  }
  bx = L16(-0x1a);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(bx + 0x62)) != 1) goto L0bf1;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax >= 0x4b) goto L0bf1;
  if ((int16_t)di > 9) goto L08d6;
  (void)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, L16(-0x1a), L16(-0x18), 6, 9, 0x64, 0, 0xc8, 0);
  goto L0bf1;
L08d6:
  (void)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, L16(-0x1a), L16(-0x18), 0xa, 0xd, 0x64, 0, 0xc8, 0);
  goto L0bf1;
L08e8:
  ax = rd16(es, (uint16_t)(bx + 0x14));
  ax = (uint16_t)(ax + ax);
  if ((int16_t)ax >= (int16_t)L16(-0x34)) goto L08fa;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x62)) <= 1) goto L0916;
L08fa:
  ax = (uint16_t)((drd8(0x0080) & 0x10) >> 4);
  (void)CALL_ORIG(18, 0x1434, FAR_CDECL, 7, bx, es, L16(-0x30), L16(-0x2e), L16(-0x28), L16(-0x26), ax);
L0916:
  bx = L16(-0x1a);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(bx + 0x62)) != 1) goto L0bf1;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax >= 0x4b) goto L0bf1;
  (void)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, L16(-0x1a), L16(-0x18), 4, 5, 0x64, 0, 0xc8, 0);
  goto L0bf1;
L0944:
  bx = L16(-0x1a);
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x62)) > 1) goto L0bf1;
  if (drd16(0x0082) > 5) goto L096c;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0xc)) <= 0) goto L096c;
  if (rd16(es, (uint16_t)(bx + 0x62)) != 1) goto L096c;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, bx));
  goto L0789;
L096c:
  if ((int16_t)rd16(es, (uint16_t)(bx + 0xe)) <= 0) goto L0a50;
  ax = (uint16_t)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, bx, es, 0);
  L16(-2) = ax;
  if ((uint16_t)(ax + 1) != 0) goto L09b2;
  {
    uint32_t wp = rd32(L16(-0x18), (uint16_t)(L16(-0x1a) + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  ax = (uint16_t)(rd8(es, (uint16_t)(bx + 0xb)) & 0xf);
  if (ax != 1) goto L0bf1;
  {
    uint32_t wp = rd32(L16(-0x18), (uint16_t)(L16(-0x1a) + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  bx = rd16(es, (uint16_t)(bx + 8));
  bx = (uint16_t)(bx + bx);
  wr8(L16(-0x2a), (uint16_t)(bx + L16(-0x2c)), (uint8_t)(rd8(L16(-0x2a), (uint16_t)(bx + L16(-0x2c))) | 1));
  goto L0bf1;
L09b2:
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(L16(-0x18), L16(-0x1a)));
  bx = L16(-0x1a);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(bx + 6)) == 0xffff) goto L0a11;
  (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, rd16(es, (uint16_t)(bx + 6)), 0x4c08);
  ax = drd16(0x014c);
  if (drd16(0x4c0a) != ax) goto L0a11;
  bx = L16(-0x1a);
  es = L16(-0x18);
  (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, rd16(es, (uint16_t)(bx + 6)), 2, rd16(es, bx), 3, 0xfa);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, rd16(L16(-0x18), L16(-0x1a)), 0x4b8c);
L0a11:
  if (L16(-2) == 0xffff) goto L0a50;
  ax = (uint16_t)CALL_ORIG(18, 0x0864, FAR_CDECL, 3, L16(-0x1a), L16(-0x18), L16(-2));
  if (ax == 0) goto L0bf1;
  bx = L16(-0x1a);
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x62)) <= 1) goto L0bf1;
  {
    uint32_t wp = rd32(es, (uint16_t)(bx + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  si = rd16(es, (uint16_t)(bx + 0x1a));
  si = (uint16_t)(si << 4);
  wr8(drd16(0x1602), (uint16_t)(drd16(0x1600) + si + 0xf), 4);
  goto L0bf1;
L0a50:
  bx = L16(-0x1a);
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x10)) > 0) goto L0bbc;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0x12)) > 0) goto L0bbc;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0xc)) <= 0) goto L0bf1;
  if (drd16(0x0082) <= 0x28) goto L0bf1;
  if (rd16(es, (uint16_t)(bx + 0x62)) != 1) goto L0bf1;
  /* the flags of the CMP above are EQUAL here, so the JLE to 21:0x0aa0 is always taken: the code at
   * 21:0x0a82 to 0x0a9d (a 3bb4 order for a fleet with more waypoints) cannot be reached */
  goto L0aa0;
L0aa0:
  if ((int16_t)L16(-0x36) > 0x37) goto L0aba;
  if ((int16_t)L16(-0x36) <= 0x28) goto L0ad5;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 3);
  if (ax == 0) goto L0ad5;
L0aba:
  ax = (uint16_t)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, L16(-0x1a), L16(-0x18), 0, 0, 0x48, 0, 0x6c, 0);
  if (ax != 0) goto L0bf1;
L0ad5:
  bx = L16(-0x1a);
  es = L16(-0x18);
  if ((int16_t)rd16(es, (uint16_t)(bx + 0xc)) < 7) goto L0b74;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 5);
  if (ax != 0) goto L0b74;
  ax = (uint16_t)CALL_ORIG(18, 0x3ad8, FAR_CDECL, 4, rd16(L16(-0x18), (uint16_t)(L16(-0x1a) + 8)), rd16(L16(-0x18), (uint16_t)(L16(-0x1a) + 0xa)), 0x69, 1);
  si = ax;
  if ((int16_t)si == -1) goto L0b74;
  bx = L16(-0x1a);
  es = L16(-0x18);
  if (rd16(es, (uint16_t)(bx + 6)) == ax) goto L0b74;
  (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, bx, es, 1);
  L16(-0x92) = si;
  ax = L16(-0x90);
  ax = (uint16_t)((ax & 0xf1ff) | 0x0100);
  L16(-0x90) = ax;
  bx = si;
  bx = (uint16_t)(bx << 2);
  L32(-0x96) = drd32((uint16_t)(bx + 0x31ba));
  ax = L16(-0x90);
  ax = (uint16_t)((ax & 0xfff6) | 0x0006);
  L16(-0x90) = ax;
  L8(-0x8f) = (uint8_t)(L8(-0x8f) | 0x10);
  ax = L16(-0x90);
  ax = (uint16_t)((ax & 0xff4f) | 0x0040);
  L16(-0x90) = ax;
  (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, L16(-0x1a), L16(-0x18), (uint16_t)(rt_bp - 0x96), 0);
  goto L0bf1;
L0b74:
  {
    uint32_t wp = rd32(L16(-0x18), (uint16_t)(L16(-0x1a) + 0x64));
    es = (uint16_t)(wp >> 16);
    bx = (uint16_t)wp;
  }
  if ((rd8(es, (uint16_t)(bx + 0xa)) & 0xf) == 6) goto L0bf1;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(L16(-0x18), L16(-0x1a)));
  es = drd16(0x4bf2);
  bx = drd16(0x4bf0);
  ax = rd16(es, (uint16_t)(bx + 0xa));
  ax = (uint16_t)((ax & 0xfff6) | 0x0006);
  wr16(es, (uint16_t)(bx + 0xa), ax);
  wr16(drd16(0x4bf2), (uint16_t)(drd16(0x4bf0) + 0xc), 5);
  wr16(drd16(0x4bf2), (uint16_t)(drd16(0x4bf0) + 0xe), 5);
  goto L0799;
L0bbc:
  if (rd8(es, (uint16_t)(bx + 0x60)) == 4) goto L0be2;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, rd16(es, bx));
  dwr8(0x4bec, 4);
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
L0be2:
  {
    const uint16_t a_[] = {L16(-0x1a), L16(-0x18), L16(-0x2c), L16(-0x2a)};
    (void)call21(0x23d0, 4, a_);
  }
L0bf1:
  L16(-6) = (uint16_t)(L16(-6) + 4);
  ax = drd16(0x553a);
  L16(-0x12) = (uint16_t)(L16(-0x12) + 1);
  if ((int16_t)L16(-0x12) < (int16_t)ax) goto L06d8;
L0c02:
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  L16(-0x28) = 0;
  if ((int16_t)drd16(0x4b62) <= 0) goto L1054;
  L16(-0x26) = 0;
L0c20:
  r = rd32(drd16(0x160a), (uint16_t)(drd16(0x1608) + L16(-0x26)));
  si = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  L16(-0x14) = dx;
  if ((dx | si) == 0) goto L1054;
  es = L16(-0x14);
  ax = rd16(es, si);
  cx = ax;
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + drd16(0x1604));
  dx = drd16(0x1606);
  bx = ax;
  ax = (uint16_t)(ax + 2);
  L16(-0x1a) = ax;
  L16(-0x18) = dx;
  bx = (uint16_t)(bx + L16(-0x3a));
  L16(-0x2c) = bx;
  L16(-0x2a) = dx;
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, cx);
  (void)CALL_ORIG(26, 0x0100, FAR_CDECL, 1, param_1);
  ax = (uint16_t)CALL_ORIG(28, 0x41d4, FAR_CDECL, 1, drd16(0x014c));
  dx = (uint16_t)((int16_t)ax < 0 ? 0xffff : 0);
  es = L16(-0x14);
  L32(-6) = (uint32_t)((int32_t)((int64_t)(int32_t)rd32(es, (uint16_t)(si + 0x28)) * (int32_t)(int16_t)ax));
  (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, si, L16(-0x14), (uint16_t)(rt_bp - 0x74));
  (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, si, L16(-0x14), (uint16_t)(rt_bp - 0x84));
  L16(-0x1c) = 0;
  L16(-2) = 0;
  L16(-0x16) = si;
  bx = 0;
L0cc3:
  si = bx;
  eax = L32((int16_t)(si - 0x74));
  if ((int32_t)L32((int16_t)(si - 0x84)) > (int32_t)eax) goto L1038;
  bx = (uint16_t)(bx + 4);
  if ((int16_t)bx < 0x10) goto L0cc3;
  si = L16(-0x16);
  di = (uint16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, si, L16(-0x14), drd16(0x014c));
  ax = (uint16_t)CALL_ORIG(9, 0x462a, FAR_CDECL, 3, si, L16(-0x14), drd16(0x014c));
  L16(-8) = ax;
  if ((int16_t)di >= 0xa) goto L0d3c;
  q = idiv32((int32_t)(L32(-0x68) - L32(-0x78)), 0x46);
  ax = (uint16_t)q;
  ax = (uint16_t)(ax + 1);
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xc, ax, 1, 1);
  L16(-0x1c) = 1;
  goto L1038;
L0d3c:
  L16(-2) = di;
  if ((int16_t)ax <= (int16_t)di) goto L0d6c;
  if ((int32_t)(L32(-0x68) - L32(-0x78)) <= 0x46) goto L0d6c;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0xc, 1, 1, 1);
  L16(-0x1c) = 1;
L0d6c:
  es = L16(-0x14);
  if ((rd8(es, (uint16_t)(si + 5)) & 2) == 0) goto L1038;
  ax = (uint16_t)(rd8(es, (uint16_t)(si + 0x2c)) & 0xf);
  L16(-0x11c) = ax;
  if (ax == 1) goto L1038;
  if (L16(-0x11c) == 3) goto L1038;
  if (L16(-0x11c) == 6) goto L1038;
  if (L16(-0x11c) == 8) goto L1038;
  di = L16(-0x1a);
  es = L16(-0x18);
  if ((rd8(es, di) & 8) == 0) goto L0db9;
  if ((int32_t)L32(-6) <= 0x157c) goto L0e20;
L0db9:
  if ((rd8(L16(-0x2a), L16(-0x2c)) & 1) != 0) goto L0e20;
  if ((int32_t)L32(-0x10) >= 0x28) goto L0e20;
  ax = (uint16_t)CALL_ORIG(18, 0x5fe6, FAR_CDECL, 2, si, L16(-0x14));
  if (ax == 0) goto L0e20;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 1, 1, 2, 1);
  es = L16(-0x18);
  wr8(es, di, (uint8_t)(rd8(es, di) | 8));
  L16(-0x1c) = 1;
  if ((int32_t)L32(-6) <= 0x3a98) goto L0e27;
  if (drd16(0x0082) >= 0x64) goto L0e27;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 1, 1, 2, 1);
  goto L0e27;
L0e20:
  es = L16(-0x18);
  wr8(es, di, (uint8_t)(rd8(es, di) & 0xf7));
L0e27:
  es = L16(-0x14);
  if ((int32_t)rd32(es, (uint16_t)(si + 0x28)) <= 0x7d0) goto L0e7d;
  if ((drd8(0x4310) & 2) != 0) goto L0e7d;
  if ((int32_t)L32(-0xc) >= 0x32) goto L0e7d;
  if ((rd8(L16(-0x2a), L16(-0x2c)) & 6) >= 2) goto L0e7d;
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, si, L16(-0x14), 0x27ba, SEG_SEL(21));
  if ((((uint16_t)(r >> 16)) | (uint16_t)r) == 0) goto L0e7d;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, L16(-0x24), 1, 2, 1);
  L16(-0x1c) = 1;
L0e7d:
  if (drd16(0x417a) != 5) goto L0f3e;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 4);
  if (ax != 0) goto L0f3e;
  if ((int16_t)L16(-0x42) >= 0x2710) goto L0f3e;
  es = L16(-0x14);
  ax = rd16(es, si);
  L16(-2) = ax;
  cx = 0;
  L16(-8) = cx;
  if ((int16_t)drd16(0x553a) <= (int16_t)cx) goto L0ef9;
  di = drd16(0x00ba);
  ls = drd16(0x00bc);
  L16(-4) = ls;
L0ebf:
  {
    uint32_t fp = rd32(ls, di);
    es = (uint16_t)(fp >> 16);
    bx = (uint16_t)fp;
  }
  if (((uint16_t)(es | bx)) == 0) goto L0ef9;
  ax = L16(-2);
  if (rd16(es, (uint16_t)(bx + 6)) != ax) goto L0ee4;
  if ((int16_t)rd16(es, (uint16_t)(bx + 0xc)) <= 0) goto L0ee4;
  ax = drd16(0x014c);
  if (rd16(es, (uint16_t)(bx + 2)) == ax) goto L0ef2;
L0ee4:
  di = (uint16_t)(di + 4);
  cx++;
  if ((int16_t)drd16(0x553a) > (int16_t)cx) goto L0ebf;
  goto L0ef9;
L0ef2:
  ax = rd16(es, (uint16_t)(bx + 0xc));
  L16(-8) = ax;
L0ef9:
  if ((int16_t)L16(-8) < 0xa) goto L0f52;
  if ((int16_t)L16(-8) >= 0x11) goto L0f56;
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0xa);
  if (ax != 0) goto L0f3e;
L0f15:
  ax = L16(-8);
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + 1);
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, ax);
  if (ax != 0) goto L0f3e;
  (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, ax, 4, 2, 1);
  L16(-0x1c) = 1;
  goto L0f3e;
L0f52:
  goto L0f15;
L0f56:
  goto L0f3e;
L0f3e:
  es = L16(-0x2a);
  ax = rd16(es, L16(-0x2c));
  L16(-0x122) = ax;
  if ((ax & 0x20) == 0) goto L0f5a;
  ax = L16(-0x22);
  goto L0f5d;
L0f5a:
  ax = 0xffff;
L0f5d:
  L16(-2) = ax;
  ax = (uint16_t)(ax + 1);
  if (ax != 0) goto L0fe7;
  if ((L16(-0x122) & 0x40) != 0) goto L0fe7;
  if ((int16_t)L16(-0x3c) >= 0x28) goto L0fe7;
  r = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, L16(-0x16), L16(-0x14), 0x2bd8, SEG_SEL(21));
  L16(-4) = (uint16_t)r;
  L16(-2) = (uint16_t)(r >> 16);
  if (((uint16_t)(r >> 16) | (uint16_t)r) == 0) goto L0fd2;
  {
    uint32_t cf = PLANET_XY(rd16(L16(-2), L16(-4)));
    uint32_t cg = PLANET_XY(rd16(L16(-0x14), L16(-0x16)));
    r = CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)cg, (uint16_t)(cg >> 16), (uint16_t)cf, (uint16_t)(cf >> 16));
  }
  ax = (uint16_t)r;
  dx = (uint16_t)(r >> 16);
  if ((int16_t)dx > 1) goto L0fd2;
  if ((int16_t)dx < 1) goto L0fbe;
  if (ax > 0x5f90) goto L0fd2;
L0fbe:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax < 0x32) goto L0fe1;
L0fcd:
  ax = 0xffff;
  goto L0fe4;
L0fd2:
  ax = (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, 0x64);
  if ((int16_t)ax >= 0xa) goto L0fcd;
L0fe1:
  ax = L16(-0x22);
L0fe4:
  L16(-2) = ax;
L0fe7:
  if ((int16_t)L16(-0x40) <= 0x78) goto L0ff2;
  L16(-0x20) = 0xffff;
L0ff2:
  if ((int16_t)L16(-0x3e) <= 0xfa) goto L0ffe;
  L16(-0x38) = 0xffff;
L0ffe:
  {
    const uint16_t a_[] = {L16(-0x16), L16(-0x14), L16(-0x34), L16(-0x20), L16(-0x38), L16(-2)};
    di = (uint16_t)call21(0x30c2, 6, a_);
  }
  ax = (uint16_t)(di != 0 ? 1 : 0);
  L16(-0x1c) = (uint16_t)(L16(-0x1c) | ax);
  if (di == 1) L16(-0x3e) = (uint16_t)(L16(-0x3e) + 1);
  if (di == 2) L16(-0x3c) = (uint16_t)(L16(-0x3c) + 1);
  if (di == 3) L16(-0x40) = (uint16_t)(L16(-0x40) + 1);
L1038:
  (void)CALL_ORIG(26, 0x0810, FAR_CDECL, 1, L16(-0x1c));
  L16(-0x26) = (uint16_t)(L16(-0x26) + 4);
  ax = drd16(0x4b62);
  L16(-0x28) = (uint16_t)(L16(-0x28) + 1);
  if ((int16_t)L16(-0x28) < (int16_t)ax) goto L0c20;
L1054:
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  ax = (uint16_t)CALL_ORIG(18, 0x50c8, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x74), (uint16_t)(rt_bp - 0x84));
  (void)CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, L16(-0x44), param_1, ax, (uint16_t)(rt_bp - 0x74), (uint16_t)(rt_bp - 0x84));
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  (void)call21(0x123e, 0, NULL);
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  (void)call21(0x1e2a, 0, NULL);
  FRAME_LEAVE();
}
