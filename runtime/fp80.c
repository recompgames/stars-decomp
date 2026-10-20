#include "runtime/fp80.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t g_cw = FP80_CW_DEFAULT;
static uint16_t g_exc;
static Fp80TrapFn g_trap;

void fp80_set_trap_handler(Fp80TrapFn fn) { g_trap = fn; }
void fp80_clear_exceptions(void) { g_exc = 0; }
uint16_t fp80_exceptions(void) { return g_exc; }

/* Folds SoftFloat's flags (cleared before the operation) and a denormal-operand flag into
 * g_exc, and calls the trap handler for every raised flag that is unmasked in g_cw. */
static void raise(uint_fast8_t sf, uint16_t extra) {
  uint16_t e = extra;
  if (sf & softfloat_flag_invalid) e |= FP80_EXC_IE;
  if (sf & softfloat_flag_infinite) e |= FP80_EXC_ZE;
  if (sf & softfloat_flag_overflow) e |= FP80_EXC_OE;
  if (sf & softfloat_flag_underflow) e |= FP80_EXC_UE;
  if (sf & softfloat_flag_inexact) e |= FP80_EXC_PE;
  g_exc |= e;
  if (e & ~g_cw & 0x3Fu) {
    if (g_trap) g_trap((uint16_t)(e & ~g_cw & 0x3Fu));
  }
}

/* DE: a nonzero operand with a zero biased exponent (denormal / pseudo-denormal). */
static uint16_t de(Fp80 x) {
  /* SoftFloat is built little-endian (platform.h, which users of softfloat_types.h do not
   * see): the 64-bit significand is at bytes 0-7, sign and exponent at bytes 8-9. */
  uint64_t signif;
  uint16_t se;
  memcpy(&signif, &x, 8);
  memcpy(&se, (const uint8_t *)&x + 8, 2);
  return ((se & 0x7FFF) == 0 && signif != 0) ? FP80_EXC_DE : 0;
}
/* A binary operation: clear SoftFloat's flags, run, fold, restore the caller's flags. */
#define BINOP(fn, a, b)                                  \
  do {                                                   \
    uint_fast8_t saved = softfloat_exceptionFlags;       \
    Fp80 r_;                                             \
    softfloat_exceptionFlags = 0;                        \
    r_ = fn(a, b);                                       \
    raise(softfloat_exceptionFlags, (uint16_t)(de(a) | de(b))); \
    softfloat_exceptionFlags = saved;                    \
    return r_;                                           \
  } while (0)

void fp80_set_cw(uint16_t cw) {
  static const uint_fast8_t rc[4] = {softfloat_round_near_even, softfloat_round_min,
                                     softfloat_round_max, softfloat_round_minMag};
  g_cw = cw;
  softfloat_roundingMode = rc[(cw >> 10) & 3];
  switch ((cw >> 8) & 3) { /* 00 = 24-bit, 10 = 53-bit, 11 = 64-bit significand */
    case 0: extF80_roundingPrecision = 32; break;
    case 2: extF80_roundingPrecision = 64; break;
    default: extF80_roundingPrecision = 80; break; /* 01 is reserved: treat as 64-bit */
  }
}

uint16_t fp80_get_cw(void) { return g_cw; }

Fp80 fp80_from_f64(double d) {
  float64_t f;
  uint64_t u;
  Fp80 r;
  memcpy(&f, &d, sizeof f);
  memcpy(&u, &d, sizeof u);
  r = f64_to_extF80(f);
  /* FLD m64 of a denormal double raises DE (the extended result is normal). */
  raise(0, ((u >> 52) & 0x7FF) == 0 && (u & 0xFFFFFFFFFFFFFull) ? FP80_EXC_DE : 0);
  return r;
}

Fp80 fp80_from_i32(int32_t v) { return i32_to_extF80(v); }

double fp80_to_f64(Fp80 x) {
  uint_fast8_t saved = softfloat_exceptionFlags;
  float64_t f;
  double d;
  softfloat_exceptionFlags = 0;
  f = extF80_to_f64(x);
  raise(softfloat_exceptionFlags, de(x));
  softfloat_exceptionFlags = saved;
  memcpy(&d, &f, sizeof d);
  return d;
}

Fp80 fp80_add(Fp80 a, Fp80 b) { BINOP(extF80_add, a, b); }
Fp80 fp80_sub(Fp80 a, Fp80 b) { BINOP(extF80_sub, a, b); }
Fp80 fp80_mul(Fp80 a, Fp80 b) { BINOP(extF80_mul, a, b); }
Fp80 fp80_div(Fp80 a, Fp80 b) { BINOP(extF80_div, a, b); }

Fp80 fp80_sqrt(Fp80 a) {
  uint_fast8_t saved = softfloat_exceptionFlags;
  Fp80 r;
  softfloat_exceptionFlags = 0;
  r = extF80_sqrt(a);
  raise(softfloat_exceptionFlags, de(a));
  softfloat_exceptionFlags = saved;
  return r;
}

int32_t fp80_to_i32(Fp80 x) {
  uint_fast8_t saved = softfloat_exceptionFlags;
  int_fast32_t v;
  softfloat_exceptionFlags = 0;
  v = extF80_to_i32(x, softfloat_roundingMode, false);
  if (softfloat_exceptionFlags & softfloat_flag_invalid) v = INT32_MIN;
  raise(softfloat_exceptionFlags, de(x));
  softfloat_exceptionFlags = saved;
  return (int32_t)v;
}

int16_t fp80_to_i16(Fp80 x) {
  int32_t v = fp80_to_i32(x);
  /* v == INT32_MIN is the indefinite (or a real -2^31): both are out of int16 range. */
  if (v < INT16_MIN || v > INT16_MAX) return INT16_MIN;
  return (int16_t)v;
}

int32_t fp80_to_i32_ftol(Fp80 x) {
  uint_fast8_t saved = softfloat_exceptionFlags;
  int_fast64_t v;
  softfloat_exceptionFlags = 0;
  v = extF80_to_i64(x, softfloat_round_minMag, false);
  if (softfloat_exceptionFlags & softfloat_flag_invalid) v = 0; /* indefinite 2^63: low word 0 */
  raise(softfloat_exceptionFlags, de(x));
  softfloat_exceptionFlags = saved;
  return (int32_t)(uint32_t)((uint64_t)v & 0xFFFFFFFFu);
}

/* ---- FYL2X and F2XM1 (stars-nf6) -------------------------------------------------------- */

/* 128-bit fixed point, value = v / 2^FB with FB = 124 (room for values below 4). */
typedef unsigned __int128 u128;
#define FB 124
#define FX_ONE ((u128)1 << FB)

static u128 fx_mul(u128 a, u128 b) { /* (a * b) >> FB, from the 256-bit product */
  uint64_t a0 = (uint64_t)a, a1 = (uint64_t)(a >> 64), b0 = (uint64_t)b, b1 = (uint64_t)(b >> 64);
  u128 p00 = (u128)a0 * b0, p01 = (u128)a0 * b1, p10 = (u128)a1 * b0, p11 = (u128)a1 * b1;
  u128 mid = (p00 >> 64) + (uint64_t)p01 + (uint64_t)p10;
  u128 lo = (u128)(uint64_t)p00 | (mid << 64);
  u128 hi = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);
  return (hi << (128 - FB)) | (lo >> FB);
}

static u128 fx_div(u128 a, u128 b) { /* floor(a * 2^FB / b), for a < b < 2^126 */
  u128 q = 0, rem = a;
  for (int i = 0; i < FB; i++) {
    rem <<= 1;
    q <<= 1;
    if (rem >= b) { rem -= b; q |= 1; }
  }
  return q;
}

/* atanh(s) = s + s^3/3 + s^5/5 + ..., for 0 <= s < 1/2. */
static u128 fx_atanh(u128 s) {
  u128 s2 = fx_mul(s, s), term = s, sum = s;
  for (unsigned k = 3; term != 0; k += 2) {
    term = fx_mul(term, s2);
    sum += term / k;
  }
  return sum;
}

static u128 fx_ln2(void) { /* ln 2 = 2 atanh(1/3) */
  static u128 v;
  if (v == 0) v = 2 * fx_atanh(fx_div(FX_ONE, 3 * FX_ONE));
  return v;
}

/* mag / 2^fb as the nearest double (ties to even), mag != 0, result a normal double. */
static float64_t fx_to_f64(u128 mag, int fb, int neg) {
  int p = 127;
  while (!((mag >> p) & 1)) p--;
  int shift = p - 52;
  uint64_t mant;
  if (shift > 0) {
    u128 rem = mag & (((u128)1 << shift) - 1), half = (u128)1 << (shift - 1);
    mant = (uint64_t)(mag >> shift);
    if (rem > half || (rem == half && (mant & 1))) mant++;
  } else {
    mant = (uint64_t)(mag << -shift);
  }
  int e = p - fb + 1023;
  if (mant >> 53) { mant >>= 1; e++; }
  float64_t r;
  r.v = ((uint64_t)(neg != 0) << 63) | ((uint64_t)e << 52) | (mant & 0xFFFFFFFFFFFFFull);
  return r;
}

/* ln(x) of a positive normal double as the correctly rounded double (0 for x = 1). */
static float64_t ln_f64(float64_t x) {
  int e = (int)((x.v >> 52) & 0x7FF) - 1023;
  u128 m = (u128)((x.v & 0xFFFFFFFFFFFFFull) | (1ull << 52)) << (FB - 52); /* [1, 2) */
  if (m > FX_ONE + (FX_ONE >> 1) - (FX_ONE >> 3)) { /* m > 1.375: use m / 2, so m is in [0.6875, 1.375] */
    m >>= 1;
    e++;
  }
  u128 num, den = m + FX_ONE;
  int neg_t = m < FX_ONE;
  num = neg_t ? FX_ONE - m : m - FX_ONE;
  u128 t = 2 * fx_atanh(fx_div(num, den)); /* |ln m| */
  if (e == 0) {
    if (t == 0) { float64_t z; z.v = 0; return z; }
    return fx_to_f64(t, FB, neg_t);
  }
  /* e * ln2 + (+-t): Q112 so that |e| <= 1100 does not overflow */
  __int128 sum = (__int128)(e < 0 ? -e : e) * (__int128)(fx_ln2() >> (FB - 112));
  if (e < 0) sum = -sum;
  sum += neg_t ? -(__int128)(t >> (FB - 112)) : (__int128)(t >> (FB - 112));
  return fx_to_f64((u128)(sum < 0 ? -sum : sum), 112, sum < 0);
}

/* 2^|x| for 0 < |x| <= 1 (Q124, in [1, 2]) via exp(|x| ln 2). */
static u128 exp2_fx(float64_t x) {
  int ex = (int)((x.v >> 52) & 0x7FF);
  u128 mant = (x.v & 0xFFFFFFFFFFFFFull) | (ex ? 1ull << 52 : 0);
  int sh = FB - 52 + (ex - 1023); /* value = mant * 2^(ex - 1075) */
  u128 f = sh >= 0 ? mant << sh : (sh > -128 ? mant >> -sh : 0);
  u128 z = fx_mul(f, fx_ln2()), term = FX_ONE, sum = FX_ONE;
  for (unsigned k = 1; term != 0; k++) {
    term = fx_mul(term, z) / k;
    sum += term;
  }
  return sum;
}

static void fx_domain(const char *fn) {
  fprintf(stderr, "fp80: %s operand outside the modelled domain\n", fn);
  abort();
}

Fp80 fp80_fyl2x(Fp80 x, Fp80 y) {
  uint_fast8_t saved = softfloat_exceptionFlags, saved_rm = softfloat_roundingMode;
  float64_t dx = extF80_to_f64(x), dy = extF80_to_f64(y), l, ln2, r;
  if ((dx.v >> 63) || ((dx.v >> 52) & 0x7FF) == 0 || ((dx.v >> 52) & 0x7FF) == 0x7FF) fx_domain("FYL2X x");
  if (((dy.v >> 52) & 0x7FF) == 0x7FF) fx_domain("FYL2X y");
  softfloat_roundingMode = softfloat_round_near_even;
  {
    float64_t two;
    two.v = 0x4000000000000000ull;
    ln2 = ln_f64(two);
  }
  l = ln_f64(dx);
  r = f64_mul(f64_div(l, ln2), dy); /* log(x) / log(2.0) * y, as the Unicorn */
  softfloat_roundingMode = saved_rm;
  softfloat_exceptionFlags = saved;
  return f64_to_extF80(r);
}

Fp80 fp80_f2xm1(Fp80 x) {
  uint_fast8_t saved = softfloat_exceptionFlags, saved_rm = softfloat_roundingMode;
  float64_t dx = extF80_to_f64(x), p, one, r;
  int neg = (int)(dx.v >> 63);
  uint64_t ax = dx.v & 0x7FFFFFFFFFFFFFFFull;
  if (ax > 0x3FF0000000000000ull) fx_domain("F2XM1"); /* |x| > 1, NaN, inf */
  one.v = 0x3FF0000000000000ull;
  softfloat_roundingMode = softfloat_round_near_even;
  if (ax == 0) {
    r = f64_sub(one, one); /* pow(2.0, 0.0) - 1.0 */
  } else {
    float64_t a;
    a.v = ax;
    u128 e = exp2_fx(a);
    if (neg) e = fx_div(FX_ONE, e); /* 2^-|x|; 1/1 gives 1 - 2^-124, which rounds to 1.0 */
    p = fx_to_f64(e, FB, 0);
    r = f64_sub(p, one);
  }
  softfloat_roundingMode = saved_rm;
  softfloat_exceptionFlags = saved;
  return f64_to_extF80(r);
}
