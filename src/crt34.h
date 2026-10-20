/* Stand-ins for the C runtime functions of segment 34 (not decompiled, and the x87 stack cannot
 * cross CALL_ORIG). Each game segment that calls one of them includes this header, so
 * the stand-in is written once. They move into a src/seg34.c when segment 34 is decompiled.
 *
 * crt_sqrt_34_0d68 replaces the segment 34 CRT sqrt, 34:0x0D68 (mov dx,0x1AAE; jmp 34:0x1C34):
 * the operand is ST(0) and the result replaces it. Callers: 27 call sites in segments 5, 7, 8, 9,
 * 11, 18, 22 and 33. What the original does, read from 34:0x1C34..0x1CC7 and 34:0x2068: when the
 * byte DS:0x1E1E is 0 it first stores the operand as a double at DS:0x1C6A (FST m64); it
 * classifies the operand (FXAM); at 34:0x2068 it does FNSTCW, sets CW = (low byte | 0x38) | 0x1300
 * (bits 3-5, 8, 9 and 12 set: precision control 64 bits), FLDCW, FSQRT,
 * clears the exceptions, stores the result as a double at DS:0x18FC (FST m64), sets the byte
 * DS:0x1C9A to 1 and restores the old control word at 0x1CC3. The game's word is 0x1330, which the
 * change leaves as it is, so game results do not depend on it; the stand-in still mirrors it so
 * that a caller running under another word (0x037F in the tests) gets the same word back.
 * A negative operand takes the domain-error path (the matherr hook and the error message), which
 * is not modelled: the run stops here. */
#ifndef STARS_SRC_CRT34_H
#define STARS_SRC_CRT34_H

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/fp80.h"
#include "src/stars.h"

static __attribute__((unused)) Fp80 crt_sqrt_34_0d68(Fp80 v)
{
  uint16_t signexp;
  memcpy(&signexp, (const uint8_t *)&v + 8, 2);
  if (signexp & 0x8000) {
    fprintf(stderr, "34:0x0d68: sqrt of a negative operand (domain error) is not modelled\n");
    abort();
  }
  if (g_DAT_1110_1e1e == 0) {
    double d = fp80_to_f64(v);
    memcpy(g_DAT_1110_1c6a, &d, 8);
  }
  uint16_t old_cw = fp80_get_cw();                       /* FNSTCW */
  fp80_set_cw((uint16_t)(((old_cw & 0xff) | 0x38) | 0x1300)); /* FLDCW */
  Fp80 r = fp80_sqrt(v);
  double d = fp80_to_f64(r);
  memcpy(g_DAT_1110_18fc, &d, 8);
  *(uint8_t *)LINN(0x1c9a) = 1;                          /* a byte store */
  fp80_set_cw(old_cw);                                   /* 34:0x1CC3 */
  return r;
}


/* crt_pow_34_0d84 replaces the segment 34 CRT pow, 34:0x0D84 (mov dx,0x1AF8; jmp 34:0x1C04), a
 * two-operand function: ST(1) is the base and ST(0) the exponent, the result replaces both.
 * Callers: 21:0x123e, 21:0x29da and 7:0x0262 (they reach it through the 6-byte stub at 0x0D84).
 * What the original does for a positive finite base and a finite exponent, read from 34:0x1C04,
 * 0x20AA (class dispatch by FXAM), 0x1298 and 0x13CC: when the byte DS:0x1E1E is 0 it first stores
 * the base as a double at DS:0x1C6A and the exponent as a double at DS:0x1C72 (FST m64); it
 * FNSTCWs, sets CW = (low byte | 0x38) | 0x1300 (overflow masked, 64-bit precision), t = exponent *
 * log2(base) (FYL2X), n = FRNDINT(t) (nearest even), f = t - n; r = F2XM1(|f|) + 1, r = 1 / r when
 * f is negative, result = r * 2^n (FSCALE, skipped when n is 0); a zero exponent gives 1.0 (FLD1).
 * It then sets the byte DS:0x1C9A to 1, stores the result as a double at DS:0x18FC (FST m64) and
 * restores the old control word.
 * FYL2X and F2XM1 are the two transcendental steps (docs/CONVENTIONS.md "Floating point": each needs
 * its own equivalence statement). They are fp80_fyl2x and fp80_f2xm1 (runtime/fp80.c), which follow
 * the x87 of the harness' Unicorn 2.1.4 (QEMU 5.0), the oracle of the diff tests: FYL2X is
 * log(x) / log(2.0) * y and F2XM1 is 2^f - 1.0 in double arithmetic, rounded to a double and widened,
 * with the log and the power taken correctly rounded from 128-bit integer arithmetic (no host libm).
 * The harness runs the same helpers for the original's FYL2X and F2XM1 (harness_hook_x87_transc,
 * owner decision 2026-10-20), so this agrees with the oracle bit for bit (t_pow_34_0d84_native; the
 * 21:0x29da and 21:0x123e diffs compare the double this stores at DS:0x18FC); the helpers' values are
 * checked only by the reference vectors in harness/tests/test_fp.c, and not necessarily equal to a
 * real x87 (microcode with a 64-bit significand; follow-up stars-5yb); the callers
 * truncate the result times a small integer (ftol), so a last-bit difference changes a game result
 * only within about 1e-15 of an integer. A base that is not positive and normal, a non-finite or
 * denormal exponent and |t| beyond 2^14 (DS:0x1ADA; the overflow and domain error paths, the matherr
 * hook) are not modelled: the run stops. */
static __attribute__((unused)) uint16_t pow_se(Fp80 v)
{
  uint16_t se;
  memcpy(&se, (const uint8_t *)&v + 8, 2);
  return se;
}
static __attribute__((unused)) Fp80 crt_pow_34_0d84(Fp80 base, Fp80 expo)
{
  uint16_t bse = pow_se(base), ese = pow_se(expo);
  uint16_t old_cw;
  if ((bse & 0x8000) || (bse & 0x7fff) == 0 || (bse & 0x7fff) == 0x7fff ||
      (ese & 0x7fff) == 0x7fff || ((ese & 0x7fff) == 0 && ((const uint8_t *)&expo)[7] != 0)) {
    fprintf(stderr, "34:0x0d84: pow of a non-positive or non-finite operand is not modelled\n");
    abort();
  }
  if (g_DAT_1110_1e1e == 0) {
    double d1 = fp80_to_f64(base), d2 = fp80_to_f64(expo);
    memcpy(g_DAT_1110_1c6a, &d1, 8);
    memcpy(g_DAT_1110_1c72, &d2, 8);
  }
  old_cw = fp80_get_cw();
  fp80_set_cw((uint16_t)(((old_cw & 0xff) | 0x38) | 0x1300));
  Fp80 result;
  if ((ese & 0x7fff) == 0) {
    result = fp80_from_i32(1);
  } else {
    Fp80 t = fp80_fyl2x(base, expo); /* FYL2X: expo * log2(base), rounded to a double */
    Fp80 limit;
    {
      uint64_t sg = 0x8000000000000000ull; /* the xword at DS:0x1ADA: 2^14 */
      uint16_t se = 0x400d;
      memcpy(&limit, &sg, 8);
      memcpy((uint8_t *)&limit + 8, &se, 2);
    }
    Fp80 abst = t;
    uint16_t tse = (uint16_t)(pow_se(t) & 0x7fff);
    memcpy((uint8_t *)&abst + 8, &tse, 2);
    if (!extF80_lt(abst, limit)) {
      fprintf(stderr, "34:0x0d84: pow overflow (|t| beyond the limit) is not modelled\n");
      abort();
    }
    Fp80 n = extF80_roundToInt(t, softfloat_round_near_even, false); /* FRNDINT */
    Fp80 f = fp80_sub(t, n);
    bool n_zero = extF80_eq(n, fp80_from_i32(0));
    bool f_neg = (pow_se(f) & 0x8000) != 0 && !extF80_eq(f, fp80_from_i32(0));
    uint16_t fse = (uint16_t)(pow_se(f) & 0x7fff);
    Fp80 af = f;
    memcpy((uint8_t *)&af + 8, &fse, 2); /* FABS */
    Fp80 r = fp80_f2xm1(af); /* F2XM1: 2^af - 1, rounded to a double */
    r = fp80_add(r, fp80_from_i32(1));
    if (f_neg) r = fp80_div(fp80_from_i32(1), r);
    if (!n_zero) {
      int32_t ni = fp80_to_i32(n);
      uint16_t rse = pow_se(r);
      int32_t ne = (int32_t)(rse & 0x7fff) + ni;
      if (ne <= 0 || ne >= 0x7fff) {
        fprintf(stderr, "34:0x0d84: pow result out of the normal range is not modelled\n");
        abort();
      }
      rse = (uint16_t)((rse & 0x8000) | ne);
      memcpy((uint8_t *)&r + 8, &rse, 2); /* FSCALE by the integer n */
    }
    result = r;
  }
  *(uint8_t *)LINN(0x1c9a) = 1;
  double d = fp80_to_f64(result);
  memcpy(g_DAT_1110_18fc, &d, 8);
  fp80_set_cw(old_cw);
  return result;
}

#endif /* STARS_SRC_CRT34_H */
