/* Exact x87 arithmetic on 80-bit extended values (Berkeley SoftFloat 3e extF80).
 * The original C runtime runs the FPU with 64-bit precision control (control word
 * 0x037F or 0x1330, docs/CONVENTIONS.md "Floating point"), so a basic operation is rounded
 * to a 64-bit significand and only then to the 53-bit double the game stores
 * (FSTP m64). Plain C double does the second rounding only, and differs.
 *
 * The control word set by fp80_set_cw drives the precision (PC, bits 8-9) and the
 * rounding mode (RC, bits 10-11) of every operation below.
 *
 * Exceptions (owner decision: full runs use FPU_CW_DEFAULT = 0x1330 with IM, DM, ZM, OM
 * unmasked). Every operation raises the x87 exception flags it would raise (IE, DE, ZE,
 * OE, UE, PE; x87 status-word bit layout) into a sticky word read with fp80_exceptions().
 * The numeric result is ALWAYS the masked-response IEEE value (infinity, indefinite NaN,
 * denormal result). When a raised exception is UNMASKED in the control word the real CPU
 * traps (to the game's handler at 34:0x1db4) and the destination is not written; fp80
 * then calls the handler set by fp80_set_trap_handler (if any) with the unmasked bits,
 * and the caller must treat the result as invalid. The handler is where a harness stops
 * with a recorded error. There is no vectoring into the game's handler and no stack /
 * status-word state is rolled back (docs/CONVENTIONS.md "Floating point").
 */
#ifndef STARS_RUNTIME_FP80_H
#define STARS_RUNTIME_FP80_H

#include <stdint.h>

#include "softfloat.h"

typedef extFloat80_t Fp80;

#define FP80_CW_DEFAULT 0x037Fu /* FNINIT value: PC = 64-bit, RC = nearest, all masked */

/* x87 exception flag bits (status word layout). */
#define FP80_EXC_IE 0x01u /* invalid operation */
#define FP80_EXC_DE 0x02u /* denormal operand */
#define FP80_EXC_ZE 0x04u /* divide by zero */
#define FP80_EXC_OE 0x08u /* overflow */
#define FP80_EXC_UE 0x10u /* underflow */
#define FP80_EXC_PE 0x20u /* precision (inexact) */

typedef void (*Fp80TrapFn)(uint16_t unmasked);
void fp80_set_trap_handler(Fp80TrapFn fn); /* NULL = none (default) */
void fp80_clear_exceptions(void);
uint16_t fp80_exceptions(void); /* sticky flags raised since the last clear */

void fp80_set_cw(uint16_t cw);
uint16_t fp80_get_cw(void);

Fp80 fp80_from_f64(double d); /* FLD m64: exact */
Fp80 fp80_from_i32(int32_t v);
double fp80_to_f64(Fp80 x);   /* FSTP m64: rounds by RC */

Fp80 fp80_add(Fp80 a, Fp80 b); /* FADD, FSUB, FMUL, FDIV */
Fp80 fp80_sub(Fp80 a, Fp80 b);
Fp80 fp80_mul(Fp80 a, Fp80 b);
Fp80 fp80_div(Fp80 a, Fp80 b);

/* FSQRT: the correctly rounded square root, rounded by the precision control (PC) and rounding
 * control (RC) of the control word. A negative operand (not -0) raises IE and gives the real
 * indefinite. Proven against the Unicorn x87 by harness/tests/test_fp.c (test_sqrt). */
Fp80 fp80_sqrt(Fp80 a);

/* FISTP m16 / m32: round by RC; out of range or NaN gives the integer indefinite
 * (0x8000 / 0x80000000). */
int16_t fp80_to_i16(Fp80 x);
int32_t fp80_to_i32(Fp80 x);

/* ftol, the game's only runtime FP function (34:0x0de6, the C cast (long)d, 42 call sites):
 * FISTP m64 under RC = 11b (truncate toward zero), returning the low 32 bits (DX:AX).
 * Independent of the current control word. Values outside +-2^31 wrap (2^32 + 5 gives 5);
 * |d| >= 2^63 or NaN gives the 64-bit integer indefinite, whose low 32 bits are 0. */
int32_t fp80_to_i32_ftol(Fp80 x);

/* FYL2X: y * log2(x) and F2XM1: 2^x - 1, the two transcendental steps of the C runtime's pow
 * (34:0x0d84, crt_pow_34_0d84 in src/crt34.h). They follow the x87 of the harness' Unicorn 2.1.4
 * (QEMU 5.0), the oracle of the diff tests, which evaluates them in double arithmetic: FYL2X is
 * log(x) / log(2.0) * y and F2XM1 is pow(2.0, x) - 1.0, each rounded to a double and widened.
 * Here the correctly rounded double log and 2^x come from 128-bit fixed-point integer arithmetic
 * (no host libm), then the double division, multiplication and subtraction are SoftFloat f64
 * operations in round-to-nearest-even; the control word and the exception flags are not used.
 * The harness oracle runs these same helpers for FYL2X and F2XM1 (harness_hook_x87_transc, owner
 * decision 2026-10-20), so the oracle agrees by construction; the values are checked only against
 * the correctly rounded reference vectors (test_fp_vectors in harness/tests/test_fp.c), not against
 * a real x87 (64-bit significand; follow-up stars-5yb).
 * Domain: fp80_fyl2x needs x > 0 normal as a double and y finite (callers check); fp80_f2xm1
 * needs |x| <= 1 (the x87 domain). Outside it the helper prints a line and aborts. */
Fp80 fp80_fyl2x(Fp80 x, Fp80 y);
Fp80 fp80_f2xm1(Fp80 x);

#endif /* STARS_RUNTIME_FP80_H */
